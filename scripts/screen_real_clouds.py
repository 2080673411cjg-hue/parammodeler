#!/usr/bin/env python
"""Read-only XYZ survey, deterministic pilot selection and v10-assisted review gallery."""

import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import random
import time
import urllib.error
import urllib.request

import numpy as np

from primitive_names import PRIMITIVES, canonical_class_name


PREPARATION_VERSION = "double_local_exact_dedup_v1"
EXPECTED_CLASSIFIER = "ptv3_cls_v10"


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    temporary.replace(path)


def select_pilot(root, output, count, seed):
    """Freeze selection before any classifier result is available."""
    selection_path = output / "selection.json"
    settings = {"source_root": str(root), "requested_count": count, "seed": seed,
                "preparation_version": PREPARATION_VERSION}
    if selection_path.exists():
        selection = json.loads(selection_path.read_text(encoding="utf-8"))
        if selection["settings"] != settings:
            raise ValueError("Output belongs to a different selection; use a new output directory")
        return selection
    files = sorted((Path(directory, name).relative_to(root).as_posix()
                    for directory, _, names in os.walk(root) for name in names
                    if Path(name).suffix.lower() in (".xyz", ".txt")))
    if not files:
        raise ValueError("No XYZ/TXT files found")
    selected = random.Random(seed).sample(files, min(count, len(files)))
    selection = {"settings": settings, "available_files": len(files), "files": selected,
                 "created_utc": datetime.now(timezone.utc).isoformat()}
    write_json(selection_path, selection)
    return selection


def prepare_xyz(content):
    # Translation must precede float32 conversion: northings around 6.5e6 lose detail in float32.
    original = np.loadtxt(io.BytesIO(content), dtype=np.float64, usecols=(0, 1, 2), ndmin=2)
    finite = np.isfinite(original).all(axis=1)
    points = original[finite]
    if not len(points):
        raise ValueError("No finite XYZ points")
    _, indices = np.unique(points, axis=0, return_index=True)
    unique = points[np.sort(indices)]
    lo, hi = unique.min(axis=0), unique.max(axis=0)
    origin = np.r_[(lo[:2] + hi[:2]) / 2, lo[2]]
    local = unique - origin
    flags = []
    if len(unique) < 2048:
        flags.append("点数不足2048，推理会重复抽样；仍可人工选用")
    if len(original) - len(points):
        flags.append("已移除非有限坐标")
    duplicates = len(points) - len(unique)
    if duplicates / len(points) > .05:
        flags.append("完全重复坐标超过5%")
    if np.ptp(local, axis=0).min() < 1e-6:
        flags.append("至少一个方向跨度接近零，需检查")
    stats = {"original_points": len(original), "finite_points": len(points), "unique_points": len(unique),
             "duplicate_points": duplicates, "invalid_points": int((~finite).sum()),
             "bbox_span": (hi - lo).tolist(), "source_bbox_min": lo.tolist(), "source_bbox_max": hi.tolist(),
             "origin": origin.tolist(), "flags": flags,
             "coordinate_rule": "local_xyz = source_xyz - origin; no scaling or rotation",
             "units": "unchanged source units; not independently verified",
             "z_reference": "minimum observed point, not verified ground"}
    return local, stats


def render_views(points, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    if len(points) > 8000:
        points = points[np.random.default_rng(0).choice(len(points), 8000, replace=False)]
    points = points - points.mean(axis=0)
    _, vectors = np.linalg.eigh(points[:, :2].T @ points[:, :2])
    direction = vectors[:, -1]
    if direction[0] < 0:
        direction = -direction
    # Orient previews only. The inference XYZ file remains unrotated.
    rotated = points.copy()
    rotated[:, :2] = points[:, :2] @ np.column_stack((direction, [-direction[1], direction[0]]))
    az, el = np.deg2rad(-55), np.deg2rad(30)
    right = np.array([-np.sin(az), np.cos(az), 0])
    up = np.array([-np.sin(el) * np.cos(az), -np.sin(el) * np.sin(az), np.cos(el)])
    depth = np.array([np.cos(el) * np.cos(az), np.cos(el) * np.sin(az), np.sin(el)])
    order = np.argsort(rotated @ depth)
    views = [(rotated[:, :2], np.arange(len(points)), "Top"),
             (np.column_stack((rotated @ right, rotated @ up)), order, "Oblique"),
             (rotated[:, [1, 2]], np.arange(len(points)), "Side")]
    fig, axes = plt.subplots(1, 3, figsize=(12, 4.2), dpi=100)
    try:
        for ax, (projection, index, title) in zip(axes, views):
            ax.scatter(projection[index, 0], projection[index, 1], c=rotated[index, 2],
                       cmap="viridis", s=1.8, linewidths=0, rasterized=True)
            ax.set_aspect("equal", adjustable="datalim")
            ax.set_title(title, fontsize=12)
            ax.margins(.07)
            ax.set_axis_off()
        fig.tight_layout(pad=.5)
        fig.savefig(path, facecolor="white")
    finally:
        plt.close(fig)


class RemoteClassifier:
    def __init__(self, server):
        self.server = server.rstrip("/")
        # Tailnet/private LAN requests must not use the desktop's unrelated HTTP proxy.
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with self.opener.open(self.server + "/health", timeout=10) as response:
            health = json.load(response)
        self.model_dir = health.get("cls_log", "")
        if self.model_dir.replace("\\", "/").rstrip("/").split("/")[-1] != EXPECTED_CLASSIFIER:
            raise ValueError(f"Expected {EXPECTED_CLASSIFIER}, service reports {self.model_dir!r}")

    def classify(self, input_file):
        request = urllib.request.Request(self.server + "/classify",
            data=json.dumps({"points_text": input_file.read_text(encoding="ascii"),
                             "num_points": 2048, "topk": 3}).encode("utf-8"),
            headers={"Content-Type": "application/json"})
        with self.opener.open(request, timeout=200) as response:
            result = json.load(response)
        if not result.get("ok"):
            raise ValueError(result.get("error", "Classification failed"))
        if result.get("model_log_dir") != self.model_dir:
            raise ValueError("Classifier version changed or was not confirmed by the service")
        predictions = result.get("result")
        if not isinstance(predictions, list) or not predictions:
            raise ValueError("No classification predictions returned")
        known = {row[1] for row in PRIMITIVES}
        for prediction in predictions:
            prediction["class"] = canonical_class_name(prediction["class"])
            probability = prediction.get("prob")
            if prediction["class"] not in known or not isinstance(probability, (float, int)) or not np.isfinite(probability) or not 0 <= probability <= 1:
                raise ValueError("Invalid classification result")
        return {"model_dir": self.model_dir, "predictions": predictions,
                "num_points": 2048, "status": "ok"}


def render_gallery(output, selection, records):
    data = {"selection": selection, "records": records,
            "classes": [{"id": row[1], "name": row[4]} for row in PRIMITIVES],
            "dataset_key": hashlib.sha256(selection["settings"]["source_root"].encode()).hexdigest()[:16],
            "updated_utc": datetime.now(timezone.utc).isoformat()}
    embedded = json.dumps(data, ensure_ascii=False, allow_nan=False).replace("<", "\\u003c")
    template = Path(__file__).with_name("real_cloud_gallery.html").read_text(encoding="utf-8")
    html = template.replace("__GALLERY_DATA__", embedded)
    temporary = output / "index.html.tmp"
    temporary.write_text(html, encoding="utf-8")
    temporary.replace(output / "index.html")
    write_json(output / "manifest.json", data)


def run(args):
    root, output = Path(args.input).resolve(strict=True), Path(args.output).resolve()
    if output == root or root in output.parents:
        raise ValueError("Output must be outside the source dataset")
    output.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(output / ".matplotlib"))
    for name in ("images", "inputs", "records"):
        (output / name).mkdir(exist_ok=True)
    selection = select_pilot(root, output, args.count, args.seed)
    records = []
    started = time.monotonic()
    print(f"Source files: {selection['available_files']} | pilot: {len(selection['files'])} | seed={args.seed}", flush=True)
    for number, relative in enumerate(selection["files"], 1):
        source = root / relative
        source_id = hashlib.sha256(relative.encode()).hexdigest()[:20]
        record_path = output / "records" / (source_id + ".json")
        record = {"id": source_id, "relative_path": relative, "source_path": str(source),
                  "name": source.name, "status": "error"}
        try:
            content = source.read_bytes()
            digest = hashlib.sha256(content).hexdigest()
            if record_path.exists():
                cached = json.loads(record_path.read_text(encoding="utf-8"))
                if cached.get("sha256") == digest and cached.get("preparation_version") == PREPARATION_VERSION and cached.get("status") == "ready" and (output / cached["image"]).exists() and (output / cached["input_relative"]).exists():
                    records.append(cached)
                    continue
            local, stats = prepare_xyz(content)
            record.update(stats, sha256=digest, preparation_version=PREPARATION_VERSION,
                          review_id=source_id + ":" + digest[:16],
                          image=f"images/{source_id}.png", input_relative=f"inputs/{source_id}.xyz",
                          input_path=str(output / "inputs" / (source_id + ".xyz")))
            np.savetxt(output / record["input_relative"], local, fmt="%.8f")
            render_views(local, output / record["image"])
            record["status"] = "ready"
        except Exception as exc:
            record["status"] = "error"
            record["error"] = f"{type(exc).__name__}: {exc}"
            record.setdefault("review_id", source_id)
        write_json(record_path, record)
        records.append(record)
        if number == 1 or number % 10 == 0 or number == len(selection["files"]):
            render_gallery(output, selection, records)
            print(f"Prepared {number}/{len(selection['files'])} | elapsed {time.monotonic()-started:.1f}s", flush=True)
    render_gallery(output, selection, records)
    if args.server:
        try:
            classifier = RemoteClassifier(args.server)
        except Exception as exc:
            print(f"Gallery is ready; classifier unavailable: {exc}. Rerun the same command to resume.", flush=True)
            return 2
        print(f"Classifying with {classifier.model_dir}; successful cached predictions are reused.", flush=True)
        try:
            for number, record in enumerate(records, 1):
                if record["status"] != "ready":
                    continue
                previous = record.get("classification", {})
                if previous.get("status") == "ok" and previous.get("model_dir") == classifier.model_dir:
                    continue
                try:
                    record["classification"] = classifier.classify(output / record["input_relative"])
                except (urllib.error.URLError, TimeoutError) as exc:
                    record["classification"] = {"status": "error", "error": f"{type(exc).__name__}: {exc}"}
                    write_json(output / "records" / (record["id"] + ".json"), record)
                    render_gallery(output, selection, records)
                    print(f"Service connection lost: {exc}. Completed results saved; rerun to resume.", flush=True)
                    return 2
                except Exception as exc:
                    record["classification"] = {"status": "error", "error": f"{type(exc).__name__}: {exc}"}
                write_json(output / "records" / (record["id"] + ".json"), record)
                render_gallery(output, selection, records)
                successful = sum(r.get("classification", {}).get("status") == "ok" for r in records)
                elapsed = time.monotonic() - started
                print(f"Classified {number}/{len(records)} | success={successful} | elapsed {elapsed:.1f}s", flush=True)
        except KeyboardInterrupt:
            render_gallery(output, selection, records)
            print("Interrupted; gallery and completed classifications saved. Rerun to resume.", flush=True)
            return 130
    successful = sum(r.get("classification", {}).get("status") == "ok" for r in records)
    errors = sum(r["status"] != "ready" or r.get("classification", {}).get("status") == "error" for r in records)
    print(f"Done: {len(records)} samples, {successful} classified, {errors} errors | {output / 'index.html'}", flush=True)
    return 1 if errors else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="Read-only XYZ/TXT source directory")
    parser.add_argument("--output", required=True, help="New, separate gallery/cache directory")
    parser.add_argument("--count", type=int, default=200)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--server", help="Optional existing v10 service, e.g. http://100.70.215.84:8008")
    args = parser.parse_args()
    if args.count <= 0:
        parser.error("--count must be positive")
    raise SystemExit(run(args))


if __name__ == "__main__":
    main()
