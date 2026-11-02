"""Read-only LHouse data/feature audit; no inference, fitting, or dataset edits."""

import csv
import hashlib
import importlib.util
import json
from pathlib import Path

import numpy as np


OUTPUT = Path(__file__).resolve().parent
REPO = OUTPUT.parents[1]
DATA = Path("E:/pointnet/datasets")
AUGMENTED = Path("E:/pointnet/datasets_aug")
LOGS = Path("E:/pointnet/ptv3_simple/logs")
TARGETS = ["outerLength", "outerWidth", "cutoutLengthRatio", "cutoutWidthRatio", "height"]
MODELS = {"scale": "axis_scale_trial", "corner": "corner_trial"}

spec = importlib.util.spec_from_file_location("lhouse_audit_orientation", REPO / "scripts/canonical_orientation.py")
orientation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(orientation)


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def summary(values):
    values = np.asarray(values, dtype=float)
    return {"mean": float(values.mean()), "median": float(np.median(values)),
            "p95": float(np.percentile(values, 95)), "max": float(values.max())}


def correlation(first, second):
    if np.std(first) < 1e-10 or np.std(second) < 1e-10:
        return None
    return float(np.corrcoef(first, second)[0, 1])


def physical_points(root, record):
    points = np.loadtxt(root / record["file"], dtype=np.float32, usecols=(0, 1, 2))
    info = record["pointCloudInfo"]
    if info.get("normalization") == "centered_by_mean_and_scaled_by_max_radius":
        return points, points.astype(float) * info["scale"] + np.array(info["center"])
    return points, points.astype(float)


def point_features(points, record, yaw):
    # Match the checkpoint's float32, centroid-rotation, physical-span pipeline.
    canonical = orientation.rotate_points_z(points, -yaw)
    scale = record["pointCloudInfo"]["scale"]
    normalized = record["pointCloudInfo"].get("normalization") == "centered_by_mean_and_scaled_by_max_radius"
    unit = float(scale) if normalized else 1.0
    full = np.ptp(canonical, axis=0).astype(np.float32) * unit
    bounds = np.quantile(canonical, [.005, .01, .99, .995], axis=0) * unit
    return full, bounds[3] - bounds[0], bounds[2] - bounds[1]


def far_point_stats(physical, params):
    radians = np.deg2rad(params["rz"])
    c, s = np.cos(radians), np.sin(radians)
    local = physical.copy()
    local[:, :2] = physical[:, :2] @ np.array([[c, -s], [s, c]])
    length, width, height = (params[k] for k in ("outerLength", "outerWidth", "height"))
    x, y = length * (1 - params["cutoutLengthRatio"]), width * (1 - params["cutoutWidthRatio"])

    def box_distance(low, high):
        delta = np.maximum(np.maximum(np.array(low) - local, local - np.array(high)), 0)
        return np.linalg.norm(delta, axis=1)

    outside = np.minimum(box_distance([0, 0, 0], [x, width, height]),
                         box_distance([x, 0, 0], [length, y, height]))
    # Ground-truth boxes are diagnostic only, not a usable real-cloud filter.
    threshold = .05 * np.linalg.norm([length, width, height])
    extrema = [int(local[:, axis].argmin()) for axis in (0, 1)]
    extrema += [int(local[:, axis].argmax()) for axis in (0, 1)]
    return {"far_point_count": int((outside > threshold).sum()),
            "farthest_outside_m": float(outside.max()),
            "far_points_drive_xy_extrema": bool(np.any(outside[extrema] > threshold))}


def main():
    meta_path = Path("metadata/sample_params_LHouse.json")
    raw = {r["file"]: r for r in read_json(DATA / meta_path)}
    augmented = {r["file"]: r for r in read_json(AUGMENTED / meta_path)}
    assert len(raw) == len(augmented) == 1000 and set(raw) == set(augmented)
    predictions = {}
    configs = {}
    input_paths = [DATA / meta_path, AUGMENTED / meta_path, Path(__file__).resolve(),
                   REPO / "scripts/canonical_orientation.py", LOGS.parent / "main_reg.py"]
    for model, tag in MODELS.items():
        directory = LOGS / f"ptv3_reg_lhouse_{tag}_normals"
        configs[model] = read_json(directory / "regression_config.json")
        input_paths.append(directory / "regression_config.json")
        for split in ("val", "test"):
            path = directory / f"regression_predictions_{split}.csv"
            input_paths.append(path)
            with path.open(encoding="utf-8-sig", newline="") as stream:
                for item in csv.DictReader(stream):
                    predictions[model, split, Path(item["file"]).name] = item
    records = []
    feature_vectors = []
    for rel, record in augmented.items():
        assert raw[rel]["params"] == record["params"], rel
        params = record["params"]
        target = np.array([params[k] for k in ("outerLength", "outerWidth", "height")])
        row = {"file": rel, "split": record["split"], **params}
        for source, root, item in (("raw", DATA, raw[rel]), ("aug", AUGMENTED, record)):
            points, physical = physical_points(root, item)
            row[f"{source}_sha256"] = hashlib.sha256((root / rel).read_bytes()).hexdigest()
            full, trimmed_half, trimmed_one = point_features(points, item, params["rz"])
            for kind, size in (("full", full), ("trim005", trimmed_half), ("trim01", trimmed_one)):
                for index, dimension in enumerate(("length", "width", "height")):
                    row[f"{source}_{kind}_{dimension}"] = float(size[index])
                    row[f"{source}_{kind}_{dimension}_relative_error"] = float(size[index] / target[index] - 1)
            row.update({f"{source}_{k}": v for k, v in far_point_stats(physical, params).items()})
            if source == "aug" and record["split"] == "train":
                feature_vectors.append([*full, item["pointCloudInfo"]["scale"],
                                        float(np.prod(full)), float(full[1]) / max(float(full[0]), 1e-6)])
        if record["split"] in ("val", "test"):
            for model in MODELS:
                predicted = predictions[model, record["split"], Path(rel).name]
                for name in TARGETS:
                    assert abs(float(predicted["true_" + name]) - params[name]) < 3e-5
                row[f"{model}_length_error"] = float(predicted["pred_outerLength"]) - params["outerLength"]
                row[f"{model}_pred_length"] = float(predicted["pred_outerLength"])
        records.append(row)
        if len(records) % 250 == 0:
            print(f"Audited raw/augmented pairs: {len(records)}/1000", flush=True)

    features = np.asarray(feature_vectors, dtype=np.float32)
    assert features.shape == (800, 6)
    feature_mean, feature_std = features.mean(0), features.std(0)
    for config in configs.values():
        np.testing.assert_allclose(feature_mean, config["aux_mean"], rtol=2e-6, atol=2e-5)
        np.testing.assert_allclose(feature_std, config["aux_std"], rtol=2e-6, atol=2e-5)
    report = {"scope": "Read-only observation, not a causal network ablation or a new inference result. "
                       "All prediction correlations use saved metadata-yaw results; test is descriptive only. "
                       "Quantile alternatives are diagnostics, not a deployable replacement for checkpoint features.",
              "label_pairs_matched": len(records), "training_feature_mean_matches_checkpoints": True,
              "training_feature_mean": feature_mean.tolist(), "training_feature_std": feature_std.tolist(),
              "splits": {}, "prediction_checks": {}, "focus": []}
    for split in ("train", "val", "test"):
        group = [r for r in records if r["split"] == split]
        result = {"count": len(group), "features": {}}
        for source in ("raw", "aug"):
            for kind in ("full", "trim005", "trim01"):
                for dimension in ("length", "width", "height"):
                    key = f"{source}_{kind}_{dimension}"
                    error = np.array([r[key + "_relative_error"] for r in group])
                    target_key = {"length": "outerLength", "width": "outerWidth", "height": "height"}[dimension]
                    absolute_error = [abs(r[key] - r[target_key]) for r in group]
                    result["features"][key] = {"relative_error": summary(error),
                                               "mae_m": float(np.mean(absolute_error)),
                                               "inflated_over_10_percent": int((error > .1).sum())}
        result["aug_far_point_count"] = summary([r["aug_far_point_count"] for r in group])
        result["aug_far_points_drive_xy_extrema"] = sum(r["aug_far_points_drive_xy_extrema"] for r in group)
        report["splits"][split] = result
        if split == "train":
            continue
        checks = {}
        for model in MODELS:
            err = np.array([r[f"{model}_length_error"] for r in group])
            covariates = np.array([[1, r["outerLength"], r["outerWidth"],
                                    r["cutoutLengthRatio"], r["cutoutWidthRatio"]] for r in group])
            error_residual = err - covariates @ np.linalg.lstsq(covariates, err, rcond=None)[0]
            checks[model] = {"bands": [], "correlations": {}}
            for low, high in ((0, 20), (20, 30), (30, 100)):
                indices = [i for i, r in enumerate(group) if low <= r["outerLength"] < high]
                train_count = sum(r["split"] == "train" and low <= r["outerLength"] < high for r in records)
                checks[model]["bands"].append({"range": [low, high], "train_count": train_count,
                                                "count": len(indices), "mae_m": float(np.abs(err[indices]).mean()),
                                                "signed_bias_m": float(err[indices].mean())})
            for dimension, target_key in (("length", "outerLength"), ("width", "outerWidth")):
                inflation = np.array([r[f"aug_full_{dimension}"] - r[target_key] for r in group])
                residual = inflation - covariates @ np.linalg.lstsq(covariates, inflation, rcond=None)[0]
                checks[model]["correlations"][dimension] = {
                    "pearson_signed_error": correlation(inflation, err),
                    "partial_controlling_true_shape": correlation(residual, error_residual)}
        report["prediction_checks"][split] = checks

    train = [r for r in records if r["split"] == "train"]
    report["training_length_range"] = [min(r["outerLength"] for r in train), max(r["outerLength"] for r in train)]
    for number in (4, 67, 88):
        r = next(r for r in records if r["file"] == f"test/LHouse/sample_{number:05}.txt")
        focus = dict(r)
        focus["nearby_training_count"] = sum(abs(t["outerLength"] - r["outerLength"]) <= 2
                                              and abs(t["outerWidth"] - r["outerWidth"]) <= 1
                                              and abs(t["cutoutLengthRatio"] - r["cutoutLengthRatio"]) < .01
                                              and abs(t["cutoutWidthRatio"] - r["cutoutWidthRatio"]) < .01 for t in train)
        report["focus"].append(focus)

    report["inputs_sha256"] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in input_paths}
    fieldnames = list(dict.fromkeys(key for row in records for key in row))
    with (OUTPUT / "per_sample.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)
    (OUTPUT / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"label_pairs_matched": report["label_pairs_matched"],
                      "training_feature_mean_matches_checkpoints": True,
                      "training_length_range": report["training_length_range"],
                      "prediction_checks": report["prediction_checks"],
                      "output": str(OUTPUT)}, indent=2))


if __name__ == "__main__":
    main()
