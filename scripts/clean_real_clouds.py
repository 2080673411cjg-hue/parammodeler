#!/usr/bin/env python
"""Conservative, reversible isolation cleanup of manually selected real clouds."""

import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path

import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from scipy.spatial import cKDTree

from primitive_names import PRIMITIVES
from screen_real_clouds import write_json


METHOD = "small_components_gap6_density_review_v1"


def isolation_masks(points):
    """Separate only tiny, very remote components; density warnings alone never remove points."""
    points = np.asarray(points, dtype=np.float64)
    if points.ndim != 2 or points.shape[1] != 3 or not len(points) or not np.isfinite(points).all():
        raise ValueError("Expected nonempty, finite Nx3 local XYZ")
    n = len(points)
    removed, review = np.zeros(n, dtype=bool), np.zeros(n, dtype=bool)
    if n < 16:
        review[:] = True
        return removed, review, {"too_sparse": True, "automatic_removal_held": True}
    distances, neighbors = cKDTree(points).query(points, k=min(9, n))
    bounds = np.quantile(points, [.01, .99], axis=0)
    radius = max(4 * float(np.quantile(distances[:, 1], .9)),
                 .02 * float(np.linalg.norm(bounds[1] - bounds[0])), np.finfo(float).eps)
    source = np.repeat(np.arange(n), distances.shape[1] - 1)
    target = neighbors[:, 1:].ravel()
    near = distances[:, 1:].ravel() <= radius
    graph = coo_matrix((np.ones(int(near.sum())), (source[near], target[near])), shape=(n, n)).tocsr()
    count, labels = connected_components(graph, directed=False)
    sizes = np.bincount(labels)
    small_limit = max(2, min(20, int(.005 * n)))
    body = sizes[labels] > small_limit
    components = []
    if body.any():
        tree = cKDTree(points[body])
        for component in np.where(sizes <= small_limit)[0]:
            rows = np.flatnonzero(labels == component)
            gap = float(tree.query(points[rows], k=1)[0].min())
            if gap > radius:
                review[rows] = True
                components.append({"rows": rows.tolist(), "gap_to_supported_components": gap,
                                   "strong_isolation": bool(gap > 6 * radius)})
            if gap > 6 * radius:
                removed[rows] = True
    density = distances[:, 1:].mean(axis=1)
    median = float(np.median(density))
    cutoff = max(float(np.quantile(density, .95)) * 1.5,
                 median + 6 * float(np.median(np.abs(density - median))))
    review |= density > cutoff
    # Extreme elevations can determine the whole bbox even when a small attachment is dense.
    # This is only a review hint: roof crests, ground and wall points remain in the cloud.
    review |= (points[:, 2] < bounds[0, 2]) | (points[:, 2] > bounds[1, 2])
    cap = max(5, min(100, int(.01 * n)))
    proposed = int(removed.sum())
    held = proposed > cap or not body.any()
    if held:
        review |= removed
        removed[:] = False
    if not body.any():
        review[:] = True
    review &= ~removed
    return removed, review, {"neighbor_radius": radius, "automatic_gap_threshold": 6 * radius,
                            "small_component_limit": small_limit, "components": count,
                            "automatic_point_cap": cap, "proposed_separated_points": proposed,
                            "automatic_removal_held": bool(held), "density_review_threshold": cutoff,
                            "height_review_bounds_01_99": bounds[:, 2].tolist(),
                            "small_disconnected_components": components}


def load_selections(paths):
    documents = [(Path(path), json.loads(Path(path).read_text(encoding="utf-8-sig"))) for path in paths]
    roots = {document["source_root"] for _, document in documents}
    if len(roots) != 1:
        raise ValueError("Selection files must refer to the same source dataset")
    records = {}
    for _, document in sorted(documents, key=lambda item: item[1]["exported_utc"]):
        for record in document["records"]:
            if record["human_review"]["state"] == "selected":
                records[record["id"]] = record
    known = {row[1] for row in PRIMITIVES}
    for record in records.values():
        if record["human_review"]["human_class"] not in known:
            raise ValueError(f"Unconfirmed class: {record['name']}")
        if Path(record["name"]).name != record["name"] or Path(record["name"]).suffix.lower() not in (".xyz", ".txt"):
            raise ValueError("Invalid source filename")
    if not records:
        raise ValueError("No selected samples")
    return list(records.values()), documents


def render_comparison(points, removed, review, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    centered = points - points.mean(axis=0)
    _, vectors = np.linalg.eigh(centered[:, :2].T @ centered[:, :2])
    direction = vectors[:, -1]
    if direction[0] < 0:
        direction = -direction
    centered[:, :2] = centered[:, :2] @ np.column_stack((direction, [-direction[1], direction[0]]))
    az, el = np.deg2rad(-55), np.deg2rad(30)
    right = np.array([-np.sin(az), np.cos(az), 0])
    up = np.array([-np.sin(el) * np.cos(az), -np.sin(el) * np.sin(az), np.cos(el)])
    projections = [centered[:, :2], np.c_[centered @ right, centered @ up], centered[:, [1, 2]]]
    index = np.arange(len(points))
    if len(points) > 10000:
        index = np.random.default_rng(0).choice(len(points), 10000, replace=False)
    fig, axes = plt.subplots(2, 3, figsize=(12, 7), dpi=120)
    try:
        for col, (projection, title) in enumerate(zip(projections, ["Top", "Oblique", "Side"])):
            lo, hi = projection.min(axis=0), projection.max(axis=0)
            margin = np.maximum(hi - lo, max(float(np.max(hi - lo)), 1e-6) * .02) * .07
            for row in range(2):
                ax = axes[row, col]
                chosen = index if row == 0 else index[~removed[index]]
                ax.scatter(projection[chosen, 0], projection[chosen, 1], s=1.8, c="#426b89", linewidths=0)
                if review.any():
                    ax.scatter(projection[review, 0], projection[review, 1], s=8, c="#db951b", linewidths=0)
                if row == 0 and removed.any():
                    ax.scatter(projection[removed, 0], projection[removed, 1], s=26, c="#c33749", marker="x", linewidths=.9)
                ax.set(xlim=(lo[0] - margin[0], hi[0] + margin[0]),
                       ylim=(lo[1] - margin[1], hi[1] + margin[1]), aspect="equal")
                ax.set_title(("Before / " if row == 0 else "Conservative / ") + title, fontsize=11)
                ax.set_axis_off()
        fig.suptitle("Same frame and view limits | Red: separated | Amber: review, still retained", fontsize=12)
        fig.tight_layout()
        fig.savefig(path, facecolor="white")
    finally:
        plt.close(fig)


def write_page(output, manifest):
    template = Path(__file__).with_name("real_cloud_cleaning.html").read_text(encoding="utf-8")
    embedded = json.dumps(manifest, ensure_ascii=False, allow_nan=False).replace("<", "\\u003c")
    (output / "index.html").write_text(template.replace("__CLEANING_DATA__", embedded), encoding="utf-8")


def run(paths, destination):
    records, documents = load_selections(paths)
    output = Path(destination).resolve()
    for record in records:
        source = Path(record["input_path"]).resolve(strict=True)
        if output == source or output in source.parents:
            raise ValueError("Output must not contain existing input files")
    output.mkdir(parents=True, exist_ok=False)
    os.environ.setdefault("MPLCONFIGDIR", str(output / ".matplotlib"))
    for name in ("original", "cleaned", "separated", "review_points", "images", "records", "selection_exports"):
        (output / name).mkdir()
    sources = []
    for number, (source, _) in enumerate(documents, 1):
        content = source.read_bytes()
        (output / "selection_exports" / f"{number}_{source.name}").write_bytes(content)
        sources.append({"path": str(source), "sha256": hashlib.sha256(content).hexdigest()})
    manifest = {"method": METHOD, "created_utc": datetime.now(timezone.utc).isoformat(),
                "purpose": "real_validation_cleanup_review_not_training", "selection_files": sources,
                "classes": {row[1]: row[4] for row in PRIMITIVES}, "records": []}
    for record in records:
        source = Path(record["input_path"])
        content = source.read_bytes()
        points = np.loadtxt(io.BytesIO(content), dtype=np.float64, usecols=(0, 1, 2), ndmin=2)
        removed, review, audit = isolation_masks(points)
        class_name, name = record["human_review"]["human_class"], record["name"]
        files = {}
        for group, values in (("original", points), ("cleaned", points[~removed]),
                              ("separated", points[removed]), ("review_points", points[review])):
            directory = output / group / class_name
            directory.mkdir(exist_ok=True)
            if not len(values):
                files[group] = None
                continue
            target = directory / name
            if group == "original":
                target.write_bytes(content)
            else:
                np.savetxt(target, values, fmt="%.8f")
            files[group] = str(target)
        item = {"id": record["id"], "name": name, "human_class": class_name,
                "human_review": record["human_review"], "source_input_path": str(source),
                "source_input_sha256": hashlib.sha256(content).hexdigest(),
                "original_source_path": record["source_path"], "original_source_sha256": record["sha256"],
                "original_origin": record["origin"], "coordinate_rule": "same local coordinates as input; no new rotation, translation or scaling",
                "before_points": len(points), "after_points": int((~removed).sum()),
                "separated_points": int(removed.sum()), "review_kept_points": int(review.sum()),
                "before_span": np.ptp(points, axis=0).tolist(), "after_span": np.ptp(points[~removed], axis=0).tolist(),
                "z_reference": "minimum observation is not verified ground; span is not building-height truth",
                "files": files, "image": f"images/{record['id']}.png", "audit": audit,
                "separated_input_rows_zero_based": np.flatnonzero(removed).tolist(),
                "review_kept_input_rows_zero_based": np.flatnonzero(review).tolist()}
        assert len(points) == item["after_points"] + item["separated_points"]
        restored = np.loadtxt(files["cleaned"], dtype=np.float64, ndmin=2)
        np.testing.assert_allclose(restored, points[~removed], rtol=0, atol=5.1e-9)
        if source.read_bytes() != content:
            raise RuntimeError(f"Input changed during processing: {source}")
        render_comparison(points, removed, review, output / item["image"])
        write_json(output / "records" / (item["id"] + ".json"), item)
        manifest["records"].append(item)
        print(f"{name}: {len(points)} -> {item['after_points']}; separated={item['separated_points']}, review-kept={item['review_kept_points']}", flush=True)
    write_json(output / "manifest.json", manifest)
    write_page(output, manifest)
    print(f"Done: {len(records)} samples | {output / 'index.html'}", flush=True)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selections", nargs="+", required=True)
    parser.add_argument("--output", required=True, help="New output directory; existing directories are never overwritten")
    args = parser.parse_args()
    run(args.selections, args.output)


if __name__ == "__main__":
    main()
