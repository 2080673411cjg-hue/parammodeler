"""Prepare and summarize a metadata-yaw, four-group plugin geometry evaluation."""

import argparse
import ast
import csv
import hashlib
import json
from pathlib import Path
from typing import Dict, List

import numpy as np

from canonical_orientation import canonical_angle_from_metadata, canonical_target_values
from train_ptv3_reg import RECIPES


def source_functions():
    # Load the training definitions without importing Torch or the CUDA backbone.
    path = Path(__file__).with_name("main_reg.py")
    tree = ast.parse(path.read_text(encoding="utf-8-sig"))
    names = {"target_value", "target_vector", "primitive_geometry_summary", "prediction_params_for_plugin"}
    tree.body = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    scope = {"np": np, "List": List, "Dict": Dict}
    exec(compile(tree, str(path), "exec"), scope)
    return scope


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(args):
    functions = source_functions()
    samples, audit = [], []
    for name, short, _, _ in RECIPES:
        tag = "v5_normals" if name == "FourStageRoundTower" else "v4_canonical_normals"
        log = args.logs / f"ptv3_reg_{short}_{tag}"
        config_path = log / "regression_config.json"
        predictions_path = log / "regression_predictions_test.csv"
        metadata_path = args.data / "metadata" / f"sample_params_{name}.json"
        config = json.loads(config_path.read_text(encoding="utf-8-sig"))
        records = json.loads(metadata_path.read_text(encoding="utf-8-sig"))
        records = {r["file"]: r for r in records if r["split"] == "test"}
        raw_records = json.loads((args.raw / "metadata" / metadata_path.name).read_text(encoding="utf-8-sig"))
        raw_records = {r["file"]: r for r in raw_records if r["split"] == "test"}
        targets = config["targets"]
        if config.get("indented_symmetry_180") or config.get("gabled_ridge_symmetry_180") or config.get("asym_ridge_symmetry_180"):
            raise ValueError(f"{name}: symmetry-loss predictions need separate direction disambiguation")
        with predictions_path.open(encoding="utf-8-sig", newline="") as f:
            rows = list(csv.DictReader(f))
        if len(rows) != 100 or len(records) != 100:
            raise ValueError(f"{name}: expected 100 test metadata records and predictions")
        seen = set()
        max_difference = 0.0
        class_samples = []
        for row in rows:
            rel = f"test/{name}/{Path(row['file']).name}"
            if rel in seen:
                raise ValueError(f"Duplicate prediction {rel}")
            seen.add(rel)
            record = records[rel]
            if raw_records[rel]["params"] != record["params"]:
                raise ValueError(f"Raw and augmented shape labels differ: {rel}")
            values = functions["target_vector"](record, targets)
            yaw = canonical_angle_from_metadata(record, name) if config.get("canonical_align") else None
            if yaw is not None:
                values = canonical_target_values(values, targets, record, name, yaw)
            csv_truth = np.array([float(row[f"true_{t}"]) for t in targets])
            difference = float(np.max(np.abs(values - csv_truth)))
            max_difference = max(max_difference, difference)
            prediction = functions["prediction_params_for_plugin"](
                name, targets, np.array([float(row[f"pred_{t}"]) for t in targets]))
            if yaw is not None:
                prediction["poseRotateZ"] = yaw
            cloud_path = args.data / rel
            raw_path = args.raw / rel
            if not cloud_path.is_file() or not raw_path.is_file():
                raise ValueError(f"Missing raw/augmented cloud: {rel}")
            if any(abs(record["params"].get(k, 0.0)) > 1e-8 for k in ("tx", "ty", "tz", "rx", "ry")):
                raise ValueError(f"{rel}: this evaluation supports yaw-only generated poses")
            class_samples.append({"class": name, "file": rel, "cloud": str(cloud_path),
                                  "raw_cloud": str(raw_path), "record": record,
                                  "raw_info": raw_records[rel]["pointCloudInfo"],
                                  "prediction": prediction, "log": str(log)})
        status = "matched" if max_difference < 3e-5 else "LABEL_MISMATCH"
        audit.append({"class": name, "status": status, "max_label_difference": max_difference,
                      "prediction_sha256": digest(predictions_path), "config_sha256": digest(config_path),
                      "metadata_sha256": digest(metadata_path), "log": str(log)})
        # A stale prediction must not be scored against a newly generated shape.
        for sample in class_samples:
            sample["prediction_valid"] = status == "matched"
        samples.extend(class_samples)
        print(f"{name}: {status}, max label delta={max_difference:.7g}", flush=True)
    manifest = {"scope": "known class and metadata yaw; no classification or geometric-yaw inference; pre-widget geometry",
                "samples": samples, "audit": audit,
                "sources": {p: digest(Path(__file__).parent.parent / p) for p in (
                    "buildmesh.cpp", "parammodeler_dlutils.cpp", "parammodeler_dock_alignment.cpp",
                    "parammodeler_fit_metrics.h", "scripts/main_reg.py", "scripts/canonical_orientation.py",
                    "tests/evaluate_alignment.cpp", "scripts/evaluate_alignment.py")}}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def summarize(args):
    results = json.loads((args.output / "results.json").read_text(encoding="utf-8"))
    manifest = json.loads((args.output / "manifest.json").read_text(encoding="utf-8"))
    validation_path = args.output / "raw_validation.json"
    validation = json.loads(validation_path.read_text(encoding="utf-8")) if validation_path.exists() else []
    if len(validation) != len(manifest["samples"]) or any(
        r.get("status") != "ok" or r.get("rmse", float("inf")) > 1e-4 for r in validation
    ):
        raise ValueError("Raw geometry validation missing or inaccurate; do not interpret the four-group results yet")
    summaries = []
    groups = sorted({(r["class"], r["group"]) for r in results})
    for name, group in groups:
        rows = [r for r in results if r["class"] == name and r["group"] == group]
        valid = [r for r in rows if r.get("status") == "ok"]
        summary = {"class": name, "group": group, "count": len(rows), "valid": len(valid),
                   "failed": len(rows) - len(valid)}
        metrics = ["xy_error", "z_error", "rmse", "p95", "inlier_0_2", "inlier_1pct"]
        if name == "IndentedCuboid":
            metrics += ["inner_xy_error", "inner_top_z_error"]
        for metric in metrics:
            values = [r[metric] for r in valid]
            if values:
                summary[f"{metric}_median"] = float(np.median(values))
                summary[f"{metric}_p95"] = float(np.percentile(values, 95))
                summary[f"{metric}_mean"] = float(np.mean(values))
        if valid:
            summary["xy_within_0_2m"] = sum(r["xy_error"] <= .2 for r in valid)
            summary["z_within_0_2m"] = sum(r["z_error"] <= .2 for r in valid)
        summaries.append(summary)
    (args.output / "summary.json").write_text(json.dumps(summaries, indent=2), encoding="utf-8")
    keys = list(dict.fromkeys(k for r in summaries for k in r))
    with (args.output / "summary.csv").open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=keys)
        writer.writeheader()
        writer.writerows(summaries)
    keys = list(dict.fromkeys(k for r in results for k in r))
    with (args.output / "per_sample.csv").open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=keys)
        writer.writeheader()
        writer.writerows(results)
    lines = ["# Initial Alignment Evaluation", "", manifest["scope"], "",
             "All groups use metadata yaw. Translation error is measured against the generated local origin (zero), not the algorithm's own chosen target.",
             "This is a standalone geometry evaluation, not a QGIS GUI or live-service acceptance test.",
             "Classification, unknown-yaw estimation, widget rounding/limits, and actual checkpoint inference are not exercised.",
             "Existing test predictions are reused; matching labels do not prove unchanged cloud contents since training.",
             "RMSE/P95 are unsigned cloud-to-triangle distances over all displayed points (up to 2048). Outliers are retained.",
             "0.2m and 1% of true mesh diagonal are descriptive thresholds, not validated acceptance limits.",
             "Recess auto groups also reposition the inner opening; baseline/shape groups do not.", "",
             "Summary CSV includes world-space inner-opening corner XY and top-Z errors for IndentedCuboid, including outer translation.", "",
             "BuildMesh and dlutils functions are linked directly. Display-bbox/translation orchestration mirrors the plugin; LHouse uses an equivalent six-wall mesh because its declared data overload has no definition.", "",
             "| Class | Group | Valid | Median XY (m) | Median Z (m) | Median RMSE (m) | Median P95 (m) | Mean within 0.2m |",
             "|---|---|---:|---:|---:|---:|---:|---:|"]
    for row in summaries:
        if row["valid"]:
            lines.append(f"| {row['class']} | {row['group']} | {row['valid']}/{row['count']} | "
                         f"{row['xy_error_median']:.3f} | {row['z_error_median']:.3f} | "
                         f"{row['rmse_median']:.3f} | {row['p95_median']:.3f} | {100*row['inlier_0_2_mean']:.1f}% |")
    lines.extend(["", "## Label Audit", ""])
    for row in manifest["audit"]:
        lines.append(f"- {row['class']}: {row['status']}; max target difference {row['max_label_difference']:.7g}; {row['log']}")
    lines.extend(["", "## Raw Geometry Validation", ""])
    lines.append("All raw ground-truth meshes must reproduce the exported clouds with RMSE below 0.0001m before this report is accepted.")
    for name, _, _, _ in RECIPES:
        rows = [r for r in validation if r["class"] == name]
        valid = [r for r in rows if r.get("status") == "ok"]
        if valid:
            lines.append(f"- {name}: {len(valid)}/{len(rows)} valid; median RMSE {np.median([r['rmse'] for r in valid]):.8f}m; max RMSE {max(r['rmse'] for r in valid):.8f}m")
    (args.output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Saved {len(results)} rows to {args.output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "summarize"))
    parser.add_argument("--data", type=Path, default=Path("E:/pointnet/datasets_aug"))
    parser.add_argument("--raw", type=Path, default=Path("E:/pointnet/datasets"))
    parser.add_argument("--logs", type=Path, default=Path("E:/pointnet/ptv3_simple/logs"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    (prepare if args.mode == "prepare" else summarize)(args)
