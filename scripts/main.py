#!/usr/bin/env python
"""Standalone PTv3 point-cloud classification: train, test, and predict.

The backbone follows the official detached PointTransformerV3 source in
third_party/PointTransformerV3/model.py.  We run PointTransformerV3 with
cls_mode=True, pool the encoded sparse point features per cloud, and train a
small classification head for this dataset layout:

  data_root/train/<ClassName>/*.txt
  data_root/val/<ClassName>/*.txt
  data_root/test/<ClassName>/*.txt
"""

from primitive_names import canonical_class_name, class_directory

import argparse
import csv
import json
import math
import os
import random
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, Dataset

from main_reg import (data_loading_config, load_txt_points, make_data_loader, normal_kwargs,
                      normalize_points, precompute_normals, predict_normals, sample_points)
from model_ptv3 import PTv3Classifier


SPLITS = ("train", "val", "test")


def set_seed(seed: Optional[int]) -> None:
    if seed is None:
        return
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def discover_class_names(data_root: Path) -> List[str]:
    class_file = data_root / "metadata" / "class_names.txt"
    if class_file.is_file():
        names = [canonical_class_name(line.strip()) for line in class_file.read_text(encoding="utf-8").splitlines()
                 if line.strip() and line.strip() != "TriPrismPyramid"]
        if names:
            if len(names) != len(set(names)):
                raise ValueError("Duplicate old/new class aliases in class_names.txt")
            return names

    seen = []
    for split in SPLITS:
        split_dir = data_root / split
        if not split_dir.is_dir():
            continue
        for child in sorted(split_dir.iterdir(), key=lambda p: p.name.lower()):
            if child.is_dir() and child.name != "TriPrismPyramid" and canonical_class_name(child.name) not in seen:
                seen.append(canonical_class_name(child.name))
    if len(seen) < 2:
        raise RuntimeError(f"Need at least two class folders under {data_root}/train|val|test")
    return seen


def split_files(data_root: Path, split: str, class_names: List[str]) -> List[Tuple[Path, int]]:
    items: List[Tuple[Path, int]] = []
    for label, class_name in enumerate(class_names):
        class_dir = class_directory(data_root, split, class_name)
        if not class_dir.is_dir():
            continue
        files = sorted(class_dir.glob("*.txt"), key=lambda p: p.name)
        items.extend((path, label) for path in files)
    if not items:
        raise RuntimeError(f"No txt samples found in split={split!r} under {data_root}")
    return items


class PointCloudClassificationDataset(Dataset):
    def __init__(self, data_root: Path, split: str, class_names: List[str],
                 num_points: int, train: bool = False, normalize: bool = True,
                 jitter_sigma: float = 0.0, random_rotate: bool = False,
                 use_normals: bool = False, cache_points: bool = True, normal_workers: int = 0,
                 normal_backend: str = "numpy", normal_threads: int = 4):
        self.data_root = Path(data_root)
        self.split = split
        self.class_names = list(class_names)
        self.num_points = int(num_points)
        self.train = bool(train)
        self.normalize = bool(normalize)
        self.jitter_sigma = float(jitter_sigma)
        self.random_rotate = bool(random_rotate)
        self.use_normals = bool(use_normals)
        self.cache_points = bool(cache_points)
        self.samples = split_files(self.data_root, split, self.class_names)
        self._points_cache: Dict[str, np.ndarray] = {}
        self._normals_cache: Dict[str, np.ndarray] = {}

        if self.cache_points or self.use_normals:
            clouds = ((path, self._prepare_points(path)) for path, _ in self.samples)
            prepared = (precompute_normals(clouds, len(self.samples), split, normal_workers,
                                           normal_backend=normal_backend, normal_threads=normal_threads,
                                           data_root=self.data_root, source_points=self.normalize)
                        if self.use_normals else ((path, points, None) for path, points in clouds))
            for path, points, normals in prepared:
                if self.cache_points:
                    points.setflags(write=False)
                    self._points_cache[str(path)] = points
                if self.use_normals:
                    normals.setflags(write=False)
                    self._normals_cache[str(path)] = normals
        if self.cache_points:
            size_mib = sum(points.nbytes for points in self._points_cache.values()) / (1024 * 1024)
            print(f"Cached points {split}: {len(self.samples)} clouds ({size_mib:.1f} MiB)", flush=True)

    def __len__(self) -> int:
        return len(self.samples)

    def __getitem__(self, index: int):
        path, label = self.samples[index]
        points = self._points_cache.get(str(path))
        if points is None:
            points = self._prepare_points(path)
        normals = self._normals_cache.get(str(path)) if self.use_normals else None
        points = sample_points(points, self.num_points, self.train,
                               self.jitter_sigma, self.random_rotate, normals)
        return torch.from_numpy(points), torch.tensor(label, dtype=torch.long), str(path)

    def _prepare_points(self, path: Path) -> np.ndarray:
        points = load_txt_points(path)
        return normalize_points(points) if self.normalize else points


def make_loader(dataset: Dataset, batch_size: int, shuffle: bool, num_workers: int,
                device: torch.device, seed: Optional[int] = None) -> DataLoader:
    return make_data_loader(dataset, batch_size, shuffle, device, num_workers, seed)


def build_model(args, num_classes: int) -> PTv3Classifier:
    return PTv3Classifier(
        num_classes=num_classes,
        in_channels=6 if args.use_normals else 3,
        grid_size=args.ptv3_grid_size,
        patch_size=args.ptv3_patch_size,
        enable_flash=args.ptv3_flash,
        dropout=args.ptv3_dropout,
        repo_path=args.ptv3_repo,
    )


def accuracy_from_logits(logits: torch.Tensor, labels: torch.Tensor) -> Tuple[int, int]:
    pred = logits.argmax(dim=1)
    correct = int((pred == labels).sum().item())
    return correct, int(labels.numel())


def evaluate(model: nn.Module, loader: DataLoader, device: torch.device,
             criterion: nn.Module) -> Tuple[float, float, List[int], List[int], List[str]]:
    model.eval()
    total_loss = 0.0
    total_correct = 0
    total_count = 0
    y_true: List[int] = []
    y_pred: List[int] = []
    paths: List[str] = []
    with torch.no_grad():
        for points, labels, batch_paths in loader:
            points = points.to(device, non_blocking=True).float()
            labels = labels.to(device, non_blocking=True)
            logits = model(points)
            loss = criterion(logits, labels)
            correct, count = accuracy_from_logits(logits, labels)
            total_loss += float(loss.item()) * count
            total_correct += correct
            total_count += count
            y_true.extend(labels.cpu().tolist())
            y_pred.extend(logits.argmax(dim=1).cpu().tolist())
            paths.extend(list(batch_paths))
    avg_loss = total_loss / max(total_count, 1)
    acc = total_correct / max(total_count, 1)
    return avg_loss, acc, y_true, y_pred, paths


def classification_report(class_names: List[str], y_true: List[int], y_pred: List[int]):
    rows = []
    for index, name in enumerate(class_names):
        tp = sum(1 for t, p in zip(y_true, y_pred) if t == index and p == index)
        fp = sum(1 for t, p in zip(y_true, y_pred) if t != index and p == index)
        fn = sum(1 for t, p in zip(y_true, y_pred) if t == index and p != index)
        support = sum(1 for t in y_true if t == index)
        precision = tp / max(tp + fp, 1)
        recall = tp / max(tp + fn, 1)
        f1 = 2 * precision * recall / max(precision + recall, 1e-12)
        rows.append({
            "class": name,
            "precision": precision,
            "recall": recall,
            "f1": f1,
            "support": support,
        })
    return rows


def confusion_matrix(num_classes: int, y_true: List[int], y_pred: List[int]) -> np.ndarray:
    matrix = np.zeros((num_classes, num_classes), dtype=np.int64)
    for t, p in zip(y_true, y_pred):
        matrix[int(t), int(p)] += 1
    return matrix


def write_metrics(log_dir: Path, split: str, class_names: List[str],
                  y_true: List[int], y_pred: List[int], paths: List[str]) -> None:
    report_rows = classification_report(class_names, y_true, y_pred)
    report_path = log_dir / f"classification_report_{split}.csv"
    with report_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["class", "precision", "recall", "f1", "support"])
        writer.writeheader()
        writer.writerows(report_rows)

    matrix = confusion_matrix(len(class_names), y_true, y_pred)
    matrix_path = log_dir / f"confusion_matrix_{split}.csv"
    with matrix_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["actual\\pred", *class_names])
        for index, name in enumerate(class_names):
            writer.writerow([name, *matrix[index].tolist()])

    pred_path = log_dir / f"predictions_{split}.csv"
    with pred_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["file", "true", "pred", "correct"])
        for path, true_i, pred_i in zip(paths, y_true, y_pred):
            writer.writerow([path, class_names[true_i], class_names[pred_i], int(true_i == pred_i)])


def save_config(log_dir: Path, args, class_names: List[str]) -> None:
    config = {
        "class_names": class_names,
        "num_points": args.num_points,
        "normalize_points": args.normalize_points,
        "use_normals": args.use_normals,
        "normal_backend": getattr(args, "normal_backend", "numpy"),
        "data_loading": data_loading_config(args),
        "backbone": "ptv3",
        "backbone_config": {
            "grid_size": args.ptv3_grid_size,
            "patch_size": args.ptv3_patch_size,
            "flash": args.ptv3_flash,
            "dropout": args.ptv3_dropout,
            "repo": args.ptv3_repo,
        },
        "train": {
            "amp": bool(getattr(args, "amp", False)),
            "amp_dtype": "float16" if getattr(args, "amp", False) else None,
            "batch_size": args.batch_size,
            "epochs": args.epochs,
            "lr": args.lr,
            "weight_decay": args.weight_decay,
            "train_jitter_sigma": args.train_jitter_sigma,
            "random_rotate": args.random_rotate,
            "seed": args.seed,
        },
    }
    (log_dir / "classification_config.json").write_text(
        json.dumps(config, ensure_ascii=False, indent=2), encoding="utf-8")


def load_checkpoint(log_dir: Path, device: torch.device):
    ckpt_path = log_dir / "best_model.pth"
    if not ckpt_path.is_file():
        raise FileNotFoundError(f"Missing checkpoint: {ckpt_path}")
    try:
        return torch.load(ckpt_path, map_location=device, weights_only=False)
    except TypeError:
        return torch.load(ckpt_path, map_location=device)


def train(args) -> None:
    data_root = Path(args.data_root)
    log_dir = Path(args.log_dir)
    device = torch.device("cpu" if args.cpu or not torch.cuda.is_available() else "cuda")
    amp_enabled = bool(getattr(args, "amp", False))
    if amp_enabled and device.type != "cuda":
        raise RuntimeError("--amp requires CUDA. Omit --amp for CPU/FP32 training.")
    if log_dir.exists() and any(log_dir.iterdir()) and not args.overwrite:
        raise SystemExit(f"Output already contains files: {log_dir}. Use --overwrite or a new --log_dir.")
    log_dir.mkdir(parents=True, exist_ok=True)

    class_names = discover_class_names(data_root)
    train_set = PointCloudClassificationDataset(
        data_root, "train", class_names, args.num_points, train=True,
        normalize=args.normalize_points, jitter_sigma=args.train_jitter_sigma,
        random_rotate=args.random_rotate, use_normals=args.use_normals, cache_points=args.cache_points,
        normal_workers=args.normal_workers, **normal_kwargs(args))
    val_set = PointCloudClassificationDataset(
        data_root, "val", class_names, args.num_points, train=False,
        normalize=args.normalize_points, use_normals=args.use_normals, cache_points=args.cache_points,
        normal_workers=args.normal_workers, **normal_kwargs(args))

    train_loader = make_loader(train_set, args.batch_size, True, args.num_workers, device, args.seed)
    val_loader = make_loader(val_set, args.batch_size, False, args.num_workers, device,
                             args.seed + 1 if args.seed is not None else None)
    model = build_model(args, len(class_names)).to(device)
    criterion = nn.CrossEntropyLoss(label_smoothing=args.label_smoothing)
    optimizer = optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)
    # This CUDA GradScaler API also supports the Ubuntu PyTorch 2.1 environment.
    scaler = torch.cuda.amp.GradScaler() if amp_enabled else None

    save_config(log_dir, args, class_names)
    best_acc = -1.0
    best_loss = float("inf")

    print(f"=== PTv3 Classification | classes={len(class_names)} | device={device} ===", flush=True)
    print("Class order: " + ", ".join(class_names), flush=True)
    print(f"Train samples={len(train_set)} Val samples={len(val_set)}", flush=True)
    print(f"Data loading: {data_loading_config(args)} | pin_memory={device.type == 'cuda'}", flush=True)
    print(f"Training precision: {'AMP float16' if amp_enabled else 'FP32'} | validation: FP32", flush=True)

    for epoch in range(1, args.epochs + 1):
        if device.type == "cuda":
            torch.cuda.synchronize(device)
            torch.cuda.reset_peak_memory_stats(device)
        epoch_started = time.perf_counter()
        model.train()
        train_loss = 0.0
        train_correct = 0
        train_count = 0
        for points, labels, _ in train_loader:
            points = points.to(device, non_blocking=True).float()
            labels = labels.to(device, non_blocking=True)
            optimizer.zero_grad(set_to_none=True)
            with torch.autocast(device_type="cuda", dtype=torch.float16, enabled=amp_enabled):
                logits = model(points)
                loss = criterion(logits, labels)
            if scaler is None:
                loss.backward()
            else:
                scaler.scale(loss).backward()
            if args.grad_clip > 0:
                if scaler is not None:
                    scaler.unscale_(optimizer)
                nn.utils.clip_grad_norm_(model.parameters(), args.grad_clip)
            if scaler is None:
                optimizer.step()
            else:
                scaler.step(optimizer)
                scaler.update()
            correct, count = accuracy_from_logits(logits, labels)
            loss_value = float(loss.item())
            if amp_enabled and not math.isfinite(loss_value):
                raise RuntimeError("AMP training loss became non-finite. Compare with --no-amp before continuing.")
            train_loss += loss_value * count
            train_correct += correct
            train_count += count
        scheduler.step()

        val_loss, val_acc, _, _, _ = evaluate(model, val_loader, device, criterion)
        if amp_enabled and not math.isfinite(val_loss):
            raise RuntimeError("Non-finite validation loss after AMP training; checkpoint not saved.")
        train_loss /= max(train_count, 1)
        train_acc = train_correct / max(train_count, 1)
        lr = optimizer.param_groups[0]["lr"]
        print(f"Epoch {epoch:03d}/{args.epochs} | lr={lr:.2e} | "
              f"train_loss={train_loss:.4f} train_acc={train_acc:.4f} | "
              f"val_loss={val_loss:.4f} val_acc={val_acc:.4f}", flush=True)

        improved = val_acc > best_acc + 1e-12 or (abs(val_acc - best_acc) <= 1e-12 and val_loss < best_loss)
        if improved:
            best_acc = val_acc
            best_loss = val_loss
            torch.save({
                "state_dict": model.state_dict(),
                "class_names": class_names,
                "num_points": args.num_points,
                "normalize_points": args.normalize_points,
                "use_normals": args.use_normals,
                "normal_backend": getattr(args, "normal_backend", "numpy"),
                "amp": amp_enabled,
                "data_loading": data_loading_config(args),
                "backbone": "ptv3",
                "backbone_config": {
                    "grid_size": args.ptv3_grid_size,
                    "patch_size": args.ptv3_patch_size,
                    "flash": args.ptv3_flash,
                    "dropout": args.ptv3_dropout,
                    "repo": args.ptv3_repo,
                },
                "epoch": epoch,
                "val_acc": val_acc,
                "val_loss": val_loss,
            }, log_dir / "best_model.pth")
            print(f"  -> saved best_model.pth (val_acc={val_acc:.4f}, val_loss={val_loss:.4f})", flush=True)

        memory_text = "peak_gpu_allocated=N/A peak_gpu_reserved=N/A"
        if device.type == "cuda":
            torch.cuda.synchronize(device)
            allocated = torch.cuda.max_memory_allocated(device) / (1024 ** 2)
            reserved = torch.cuda.max_memory_reserved(device) / (1024 ** 2)
            memory_text = f"peak_gpu_allocated={allocated:.1f} MiB peak_gpu_reserved={reserved:.1f} MiB"
        epoch_seconds = time.perf_counter() - epoch_started
        scale_text = f" | amp_scale={scaler.get_scale():.0f}" if scaler is not None else ""
        print(f"  epoch_time={epoch_seconds:.2f}s | {memory_text}{scale_text}", flush=True)

    print(f"Best val_acc={best_acc:.4f} val_loss={best_loss:.4f}", flush=True)


def test(args) -> None:
    log_dir = Path(args.log_dir)
    device = torch.device("cpu" if args.cpu or not torch.cuda.is_available() else "cuda")
    ckpt = load_checkpoint(log_dir, device)
    class_names = [canonical_class_name(name) for name in ckpt["class_names"]]
    args.num_points = int(ckpt.get("num_points", args.num_points))
    args.normalize_points = bool(ckpt.get("normalize_points", args.normalize_points))
    args.use_normals = bool(ckpt.get("use_normals", args.use_normals))
    config = ckpt.get("backbone_config", {})
    args.ptv3_grid_size = float(config.get("grid_size", args.ptv3_grid_size))
    args.ptv3_patch_size = int(config.get("patch_size", args.ptv3_patch_size))
    args.ptv3_flash = bool(config.get("flash", args.ptv3_flash))
    args.ptv3_dropout = float(config.get("dropout", args.ptv3_dropout))
    if config.get("repo") and args.ptv3_repo is None:
        args.ptv3_repo = config.get("repo")

    dataset = PointCloudClassificationDataset(
        Path(args.data_root), args.split, class_names, args.num_points, train=False,
        normalize=args.normalize_points, use_normals=args.use_normals, cache_points=args.cache_points,
        normal_workers=args.normal_workers, **normal_kwargs(args, ckpt))
    loader = make_loader(dataset, args.batch_size, False, args.num_workers, device, args.seed)
    model = build_model(args, len(class_names)).to(device)
    model.load_state_dict(ckpt["state_dict"])
    criterion = nn.CrossEntropyLoss()
    loss, acc, y_true, y_pred, paths = evaluate(model, loader, device, criterion)
    write_metrics(log_dir, args.split, class_names, y_true, y_pred, paths)
    print(f"{args.split}: loss={loss:.4f} acc={acc:.4f} samples={len(dataset)}", flush=True)
    print(f"Wrote metrics to {log_dir}", flush=True)


def predict(args) -> None:
    log_dir = Path(args.log_dir)
    device = torch.device("cpu" if args.cpu or not torch.cuda.is_available() else "cuda")
    ckpt = load_checkpoint(log_dir, device)
    class_names = [canonical_class_name(name) for name in ckpt["class_names"]]
    args.num_points = int(ckpt.get("num_points", args.num_points))
    args.normalize_points = bool(ckpt.get("normalize_points", args.normalize_points))
    args.use_normals = bool(ckpt.get("use_normals", args.use_normals))
    config = ckpt.get("backbone_config", {})
    args.ptv3_grid_size = float(config.get("grid_size", args.ptv3_grid_size))
    args.ptv3_patch_size = int(config.get("patch_size", args.ptv3_patch_size))
    args.ptv3_flash = bool(config.get("flash", args.ptv3_flash))
    args.ptv3_dropout = float(config.get("dropout", args.ptv3_dropout))
    if config.get("repo") and args.ptv3_repo is None:
        args.ptv3_repo = config.get("repo")

    points = load_txt_points(Path(args.input))
    source_points = points
    if args.normalize_points:
        points = normalize_points(points)
    normals = predict_normals(source_points, points, args, ckpt)
    points = sample_points(points, args.num_points, False, 0.0, False, normals)
    tensor = torch.from_numpy(points).unsqueeze(0).to(device).float()

    model = build_model(args, len(class_names)).to(device)
    model.load_state_dict(ckpt["state_dict"])
    model.eval()
    with torch.no_grad():
        prob = torch.softmax(model(tensor), dim=1)[0].cpu().numpy()
    order = np.argsort(-prob)
    result = [{"class": class_names[i], "prob": float(prob[i])}
              for i in order[:max(1, args.topk)]]
    print(json.dumps(result, ensure_ascii=False, indent=2))


def parse_args():
    parser = argparse.ArgumentParser(description="PTv3 point-cloud classification")
    parser.add_argument("--mode", choices=["train", "test", "predict"], default="train")
    parser.add_argument("--data_root", default="E:/pointnet/datasets_aug_v7")
    parser.add_argument("--log_dir", default="logs/ptv3_cls")
    parser.add_argument("--split", default="test", choices=list(SPLITS))
    parser.add_argument("--input", help="Input txt for predict mode")
    parser.add_argument("--num_points", type=int, default=2048)
    parser.add_argument("--batch_size", type=int, default=8)
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--weight_decay", type=float, default=1e-4)
    parser.add_argument("--label_smoothing", type=float, default=0.05)
    parser.add_argument("--train_jitter_sigma", type=float, default=0.003)
    parser.add_argument("--random_rotate", action="store_true")
    parser.add_argument("--grad_clip", type=float, default=1.0)
    parser.add_argument("--amp", action=argparse.BooleanOptionalAction, default=False,
                        help="CUDA float16 mixed-precision training; validation/test/predict stay FP32.")
    parser.add_argument("--num_workers", type=int, default=0 if os.name == "nt" else 2)
    parser.add_argument("--normal_workers", type=int, default=0 if os.name == "nt" else 2,
                        help="CPU normal-preparation processes; 0 or 1 uses serial computation.")
    parser.add_argument("--normal_backend", choices=["numpy", "pcl"], default="numpy",
                        help="PCL caches normals on disk; test/predict use the checkpoint backend.")
    parser.add_argument("--normal_threads", type=int, default=4,
                        help="OpenMP threads for PCL (no outer normal-preparation processes).")
    parser.add_argument("--cache_points", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--normalize_points", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--use_normals", action="store_true")
    parser.add_argument("--cpu", action="store_true")
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--topk", type=int, default=3)
    parser.add_argument("--backbone", choices=["ptv3"], default="ptv3")
    parser.add_argument("--ptv3_repo", default=None,
                        help="Official standalone PTv3 repository (or PTV3_ROOT environment variable)")
    parser.add_argument("--ptv3_grid_size", type=float, default=0.02)
    parser.add_argument("--ptv3_patch_size", type=int, default=128)
    parser.add_argument("--ptv3_flash", action="store_true")
    parser.add_argument("--ptv3_dropout", type=float, default=0.1)
    args = parser.parse_args()

    if args.mode == "predict" and not args.input:
        raise SystemExit("--input is required for predict mode")
    if args.amp and args.mode != "train":
        parser.error("--amp is for training only; validation/test/predict use FP32")
    if args.epochs < 1 or args.batch_size < 1 or args.num_points < 1:
        parser.error("epochs/batch_size/num_points must be positive")
    if args.num_workers < 0:
        parser.error("num_workers must be nonnegative")
    if args.normal_workers < 0:
        parser.error("normal_workers must be nonnegative")
    if args.normal_threads < 1:
        parser.error("normal_threads must be positive")
    if args.ptv3_grid_size <= 0 or args.ptv3_patch_size < 1:
        parser.error("ptv3_grid_size/ptv3_patch_size must be positive")
    if not 0 <= args.label_smoothing < 1:
        parser.error("label_smoothing must be in [0, 1)")
    return args


def main() -> None:
    args = parse_args()
    set_seed(args.seed)
    if args.mode == "train":
        started = time.perf_counter()
        try:
            train(args)
        finally:
            elapsed = time.perf_counter() - started
            hours, remainder = divmod(int(elapsed), 3600)
            minutes, seconds = divmod(remainder, 60)
            print(f"Classification training duration: {hours:02d}:{minutes:02d}:{seconds:02d} "
                  "(includes data/normal preparation and validation)", flush=True)
    elif args.mode == "test":
        test(args)
    elif args.mode == "predict":
        predict(args)


if __name__ == "__main__":
    main()
