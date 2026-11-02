"""CPU-only normal-preparation comparison; no network or dataset writes."""

import hashlib
import json
from pathlib import Path
import sys
import time
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
# The benchmark exercises the production NumPy path without loading PTv3/CUDA.
sys.modules["model_ptv3"] = SimpleNamespace(PTv3Regressor=object)
from main_reg import load_txt_points, normalize_points, precompute_normals


if __name__ == "__main__":
    output = Path(__file__).with_name("result.json")
    if output.exists():
        raise SystemExit(f"Preserving existing benchmark: {output}")
    root = Path(r"E:\pointnet\datasets_aug\test")
    paths = [path for folder in sorted(root.iterdir()) if folder.is_dir()
             and folder.name != "TriPrismPyramid" for path in sorted(folder.glob("*.txt"))[:10]]
    runs = []
    for workers in (0, 2):
        started = time.perf_counter()
        clouds = ((path, normalize_points(load_txt_points(path))) for path in paths)
        fingerprints = []
        for path, points, normals in precompute_normals(clouds, len(paths), "benchmark", workers):
            fingerprints.append({"file": str(path), "points": len(points),
                                 "normal_sha256": hashlib.sha256(normals.tobytes()).hexdigest()})
        runs.append({"workers": workers, "elapsed_seconds": time.perf_counter() - started,
                     "fingerprints": fingerprints})
    payload = {"scope": "Windows local CPU; includes reads, normalization, spawn and result collection",
               "cloud_count": len(paths), "same_results": runs[0]["fingerprints"] == runs[1]["fingerprints"],
               "source_sha256": hashlib.sha256((ROOT / "scripts/main_reg.py").read_bytes()).hexdigest(),
               "runs": runs}
    output.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in payload.items() if key != "runs"}))
    print("SERIAL_SECONDS", runs[0]["elapsed_seconds"], "PARALLEL_SECONDS", runs[1]["elapsed_seconds"])
    if not payload["same_results"]:
        raise SystemExit("Normal outputs differ")
