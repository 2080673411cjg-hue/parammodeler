"""Augment only new dataset samples, preserving existing augmented files."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import tempfile

import numpy as np


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("E:/pointnet/datasets"))
    parser.add_argument("--output", type=Path, default=Path("E:/pointnet/datasets_aug"))
    parser.add_argument("--classes", nargs="+", default=None,
                        help="Classes to augment; default: raw classes except TriPrismPyramid.")
    parser.add_argument("--splits", nargs="+", choices=["train", "val", "test"],
                        default=["train", "val", "test"],
                        help="Splits to append; use train to preserve the existing validation/test manifest.")
    parser.add_argument("--augment-module", type=Path, default=Path("E:/pointnet/ptv3_simple/augment_dataset.py"))
    parser.add_argument("--holes", action=argparse.BooleanOptionalAction, default=None)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def read_records(path):
    records = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(records, list):
        raise ValueError(f"Expected a JSON array: {path}")
    return records


def records_by_path(records):
    result = {}
    for record in records:
        relative = str(record["file"]).replace("\\", "/")
        parts = PurePosixPath(relative).parts
        if (len(parts) != 3 or parts[0] != record.get("split")
                or parts[0] not in ("train", "val", "test") or parts[1] != record.get("type")
                or not parts[2].endswith(".txt") or relative in result):
            raise ValueError(f"Invalid or duplicate metadata path: {relative}")
        result[relative] = record
    return result


def write_records(path, records):
    temp_path = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".metadata-", suffix=".json", dir=path.parent, delete=False) as handle:
            temp_path = Path(handle.name)
            handle.write(json.dumps(records, ensure_ascii=False, indent=2).encode("utf-8"))
        os.replace(temp_path, path)
    finally:
        if temp_path is not None:
            temp_path.unlink(missing_ok=True)


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
    old_by_path = records_by_path(old_records)
    raw_by_path = records_by_path(raw_records)

    selected = set(args.classes) if args.classes else {record["type"] for record in raw_records
                                                    if record["type"] != "TriPrismPyramid"}
    unknown = selected - {record["type"] for record in raw_records}
    if unknown:
        raise ValueError(f"Classes absent from raw metadata: {sorted(unknown)}")
    for relative, record in old_by_path.items():
        if record["type"] in selected and raw_by_path.get(relative) != record:
            raise ValueError(f"Existing selected labels changed or disappeared: {relative}")
    eligible = {relative: record for relative, record in raw_by_path.items()
                if record["type"] in selected and record["split"] in args.splits}
    additions = [relative for relative in eligible if not (output_root / relative).is_file()]
    new_records = [record for relative, record in eligible.items() if relative not in old_by_path]
    final_records = old_records + new_records
    for relative in additions:
        if not (input_root / relative).is_file():
            raise FileNotFoundError(input_root / relative)
    print(f"New samples to augment: {len(additions)}")
    print(f"Metadata records to append: {len(new_records)} | selected splits: {', '.join(args.splits)}")
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

    for index, rel in enumerate(additions, 1):
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

    missing = [record["file"] for record in final_records if record["type"] in selected
               and not (output_root / record["file"].replace("\\", "/")).is_file()]
    if missing:
        raise RuntimeError(f"Selected classes still have {len(missing)} missing samples; metadata not updated: {missing[:3]}")

    metadata_dir = output_root / "metadata"
    for name in sorted(selected):
        write_records(metadata_dir / f"sample_params_{name}.json",
                      [record for record in final_records if record["type"] == name])
    write_records(aug_meta, final_records)
    print(f"Metadata synced. Existing augmented point clouds were not changed: {output_root}")


if __name__ == "__main__":
    main()
