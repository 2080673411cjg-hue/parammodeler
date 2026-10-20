"""Run per-class PTv3 regression with the existing v6 targets/loss recipe."""

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time


# Stable-footprint targets avoid length/width swaps for rectangular primitives.
RECIPES = [
    ("Cuboid", "cuboid", "geom_footprint_long geom_footprint_short geom_total_height", "1.5 1.5 1.4"),
    ("Cylinder", "cylinder", "radius height", "1.5 1.4"),
    ("LHouse", "lhouse", "outerLength outerWidth cutoutLengthRatio cutoutWidthRatio height", "1.5 1.5 1 1 1.4"),
    ("ConeCylinder", "conecylinder", "radius totalHeight cylinderRatio", "1.5 1.4 1"),
    ("GabledRoof", "gabledroof", "length width totalHeight wallRatio ridgeRatio", "1.5 1.5 1.4 0.9 0.9"),
    ("PyramidRoof", "pyramidroof", "geom_footprint_long geom_footprint_short geom_total_height wallRatio", "1.5 1.5 1.4 0.9"),
    ("TruncatedPyramidRoof", "truncatedpyramid", "geom_footprint_long geom_footprint_short geom_top_long_ratio geom_top_short_ratio geom_total_height wallRatio", "1.5 1.5 1 1 1.4 0.9"),
    ("HalfCylinderRoof", "halfcylinder", "length width wallHeight", "1.5 1.5 1.3"),
    ("CylinderDome", "cylinderdome", "radius totalHeight cylinderRatio bulge", "1.5 1.4 1 0.8"),
    ("IndentedCuboid", "indentedcuboid", "geom_footprint_long geom_footprint_short geom_total_height innerHeight geom_long_start_margin_ratio geom_long_end_margin_ratio geom_short_start_margin_ratio geom_short_end_margin_ratio", "1.5 1.5 1.4 1.2 0.9 0.9 0.9 0.9"),
    ("AsymmetricGableHouse", "asymgable", "length width totalHeight wallRatio ridgeLength ridgeRatio", "1.5 1.5 1.4 0.9 1 0.9"),
    ("FourStageRoundTower", "fourstage", "baseRadius baseHeight middleHeight middleTopRadius middleRise coneHeight eaveRadius eaveHeight", "1.5 1.3 1.2 1.1 0.8 1.2 1.0 1.0"),
    ("TwoGableHouses", "twogable", "length1 length2 width totalHeight wallRatio angle ridgeRatio", "1.4 1.4 1.5 1.4 0.9 0.8 0.9"),
]


def validate_split_counts(data_root, recipes, expected):
    metadata = Path(data_root) / "metadata/sample_params.json"
    records = json.loads(metadata.read_text(encoding="utf-8"))
    if not isinstance(records, list):
        raise ValueError(f"Expected a JSON array in {metadata}")
    classes = {recipe[0] for recipe in recipes}
    counts = {name: {split: 0 for split in ("train", "val", "test")} for name in classes}
    paths = set()
    for record in records:
        name = record.get("type")
        if name not in classes:
            continue
        split = record.get("split")
        rel = record.get("file")
        if split not in counts[name] or not isinstance(rel, str) or not rel or rel in paths:
            raise ValueError(f"Invalid or duplicate metadata record for {name}: {rel}")
        parts = Path(rel).parts
        if len(parts) < 3 or parts[0] != split or parts[1] != name:
            raise ValueError(f"Metadata path does not match class/split: {rel}")
        paths.add(rel)
        counts[name][split] += 1
        if not (Path(data_root) / rel).is_file():
            raise ValueError(f"Missing augmented point cloud: {rel}")
    for name in sorted(classes):
        actual = tuple(counts[name][split] for split in ("train", "val", "test"))
        if actual != tuple(expected):
            raise ValueError(f"{name}: expected train/val/test={tuple(expected)}, found {actual}")
    return counts


def validate_tower_targets(data_root, recipes):
    if not any(recipe[0] == "FourStageRoundTower" for recipe in recipes):
        return
    records = json.loads((Path(data_root) / "metadata/sample_params.json").read_text(encoding="utf-8"))
    required = next(recipe[2].split() for recipe in recipes if recipe[0] == "FourStageRoundTower")
    found = 0
    for record in records:
        if record.get("type") != "FourStageRoundTower":
            continue
        found += 1
        params = record.get("params", {})
        missing = [name for name in required if name not in params]
        if missing or "middleBulge" in params:
            raise ValueError(f"{record.get('file')}: missing {missing} or legacy middleBulge label. Regenerate the tower dataset with middleRise in metres and physical eave dimensions; old labels cannot be converted directly.")
        if any(isinstance(params[name], bool) or not isinstance(params[name], (int, float)) or not math.isfinite(params[name]) for name in required):
            raise ValueError(f"{record.get('file')}: non-finite tower parameters")
        radius, height = params["baseRadius"], params["baseHeight"]
        if radius <= 0 or height <= 0:
            raise ValueError(f"{record.get('file')}: invalid tower base dimensions")
        if params["eaveRadius"] <= radius:
            raise ValueError(f"{record.get('file')}: eaveRadius must exceed baseRadius")
        if not 0 < params["middleTopRadius"] < params["eaveRadius"]:
            raise ValueError(f"{record.get('file')}: middleTopRadius must be positive and below eaveRadius")
        if not 0 < params["eaveHeight"] < height:
            raise ValueError(f"{record.get('file')}: eaveHeight must be positive and below baseHeight")
        if params["middleHeight"] <= 0 or not 0 <= params["middleRise"] <= params["middleHeight"] * 0.25:
            raise ValueError(f"{record.get('file')}: middleRise must be between zero and middleHeight/4")
    if not found:
        raise ValueError("No FourStageRoundTower samples found in metadata")


def validate_gabled_targets(data_root, recipes):
    if not any(recipe[0] == "GabledRoof" for recipe in recipes):
        return
    records = json.loads((Path(data_root) / "metadata/sample_params.json").read_text(encoding="utf-8-sig"))
    found = 0
    for record in records:
        if record.get("type") != "GabledRoof":
            continue
        found += 1
        ratio = record.get("params", {}).get("ridgeRatio")
        if isinstance(ratio, bool) or not isinstance(ratio, (int, float)) or not math.isfinite(ratio) or not 0.2 <= ratio <= 0.8:
            raise ValueError(f"{record.get('file')}: missing/invalid ridgeRatio. Regenerate GabledRoof with ridge offset; legacy labels cannot train this target.")
    if not found:
        raise ValueError("No GabledRoof samples found in metadata")


def commands(args, recipe):
    name, slug, targets, weights = recipe
    log = Path(args.log_root) / f"ptv3_reg_{slug}_{args.run_tag}_normals"
    common = [sys.executable, "-u", str(Path(__file__).with_name("main_reg.py")),
              "--backbone", "ptv3", "--data_root", str(args.data_root),
              "--metadata", str(Path(args.data_root) / "metadata/sample_params.json"),
              "--class_name", name, "--log_dir", str(log), "--num_points", "2048",
              "--batch_size", str(args.batch_size), "--seed", str(args.seed)]
    if args.ptv3_repo:
        common += ["--ptv3_repo", args.ptv3_repo]
    if getattr(args, "canonical_align", False):
        common += ["--canonical_align"]
    target_names = targets.split()
    uses_stable_geometry_targets = any(t.startswith("geom_") for t in target_names)
    relative_loss_weight = "0.0" if uses_stable_geometry_targets else "0.05"
    train = common + ["--mode", "train", "--targets", *target_names,
                     "--target_weights", *[f"{t}={w}" for t, w in zip(target_names, weights.split())],
                     *([] if not uses_stable_geometry_targets else ["--target_mode", "bbox_residual"]),
                     "--epochs", str(args.epochs), "--lr", "3e-4", "--weight_decay", "1e-4",
                     "--smooth_l1_beta", "1.0", "--relative_loss_weight", relative_loss_weight,
                     "--relative_loss_eps", "0.05", "--train_jitter_sigma", "0.003",
                     "--geom_aux_targets", "geom_footprint_long", "geom_footprint_short",
                     "geom_total_height", "geom_aspect_xy", "--geom_loss_weight", "0.2",
                     "--aux_features", "bbox_x", "bbox_y", "bbox_z", "scale", "bbox_volume",
                     "aspect_ratio_xy", "--use_normals", "--ptv3_grid_size", str(args.grid_size),
                     "--ptv3_patch_size", str(args.patch_size)]
    if name == "TwoGableHouses":
        train += ["--circular_targets", "angle"]
    if name == "IndentedCuboid" and getattr(args, "indented_symmetry_180", False):
        train += ["--indented_symmetry_180"]
    if name == "AsymmetricGableHouse" and getattr(args, "asym_ridge_symmetry_180", False):
        train += ["--asym_ridge_symmetry_180"]
    if name == "GabledRoof" and getattr(args, "gabled_ridge_symmetry_180", False):
        train += ["--gabled_ridge_symmetry_180"]
    if args.flash:
        train += ["--ptv3_flash"]
    return log, train, common + ["--mode", "test", "--split", "test"]


def format_duration(seconds):
    total_tenths = max(0, round(seconds * 10))
    whole_seconds, tenths = divmod(total_tenths, 10)
    hours, remainder = divmod(whole_seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{tenths:01d}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--classes", nargs="+", choices=[r[0] for r in RECIPES])
    parser.add_argument("--data_root", default=os.environ.get("DATA_ROOT", str(Path(__file__).resolve().parent.parent / "datasets_aug")))
    parser.add_argument("--log_root", default="logs")
    parser.add_argument("--run_tag", default="v6")
    parser.add_argument("--batch_size", type=int, default=4)
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--grid_size", type=float, default=0.02)
    parser.add_argument("--patch_size", type=int, default=128)
    parser.add_argument("--ptv3_repo", default=None)
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--canonical_align", action=argparse.BooleanOptionalAction, default=True,
                        help="Train with the metadata-derived canonical frame and save inference settings (default: enabled).")
    parser.add_argument("--indented_symmetry_180", action="store_true",
                        help="Use an experimental 180-degree symmetry-aware loss for IndentedCuboid only.")
    parser.add_argument("--asym_ridge_symmetry_180", action="store_true",
                        help="Experimental q/1-q loss for AsymmetricGableHouse only; pose disambiguation is still required.")
    parser.add_argument("--gabled_ridge_symmetry_180", action="store_true",
                        help="Experimental q/1-q loss for GabledRoof only.")
    parser.add_argument("--expected_split_counts", nargs=3, type=int,
                        metavar=("TRAIN", "VAL", "TEST"),
                        help="Require exact per-class split counts and matching point-cloud files before training.")
    parser.add_argument("--dry_run", action="store_true")
    args = parser.parse_args()
    selected = [r for r in RECIPES if args.classes is None or r[0] in args.classes]
    if args.asym_ridge_symmetry_180 and not any(r[0] == "AsymmetricGableHouse" for r in selected):
        parser.error("--asym_ridge_symmetry_180 requires AsymmetricGableHouse in --classes")
    if args.gabled_ridge_symmetry_180 and not any(r[0] == "GabledRoof" for r in selected):
        parser.error("--gabled_ridge_symmetry_180 requires GabledRoof in --classes")
    if args.canonical_align and (args.indented_symmetry_180
                                 or args.asym_ridge_symmetry_180
                                 or args.gabled_ridge_symmetry_180):
        parser.error("Canonical training transforms directional labels; do not combine it with 180-degree symmetry losses.")
    if args.epochs < 1 or args.batch_size < 1 or args.grid_size <= 0 or args.patch_size < 1:
        parser.error("epochs/batch_size/grid_size/patch_size must be positive")
    if args.expected_split_counts and any(value < 1 for value in args.expected_split_counts):
        parser.error("--expected_split_counts values must be positive")
    if not args.dry_run:
        root = Path(args.data_root)
        for path in [root / "metadata/sample_params.json", root / "train", root / "val", root / "test"]:
            if not path.exists():
                parser.error(f"Missing {path}; a separate validation split is required")
        if args.expected_split_counts:
            try:
                validate_split_counts(root, selected, args.expected_split_counts)
            except ValueError as exc:
                parser.error(str(exc))
        try:
            validate_tower_targets(root, selected)
            validate_gabled_targets(root, selected)
        except ValueError as exc:
            parser.error(str(exc))
        # Check all output destinations before starting the first class.
        for recipe in selected:
            log, _, _ = commands(args, recipe)
            if log.exists() and any(log.iterdir()):
                parser.error(f"Output already contains files: {log}. Use a new --run_tag.")
        subprocess.run([sys.executable, str(Path(__file__).with_name("check_ptv3.py")),
                        *(["--ptv3_repo", args.ptv3_repo] if args.ptv3_repo else []),
                        *(["--flash"] if args.flash else [])], check=True)
    for recipe in selected:
        log, train, test = commands(args, recipe)
        print(f"\n{recipe[0]} -> {log}", flush=True)
        if args.dry_run:
            import shlex
            print(shlex.join(train))
            print(shlex.join(test))
            continue

        train_started = time.perf_counter()
        print(f"{recipe[0]} training started (includes data/normal preparation, validation, and checkpoint save).", flush=True)
        try:
            subprocess.run(train, check=True)
        finally:
            train_elapsed = time.perf_counter() - train_started
            print(f"{recipe[0]} training duration: {format_duration(train_elapsed)} (HH:MM:SS.s)", flush=True)

        subprocess.run(test, check=True)


if __name__ == "__main__":
    main()
