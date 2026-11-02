"""Exercise test-time geometry frames without CUDA or model weights."""

import importlib.util
import csv
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np
import torch


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("canonical_main_reg_test", SCRIPTS / "main_reg.py")
regression = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {"model_ptv3": SimpleNamespace(PTv3Regressor=object)}):
    spec.loader.exec_module(regression)
sys.path.pop(0)


class CanonicalPipelineTests(unittest.TestCase):
    def dataset(self, source, reliable=True):
        record = {"type": "GabledRoof", "split": "test", "file": "test/GabledRoof/a.txt",
                  "params": {"length": 12, "width": 10, "totalHeight": 8,
                             "wallRatio": 0.6, "ridgeRatio": 0.3, "rz": 120},
                  "pointCloudInfo": {"bboxSize": [12, 10, 8], "scale": 7}}
        points = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 1], [1, 1, 1]], dtype=np.float32)
        result = {"reliable": reliable, "reason": "test geometry", "canonical_angle_deg": -55.0}
        with patch.object(regression, "load_metadata", return_value=[record]), \
             patch.object(regression, "load_txt_points", return_value=points), \
             patch.object(regression, "canonical_orientation", return_value=result), \
             patch.object(Path, "exists", return_value=True):
            dataset = regression.ParamRegressionDataset(
                "data", "metadata.json", "GabledRoof", "test",
                ["length", "width", "totalHeight", "wallRatio", "ridgeRatio"], 4,
                aux_names=["bbox_x", "bbox_y", "bbox_z", "scale"],
                canonical_align=True, orientation_source=source)
        return dataset

    def test_geometry_source_uses_estimated_angle_and_transforms_labels(self):
        dataset = self.dataset("geometry")
        self.assertEqual(dataset.samples[0][-1], -55)
        self.assertAlmostEqual(float(dataset.samples[0][1][-1]), 0.7, places=6)
        self.assertAlmostEqual(dataset.orientation_diagnostics[0]["axis_error_deg"], 5)

    def test_metadata_source_remains_unchanged(self):
        dataset = self.dataset("metadata")
        self.assertEqual(dataset.samples[0][-1], -60)
        self.assertEqual(dataset.orientation_diagnostics, [])

    def test_failed_geometry_is_counted_without_using_ground_truth_yaw(self):
        dataset = self.dataset("geometry", reliable=False)
        self.assertEqual(len(dataset), 0)
        self.assertEqual(len(dataset.orientation_diagnostics), 1)
        self.assertFalse(dataset.orientation_diagnostics[0]["applied"])

    def test_ridge_geometry_detects_mixed_frame_even_with_perfect_dimensions(self):
        dataset = self.dataset("metadata")
        target = dataset.samples[0][1]
        good = regression.centered_geometry_errors(dataset, dataset.targets, {"pred": np.array([target])})
        self.assertLess(good["ridge_endpoint_error_mean"], 1e-6)
        dataset.samples[0] = (*dataset.samples[0][:-1], 120)
        bad = regression.centered_geometry_errors(dataset, dataset.targets, {"pred": np.array([target])})
        self.assertAlmostEqual(bad["ridge_endpoint_error_mean"], 4, places=5)

    def test_geometry_reports_do_not_overwrite_metadata_reports(self):
        dataset = self.dataset("geometry")
        checkpoint = {"class_name": "GabledRoof", "canonical_align": True}
        args = SimpleNamespace(log_dir="existing-weights", cpu=True, class_name=None,
                               metadata="metadata.json", data_root="data", num_points=4,
                               test_orientation="geometry", split="test", batch_size=1,
                               circular_targets=[])
        metrics = {"pred": np.array([dataset.samples[0][1]]), "mae": np.zeros(5),
                   "rmse": np.zeros(5), "mape": np.zeros(5), "r2": np.ones(5)}
        checkpoint_result = (None, checkpoint, dataset.targets, None, None, None, None)
        with patch.object(regression, "load_checkpoint", return_value=checkpoint_result), \
             patch.object(regression, "ParamRegressionDataset", return_value=dataset), \
             patch.object(regression, "evaluate", return_value=metrics), \
             patch.object(regression, "save_metrics") as save_metrics, \
             patch.object(regression, "save_predictions") as save_predictions, \
             patch.object(regression, "save_regression_report") as save_report, \
             patch.object(regression, "save_regression_errors") as save_errors, \
             patch.object(Path, "write_text") as write_report:
            regression.test(args)
        for writer in (save_metrics, save_predictions, save_report, save_errors):
            self.assertEqual(writer.call_args.args[1], "test_geometry")
        self.assertEqual(write_report.call_count, 2)

    def test_separate_evaluation_preserves_checkpoint_reports(self):
        dataset = self.dataset("geometry")
        checkpoint = {"class_name": "GabledRoof", "canonical_align": True}
        truth = np.array([dataset.samples[0][1]])
        metrics = {"paths": ["test/GabledRoof/a.txt"], "true": truth, "pred": truth.copy(),
                   "mae": np.zeros(5), "rmse": np.zeros(5), "mape": np.zeros(5), "r2": np.ones(5)}
        loaded = (None, checkpoint, dataset.targets, None, None, None, None)
        with tempfile.TemporaryDirectory(dir=SCRIPTS.parent / "tests") as temp:
            root = Path(temp)
            weights, output = root / "weights", root / "evaluation"
            weights.mkdir()
            (weights / "best_model.pth").write_bytes(b"keep checkpoint")
            (weights / "regression_report_test.csv").write_bytes(b"keep old report")
            before = {path.name: path.read_bytes() for path in weights.iterdir()}
            args = SimpleNamespace(log_dir=str(weights), eval_output_dir=str(output), cpu=True,
                                   class_name=None, metadata="metadata.json", data_root="data",
                                   num_points=4, test_orientation="geometry", split="test",
                                   batch_size=1, circular_targets=[])
            with patch.object(regression, "load_checkpoint", return_value=loaded) as load, \
                 patch.object(regression, "ParamRegressionDataset", return_value=dataset), \
                 patch.object(regression, "evaluate", return_value=metrics), patch("builtins.print"):
                regression.test(args)
            self.assertEqual(load.call_args.args[0], weights)
            self.assertEqual({path.name: path.read_bytes() for path in weights.iterdir()}, before)
            for name in ("metrics", "predictions", "report", "errors"):
                self.assertTrue((output / f"regression_{name}_test_geometry.csv").is_file())
            self.assertTrue((output / "orientation_test_geometry.json").is_file())

    def test_separate_evaluation_rejects_checkpoint_or_nonempty_output(self):
        with tempfile.TemporaryDirectory(dir=SCRIPTS.parent / "tests") as temp:
            weights, output = Path(temp) / "weights", Path(temp) / "evaluation"
            weights.mkdir()
            output.mkdir()
            (output / "keep.txt").write_text("existing result", encoding="utf-8")
            for destination in (weights, output):
                args = SimpleNamespace(log_dir=str(weights), eval_output_dir=str(destination))
                with patch.object(regression, "load_checkpoint") as load, self.assertRaises(ValueError):
                    regression.test(args)
                load.assert_not_called()
            self.assertEqual((output / "keep.txt").read_text(encoding="utf-8"), "existing result")

    def test_evaluation_directory_is_test_only(self):
        with patch.object(sys, "argv", ["main_reg.py", "--mode", "predict", "--input", "x.txt",
                                       "--eval_output_dir", "evaluation"]):
            with self.assertRaisesRegex(SystemExit, "only supported in test mode"):
                regression.parse_args()

    def lhouse_dataset(self, enabled, source="metadata", estimated=120.0):
        targets = ["outerLength", "outerWidth", "cutoutLengthRatio", "cutoutWidthRatio", "height"]
        record = {"type": "LHouse", "split": "test", "file": "test/LHouse/a.txt",
                  "params": dict(zip(targets, [20, 10, .4, .6, 5])),
                  "pointCloudInfo": {"bboxSize": [20, 10, 5], "scale": 10}}
        record["params"]["rz"] = 120
        points = np.array([[0, 0, 0], [20, 0, 5], [0, 10, 5], [12, 4, 5]], dtype=np.float32)
        orientation = {"reliable": True, "reason": "test geometry", "canonical_angle_deg": estimated}
        with patch.object(regression, "load_metadata", return_value=[record]), \
             patch.object(regression, "load_txt_points", return_value=points), \
             patch.object(regression, "canonical_orientation", return_value=orientation), \
             patch.object(Path, "exists", return_value=True):
            dataset = regression.ParamRegressionDataset(
                "data", "metadata.json", "LHouse", "test", targets, 4,
                canonical_align=True, lhouse_canonical=enabled, orientation_source=source)
        return dataset

    def test_lhouse_opt_in_rotates_points_but_not_shape_labels(self):
        legacy, trial = self.lhouse_dataset(False), self.lhouse_dataset(True)
        self.assertEqual(legacy.samples[0][-1], 0)
        self.assertEqual(trial.samples[0][-1], 120)
        np.testing.assert_array_equal(legacy.samples[0][1], trial.samples[0][1])

    def test_lhouse_geometry_does_not_hide_a_180_degree_failure(self):
        dataset = self.lhouse_dataset(True, "geometry", estimated=-60)
        self.assertAlmostEqual(dataset.orientation_diagnostics[0]["axis_error_deg"], 180)
        metrics = {"pred": np.array([dataset.samples[0][1]])}
        self.assertGreater(regression.centered_geometry_errors(dataset, dataset.targets, metrics)["notch_corner_error_mean"], 1)

    def test_lhouse_centered_shape_reports_notch_position_in_metres(self):
        dataset = self.lhouse_dataset(True)
        target = dataset.samples[0][1]
        result = regression.centered_geometry_errors(dataset, dataset.targets, {"pred": np.array([target])})
        self.assertLess(result["notch_corner_error_mean"], 1e-6)
        changed = target.copy()
        changed[2] += .05
        result = regression.centered_geometry_errors(dataset, dataset.targets, {"pred": np.array([changed])})
        self.assertAlmostEqual(result["notch_corner_error_mean"], 1.0, places=5)

    def test_lhouse_config_persists_the_opt_in_frame(self):
        targets = ["outerLength", "outerWidth", "cutoutLengthRatio", "cutoutWidthRatio", "height"]
        argv = ["main_reg.py", "--mode", "train", "--class_name", "LHouse", "--targets", *targets,
                "--canonical_align", "--lhouse_canonical"]
        with patch.object(sys, "argv", argv):
            args = regression.parse_args()
        with patch.object(Path, "write_text") as writer:
            regression.save_config(Path("unused"), args, targets, np.zeros(5), np.ones(5), [], np.zeros(0), np.ones(0))
        payload = json.loads(writer.call_args.args[0])
        self.assertTrue(payload["lhouse_canonical"])
        self.assertEqual(payload["canonical_frame_version"], "lhouse_full_yaw_v1")
        self.assertEqual(payload["canonical_aux_units"], "legacy")
        self.assertEqual(payload["targets"], targets)
        args.canonical_aux_units = "physical"
        with patch.object(Path, "write_text") as writer:
            regression.save_config(Path("unused"), args, targets, np.zeros(5), np.ones(5), [], np.zeros(0), np.ones(0))
        self.assertEqual(json.loads(writer.call_args.args[0])["canonical_aux_units"], "physical")

    def predict_lhouse(self, enabled, reliable=True):
        targets = ["outerLength", "outerWidth", "cutoutLengthRatio", "cutoutWidthRatio", "height"]
        checkpoint = {"class_name": "LHouse", "canonical_align": True, "normalize_points": False,
                      "lhouse_canonical": enabled}
        points = np.zeros((4, 3), dtype=np.float32)
        orientation = {"reliable": enabled and reliable, "status": "GEOMETRY_UNCERTAIN" if enabled else "SKIPPED_CLASS",
                       "reason": "test direction", "aligned_points": points, "canonical_angle_deg": 120,
                       "verdict": "PASS", "pca_ratio": 2, "peak_concentration": 0, "n_ransac_segments": 0}
        model = lambda batch, aux: torch.tensor([[20, 10, .4, .6, 5]])
        loaded = (model, checkpoint, targets, torch.zeros(5), torch.ones(5), torch.zeros(0), torch.ones(0))
        args = SimpleNamespace(log_dir="unused", cpu=True, input="points.txt", num_points=4, canonical_align=False)
        with patch.object(regression, "load_checkpoint", return_value=loaded), \
             patch.object(regression, "load_txt_points", return_value=points), \
             patch.object(regression, "canonical_orientation", return_value=orientation) as orient, \
             patch.object(regression, "sample_points", return_value=points), \
             patch("builtins.print") as printer:
            regression.predict(args)
        self.assertEqual(orient.call_args.kwargs["lhouse_canonical"], enabled)
        return json.loads(printer.call_args.args[0])

    def test_lhouse_inference_uses_checkpoint_flag_and_full_pose(self):
        legacy = self.predict_lhouse(False)
        self.assertFalse(legacy["canonical_alignment"]["applied"])
        self.assertNotIn("poseRotateZ", legacy)
        trial = self.predict_lhouse(True)
        self.assertTrue(trial["canonical_alignment"]["applied"])
        self.assertEqual(trial["poseRotateZ"], 120)

    def test_lhouse_uncertain_inference_does_not_silently_use_legacy_frame(self):
        with self.assertRaisesRegex(SystemExit, "uncertain"):
            self.predict_lhouse(True, reliable=False)


class LHouseCornerTrainingTests(unittest.TestCase):
    targets = ["outerLength", "outerWidth", "cutoutLengthRatio", "cutoutWidthRatio", "height"]

    def args(self, *extra):
        argv = ["main_reg.py", "--mode", "train", "--class_name", "LHouse", "--targets", *self.targets,
                "--canonical_align", "--lhouse_canonical", "--canonical_aux_units", "physical", *extra]
        with patch.object(sys, "argv", argv):
            return regression.parse_args()

    def test_notch_formula_and_primary_output_gradients(self):
        true = torch.tensor([[15.1, 6.2, .6, .6, 5.2]])
        torch.testing.assert_close(regression.lhouse_notch_xy(true, self.targets), torch.tensor([[6.04, 2.48]]))
        pred = torch.tensor([[17., 6.35, .56, .59, 5.3]], requires_grad=True)
        std = torch.tensor([[7.6, 2.4, .11, .07, 1.3]])
        criterion = torch.nn.SmoothL1Loss()
        loss = regression.lhouse_corner_loss(pred, true, self.targets, std, criterion)
        self.assertGreater(loss.item(), 0)
        self.assertEqual(regression.lhouse_corner_loss(true, true, self.targets, std, criterion).item(), 0)
        loss.backward()
        self.assertTrue(torch.isfinite(pred.grad).all())
        self.assertTrue((pred.grad[0, :2] > 0).all())
        self.assertTrue((pred.grad[0, 2:4] < 0).all())
        self.assertEqual(pred.grad[0, 4].item(), 0)

    def test_loss_is_unit_invariant_and_uses_target_names(self):
        true = torch.tensor([[15.1, 6.2, .6, .6, 5.2]])
        pred = torch.tensor([[17., 6.35, .56, .59, 5.3]])
        std = torch.tensor([7.6, 2.4, .11, .07, 1.3])
        criterion = torch.nn.SmoothL1Loss()
        expected = regression.lhouse_corner_loss(pred, true, self.targets, std, criterion)
        units = torch.tensor([100., 100., 1., 1., 100.])
        actual = regression.lhouse_corner_loss(pred * units, true * units, self.targets, std * units, criterion)
        torch.testing.assert_close(actual, expected)
        order = [4, 3, 2, 1, 0]
        actual = regression.lhouse_corner_loss(pred[:, order], true[:, order],
                                              [self.targets[i] for i in order], std[order], criterion)
        torch.testing.assert_close(actual, expected)

    def test_corner_loss_does_not_replace_parameter_loss(self):
        true = torch.tensor([[20., 10., .4, .6, 5.]])
        pred = torch.tensor([[24., 10., .5, .6, 5.]])
        criterion = torch.nn.SmoothL1Loss()
        self.assertEqual(regression.lhouse_corner_loss(pred, true, self.targets, torch.ones(5), criterion).item(), 0)
        self.assertGreater(criterion(pred, true).item(), 0)

    def test_flag_is_opt_in_and_invalid_training_modes_are_rejected(self):
        self.assertEqual(self.args().lhouse_corner_loss_weight, 0)
        self.assertEqual(self.args("--lhouse_corner_loss_weight", ".2").lhouse_corner_loss_weight, .2)
        invalid = [("--lhouse_corner_loss_weight", value) for value in ("-.1", "nan", "inf")]
        invalid += [("--lhouse_corner_loss_weight", ".2", *option) for option in
                    [("--class_name", "Cuboid"), ("--canonical_aux_units", "legacy"),
                     ("--target_mode", "bbox_residual"), ("--targets", "outerLength")]]
        for option in invalid:
            with self.subTest(option=option), self.assertRaises(SystemExit):
                self.args(*option)

    def test_robust_bbox_is_opt_in_and_cannot_mix_experiments(self):
        self.assertFalse(self.args().lhouse_robust_bbox)
        self.assertTrue(self.args("--lhouse_robust_bbox").lhouse_robust_bbox)
        for option in (("--canonical_aux_units", "legacy"), ("--class_name", "Cuboid"),
                       ("--lhouse_corner_loss_weight", ".2")):
            with self.subTest(option=option), self.assertRaises(SystemExit):
                self.args("--lhouse_robust_bbox", *option)

    def test_training_wires_loss_logs_and_checkpoint_without_changing_selection(self):
        targets = self.targets

        class TinyDataset:
            geom_aux_targets = []

            def __init__(self):
                self.samples = [(Path(f"sample_{i}.txt"), np.array(y, dtype=np.float32), None, {}, 0)
                                for i, y in enumerate([[20, 10, .4, .6, 5], [30, 12, .5, .5, 6]])]

            def __len__(self):
                return len(self.samples)

            def __getitem__(self, index):
                path, y, _, _, _ = self.samples[index]
                return torch.zeros(4, 3), torch.zeros(0), torch.from_numpy(y), torch.zeros(0), str(path)

        class TinyModel(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.output = torch.nn.Parameter(torch.zeros(1, len(targets)))

            def forward(self, points, aux):
                return self.output.expand(points.shape[0], -1)

        for weight, robust in ((0, False), (.2, False), (0, True)):
            with self.subTest(weight=weight, robust=robust), tempfile.TemporaryDirectory() as directory:
                extra = ["--lhouse_robust_bbox"] if robust else []
                args = self.args("--lhouse_corner_loss_weight", str(weight), "--epochs", "1", "--no_aux", "--cpu", *extra)
                args.log_dir, args.backbone = directory, "test_stub"
                with patch.object(regression, "build_model", return_value=TinyModel()), \
                     patch.object(regression, "ParamRegressionDataset", return_value=TinyDataset()) as constructor, \
                     patch.object(regression, "lhouse_corner_loss", wraps=regression.lhouse_corner_loss) as loss, \
                     patch("builtins.print"):
                    regression.train(args)
                self.assertEqual(loss.call_count, 1 if weight else 0)
                expected_mode = regression.LHOUSE_ROBUST_BBOX_MODE if robust else "full"
                for call in constructor.call_args_list:
                    self.assertEqual(call.kwargs["canonical_bbox_mode"], expected_mode)
                config = json.loads((Path(directory) / "regression_config.json").read_text(encoding="utf-8"))
                checkpoint = torch.load(Path(directory) / "best_model.pth", map_location="cpu", weights_only=False)
                for payload in (config, checkpoint):
                    self.assertEqual(payload["lhouse_corner_loss_weight"], weight)
                    self.assertEqual(payload["canonical_bbox_mode"], expected_mode)
                    self.assertEqual(payload["targets"], targets)
                    self.assertEqual(payload["selection_metric"], "weighted_standardized_mae")
                    self.assertEqual(payload["loss_recipe"], "standardized_wrapped_smooth_l1_lhouse_corner_v1"
                                     if weight else "standardized_wrapped_smooth_l1_v2")
                with (Path(directory) / "train_history.csv").open(encoding="utf-8-sig", newline="") as f:
                    row = next(csv.DictReader(f))
                self.assertEqual("val_local_notch_mean" in row, bool(weight))
                if weight:
                    self.assertTrue(np.isfinite(float(row["val_local_notch_mean"])))
                    self.assertGreaterEqual(float(row["val_local_notch_p95"]), float(row["val_local_notch_mean"]))


if __name__ == "__main__":
    unittest.main()
