"""
canonical_orientation.py — 建筑点云规范姿态估计模块

按稳健外接矩形估计建筑主轴，屋顶用顶部点云判断屋脊对应的轴。
近正方形保留矩形方向并标记 WEAK；不适用的类别明确跳过。
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
    quarter_turn = int(np.rint(delta / (np.pi / 2))) % 4

    if class_name in {"GabledRoof", "AsymmetricGableHouse"} and "ridgeRatio" in targets:
        if quarter_turn == 2:
            index = targets.index("ridgeRatio")
            result[index] = 1.0 - result[index]

    if class_name == "IndentedCuboid":
        side_values = {
            "left": float(params.get("leftMarginRatio", 0.2)),
            "right": float(params.get("rightMarginRatio", 0.4)),
            "front": float(params.get("frontMarginRatio", 0.2)),
            "back": float(params.get("backMarginRatio", 0.4)),
        }
        x_start, x_end, y_start, y_end = (
            ("left", "right", "front", "back"),
            ("back", "front", "left", "right"),
            ("right", "left", "back", "front"),
            ("front", "back", "right", "left"),
        )[quarter_turn]
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



def _rectangle_axis(points_xyz: np.ndarray):
    """Fit a robust outer rectangle; PCA alone cannot orient a square footprint."""
    xy = np.asarray(points_xyz[:, :2], dtype=np.float64)
    if len(xy) > 4096:
        xy = xy[np.linspace(0, len(xy) - 1, 4096, dtype=int)]
    center = np.median(xy, axis=0)
    radius = np.linalg.norm(xy - center, axis=1)
    core = xy[radius <= np.quantile(radius, 0.995)]
    if len(core) < 4 or np.linalg.matrix_rank(core - core.mean(axis=0)) < 2:
        return None
    hull = core[ConvexHull(core).vertices]
    edges = np.roll(hull, -1, axis=0) - hull
    # Hull directions give exact noiseless edges; the grid handles noisy hulls.
    angles = np.unique(np.concatenate((np.mod(np.arctan2(edges[:, 1], edges[:, 0]), np.pi / 2),
                                       np.deg2rad(np.arange(0.0, 90.0, 0.5)))))
    c, s = np.cos(angles), np.sin(angles)
    x = xy[:, 0, None] * c + xy[:, 1, None] * s
    y = -xy[:, 0, None] * s + xy[:, 1, None] * c
    dx = np.diff(np.quantile(x, [0.01, 0.99], axis=0), axis=0)[0]
    dy = np.diff(np.quantile(y, [0.01, 0.99], axis=0), axis=0)[0]
    best = int(np.argmin(dx * dy))
    if min(dx[best], dy[best]) < 1e-8:
        return None
    angle = float(np.degrees(angles[best]))
    if dy[best] > dx[best]:
        angle += 90.0
    ratio = float(max(dx[best], dy[best]) / min(dx[best], dy[best]))
    return fold_axis_degrees(angle), ratio


def canonical_orientation(points_xyz: np.ndarray, class_name: str = None) -> dict:
    """Estimate the model X axis, returning a matching inverse point rotation."""
    result = {
        "reliable": False,
        "verdict": "FAIL",
        "status": "GEOMETRY_UNCERTAIN",
        "canonical_angle_rad": 0.0,
        "canonical_angle_deg": 0.0,
        "pca_ratio": 0.0,
        "peak_concentration": 0.0,
        "circular_std_deg": 90.0,
        "n_ransac_segments": 0,
        "aligned_points": points_xyz.copy(),
        "rotation_matrix": np.eye(3, dtype=np.float32),
        "method": "none",
        "reason": "",
    }
    if class_name is not None and class_name not in _ROTATION_ENABLE_CLASSES:
        result.update(status="SKIPPED_CLASS",
                      reason=f"Class '{class_name}' not in rotation enable list")
        return result

    rectangle = _rectangle_axis(points_xyz)
    if rectangle is None:
        result["reason"] = "Insufficient two-dimensional footprint"
        return result
    angle, ratio = rectangle
    verdict = "PASS" if ratio >= 1.08 else "WEAK"
    reason = f"Robust outer rectangle aspect={ratio:.3f}"
    if ratio < 1.08:
        reason += "; near-square footprint, interchangeable axes"
    if class_name in _RIDGE_AXIS_CLASSES:
        ridge = _top_axis(points_xyz)
        if ridge is None:
            result["reason"] = "Roof ridge direction could not be estimated from the upper surface"
            return result
        ridge_deg = float(np.degrees(ridge[0]))
        # Roof length follows the ridge, even if the house is wider than it is long.
        if abs(fold_axis_degrees(angle - ridge_deg)) > 45.0:
            angle = fold_axis_degrees(angle + 90.0)
        disagreement = abs(fold_axis_degrees(angle - ridge_deg))
        verdict = "PASS" if disagreement <= 10.0 else "WEAK"
        reason += f"; upper-surface axis disagreement={disagreement:.2f} deg"

    radians = float(np.deg2rad(angle))
    c, s = np.cos(radians), np.sin(radians)
    eigenvalues = np.linalg.eigvalsh(np.cov(points_xyz[:, :2].T))
    result.update({
        "reliable": True,
        "verdict": verdict,
        "status": "RIDGE_AXIS_OK" if class_name in _RIDGE_AXIS_CLASSES else "GEOMETRY_OK",
        "canonical_angle_rad": radians,
        "canonical_angle_deg": angle,
        "pca_ratio": float(eigenvalues[-1] / max(eigenvalues[0], 1e-9)),
        "aligned_points": rotate_points_z(points_xyz, -angle),
        "rotation_matrix": np.array([[c, s, 0.0], [-s, c, 0.0], [0.0, 0.0, 1.0]], dtype=np.float32),
        "method": "robust_rectangle",
        "reason": reason,
    })
    return result
