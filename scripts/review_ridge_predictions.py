"""Audit ridge position and magnitude without treating TwoGableHouses as symmetric."""

import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def review(path, allow_180):
    with path.open(encoding="utf-8-sig", newline="") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise ValueError(f"No predictions: {path}")
    true = [float(row["true_ridgeRatio"]) for row in rows]
    pred = [float(row["pred_ridgeRatio"]) for row in rows]
    if not all(math.isfinite(value) for value in true + pred):
        raise ValueError(f"Non-finite ridge prediction: {path}")
    mean = statistics.mean
    result = {"samples": len(rows), "direct_mae": mean(abs(a-b) for a, b in zip(true, pred)),
              "magnitude_mae": mean(abs(abs(a-.5)-abs(b-.5)) for a, b in zip(true, pred)),
              "true_offset_mean": mean(abs(a-.5) for a in true),
              "pred_offset_mean": mean(abs(b-.5) for b in pred),
              "pred_near_center_count": sum(abs(b-.5) < .1 for b in pred)}
    if allow_180:
        result["equivalent_mae"] = mean(min(abs(a-b), abs(1-a-b)) for a, b in zip(true, pred))
        result["pose_disambiguation_required"] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log_root", type=Path, required=True)
    parser.add_argument("--run_tag", default="v3")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = {}
    for name, slug in (("AsymmetricGableHouse", "asymgable"), ("TwoGableHouses", "twogable"), ("GabledRoof", "gabledroof")):
        for split in ("val", "test"):
            path = args.log_root / f"ptv3_reg_{slug}_{args.run_tag}_normals" / f"regression_predictions_{split}.csv"
            if path.is_file():
                # Legacy GabledRoof checkpoints do not contain a ridge target.
                with path.open(encoding="utf-8-sig", newline="") as source:
                    if "true_ridgeRatio" not in next(csv.reader(source)):
                        continue
                report[f"{name}/{split}"] = review(path, name in ("AsymmetricGableHouse", "GabledRoof"))
    if not report:
        parser.error("No matching ridge prediction files found")
    text = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
