"""Classify one point cloud, then run the matching PTv3 regression model."""

from __future__ import annotations

from primitive_names import canonical_class_name, regression_slug

import argparse
import json
from pathlib import Path
from types import SimpleNamespace
from typing import Dict, List, Optional, Tuple

import numpy as np
import torch

import main as cls_main
import main_reg as reg_main
from train_ptv3_reg import RECIPES


RECIPE_BY_CLASS = {name: (slug, targets, weights) for name, slug, targets, weights in RECIPES}


def device_from_args(args) -> torch.device:
    if args.cpu:
        return torch.device("cpu")
    if not torch.cuda.is_available():
        raise RuntimeError("CUDA is required for PTv3 prediction. Use a GPU environment.")
    return torch.device("cuda")


def regression_log_dir(log_root: Path, class_name: str, run_tag: str) -> Path:
    class_name = canonical_class_name(class_name)
    if class_name not in RECIPE_BY_CLASS:
        known = ", ".join(sorted(RECIPE_BY_CLASS))
        raise KeyError(f"Unknown class {class_name!r}. Known classes: {known}")
    slug = regression_slug(class_name, run_tag)
    return log_root / f"ptv3_reg_{slug}_{run_tag}_normals"


def load_classifier(args, device: torch.device):
    ckpt = cls_main.load_checkpoint(Path(args.cls_log_dir), device)
    class_names = [canonical_class_name(name) for name in ckpt["class_names"]]
    cls_args = SimpleNamespace(
        num_points=int(ckpt.get("num_points", args.num_points)),
        normalize_points=bool(ckpt.get("normalize_points", True)),
        use_normals=bool(ckpt.get("use_normals", False)),
        backbone="ptv3",
        ptv3_repo=args.ptv3_repo,
        ptv3_grid_size=0.02,
        ptv3_patch_size=128,
        ptv3_flash=False,
        ptv3_dropout=0.1,
    )
    config = ckpt.get("backbone_config", {})
    cls_args.ptv3_grid_size = float(config.get("grid_size", cls_args.ptv3_grid_size))
    cls_args.ptv3_patch_size = int(config.get("patch_size", cls_args.ptv3_patch_size))
    cls_args.ptv3_flash = bool(config.get("flash", cls_args.ptv3_flash))
    cls_args.ptv3_dropout = float(config.get("dropout", cls_args.ptv3_dropout))
    cls_args.spatial_pyramid = bool(config.get("spatial_pyramid", False))
    cls_args.canonical_align = bool(config.get("canonical_align", cls_args.spatial_pyramid))
    if config.get("repo") and cls_args.ptv3_repo is None:
        cls_args.ptv3_repo = config.get("repo")

    model = cls_main.build_model(cls_args, len(class_names)).to(device)
    model.load_state_dict(ckpt["state_dict"])
    model.eval()
    return model, ckpt, class_names, cls_args


def classify_input(model, class_names: List[str], cls_args, input_path: Path,
                   device: torch.device, topk: int, checkpoint: dict) -> List[dict]:
    source_points = cls_main.load_txt_points(input_path)
    points = cls_main.normalize_points(source_points) if cls_args.normalize_points else source_points
    normals = cls_main.predict_normals(source_points, points, cls_args, checkpoint)
    points = cls_main.sample_classification_points(points, cls_args.num_points, False, 0.0, False, normals,
                                                   cls_main.checkpoint_sampling_mode(checkpoint))
    tensor = torch.from_numpy(points).unsqueeze(0).to(device).float()
    with torch.no_grad():
        prob = torch.softmax(model(tensor), dim=1)[0].detach().cpu().numpy()
    order = np.argsort(-prob)[:max(1, topk)]
    return [{"class": class_names[i], "prob": float(prob[i])} for i in order]


def cloud_aux_from_bbox(input_path: Path, aux_names: List[str]) -> Tuple[np.ndarray, Dict[str, float]]:
    points = reg_main.load_txt_points(input_path)
    bbox = points.max(axis=0) - points.min(axis=0)
    centered = points - points.mean(axis=0, keepdims=True)
    scale = float(np.linalg.norm(centered, axis=1).max())
    if scale <= 1e-6:
        scale = 1.0
    record = {
        "pointCloudInfo": {
            "bboxSize": [float(bbox[0]), float(bbox[1]), float(bbox[2])],
            "scale": scale,
        }
    }
    aux = reg_main.aux_vector(record, aux_names)
    aux_info = {
        "bbox_x": float(bbox[0]),
        "bbox_y": float(bbox[1]),
        "bbox_z": float(bbox[2]),
        "scale": scale,
    }
    return aux, aux_info


def aux_for_input(args, input_path: Path, ckpt: dict, aux_names: List[str]) -> Tuple[np.ndarray, str, Dict[str, float]]:
    if not aux_names:
        return np.zeros((0,), dtype=np.float32), "none", {}

    metadata = Path(args.metadata) if args.metadata else Path(args.data_root) / "metadata" / "sample_params.json"
    if metadata.exists():
        records = reg_main.load_metadata(metadata)
        record = reg_main.find_metadata_record(records, args.data_root, str(input_path))
        if record is not None:
            aux = reg_main.aux_vector(record, aux_names)
            return aux, f"metadata:{metadata}", {name: float(value) for name, value in zip(aux_names, aux)}

    manual = SimpleNamespace(
        bbox_x=args.bbox_x,
        bbox_y=args.bbox_y,
        bbox_z=args.bbox_z,
        scale=args.scale,
    )
    aux = reg_main.aux_vector_from_args(manual, aux_names)
    if aux is not None:
        return aux, "manual_bbox_args", {name: float(value) for name, value in zip(aux_names, aux)}

    if args.estimate_aux_from_cloud:
        aux, bbox_info = cloud_aux_from_bbox(input_path, aux_names)
        return aux, "estimated_from_input_bbox", {**bbox_info, **{name: float(value) for name, value in zip(aux_names, aux)}}

    raise RuntimeError(
        "Regression aux features are required. Provide --metadata, --bbox_x/y/z --scale, "
        "or enable --estimate_aux_from_cloud."
    )


def regression_args(args, log_dir: Path) -> SimpleNamespace:
    return SimpleNamespace(
        log_dir=str(log_dir),
        input=str(args.input),
        data_root=str(args.data_root),
        metadata=args.metadata,
        class_name=None,
        num_points=args.num_points,
        normalize_points=True,
        cpu=args.cpu,
        backbone="ptv3",
        ptv3_repo=args.ptv3_repo,
        ptv3_grid_size=0.02,
        ptv3_patch_size=128,
        ptv3_flash=False,
        ptv3_dropout=0.1,
        circular_targets=[],
        canonical_align=args.canonical_align,
        bbox_x=args.bbox_x,
        bbox_y=args.bbox_y,
        bbox_z=args.bbox_z,
        scale=args.scale,
    )


def regress_input(args, class_name: str, input_path: Path, device: torch.device) -> dict:
    log_dir = regression_log_dir(Path(args.reg_log_root), class_name, args.run_tag)
    if not (log_dir / "best_model.pth").is_file():
        raise FileNotFoundError(f"Missing regression checkpoint: {log_dir / 'best_model.pth'}")

    r_args = regression_args(args, log_dir)
    model, ckpt, targets, mean, std, aux_mean, aux_std = reg_main.load_checkpoint(log_dir, device, r_args)
    num_points = int(ckpt.get("num_points", r_args.num_points))
    normalize = bool(ckpt.get("normalize_points", True))
    use_normals = bool(ckpt.get("use_normals", False))
    aux_names = list(ckpt.get("aux_names", []))
    aux, aux_source, aux_values = aux_for_input(args, input_path, ckpt, aux_names)

    points = reg_main.load_txt_points(input_path)
    orientation_info = None
    pose_rz_deg = None
    if args.canonical_align:
        if not reg_main._HAS_CANONICAL:
            raise RuntimeError("canonical_orientation.py is not available.")
        orientation_info = reg_main.canonical_orientation(points, class_name=class_name)
        if orientation_info["reliable"]:
            points = orientation_info["aligned_points"]
            pose_rz_deg = float(orientation_info["canonical_angle_deg"])

    if normalize:
        points = reg_main.normalize_points(points)
    normals = reg_main.estimate_normals(points) if use_normals else None
    points = reg_main.sample_points(points, num_points, train=False, jitter_sigma=0.0,
                                    random_rotate=False, normals=normals)
    batch = torch.from_numpy(points).unsqueeze(0).to(device).float()
    aux_batch = torch.from_numpy(aux).unsqueeze(0).to(device).float()
    aux_norm = (aux_batch - aux_mean) / aux_std if aux_batch.shape[1] > 0 else aux_batch
    with torch.no_grad():
        pred_out = model(batch, aux_norm)
        pred = reg_main.decode_targets(pred_out[:, :len(targets)], mean, std).squeeze(0).cpu().numpy()

    result = {
        "checkpoint": str(log_dir / "best_model.pth"),
        "class": class_name,
        "params": {name: float(value) for name, value in zip(targets, pred)},
        "aux_source": aux_source,
        "aux_features": aux_values,
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
    return result


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="Input point cloud txt")
    parser.add_argument("--data_root", default="/home/xubo/pointnet/datasets_aug")
    parser.add_argument("--metadata", default=None)
    parser.add_argument("--cls_log_dir", default="/home/xubo/pointnet/ptv3_simple/logs/ptv3_cls_v10")
    parser.add_argument("--reg_log_root", default="/home/xubo/pointnet/ptv3_simple/logs")
    parser.add_argument("--run_tag", default="v10")
    parser.add_argument("--ptv3_repo", default=None)
    parser.add_argument("--num_points", type=int, default=2048)
    parser.add_argument("--topk", type=int, default=3)
    parser.add_argument("--force_class", type=canonical_class_name, choices=sorted(RECIPE_BY_CLASS), default=None,
                        help="Skip classifier result for regression and use this class.")
    parser.add_argument("--output", default=None, help="Optional JSON output path")
    parser.add_argument("--bbox_x", type=float, default=None)
    parser.add_argument("--bbox_y", type=float, default=None)
    parser.add_argument("--bbox_z", type=float, default=None)
    parser.add_argument("--scale", type=float, default=None)
    parser.add_argument("--estimate_aux_from_cloud", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--canonical_align", action="store_true")
    parser.add_argument("--cpu", action="store_true")
    args = parser.parse_args()
    if args.num_points < 1 or args.topk < 1:
        parser.error("--num_points and --topk must be positive")
    return args


def main():
    args = parse_args()
    input_path = Path(args.input)
    if not input_path.is_file():
        raise FileNotFoundError(f"Missing input point cloud: {input_path}")

    device = device_from_args(args)
    cls_model, cls_ckpt, class_names, cls_args = load_classifier(args, device)
    topk = classify_input(cls_model, class_names, cls_args, input_path, device, args.topk, cls_ckpt)
    chosen = args.force_class or topk[0]["class"]
    regression = regress_input(args, chosen, input_path, device)

    result = {
        "input": str(input_path),
        "device": str(device),
        "classification_checkpoint": str(Path(args.cls_log_dir) / "best_model.pth"),
        "classification_topk": topk,
        "chosen_class": chosen,
        "chosen_class_source": "force_class" if args.force_class else "classifier_top1",
        "regression": regression,
    }
    text = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
