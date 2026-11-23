"""CPU checks for the opt-in physical tower-profile training experiment."""
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

from test_canonical_pipeline import regression

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("profile_runner", ROOT / "scripts/train_ptv3_reg.py")
runner = importlib.util.module_from_spec(spec)
sys.path.insert(0, str(ROOT / "scripts"))
spec.loader.exec_module(runner)
sys.path.pop(0)
TARGETS = list(regression.TOWER_PROFILE_TARGETS)
VALUES = torch.tensor([[8, 2, 3, 1, .3, 1.5, 8.8, .4],
                       [12, 4, 5, 2, .8, 2.5, 13, .7],
                       [4, 1, 2, .5, .1, 1, 4.5, .2]], dtype=torch.float64)


def settings(*extra):
    with patch.object(sys, "argv", ["main_reg.py", "--mode", "train", "--class_name", "CurvedRoofRoundHouse",
                                   "--targets", *TARGETS, *extra]):
        return regression.parse_args()


def statistics():
    dataset = SimpleNamespace(samples=[(None, row.numpy()) for row in VALUES])
    return regression.compute_tower_profile_std(dataset, TARGETS)


def metrics(true, pred):
    error = np.abs(pred - true)
    return dict(paths=[f"sample_{i}.txt" for i in range(len(true))], true=true, pred=pred,
                mae=error.mean(axis=0), rmse=np.sqrt((error ** 2).mean(axis=0)),
                mape=(error / np.maximum(np.abs(true), 1e-6)).mean(axis=0) * 100,
                r2=np.zeros(true.shape[1]))


class TowerProfileTests(unittest.TestCase):
    def test_endpoints_midpoint_and_independent_cubic_reference(self):
        parts = regression.tower_profile_points(VALUES, TARGETS)
        for i, (base_r, base_h, roof_h, cap_r, rise, cone_h, eave_r, eave_h) in enumerate(VALUES.numpy()):
            np.testing.assert_allclose(parts["eave"][i], [[base_r, base_h-eave_h], [eave_r, base_h-eave_h],
                                                         [eave_r, base_h], [base_r, base_h]])
            controls = np.array([[eave_r, base_h],
                                 [eave_r-(eave_r-cap_r)/3, base_h+roof_h/3+4*rise/3],
                                 [eave_r-2*(eave_r-cap_r)/3, base_h+2*roof_h/3+4*rise/3],
                                 [cap_r, base_h+roof_h]])
            t = np.linspace(0, 1, 17)[:, None]
            expected = (1-t)**3 * controls[0] + 3*(1-t)**2*t * controls[1] + 3*(1-t)*t**2 * controls[2] + t**3 * controls[3]
            np.testing.assert_allclose(parts["roof"][i], expected)
            self.assertAlmostEqual(parts["roof"][i, 8, 1].item(), base_h+roof_h/2+rise)
            np.testing.assert_allclose(parts["cone"][i], [[cap_r, base_h+roof_h], [0, base_h+roof_h+cone_h]])

    def test_target_order_does_not_change_the_profile(self):
        original = regression.tower_profile_points(VALUES, TARGETS)
        reversed_parts = regression.tower_profile_points(VALUES.flip(1), TARGETS[::-1])
        for name in original:
            torch.testing.assert_close(original[name], reversed_parts[name])

    def test_clear_height_uses_existing_labels_and_preserves_physical_profile(self):
        targets = list(regression.TOWER_CLEAR_HEIGHT_TARGETS)
        params = dict(zip(TARGETS, VALUES[0].tolist()))
        row = regression.target_vector({"params": params}, targets)
        self.assertAlmostEqual(float(row[targets.index("cylinderHeight")]), 1.6, places=6)
        prediction = regression.prediction_params_for_plugin("CurvedRoofRoundHouse", targets, row)
        self.assertAlmostEqual(prediction["baseHeight"], 2.0, places=6)
        self.assertAlmostEqual(prediction["eaveRadius"], params["eaveRadius"], places=6)
        new_parts = regression.tower_profile_points(torch.as_tensor(row[None], dtype=torch.float64), targets)
        old_parts = regression.tower_profile_points(VALUES[:1], TARGETS)
        for name in old_parts:
            torch.testing.assert_close(new_parts[name], old_parts[name])

    def test_clear_height_flag_replaces_only_the_tower_height_target(self):
        recipe = next(r for r in runner.RECIPES if r[0] == "CurvedRoofRoundHouse")
        args = SimpleNamespace(log_root="logs", run_tag="v14_clear_height", data_root="data", batch_size=4,
                               seed=42, ptv3_repo=None, canonical_align=True, epochs=100, grid_size=.02,
                               patch_size=128, flash=False, tower_clear_height=True)
        _, train, test = runner.commands(args, recipe)
        self.assertEqual(train[train.index("--targets") + 1:train.index("--target_weights")],
                         list(regression.TOWER_CLEAR_HEIGHT_TARGETS))
        self.assertIn("cylinderHeight=1.3", train)
        self.assertNotIn("--targets", test)

    def test_identity_and_equal_segment_weighting(self):
        scale = statistics()
        loss, _ = regression.tower_profile_loss(VALUES, VALUES, TARGETS, scale)
        self.assertEqual(loss.item(), 0)
        pred = VALUES.clone()
        pred[:, 4] += .2
        loss, parts = regression.tower_profile_loss(pred, VALUES, TARGETS, scale)
        self.assertEqual(parts["eave"].item(), 0)
        self.assertEqual(parts["cone"].item(), 0)
        self.assertAlmostEqual(loss.item(), parts["roof"].item()/3)

    def test_all_parameters_receive_finite_gradients_without_clipping(self):
        scale = statistics()
        for index, name in enumerate(TARGETS):
            with self.subTest(parameter=name):
                pred = VALUES.clone()
                pred[:, index] += .1
                pred.requires_grad_()
                loss, _ = regression.tower_profile_loss(pred, VALUES, TARGETS, scale)
                loss.backward()
                self.assertTrue(torch.isfinite(pred.grad).all())
                self.assertTrue((pred.grad[:, index].abs() > 0).all())
        pred = VALUES.clone()
        pred[:, 4] = -.1
        pred.requires_grad_()
        regression.tower_profile_loss(pred, VALUES, TARGETS, scale)[0].backward()
        self.assertTrue((pred.grad[:, 4] < 0).all())

    def test_autograd_matches_finite_differences(self):
        pred = (VALUES + .02).requires_grad_()
        self.assertTrue(torch.autograd.gradcheck(
            lambda x: regression.tower_profile_loss(x, VALUES, TARGETS, statistics())[0], (pred,)))

    def test_statistics_use_only_training_labels_and_leave_rng_unchanged(self):
        rng = torch.get_rng_state().clone()
        parts = regression.tower_profile_points(VALUES, TARGETS)
        scale = statistics()
        for name in parts:
            np.testing.assert_allclose(scale[name], np.maximum(np.std(parts[name].numpy(), axis=0), 1e-6))
        torch.testing.assert_close(torch.get_rng_state(), rng)
        singleton = SimpleNamespace(samples=[(None, VALUES[0].numpy())])
        for value in regression.compute_tower_profile_std(singleton, TARGETS).values():
            np.testing.assert_array_equal(value, np.full_like(value, 1e-6))

    def test_physical_reports_are_corresponding_distances_with_equal_segment_mean(self):
        pred = VALUES.numpy().copy()
        pred[:, 5] += .6
        result = metrics(VALUES.numpy(), pred)
        distances = regression.tower_profile_distances(result, TARGETS)
        np.testing.assert_allclose(distances["cone"], [[0, .6]]*3, atol=1e-12)
        np.testing.assert_allclose(distances["equal_segment_mean"], .1)
        with tempfile.TemporaryDirectory(dir=ROOT / "tests") as temp:
            regression.save_tower_profile_report(Path(temp), "test", TARGETS, result)
            with (Path(temp) / "regression_profile_report_test.csv").open(encoding="utf-8-sig") as f:
                rows = list(csv.DictReader(f))
            self.assertEqual([row["segment"] for row in rows], ["eave", "roof", "cone", "equal_segment_mean"])
            self.assertAlmostEqual(float(rows[-1]["mean_distance"]), .1)
            with (Path(temp) / "regression_profile_errors_test.csv").open(encoding="utf-8-sig") as f:
                self.assertEqual(len(list(csv.DictReader(f))), 3)

    def test_cli_defaults_and_invalid_combinations(self):
        self.assertEqual(settings().tower_profile_loss_weight, 0)
        self.assertEqual(settings("--tower_profile_loss_weight", ".2").tower_profile_loss_weight, .2)
        for value in ("-1", "nan", "inf"):
            with self.subTest(value=value), self.assertRaises(SystemExit):
                settings("--tower_profile_loss_weight", value)
        for extra in (("--class_name", "FlatRoofHouse"), ("--targets", "baseRadius"),
                      ("--target_mode", "bbox_residual"), ("--mode", "test")):
            with self.subTest(extra=extra), self.assertRaises(SystemExit):
                settings("--tower_profile_loss_weight", ".2", *extra)

    def test_runner_flag_changes_only_the_tower_train_command(self):
        args = SimpleNamespace(log_root="logs", run_tag="trial", data_root="data", batch_size=4,
                               seed=42, ptv3_repo=None, canonical_align=True, epochs=100,
                               grid_size=.02, patch_size=128, flash=False)
        recipe = next(r for r in runner.RECIPES if r[0] == "CurvedRoofRoundHouse")
        _, baseline, test = runner.commands(args, recipe)
        args.tower_profile_loss_weight = .2
        _, experiment, test_after = runner.commands(args, recipe)
        self.assertEqual(experiment, baseline + ["--tower_profile_loss_weight", "0.2"])
        self.assertEqual(test, test_after)
        self.assertNotIn("--tower_profile_loss_weight", runner.commands(args, runner.RECIPES[0])[1])
        for value in ([], ["--classes", "FlatRoofHouse"], ["--tower_profile_loss_weight", "nan"]):
            argv = ["runner", "--dry_run", "--tower_profile_loss_weight", ".2", *value]
            with patch.object(sys, "argv", argv), patch("sys.stderr"), self.assertRaises(SystemExit):
                runner.main()

    def run_tiny_training(self, directory, weight):
        raw = VALUES.numpy().astype(np.float32)

        class Dataset:
            geom_aux_targets = []

            def __init__(self, *args, **kwargs):
                self.samples = [(Path(f"sample_{i}.txt"), row, np.zeros(0, np.float32), {}, 0)
                                for i, row in enumerate(raw)]

            def __len__(self):
                return len(self.samples)

            def __getitem__(self, index):
                path, y, aux, _, _ = self.samples[index]
                return torch.zeros(4, 3), torch.from_numpy(aux), torch.from_numpy(y), torch.zeros(0), str(path)

        class Model(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.output = torch.nn.Parameter(torch.zeros(1, 8))

            def forward(self, points, aux):
                return self.output.expand(points.shape[0], -1)

        # Second epoch has a better parameter score but a worse physical profile.
        first, second = raw.copy(), raw.copy()
        first[:, 4] += .2
        second[:, 6] += .2
        args = settings("--cpu", "--no_aux", "--epochs", "2", "--batch_size", "3", "--seed", "42",
                        "--tower_profile_loss_weight", str(weight))
        args.log_dir, args.backbone = str(directory), "test_stub"
        with patch.object(regression, "ParamRegressionDataset", Dataset), \
             patch.object(regression, "build_model", return_value=Model()), \
             patch.object(regression, "evaluate", side_effect=[metrics(raw, first), metrics(raw, second)]), \
             patch.object(regression, "tower_profile_loss", wraps=regression.tower_profile_loss) as loss, \
             patch("builtins.print"):
            regression.train(args)
        self.assertEqual(loss.call_count, 2 if weight else 0)
        config = json.loads((directory / "regression_config.json").read_text(encoding="utf-8"))
        checkpoint = torch.load(directory / "best_model.pth", map_location="cpu", weights_only=False)
        with (directory / "train_history.csv").open(encoding="utf-8-sig") as f:
            history = list(csv.DictReader(f))
        return config, checkpoint, history

    def test_training_saves_metadata_but_keeps_parameter_based_selection(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tests") as temp:
            config, checkpoint, history = self.run_tiny_training(Path(temp), .2)
            self.assertEqual(checkpoint["epoch"], 2)
            for payload in (config, checkpoint):
                self.assertEqual(payload["targets"], TARGETS)
                self.assertEqual(payload["tower_profile_loss_weight"], .2)
                self.assertEqual(payload["tower_profile_version"], regression.TOWER_PROFILE_VERSION)
                self.assertEqual(payload["selection_metric"], "weighted_standardized_mae")
                for name in statistics():
                    np.testing.assert_allclose(payload["tower_profile_std"][name], statistics()[name], rtol=1e-6)
            self.assertLess(float(history[1]["val_selection_score"]), float(history[0]["val_selection_score"]))
            self.assertGreater(float(history[1]["val_profile_mean_distance"]), float(history[0]["val_profile_mean_distance"]))
            self.assertTrue((Path(temp) / "regression_profile_report_val.csv").is_file())

    def test_zero_weight_bypasses_profile_loss_and_keeps_legacy_history(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tests") as temp:
            config, checkpoint, history = self.run_tiny_training(Path(temp), 0)
            self.assertEqual(len(history[0]), 6)
            self.assertNotIn("tower_profile_std", checkpoint)
            self.assertNotIn("tower_profile_version", config)
            self.assertEqual(checkpoint["loss_recipe"], "standardized_wrapped_smooth_l1_v2")
            self.assertFalse((Path(temp) / "regression_profile_report_val.csv").exists())

    def test_test_mode_adds_profile_reports_to_old_eight_target_models_only(self):
        class Dataset:
            orientation_diagnostics = []

            def __len__(self):
                return 3

        for count in (8, 6):
            with self.subTest(targets=count), tempfile.TemporaryDirectory(dir=ROOT / "tests") as temp:
                args = settings("--mode", "test", "--cpu", "--log_dir", temp)
                targets = TARGETS[:count]
                # A pre-experiment checkpoint has no tower_profile_* fields.
                checkpoint = {"class_name": "CurvedRoofRoundHouse", "target_mode": "raw"}
                loaded = (object(), checkpoint, targets, None, None, None, None)
                result = metrics(VALUES[:, :count].numpy(), VALUES[:, :count].numpy())
                with patch.object(regression, "load_checkpoint", return_value=loaded), \
                     patch.object(regression, "ParamRegressionDataset", return_value=Dataset()), \
                     patch.object(regression, "make_data_loader", return_value=[]), \
                     patch.object(regression, "evaluate", return_value=result), patch("builtins.print"):
                    regression.test(args)
                self.assertEqual((Path(temp) / "regression_profile_report_test.csv").is_file(), count == 8)


if __name__ == "__main__":
    unittest.main()
