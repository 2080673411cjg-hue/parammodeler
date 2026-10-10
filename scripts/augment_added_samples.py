"""Augment only new dataset samples, preserving existing augmented files."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile

import numpy as np


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("E:/pointnet/datasets"))
    parser.add_argument("--output", type=Path, default=Path("E:/pointnet/datasets_aug"))
    parser.add_argument("--classes", nargs="+", default=None,
                        help="Classes to augment; default: all classes in raw metadata.")
    parser.add_argument("--augment-module", type=Path, default=Path("E:/pointnet/ptv3_simple/augment_dataset.py"))
    parser.add_argument("--holes", action=argparse.BooleanOptionalAction, default=None)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def read_records(path):
    records = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(records, list):
        raise ValueError(f"Expected a JSON array: {path}")
    return records


def main():
    args = parse_args()
    input_root = args.input.resolve()
    output_root = args.output.resolve()
    if input_root == output_root:
        raise ValueError("Input and output must be different folders")

    raw_meta = input_root / "metadata/sample_params.json"
    aug_meta = output_root / "metadata/sample_params.json"
    config_path = output_root / "metadata/augment_config.json"
    if not raw_meta.is_file() or not aug_meta.is_file() or not config_path.is_file():
        raise FileNotFoundError("Both datasets and augment_config.json must already exist")

    raw_records = read_records(raw_meta)
    old_records = read_records(aug_meta)
    config = json.loads(config_path.read_text(encoding="utf-8"))
    old_paths = {record["file"] for record in old_records}
    raw_paths = {record["file"] for record in raw_records}
    if len(raw_paths) != len(raw_records) or not old_paths.issubset(raw_paths):
        raise ValueError("Metadata contains duplicate paths or old augmented records are absent from raw metadata")

    selected = set(args.classes) if args.classes else {record["type"] for record in raw_records}
    unknown = selected - {record["type"] for record in raw_records}
    if unknown:
        raise ValueError(f"Classes absent from raw metadata: {sorted(unknown)}")
    additions = [record for record in raw_records if record["type"] in selected
                 and not (output_root / record["file"]).exists()]
    for record in additions:
        if not (input_root / record["file"]).is_file():
            raise FileNotFoundError(input_root / record["file"])
    print(f"New samples to augment: {len(additions)}")
    if args.dry_run:
        return

    if not args.augment_module.is_file():
        raise FileNotFoundError(args.augment_module)
    spec = importlib.util.spec_from_file_location("dataset_augmentation", args.augment_module)
    if spec is None or spec.loader is None:
        raise ImportError(args.augment_module)
    augmentation = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(augmentation)

    holes = args.holes if args.holes is not None else config.get("holes", True)
    if args.holes is None and "holes" not in config:
        print("augment_config.json does not record holes; using augment_dataset.py default (enabled).")
    settings = argparse.Namespace(
        num_points=int(config["num_points"]),
        drop_bottom=bool(config["drop_bottom"]),
        bottom_height=float(config["bottom_height"]),
        holes=holes,
        hole_prob=float(config.get("hole_prob", 0.15)),
        max_holes=int(config.get("max_holes", 2)),
        hole_radius=float(config.get("hole_radius", 0.08)),
        noise=bool(config["noise"]),
        noise_sigma=float(config["noise_sigma"]),
        outliers=bool(config["outliers"]),
        outlier_ratio=float(config["outlier_ratio"]),
        outlier_extent=float(config["outlier_extent"]),
    )

    for index, record in enumerate(additions, 1):
        rel = record["file"]
        dest = output_root / rel
        seed_bytes = hashlib.blake2b(rel.encode("utf-8"), digest_size=4).digest()
        np.random.seed((int(config["seed"]) + int.from_bytes(seed_bytes, "little")) % (2 ** 32))
        points = augmentation.augment_points(augmentation.load_points(input_root / rel), settings)
        dest.parent.mkdir(parents=True, exist_ok=True)
        handle = tempfile.NamedTemporaryFile(prefix=".augment-", suffix=".txt", dir=dest.parent, delete=False)
        temp_path = Path(handle.name)
        handle.close()
        try:
            augmentation.write_points(temp_path, points)
            os.replace(temp_path, dest)
        finally:
            temp_path.unlink(missing_ok=True)
        if index % 100 == 0 or index == len(additions):
            print(f"Augmented {index}/{len(additions)}")

    missing = [record["file"] for record in raw_records if not (output_root / record["file"]).is_file()]
    if missing:
        raise RuntimeError(f"Augmented dataset still has {len(missing)} missing samples; metadata not updated")

    metadata_dir = output_root / "metadata"
    for name in ["sample_params.json", *[f"sample_params_{name}.json" for name in selected]]:
        source = input_root / "metadata" / name
        dest = metadata_dir / name
        if not source.is_file():
            raise FileNotFoundError(source)
        with tempfile.NamedTemporaryFile(prefix=".metadata-", suffix=".json", dir=metadata_dir, delete=False) as handle:
            temp_path = Path(handle.name)
            handle.write(source.read_bytes())
        os.replace(temp_path, dest)
    print(f"Metadata synced. Existing augmented point clouds were not changed: {output_root}")


if __name__ == "__main__":
    main()
