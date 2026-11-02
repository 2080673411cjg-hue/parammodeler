"""Check cached preprocessing, live augmentation, and CPU loader options."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np
import torch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("point_loading_regression", ROOT / "scripts/main_reg.py")
regression = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {"model_ptv3": SimpleNamespace(PTv3Regressor=object)}):
    spec.loader.exec_module(regression)
spec = importlib.util.spec_from_file_location("point_loading_runner", ROOT / "scripts/train_ptv3_reg.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
sys.path.pop(0)


class PointLoadingTests(unittest.TestCase):
    def dataset(self, cache, canonical, normalize, normals, source="metadata"):
        record = {"type": "GabledRoof", "split": "train", "file": "train/GabledRoof/a.txt",
                  "params": {"length": 12, "width": 10, "totalHeight": 8,
                             "wallRatio": .6, "ridgeRatio": .3, "rz": 120},
                  "pointCloudInfo": {"bboxSize": [12, 10, 8], "scale": 7}}
        return regression.ParamRegressionDataset(
            "data", "metadata.json", "GabledRoof", "train",
            ["length", "width", "totalHeight", "wallRatio", "ridgeRatio"], 8,
            train=True, normalize=normalize, jitter_sigma=.003, random_rotate=not canonical,
            aux_names=["bbox_x", "bbox_y", "bbox_z", "scale"], use_normals=normals,
            canonical_align=canonical, orientation_source=source, cache_points=cache)

    def patches(self):
        points = np.random.default_rng(123).normal(size=(12, 3)).astype(np.float32)
        record = {"type": "GabledRoof", "split": "train", "file": "train/GabledRoof/a.txt",
                  "params": {"length": 12, "width": 10, "totalHeight": 8,
                             "wallRatio": .6, "ridgeRatio": .3, "rz": 120},
                  "pointCloudInfo": {"bboxSize": [12, 10, 8], "scale": 7}}
        return points, record

    def test_cache_and_legacy_match_under_the_same_random_state(self):
        points, record = self.patches()
        orientation = {"reliable": True, "reason": "fixture", "canonical_angle_deg": -55.}
        with patch.object(regression, "load_metadata", return_value=[record]), \
             patch.object(regression, "load_txt_points", side_effect=lambda _: points.copy()), \
             patch.object(regression, "canonical_orientation", return_value=orientation), \
             patch.object(Path, "exists", return_value=True), patch("builtins.print"):
            for canonical in (False, True):
                for normalize in (False, True):
                    for normals in (False, True):
                        for source in ("metadata", "geometry"):
                            with self.subTest(canonical=canonical, normalize=normalize, normals=normals, source=source):
                                legacy = self.dataset(False, canonical, normalize, normals, source)
                                cached = self.dataset(True, canonical, normalize, normals, source)
                                np.random.seed(42)
                                before = legacy[0]
                                np.random.seed(42)
                                after = cached[0]
                                for old, new in zip(before[:-1], after[:-1]):
                                    torch.testing.assert_close(old, new, rtol=0, atol=0)
                                self.assertEqual(before[-1], after[-1])

    def test_cached_reads_happen_once_and_augmentation_does_not_mutate_cache(self):
        points, record = self.patches()
        with patch.object(regression, "load_metadata", return_value=[record]), \
             patch.object(regression, "load_txt_points", side_effect=lambda _: points.copy()) as read, \
             patch.object(Path, "exists", return_value=True), patch("builtins.print"):
            cached = self.dataset(True, True, True, True)
            self.assertEqual(read.call_count, 1)
            key = str(cached.samples[0][0])
            xyz_before = cached._points_cache[key].copy()
            normals_before = cached._normals_cache[key].copy()
            self.assertFalse(cached._points_cache[key].flags.writeable)
            self.assertFalse(cached._normals_cache[key].flags.writeable)
            first, second = cached[0][0], cached[0][0]
            self.assertFalse(torch.equal(first, second))
            first.add_(100)
            np.testing.assert_array_equal(cached._points_cache[key], xyz_before)
            np.testing.assert_array_equal(cached._normals_cache[key], normals_before)
            self.assertEqual(read.call_count, 1)
            with patch.object(regression, "load_txt_points", side_effect=AssertionError("no disk reads")):
                cached[0]

    def test_uncached_path_still_reads_each_item(self):
        points, record = self.patches()
        with patch.object(regression, "load_metadata", return_value=[record]), \
             patch.object(regression, "load_txt_points", side_effect=lambda _: points.copy()) as read, \
             patch.object(Path, "exists", return_value=True), patch("builtins.print"):
            legacy = self.dataset(False, False, True, False)
            self.assertEqual(read.call_count, 0)
            legacy[0]
            legacy[0]
            self.assertEqual(read.call_count, 2)
            self.assertFalse(legacy._points_cache)

    def test_loader_uses_cpu_workers_persistence_and_cuda_only_pinning(self):
        dataset = torch.utils.data.TensorDataset(torch.zeros(4, 3))
        loader = regression.make_data_loader(dataset, 2, False, torch.device("cpu"), 0, 42)
        self.assertFalse(loader.pin_memory)
        self.assertFalse(loader.persistent_workers)
        self.assertIsNone(loader.generator)
        loader = regression.make_data_loader(dataset, 2, False, torch.device("cuda"), 2, 42)
        self.assertTrue(loader.pin_memory)
        self.assertTrue(loader.persistent_workers)
        self.assertEqual(loader.prefetch_factor, 2)
        self.assertEqual(loader.multiprocessing_context.get_start_method(), "spawn")
        self.assertEqual(loader.generator.initial_seed(), 42)

    def test_worker_seeds_numpy_and_python_from_torch(self):
        with patch.object(torch, "initial_seed", return_value=2 ** 32 + 123), \
             patch.object(np.random, "seed") as numpy_seed, patch.object(random, "seed") as python_seed:
            regression.seed_data_worker(1)
        numpy_seed.assert_called_once_with(123)
        python_seed.assert_called_once_with(123)

    def test_cli_and_runner_forward_fallback_options_to_train_and_test(self):
        with patch.object(sys, "argv", ["main_reg.py", "--mode", "test", "--no-cache_points",
                                       "--num_workers", "2", "--normal_workers", "4"]):
            args = regression.parse_args()
        self.assertFalse(args.cache_points)
        self.assertEqual(args.num_workers, 2)
        self.assertEqual(args.normal_workers, 4)
        with patch.object(sys, "argv", ["main_reg.py", "--mode", "test", "--num_workers", "-1"]):
            with self.assertRaises(SystemExit):
                regression.parse_args()
        args = SimpleNamespace(log_root="logs", run_tag="fixture", data_root="data", batch_size=4,
                               seed=42, ptv3_repo=None, canonical_align=True, epochs=100, grid_size=.02,
                               patch_size=128, flash=False, num_workers=2, normal_workers=4, cache_points=False,
                               normal_backend="pcl", normal_threads=3)
        _, train, test = runner.commands(args, runner.RECIPES[0])
        for command in (train, test):
            self.assertEqual(command[command.index("--num_workers") + 1], "2")
            self.assertEqual(command[command.index("--normal_workers") + 1], "4")
            self.assertEqual(command[command.index("--normal_backend") + 1], "pcl")
            self.assertEqual(command[command.index("--normal_threads") + 1], "3")
            self.assertIn("--no-cache_points", command)
        self.assertEqual(regression.data_loading_config(args)["num_workers"], 2)

    def test_v9_batch_keeps_class_specific_recipes_and_geometry_tests(self):
        argv = ["train_ptv3_reg.py", "--run_tag", "v9", "--normal_backend", "pcl",
                "--lhouse_canonical", "--lhouse_robust_bbox", "--truncated_scale_balanced", "--dry_run"]
        stream = io.StringIO()
        with patch.object(sys, "argv", argv), contextlib.redirect_stdout(stream), \
             patch.object(runner, "commands", wraps=runner.commands) as generated:
            runner.main()
        self.assertEqual(len(generated.call_args_list), 13)
        for call in generated.call_args_list:
            settings, recipe = call.args
            log, train, test = runner.commands(settings, recipe)
            self.assertTrue(str(log).endswith("_v9_normals"))
            self.assertEqual("--lhouse_canonical" in train, recipe[0] == "LHouse")
            self.assertEqual("--lhouse_robust_bbox" in train, recipe[0] == "LHouse")
            self.assertEqual("--truncated_scale_balanced" in train, recipe[0] == "TruncatedPyramidRoof")
            self.assertEqual(runner.needs_geometry_test(settings, recipe), recipe[0] in ("LHouse", "TruncatedPyramidRoof"))
            self.assertEqual(train[train.index("--normal_backend") + 1], "pcl")
            self.assertNotIn("--truncated_scale_balanced", test)
        self.assertEqual(stream.getvalue().count("--test_orientation geometry"), 2)
        self.assertNotIn("TriPrismPyramid", stream.getvalue())
        for extra in (["--classes", "Cuboid"], ["--no-canonical_align"]):
            with patch.object(sys, "argv", ["train_ptv3_reg.py", "--lhouse_canonical", "--dry_run", *extra]), \
                 patch("sys.stderr"), self.assertRaises(SystemExit):
                runner.main()

    def test_spawned_workers_keep_live_augmentation_reproducible_with_cache(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tests") as temp:
            root = Path(temp)
            for name in ("main_reg.py", "canonical_orientation.py"):
                shutil.copy2(ROOT / "scripts" / name, root / name)
            (root / "model_ptv3.py").write_text("class PTv3Regressor: pass\n", encoding="utf-8")
            records = []
            for i in range(4):
                relative = f"train/Cylinder/sample_{i}.txt"
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                np.savetxt(path, np.random.default_rng(i).normal(size=(12, 3)))
                records.append({"type": "Cylinder", "split": "train", "file": relative,
                                "params": {"radius": 1, "height": 2}})
            (root / "metadata.json").write_text(json.dumps(records), encoding="utf-8")
            harness = root / "smoke.py"
            harness.write_text('''
from pathlib import Path
import torch
from main_reg import ParamRegressionDataset, make_data_loader

def run(cache):
    root = Path(__file__).parent
    ds = ParamRegressionDataset(root, root / "metadata.json", "Cylinder", "train",
        ["radius", "height"], 8, train=True, jitter_sigma=.003, random_rotate=True, cache_points=cache)
    loader = make_data_loader(ds, 2, True, torch.device("cpu"), 2, 42)
    result = []
    for _ in range(2):
        result.append([(batch[0].clone(), batch[-1]) for batch in loader])
    return result

if __name__ == "__main__":
    old, new = run(False), run(True)
    for a_epoch, b_epoch in zip(old, new):
        for (a, paths_a), (b, paths_b) in zip(a_epoch, b_epoch):
            assert paths_a == paths_b
            torch.testing.assert_close(a, b, rtol=0, atol=0)
    assert not torch.equal(new[0][0][0], new[1][0][0])
    print("SPAWN_CACHE_PASS")
''', encoding="utf-8")
            result = subprocess.run([sys.executable, "-B", str(harness)], cwd=root,
                                    check=True, capture_output=True, text=True, timeout=90)
            self.assertIn("SPAWN_CACHE_PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
