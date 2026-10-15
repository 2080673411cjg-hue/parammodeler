"""Exercise test-time geometry frames without CUDA or model weights."""

import importlib.util
from pathlib import Path
import sys
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


if __name__ == "__main__":
    unittest.main()
