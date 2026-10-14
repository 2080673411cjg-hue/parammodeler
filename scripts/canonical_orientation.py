"""
canonical_orientation.py — 建筑点云规范姿态估计模块

基于 RANSAC 线段 + 方向直方图 + PCA 门控，提取建筑的几何主方向。

Gate logic:
    PCA l1/l2 > 2.0  => PASS  (direction clear)
    PCA l1/l2 > 1.5  => WEAK (RANSAC extra check)
    PCA l1/l2 < 1.5  => FAIL (rotationally symmetric)
    Class in skip list => FAIL (curved surface, alignment not applicable)
"""

# 旋转对齐白名单：只有明确验证过的类别才启用旋转对齐
# 不在白名单中的类别默认跳过（安全第一）
_ROTATION_ENABLE_CLASSES = {
    "Cuboid",
    "IndentedCuboid",
    "GabledRoof",
    "PyramidRoof",
    "TruncatedPyramidRoof",
    "AsymmetricGableHouse",
    "HalfCylinderRoof",
}

_RIDGE_AXIS_CLASSES = {"GabledRoof", "AsymmetricGableHouse", "HalfCylinderRoof"}

import numpy as np
from scipy.spatial import ConvexHull
from scipy.ndimage import binary_fill_holes, binary_dilation, binary_erosion


def fold_axis_degrees(angle_deg: float) -> float:
    """Fold an unoriented axis angle into [-90, 90)."""
    return (float(angle_deg) + 90.0) % 180.0 - 90.0


def rotate_points_z(points: np.ndarray, angle_deg: float) -> np.ndarray:
    """Rotate xyz points around their XY centroid, preserving elevation."""
    points = np.asarray(points, dtype=np.float32)
    theta = np.deg2rad(float(angle_deg))
    c, s = np.cos(theta), np.sin(theta)
    rot = np.array([[c, -s], [s, c]], dtype=np.float32)
    out = points.copy()
    center = points[:, :2].mean(axis=0)
    out[:, :2] = (points[:, :2] - center) @ rot.T + center
    return out


def canonical_angle_from_metadata(record: dict, class_name: str) -> float | None:
    """Return the training-frame yaw from generated metadata, or None if unsupported."""
    if class_name not in _ROTATION_ENABLE_CLASSES:
        return None
    params = record.get("params", {})
    if "rz" not in params:
        raise ValueError(f"{record.get('file', class_name)}: canonical training requires params.rz")
    rz = float(params["rz"])
    if class_name in _RIDGE_AXIS_CLASSES:
        axis_offset = 0.0
    else:
        dim_keys = {
            "Cuboid": ("length", "width"),
            "IndentedCuboid": ("outerLength", "outerWidth"),
            "PyramidRoof": ("length", "width"),
            "TruncatedPyramidRoof": ("bottomLength", "bottomWidth"),
        }
        length_key, width_key = dim_keys[class_name]
        length = float(params[length_key])
        width = float(params[width_key])
        axis_offset = 90.0 if width > length else 0.0
    return fold_axis_degrees(rz + axis_offset)


def canonical_target_values(values: np.ndarray, targets: list[str], record: dict,
                            class_name: str, canonical_angle_deg: float) -> np.ndarray:
    """Transform directional labels into the same folded frame as the point cloud."""
    result = np.asarray(values, dtype=np.float32).copy()
    params = record.get("params", {})
    rz = float(params.get("rz", canonical_angle_deg))
    delta = np.deg2rad(rz - float(canonical_angle_deg))
    c, s = np.cos(delta), np.sin(delta)

    if class_name in {"GabledRoof", "AsymmetricGableHouse"} and "ridgeRatio" in targets:
        if c < -0.999:
            index = targets.index("ridgeRatio")
            result[index] = 1.0 - result[index]

    if class_name == "IndentedCuboid":
        side_values = {
            "left": float(params.get("leftMarginRatio", 0.2)),
            "right": float(params.get("rightMarginRatio", 0.4)),
            "front": float(params.get("frontMarginRatio", 0.2)),
            "back": float(params.get("backMarginRatio", 0.4)),
        }
        if abs(c) > 0.999:
            x_start, x_end = (("left", "right") if c > 0 else ("right", "left"))
            y_start, y_end = (("front", "back") if c > 0 else ("back", "front"))
        else:
            x_start, x_end = (("front", "back") if s < 0 else ("back", "front"))
            y_start, y_end = (("right", "left") if s < 0 else ("left", "right"))
        canonical_sides = {
            "geom_long_start_margin_ratio": side_values[x_start],
            "geom_long_end_margin_ratio": side_values[x_end],
            "geom_short_start_margin_ratio": side_values[y_start],
            "geom_short_end_margin_ratio": side_values[y_end],
        }
        for index, name in enumerate(targets):
            if name in canonical_sides:
                result[index] = canonical_sides[name]
    return result


def _top_axis(points_xyz: np.ndarray):
    """Estimate the roof ridge direction from the highest surface points."""
    z = points_xyz[:, 2]
    span = float(np.ptp(z))
    if span < 1e-6:
        return None
    for fraction in (0.96, 0.90, 0.82, 0.72):
        top = points_xyz[z >= float(z.min()) + span * fraction, :2]
        if len(top) < 12:
            continue
        centered = top - top.mean(axis=0)
        cov = centered.T @ centered / max(len(top) - 1, 1)
        vals, vecs = np.linalg.eigh(cov)
        vals, vecs = vals[::-1], vecs[:, ::-1]
        ratio = float(vals[0] / max(vals[1], 1e-9))
        angle = fold_axis_degrees(np.degrees(np.arctan2(vecs[1, 0], vecs[0, 0])))
        if ratio >= 1.35:
            return float(np.deg2rad(angle)), ratio, len(top)
    return None


# ============================================================================
# 1. Footprint 提取
# ============================================================================

def extract_footprint_boundary(
    xy: np.ndarray,
    grid_res: float = 0.03,
    occupy_thresh: int = 2,
    dilate_iter: int = 2,
    erode_iter: int = 1,
):
    """
    2D 网格法提取 footprint 边界点。

    参数:
        xy: (N, 2) XY 坐标
        grid_res: 网格分辨率
        occupy_thresh: 网格被判定为"被占用"的最小点数
        dilate_iter, erode_iter: 形态学操作迭代次数

    返回:
        boundary_pts: (M, 2) 边界点坐标
    """
    x_min, x_max = xy[:, 0].min(), xy[:, 0].max()
    y_min, y_max = xy[:, 1].min(), xy[:, 1].max()
    pad = grid_res * 2

    x_bins = int((x_max - x_min + 2 * pad) / grid_res) + 1
    y_bins = int((y_max - y_min + 2 * pad) / grid_res) + 1

    x_idx = np.clip(((xy[:, 0] - x_min + pad) / grid_res).astype(int), 0, x_bins - 1)
    y_idx = np.clip(((xy[:, 1] - y_min + pad) / grid_res).astype(int), 0, y_bins - 1)

    grid = np.zeros((y_bins, x_bins), dtype=int)
    for xi, yi in zip(x_idx, y_idx):
        grid[yi, xi] += 1

    occupied = grid >= occupy_thresh
    structure = np.ones((3, 3), dtype=bool)
    occupied = binary_dilation(occupied, structure=structure, iterations=dilate_iter)
    occupied = binary_fill_holes(occupied)
    occupied = binary_erosion(occupied, structure=structure, iterations=erode_iter)

    boundary_mask = binary_dilation(occupied, structure=structure, iterations=1) & ~occupied

    by, bx = np.where(boundary_mask)
    boundary_pts = np.column_stack([
        x_min - pad + bx * grid_res + grid_res / 2,
        y_min - pad + by * grid_res + grid_res / 2,
    ])

    return boundary_pts


# ============================================================================
# 2. RANSAC 线段迭代提取
# ============================================================================

def fit_line_ransac(
    points: np.ndarray,
    distance_threshold: float,
    min_inliers: int = 10,
    max_trials: int = 500,
):
    """
    对 2D 点集迭代 RANSAC 提取多条线段。

    参数:
        points: (N, 2)
        distance_threshold: 点到线的垂直距离阈值
        min_inliers: 最小内点数
        max_trials: 每次 RANSAC 的最大迭代次数

    返回:
        list[dict]: 每条线段 {angle_rad, length, ...}
    """
    n_points = len(points)
    if n_points < 2:
        return []

    segments = []
    mask_active = np.ones(n_points, dtype=bool)

    while mask_active.sum() >= min_inliers:
        active_pts = points[mask_active]
        active_idx = np.where(mask_active)[0]
        n_active = len(active_pts)

        best_inlier_mask_local = None
        best_count = 0
        best_line = None

        for _ in range(max_trials):
            i, j = np.random.choice(n_active, 2, replace=False)
            p1, p2 = active_pts[i], active_pts[j]
            vec = p2 - p1
            vec_len = np.linalg.norm(vec)
            if vec_len < 1e-9:
                continue

            vec_norm = vec / vec_len
            # 点到线的垂直距离
            dists = np.abs(
                (active_pts[:, 0] - p1[0]) * (-vec_norm[1])
                + (active_pts[:, 1] - p1[1]) * vec_norm[0]
            )
            inliers_local = dists <= distance_threshold
            n_inliers = inliers_local.sum()

            if n_inliers > best_count:
                best_count = n_inliers
                best_inlier_mask_local = inliers_local
                best_line = (p1, p2)

        if best_count < min_inliers:
            break

        # 最小二乘重拟合
        inlier_pts_local = active_pts[best_inlier_mask_local]
        center_local = inlier_pts_local.mean(axis=0)
        centered = inlier_pts_local - center_local
        _, _, vh = np.linalg.svd(centered, full_matrices=False)
        direction = vh[0]

        # 统一方向到 [-π/2, π/2)
        angle = np.arctan2(direction[1], direction[0])
        while angle >= np.pi / 2:
            angle -= np.pi
        while angle < -np.pi / 2:
            angle += np.pi
        direction = np.array([np.cos(angle), np.sin(angle)])

        # 端点
        proj = centered @ direction
        t_min, t_max = proj.min(), proj.max()
        length = float(t_max - t_min)

        segments.append({
            "angle_rad": float(angle),
            "length": length,
            "n_inliers": int(best_count),
        })

        # 移除内点
        original_indices = active_idx[best_inlier_mask_local]
        mask_active[original_indices] = False

    return segments


# ============================================================================
# 3. 方向直方图 + 峰值分析
# ============================================================================

def _build_histogram(segments, n_bins=72):
    """构建线段长度加权的方向直方图"""
    bin_edges = np.linspace(-np.pi / 2, np.pi / 2, n_bins + 1)
    bin_centers = (bin_edges[:-1] + bin_edges[1:]) / 2
    hist = np.zeros(n_bins)

    for seg in segments:
        idx = np.clip(np.digitize(seg["angle_rad"], bin_edges) - 1, 0, n_bins - 1)
        hist[idx] += seg["length"]

    # 高斯平滑
    sigma = 2.0
    ksize = int(4 * sigma) | 1
    x_k = np.arange(ksize) - ksize // 2
    kernel = np.exp(-0.5 * (x_k / sigma) ** 2)
    kernel /= kernel.sum()

    hist_padded = np.concatenate([hist[-ksize // 2:], hist, hist[:ksize // 2]])
    smooth = np.convolve(hist_padded, kernel, mode="same")[ksize // 2:ksize // 2 + n_bins]

    # 峰值检测
    peaks = []
    for i in range(1, n_bins - 1):
        if smooth[i] > smooth[i - 1] and smooth[i] > smooth[i + 1]:
            peaks.append(i)
    peaks.sort(key=lambda i: smooth[i], reverse=True)

    return {
        "hist": hist, "bin_centers": bin_centers, "smooth_hist": smooth,
        "peaks": peaks,
        "peak_angles": [float(bin_centers[i]) for i in peaks],
        "peak_weights": [float(smooth[i]) for i in peaks],
    }


def _analyze_histogram(histo):
    """从直方图提取主方向及各项指标"""
    smooth = histo["smooth_hist"]
    bin_centers = histo["bin_centers"]
    peaks = histo["peaks"]
    hist_raw = histo["hist"]

    if smooth.sum() < 1e-9 or not peaks:
        return {"angle_rad": 0.0, "angle_deg": 0.0,
                "peak_concentration": 0.0, "n_peaks": 0,
                "is_orthogonal": False, "circular_std_deg": 90.0}

    # 峰值集中度: 主峰 ±1 bin 权重 / 总权重
    pk_idx = peaks[0]
    pk_l = max(0, pk_idx - 1)
    pk_r = min(len(smooth) - 1, pk_idx + 1)
    peak_concentration = float(smooth[pk_l:pk_r + 1].sum() / max(smooth.sum(), 1e-9))

    primary_angle = float(bin_centers[peaks[0]])

    # 循环标准差
    if hist_raw.sum() > 1e-9:
        complex_weights = hist_raw * np.exp(1j * 2.0 * bin_centers)
        R = abs(complex_weights.sum()) / max(hist_raw.sum(), 1e-9)
        circular_std_deg = float(np.degrees(np.sqrt(-2.0 * np.log(max(R, 1e-9)))) / 2.0)
        circular_std_deg = min(circular_std_deg, 90.0)
    else:
        circular_std_deg = 90.0

    # 正交检测
    is_orthogonal = False
    if len(peaks) >= 2:
        for i in range(1, min(len(peaks), 6)):
            if abs(peaks[i] - pk_idx) <= 3:
                continue
            diff = abs(bin_centers[peaks[i]] - primary_angle)
            while diff > np.pi / 2:
                diff -= np.pi / 2
            if abs(abs(diff) - np.pi / 2) < np.deg2rad(15):
                if smooth[peaks[i]] > smooth[pk_idx] * 0.3:
                    is_orthogonal = True
                    break

    return {
        "angle_rad": primary_angle,
        "angle_deg": float(np.degrees(primary_angle)),
        "peak_concentration": peak_concentration,
        "n_peaks": len(peaks),
        "is_orthogonal": is_orthogonal,
        "circular_std_deg": circular_std_deg,
    }


# ============================================================================
# 4. PCA 方向 (门控用)
# ============================================================================

def _pca_direction(xy: np.ndarray):
    """PCA 提取点云主方向和 λ1/λ2 置信度"""
    centered = xy - xy.mean(axis=0)
    cov = np.cov(centered.T)
    eigenvalues, eigenvectors = np.linalg.eigh(cov)
    eigenvalues = eigenvalues[::-1]
    eigenvectors = eigenvectors[:, ::-1]

    principal_vec = eigenvectors[:, 0]
    angle = np.arctan2(principal_vec[1], principal_vec[0])
    while angle >= np.pi / 2:
        angle -= np.pi
    while angle < -np.pi / 2:
        angle += np.pi

    ratio = float(eigenvalues[0] / max(eigenvalues[1], 1e-9))
    return {"angle_rad": angle, "angle_deg": float(np.degrees(angle)),
            "ratio": ratio}


# ============================================================================
# 5. 主入口
# ============================================================================

def canonical_orientation(points_xyz: np.ndarray, class_name: str = None) -> dict:
    """
    计算建筑点云的规范姿态 (canonical orientation).

    输入:
        points_xyz: (N, 3) 点云 (可以是原始坐标或归一化后的)
        class_name: 建筑类别名 (如 "Cuboid", "Cylinder"), 用于类别门控

    返回:
        {
            "reliable": bool,                   # 是否可靠，可用于旋转对齐
            "verdict": "PASS" | "WEAK" | "FAIL",
            ...
        }
    """
    xy = points_xyz[:, :2].copy()

    # ---- Step 0: 类别白名单 ----
    if class_name is not None and class_name not in _ROTATION_ENABLE_CLASSES:
        return {
            "reliable": False,
            "verdict": "FAIL",
            "status": "SKIPPED_CLASS",
            "canonical_angle_rad": 0.0,
            "canonical_angle_deg": 0.0,
            "pca_ratio": 0.0,
            "peak_concentration": 0.0,
            "circular_std_deg": 90.0,
            "n_ransac_segments": 0,
            "aligned_points": points_xyz.copy(),
            "rotation_matrix": np.eye(3, dtype=np.float32),
            "method": "none",
            "reason": f"Class '{class_name}' not in rotation enable list",
        }

    # ---- Step 1: PCA 门控 ----
    pca = _pca_direction(xy)
    pca_ratio = pca["ratio"]

    result = {
        "reliable": False,
        "verdict": "FAIL",
        "status": "GEOMETRY_UNCERTAIN",
        "canonical_angle_rad": 0.0,
        "canonical_angle_deg": 0.0,
        "pca_ratio": pca_ratio,
        "peak_concentration": 0.0,
        "circular_std_deg": 90.0,
        "n_ransac_segments": 0,
        "aligned_points": points_xyz.copy(),
        "rotation_matrix": np.eye(3, dtype=np.float32),
        "method": "none",
        "reason": "",
    }

    if class_name in _RIDGE_AXIS_CLASSES:
        ridge = _top_axis(points_xyz)
        if ridge is None:
            result["reason"] = "Roof ridge direction could not be estimated from the upper surface"
            return result
        angle_rad, ridge_ratio, ridge_points = ridge
        theta = -angle_rad
        c, s = np.cos(theta), np.sin(theta)
        rot_mat = np.array([[c, -s, 0.0], [s, c, 0.0], [0.0, 0.0, 1.0]], dtype=np.float32)
        center_xy = xy.mean(axis=0)
        aligned = points_xyz.copy()
        aligned[:, :2] = aligned[:, :2] - center_xy
        aligned = aligned @ rot_mat.T
        aligned[:, :2] = aligned[:, :2] + center_xy
        result.update({
            "reliable": True,
            "verdict": "PASS" if ridge_ratio >= 2.0 else "WEAK",
            "status": "RIDGE_AXIS_OK",
            "canonical_angle_rad": angle_rad,
            "canonical_angle_deg": float(np.degrees(angle_rad)),
            "pca_ratio": ridge_ratio,
            "aligned_points": aligned,
            "rotation_matrix": rot_mat,
            "method": "upper_surface_pca",
            "reason": f"Upper-surface axis ratio={ridge_ratio:.2f}, points={ridge_points}",
        })
        return result

    if pca_ratio < 1.5:
        result["reason"] = f"PCA l1/l2={pca_ratio:.1f} < 1.5, rotationally symmetric"
        return result

    # ---- Step 2: Footprint + RANSAC ----
    boundary_pts = extract_footprint_boundary(xy)

    rng = max(np.ptp(xy[:, 0]), np.ptp(xy[:, 1]))
    ransac_threshold = max(rng * 0.015, 0.01)
    min_inliers = max(8, int(len(boundary_pts) * 0.05))

    segments_raw = fit_line_ransac(boundary_pts, ransac_threshold, min_inliers=min_inliers)

    # 过滤短线段 (剔除网格锯齿)
    ft_diag = rng * 1.5
    min_seg_len = ft_diag * 0.06
    segments = [s for s in segments_raw if s["length"] >= min_seg_len]

    result["n_ransac_segments"] = len(segments)

    if not segments:
        result["reason"] = f"No RANSAC segments survived filtering (PCA l1/l2={pca_ratio:.1f})"
        return result

    # ---- Step 3: 方向直方图 ----
    histo = _build_histogram(segments)
    analysis = _analyze_histogram(histo)
    peak_conc = analysis["peak_concentration"]
    n_peaks = analysis["n_peaks"]
    angle_rad = analysis["angle_rad"]

    result["peak_concentration"] = peak_conc
    result["circular_std_deg"] = analysis["circular_std_deg"]

    # ---- Step 4: 组合判定 ----
    if pca_ratio > 2.0:
        # PASS
        result["verdict"] = "PASS"
        result["status"] = "GEOMETRY_OK"
        result["reliable"] = True
        result["canonical_angle_rad"] = angle_rad
        result["canonical_angle_deg"] = analysis["angle_deg"]
        result["method"] = "ransac"
        result["reason"] = f"PCA l1/l2={pca_ratio:.1f} > 2.0, RANSAC angle={analysis['angle_deg']:.1f} deg"

    elif peak_conc > 0.30 and n_peaks <= 4:
        # WEAK but RANSAC concentrated enough
        result["verdict"] = "WEAK"
        result["status"] = "GEOMETRY_OK"
        result["reliable"] = True
        result["canonical_angle_rad"] = angle_rad
        result["canonical_angle_deg"] = analysis["angle_deg"]
        result["method"] = "ransac"
        result["reason"] = (f"PCA l1/l2={pca_ratio:.1f} (weak) but "
                            f"peak_conc={peak_conc:.2f}, peaks={n_peaks} -> ok")

    else:
        result["verdict"] = "WEAK"
        result["reason"] = (f"PCA l1/l2={pca_ratio:.1f} (weak) + "
                            f"peak_conc={peak_conc:.2f}, peaks={n_peaks} -> unreliable")

    # ---- Step 5: 应用旋转 ----
    if result["reliable"]:
        theta = -result["canonical_angle_rad"]  # 反向旋转使主方向 = X 轴
        cos_t = np.cos(theta)
        sin_t = np.sin(theta)
        rot_mat = np.array([
            [cos_t, -sin_t, 0.0],
            [sin_t,  cos_t, 0.0],
            [0.0,    0.0,   1.0],
        ], dtype=np.float32)

        # 绕点云中心旋转 (保持中心不变)
        center_xy = xy.mean(axis=0)
        aligned = points_xyz.copy()
        aligned[:, :2] = aligned[:, :2] - center_xy
        aligned = aligned @ rot_mat.T
        aligned[:, :2] = aligned[:, :2] + center_xy

        result["aligned_points"] = aligned
        result["rotation_matrix"] = rot_mat

    return result
