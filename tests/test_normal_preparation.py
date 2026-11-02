"""Check timed normal preparation and real spawned dataset preprocessing."""

import contextlib
import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("normal_preparation_regression", ROOT / "scripts/main_reg.py")
regression = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {"model_ptv3": SimpleNamespace(PTv3Regressor=object)}):
    spec.loader.exec_module(regression)


class NormalPreparationTests(unittest.TestCase):
    def test_serial_values_order_and_timing(self):
        clouds = [(str(i), np.random.default_rng(i).normal(size=(32, 3)).astype(np.float32))
                  for i in range(2)]
        expected = [regression.estimate_normals(points) for _, points in clouds]
        before = [points.copy() for _, points in clouds]
        stream = io.StringIO()
        with patch.object(regression.time, "perf_counter", side_effect=[0., 1., 2., 3.]), \
             patch.object(regression, "ProcessPoolExecutor", side_effect=AssertionError("serial")), \
             contextlib.redirect_stdout(stream):
            results = list(regression.precompute_normals(iter(clouds), 2, "train", 0))
        self.assertEqual([item[0] for item in results], ["0", "1"])
        for (_, points, normals), original, reference in zip(results, before, expected):
            np.testing.assert_array_equal(points, original)
            np.testing.assert_array_equal(normals, reference)
        self.assertIn("1/2 (50.0%) | elapsed 00:00:01 | ETA 00:00:01", stream.getvalue())
        self.assertIn("Normals train complete: 2 clouds | elapsed 00:00:03 (3.00s)", stream.getvalue())

    def test_one_worker_and_empty_input_do_not_start_a_pool(self):
        points = np.zeros((3, 3), dtype=np.float32)
        with patch.object(regression, "ProcessPoolExecutor", side_effect=AssertionError("serial")), \
             patch("builtins.print"):
            rows = list(regression.precompute_normals([("a", points)], 1, "test", 1))
            empty = list(regression.precompute_normals([], 0, "test", 2))
        np.testing.assert_array_equal(rows[0][2], np.zeros_like(points))
        self.assertEqual(empty, [])

    def test_negative_workers_and_preparation_failures_are_not_hidden(self):
        with self.assertRaisesRegex(ValueError, "non-negative"):
            list(regression.precompute_normals([], 0, "test", -1))
        stream = io.StringIO()
        with patch.object(regression, "estimate_normals", side_effect=ValueError("bad cloud")), \
             contextlib.redirect_stdout(stream), self.assertRaisesRegex(ValueError, "bad cloud"):
            list(regression.precompute_normals([("bad", np.zeros((4, 3)))], 1, "test"))
        self.assertNotIn("complete:", stream.getvalue())

    def test_real_spawn_preserves_normals_targets_rng_and_live_augmentation(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tests") as folder:
            root = Path(folder)
            for name in ("main.py", "main_reg.py", "canonical_orientation.py"):
                shutil.copy2(ROOT / "scripts" / name, root / name)
            (root / "model_ptv3.py").write_text(
                "class PTv3Regressor: pass\nclass PTv3Classifier: pass\n", encoding="utf-8")
            harness = root / "normal_smoke.py"
            harness.write_text('''
import contextlib
import io
import json
from pathlib import Path
import numpy as np
import torch
from main import PointCloudClassificationDataset
from main_reg import ParamRegressionDataset, precompute_normals

def compare(a, b):
    assert len(a) == len(b)
    for key in a._normals_cache:
        np.testing.assert_array_equal(a._normals_cache[key], b._normals_cache[key])
        assert not b._normals_cache[key].flags.writeable
    for key in a._points_cache:
        np.testing.assert_array_equal(a._points_cache[key], b._points_cache[key])
        assert not b._points_cache[key].flags.writeable
    for index in range(len(a)):
        np.random.seed(42)
        left = a[index]
        np.random.seed(42)
        right = b[index]
        for old, new in zip(left, right):
            if isinstance(old, torch.Tensor):
                torch.testing.assert_close(old, new, rtol=0, atol=0)
            else:
                assert old == new
    assert not torch.equal(b[0][0], b[0][0])

if __name__ == "__main__":
    root = Path(__file__).parent
    records = []
    for i in range(4):
        relative = f"train/GabledRoof/{i}.txt"
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        np.savetxt(path, np.random.default_rng(i).normal(size=(32, 3)))
        records.append({"type": "GabledRoof", "split": "train", "file": relative,
            "params": {"length": 12, "width": 10, "totalHeight": 8,
                       "wallRatio": .6, "ridgeRatio": .3, "rz": 120},
            "pointCloudInfo": {"bboxSize": [12, 10, 8], "scale": 7}})
    (root / "metadata.json").write_text(json.dumps(records), encoding="utf-8")
    for cache in (False, True):
        options = dict(train=True, jitter_sigma=.003, use_normals=True, cache_points=cache)
        old = PointCloudClassificationDataset(root, "train", ["GabledRoof"], 16,
            random_rotate=True, normal_workers=0, **options)
        np.random.seed(987)
        rng_before = np.random.get_state()
        new = PointCloudClassificationDataset(root, "train", ["GabledRoof"], 16,
            random_rotate=True, normal_workers=2, **options)
        rng_after = np.random.get_state()
        assert rng_before[0] == rng_after[0]
        np.testing.assert_array_equal(rng_before[1], rng_after[1])
        assert rng_before[2:] == rng_after[2:]
        compare(old, new)
        regression_options = dict(canonical_align=True, aux_names=["bbox_x", "bbox_y", "scale"],
                                  geom_aux_targets=["geom_total_height"], **options)
        args = (root, root / "metadata.json", "GabledRoof", "train",
                ["length", "width", "totalHeight", "wallRatio", "ridgeRatio"], 16)
        compare(ParamRegressionDataset(*args, normal_workers=0, **regression_options),
                ParamRegressionDataset(*args, normal_workers=2, **regression_options))

    loaded = []
    def clouds():
        for index in range(20):
            loaded.append(index)
            yield str(index), np.random.default_rng(index).normal(size=(32, 3))
    prepared = precompute_normals(clouds(), 20, "bounded", 2)
    assert next(prepared)[0] == "0"
    assert len(loaded) <= 5
    assert [item[0] for item in prepared] == [str(i) for i in range(1, 20)]
    stream = io.StringIO()
    with contextlib.redirect_stdout(stream):
        try:
            list(precompute_normals([("bad", np.zeros((4, 2))),
                                     ("valid", np.zeros((4, 3)))], 2, "failure", 2))
        except ValueError:
            pass
        else:
            raise AssertionError("worker error was hidden")
    assert "complete:" not in stream.getvalue()
    print("NORMAL_SPAWN_PASS")
''', encoding="utf-8")
            result = subprocess.run([sys.executable, "-B", str(harness)], cwd=root,
                                    capture_output=True, text=True, timeout=180)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("NORMAL_SPAWN_PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
