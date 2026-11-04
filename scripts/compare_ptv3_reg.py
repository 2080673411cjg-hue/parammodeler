"""Compare v4/v5 against v7 on one frozen 300-cloud test set, without training."""

from primitive_names import canonical_class_name

import argparse
import csv
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import shlex
import shutil
import subprocess
import sys

import numpy as np


RECIPES = (
    ("FlatRoofHouse", "cuboid", "v4"),
    ("FlatTopHipRoofHouse", "truncatedpyramid", "v4"),
    ("CurvedRoofRoundHouse", "fourstage", "v5"),
)
GROUPS = {"all": None, "small": 0, "medium": 1, "large": 2}


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def prepare_plan(data_root, log_root, output_root):
    if output_root.exists() and (not output_root.is_dir() or any(output_root.iterdir())):
        raise ValueError(f"Refusing to overwrite an existing evaluation: {output_root}")
    if output_root.resolve().is_relative_to(data_root.resolve()):
        raise ValueError("Keep evaluation output outside the source dataset")
    classes = {recipe[0] for recipe in RECIPES}
    records = []
    seen = set()
    for record in read_json(data_root / "metadata/sample_params.json"):
        if canonical_class_name(record.get("type")) not in classes or record.get("split") != "test":
            continue
        relative = str(record.get("file", "")).replace("\\", "/")
        parts = PurePosixPath(relative).parts
        if (len(parts) != 3 or parts[:2] != ("test", record["type"])
                or not parts[2].endswith(".txt") or relative in seen):
            raise ValueError(f"Invalid or duplicate test file: {relative}")
        if record.get("horizontalScaleBand") not in (0, 1, 2):
            raise ValueError(f"Missing horizontalScaleBand for {relative}; use the v7 dataset")
        if not (data_root / relative).is_file():
            raise FileNotFoundError(data_root / relative)
        seen.add(relative)
        records.append(record)
    models = {}
    for class_name, slug, old_tag in RECIPES:
        count = sum(canonical_class_name(record["type"]) == class_name for record in records)
        if count != 100:
            raise ValueError(f"Expected 100 test clouds for {class_name}, found {count}")
        models[class_name] = {}
        for tag in (old_tag, "v7"):
            directory = log_root / f"ptv3_reg_{slug}_{tag}_normals"
            if not (directory / "best_model.pth").is_file():
                raise FileNotFoundError(directory / "best_model.pth")
            config = read_json(directory / "regression_config.json")
            if canonical_class_name(config.get("class_name")) != class_name or not config.get("canonical_align"):
                raise ValueError(f"Unexpected class or non-canonical model: {directory}")
            models[class_name][tag] = {"directory": str(directory), "targets": config["targets"]}
        if models[class_name][old_tag]["targets"] != models[class_name]["v7"]["targets"]:
            raise ValueError(f"Target order differs between checkpoints for {class_name}")
    return records, models


def test_command(dataset, model_dir, output_dir, class_name, orientation):
    return [sys.executable, "-u", str(Path(__file__).with_name("main_reg.py")),
            "--mode", "test", "--data_root", str(dataset),
            "--metadata", str(dataset / "metadata/sample_params.json"),
            "--class_name", class_name, "--log_dir", str(model_dir),
            "--eval_output_dir", str(output_dir), "--test_orientation", orientation,
            "--split", "test", "--num_points", "2048", "--batch_size", "4", "--seed", "42"]


def snapshot_dataset(data_root, output_root, records, models):
    dataset = output_root / "dataset"
    (dataset / "metadata").mkdir(parents=True)
    files = []
    for record in records:
        relative = record["file"].replace("\\", "/")
        destination = dataset / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(data_root / relative, destination)
        files.append({"file": relative, "sha256": sha256(destination)})
    (dataset / "metadata/sample_params.json").write_text(
        json.dumps(records, ensure_ascii=False, indent=2), encoding="utf-8")
    for pair in models.values():
        for model in pair.values():
            directory = Path(model["directory"])
            model["checkpoint_sha256"] = sha256(directory / "best_model.pth")
            model["config_sha256"] = sha256(directory / "regression_config.json")
    manifest = {"source_data_root": str(data_root), "seed": 42, "num_points": 2048,
                "source_metadata_sha256": sha256(data_root / "metadata/sample_params.json"),
                "test_files": files, "models": models,
                "stages": ["metadata", "geometry"], "status": "prepared"}
    (output_root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return dataset, manifest


def load_predictions(path, targets, expected):
    result = {}
    with path.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            relative = "/".join(PurePosixPath(row["file"].replace("\\", "/")).parts[-3:])
            if relative not in expected or relative in result:
                raise ValueError(f"Unexpected or duplicate prediction: {relative}")
            values = {target: (float(row["true_" + target]), float(row["pred_" + target]))
                      for target in targets}
            if not np.isfinite(list(values.values())).all():
                raise ValueError(f"Non-finite prediction for {relative}")
            result[relative] = values
    return result


def compare_predictions(class_name, orientation, targets, expected, old, new):
    summaries, failures = [], []
    if orientation == "metadata" and (set(old) != set(expected) or set(new) != set(expected)):
        raise ValueError(f"Metadata-frame test must predict every cloud for {class_name}")
    paired = set(old) & set(new)
    for relative in paired:
        for target in targets:
            if not np.isclose(old[relative][target][0], new[relative][target][0], rtol=1e-5, atol=1e-6):
                raise ValueError(f"Ground truth differs between models: {relative}, {target}")
    for relative in sorted(set(expected) - paired):
        failures.append({"class": class_name, "orientation": orientation, "file": relative,
                         "old_failed": relative not in old, "v7_failed": relative not in new})
    for group, band in GROUPS.items():
        members = {relative for relative, record in expected.items()
                   if band is None or record["horizontalScaleBand"] == band}
        common = sorted(members & paired)
        for target in targets:
            row = {"class": class_name, "orientation": orientation, "group": group, "param": target,
                   "expected": len(members), "paired": len(common),
                   "old_failed": len(members - set(old)), "v7_failed": len(members - set(new))}
            for label, predictions in (("old", old), ("v7", new)):
                truth = np.array([predictions[relative][target][0] for relative in common])
                errors = np.array([abs(predictions[relative][target][1] - predictions[relative][target][0])
                                   for relative in common])
                row.update({label + "_mae": float(errors.mean()) if common else None,
                            label + "_rmse": float(np.sqrt(np.mean(errors ** 2))) if common else None,
                            label + "_mape_percent": float(np.mean(errors / np.maximum(abs(truth), 1e-6)) * 100) if common else None,
                            label + "_p95": float(np.percentile(errors, 95)) if common else None})
            old_errors = np.array([abs(old[relative][target][1] - old[relative][target][0]) for relative in common])
            new_errors = np.array([abs(new[relative][target][1] - new[relative][target][0]) for relative in common])
            ties = np.isclose(old_errors, new_errors, rtol=1e-6, atol=1e-8)
            row["v7_win_percent"] = float(np.mean((new_errors < old_errors) & ~ties) * 100) if common else None
            row["tie_percent"] = float(ties.mean() * 100) if common else None
            summaries.append(row)
    return summaries, failures


def write_csv(path, rows, fieldnames=None):
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames or list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def summarize(output_root, records, models):
    summaries, failures = [], []
    for class_name, _, old_tag in RECIPES:
        expected = {record["file"].replace("\\", "/"): record
                    for record in records if record["type"] == class_name}
        targets = models[class_name]["v7"]["targets"]
        for orientation in ("metadata", "geometry"):
            suffix = "test_geometry" if orientation == "geometry" else "test"
            pairs = [load_predictions(output_root / class_name / tag / orientation /
                                      f"regression_predictions_{suffix}.csv", targets, expected)
                     for tag in (old_tag, "v7")]
            rows, missing = compare_predictions(class_name, orientation, targets, expected, *pairs)
            summaries.extend(rows)
            failures.extend(missing)
    write_csv(output_root / "comparison.csv", summaries)
    write_csv(output_root / "failures.csv", failures,
              ["class", "orientation", "file", "old_failed", "v7_failed"])
    for row in summaries:
        if row["group"] == "all":
            print(f'{row["class"]}/{row["orientation"]}/{row["param"]}: '
                  f'MAE old={row["old_mae"]} v7={row["v7_mae"]}; '
                  f'v7 wins={row["v7_win_percent"]}%; paired={row["paired"]}/{row["expected"]}; '
                  f'failed old={row["old_failed"]} v7={row["v7_failed"]}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data_root", default=str(Path(__file__).resolve().parent.parent / "datasets_aug"))
    parser.add_argument("--log_root", default="logs")
    parser.add_argument("--output_root", default="evaluations/ptv3_v7_same_set")
    parser.add_argument("--dry_run", action="store_true")
    args = parser.parse_args()
    data_root, log_root, output_root = (Path(value).resolve() for value in
                                      (args.data_root, args.log_root, args.output_root))
    try:
        records, models = prepare_plan(data_root, log_root, output_root)
        if not args.dry_run:
            import torch
            if not torch.cuda.is_available() or importlib.util.find_spec("spconv") is None:
                raise RuntimeError("PTv3 comparison requires CUDA and spconv. Run in the Ubuntu ptv3_train environment.")
        dataset = output_root / "dataset"
        if args.dry_run:
            print(f"Read-only plan: {len(records)} test clouds; 6 checkpoints; 12 serial tests; no training.")
        else:
            dataset, manifest = snapshot_dataset(data_root, output_root, records, models)
        for class_name, _, old_tag in RECIPES:
            for orientation in ("metadata", "geometry"):
                for tag in (old_tag, "v7"):
                    destination = output_root / class_name / tag / orientation
                    command = test_command(dataset, models[class_name][tag]["directory"], destination,
                                           class_name, orientation)
                    if args.dry_run:
                        print(shlex.join(command))
                    else:
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        print(f"Testing {class_name}/{tag}/{orientation}", flush=True)
                        with destination.with_suffix(".log").open("w", encoding="utf-8") as stream:
                            subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)
        if not args.dry_run:
            summarize(output_root, records, models)
            manifest["status"] = "complete"
            (output_root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
            print(f"Results: {output_root / 'comparison.csv'}", flush=True)
    except (ValueError, RuntimeError, FileNotFoundError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Comparison stopped: {exc}\n")


if __name__ == "__main__":
    main()
