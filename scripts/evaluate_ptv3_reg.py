"""Test all thirteen regression checkpoints using estimated geometry orientation."""

from primitive_names import canonical_class_name, regression_slug

import argparse
import csv
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import shlex
import subprocess
import sys

from compare_ptv3_reg import read_json, sha256, write_csv
from train_ptv3_reg import RECIPES


def prepare_plan(data_root, log_root, output_root, run_tag):
    if output_root.exists() and (not output_root.is_dir() or any(output_root.iterdir())):
        raise ValueError(f"Refusing to overwrite an existing evaluation: {output_root}")
    if any(output_root.resolve().is_relative_to(root.resolve()) for root in (data_root, log_root)):
        raise ValueError("Keep evaluation output outside the dataset and checkpoint directories")
    classes = {recipe[0] for recipe in RECIPES}
    records = [row for row in read_json(data_root / "metadata/sample_params.json")
               if row.get("split") == "test" and canonical_class_name(row.get("type")) in classes]
    seen = set()
    for row in records:
        relative = str(row.get("file", "")).replace("\\", "/")
        parts = PurePosixPath(relative).parts
        if (len(parts) != 3 or parts[:2] != ("test", row["type"])
                or not parts[2].endswith(".txt") or relative in seen):
            raise ValueError(f"Invalid or duplicate test path: {relative}")
        if not (data_root / relative).is_file():
            raise FileNotFoundError(data_root / relative)
        seen.add(relative)
    jobs = []
    for name, slug, _, _ in RECIPES:
        count = sum(canonical_class_name(row["type"]) == name for row in records)
        if count != 200:
            raise ValueError(f"Expected 200 test clouds for {name}, found {count}")
        directory = log_root / f"ptv3_reg_{regression_slug(name, run_tag)}_{run_tag}_normals"
        if not (directory / "best_model.pth").is_file():
            raise FileNotFoundError(directory / "best_model.pth")
        config = read_json(directory / "regression_config.json")
        if canonical_class_name(config.get("class_name")) != name or not config.get("canonical_align"):
            raise ValueError(f"Unexpected class or non-canonical checkpoint: {directory}")
        jobs.append({"class": name, "directory": str(directory), "expected": count})
    return records, jobs


def test_command(data_root, job, output_root):
    return [sys.executable, "-u", str(Path(__file__).with_name("main_reg.py")),
            "--mode", "test", "--data_root", str(data_root),
            "--metadata", str(data_root / "metadata/sample_params.json"),
            "--class_name", job["class"], "--log_dir", job["directory"],
            "--eval_output_dir", str(output_root / job["class"]),
            "--test_orientation", "geometry", "--split", "test",
            "--num_points", "2048", "--batch_size", "4", "--seed", "42",
            "--num_workers", "2" if os.name != "nt" else "0"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data_root", required=True)
    parser.add_argument("--log_root", default="logs")
    parser.add_argument("--output_root", default="evaluations/ptv3_v10_geometry")
    parser.add_argument("--run_tag", default="v10")
    parser.add_argument("--dry_run", action="store_true")
    args = parser.parse_args()
    data_root, log_root, output_root = (Path(value).resolve() for value in
                                      (args.data_root, args.log_root, args.output_root))
    manifest = None
    try:
        records, jobs = prepare_plan(data_root, log_root, output_root, args.run_tag)
        if args.dry_run:
            print(f"Read-only plan: {len(jobs)} checkpoints; {len(records)} test clouds; no training.")
            for job in jobs:
                print(shlex.join(test_command(data_root, job, output_root)))
            return
        import torch
        if not torch.cuda.is_available() or importlib.util.find_spec("spconv") is None:
            raise RuntimeError("PTv3 evaluation requires CUDA and spconv; run in Ubuntu ptv3_train.")
        output_root.mkdir(parents=True, exist_ok=True)
        manifest = {"status": "running", "run_tag": args.run_tag, "seed": 42,
                    "source_data_root": str(data_root), "orientation_source": "geometry",
                    "metadata_sha256": sha256(data_root / "metadata/sample_params.json"),
                    "test_files": [{"file": row["file"], "sha256": sha256(data_root / row["file"])}
                                   for row in records], "models": jobs, "completed": []}
        for job in jobs:
            directory = Path(job["directory"])
            job["checkpoint_sha256"] = sha256(directory / "best_model.pth")
            job["config_sha256"] = sha256(directory / "regression_config.json")
        manifest_path = output_root / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        summary = []
        for job in jobs:
            print(f'Testing {job["class"]}/{args.run_tag}/geometry', flush=True)
            with (output_root / f'{job["class"]}.log').open("w", encoding="utf-8") as stream:
                subprocess.run(test_command(data_root, job, output_root),
                               stdout=stream, stderr=subprocess.STDOUT, check=True)
            destination = output_root / job["class"]
            orientation = read_json(destination / "orientation_test_geometry.json")
            with (destination / "regression_metrics_test_geometry.csv").open(encoding="utf-8-sig", newline="") as stream:
                for metric in csv.DictReader(stream):
                    summary.append({"class": job["class"], "expected": job["expected"],
                                    "alignment_attempted": orientation["alignment_attempted"],
                                    "accepted": orientation["accepted"], "failed": orientation["failed"],
                                    **metric})
            manifest["completed"].append(job["class"])
            manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        write_csv(output_root / "summary.csv", summary)
        manifest["status"] = "complete"
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        print(f"Results: {output_root}", flush=True)
        print("ALL_V8_GEOMETRY_DONE", flush=True)
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as exc:
        if manifest is not None:
            manifest.update(status="failed", error=str(exc))
            (output_root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        parser.exit(1, f"Evaluation stopped: {exc}\n")


if __name__ == "__main__":
    main()
