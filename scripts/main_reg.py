#!/usr/bin/env python
"""Standalone PTv3 parameter regression: train, test, and single-cloud predict.

Data/target/loss definitions are copied from the existing PCT v6 pipeline.
This directory is self-contained and does not import or modify pct_simple.
See PTv3_EXPERIMENT.md for Ubuntu setup and training commands.
"""

import argparse
import csv
import json
import multiprocessing
import os
import random
import time
from collections import deque
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, Dataset

from model_ptv3 import PTv3Regressor

# ── Canonical orientation (geometric prior) ─────────────────────────────
try:
    from canonical_orientation import (
        canonical_angle_from_metadata,
        canonical_orientation,
        canonical_target_values,
        fold_axis_degrees,
        rotate_points_z,
    )
    _HAS_CANONICAL = True
except ImportError:
    _HAS_CANONICAL = False

# ============================================================================
# Data loading (same as main_reg.py — kept here so this file is standalone)
# ============================================================================

def load_txt_points(path: Path) -> np.ndarray:
    points = []
    with path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.replace(",", " ").split()
            if len(parts) < 3:
                continue
            try:
                points.append([float(parts[0]), float(parts[1]), float(parts[2])])
            except ValueError:
                continue
    if not points:
        raise ValueError(f"No xyz points found in {path}")
    return np.asarray(points, dtype=np.float32)


def normalize_points(points: np.ndarray) -> np.ndarray:
    points = points.astype(np.float32)
    centroid = points.mean(axis=0, keepdims=True)
    points = points - centroid
    scale = np.linalg.norm(points, axis=1).max()
    if scale > 1e-6:
        points = points / scale
    return points.astype(np.float32)


def estimate_normals(points: np.ndarray, k: int = 16) -> np.ndarray:
    """Estimate per-point normals via kNN PCA (pure numpy, no scipy).

    Each point's normal is the smallest-eigenvalue eigenvector of the local
    covariance over its k nearest neighbors.  Normals are oriented outward
    relative to the cloud centroid so their signs stay consistent — this is
    essential for curvature-sensitive parameters like bulge / middleBulge.
    """
    points = np.asarray(points, dtype=np.float32)
    n = points.shape[0]
    if n < 4:
        return np.zeros((n, 3), dtype=np.float32)
    k = int(min(k, n - 1))
    if k < 3:
        k = 3

    diff = points[:, None, :] - points[None, :, :]          # (n, n, 3)
    dist2 = np.einsum('ijk,ijk->ij', diff, diff)            # (n, n)
    np.fill_diagonal(dist2, np.inf)                         # exclude self
    idx = np.argpartition(dist2, k, axis=1)[:, :k]          # (n, k) k nearest

    centroid = points.mean(axis=0)
    normals = np.zeros((n, 3), dtype=np.float32)
    for i in range(n):
        neigh = points[idx[i]]                              # (k, 3)
        centered = neigh - neigh.mean(axis=0)
        cov = centered.T @ centered                         # (3, 3)
        _, eigvecs = np.linalg.eigh(cov)                    # ascending eigenvalues
        normal = eigvecs[:, 0]                              # smallest → normal
        if float(np.dot(normal, points[i] - centroid)) < 0.0:
            normal = -normal
        normals[i] = normal
    return normals.astype(np.float32)


def duration_text(seconds):
    hours, remainder = divmod(int(seconds), 3600)
    minutes, seconds = divmod(remainder, 60)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}"


def init_normal_worker():
    torch.set_num_threads(1)


def precompute_normals(clouds, total, label, num_workers=0, normal_backend="numpy",
                       normal_threads=4, data_root=None, source_points=False):
    """Yield complete clouds in input order, with a bounded normal-task queue."""
    if num_workers < 0:
        raise ValueError("normal_workers must be non-negative")
    if normal_backend not in ("numpy", "pcl") or normal_threads < 1:
        raise ValueError("Unsupported normal backend or non-positive normal_threads")
    workers = min(num_workers, total) if num_workers > 1 else 0
    started = time.perf_counter()
    cache = None
    if normal_backend == "pcl":
        from pcl_normals import NormalCache, PCLNormals
        if data_root is None:
            raise ValueError("PCL disk cache requires data_root")
        cache = NormalCache(data_root, PCLNormals(), normal_threads)
        workers = 0  # PCL uses its own OpenMP threads, without an outer process pool.
        print(f"Normals {label}: PCL {cache.calculator.version} | threads={normal_threads} | "
              f"disk_cache={cache.root}", flush=True)
    print(f"Normals {label}: preparing {total} clouds | workers={workers}", flush=True)

    def results():
        if cache is not None:
            for path, points in clouds:
                base = points if source_points else normalize_points(load_txt_points(Path(path)))
                yield path, points, cache.get(path, base)
            return
        if workers < 2:
            for path, points in clouds:
                yield path, points, estimate_normals(points)
            return
        source = iter(clouds)
        with ProcessPoolExecutor(max_workers=workers, mp_context=multiprocessing.get_context("spawn"),
                                 initializer=init_normal_worker) as pool:
            pending = deque()

            def submit_next():
                item = next(source, None)
                if item is not None:
                    path, points = item
                    pending.append((path, points, pool.submit(estimate_normals, points)))

            for _ in range(workers * 2):
                submit_next()
            while pending:
                path, points, future = pending.popleft()
                normals = future.result()
                submit_next()
                yield path, points, normals

    last_report = started
    for index, item in enumerate(results(), start=1):
        now = time.perf_counter()
        if index == 1 or index % 50 == 0 or index == total or now - last_report >= 10:
            elapsed = now - started
            remaining = elapsed / index * (total - index)
            print(f"Normals {label}: {index}/{total} ({index / total:.1%}) | "
                  f"elapsed {duration_text(elapsed)} | ETA {duration_text(remaining)} | "
                  f"{index / max(elapsed, 1e-9):.2f} clouds/s", flush=True)
            last_report = now
        yield item
    elapsed = time.perf_counter() - started
    print(f"Normals {label} complete: {total} clouds | elapsed {duration_text(elapsed)} "
          f"({elapsed:.2f}s) | {total / max(elapsed, 1e-9):.2f} clouds/s", flush=True)
    if cache is not None:
        print(f"Normals {label} cache: hit={cache.hits} computed={cache.computed}", flush=True)


def normal_kwargs(args, checkpoint=None):
    # Existing checkpoints always keep their original NumPy features.
    backend = (checkpoint.get("normal_backend", "numpy") if checkpoint is not None
               else getattr(args, "normal_backend", "numpy"))
    if backend not in ("numpy", "pcl"):
        raise ValueError(f"Unsupported checkpoint normal backend: {backend}")
    return {"normal_backend": backend, "normal_threads": getattr(args, "normal_threads", 4)}


def predict_normals(source_points, prepared_points, args, checkpoint, angle_deg=0.0):
    if not checkpoint.get("use_normals", False):
        return None
    options = normal_kwargs(args, checkpoint)
    if options["normal_backend"] == "numpy":
        return estimate_normals(prepared_points)
    from pcl_normals import PCLNormals, rotate_normals_z
    normals, _, invalid = PCLNormals()(normalize_points(source_points), threads=options["normal_threads"])
    if invalid:
        raise ValueError(f"PCL produced {invalid} invalid normals for inference")
    return rotate_normals_z(normals, angle_deg) if angle_deg else normals


def sample_points(points: np.ndarray, num_points: int, train: bool,
                  jitter_sigma: float, random_rotate: bool,
                  normals: Optional[np.ndarray] = None) -> np.ndarray:
    n = len(points)
    replace = n < num_points
    if train:
        idx = np.random.choice(n, num_points, replace=replace)
    elif n == num_points:
        idx = np.arange(n)
    else:
        # Evaluation must not depend on previous batches or training RNG state.
        idx = np.random.default_rng(0).choice(n, num_points, replace=replace)
    sampled = points[idx].astype(np.float32)

    sampled_n = None
    if normals is not None:
        sampled_n = np.asarray(normals, dtype=np.float32)[idx]

    if train and random_rotate:
        theta = np.random.uniform(0, 2 * np.pi)
        rot = np.array([
            [np.cos(theta), -np.sin(theta), 0.0],
            [np.sin(theta),  np.cos(theta), 0.0],
            [0.0, 0.0, 1.0],
        ], dtype=np.float32)
        sampled = sampled @ rot.T
        if sampled_n is not None:
            sampled_n = sampled_n @ rot.T

    if train and jitter_sigma > 0:
        sampled = sampled + np.random.normal(0.0, jitter_sigma, sampled.shape).astype(np.float32)

    if sampled_n is not None:
        return np.concatenate([sampled, sampled_n], axis=1).astype(np.float32)
    return sampled.astype(np.float32)


def load_metadata(metadata_path: Path) -> List[dict]:
    data = json.loads(metadata_path.read_text(encoding="utf-8"))
    if not isinstance(data, list):
        raise ValueError(f"Metadata must be a JSON list: {metadata_path}")
    return data


def target_value(params: dict, name: str):
    if name in params:
        return float(params[name])
    outer_length = float(params.get("outerLength", 0.0))
    outer_width = float(params.get("outerWidth", 0.0))
    if name == "leftMarginRatio":
        if "innerMinXRatio" in params:
            return float(params["innerMinXRatio"])
        if "offsetX" in params and outer_length > 1e-6:
            return float(params["offsetX"]) / outer_length
    if name == "rightMarginRatio":
        if "innerMinXRatio" in params and "innerLengthRatio" in params:
            return max(0.0, 1.0 - float(params["innerMinXRatio"]) - float(params["innerLengthRatio"]))
        if {"offsetX", "innerLength"}.issubset(params) and outer_length > 1e-6:
            return max(0.0, (outer_length - float(params["offsetX"]) - float(params["innerLength"])) / outer_length)
    if name == "frontMarginRatio":
        if "innerMinYRatio" in params:
            return float(params["innerMinYRatio"])
        if "offsetY" in params and outer_width > 1e-6:
            return float(params["offsetY"]) / outer_width
    if name == "backMarginRatio":
        if "innerMinYRatio" in params and "innerWidthRatio" in params:
            return max(0.0, 1.0 - float(params["innerMinYRatio"]) - float(params["innerWidthRatio"]))
        if {"offsetY", "innerWidth"}.issubset(params) and outer_width > 1e-6:
            return max(0.0, (outer_width - float(params["offsetY"]) - float(params["innerWidth"])) / outer_width)
    return None


def target_vector(record: dict, targets: List[str]) -> np.ndarray:
    params = record.get("params", {})
    geometry = None
    values = []
    missing = []
    for name in targets:
        value = target_value(params, name)
        if value is None and name.startswith("geom_"):
            if geometry is None:
                geometry = primitive_geometry_summary(record)
            value = geometry.get(name)
        if value is None:
            missing.append(name)
        else:
            values.append(value)
    if missing:
        raise KeyError(f"Missing target(s) {missing} in {record.get('file')}")
    return np.asarray(values, dtype=np.float32)


def target_mode_is_bbox_residual(target_mode: str) -> bool:
    return target_mode == "bbox_residual"


def target_baseline_from_aux_values(targets: List[str], aux_names: List[str],
                                    aux_values: np.ndarray, target_mode: str) -> np.ndarray:
    if not target_mode_is_bbox_residual(target_mode):
        return np.zeros((len(targets),), dtype=np.float32)
    aux_index = {name: i for i, name in enumerate(aux_names)}

    def aux_value(name: str):
        if name not in aux_index:
            return None
        return float(aux_values[aux_index[name]])

    values = []
    missing = []
    for target in targets:
        bbox_x = aux_value("bbox_x")
        bbox_y = aux_value("bbox_y")
        bbox_z = aux_value("bbox_z")
        if target in ("length", "outerLength", "bottomLength", "bbox_x"):
            value = bbox_x
        elif target in ("width", "outerWidth", "bottomWidth", "bbox_y"):
            value = bbox_y
        elif target in ("height", "outerHeight", "totalHeight", "geom_total_height", "bbox_z"):
            value = bbox_z
        elif target == "geom_footprint_long" and bbox_x is not None and bbox_y is not None:
            value = max(bbox_x, bbox_y)
        elif target == "geom_footprint_short" and bbox_x is not None and bbox_y is not None:
            value = min(bbox_x, bbox_y)
        else:
            value = 0.0
        if value is None:
            missing.append(target)
        else:
            values.append(value)
    if missing:
        raise KeyError(
            "bbox_residual target_mode requires bbox_x/bbox_y/bbox_z auxiliary features; "
            f"missing baseline for target(s): {missing}"
        )
    return np.asarray(values, dtype=np.float32)


def target_baseline_from_aux_tensor(targets: List[str], aux_names: List[str],
                                    aux_tensor: torch.Tensor, target_mode: str) -> torch.Tensor:
    if not target_mode_is_bbox_residual(target_mode):
        return torch.zeros((aux_tensor.shape[0], len(targets)), dtype=aux_tensor.dtype, device=aux_tensor.device)
    aux_index = {name: i for i, name in enumerate(aux_names)}

    def aux_col(name: str):
        if name not in aux_index:
            return None
        return aux_tensor[:, aux_index[name]]

    cols = []
    missing = []
    for target in targets:
        bbox_x = aux_col("bbox_x")
        bbox_y = aux_col("bbox_y")
        bbox_z = aux_col("bbox_z")
        if target in ("length", "outerLength", "bottomLength", "bbox_x"):
            col = bbox_x
        elif target in ("width", "outerWidth", "bottomWidth", "bbox_y"):
            col = bbox_y
        elif target in ("height", "outerHeight", "totalHeight", "geom_total_height", "bbox_z"):
            col = bbox_z
        elif target == "geom_footprint_long" and bbox_x is not None and bbox_y is not None:
            col = torch.maximum(bbox_x, bbox_y)
        elif target == "geom_footprint_short" and bbox_x is not None and bbox_y is not None:
            col = torch.minimum(bbox_x, bbox_y)
        else:
            col = torch.zeros((aux_tensor.shape[0],), dtype=aux_tensor.dtype, device=aux_tensor.device)
        if col is None:
            missing.append(target)
        else:
            cols.append(col)
    if missing:
        raise KeyError(
            "bbox_residual target_mode requires bbox_x/bbox_y/bbox_z auxiliary features; "
            f"missing baseline for target(s): {missing}"
        )
    return torch.stack(cols, dim=1)


def apply_target_transform(raw_targets: np.ndarray, baseline: np.ndarray, target_mode: str) -> np.ndarray:
    if target_mode_is_bbox_residual(target_mode):
        return (raw_targets - baseline).astype(np.float32)
    return raw_targets.astype(np.float32)


def invert_target_transform_values(values: np.ndarray, baseline: np.ndarray, target_mode: str) -> np.ndarray:
    if target_mode_is_bbox_residual(target_mode):
        return (values + baseline).astype(np.float32)
    return values.astype(np.float32)


def invert_target_transform_tensor(values: torch.Tensor, baseline: torch.Tensor, target_mode: str) -> torch.Tensor:
    if target_mode_is_bbox_residual(target_mode):
        return values + baseline
    return values


def prediction_params_for_plugin(class_name: str, targets: List[str], values: np.ndarray) -> Dict[str, float]:
    params = {name: float(value) for name, value in zip(targets, values)}

    def put(source: str, dest: str):
        if source in params:
            params[dest] = params[source]

    if class_name == "Cuboid":
        put("geom_footprint_long", "length")
        put("geom_footprint_short", "width")
        put("geom_total_height", "height")
    elif class_name == "PyramidRoof":
        put("geom_footprint_long", "length")
        put("geom_footprint_short", "width")
        put("geom_total_height", "totalHeight")
    elif class_name == "TruncatedPyramidRoof":
        put("geom_footprint_long", "bottomLength")
        put("geom_footprint_short", "bottomWidth")
        put("geom_top_long_ratio", "topLengthRatio")
        put("geom_top_short_ratio", "topWidthRatio")
        put("geom_total_height", "totalHeight")

    elif class_name == "IndentedCuboid":
        put("geom_footprint_long", "outerLength")
        put("geom_footprint_short", "outerWidth")
        put("geom_total_height", "outerHeight")
        put("geom_long_start_margin_ratio", "leftMarginRatio")
        put("geom_long_end_margin_ratio", "rightMarginRatio")
        put("geom_short_start_margin_ratio", "frontMarginRatio")
        put("geom_short_end_margin_ratio", "backMarginRatio")
    return params


def aux_vector(record: dict, aux_names: List[str]) -> np.ndarray:
    if not aux_names:
        return np.zeros((0,), dtype=np.float32)
    info = record.get("pointCloudInfo", {})
    bbox_size = info.get("bboxSize", [0.0, 0.0, 0.0])
    scale = float(info.get("scale", 1.0))
    if scale <= 1e-6:
        scale = 1.0
    values = []
    for name in aux_names:
        if name == "bbox_x":
            values.append(float(bbox_size[0]))
        elif name == "bbox_y":
            values.append(float(bbox_size[1]))
        elif name == "bbox_z":
            values.append(float(bbox_size[2]))
        elif name == "bbox_x_norm":
            values.append(float(bbox_size[0]) / scale)
        elif name == "bbox_y_norm":
            values.append(float(bbox_size[1]) / scale)
        elif name == "bbox_z_norm":
            values.append(float(bbox_size[2]) / scale)
        elif name == "scale":
            values.append(scale)
        elif name == "bbox_volume":
            values.append(float(bbox_size[0]) * float(bbox_size[1]) * float(bbox_size[2]))
        elif name == "aspect_ratio_xy":
            w = max(float(bbox_size[0]), 1e-6)
            values.append(float(bbox_size[1]) / w)
        elif name == "bbox_diagonal":
            values.append(float(np.sqrt(
                float(bbox_size[0])**2 + float(bbox_size[1])**2 + float(bbox_size[2])**2
            )))
        else:
            raise KeyError(f"Unsupported aux feature: {name}")
    return np.asarray(values, dtype=np.float32)


def aux_vector_from_args(args, aux_names: List[str]):
    if not aux_names:
        return np.zeros((0,), dtype=np.float32)
    if args.bbox_x is None or args.bbox_y is None or args.bbox_z is None or args.scale is None:
        return None
    record = {"pointCloudInfo": {"bboxSize": [args.bbox_x, args.bbox_y, args.bbox_z], "scale": args.scale}}
    return aux_vector(record, aux_names)


def aux_vector_for_bbox(aux_values: np.ndarray, aux_names: List[str], bbox_size: np.ndarray) -> np.ndarray:
    if not aux_names:
        return np.zeros((0,), dtype=np.float32)
    scale = float(aux_values[aux_names.index("scale")]) if "scale" in aux_names else 1.0
    record = {"pointCloudInfo": {"bboxSize": np.asarray(bbox_size, dtype=np.float32).tolist(),
                                 "scale": scale}}
    return aux_vector(record, aux_names)

LHOUSE_ROBUST_BBOX_MODE = "lhouse_xy_quantile_005_995_v1"


def validate_canonical_bbox_mode(mode, class_name, canonical_align, lhouse_canonical, aux_units):
    if mode not in ("full", LHOUSE_ROBUST_BBOX_MODE):
        raise ValueError(f"Unsupported canonical bbox mode: {mode}")
    if mode != "full" and (class_name != "LHouse" or not canonical_align
                            or not lhouse_canonical or aux_units != "physical"):
        raise ValueError("Robust bbox requires canonical LHouse with physical auxiliary units")


def physical_bbox_size(points: np.ndarray, input_coordinates: str, scale=None, bbox_mode="full") -> np.ndarray:
    """Convert a canonical point span to scene units before deriving auxiliary features."""
    span = np.ptp(points, axis=0).astype(np.float32)
    if bbox_mode == LHOUSE_ROBUST_BBOX_MODE:
        bounds = np.quantile(points[:, :2], [0.005, 0.995], axis=0)
        span[:2] = bounds[1] - bounds[0]
    elif bbox_mode != "full":
        raise ValueError(f"Unsupported canonical bbox mode: {bbox_mode}")
    if input_coordinates == "normalized":
        if scale is None or not np.isfinite(scale) or scale <= 0:
            raise ValueError("Normalized input requires a finite positive physical scale")
        span = span * float(scale)
    elif input_coordinates != "physical":
        raise ValueError("Declare input_coordinates as physical or normalized for physical auxiliary features")
    return span


def primitive_geometry_summary(record: dict) -> Dict[str, float]:
    """Geometry labels used only as auxiliary supervision for regression training."""
    params = record.get("params", {})
    prim = record.get("type", "")
    info = record.get("pointCloudInfo", {})
    bbox = info.get("bboxSize", [0.0, 0.0, 0.0])
    bbox_x, bbox_y, bbox_z = float(bbox[0]), float(bbox[1]), float(bbox[2])

    def p(name, default=0.0):
        return float(params.get(name, default))

    length = max(bbox_x, bbox_y)
    width = min(bbox_x, bbox_y)
    height = bbox_z

    if prim == "Cuboid":
        length, width, height = p("length"), p("width"), p("height")
    elif prim == "Cylinder":
        length = width = 2.0 * p("radius")
        height = p("height")
    elif prim == "LHouse":
        length = p("outerLength", p("totalLength", length))
        width = p("outerWidth", p("totalWidth", width))
        height = p("height", height)
    elif prim == "ConeCylinder":
        length = width = 2.0 * p("radius")
        height = p("totalHeight", p("cylHeight") + p("coneHeight"))
    elif prim in ("GabledRoof", "PyramidRoof", "AsymmetricGableHouse"):
        length, width = p("length", length), p("width", width)
        height = p("totalHeight", p("wallHeight") + p("roofHeight"))
    elif prim == "TruncatedPyramidRoof":
        length, width = p("bottomLength", length), p("bottomWidth", width)
        height = p("totalHeight", p("wallHeight") + p("roofHeight"))
    elif prim == "HalfCylinderRoof":
        length, width = p("length", length), p("width", width)
        height = p("wallHeight", height) + 0.5 * p("width", width)
    elif prim == "CylinderDome":
        length = width = 2.0 * p("radius")
        height = p("totalHeight", p("cylHeight") + p("domeHeight"))
    elif prim == "IndentedCuboid":
        length, width, height = p("outerLength", length), p("outerWidth", width), p("outerHeight", height)
    elif prim == "FourStageRoundTower":
        length = width = 2.0 * p("baseRadius")
        height = p("baseHeight") + p("middleHeight") + p("coneHeight")
    elif prim == "TwoGableHouses":
        length = p("length1", 0.0) + p("length2", 0.0)
        width = p("width", width)
        height = p("totalHeight", p("wallHeight") + p("roofHeight"))

    length = max(float(length), 1e-6)
    width = max(float(width), 1e-6)
    height = max(float(height), 1e-6)
    summary = {
        "geom_footprint_long": max(length, width),
        "geom_footprint_short": min(length, width),
        "geom_total_height": height,
        "geom_aspect_xy": min(length, width) / max(max(length, width), 1e-6),
        "geom_volume_proxy": length * width * height,
        "geom_bbox_x": bbox_x,
        "geom_bbox_y": bbox_y,
        "geom_bbox_z": bbox_z,
    }
    if prim == "TruncatedPyramidRoof":
        if length >= width:
            summary["geom_top_long_ratio"] = p("topLengthRatio", 0.6)
            summary["geom_top_short_ratio"] = p("topWidthRatio", 0.6)
        else:
            summary["geom_top_long_ratio"] = p("topWidthRatio", 0.6)
            summary["geom_top_short_ratio"] = p("topLengthRatio", 0.6)
    if prim == "IndentedCuboid":
        if length >= width:
            summary["geom_long_start_margin_ratio"] = p("leftMarginRatio", 0.2)
            summary["geom_long_end_margin_ratio"] = p("rightMarginRatio", 0.4)
            summary["geom_short_start_margin_ratio"] = p("frontMarginRatio", 0.2)
            summary["geom_short_end_margin_ratio"] = p("backMarginRatio", 0.4)
        else:
            summary["geom_long_start_margin_ratio"] = p("frontMarginRatio", 0.2)
            summary["geom_long_end_margin_ratio"] = p("backMarginRatio", 0.4)
            summary["geom_short_start_margin_ratio"] = p("leftMarginRatio", 0.2)
            summary["geom_short_end_margin_ratio"] = p("rightMarginRatio", 0.4)
    return summary


def geometry_aux_vector(record: dict, geom_names: List[str]) -> np.ndarray:
    if not geom_names:
        return np.zeros((0,), dtype=np.float32)
    summary = primitive_geometry_summary(record)
    missing = [name for name in geom_names if name not in summary]
    if missing:
        raise KeyError(f"Unsupported geometry auxiliary target(s): {missing}")
    return np.asarray([float(summary[name]) for name in geom_names], dtype=np.float32)


def find_metadata_record(records: List[dict], data_root: str, input_path: str):
    target = Path(input_path).resolve()
    root = Path(data_root).resolve()
    for rec in records:
        rel = str(rec.get("file", "")).replace("\\", "/")
        candidate = (root / rel).resolve()
        if candidate == target:
            return rec
    return None


# ============================================================================
# Dataset
# ============================================================================

class ParamRegressionDataset(Dataset):
    def __init__(self, data_root, metadata_path, class_name, split,
                 targets, num_points, train=False, normalize=True,
                 jitter_sigma=0.0, random_rotate=False, aux_names=None,
                 use_normals=False, geom_aux_targets=None, target_mode="raw",
                 canonical_align=False, orientation_source="metadata", lhouse_canonical=False,
                 canonical_aux_units="legacy", canonical_bbox_mode="full", cache_points=True,
                 normal_workers=0, normal_backend="numpy", normal_threads=4):
        self.data_root = Path(data_root)
        self.metadata_path = Path(metadata_path)
        self.class_name = class_name
        self.split = split
        self.targets = targets
        self.num_points = num_points
        self.train = train
        self.normalize = normalize
        self.jitter_sigma = jitter_sigma
        self.random_rotate = random_rotate
        self.aux_names = aux_names or []
        self.use_normals = use_normals
        self.geom_aux_targets = geom_aux_targets or []
        self.target_mode = target_mode
        self.canonical_align = bool(canonical_align)
        self.lhouse_canonical = bool(lhouse_canonical)
        self.canonical_aux_units = canonical_aux_units
        self.canonical_bbox_mode = canonical_bbox_mode
        self.cache_points = bool(cache_points)
        self._points_cache: Dict[str, np.ndarray] = {}
        if canonical_aux_units not in ("legacy", "physical"):
            raise ValueError(f"Unsupported canonical auxiliary units: {canonical_aux_units}")
        validate_canonical_bbox_mode(canonical_bbox_mode, class_name, self.canonical_align,
                                     self.lhouse_canonical, canonical_aux_units)
        self.orientation_diagnostics = []
        self.geom_values: List[np.ndarray] = []

        if self.canonical_align and not _HAS_CANONICAL:
            raise RuntimeError("Canonical training requires canonical_orientation.py beside main_reg.py")
        if self.canonical_align and self.random_rotate:
            raise ValueError("--random_rotate cannot be combined with --canonical_align")

        records = load_metadata(self.metadata_path)
        self.samples: List[Tuple[Path, np.ndarray, np.ndarray, dict, float]] = []
        for rec in records:
            if rec.get("type") != class_name or rec.get("split") != split:
                continue
            rel = str(rec.get("file", "")).replace("\\", "/")
            path = self.data_root / rel
            if not path.exists():
                raise FileNotFoundError(f"Metadata points to missing point cloud: {path}")
            theta = 0.0
            original_points = None
            canonical_rec = rec
            if self.canonical_align:
                theta_value = canonical_angle_from_metadata(rec, class_name, self.lhouse_canonical)
                if theta_value is not None:
                    theta = float(theta_value)
                    original_points = load_txt_points(path)
                    if orientation_source == "geometry":
                        orientation = canonical_orientation(original_points, class_name, self.lhouse_canonical)
                        diagnostic = {"file": rel, "applied": orientation["reliable"],
                                      "reason": orientation["reason"]}
                        if not orientation["reliable"]:
                            self.orientation_diagnostics.append(diagnostic)
                            continue
                        theta = float(orientation["canonical_angle_deg"])
                        diagnostic["axis_error_deg"] = (abs((theta - theta_value + 180.0) % 360.0 - 180.0)
                                                        if self.lhouse_canonical
                                                        else abs(fold_axis_degrees(theta - theta_value)))
                        self.orientation_diagnostics.append(diagnostic)
                    points = rotate_points_z(original_points, -theta)
                    info = dict(rec.get("pointCloudInfo", {}))
                    if self.canonical_aux_units == "physical":
                        coordinates = ("normalized" if info.get("normalization") ==
                                       "centered_by_mean_and_scaled_by_max_radius" else "physical")
                        bbox = physical_bbox_size(points, coordinates, info.get("scale"), self.canonical_bbox_mode)
                    else:
                        bbox = np.ptp(points, axis=0)
                    info["bboxSize"] = bbox.astype(float).tolist()
                    canonical_rec = dict(rec)
                    canonical_rec["pointCloudInfo"] = info
            aux = aux_vector(canonical_rec, self.aux_names)
            raw_target = target_vector(rec, targets)
            if self.canonical_align and canonical_angle_from_metadata(rec, class_name, self.lhouse_canonical) is not None:
                raw_target = canonical_target_values(raw_target, targets, rec, class_name, theta)
            baseline = target_baseline_from_aux_values(targets, self.aux_names, aux, self.target_mode)
            target = apply_target_transform(raw_target, baseline, self.target_mode)
            self.samples.append((path, target, aux, canonical_rec, theta))
            self.geom_values.append(geometry_aux_vector(canonical_rec, self.geom_aux_targets))
            if self.cache_points:
                fixed_points = self._prepare_points(path, theta, original_points)
                fixed_points.setflags(write=False)
                self._points_cache[str(path)] = fixed_points
        if not self.samples and not self.orientation_diagnostics:
            raise RuntimeError(f"No samples for class={class_name}, split={split} in {metadata_path}")
        if self.cache_points:
            size_mib = sum(points.nbytes for points in self._points_cache.values()) / (1024 * 1024)
            print(f"Cached points {class_name}/{split}: {len(self._points_cache)} clouds ({size_mib:.1f} MiB)", flush=True)

        # Precompute per-point normals once (kNN PCA is the bottleneck; caching
        # keeps every epoch fast instead of re-estimating per sample).
        self._normals_cache: Dict[str, np.ndarray] = {}
        if self.use_normals:
            def clouds():
                for path, _, _, _, theta in self.samples:
                    pts = self._points_cache.get(str(path))
                    if pts is None:
                        pts = self._prepare_points(path, theta)
                    yield path, pts

            prepared = precompute_normals(
                    clouds(), len(self.samples), f"{class_name}/{split}", normal_workers,
                    normal_backend=normal_backend, normal_threads=normal_threads, data_root=self.data_root)
            for (path, _, normals), (_, _, _, _, theta) in zip(prepared, self.samples):
                if normal_backend == "pcl" and self.canonical_align:
                    from pcl_normals import rotate_normals_z
                    # Normals are directions: apply rotation only, never centroid translation.
                    if theta:
                        normals = rotate_normals_z(normals, -theta)
                normals.setflags(write=False)
                self._normals_cache[str(path)] = normals

    def __len__(self):
        return len(self.samples)

    def _prepare_points(self, path, theta, points=None):
        if points is None:
            points = load_txt_points(path)
        if self.canonical_align and theta:
            points = rotate_points_z(points, -theta)
        if self.normalize:
            points = normalize_points(points)
        return points

    def __getitem__(self, index):
        path, target, aux, _, theta = self.samples[index]
        points = self._points_cache.get(str(path))
        if points is None:
            points = self._prepare_points(path, theta)
        normals = self._normals_cache.get(str(path)) if self.use_normals else None
        points = sample_points(points, self.num_points, self.train,
                               self.jitter_sigma, self.random_rotate, normals)
        geom = self.geom_values[index] if self.geom_aux_targets else np.zeros((0,), dtype=np.float32)
        return torch.from_numpy(points), torch.from_numpy(aux), torch.from_numpy(target), torch.from_numpy(geom), str(path)


def seed_data_worker(_worker_id):
    seed = torch.initial_seed() % (2 ** 32)
    np.random.seed(seed)
    random.seed(seed)


def make_data_loader(dataset, batch_size, shuffle, device, num_workers=0, seed=None):
    options = {"num_workers": num_workers, "pin_memory": device.type == "cuda"}
    if num_workers > 0:
        options.update(persistent_workers=True, prefetch_factor=2,
                       multiprocessing_context="spawn", worker_init_fn=seed_data_worker)
        if seed is not None:
            options["generator"] = torch.Generator().manual_seed(seed)
    return DataLoader(dataset, batch_size=batch_size, shuffle=shuffle, **options)


def data_loading_config(args):
    workers = getattr(args, "num_workers", 0)
    return {"cache_points": getattr(args, "cache_points", True), "num_workers": workers,
            "normal_workers": getattr(args, "normal_workers", 0),
            "normal_backend": getattr(args, "normal_backend", "numpy"),
            "normal_threads": getattr(args, "normal_threads", 4),
            "persistent_workers": workers > 0,
            "worker_seed_mode": "torch_initial_seed_numpy_python_v1" if workers else "main_process"}


# ============================================================================
# Stats & normalization
# ============================================================================

def compute_target_stats(dataset):
    y = np.stack([target for _, target, _, _, _ in dataset.samples], axis=0).astype(np.float32)
    mean = y.mean(axis=0)
    std = y.std(axis=0)
    std = np.maximum(std, 1e-6)
    return mean, std


def compute_aux_stats(dataset, aux_dim):
    if aux_dim == 0:
        return np.zeros((0,), dtype=np.float32), np.ones((0,), dtype=np.float32)
    aux = np.stack([a for _, _, a, _, _ in dataset.samples], axis=0).astype(np.float32)
    mean = aux.mean(axis=0)
    std = aux.std(axis=0)
    std = np.maximum(std, 1e-6)
    return mean, std

def compute_geometry_aux_stats(dataset):
    if not getattr(dataset, "geom_aux_targets", []):
        return np.zeros((0,), dtype=np.float32), np.ones((0,), dtype=np.float32)
    geom = np.stack(dataset.geom_values, axis=0).astype(np.float32)
    mean = geom.mean(axis=0)
    std = geom.std(axis=0)
    std = np.maximum(std, 1e-6)
    return mean, std


def encode_targets(targets, mean, std):
    return (targets - mean) / std


def decode_targets(pred, mean, std):
    return pred * std + mean


def parse_target_weight_specs(targets, specs):
    """Parse CLI weights like length=1.5 default=1.0 into a target-aligned array."""
    weights = {name: 1.0 for name in targets}
    for spec in specs or []:
        if "=" not in spec:
            raise SystemExit(f"Invalid --target_weights entry: {spec!r}; expected name=value")
        name, raw_value = spec.split("=", 1)
        name = name.strip()
        try:
            value = float(raw_value)
        except ValueError as exc:
            raise SystemExit(f"Invalid --target_weights value in {spec!r}") from exc
        if not np.isfinite(value) or value < 0:
            raise SystemExit(f"Invalid --target_weights value in {spec!r}; weights must be finite and non-negative")
        if name == "default":
            for key in weights:
                weights[key] = value
        elif name in weights:
            weights[name] = value
        else:
            known = ", ".join(targets)
            raise SystemExit(f"Unknown --target_weights target {name!r}. Known targets: {known}")
    result = np.array([weights[name] for name in targets], dtype=np.float32)
    if not np.isfinite(result).all() or result.sum() <= 0:
        raise SystemExit("At least one target weight must be positive; all weights must be finite")
    return result


def weighted_regression_loss(pred_norm, y_norm, pred_all, y, target_weights,
                             criterion_elem, circular_indices=None,
                             relative_weight=0.0, relative_eps=1e-3,
                             target_std=None, reduction="mean"):
    """Weighted standardized SmoothL1, using shortest angular errors in degrees."""
    if circular_indices is None:
        circular_indices = []
    circular_set = set(circular_indices)
    device = pred_norm.device
    loss_total = torch.zeros(pred_norm.shape[0], device=device, dtype=pred_norm.dtype)
    # A shared denominator preserves the relative weights of angles and lengths.
    denom = torch.clamp(target_weights.sum(), min=1e-6)

    regular_indices = [i for i in range(pred_norm.shape[1]) if i not in circular_set]
    if regular_indices:
        reg_idx = torch.tensor(regular_indices, dtype=torch.long, device=device)
        base = criterion_elem(pred_norm.index_select(1, reg_idx), y_norm.index_select(1, reg_idx))
        base_weights = target_weights.index_select(0, reg_idx).view(1, -1)
        loss_total = loss_total + (base * base_weights).sum(dim=1) / denom

        if relative_weight > 0:
            rel = torch.abs(pred_all.index_select(1, reg_idx) - y.index_select(1, reg_idx)) / \
                  torch.clamp(torch.abs(y.index_select(1, reg_idx)), min=relative_eps)
            loss_total = loss_total + relative_weight * (rel * base_weights).sum(dim=1) / denom

    if circular_indices:
        if target_std is None:
            raise ValueError("Circular loss requires training target_std")
        circ_idx = torch.tensor(circular_indices, dtype=torch.long, device=device)
        error_deg = pred_all.index_select(1, circ_idx) - y.index_select(1, circ_idx)
        error_deg = torch.remainder(error_deg + 180.0, 360.0) - 180.0
        circ_std = target_std.reshape(1, -1).index_select(1, circ_idx).clamp_min(1e-6)
        error_norm = error_deg / circ_std
        circ = criterion_elem(error_norm, torch.zeros_like(error_norm))
        circ_weights = target_weights.index_select(0, circ_idx).view(1, -1)
        loss_total = loss_total + (circ * circ_weights).sum(dim=1) / denom

    if reduction == "none":
        return loss_total
    if reduction == "mean":
        return loss_total.mean()
    raise ValueError(f"Unsupported loss reduction: {reduction}")


def lhouse_notch_xy(values, targets):
    length, width, length_ratio, width_ratio = (
        values[:, targets.index(name)] for name in
        ("outerLength", "outerWidth", "cutoutLengthRatio", "cutoutWidthRatio"))
    return torch.stack((length * (1.0 - length_ratio), width * (1.0 - width_ratio)), dim=1)


def lhouse_corner_loss(pred, true, targets, target_std, criterion):
    # Decode the actual corner from the primary outputs, not a separate auxiliary head.
    difference = lhouse_notch_xy(pred, targets) - lhouse_notch_xy(true, targets)
    indices = [targets.index("outerLength"), targets.index("outerWidth")]
    scale = target_std.reshape(1, -1)[:, indices].clamp_min(1e-6)
    return criterion(difference / scale, torch.zeros_like(difference))


def symmetry_weighted_regression_loss(pred_norm, y, mean, std, target_weights,
                                      criterion_elem, permutation,
                                      relative_weight=0.0, relative_eps=1e-3,
                                      complement_index=None):
    """Select one equivalent target vector per cloud in physical target space."""
    pred_all = decode_targets(pred_norm, mean, std)
    direct = weighted_regression_loss(
        pred_norm, encode_targets(y, mean, std), pred_all, y,
        target_weights, criterion_elem, relative_weight=relative_weight,
        relative_eps=relative_eps, reduction="none")
    flipped_y = y.index_select(1, permutation)
    if complement_index is not None:
        flipped_y = flipped_y.clone()
        flipped_y[:, complement_index] = 1.0 - flipped_y[:, complement_index]
    flipped = weighted_regression_loss(
        pred_norm, encode_targets(flipped_y, mean, std), pred_all, flipped_y,
        target_weights, criterion_elem, relative_weight=relative_weight,
        relative_eps=relative_eps, reduction="none")
    if complement_index is not None:
        # At q=0.5 a minimum splits tied gradients and cancels the ridge signal.
        return torch.where(direct <= flipped, direct, flipped).mean()
    return torch.minimum(direct, flipped).mean()


def standardized_mae_score(mae, target_std, target_weights):
    """Dimensionless validation score; statistics come only from training data."""
    mae = np.asarray(mae, dtype=np.float64).reshape(-1)
    std = np.asarray(target_std, dtype=np.float64).reshape(-1)
    weights = np.asarray(target_weights, dtype=np.float64).reshape(-1)
    if not (mae.shape == std.shape == weights.shape):
        raise ValueError("MAE, target std, and weights must have matching shapes")
    if (not np.isfinite(std).all() or np.any(std <= 0)
            or not np.isfinite(weights).all() or np.any(weights < 0) or weights.sum() <= 0):
        raise ValueError("Expected positive finite std and non-negative finite weights")
    return float(np.sum((mae / np.maximum(std, 1e-6)) * weights) / weights.sum())


TRUNCATED_DIMENSION_TARGETS = ("geom_footprint_long", "geom_footprint_short", "geom_total_height")
TRUNCATED_SCALE_SETTINGS = {
    "truncated_scale_balanced": True,
    "dimension_relative_loss_weight": 0.05,
    "dimension_relative_targets": list(TRUNCATED_DIMENSION_TARGETS),
    "selection_metric": "scale_balanced_dimensionless_mae_v1",
    "loss_recipe": "standardized_smooth_l1_truncated_relative_dimensions_v1",
    "scale_band_source": "horizontalScaleBand",
    "selection_ratio_error": "absolute",
}


def truncated_dimension_relative_loss(pred, true, targets, target_weights, eps):
    indices = [targets.index(name) for name in TRUNCATED_DIMENSION_TARGETS]
    weights = target_weights[indices]
    errors = (pred[:, indices] - true[:, indices]).abs() / true[:, indices].abs().clamp_min(eps)
    return ((errors * weights).sum(dim=1) / weights.sum()).mean()


def truncated_scale_bands(dataset):
    bands = [sample[3].get("horizontalScaleBand") for sample in dataset.samples]
    if any(type(band) is not int or band not in (0, 1, 2) for band in bands) or set(bands) != {0, 1, 2}:
        raise ValueError("Truncated scale experiment requires horizontalScaleBand 0/1/2 in train and val")
    return np.asarray(bands, dtype=np.int64)


def truncated_scale_balanced_score(metrics, targets, target_weights, bands, eps):
    pred, true = np.asarray(metrics["pred"]), np.asarray(metrics["true"])
    weights = np.asarray(target_weights, dtype=np.float64)
    if (pred.shape != true.shape or pred.ndim != 2 or pred.shape[1] != len(targets)
            or len(bands) != len(true) or set(bands) != {0, 1, 2}):
        raise ValueError("Scale-balanced validation requires matching predictions and all three bands")
    errors = np.abs(pred - true).astype(np.float64)
    indices = [targets.index(name) for name in TRUNCATED_DIMENSION_TARGETS]
    errors[:, indices] /= np.maximum(np.abs(true[:, indices]), eps)
    # Ratio errors are already dimensionless; only physical dimensions are divided by their labels.
    per_cloud = (errors * weights).sum(axis=1) / weights.sum()
    scores = [float(per_cloud[np.asarray(bands) == band].mean()) for band in (0, 1, 2)]
    return float(np.mean(scores)), scores


INDENTED_180_MARGIN_PAIRS = (
    ("geom_long_start_margin_ratio", "geom_long_end_margin_ratio"),
    ("geom_short_start_margin_ratio", "geom_short_end_margin_ratio"),
)


def indented_180_permutation(class_name, targets):
    if class_name != "IndentedCuboid":
        raise ValueError("180-degree margin symmetry is only defined for IndentedCuboid")
    if len(set(targets)) != len(targets):
        raise ValueError("Targets must be unique for 180-degree margin symmetry")
    indices = {name: i for i, name in enumerate(targets)}
    missing = [name for pair in INDENTED_180_MARGIN_PAIRS for name in pair if name not in indices]
    if missing:
        raise ValueError(f"180-degree margin symmetry requires targets: {missing}")
    permutation = list(range(len(targets)))
    for start, end in INDENTED_180_MARGIN_PAIRS:
        permutation[indices[start]], permutation[indices[end]] = indices[end], indices[start]
    return permutation


def asym_ridge_180_index(class_name, targets):
    if class_name != "AsymmetricGableHouse":
        raise ValueError("Ridge 180-degree symmetry is only defined for AsymmetricGableHouse")
    if len(set(targets)) != len(targets) or "ridgeRatio" not in targets:
        raise ValueError("Ridge symmetry requires unique targets including ridgeRatio")
    return targets.index("ridgeRatio")


def roof_ridge_180_index(class_name, targets):
    if class_name not in ("GabledRoof", "AsymmetricGableHouse"):
        raise ValueError("Ridge symmetry requires GabledRoof or AsymmetricGableHouse")
    return asym_ridge_180_index("AsymmetricGableHouse", targets)


def symmetry_standardized_mae_score(metrics, target_std, target_weights, permutation,
                                   complement_index=None):
    """Per-cloud weighted MAE, allowing one global 180-degree margin flip."""
    pred = np.asarray(metrics["pred"], dtype=np.float64)
    true = np.asarray(metrics["true"], dtype=np.float64)
    std = np.asarray(target_std, dtype=np.float64).reshape(1, -1)
    weights = np.asarray(target_weights, dtype=np.float64).reshape(1, -1)
    if pred.shape != true.shape or pred.shape[1] != len(permutation):
        raise ValueError("Symmetry score requires matching predictions, labels, and permutation")
    scale = weights / np.maximum(std, 1e-6)
    direct = (np.abs(pred - true) * scale).sum(axis=1)
    flipped_true = true[:, permutation].copy()
    if complement_index is not None:
        flipped_true[:, complement_index] = 1.0 - flipped_true[:, complement_index]
    flipped = (np.abs(pred - flipped_true) * scale).sum(axis=1)
    return float(np.minimum(direct, flipped).mean() / weights.sum())

# ============================================================================
# Evaluation
# ============================================================================

def circular_diff_deg(pred, true):
    diff = pred - true
    return np.rad2deg(np.arctan2(np.sin(np.deg2rad(diff)), np.cos(np.deg2rad(diff))))


def evaluate(model, loader, device, mean, std, aux_mean, aux_std,
             target_names, aux_names, target_mode, circular_indices=None):
    if circular_indices is None:
        circular_indices = []
    circular_set = set(circular_indices)

    model.eval()
    pred_rows, true_rows, paths = [], [], []
    with torch.no_grad():
        for points, aux, targets, geom_targets, batch_paths in loader:
            points = points.to(device, non_blocking=device.type == "cuda")
            aux = aux.to(device, non_blocking=device.type == "cuda")
            targets = targets.to(device, non_blocking=device.type == "cuda")
            aux_norm = (aux - aux_mean) / aux_std if aux.shape[1] > 0 else aux
            pred_out = model(points, aux_norm)
            pred_norm = pred_out[:, :mean.shape[1]]
            pred_transformed = decode_targets(pred_norm, mean, std)
            baseline = target_baseline_from_aux_tensor(target_names, aux_names, aux, target_mode)
            pred = invert_target_transform_tensor(pred_transformed, baseline, target_mode)
            true_values = invert_target_transform_tensor(targets, baseline, target_mode)
            pred_rows.append(pred.cpu().numpy())
            true_rows.append(true_values.cpu().numpy())
            paths.extend(batch_paths)

    y_pred = np.concatenate(pred_rows, axis=0)
    y_true = np.concatenate(true_rows, axis=0)
    n_targets = y_true.shape[1]

    err = np.zeros_like(y_pred)
    for i in range(n_targets):
        if i in circular_set:
            err[:, i] = circular_diff_deg(y_pred[:, i], y_true[:, i])
        else:
            err[:, i] = y_pred[:, i] - y_true[:, i]

    mae = np.mean(np.abs(err), axis=0)
    rmse = np.sqrt(np.mean(err ** 2, axis=0))
    denom = np.maximum(np.abs(y_true), 1e-6)
    mape = np.mean(np.abs(err) / denom, axis=0) * 100.0
    ss_res = np.sum(err ** 2, axis=0)
    ss_tot = np.sum((y_true - y_true.mean(axis=0, keepdims=True)) ** 2, axis=0)
    r2 = 1.0 - ss_res / np.maximum(ss_tot, 1e-6)
    return {"paths": paths, "pred": y_pred, "true": y_true,
            "mae": mae, "rmse": rmse, "mape": mape, "r2": r2}


# ============================================================================
# Logging (same outputs as main_reg.py)
# ============================================================================

def save_metrics(log_dir, split, targets, metrics):
    log_dir.mkdir(parents=True, exist_ok=True)
    with (log_dir / f"regression_metrics_{split}.csv").open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(["target", "mae", "rmse", "mape_percent", "r2"])
        for i, name in enumerate(targets):
            w.writerow([name, float(metrics["mae"][i]), float(metrics["rmse"][i]),
                        float(metrics["mape"][i]), float(metrics["r2"][i])])


def save_predictions(log_dir, split, targets, metrics):
    with (log_dir / f"regression_predictions_{split}.csv").open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        header = ["file"] + [f"true_{t}" for t in targets] + [f"pred_{t}" for t in targets] + [f"abs_err_{t}" for t in targets]
        w.writerow(header)
        for path, true, pred in zip(metrics["paths"], metrics["true"], metrics["pred"]):
            w.writerow([path] + true.tolist() + pred.tolist() + np.abs(pred - true).tolist())


def save_regression_report(log_dir, split, class_name, targets, metrics):
    with (log_dir / f"regression_report_{split}.csv").open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(["class", "param", "mae", "rmse", "mape_percent", "r2", "num_samples"])
        n = int(len(metrics["paths"]))
        for i, name in enumerate(targets):
            w.writerow([class_name, name,
                        float(metrics["mae"][i]), float(metrics["rmse"][i]),
                        float(metrics["mape"][i]), float(metrics["r2"][i]), n])


def save_regression_errors(log_dir, split, class_name, targets, metrics):
    with (log_dir / f"regression_errors_{split}.csv").open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(["file", "class", "param", "true", "pred", "error", "abs_error", "percent_error"])
        for path, true, pred in zip(metrics["paths"], metrics["true"], metrics["pred"]):
            err = pred - true
            for i, name in enumerate(targets):
                w.writerow([path, class_name, name,
                            float(true[i]), float(pred[i]), float(err[i]),
                            float(abs(err[i])), float(abs(err[i]) / max(float(abs(true[i])), 1e-6) * 100)])


def save_config(log_dir, args, targets, mean, std, aux_names, aux_mean, aux_std, circular_targets=None):
    payload = {
        "model": "ptv3",
        "backbone": args.backbone,
        "backbone_config": backbone_config(args),
        "seed": args.seed,
        "class_name": args.class_name,
        "targets": targets,
        "target_mode": getattr(args, "target_mode", "raw"),
        "target_mean": mean.tolist(),
        "target_std": std.tolist(),
        "num_points": args.num_points,
        "data_loading": data_loading_config(args),
        "normalize_points": args.normalize_points,
        "canonical_align": getattr(args, "canonical_align", False),
        "lhouse_canonical": getattr(args, "lhouse_canonical", False),
        "canonical_aux_units": getattr(args, "canonical_aux_units", "legacy"),
        "canonical_bbox_mode": (LHOUSE_ROBUST_BBOX_MODE if getattr(args, "lhouse_robust_bbox", False) else "full"),
        "canonical_frame_version": ("lhouse_full_yaw_v1" if getattr(args, "lhouse_canonical", False)
                                    else "metadata_yaw_v1" if getattr(args, "canonical_align", False) else None),
        "aux_names": aux_names,
        "aux_mean": aux_mean.tolist(),
        "aux_std": aux_std.tolist(),
        "circular_targets": circular_targets or [],
        "geom_aux_targets": getattr(args, "geom_aux_targets", []),
        "geom_loss_weight": getattr(args, "geom_loss_weight", 0.0),
        "lhouse_corner_loss_weight": getattr(args, "lhouse_corner_loss_weight", 0.0),
        "use_normals": getattr(args, 'use_normals', False),
        "normal_backend": getattr(args, "normal_backend", "numpy"),
        "selection_metric": ("symmetry_weighted_standardized_mae" if (getattr(args, "indented_symmetry_180", False)
                              or getattr(args, "asym_ridge_symmetry_180", False)
                              or getattr(args, "gabled_ridge_symmetry_180", False))
                             else "weighted_standardized_mae"),
        "indented_symmetry_180": getattr(args, "indented_symmetry_180", False),
        "asym_ridge_symmetry_180": getattr(args, "asym_ridge_symmetry_180", False),
        "gabled_ridge_symmetry_180": getattr(args, "gabled_ridge_symmetry_180", False),
        "target_weights": parse_target_weight_specs(targets, args.target_weights).tolist(),
        "loss_recipe": ("standardized_wrapped_smooth_l1_indented_180_v1"
                        if getattr(args, "indented_symmetry_180", False)
                        else "standardized_wrapped_smooth_l1_gabled_ridge_180_v1"
                        if getattr(args, "gabled_ridge_symmetry_180", False)
                        else "standardized_wrapped_smooth_l1_asym_ridge_180_v1"
                        if getattr(args, "asym_ridge_symmetry_180", False)
                        else "standardized_wrapped_smooth_l1_lhouse_corner_v1"
                        if getattr(args, "lhouse_corner_loss_weight", 0.0) > 0
                        else "standardized_wrapped_smooth_l1_v2"),
    }
    if getattr(args, "truncated_scale_balanced", False):
        payload.update(TRUNCATED_SCALE_SETTINGS)
        payload["dimension_relative_eps"] = args.relative_loss_eps
    (log_dir / "regression_config.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def load_config(log_dir):
    path = log_dir / "regression_config.json"
    if not path.exists():
        raise FileNotFoundError(f"Missing regression config: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


# ============================================================================
# Training
# ============================================================================

def backbone_config(args):
    names = ("ptv3_grid_size", "ptv3_patch_size", "ptv3_flash", "ptv3_dropout")
    return {name: getattr(args, name) for name in names if hasattr(args, name)}


def build_model(target_dim, aux_dim, args, in_channels=3):
    return PTv3Regressor(
        target_dim=target_dim, aux_dim=aux_dim, in_channels=in_channels,
        grid_size=args.ptv3_grid_size, patch_size=args.ptv3_patch_size,
        enable_flash=args.ptv3_flash, dropout=args.ptv3_dropout,
        repo_path=args.ptv3_repo,
    )


def train(args):
    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    device = torch.device("cpu" if args.cpu or not torch.cuda.is_available() else "cuda")
    targets = args.targets
    symmetry_perm = (indented_180_permutation(args.class_name, targets)
                     if args.indented_symmetry_180 else None)
    ridge_index = None
    if args.asym_ridge_symmetry_180 or args.gabled_ridge_symmetry_180:
        ridge_index = roof_ridge_180_index(args.class_name, targets)
        symmetry_perm = list(range(len(targets)))
    target_mode = args.target_mode
    corner_weight = args.lhouse_corner_loss_weight
    scale_balanced = getattr(args, "truncated_scale_balanced", False)
    bbox_mode = LHOUSE_ROBUST_BBOX_MODE if args.lhouse_robust_bbox else "full"
    aux_names = [] if args.no_aux else args.aux_features

    if args.backbone == "ptv3" and device.type != "cuda":
        raise RuntimeError("PTv3 requires CUDA/spconv. Run this experiment on the Ubuntu GPU machine.")
    output_dim = len(targets) + len(args.geom_aux_targets)
    model = build_model(output_dim, len(aux_names), args,
                        in_channels=6 if args.use_normals else 3).to(device)
    print(f"Preparing {args.backbone} datasets and cached normals...", flush=True)

    train_ds = ParamRegressionDataset(
        args.data_root, args.metadata, args.class_name, "train",
        targets, args.num_points, train=True, normalize=args.normalize_points,
        jitter_sigma=args.train_jitter_sigma, random_rotate=args.random_rotate,
        aux_names=aux_names, use_normals=args.use_normals,
        geom_aux_targets=args.geom_aux_targets,
        target_mode=target_mode,
        canonical_align=args.canonical_align,
        lhouse_canonical=args.lhouse_canonical,
        canonical_aux_units=args.canonical_aux_units,
        canonical_bbox_mode=bbox_mode,
        cache_points=getattr(args, "cache_points", True),
        normal_workers=getattr(args, "normal_workers", 0),
        **normal_kwargs(args),
    )
    val_split = "val"
    val_ds = ParamRegressionDataset(
        args.data_root, args.metadata, args.class_name, val_split,
        targets, args.num_points, train=False, normalize=args.normalize_points,
        aux_names=aux_names, use_normals=args.use_normals,
        geom_aux_targets=args.geom_aux_targets,
        target_mode=target_mode,
        canonical_align=args.canonical_align,
        lhouse_canonical=args.lhouse_canonical,
        canonical_aux_units=args.canonical_aux_units,
        canonical_bbox_mode=bbox_mode,
        cache_points=getattr(args, "cache_points", True),
        normal_workers=getattr(args, "normal_workers", 0),
        **normal_kwargs(args),
    )
    if scale_balanced:
        truncated_scale_bands(train_ds)
        val_scale_bands = truncated_scale_bands(val_ds)

    mean_np, std_np = compute_target_stats(train_ds)
    aux_mean_np, aux_std_np = compute_aux_stats(train_ds, len(aux_names))
    geom_mean_np, geom_std_np = compute_geometry_aux_stats(train_ds)
    save_config(log_dir, args, targets, mean_np, std_np, aux_names, aux_mean_np, aux_std_np,
                args.circular_targets)
    mean = torch.tensor(mean_np, dtype=torch.float32, device=device).view(1, -1)
    std = torch.tensor(std_np, dtype=torch.float32, device=device).view(1, -1)
    aux_mean = torch.tensor(aux_mean_np, dtype=torch.float32, device=device).view(1, -1)
    aux_std = torch.tensor(aux_std_np, dtype=torch.float32, device=device).view(1, -1)
    geom_mean = torch.tensor(geom_mean_np, dtype=torch.float32, device=device).view(1, -1)
    geom_std = torch.tensor(geom_std_np, dtype=torch.float32, device=device).view(1, -1)

    workers = getattr(args, "num_workers", 0)
    seed = getattr(args, "seed", None)
    train_loader = make_data_loader(train_ds, args.batch_size, True, device, workers, seed)
    val_loader = make_data_loader(val_ds, args.batch_size, False, device, workers,
                                  seed + 1 if seed is not None else None)

    print(f"Model: PTv3 | params: {sum(p.numel() for p in model.parameters())/1e6:.2f}M")
    print(f"Train samples: {len(train_ds)} | Val samples: {len(val_ds)}")
    print(f"Data loading: {data_loading_config(args)} | pin_memory={train_loader.pin_memory}")
    print(f"Targets: {targets} | TargetMode: {target_mode} | Aux: {aux_names} | GeomAux: {args.geom_aux_targets} (weight={args.geom_loss_weight})")
    if bbox_mode != "full":
        print(f"Canonical bbox features: {bbox_mode} | XY quantiles=0.005,0.995 | Z=full span")

    criterion = nn.SmoothL1Loss(beta=args.smooth_l1_beta)
    criterion_elem = nn.SmoothL1Loss(beta=args.smooth_l1_beta, reduction="none")
    target_weight_np = parse_target_weight_specs(targets, args.target_weights)
    target_weights = torch.tensor(target_weight_np, dtype=torch.float32, device=device)
    symmetry_perm_tensor = (torch.tensor(symmetry_perm, dtype=torch.long, device=device)
                            if symmetry_perm is not None else None)
    if args.target_weights or args.relative_loss_weight > 0:
        print(f"Loss: weighted SmoothL1 | target_weights={dict(zip(targets, target_weight_np.tolist()))} | "
              f"relative_loss_weight={args.relative_loss_weight}")
    if corner_weight > 0:
        print(f"LHouse corner loss: weight={corner_weight} | origin=outer_lower_left | scale=train_length_width_std")
    if scale_balanced:
        print("Truncated scale experiment: dimension relative weight=0.05 | ratios=original loss | "
              "selection=equal mean of small/medium/large dimensionless validation MAE")
    optimizer = optim.Adam(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs, eta_min=args.lr * 1e-3)

    circular_set = set(args.circular_targets)
    circular_indices = [i for i, t in enumerate(targets) if t in circular_set]
    regular_indices = [i for i, t in enumerate(targets) if t not in circular_set]

    best_score = float("inf")
    history_path = log_dir / "train_history.csv"
    with history_path.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.writer(f)
        history_columns = ["epoch", "train_loss", "val_mae_mean", "val_rmse_mean",
                           "val_standardized_mae", "val_selection_score"]
        if corner_weight > 0:
            history_columns += ["val_local_notch_mean", "val_local_notch_p95"]
        if scale_balanced:
            history_columns += ["train_dimension_relative_loss", "val_scale_small", "val_scale_medium", "val_scale_large"]
        writer.writerow(history_columns)

        for epoch in range(1, args.epochs + 1):
            model.train()
            total_loss, total_count = 0.0, 0
            total_dimension_relative = 0.0
            for points, aux, y, geom_y, _ in train_loader:
                points = points.to(device, non_blocking=device.type == "cuda")
                aux = aux.to(device, non_blocking=device.type == "cuda")
                y = y.to(device, non_blocking=device.type == "cuda")
                geom_y = geom_y.to(device, non_blocking=device.type == "cuda")
                y_norm = encode_targets(y, mean, std)
                geom_norm = encode_targets(geom_y, geom_mean, geom_std) if geom_y.shape[1] > 0 else geom_y
                aux_norm = (aux - aux_mean) / aux_std if aux.shape[1] > 0 else aux

                optimizer.zero_grad()
                pred_out = model(points, aux_norm)
                pred_norm = pred_out[:, :len(targets)]
                pred_geom_norm = pred_out[:, len(targets):]

                if symmetry_perm_tensor is not None:
                    loss_total = symmetry_weighted_regression_loss(
                        pred_norm, y, mean, std, target_weights, criterion_elem,
                        symmetry_perm_tensor, args.relative_loss_weight,
                        args.relative_loss_eps, complement_index=ridge_index)
                elif args.target_weights or args.relative_loss_weight > 0 or circular_indices:
                    pred_all = decode_targets(pred_norm, mean, std)
                    loss_total = weighted_regression_loss(
                        pred_norm, y_norm, pred_all, y, target_weights, criterion_elem,
                        circular_indices=circular_indices,
                        relative_weight=args.relative_loss_weight,
                        relative_eps=args.relative_loss_eps,
                        target_std=std,
                    )
                else:
                    loss_total = criterion(pred_norm, y_norm)
                if scale_balanced:
                    baseline = target_baseline_from_aux_tensor(targets, aux_names, aux, target_mode)
                    physical_pred = invert_target_transform_tensor(decode_targets(pred_norm, mean, std), baseline, target_mode)
                    physical_true = invert_target_transform_tensor(y, baseline, target_mode)
                    dimension_relative = truncated_dimension_relative_loss(
                        physical_pred, physical_true, targets, target_weights, args.relative_loss_eps)
                    loss_total = loss_total + TRUNCATED_SCALE_SETTINGS["dimension_relative_loss_weight"] * dimension_relative
                    total_dimension_relative += dimension_relative.item() * points.size(0)
                if corner_weight > 0:
                    corner_loss = lhouse_corner_loss(decode_targets(pred_norm, mean, std), y,
                                                     targets, std, criterion)
                    loss_total = loss_total + corner_weight * corner_loss
                if args.geom_aux_targets and args.geom_loss_weight > 0:
                    geom_loss = criterion(pred_geom_norm, geom_norm)
                    loss_total = loss_total + args.geom_loss_weight * geom_loss

                loss_total.backward()
                optimizer.step()
                total_loss += loss_total.item() * points.size(0)
                total_count += points.size(0)

            scheduler.step()
            train_loss = total_loss / max(total_count, 1)
            val_metrics = evaluate(model, val_loader, device, mean, std, aux_mean, aux_std, targets, aux_names, target_mode, circular_indices)
            val_mae_mean = float(np.mean(val_metrics["mae"]))
            val_rmse_mean = float(np.mean(val_metrics["rmse"]))
            val_direct_score = standardized_mae_score(val_metrics["mae"], std_np, target_weight_np)
            val_score = (symmetry_standardized_mae_score(val_metrics, std_np, target_weight_np, symmetry_perm,
                                                       complement_index=ridge_index)
                             if symmetry_perm is not None else val_direct_score)
            if scale_balanced:
                val_score, band_scores = truncated_scale_balanced_score(
                    val_metrics, targets, target_weight_np, val_scale_bands, args.relative_loss_eps)
            history_row = [epoch, train_loss, val_mae_mean, val_rmse_mean, val_direct_score, val_score]
            corner_message = ""
            if corner_weight > 0:
                corner_delta = (lhouse_notch_xy(torch.as_tensor(val_metrics["pred"]), targets)
                                - lhouse_notch_xy(torch.as_tensor(val_metrics["true"]), targets))
                corner_errors = torch.linalg.vector_norm(corner_delta, dim=1).numpy()
                corner_mean, corner_p95 = float(corner_errors.mean()), float(np.percentile(corner_errors, 95))
                history_row += [corner_mean, corner_p95]
                corner_message = f" | val_local_notch_mean {corner_mean:.4f} | val_local_notch_p95 {corner_p95:.4f}"
            if scale_balanced:
                history_row += [total_dimension_relative / max(total_count, 1), *band_scores]
                corner_message += (f" | val_scale_small {band_scores[0]:.4f}"
                                   f" | val_scale_medium {band_scores[1]:.4f}"
                                   f" | val_scale_large {band_scores[2]:.4f}")
            writer.writerow(history_row)
            f.flush()

            print(f"Epoch {epoch:03d}/{args.epochs} | loss {train_loss:.5f} | "
                  f"val_mae {val_mae_mean:.4f} | val_rmse {val_rmse_mean:.4f} | "
                  f"val_standardized_mae {val_direct_score:.4f} | val_selection_score {val_score:.4f}"
                  f"{corner_message}")

            if val_score < best_score:
                best_score = val_score
                torch.save({
                    "model_state": model.state_dict(),
                    "class_name": args.class_name, "targets": targets,
                    "target_mode": target_mode,
                    "target_mean": mean_np.tolist(), "target_std": std_np.tolist(),
                    "geom_aux_targets": args.geom_aux_targets,
                    "geom_aux_mean": geom_mean_np.tolist(), "geom_aux_std": geom_std_np.tolist(),
                    "lhouse_corner_loss_weight": corner_weight,
                    "num_points": args.num_points, "normalize_points": args.normalize_points,
                    "data_loading": data_loading_config(args),
                    "canonical_align": args.canonical_align,
                    "lhouse_canonical": args.lhouse_canonical,
                    "canonical_aux_units": args.canonical_aux_units,
                    "canonical_bbox_mode": bbox_mode,
                    "canonical_frame_version": ("lhouse_full_yaw_v1" if args.lhouse_canonical
                                                else "metadata_yaw_v1" if args.canonical_align else None),
                    "aux_names": aux_names,
                    "aux_mean": aux_mean_np.tolist(), "aux_std": aux_std_np.tolist(),
                    "circular_targets": args.circular_targets,
                    "backbone": args.backbone,
                    "backbone_config": backbone_config(args),
                    "seed": args.seed,
                    "use_normals": args.use_normals,
                    "normal_backend": getattr(args, "normal_backend", "numpy"),
                    "epoch": epoch,
                    "selection_metric": ("symmetry_weighted_standardized_mae" if symmetry_perm is not None
                                         else "weighted_standardized_mae"),
                    "selection_score": best_score,
                    "indented_symmetry_180": args.indented_symmetry_180,
                    "asym_ridge_symmetry_180": args.asym_ridge_symmetry_180,
                    "gabled_ridge_symmetry_180": args.gabled_ridge_symmetry_180,
                    "target_weights": target_weight_np.tolist(),
                    "loss_recipe": ("standardized_wrapped_smooth_l1_indented_180_v1"
                                    if args.indented_symmetry_180
                                    else "standardized_wrapped_smooth_l1_gabled_ridge_180_v1"
                                    if args.gabled_ridge_symmetry_180
                                    else "standardized_wrapped_smooth_l1_asym_ridge_180_v1"
                                    if args.asym_ridge_symmetry_180
                                    else "standardized_wrapped_smooth_l1_lhouse_corner_v1"
                                    if corner_weight > 0
                                    else "standardized_wrapped_smooth_l1_v2"),
                    **({**TRUNCATED_SCALE_SETTINGS, "dimension_relative_eps": args.relative_loss_eps}
                       if scale_balanced else {}),
                }, log_dir / "best_model.pth")
                save_metrics(log_dir, val_split, targets, val_metrics)
                save_predictions(log_dir, val_split, targets, val_metrics)
                save_regression_report(log_dir, val_split, args.class_name, targets, val_metrics)
                save_regression_errors(log_dir, val_split, args.class_name, targets, val_metrics)
                print(f"  -> saved best_model.pth (val_selection_score={best_score:.4f})")

    print(f"Training done. Best validation selection score: {best_score:.4f}")


# ============================================================================
# Test & Predict (same inference logic as main_reg.py)
# ============================================================================

def load_checkpoint(log_dir, device, args):
    ckpt_path = log_dir / "best_model.pth"
    if not ckpt_path.exists():
        raise FileNotFoundError(f"Missing checkpoint: {ckpt_path}")
    ckpt = torch.load(ckpt_path, map_location=device, weights_only=False)
    if ckpt.get("canonical_aux_units", "legacy") not in ("legacy", "physical"):
        raise ValueError("Unsupported checkpoint canonical auxiliary units")
    validate_canonical_bbox_mode(ckpt.get("canonical_bbox_mode", "full"), ckpt.get("class_name"),
                                 ckpt.get("canonical_align", False), ckpt.get("lhouse_canonical", False),
                                 ckpt.get("canonical_aux_units", "legacy"))
    if ckpt.get("backbone") != "ptv3":
        raise RuntimeError("This directory loads PTv3 checkpoints only. Use pct_simple for PCT weights.")
    args.circular_targets = list(ckpt.get("circular_targets", args.circular_targets))
    for name, value in ckpt.get("backbone_config", {}).items():
        if name in backbone_config(args):
            setattr(args, name, value)
    if args.backbone == "ptv3" and device.type != "cuda":
        raise RuntimeError("PTv3 checkpoints require CUDA/spconv; CPU prediction is not supported.")
    targets = list(ckpt["targets"])
    aux_names = list(ckpt.get("aux_names", []))
    in_channels = 6 if ckpt.get("use_normals", False) else 3
    geom_aux_targets = list(ckpt.get("geom_aux_targets", []))
    model = build_model(len(targets) + len(geom_aux_targets), len(aux_names), args, in_channels=in_channels).to(device)
    model.load_state_dict(ckpt["model_state"])
    model.eval()
    mean = torch.tensor(ckpt["target_mean"], dtype=torch.float32, device=device).view(1, -1)
    std = torch.tensor(ckpt["target_std"], dtype=torch.float32, device=device).view(1, -1)
    aux_mean = torch.tensor(ckpt.get("aux_mean", []), dtype=torch.float32, device=device).view(1, -1)
    aux_std = torch.tensor(ckpt.get("aux_std", []), dtype=torch.float32, device=device).view(1, -1)
    return model, ckpt, targets, mean, std, aux_mean, aux_std


def centered_geometry_errors(dataset, targets, metrics):
    """Measure shape/pose together, after aligning footprint centers (not translations)."""
    class_name = dataset.class_name
    if class_name == "LHouse" and not dataset.lhouse_canonical:
        return {}
    keys = {
        "Cuboid": ("length", "width"), "PyramidRoof": ("length", "width"),
        "TruncatedPyramidRoof": ("bottomLength", "bottomWidth"),
        "IndentedCuboid": ("outerLength", "outerWidth"),
        "GabledRoof": ("length", "width"), "AsymmetricGableHouse": ("length", "width"),
        "HalfCylinderRoof": ("length", "width"),
        "LHouse": ("outerLength", "outerWidth"),
    }
    if class_name not in keys:
        return {}

    def rotate_xy(points, yaw):
        a = np.deg2rad(yaw)
        c, s = np.cos(a), np.sin(a)
        return np.asarray(points) @ np.array([[c, s], [-s, c]])

    corners = np.array([[-0.5, -0.5], [-0.5, 0.5], [0.5, -0.5], [0.5, 0.5]])
    rows = []
    for sample, prediction in zip(dataset.samples, metrics["pred"]):
        path, _, _, record, yaw = sample
        true = record["params"]
        predicted = prediction_params_for_plugin(class_name, targets, prediction)
        length_key, width_key = keys[class_name]
        true_size = [float(true[length_key]), float(true[width_key])]
        pred_size = [predicted[length_key], predicted[width_key]]
        true_xy = rotate_xy(corners * true_size, float(true["rz"]))
        pred_xy = rotate_xy(corners * pred_size, yaw)
        if class_name == "LHouse":
            def outline(params):
                length, width = params["outerLength"], params["outerWidth"]
                x = length * (1.0 - params["cutoutLengthRatio"])
                y = width * (1.0 - params["cutoutWidthRatio"])
                return np.array([[0, 0], [length, 0], [length, y], [x, y],
                                 [x, width], [0, width]]) - np.array([length, width]) / 2
            true_xy = rotate_xy(outline(true), float(true["rz"]))
            pred_xy = rotate_xy(outline(predicted), yaw)
        distances = np.linalg.norm(pred_xy[:, None, :] - true_xy[None, :, :], axis=2)
        row = {"file": str(path), "footprint_corner_error": float(
            (distances.min(axis=0).mean() + distances.min(axis=1).mean()) / 2)}
        if class_name == "LHouse":
            row["notch_corner_error"] = float(np.linalg.norm(pred_xy[3] - true_xy[3]))
        if class_name in ("GabledRoof", "AsymmetricGableHouse"):
            length_name = "ridgeLength" if class_name == "AsymmetricGableHouse" else "length"
            true_ridge = rotate_xy([[-true[length_name] / 2, (true["ridgeRatio"] - 0.5) * true_size[1]],
                                    [true[length_name] / 2, (true["ridgeRatio"] - 0.5) * true_size[1]]], true["rz"])
            pred_ridge = rotate_xy([[-predicted[length_name] / 2, (predicted["ridgeRatio"] - 0.5) * pred_size[1]],
                                    [predicted[length_name] / 2, (predicted["ridgeRatio"] - 0.5) * pred_size[1]]], yaw)
            row["ridge_endpoint_error"] = float(min(
                np.linalg.norm(pred_ridge - true_ridge, axis=1).mean(),
                np.linalg.norm(pred_ridge - true_ridge[::-1], axis=1).mean()))
        rows.append(row)
    result = {"centered_geometry_samples": rows}
    for key in ("footprint_corner_error", "ridge_endpoint_error", "notch_corner_error"):
        values = [row[key] for row in rows if key in row]
        if values:
            result[key + "_mean"] = float(np.mean(values))
            result[key + "_p95"] = float(np.percentile(values, 95))
    return result


def test(args):
    log_dir = Path(args.log_dir)
    separate_output = getattr(args, "eval_output_dir", None)
    output_dir = Path(separate_output) if separate_output else log_dir
    if separate_output:
        if output_dir.resolve() == log_dir.resolve():
            raise ValueError("Evaluation output must differ from the checkpoint directory")
        if output_dir.exists() and (not output_dir.is_dir() or any(output_dir.iterdir())):
            raise ValueError(f"Evaluation output must be a new or empty directory: {output_dir}")
    device = torch.device("cpu" if args.cpu or not torch.cuda.is_available() else "cuda")
    model, ckpt, targets, mean, std, aux_mean, aux_std = load_checkpoint(log_dir, device, args)
    class_name = args.class_name or ckpt["class_name"]
    metadata = args.metadata or str(Path(args.data_root) / "metadata" / "sample_params.json")
    num_points = args.num_points or int(ckpt.get("num_points", 2048))
    normalize = bool(ckpt.get("normalize_points", True))
    use_normals = bool(ckpt.get("use_normals", False))
    aux_names = list(ckpt.get("aux_names", []))
    target_mode = ckpt.get("target_mode", "raw")
    geometry_test = args.test_orientation == "geometry"
    if geometry_test and not ckpt.get("canonical_align", False):
        raise ValueError("Geometry-frame testing requires a canonical checkpoint")

    ds = ParamRegressionDataset(args.data_root, metadata, class_name, args.split,
                                targets, num_points, train=False, normalize=normalize,
                                aux_names=aux_names, use_normals=use_normals,
                                geom_aux_targets=list(ckpt.get("geom_aux_targets", [])),
                                target_mode=target_mode,
                                canonical_align=bool(ckpt.get("canonical_align", False)),
                                lhouse_canonical=bool(ckpt.get("lhouse_canonical", False)),
                                canonical_aux_units=ckpt.get("canonical_aux_units", "legacy"),
                                canonical_bbox_mode=ckpt.get("canonical_bbox_mode", "full"),
                                cache_points=getattr(args, "cache_points", True),
                                normal_workers=getattr(args, "normal_workers", 0),
                                **normal_kwargs(args, ckpt),
                                orientation_source=args.test_orientation)
    output_split = args.split + "_geometry" if geometry_test else args.split
    orientation_report = {"orientation_source": args.test_orientation,
                          "alignment_attempted": len(ds.orientation_diagnostics),
                          "accepted": len(ds),
                          "failed": sum(not row["applied"] for row in ds.orientation_diagnostics),
                          "samples": ds.orientation_diagnostics}
    if separate_output:
        output_dir.mkdir(parents=True, exist_ok=True)
    if geometry_test:
        (output_dir / f"orientation_{output_split}.json").write_text(
            json.dumps(orientation_report, indent=2), encoding="utf-8")
    if not len(ds):
        raise RuntimeError("No usable test samples; inspect the orientation report")
    loader = make_data_loader(ds, args.batch_size, False, device,
                              getattr(args, "num_workers", 0), getattr(args, "seed", None))
    circ_indices = [i for i, t in enumerate(targets) if t in set(args.circular_targets)]
    metrics = evaluate(model, loader, device, mean, std, aux_mean, aux_std, targets, aux_names, target_mode, circ_indices)
    save_metrics(output_dir, output_split, targets, metrics)
    save_predictions(output_dir, output_split, targets, metrics)
    save_regression_report(output_dir, output_split, class_name, targets, metrics)
    save_regression_errors(output_dir, output_split, class_name, targets, metrics)
    if geometry_test or ckpt.get("lhouse_canonical", False):
        orientation_report.update(centered_geometry_errors(ds, targets, metrics))
        (output_dir / f"orientation_{output_split}.json").write_text(
            json.dumps(orientation_report, indent=2), encoding="utf-8")
    print(f"Test samples: {len(ds)}")
    for i, name in enumerate(targets):
        print(f"  {name:20s}  MAE={metrics['mae'][i]:.4f}  RMSE={metrics['rmse'][i]:.4f}  "
              f"MAPE={metrics['mape'][i]:.2f}%  R²={metrics['r2'][i]:.4f}")
    print(f"Mean MAE: {float(np.mean(metrics['mae'])):.4f}")
    if ckpt.get("indented_symmetry_180", False):
        permutation = indented_180_permutation(class_name, targets)
        weights = np.asarray(ckpt["target_weights"], dtype=np.float64)
        std_values = np.asarray(ckpt["target_std"], dtype=np.float64)
        print(f"Directed standardized MAE: {standardized_mae_score(metrics['mae'], std_values, weights):.4f}")
        print(f"180-symmetry standardized MAE: {symmetry_standardized_mae_score(metrics, std_values, weights, permutation):.4f}")
    if ckpt.get("asym_ridge_symmetry_180", False) or ckpt.get("gabled_ridge_symmetry_180", False):
        index = roof_ridge_180_index(class_name, targets)
        weights = np.asarray(ckpt["target_weights"], dtype=np.float64)
        std_values = np.asarray(ckpt["target_std"], dtype=np.float64)
        score = symmetry_standardized_mae_score(metrics, std_values, weights,
                                               list(range(len(targets))), complement_index=index)
        direct = np.abs(metrics["pred"][:, index] - metrics["true"][:, index])
        alternate = np.abs(metrics["pred"][:, index] - (1.0 - metrics["true"][:, index]))
        summary = {"direct_standardized_mae": standardized_mae_score(metrics["mae"], std_values, weights),
                   "symmetry_standardized_mae": score,
                   "ridge_direct_mae": float(direct.mean()),
                   "ridge_equivalent_mae": float(np.minimum(direct, alternate).mean()),
                   "pose_disambiguation_required": True}
        (output_dir / f"ridge_symmetry_{output_split}.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
        print(f"Ridge symmetry diagnostics: {summary}")


def predict(args):
    log_dir = Path(args.log_dir)
    device = torch.device("cpu" if args.cpu or not torch.cuda.is_available() else "cuda")
    model, ckpt, targets, mean, std, aux_mean, aux_std = load_checkpoint(log_dir, device, args)
    num_points = args.num_points or int(ckpt.get("num_points", 2048))
    normalize = bool(ckpt.get("normalize_points", True))
    aux_names = list(ckpt.get("aux_names", []))
    target_mode = ckpt.get("target_mode", "raw")

    record = None
    aux = np.zeros((0,), dtype=np.float32)
    if aux_names:
        metadata = args.metadata or str(Path(args.data_root) / "metadata" / "sample_params.json")
        metadata_path = Path(metadata)
        if metadata_path.exists():
            record = find_metadata_record(load_metadata(metadata_path), args.data_root, args.input)
        if record is not None:
            aux = aux_vector(record, aux_names)
        else:
            aux_from_args = aux_vector_from_args(args, aux_names)
            if aux_from_args is None:
                raise SystemExit("Aux features required. Pass --bbox_x/y/z --scale or --metadata.")
            aux = aux_from_args

    points = load_txt_points(Path(args.input))
    source_points = points

    # ---- Canonical orientation alignment ----
    pose_rz_deg = None
    orientation_info = None
    canonical_requested = bool(getattr(args, "canonical_align", False)
                               or ckpt.get("canonical_align", False))
    canonical_applied = False
    if canonical_requested:
        if not _HAS_CANONICAL:
            raise SystemExit("Canonical checkpoint requires canonical_orientation.py beside main_reg.py")
        else:
            orientation_info = canonical_orientation(points, class_name=ckpt.get("class_name"),
                                                     lhouse_canonical=bool(ckpt.get("lhouse_canonical", False)))
            if orientation_info["reliable"]:
                points = orientation_info["aligned_points"]
                pose_rz_deg = orientation_info["canonical_angle_deg"]
                canonical_applied = True
            elif orientation_info.get("status") != "SKIPPED_CLASS" and ckpt.get("canonical_align", False):
                raise SystemExit("Canonical orientation is uncertain for this cloud: "
                                 + orientation_info.get("reason", "unknown geometry"))

    if canonical_applied:
        bbox = np.ptp(points, axis=0)
        if aux_names and ckpt.get("canonical_aux_units", "legacy") == "physical":
            coordinates = getattr(args, "input_coordinates", None)
            info = record.get("pointCloudInfo", {}) if record is not None else {}
            if coordinates is None and record is not None:
                coordinates = ("normalized" if info.get("normalization") ==
                               "centered_by_mean_and_scaled_by_max_radius" else "physical")
            scale = (float(aux[aux_names.index("scale")]) if "scale" in aux_names
                     else info.get("scale", getattr(args, "scale", None)))
            bbox = physical_bbox_size(points, coordinates, scale, ckpt.get("canonical_bbox_mode", "full"))
        aux = aux_vector_for_bbox(aux, aux_names, bbox)

    if normalize:
        points = normalize_points(points)
    normals = predict_normals(source_points, points, args, ckpt,
                              -pose_rz_deg if canonical_applied else 0.0)
    points = sample_points(points, num_points, train=False, jitter_sigma=0.0, random_rotate=False, normals=normals)
    batch = torch.from_numpy(points).unsqueeze(0).to(device)
    aux_batch = torch.from_numpy(aux).unsqueeze(0).to(device)
    aux_norm = (aux_batch - aux_mean) / aux_std if aux_batch.shape[1] > 0 else aux_batch
    with torch.no_grad():
        pred_out = model(batch, aux_norm)
        pred_transformed = decode_targets(pred_out[:, :len(targets)], mean, std).squeeze(0).cpu().numpy()
    baseline = target_baseline_from_aux_values(targets, aux_names, aux, target_mode)
    pred = invert_target_transform_values(pred_transformed, baseline, target_mode)
    result = {
        "class": ckpt["class_name"],
        "params": prediction_params_for_plugin(ckpt["class_name"], targets, pred),
        "aux_features": {name: float(value) for name, value in zip(aux_names, aux)} if aux_names else {},
    }

    if canonical_requested:
        result["canonical_alignment"] = {
            "applied": canonical_applied,
            "status": orientation_info.get("status") if orientation_info else "UNAVAILABLE",
            "reason": orientation_info.get("reason") if orientation_info else "",
        }

    if pose_rz_deg is not None:
        result["poseRotateZ"] = pose_rz_deg
        if orientation_info is not None:
            result["orientation"] = {
                "verdict": orientation_info["verdict"],
                "pca_ratio": orientation_info["pca_ratio"],
                "peak_concentration": orientation_info["peak_concentration"],
                "n_segments": orientation_info["n_ransac_segments"],
                "reason": orientation_info["reason"],
            }

    print(json.dumps(result, ensure_ascii=False, indent=2))


# ============================================================================
# CLI
# ============================================================================

def parse_args():
    parser = argparse.ArgumentParser(
        description="PTv3 point-cloud parameter regression")

    # ── Mode ──
    parser.add_argument("--mode", choices=["train", "test", "predict"], required=True)

    # ── Data ──
    parser.add_argument("--data_root", default="E:/pointnet/datasets_aug")
    parser.add_argument("--metadata", default=None)
    parser.add_argument("--class_name", default=None)
    parser.add_argument("--targets", nargs="+", default=None)
    parser.add_argument("--target_mode", choices=["raw", "bbox_residual"], default="raw",
                        help="raw predicts target values directly; bbox_residual predicts target - bbox baseline.")
    parser.add_argument("--split", default="test")
    parser.add_argument("--test_orientation", choices=["metadata", "geometry"], default="metadata",
                        help="Test using metadata yaw or deployment geometry; geometry results use a separate file suffix.")
    parser.add_argument("--input", help="Input txt for predict mode")

    # ── Logging ──
    parser.add_argument("--log_dir", default="logs/ptv3_reg_test")
    parser.add_argument("--eval_output_dir", default=None,
                        help="Test-only report directory; must be empty and differ from --log_dir.")

    # ── Training hyperparams ──
    parser.add_argument("--num_points", type=int, default=2048)
    parser.add_argument("--cache_points", action=argparse.BooleanOptionalAction, default=True,
                        help="Cache fixed point preprocessing in RAM; random training augmentation stays online.")
    parser.add_argument("--num_workers", type=int, default=0,
                        help="CPU data-loading processes; positive values use persistent spawned workers.")
    parser.add_argument("--normal_workers", type=int, default=0 if os.name == "nt" else 2,
                        help="CPU normal-preparation processes; 0 or 1 uses serial computation.")
    parser.add_argument("--normal_backend", choices=["numpy", "pcl"], default="numpy",
                        help="PCL uses persistent source-frame normals; test/predict follow the checkpoint.")
    parser.add_argument("--normal_threads", type=int, default=4,
                        help="OpenMP threads per PCL cloud; normal_workers applies only to NumPy.")
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--batch_size", type=int, default=16)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--weight_decay", type=float, default=1e-4)
    parser.add_argument("--smooth_l1_beta", type=float, default=1.0)
    parser.add_argument("--target_weights", nargs="*", default=[],
                        help="Optional per-target loss weights, e.g. length=1.5 width=1.5 default=1.0")
    parser.add_argument("--relative_loss_weight", type=float, default=0.0,
                        help="Add raw-space relative error with this multiplier; 0 keeps legacy loss.")
    parser.add_argument("--relative_loss_eps", type=float, default=1e-3)
    parser.add_argument("--indented_symmetry_180", action="store_true",
                        help="Train IndentedCuboid with a per-sample 180-degree margin-swap loss and selection metric.")
    parser.add_argument("--asym_ridge_symmetry_180", action="store_true",
                        help="Experimental AsymmetricGableHouse q/1-q loss; deployment still needs pose disambiguation.")
    parser.add_argument("--gabled_ridge_symmetry_180", action="store_true",
                        help="Experimental GabledRoof q/1-q loss; deployment still needs pose disambiguation.")
    parser.add_argument("--geom_aux_targets", nargs="*", default=[],
                        help="Optional geometry auxiliary regression targets, e.g. geom_footprint_long geom_total_height")
    parser.add_argument("--geom_loss_weight", type=float, default=0.0,
                        help="Weight for optional geometry auxiliary regression loss.")
    parser.add_argument("--lhouse_corner_loss_weight", type=float, default=0.0,
                        help="Optional LHouse primary-output notch-position loss; 0 preserves the existing recipe.")
    parser.add_argument("--truncated_scale_balanced", action="store_true",
                        help="TruncatedPyramidRoof-only experiment: physical-dimension relative loss and equal-band validation selection.")
    parser.add_argument("--train_jitter_sigma", type=float, default=0.003)
    parser.add_argument("--random_rotate", action="store_true")
    parser.add_argument("--aux_features", nargs="+",
                        default=["bbox_x", "bbox_y", "bbox_z", "scale",
                                 "bbox_volume", "aspect_ratio_xy"])
    parser.add_argument("--circular_targets", nargs="*", default=[])
    parser.add_argument("--no_aux", action="store_true")
    parser.add_argument("--bbox_x", type=float, default=None)
    parser.add_argument("--bbox_y", type=float, default=None)
    parser.add_argument("--bbox_z", type=float, default=None)
    parser.add_argument("--scale", type=float, default=None)
    parser.add_argument("--input_coordinates", choices=["physical", "normalized"], default=None,
                        help="Predict input coordinate units; required for physical-aux checkpoints without metadata")
    parser.add_argument("--normalize_points", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--canonical_align", action="store_true",
                        help="Canonicalize training/test samples from metadata rz and apply the checkpoint frame at inference")
    parser.add_argument("--lhouse_canonical", action="store_true",
                        help="Experimental LHouse full-direction frame; stored in the checkpoint, legacy models stay unchanged")
    parser.add_argument("--canonical_aux_units", choices=["legacy", "physical"], default="legacy",
                        help="Training auxiliary bbox units; test/predict use the saved checkpoint setting")
    parser.add_argument("--lhouse_robust_bbox", action="store_true",
                        help="LHouse-only XY 0.005/0.995 bbox quantiles; checkpoint controls test/predict, Z stays full-span")
    parser.add_argument("--cpu", action="store_true")
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--backbone", choices=["ptv3"], default="ptv3")
    parser.add_argument("--ptv3_repo", default=None,
                        help="Official standalone PTv3 repository (or PTV3_ROOT environment variable)")
    parser.add_argument("--ptv3_grid_size", type=float, default=0.02,
                        help="Voxel size in normalized input coordinates")
    parser.add_argument("--ptv3_patch_size", type=int, default=128)
    parser.add_argument("--ptv3_flash", action="store_true")
    parser.add_argument("--ptv3_dropout", type=float, default=0.1)

    parser.add_argument("--use_normals", action="store_true",
                        help="Append kNN-PCA per-point normals as extra channels (xyz→xyz+nx ny nz).")

    args = parser.parse_args()

    if args.num_workers < 0:
        raise SystemExit("--num_workers must be non-negative")
    if args.normal_workers < 0:
        raise SystemExit("--normal_workers must be non-negative")
    if args.normal_threads < 1:
        raise SystemExit("--normal_threads must be positive")
    if args.eval_output_dir and args.mode != "test":
        raise SystemExit("--eval_output_dir is only supported in test mode")
    if args.truncated_scale_balanced:
        required = {*TRUNCATED_DIMENSION_TARGETS, "geom_top_long_ratio", "geom_top_short_ratio", "wallRatio"}
        if (args.mode != "train" or args.class_name != "TruncatedPyramidRoof"
                or not args.canonical_align or args.target_mode != "bbox_residual"
                or len(args.targets or []) != len(required) or set(args.targets or []) != required):
            raise SystemExit("--truncated_scale_balanced requires canonical TruncatedPyramidRoof training with the six bbox_residual targets")
        if (args.relative_loss_weight != 0 or args.lhouse_corner_loss_weight != 0
                or args.indented_symmetry_180 or args.asym_ridge_symmetry_180 or args.gabled_ridge_symmetry_180):
            raise SystemExit("Truncated scale experiment cannot combine with generic relative, corner or symmetry losses")
        if not np.isfinite(args.relative_loss_eps) or args.relative_loss_eps <= 0:
            raise SystemExit("Truncated scale experiment requires a finite positive relative_loss_eps")
        weights = parse_target_weight_specs(args.targets, args.target_weights)
        if sum(weights[args.targets.index(name)] for name in TRUNCATED_DIMENSION_TARGETS) <= 0:
            raise SystemExit("Truncated scale experiment requires a positive physical-dimension weight")
    if args.mode == "train":
        if not args.class_name:
            raise SystemExit("--class_name is required for train mode")
        if not args.targets:
            raise SystemExit("--targets is required for train mode")
        if args.lhouse_canonical and (args.class_name != "LHouse" or not args.canonical_align):
            raise SystemExit("--lhouse_canonical requires LHouse and --canonical_align")
        if args.canonical_aux_units == "physical" and not args.canonical_align:
            raise SystemExit("Physical canonical auxiliary features require --canonical_align")
        if args.lhouse_robust_bbox:
            try:
                validate_canonical_bbox_mode(LHOUSE_ROBUST_BBOX_MODE, args.class_name,
                                             args.canonical_align, args.lhouse_canonical, args.canonical_aux_units)
            except ValueError as exc:
                raise SystemExit(str(exc)) from exc
            if args.lhouse_corner_loss_weight != 0:
                raise SystemExit("Run the robust-bbox comparison without LHouse corner loss")
        if not np.isfinite(args.lhouse_corner_loss_weight) or args.lhouse_corner_loss_weight < 0:
            raise SystemExit("--lhouse_corner_loss_weight must be finite and non-negative")
        if args.lhouse_corner_loss_weight > 0:
            required = {"outerLength", "outerWidth", "cutoutLengthRatio", "cutoutWidthRatio", "height"}
            if (args.class_name != "LHouse" or not args.lhouse_canonical
                    or args.canonical_aux_units != "physical" or args.target_mode != "raw"
                    or len(args.targets) != len(required) or set(args.targets) != required):
                raise SystemExit("LHouse corner loss requires the five raw LHouse targets, --lhouse_canonical and physical auxiliary units")
        if not args.metadata:
            args.metadata = str(Path(args.data_root) / "metadata" / "sample_params.json")
        if args.canonical_align and args.random_rotate:
            raise SystemExit("--canonical_align cannot be combined with --random_rotate")
        if args.canonical_align and (args.indented_symmetry_180
                                     or args.asym_ridge_symmetry_180
                                     or args.gabled_ridge_symmetry_180):
            raise SystemExit("Canonical targets are direction-aware; disable the 180-degree symmetry losses")
        if args.indented_symmetry_180:
            if args.circular_targets:
                raise SystemExit("--indented_symmetry_180 does not support circular targets")
            try:
                indented_180_permutation(args.class_name, args.targets)
            except ValueError as exc:
                raise SystemExit(str(exc)) from exc
        if args.asym_ridge_symmetry_180:
            if args.indented_symmetry_180 or args.circular_targets or args.target_mode != "raw":
                raise SystemExit("Ridge symmetry requires raw targets and cannot combine with other symmetry/circular modes")
            try:
                asym_ridge_180_index(args.class_name, args.targets)
            except ValueError as exc:
                raise SystemExit(str(exc)) from exc
        if args.gabled_ridge_symmetry_180:
            if args.class_name != "GabledRoof" or args.asym_ridge_symmetry_180 or args.indented_symmetry_180 or args.circular_targets or args.target_mode != "raw":
                raise SystemExit("Gabled ridge symmetry requires GabledRoof raw targets and no other symmetry/circular mode")
            try:
                roof_ridge_180_index(args.class_name, args.targets)
            except ValueError as exc:
                raise SystemExit(str(exc)) from exc
    elif args.mode == "test":
        if not args.data_root:
            raise SystemExit("--data_root is required for test mode")
    elif args.mode == "predict":
        if not args.input:
            raise SystemExit("--input is required for predict mode")
    return args


def main():
    args = parse_args()
    if args.seed is not None:
        random.seed(args.seed)
        np.random.seed(args.seed)
        torch.manual_seed(args.seed)
        if torch.cuda.is_available():
            torch.cuda.manual_seed_all(args.seed)
    if args.mode != "predict":
        print(f"=== PTv3 Regression | mode={args.mode} ===", flush=True)
    if args.mode == "train":
        train(args)
    elif args.mode == "test":
        test(args)
    elif args.mode == "predict":
        predict(args)


if __name__ == "__main__":
    main()
