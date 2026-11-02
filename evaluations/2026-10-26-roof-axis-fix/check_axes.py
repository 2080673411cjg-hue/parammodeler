"""CPU-only before/after audit. Metadata is used only for scoring results."""
import csv
import hashlib
import importlib.util
import inspect
import json
from pathlib import Path
import time
import sys

import numpy as np

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[1]
DATA = Path("E:/pointnet/datasets_aug")
SOURCE = ROOT / "scripts/canonical_orientation.py"
BASELINE = OUT / "baseline_canonical_orientation.py"
ROOFS = ("GabledRoof", "AsymmetricGableHouse", "HalfCylinderRoof")


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def records():
    rows = json.loads((DATA / "metadata/sample_params.json").read_text(encoding="utf-8-sig"))
    return rows if isinstance(rows, list) else rows["samples"]


def experiment():
    if not BASELINE.exists():
        BASELINE.write_bytes(SOURCE.read_bytes())
    old = load(BASELINE, "baseline")
    rows = []
    started = time.perf_counter()
    for rec in records():
        cls = rec["type"]
        if rec["split"] != "test" or cls not in ROOFS:
            continue
        points = np.loadtxt(DATA / rec["file"], dtype=np.float32)[:, :3]
        before = old.canonical_orientation(points, cls)
        angle = old._rectangle_axis(points)[0]
        xy = old.rotate_points_z(points, -angle)[:, :2]
        full_spread = np.diff(np.quantile(xy, [.01, .99], axis=0), axis=0)[0]
        z = points[:, 2]
        # All estimator candidates below receive only the actual cloud.
        estimates = {"before": before["canonical_angle_deg"]}
        for low in (.75, .8, .85, .9, .95, .97):
            top = xy[(z >= np.quantile(z, low)) & (z <= np.quantile(z, .995))]
            for trim in (0, .05, .1):
                spread = np.diff(np.quantile(top, [trim, 1 - trim], axis=0), axis=0)[0]
                estimates[f"q{low}_trim{trim}"] = old.fold_axis_degrees(angle + (90 if spread[1] > spread[0] else 0))
                relative = spread / full_spread
                estimates[f"relative_q{low}_trim{trim}"] = old.fold_axis_degrees(angle + (90 if relative[1] > relative[0] else 0))
        truth = old.canonical_angle_from_metadata(rec, cls)
        errors = {key: abs(old.fold_axis_degrees(value - truth)) for key, value in estimates.items()}
        rows.append({"class": cls, "file": rec["file"], "verdict": before["verdict"], "errors": errors})
    (OUT / "experiments.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    for cls in ROOFS:
        group = [row for row in rows if row["class"] == cls]
        print(cls, len(group), flush=True)
        for key in rows[0]["errors"]:
            wrong = [r["file"] for r in group if r["errors"][key] > 45]
            print(key, len(wrong), wrong, flush=True)
    print("elapsed", time.perf_counter() - started, flush=True)


def scalar_result(result):
    return {key: value.tolist() if isinstance(value, np.ndarray) else value
            for key, value in result.items() if key != "aligned_points"}


def audit(split="test"):
    old, new = load(BASELINE, "baseline"), load(SOURCE, "candidate")
    for name in ("canonical_orientation", "canonical_angle_from_metadata", "canonical_target_values"):
        assert inspect.signature(getattr(old, name)) == inspect.signature(getattr(new, name))
    for name in ("fold_axis_degrees", "rotate_points_z", "canonical_angle_from_metadata",
                 "canonical_target_values", "_rectangle_axis", "_lhouse_direction"):
        assert inspect.getsource(getattr(old, name)) == inspect.getsource(getattr(new, name)), name
    started = time.perf_counter()
    rows, sampled = [], {}
    seconds = [0., 0.]
    other_classes = ("Cuboid", "IndentedCuboid", "PyramidRoof", "TruncatedPyramidRoof",
                     "LHouse", "Cylinder", "ConeCylinder", "CylinderDome", "FourStageRoundTower",
                     "TwoGableHouses")
    sentinel_files = {"test/LHouse/sample_00978.txt", "test/PyramidRoof/sample_01087.txt"}
    for rec in records():
        cls, file = rec["type"], rec["file"].replace("\\", "/")
        if rec["split"] != split:
            continue
        if cls not in ROOFS:
            if split != "test" or cls not in other_classes:
                continue
            if sampled.get(cls, 0) >= 10 and file not in sentinel_files:
                continue
            sampled[cls] = sampled.get(cls, 0) + 1
        points = np.loadtxt(DATA / file, dtype=np.float32)[:, :3]
        original = points.copy()
        results = []
        for index, module in enumerate((old, new)):
            clock = time.perf_counter()
            result = module.canonical_orientation(points, cls, lhouse_canonical=cls == "LHouse")
            seconds[index] += time.perf_counter() - clock
            assert set(result) == set(old.canonical_orientation(original[:0], "TwoGableHouses"))
            assert result["aligned_points"].shape == points.shape
            assert result["rotation_matrix"].shape == (3, 3)
            np.testing.assert_array_equal(points, original)
            results.append(result)
        if cls not in ROOFS:
            for key in results[0]:
                np.testing.assert_equal(results[0][key], results[1][key], err_msg=f"{file} {key}")
        # Ground truth enters only here, after both estimates have been completed.
        truth = old.canonical_angle_from_metadata(rec, cls, lhouse_canonical=cls == "LHouse")
        errors = []
        for result in results:
            if truth is None or not result["reliable"]:
                errors.append(None)
            elif cls == "LHouse":
                errors.append(abs((result["canonical_angle_deg"] - truth + 180) % 360 - 180))
            else:
                errors.append(abs(old.fold_axis_degrees(result["canonical_angle_deg"] - truth)))
        rows.append({"class_name": cls, "file": file, "errors_deg": errors,
                     "before": scalar_result(results[0]), "after": scalar_result(results[1])})
    summary = []
    for cls in ROOFS:
        group = [r for r in rows if r["class_name"] == cls]
        assert len(group) == 200, (split, cls, len(group))
        row = {"class_name": cls, "count": len(group)}
        for index, version in enumerate(("before", "after")):
            errors = [r["errors_deg"][index] for r in group if r["errors_deg"][index] is not None]
            row.update({f"{version}_mean_deg": float(np.mean(errors)),
                        f"{version}_p95_deg": float(np.quantile(errors, .95)),
                        f"{version}_max_deg": float(np.max(errors)),
                        f"{version}_failed": sum(not r[version]["reliable"] for r in group),
                        f"{version}_wrong_axis_gt45": sum(e > 45 for e in errors),
                        f"{version}_weak": sum(r[version]["verdict"] == "WEAK" for r in group)})
        row["regressed_gt_1e-6_deg"] = sum(a is not None and b is not None and b > a + 1e-6
                                              for a, b in (r["errors_deg"] for r in group))
        summary.append(row)
        print(split, json.dumps(row), flush=True)
    report = {"split": split, "wall_seconds": time.perf_counter() - started,
              "function_seconds_before_after": seconds, "other_class_samples": sampled,
              "source_sha256": {"before": hashlib.sha256(BASELINE.read_bytes()).hexdigest(),
                                "after": hashlib.sha256(SOURCE.read_bytes()).hexdigest()},
              "summary": summary, "diagnostics": rows}
    (OUT / f"{split}_before_after.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    with (OUT / f"{split}_summary.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    print("elapsed", report["wall_seconds"], "function seconds", seconds, flush=True)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        audit(sys.argv[1])
    else:
        experiment()
