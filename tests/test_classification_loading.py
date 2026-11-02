"""Verify classifier caching, live augmentation, and real worker loading."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
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
spec = importlib.util.spec_from_file_location("classification_loading", ROOT / "scripts/main.py")
classification = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {"model_ptv3": SimpleNamespace(PTv3Regressor=object, PTv3Classifier=object)}):
    spec.loader.exec_module(classification)
sys.path.pop(0)


class TinyClassifier(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.head = torch.nn.Linear(3, 2)

    def forward(self, points):
        return self.head(points.mean(dim=1))


class ClassificationLoadingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "tests")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "metadata").mkdir()
        (self.root / "metadata/class_names.txt").write_text(
            "Cuboid\nCylinder\nTriPrismPyramid\n", encoding="utf-8")
        for split, count in (("train", 4), ("val", 2)):
            for name in ("Cuboid", "Cylinder", "TriPrismPyramid"):
                folder = self.root / split / name
                folder.mkdir(parents=True)
                for index in range(count):
                    np.savetxt(folder / f"{index}.txt",
                               np.random.default_rng(index).normal(size=(12, 3)))

    def dataset(self, cache, normalize=True, normals=True):
        return classification.PointCloudClassificationDataset(
            self.root, "train", ["Cuboid", "Cylinder"], 8, train=True,
            normalize=normalize, jitter_sigma=.003, random_rotate=True,
            use_normals=normals, cache_points=cache)

    def test_cache_on_off_give_identical_inputs_with_live_augmentation(self):
        with patch("builtins.print"):
            for normalize in (False, True):
                for normals in (False, True):
                    with self.subTest(normalize=normalize, normals=normals):
                        cached = self.dataset(True, normalize, normals)
                        uncached = self.dataset(False, normalize, normals)
                        np.random.seed(42)
                        a = uncached[0]
                        np.random.seed(42)
                        b = cached[0]
                        torch.testing.assert_close(a[0], b[0], rtol=0, atol=0)
                        torch.testing.assert_close(a[1], b[1], rtol=0, atol=0)
                        self.assertEqual(a[2], b[2])
                        self.assertFalse(torch.equal(cached[0][0], cached[0][0]))

    def test_cached_points_are_read_once_and_cannot_be_changed_by_samples(self):
        with patch.object(classification, "load_txt_points", wraps=classification.load_txt_points) as read, \
             patch("builtins.print"):
            dataset = self.dataset(True)
            self.assertEqual(read.call_count, len(dataset))
            key = str(dataset.samples[0][0])
            points = dataset._points_cache[key].copy()
            normals = dataset._normals_cache[key].copy()
            self.assertFalse(dataset._points_cache[key].flags.writeable)
            self.assertFalse(dataset._normals_cache[key].flags.writeable)
            dataset[0][0].add_(100)
            dataset[0]
            self.assertEqual(read.call_count, len(dataset))
            np.testing.assert_array_equal(dataset._points_cache[key], points)
            np.testing.assert_array_equal(dataset._normals_cache[key], normals)

    def test_uncached_path_keeps_loading_each_item(self):
        with patch.object(classification, "load_txt_points", wraps=classification.load_txt_points) as read:
            dataset = self.dataset(False, normals=False)
            self.assertEqual(read.call_count, 0)
            dataset[0]
            dataset[0]
            self.assertEqual(read.call_count, 2)

    def test_triangular_prism_is_excluded_from_metadata_and_folder_discovery(self):
        self.assertEqual(classification.discover_class_names(self.root), ["Cuboid", "Cylinder"])
        (self.root / "metadata/class_names.txt").unlink()
        self.assertEqual(classification.discover_class_names(self.root), ["Cuboid", "Cylinder"])

    def test_loader_uses_explicit_cpu_device_and_spawn_options(self):
        with patch("builtins.print"):
            dataset = self.dataset(True, normals=False)
        for workers in (0, 2):
            loader = classification.make_loader(dataset, 2, True, workers, torch.device("cpu"), 42)
            self.assertFalse(loader.pin_memory)
            self.assertEqual(loader.persistent_workers, workers > 0)
            if workers:
                self.assertEqual(loader.prefetch_factor, 2)
                self.assertEqual(loader.multiprocessing_context.get_start_method(), "spawn")
                self.assertEqual(loader.generator.initial_seed(), 42)

    def test_cli_fallbacks_and_negative_worker_rejection(self):
        with patch.object(sys, "argv", ["main.py", "--no-cache_points", "--num_workers", "0",
                                       "--normal_workers", "4"]):
            args = classification.parse_args()
        self.assertFalse(args.cache_points)
        self.assertEqual(args.num_workers, 0)
        self.assertEqual(args.normal_workers, 4)
        with patch.object(sys, "argv", ["main.py", "--num_workers", "-1"]), \
             patch("sys.stderr"), self.assertRaises(SystemExit):
            classification.parse_args()
        with patch.object(sys, "argv", ["main.py", "--normal_workers", "-1"]), \
             patch("sys.stderr"), self.assertRaises(SystemExit):
            classification.parse_args()

    def test_amp_cli_is_opt_in_and_training_only(self):
        for flags, expected in (([], False), (["--amp"], True), (["--amp", "--no-amp"], False)):
            with patch.object(sys, "argv", ["main.py", *flags]):
                self.assertEqual(classification.parse_args().amp, expected)
        for mode in ("test", "predict"):
            with patch.object(sys, "argv", ["main.py", "--mode", mode, "--input", "cloud.txt", "--amp"]), \
                 patch("sys.stderr"), self.assertRaises(SystemExit):
                classification.parse_args()

    def test_amp_on_cpu_fails_before_preparing_data_or_creating_output(self):
        output = self.root / "invalid_amp"
        argv = ["main.py", "--data_root", str(self.root), "--log_dir", str(output), "--cpu", "--amp"]
        with patch.object(sys, "argv", argv):
            args = classification.parse_args()
        with patch.object(classification, "discover_class_names", side_effect=AssertionError("too late")), \
             self.assertRaisesRegex(RuntimeError, "requires CUDA"):
            classification.train(args)
        self.assertFalse(output.exists())

    def test_cpu_training_records_loading_settings_without_triangular_prism(self):
        output = self.root / "trained"
        argv = ["main.py", "--data_root", str(self.root), "--log_dir", str(output),
                "--epochs", "2", "--batch_size", "2", "--num_points", "8",
                "--num_workers", "0", "--seed", "42", "--cpu"]
        model = TinyClassifier()
        original = model.head.bias.detach().clone()
        stream = io.StringIO()
        with patch.object(sys, "argv", argv), patch.object(classification, "build_model", return_value=model), \
             contextlib.redirect_stdout(stream):
            classification.main()
        config = json.loads((output / "classification_config.json").read_text())
        checkpoint = classification.load_checkpoint(output, torch.device("cpu"))
        self.assertEqual(config["class_names"], ["Cuboid", "Cylinder"])
        self.assertEqual(checkpoint["class_names"], config["class_names"])
        self.assertEqual(checkpoint["data_loading"], config["data_loading"])
        self.assertTrue(config["data_loading"]["cache_points"])
        self.assertFalse(config["train"]["amp"])
        self.assertIsNone(config["train"]["amp_dtype"])
        self.assertFalse(checkpoint["amp"])
        self.assertFalse(torch.equal(original, model.head.bias))
        self.assertIn("Train samples=8 Val samples=4", stream.getvalue())
        self.assertIn("Classification training duration:", stream.getvalue())
        self.assertIn("Training precision: FP32 | validation: FP32", stream.getvalue())
        self.assertEqual(stream.getvalue().count("epoch_time="), 2)
        self.assertIn("peak_gpu_allocated=N/A", stream.getvalue())

    @unittest.skipUnless(torch.cuda.is_available(), "CUDA is required for real FP16/GradScaler verification")
    def test_cuda_amp_unscales_before_clip_and_keeps_validation_and_weights_fp32(self):
        output = self.root / "cuda_amp"
        argv = ["main.py", "--data_root", str(self.root), "--log_dir", str(output), "--amp",
                "--epochs", "1", "--batch_size", "2", "--num_points", "8", "--seed", "42",
                "--num_workers", "0"]
        with patch.object(sys, "argv", argv):
            args = classification.parse_args()
        model = TinyClassifier()
        before = model.head.weight.detach().clone()
        dtypes = []
        model.register_forward_hook(lambda module, inputs, result: dtypes.append((module.training, result.dtype)))
        scaler = torch.cuda.amp.GradScaler(init_scale=16.)
        events = []

        def observe(name, function):
            def call(*values, **keywords):
                events.append(name)
                return function(*values, **keywords)
            return call

        stream = io.StringIO()
        with patch.object(classification, "build_model", return_value=model), \
             patch.object(torch.cuda.amp, "GradScaler", return_value=scaler), \
             patch.object(scaler, "unscale_", side_effect=observe("unscale", scaler.unscale_)), \
             patch.object(torch.nn.utils, "clip_grad_norm_", side_effect=observe("clip", torch.nn.utils.clip_grad_norm_)), \
             patch.object(scaler, "step", side_effect=observe("step", scaler.step)), \
             patch.object(scaler, "update", side_effect=observe("update", scaler.update)), \
             contextlib.redirect_stdout(stream):
            classification.train(args)
        self.assertEqual(events, ["unscale", "clip", "step", "update"] * 4)
        self.assertTrue(all(dtype == torch.float16 for train, dtype in dtypes if train))
        self.assertTrue(all(dtype == torch.float32 for train, dtype in dtypes if not train))
        self.assertFalse(torch.equal(before, model.head.weight.detach().cpu()))
        checkpoint = classification.load_checkpoint(output, torch.device("cpu"))
        config = json.loads((output / "classification_config.json").read_text())
        self.assertTrue(checkpoint["amp"])
        self.assertTrue(config["train"]["amp"])
        self.assertTrue(all(value.dtype == torch.float32 for value in checkpoint["state_dict"].values()))
        self.assertIn("amp_scale=", stream.getvalue())
        self.assertNotIn("peak_gpu_allocated=N/A", stream.getvalue())
        with torch.no_grad():
            model.head.weight.fill_(float("nan"))
        args.log_dir = str(self.root / "nonfinite_amp")
        with patch.object(classification, "build_model", return_value=model), \
             contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(RuntimeError, "non-finite"):
            classification.train(args)
        self.assertFalse((Path(args.log_dir) / "best_model.pth").exists())

    def test_real_spawn_two_epochs_matches_uncached_loading(self):
        for name in ("main.py", "main_reg.py", "canonical_orientation.py"):
            shutil.copy2(ROOT / "scripts" / name, self.root / name)
        (self.root / "model_ptv3.py").write_text(
            "class PTv3Regressor: pass\nclass PTv3Classifier: pass\n", encoding="utf-8")
        harness = self.root / "smoke.py"
        harness.write_text('''
from pathlib import Path
import torch
from main import PointCloudClassificationDataset, make_loader

def run(cache):
    root = Path(__file__).parent
    ds = PointCloudClassificationDataset(root, "train", ["Cuboid", "Cylinder"], 8,
        train=True, jitter_sigma=.003, random_rotate=True, cache_points=cache)
    loader = make_loader(ds, 2, True, 2, torch.device("cpu"), 42)
    return [[(points.clone(), labels.clone(), paths) for points, labels, paths in loader]
            for _ in range(2)]

if __name__ == "__main__":
    old, new = run(False), run(True)
    for a_epoch, b_epoch in zip(old, new):
        for (a, labels_a, paths_a), (b, labels_b, paths_b) in zip(a_epoch, b_epoch):
            assert paths_a == paths_b
            torch.testing.assert_close(labels_a, labels_b, rtol=0, atol=0)
            torch.testing.assert_close(a, b, rtol=0, atol=0)
    assert not torch.equal(new[0][0][0], new[1][0][0])
    print("CLASSIFICATION_SPAWN_PASS")
''', encoding="utf-8")
        result = subprocess.run([sys.executable, "-B", str(harness)], cwd=self.root,
                                capture_output=True, text=True, timeout=90, check=True)
        self.assertIn("CLASSIFICATION_SPAWN_PASS", result.stdout)

    def test_predict_uses_checkpoint_class_order(self):
        output = self.root / "predict_model"
        output.mkdir()
        model = TinyClassifier()
        with torch.no_grad():
            model.head.weight.zero_()
            model.head.bias.copy_(torch.tensor([10., -10.]))
        torch.save({"state_dict": model.state_dict(), "class_names": ["LHouse", "Cuboid"],
                    "num_points": 8, "normalize_points": True, "use_normals": False},
                   output / "best_model.pth")
        argv = ["main.py", "--mode", "predict", "--input", str(self.root / "train/Cuboid/0.txt"),
                "--data_root", str(self.root), "--log_dir", str(output), "--topk", "2", "--cpu"]
        stream = io.StringIO()
        with patch.object(sys, "argv", argv), patch.object(classification, "build_model", return_value=TinyClassifier()), \
             patch.object(classification, "discover_class_names", side_effect=AssertionError("checkpoint order required")), \
             contextlib.redirect_stdout(stream):
            classification.main()
        result = json.loads(stream.getvalue())
        self.assertEqual([item["class"] for item in result], ["LHouse", "Cuboid"])
        self.assertGreater(result[0]["prob"], .99)

    def test_amp_checkpoint_remains_loadable_for_fp32_prediction(self):
        output = self.root / "amp_checkpoint"
        output.mkdir()
        model = TinyClassifier()
        torch.save({"state_dict": model.state_dict(), "class_names": ["Cuboid", "Cylinder"],
                    "num_points": 8, "normalize_points": True, "use_normals": False, "amp": True},
                   output / "best_model.pth")
        dtypes = []
        model.register_forward_hook(lambda module, inputs, result: dtypes.append(result.dtype))
        argv = ["main.py", "--mode", "predict", "--input", str(self.root / "train/Cuboid/0.txt"),
                "--data_root", str(self.root), "--log_dir", str(output), "--cpu"]
        stream = io.StringIO()
        with patch.object(sys, "argv", argv), patch.object(classification, "build_model", return_value=model), \
             contextlib.redirect_stdout(stream):
            classification.main()
        self.assertEqual(dtypes, [torch.float32])
        self.assertEqual(len(json.loads(stream.getvalue())), 2)


if __name__ == "__main__":
    unittest.main()
