"""CPU tests for the opt-in truncated-pyramid scale experiment."""

import csv
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np
import torch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("truncated_main_reg", ROOT / "scripts/main_reg.py")
regression = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {"model_ptv3": SimpleNamespace(PTv3Regressor=object)}):
    spec.loader.exec_module(regression)
spec = importlib.util.spec_from_file_location("truncated_runner", ROOT / "scripts/train_ptv3_reg.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
sys.path.pop(0)

RECIPE = next(recipe for recipe in runner.RECIPES if recipe[0] == "FlatTopHipRoofHouse")
TARGETS = RECIPE[2].split()
WEIGHTS = torch.tensor(list(map(float, RECIPE[3].split())))


def args(*extra):
    argv = ["main_reg.py", "--mode", "train", "--class_name", "FlatTopHipRoofHouse",
            "--targets", *TARGETS, "--target_mode", "bbox_residual", "--canonical_align",
            "--target_weights", *[f"{name}={weight}" for name, weight in zip(TARGETS, RECIPE[3].split())],
            "--relative_loss_eps", "0.05", *extra]
    with patch.object(sys, "argv", argv):
        return regression.parse_args()


class TruncatedScaleTrainingTests(unittest.TestCase):
    def test_relative_dimensions_use_actual_size_and_exclude_ratios(self):
        true = torch.tensor([[10., 8., .5, .6, 5., .7], [40., 32., .5, .6, 20., .7]])
        pred = true.clone()
        pred[:, 0] *= 1.1
        pred[:, 2:4] += 100
        pred[:, 5] += 100
        pred.requires_grad_()
        actual = regression.truncated_dimension_relative_loss(pred, true, TARGETS, WEIGHTS, .05)
        expected = .1 * WEIGHTS[0] / WEIGHTS[[0, 1, 4]].sum()
        torch.testing.assert_close(actual, expected)
        actual.backward()
        self.assertGreater(pred.grad[:, 0].abs().sum().item(), 0)
        self.assertEqual(pred.grad[:, [2, 3, 5]].abs().sum().item(), 0)

    def test_relative_loss_is_unit_and_target_order_invariant(self):
        true = torch.tensor([[10., 8., .5, .6, 5., .7]])
        pred = true + torch.tensor([[1., .4, 0, 0, .5, 0]])
        expected = regression.truncated_dimension_relative_loss(pred, true, TARGETS, WEIGHTS, .05)
        scaled_true, scaled_pred = true.clone(), pred.clone()
        scaled_true[:, [0, 1, 4]] *= 100
        scaled_pred[:, [0, 1, 4]] *= 100
        actual = regression.truncated_dimension_relative_loss(scaled_pred, scaled_true, TARGETS, WEIGHTS, 5)
        torch.testing.assert_close(actual, expected)
        order = [5, 4, 3, 2, 1, 0]
        actual = regression.truncated_dimension_relative_loss(
            pred[:, order], true[:, order], [TARGETS[i] for i in order], WEIGHTS[order], .05)
        torch.testing.assert_close(actual, expected)

    def test_selection_averages_three_bands_not_cloud_counts(self):
        true = np.tile(np.array([10., 8., .5, .6, 5., .7]), (3, 1))
        pred = true.copy()
        pred[:, 0] += np.array([1, 2, 3])
        pred[:, 2] += .1
        metrics = {"pred": pred, "true": true}
        score, bands = regression.truncated_scale_balanced_score(metrics, TARGETS, WEIGHTS.numpy(), [0, 1, 2], .05)
        expected = (.2 * float(WEIGHTS[0]) + .1 * float(WEIGHTS[2])) / float(WEIGHTS.sum())
        self.assertAlmostEqual(score, expected, places=7)
        repeated = [0] * 30 + [1, 2]
        other, _ = regression.truncated_scale_balanced_score(
            {"pred": pred[repeated], "true": true[repeated]}, TARGETS, WEIGHTS.numpy(), [0] * 30 + [1, 2], .05)
        self.assertAlmostEqual(score, other)
        self.assertLess(bands[0], bands[1])
        self.assertLess(bands[1], bands[2])

    def test_missing_or_invalid_band_is_not_silently_ignored(self):
        for bands in ([0, 1], [0, 1, 3], [False, 1, 2], [0, None, 2]):
            dataset = SimpleNamespace(samples=[(None, None, None, {"horizontalScaleBand": band}, 0) for band in bands])
            with self.subTest(bands=bands), self.assertRaises(ValueError):
                regression.truncated_scale_bands(dataset)
        true = np.ones((3, 6))
        with self.assertRaises(ValueError):
            regression.truncated_scale_balanced_score({"true": true, "pred": true}, TARGETS, WEIGHTS.numpy(), [0, 1, 1], .05)

    def test_flags_are_opt_in_and_restricted_to_this_recipe(self):
        self.assertFalse(args().truncated_scale_balanced)
        self.assertTrue(args("--truncated_scale_balanced").truncated_scale_balanced)
        invalid = [("--class_name", "FlatRoofHouse"), ("--target_mode", "raw"), ("--targets", "length"),
                   ("--relative_loss_weight", ".05"), ("--indented_symmetry_180",), ("--mode", "test"),
                   ("--relative_loss_eps", "0"), ("--relative_loss_eps", "nan"),
                   ("--target_weights", "default=0", "wallRatio=1")]
        for extra in invalid:
            with self.subTest(extra=extra), self.assertRaises(SystemExit):
                args("--truncated_scale_balanced", *extra)

    def test_runner_passes_flag_to_train_only_and_leaves_baseline_unchanged(self):
        settings = SimpleNamespace(log_root="logs", run_tag="v7_scale_trial", data_root="data", batch_size=4,
                                   seed=42, ptv3_repo=None, canonical_align=True, epochs=100, grid_size=.02,
                                   patch_size=128, flash=False)
        _, baseline, test = runner.commands(settings, RECIPE)
        settings.truncated_scale_balanced = True
        _, experiment, test_after = runner.commands(settings, RECIPE)
        self.assertEqual(experiment, baseline + ["--truncated_scale_balanced"])
        self.assertEqual(test, test_after)
        self.assertEqual(experiment[experiment.index("--relative_loss_weight") + 1], "0.0")
        cuboid = next(recipe for recipe in runner.RECIPES if recipe[0] == "FlatRoofHouse")
        self.assertNotIn("--truncated_scale_balanced", runner.commands(settings, cuboid)[1])
        for extra in (["--classes", "FlatRoofHouse"], ["--no-canonical_align"]):
            argv = ["train_ptv3_reg.py", "--truncated_scale_balanced", "--dry_run", *extra]
            with patch.object(sys, "argv", argv), patch("sys.stderr"), self.assertRaises(SystemExit):
                runner.main()

    def test_preflight_uses_train_and_validation_bands_not_test_labels(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tests") as temp:
            root = Path(temp)
            (root / "metadata").mkdir()
            records = [{"type": "FlatTopHipRoofHouse", "split": split, "horizontalScaleBand": band}
                       for split in ("train", "val") for band in (0, 1, 2)]
            records.append({"type": "FlatTopHipRoofHouse", "split": "test"})
            metadata = root / "metadata/sample_params.json"
            metadata.write_text(json.dumps(records), encoding="utf-8")
            runner.validate_truncated_scale_bands(root)
            records[3].pop("horizontalScaleBand")
            metadata.write_text(json.dumps(records), encoding="utf-8")
            with self.assertRaises(ValueError):
                runner.validate_truncated_scale_bands(root)

    def test_training_restores_dimensions_and_selects_scale_balanced_checkpoint(self):
        raw = np.array([[length, length * .6, .5 + i * .01, .6, 5 + i * .1, .7]
                        for i, length in enumerate((8, 10, 18, 22, 32, 38))], dtype=np.float32)
        aux = raw[:, [0, 1, 4]] * .8
        baseline = np.column_stack((aux[:, 0], aux[:, 1], np.zeros((6, 2)), aux[:, 2], np.zeros(6)))
        transformed = (raw - baseline).astype(np.float32)

        class TinyDataset:
            geom_aux_targets = []

            def __init__(self):
                self.samples = [(Path(f"sample_{i}.txt"), y, a, {"horizontalScaleBand": i // 2}, 0)
                                for i, (y, a) in enumerate(zip(transformed, aux))]

            def __len__(self):
                return len(self.samples)

            def __getitem__(self, index):
                path, y, a, _, _ = self.samples[index]
                return torch.zeros(4, 3), torch.from_numpy(a), torch.from_numpy(y), torch.zeros(0), str(path)

        class TinyModel(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.output = torch.nn.Parameter(torch.zeros(1, 6))

            def forward(self, points, aux):
                return self.output.expand(points.shape[0], -1)

        validation = []
        for rates in ((.10, .01, .01), (.01, .02, .04)):
            pred = raw.copy()
            pred[:, 0] *= 1 + np.repeat(rates, 2)
            error = np.abs(pred - raw)
            validation.append({"paths": [f"sample_{i}.txt" for i in range(6)], "true": raw, "pred": pred,
                               "mae": error.mean(axis=0), "rmse": np.sqrt((error ** 2).mean(axis=0)),
                               "mape": (error / raw).mean(axis=0) * 100, "r2": np.zeros(6)})
        with tempfile.TemporaryDirectory(dir=ROOT / "tests") as temp:
            settings = args("--truncated_scale_balanced", "--epochs", "2", "--batch_size", "6", "--cpu",
                            "--aux_features", "bbox_x", "bbox_y", "bbox_z")
            settings.log_dir, settings.backbone = temp, "test_stub"
            with patch.object(regression, "build_model", return_value=TinyModel()), \
                 patch.object(regression, "ParamRegressionDataset", return_value=TinyDataset()), \
                 patch.object(regression, "truncated_dimension_relative_loss", wraps=regression.truncated_dimension_relative_loss) as loss, \
                 patch.object(regression, "evaluate", side_effect=validation), patch("builtins.print"):
                regression.train(settings)
            self.assertEqual(loss.call_count, 2)
            self.assertEqual(sorted(loss.call_args.args[1][:, 0].tolist()), sorted(raw[:, 0].tolist()))
            config = json.loads((Path(temp) / "regression_config.json").read_text(encoding="utf-8"))
            checkpoint = torch.load(Path(temp) / "best_model.pth", map_location="cpu", weights_only=False)
            self.assertEqual(checkpoint["epoch"], 2)
            for payload in (config, checkpoint):
                for key, value in regression.TRUNCATED_SCALE_SETTINGS.items():
                    self.assertEqual(payload[key], value)
                self.assertEqual(payload["targets"], TARGETS)
                self.assertEqual(payload["target_mode"], "bbox_residual")
                self.assertEqual(payload["dimension_relative_eps"], .05)
            with (Path(temp) / "train_history.csv").open(encoding="utf-8-sig", newline="") as stream:
                history = list(csv.DictReader(stream))
            self.assertLess(float(history[0]["val_standardized_mae"]), float(history[1]["val_standardized_mae"]))
            self.assertGreater(float(history[0]["val_selection_score"]), float(history[1]["val_selection_score"]))
            self.assertGreater(float(history[0]["train_dimension_relative_loss"]), 0)
            self.assertIn("val_scale_medium", history[1])


if __name__ == "__main__":
    unittest.main()
