from __future__ import annotations

from primitive_names import canonical_class_name

import argparse
import json
import shutil
from pathlib import Path

import numpy as np


# ====================================================================
# 基础 IO / 归一化 / 采样
# ====================================================================

def load_points(path: Path) -> np.ndarray:
    pts = []
    with path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.replace(",", " ").split()
            if len(parts) < 3:
                continue
            try:
                pts.append([float(parts[0]), float(parts[1]), float(parts[2])])
            except ValueError:
                continue
    if not pts:
        raise ValueError(f"No xyz points found in {path}")
    return np.asarray(pts, dtype=np.float32)


def normalize_points(points: np.ndarray) -> np.ndarray:
    points = points.astype(np.float32)
    center = points.mean(axis=0, keepdims=True)
    points = points - center
    radius = np.linalg.norm(points, axis=1).max()
    if radius > 1e-6:
        points = points / radius
    return points.astype(np.float32)


def fixed_sample(points: np.ndarray, num_points: int) -> np.ndarray:
    if len(points) == 0:
        raise ValueError("Cannot sample empty point cloud")
    replace = len(points) < num_points
    idx = np.random.choice(len(points), num_points, replace=replace)
    return points[idx].astype(np.float32)


# ====================================================================
# 底面删除（兜底，主逻辑已在 C++ 导出时通过法向跳过）
# ====================================================================

def drop_bottom_face(points: np.ndarray, height: float = 0.015) -> np.ndarray:
    """Remove points within `height` of Z-min (normalized space)."""
    if len(points) < 16:
        return points
    z_min = np.min(points[:, 2])
    mask = points[:, 2] > z_min + height
    kept = points[mask]
    return kept if len(kept) >= 16 else points


# ====================================================================
# 孔洞（模拟纹理不足/匹配失败导致的局部缺失）


def add_holes(points: np.ndarray, hole_prob: float = 0.15,
              max_holes: int = 2, hole_radius: float = 0.08) -> np.ndarray:
    """Simulate photogrammetry matching failures as 3D contiguous gaps.

    hole_prob: probability of having any holes at all
    max_holes: at most this many holes per cloud (usually 0, rarely 1-2)
    hole_radius: size of each missing region (in normalized space)
    """
    if len(points) < 64 or hole_prob <= 0:
        return points

    # Most models: no holes.  Some: 1 hole.  Very few: 2 holes.
    if np.random.rand() > hole_prob:
        return points

    n_holes = 1
    if max_holes >= 2 and np.random.rand() < 0.25:  # 25% of hole-bearing clouds get 2
        n_holes = 2

    kept = points
    for _ in range(n_holes):
        if len(kept) < 64:
            break
        center = kept[np.random.randint(len(kept))]
        radius = np.random.uniform(hole_radius * 0.7, hole_radius * 1.3)
        dist = np.linalg.norm(kept - center, axis=1)
        kept = kept[dist > radius]
        if len(kept) < 32:
            return points  # too destructive, abort

    return kept.astype(np.float32)


# ====================================================================
# 噪声 & 离群点
# ====================================================================

def add_noise(points: np.ndarray, sigma: float = 0.01) -> np.ndarray:
    noise = np.random.normal(0.0, sigma, size=points.shape).astype(np.float32)
    return (points + noise).astype(np.float32)


def add_outliers(points: np.ndarray, ratio: float = 0.01, extent: float = 0.08) -> np.ndarray:
    """Add sparse exterior clutter without placing false points inside geometry.

    Noise still jitters every point through add_noise().  This function is for a
    tiny number of false-positive points, so sample them from a thin shell just
    outside the normalized cloud bbox instead of from the origin.  The unit-radius
    clamp keeps metadata scale/center self-consistent.
    """
    count = int(round(len(points) * ratio))
    if count <= 0 or len(points) == 0:
        return points

    mn = points.min(axis=0)
    mx = points.max(axis=0)
    pad = max(float(extent), 0.01)

    outliers = []
    attempts = 0
    max_attempts = count * 80
    while len(outliers) < count and attempts < max_attempts:
        attempts += 1
        axis = np.random.randint(3)
        side = -1.0 if np.random.rand() < 0.5 else 1.0
        p = np.random.uniform(mn - pad, mx + pad).astype(np.float32)
        if side < 0:
            p[axis] = mn[axis] - np.random.uniform(pad * 0.35, pad)
        else:
            p[axis] = mx[axis] + np.random.uniform(pad * 0.35, pad)

        if np.linalg.norm(p) <= 1.0:
            outliers.append(p)

    if not outliers:
        return points
    return np.vstack([points, np.asarray(outliers, dtype=np.float32)]).astype(np.float32)


# ====================================================================
# 增强（简化版：遮挡已在 C++ 导出时内嵌）
# ====================================================================

def augment_points(points: np.ndarray, args) -> np.ndarray:
    """归一化 → drop_bottom → holes → noise → outliers → fixed_sample.

    增强在「自归一化坐标系」（mean=0, max|p|=1）里做 —— 这样 extent /
    hole_radius / bottom_height 这些常数才真是「占建筑跨度的比例」，符合直觉。

    做完必须映射回**导出的 metadata 坐标系**（p * r + m）。原因：
    输入点云虽然叫「归一化」，但它并不是以自身 mean/半径归一的 ——
    导出时的 center/scale 是对另一批点算的（实测该点云自身 mean_z=0.308、
    max半径=1.094，而 p*scale+center 能精确还原原始坐标）。
    如果在里面重新归一化就**不映射回去**，整朵云会平移+缩放 1.6–1.8 m，
    插件的 p*scale+center 反归一化跟着漂，左下角就对不上了。
    """
    m = points.mean(axis=0, keepdims=True)
    r = float(np.linalg.norm(points - m, axis=1).max())
    if r <= 1e-6:
        return fixed_sample(points.astype(np.float32), args.num_points)

    pts = ((points - m) / r).astype(np.float32)   # 进自归一化坐标系

    if args.drop_bottom:
        pts = drop_bottom_face(pts, args.bottom_height)

    if args.holes:
        pts = add_holes(pts, args.hole_prob, args.max_holes, args.hole_radius)

    if args.noise and args.noise_sigma > 0:
        pts = add_noise(pts, args.noise_sigma)
    if args.outliers and args.outlier_ratio > 0:
        pts = add_outliers(pts, args.outlier_ratio, args.outlier_extent)

    pts = (pts.astype(np.float32) * r + m).astype(np.float32)   # 回导出坐标系
    return fixed_sample(pts, args.num_points)


# ====================================================================
# 输出
# ====================================================================

def write_points(path: Path, points: np.ndarray):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        for p in points:
            f.write(f"{p[0]:.8f} {p[1]:.8f} {p[2]:.8f}\n")


def copy_metadata(input_root: Path, output_root: Path, args):
    src_meta = input_root / "metadata"
    legacy_meta = input_root.parent / "metadata"
    dst_meta = output_root / "metadata"
    dst_meta.mkdir(parents=True, exist_ok=True)

    if not src_meta.exists() and legacy_meta.exists():
        src_meta = legacy_meta

    if src_meta.exists():
        for item in src_meta.iterdir():
            if not item.is_file():
                continue
            shutil.copy2(item, dst_meta / item.name)

    config = {
        "input_root": str(input_root),
        "output_root": str(output_root),
        "num_points": args.num_points,
        "seed": args.seed,
        "drop_bottom": args.drop_bottom,
        "bottom_height": args.bottom_height,
        "noise": args.noise,
        "noise_sigma": args.noise_sigma,
        "outliers": args.outliers,
        "outlier_ratio": args.outlier_ratio,
        "outlier_extent": args.outlier_extent,
    }
    (dst_meta / "augment_config.json").write_text(
        json.dumps(config, ensure_ascii=False, indent=2), encoding="utf-8")


# ====================================================================
# 数据集遍历
# ====================================================================

def normalize_class_filters(classes: list[str] | None) -> set[str] | None:
    if not classes:
        return None
    selected = set()
    for item in classes:
        for name in item.split(","):
            name = name.strip()
            if name:
                selected.add(name)
    return selected or None


def remove_selected_output_dirs(output_root: Path, selected_classes: set[str]):
    for split in ["train", "val", "test"]:
        split_root = output_root / split
        if not split_root.exists():
            continue
        for class_name in selected_classes:
            target = split_root / class_name
            if target.exists():
                shutil.rmtree(target)


def augment_dataset(args):
    input_root = Path(args.input).resolve()
    output_root = Path(args.output).resolve()
    selected_classes = normalize_class_filters(args.classes)

    if not input_root.exists():
        raise FileNotFoundError(f"Input dataset does not exist: {input_root}")
    if output_root.exists() and not args.overwrite:
        raise FileExistsError(
            f"Output already exists: {output_root}. Use --overwrite to replace files.")
    if output_root.exists() and args.overwrite:
        if output_root == input_root:
            raise ValueError("Output and input must differ")
        if selected_classes:
            remove_selected_output_dirs(output_root, selected_classes)
        else:
            shutil.rmtree(output_root)

    np.random.seed(args.seed)
    total = 0
    failed = 0
    # 坐标系不变式检查：写出的点云必须还在输入点云的坐标系里。
    # 判据 = |bboxMin_out - bboxMin_in| / bbox 对角线长度。
    # 这个值一大，说明增强动了坐标系 —— 插件的 p*scale+center 反归一化会漂，
    # 对齐就落不到建筑上；metadata 的 bboxSize/center/scale 也会失效。
    drifts = []

    for split in ["train", "val", "test"]:
        split_root = input_root / split
        if not split_root.exists():
            continue
        for class_dir in sorted([p for p in split_root.iterdir() if p.is_dir()]):
            if class_dir.name == "TriPrismPyramid":
                continue
            if selected_classes and canonical_class_name(class_dir.name) not in {canonical_class_name(n) for n in selected_classes}:
                continue
            files = sorted(class_dir.glob("*.txt"))
            for src in files:
                rel = src.relative_to(input_root)
                dst = output_root / rel
                try:
                    points = load_points(src)
                    aug = augment_points(points, args)
                    write_points(dst, aug)
                    lo_in = points.min(axis=0)
                    hi_in = points.max(axis=0)
                    diag = float(np.linalg.norm(hi_in - lo_in)) or 1.0
                    drifts.append(float(np.linalg.norm(aug.min(axis=0) - lo_in)) / diag)
                    total += 1
                except Exception as exc:
                    failed += 1
                    print(f"FAILED {src}: {exc}")

    print(f"Augmentation done. success={total}, failed={failed}, output={output_root}")
    if drifts:
        d = np.asarray(drifts)
        print(f"  bbox-min frame drift (rel. to cloud diagonal): "
              f"median={np.median(d):.4f} p99={np.quantile(d, 0.99):.4f} max={d.max():.4f}")
        if d.max() > 0.05:
            print(f"  WARNING: {int((d > 0.05).sum())} cloud(s) drifted >5% — the export "
                  f"frame is not being preserved; the plugin's p*scale+center "
                  f"de-normalization will be off (lower --outlier_extent).")
    copy_metadata(input_root, output_root, args)


# ====================================================================
# CLI
# ====================================================================

def parse_args():
    parser = argparse.ArgumentParser(
        description="Lightweight augmentation: noise + outliers + fixed sampling. "
                    "Occlusion is baked in by the QGIS plugin during export.")

    parser.add_argument("--input", default="E:/pointnet/datasets")
    parser.add_argument("--output", default="E:/pointnet/datasets_aug")
    parser.add_argument("--num_points", type=int, default=2048)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--classes", nargs="+", default=None)

    parser.add_argument("--drop_bottom", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--bottom_height", type=float, default=0.015)

    parser.add_argument("--holes", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--hole_prob", type=float, default=0.15,
                        help="Probability a cloud gets any holes (most: none)")
    parser.add_argument("--max_holes", type=int, default=2)
    parser.add_argument("--hole_radius", type=float, default=0.08,
                        help="Hole size in normalized space (~8%% of building span)")

    parser.add_argument("--noise", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--noise_sigma", type=float, default=0.01)

    parser.add_argument("--outliers", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--outlier_ratio", type=float, default=0.01)
    parser.add_argument("--outlier_extent", type=float, default=0.08,
                        help="Half-width of the outlier cube in NORMALIZED space "
                             "(building max|p| == 1). Keep it a few %% of the "
                             "building span; 1.15 used to push outliers outside "
                             "the building's own radius and wreck alignment.")

    return parser.parse_args()


if __name__ == "__main__":
    augment_dataset(parse_args())
