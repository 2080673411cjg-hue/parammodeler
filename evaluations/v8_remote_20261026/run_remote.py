"""One-off real HTTP inference check; all outputs stay beside this script."""
import concurrent.futures
import importlib.util
import json
from pathlib import Path
import statistics
import sys
import time
from types import SimpleNamespace
import urllib.request
from unittest.mock import patch

import numpy as np

OUT = Path(__file__).resolve().parent.parent / "v8_remote_tailscale_20261026"
ROOT = OUT.parents[1]
DATA = Path(r"E:\pointnet\datasets_aug")
LOGS = Path(r"E:\pointnet\ptv3_simple\logs")
SERVER = "http://100.70.215.84:8008"
sys.path.insert(0, str(ROOT / "scripts"))
from train_ptv3_reg import RECIPES
from compare_ptv3_reg import read_json, sha256, write_csv
spec = importlib.util.spec_from_file_location("remote_check_targets", ROOT / "scripts/main_reg.py")
regression = importlib.util.module_from_spec(spec)
# No local model is constructed: the real network runs on the Ubuntu service.
with patch.dict(sys.modules, {"model_ptv3": SimpleNamespace(PTv3Regressor=object)}):
    spec.loader.exec_module(regression)


def request(row):
    started = time.perf_counter()
    info = row["pointCloudInfo"]
    bbox = info["bboxSize"]
    coordinates = ("normalized" if info.get("normalization") ==
                   "centered_by_mean_and_scaled_by_max_radius" else "physical")
    payload = dict(points_text=(DATA / row["file"]).read_text(encoding="utf-8-sig"),
                   primitive_type=row["type"], bbox_x=bbox[0], bbox_y=bbox[1],
                   bbox_z=bbox[2], scale=info["scale"], num_points=2048,
                   model_suffix="_v8_normals", input_coordinates=coordinates)
    result = {"file": row["file"], "class": row["type"], "band": row["horizontalScaleBand"]}
    try:
        req = urllib.request.Request(SERVER + "/regress", data=json.dumps(payload).encode(),
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=180) as stream:
            response = json.load(stream)
        result["response"] = response
        if not response.get("ok"):
            raise ValueError(response.get("error", "Unknown server error"))
        expected = f'ptv3_reg_{STEMS[row["type"]]}_v8_normals'
        if response.get("model_log_dir", "").rstrip("/").split("/")[-1] != expected:
            raise ValueError(f"Wrong checkpoint: {response.get('model_log_dir')}; expected {expected}")
        prediction = response["result"]
        targets = CONFIGS[row["type"]]["targets"]
        values = np.array([prediction["params"][target] for target in targets], dtype=float)
        if not np.isfinite(values).all():
            raise ValueError("Non-finite predicted parameter")
        truth = regression.target_vector(row, targets)
        if regression.canonical_angle_from_metadata(row, row["type"],
                CONFIGS[row["type"]].get("lhouse_canonical", False)) is not None:
            if not prediction.get("canonical_alignment", {}).get("applied"):
                raise ValueError("Expected canonical alignment was not applied")
            truth = regression.canonical_target_values(truth, targets, row, row["type"],
                                                       float(prediction["poseRotateZ"]))
        result.update(ok=True, truth=dict(zip(targets, map(float, truth))),
                      prediction=dict(zip(targets, map(float, values))))
    except Exception as exc:
        result.update(ok=False, error=f"{type(exc).__name__}: {exc}")
    result["seconds"] = time.perf_counter() - started
    return result


def save(result):
    path = OUT / "responses" / result["class"] / (Path(result["file"]).stem + ".json")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")


STEMS = {name: stem for name, stem, _, _ in RECIPES}
CONFIGS = {name: read_json(LOGS / f"ptv3_reg_{stem}_v8_normals/regression_config.json")
           for name, stem, _, _ in RECIPES}


def main():
    OUT.mkdir(exist_ok=True)
    if (OUT / "manifest.json").exists():
        raise SystemExit("Refusing to overwrite existing remote results")
    records = [row for row in read_json(DATA / "metadata/sample_params.json")
               if row.get("split") == "test" and row.get("type") in STEMS]
    by_file = {row["file"]: row for row in records}
    selected = {}
    for name in STEMS:
        for band in (0, 1, 2):
            group = sorted([row for row in records if row["type"] == name and
                            row["horizontalScaleBand"] == band], key=lambda row: row["file"])
            for index in np.linspace(0, len(group) - 1, 10, dtype=int):
                selected[group[index]["file"]] = group[index]
    audit = ROOT / "evaluations/v8_orientation_cpu_20261026"
    for name in ("axis_outliers_gt10.json", "failures.json"):
        for row in read_json(audit / name):
            selected[row["file"]] = by_file[row["file"]]
    # Include the previously discussed LHouse regression examples as well.
    for number in (4, 44, 67, 88, 93):
        key = f"test/LHouse/sample_{900 + number:05d}.txt"
        selected[key] = by_file[key]
    rows = list(selected.values())
    manifest = {"status": "running", "server": SERVER, "model_suffix": "_v8_normals",
                "selection": "10 evenly spaced files per class/scale band plus orientation outliers and five LHouse examples",
                "workers": 2, "planned": len(rows), "completed": 0,
                "files": [{"file": row["file"], "sha256": sha256(DATA / row["file"])} for row in rows]}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f'Planned {len(rows)} real remote inferences; two concurrent requests.', flush=True)
    first = [next(row for row in rows if row["type"] == name) for name in STEMS]
    rest = [row for row in rows if row["file"] not in {item["file"] for item in first}]
    results = []
    started = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        for batch in (first, rest):
            for result in pool.map(request, batch):
                save(result)
                results.append(result)
                manifest.update(completed=len(results), failed=sum(not item["ok"] for item in results))
                (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
                if len(results) <= 13 or len(results) % 10 == 0 or not result["ok"]:
                    print(f'{len(results)}/{len(rows)} {result["file"]} ok={result["ok"]} '
                          f'seconds={result["seconds"]:.2f} {result.get("error", "")[:240]}', flush=True)
            if batch is first and any(not item["ok"] for item in results):
                raise RuntimeError("At least one class failed the v8 routing preflight; inspect saved responses")
    summary, failures = [], []
    for name in STEMS:
        group = [item for item in results if item["class"] == name]
        good = [item for item in group if item["ok"]]
        for target in CONFIGS[name]["targets"]:
            errors = np.array([abs(item["prediction"][target] - item["truth"][target]) for item in good])
            summary.append(dict(class_name=name, target=target, planned=len(group), accepted=len(good),
                                failed=len(group)-len(good), mae=float(errors.mean()),
                                p95=float(np.percentile(errors, 95)),
                                rmse=float(np.sqrt(np.mean(errors**2)))))
        failures.extend({"class": item["class"], "file": item["file"], "error": item["error"]}
                        for item in group if not item["ok"])
    write_csv(OUT / "summary.csv", summary)
    write_csv(OUT / "failures.csv", failures, ["class", "file", "error"])
    manifest.update(status="complete", elapsed_seconds=time.perf_counter()-started,
                    request_median_seconds=statistics.median(item["seconds"] for item in results))
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print("ALL_V8_REMOTE_DONE", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (Exception, KeyboardInterrupt) as exc:
        path = OUT / "manifest.json"
        if path.exists():
            manifest = read_json(path)
            if manifest.get("status") == "running":
                manifest.update(status="interrupted" if isinstance(exc, KeyboardInterrupt) else "failed",
                                error=str(exc))
                path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        raise
