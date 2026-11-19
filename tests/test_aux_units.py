"""Prediction auxiliary features must match each checkpoint's training units."""

import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np
import torch

from test_canonical_pipeline import regression


class AuxiliaryUnitTests(unittest.TestCase):
    names = ["bbox_x", "bbox_y", "bbox_z", "scale", "bbox_volume", "aspect_ratio_xy",
             "bbox_diagonal", "bbox_x_norm", "bbox_y_norm", "bbox_z_norm"]
    targets = ["outerLength", "outerWidth", "cutoutLengthRatio", "cutoutWidthRatio", "height"]
    scale = 10.0

    def setUp(self):
        self.local = np.array([[0, 0, 0], [20, 0, 5], [0, 10, 5], [12, 4, 5]], dtype=np.float32)
        self.physical = regression.rotate_points_z(self.local, 120) + [100, -50, 20]
        self.normalized = (self.physical - self.physical.mean(0)) / self.scale

    def record(self, normalized):
        info = {"bboxSize": np.ptp(self.physical, axis=0).tolist(), "scale": self.scale}
        if normalized:
            info["normalization"] = "centered_by_mean_and_scaled_by_max_radius"
        return {"type": "LShapedHouse", "split": "test", "file": "test/LShapedHouse/a.txt",
                "params": dict(zip(self.targets, [20, 10, .4, .6, 5]), rz=120),
                "pointCloudInfo": info}

    def dataset(self, normalized, units="physical", train=False, bbox_mode="full"):
        points = self.normalized if normalized else self.physical
        with patch.object(regression, "load_metadata", return_value=[self.record(normalized)]), \
             patch.object(regression, "load_txt_points", return_value=points), \
             patch.object(Path, "exists", return_value=True):
            return regression.ParamRegressionDataset(
                "data", "metadata.json", "LShapedHouse", "test", self.targets, 4, train=train,
                aux_names=self.names, canonical_align=True, lhouse_canonical=True,
                canonical_aux_units=units, canonical_bbox_mode=bbox_mode)

    def predict(self, normalized, units="physical", coordinates="declared", metadata=False,
                bbox_mode=None, robust_flag=False, class_name="LShapedHouse",
                canonical_align=True, target_mode="raw", input_scale=None):
        points = self.normalized if normalized else self.physical
        orientation = {"reliable": True, "status": "LHOUSE_DIRECTION_OK", "reason": "test direction",
                       "aligned_points": regression.rotate_points_z(points, -120),
                       "canonical_angle_deg": 120, "verdict": "PASS", "pca_ratio": 2,
                       "peak_concentration": 0, "n_ransac_segments": 0}
        checkpoint = {"class_name": class_name, "canonical_align": canonical_align, "lhouse_canonical": True,
                      "aux_names": self.names, "target_mode": target_mode}
        if units is not None:
            checkpoint["canonical_aux_units"] = units
        if bbox_mode is not None:
            checkpoint["canonical_bbox_mode"] = bbox_mode
        if coordinates == "declared":
            coordinates = "normalized" if normalized else "physical"
        captured = {}

        def model(batch, aux):
            captured["points"] = batch.numpy()
            captured["aux"] = aux.numpy()[0]
            return torch.tensor([[20, 10, .4, .6, 5]])

        loaded = (model, checkpoint, self.targets, torch.zeros(5), torch.ones(5),
                  torch.zeros(len(self.names)), torch.ones(len(self.names)))
        args = SimpleNamespace(log_dir="unused", cpu=True, input="a.txt", num_points=4,
                               canonical_align=False, input_coordinates=coordinates,
                               metadata="metadata.json", data_root="data",
                               scale=self.scale if input_scale is None else input_scale,
                               bbox_x=20, bbox_y=10, bbox_z=5, lhouse_robust_bbox=robust_flag)
        record = self.record(normalized)
        with patch.object(regression, "load_checkpoint", return_value=loaded), \
             patch.object(regression, "load_txt_points", return_value=points), \
             patch.object(regression, "canonical_orientation", return_value=orientation), \
             patch.object(regression, "sample_points", side_effect=lambda p, *a, **kw: p), \
             patch.object(Path, "exists", return_value=metadata), \
             patch.object(regression, "load_metadata", return_value=[record]), \
             patch.object(regression, "find_metadata_record", return_value=record), \
             patch("builtins.print") as printer:
            regression.predict(args)
        return captured, json.loads(printer.call_args.args[0])

    def test_dataset_uses_identical_scene_units_for_both_storage_formats(self):
        physical = self.dataset(False)
        normalized = self.dataset(True)
        np.testing.assert_allclose(physical.samples[0][2], normalized.samples[0][2], rtol=1e-5)
        expected = [20, 10, 5, 10, 1000, .5, np.sqrt(525), 2, 1, .5]
        np.testing.assert_allclose(normalized.samples[0][2], expected, rtol=1e-5)
        np.testing.assert_array_equal(physical.samples[0][1], normalized.samples[0][1])

    def test_train_and_test_have_the_same_auxiliary_contract(self):
        np.testing.assert_array_equal(self.dataset(True, train=True).samples[0][2],
                                      self.dataset(True, train=False).samples[0][2])

    def test_legacy_dataset_preserves_the_previous_normalized_span(self):
        legacy = self.dataset(True, units="legacy").samples[0][2]
        physical = self.dataset(True).samples[0][2]
        np.testing.assert_allclose(legacy[:3] * self.scale, physical[:3], rtol=1e-5)
        self.assertAlmostEqual(float(legacy[4]) * self.scale**3, float(physical[4]), places=2)

    def test_remote_style_predict_agrees_for_physical_and_normalized_inputs(self):
        physical, _ = self.predict(False)
        normalized, result = self.predict(True)
        np.testing.assert_allclose(physical["aux"], normalized["aux"], rtol=1e-5)
        np.testing.assert_allclose(physical["points"], normalized["points"], atol=1e-6)
        np.testing.assert_allclose(normalized["aux"], self.dataset(True).samples[0][2], rtol=1e-5)
        self.assertAlmostEqual(result["aux_features"]["bbox_volume"], 1000, places=2)

    def test_local_metadata_can_declare_the_input_storage_frame(self):
        direct, _ = self.predict(True)
        from_metadata, _ = self.predict(True, coordinates=None, metadata=True)
        np.testing.assert_array_equal(direct["aux"], from_metadata["aux"])

    def test_old_checkpoint_without_unit_marker_keeps_legacy_behavior(self):
        old, _ = self.predict(True, units=None, coordinates=None)
        np.testing.assert_allclose(old["aux"], self.dataset(True, units="legacy").samples[0][2], rtol=1e-5)

    def test_legacy_canonical_prediction_matches_normalized_training_features(self):
        expected = self.dataset(True, units="legacy").samples[0][2]
        for class_name in ("GableRoofHouse", "ShortRidgeHipRoofHouse", "PyramidalRoofHouse",
                           "FlatTopHipRoofHouse", "BarrelRoofHouse", "RecessedRoofHouse",
                           "AngledGableHouse", "FlatRoofHouse"):
            with self.subTest(class_name=class_name):
                physical, actual = self.predict(False, units="legacy", class_name=class_name)
                normalized, reference = self.predict(True, units="legacy", class_name=class_name)
                np.testing.assert_allclose(physical["aux"], expected, rtol=1e-5)
                np.testing.assert_allclose(physical["aux"], normalized["aux"], rtol=1e-5)
                np.testing.assert_allclose(physical["points"], normalized["points"], atol=1e-6)
                self.assertEqual(actual["params"], reference["params"])

    def test_old_checkpoint_with_declared_physical_input_also_converts_legacy_units(self):
        physical, _ = self.predict(False, units=None)
        normalized, _ = self.predict(True, units=None)
        np.testing.assert_allclose(physical["aux"], normalized["aux"], rtol=1e-5)

    def test_legacy_normalized_metadata_preserves_existing_predictions(self):
        direct, _ = self.predict(True, units="legacy")
        metadata, _ = self.predict(True, units="legacy", coordinates=None, metadata=True)
        np.testing.assert_array_equal(direct["aux"], metadata["aux"])

    def test_legacy_metadata_physical_input_matches_normalized_training_features(self):
        physical, _ = self.predict(False, units="legacy", coordinates=None, metadata=True)
        normalized, _ = self.predict(True, units="legacy", coordinates=None, metadata=True)
        np.testing.assert_allclose(physical["aux"], normalized["aux"], rtol=1e-5)

    def test_legacy_bbox_residual_prediction_uses_the_same_training_baseline(self):
        _, physical = self.predict(False, units="legacy", class_name="FlatRoofHouse",
                                   target_mode="bbox_residual")
        _, normalized = self.predict(True, units="legacy", class_name="FlatRoofHouse",
                                     target_mode="bbox_residual")
        np.testing.assert_allclose(list(physical["params"].values()),
                                   list(normalized["params"].values()), rtol=1e-5)

    def test_noncanonical_prediction_preserves_physical_metadata_auxiliary_features(self):
        physical, _ = self.predict(False, units="legacy", canonical_align=False)
        normalized, _ = self.predict(True, units="legacy", canonical_align=False)
        np.testing.assert_allclose(physical["aux"], normalized["aux"], rtol=1e-5)
        self.assertAlmostEqual(float(physical["aux"][4]), 1000, places=2)

    def test_legacy_physical_input_requires_a_valid_normalization_scale(self):
        for scale in (0, -1, float("nan"), float("inf")):
            with self.subTest(scale=scale), self.assertRaisesRegex(ValueError, "positive physical scale"):
                self.predict(False, units="legacy", input_scale=scale)

    def test_physical_checkpoint_rejects_undeclared_remote_input(self):
        with self.assertRaisesRegex(ValueError, "Declare input_coordinates"):
            self.predict(True, coordinates=None)

    def test_normalized_input_requires_a_valid_scale(self):
        for scale in (None, 0, -1, float("nan"), float("inf")):
            with self.subTest(scale=scale), self.assertRaisesRegex(ValueError, "positive physical scale"):
                regression.physical_bbox_size(self.normalized, "normalized", scale)

    def test_unknown_unit_version_is_not_silently_treated_as_legacy(self):
        with self.assertRaisesRegex(ValueError, "Unsupported canonical auxiliary units"):
            self.dataset(True, units="unknown")
        with patch.object(Path, "exists", return_value=True), \
             patch.object(torch, "load", return_value={"canonical_aux_units": "unknown"}), \
             self.assertRaisesRegex(ValueError, "Unsupported checkpoint"):
            regression.load_checkpoint(Path("unused"), torch.device("cpu"), SimpleNamespace())

    def add_outlier(self):
        self.local = np.vstack((np.tile(self.local, (250, 1)), [100, 100, 50])).astype(np.float32)
        self.physical = regression.rotate_points_z(self.local, 120) + [100, -50, 20]
        self.normalized = (self.physical - self.physical.mean(0)) / self.scale

    def test_robust_bbox_changes_xy_only_and_preserves_input(self):
        self.add_outlier()
        points = self.local.copy()
        mode = regression.LHOUSE_ROBUST_BBOX_MODE
        full = regression.physical_bbox_size(points, "physical")
        robust = regression.physical_bbox_size(points, "physical", bbox_mode=mode)
        np.testing.assert_array_equal(full, [100, 100, 50])
        np.testing.assert_array_equal(robust, [20, 10, 50])
        np.testing.assert_array_equal(points, self.local)

    def test_robust_train_test_predict_and_coordinate_units_agree(self):
        self.add_outlier()
        mode = regression.LHOUSE_ROBUST_BBOX_MODE
        expected = self.dataset(True, bbox_mode=mode).samples[0][2]
        for normalized in (False, True):
            dataset = self.dataset(normalized, train=True, bbox_mode=mode)
            np.testing.assert_allclose(dataset.samples[0][2], expected, rtol=1e-5, atol=1e-4)
            captured, result = self.predict(normalized, bbox_mode=mode)
            np.testing.assert_allclose(captured["aux"], expected, rtol=1e-5, atol=1e-4)
            full, _ = self.predict(normalized)
            np.testing.assert_array_equal(captured["points"], full["points"])
            np.testing.assert_array_equal(dataset.samples[0][1], self.dataset(normalized).samples[0][1])
            self.assertAlmostEqual(result["aux_features"]["bbox_x"], 20, places=3)
            self.assertAlmostEqual(result["aux_features"]["bbox_y"], 10, places=3)
            self.assertAlmostEqual(result["aux_features"]["bbox_z"], 50, places=3)
            self.assertAlmostEqual(result["aux_features"]["aspect_ratio_xy"], .5, places=4)

    def test_predict_flag_cannot_override_old_checkpoint_bbox(self):
        self.add_outlier()
        old, _ = self.predict(True)
        flagged, _ = self.predict(True, robust_flag=True)
        np.testing.assert_array_equal(flagged["aux"], old["aux"])
        np.testing.assert_allclose(old["aux"], self.dataset(True).samples[0][2], rtol=1e-5)

    def test_unknown_or_incompatible_bbox_checkpoint_fails_before_model_load(self):
        mode = regression.LHOUSE_ROBUST_BBOX_MODE
        with self.assertRaisesRegex(ValueError, "Unsupported canonical bbox mode"):
            regression.physical_bbox_size(self.local, "physical", bbox_mode="unknown")
        for units, name, canonical, lhouse in (("legacy", "LShapedHouse", True, True),
                                                ("physical", "FlatRoofHouse", True, True),
                                                ("physical", "LShapedHouse", False, True),
                                                ("physical", "LShapedHouse", True, False)):
            with self.subTest(units=units, name=name, canonical=canonical, lhouse=lhouse), \
                 self.assertRaisesRegex(ValueError, "Robust bbox requires"):
                regression.validate_canonical_bbox_mode(mode, name, canonical, lhouse, units)
        with self.assertRaisesRegex(ValueError, "Unsupported canonical bbox mode"):
            self.dataset(True, bbox_mode="unknown")
        for checkpoint in ({"canonical_bbox_mode": "unknown"},
                           {"canonical_bbox_mode": mode, "class_name": "FlatRoofHouse", "canonical_aux_units": "physical"}):
            with patch.object(Path, "exists", return_value=True), \
                 patch.object(torch, "load", return_value=checkpoint), \
                 self.assertRaises(ValueError):
                regression.load_checkpoint(Path("unused"), torch.device("cpu"), SimpleNamespace())

    def test_test_entry_point_uses_saved_bbox_mode(self):
        self.add_outlier()
        mode = regression.LHOUSE_ROBUST_BBOX_MODE
        dataset = self.dataset(True, bbox_mode=mode)
        checkpoint = {"class_name": "LShapedHouse", "canonical_align": True, "lhouse_canonical": True,
                      "canonical_aux_units": "physical", "canonical_bbox_mode": mode}
        args = SimpleNamespace(log_dir="unused", cpu=True, class_name=None, num_points=4,
                               metadata="metadata.json", data_root="data", split="test", batch_size=1,
                               test_orientation="metadata", circular_targets=[], lhouse_robust_bbox=False)
        metrics = {"pred": np.array([dataset.samples[0][1]]), "mae": np.zeros(5),
                   "rmse": np.zeros(5), "mape": np.zeros(5), "r2": np.ones(5)}
        with patch.object(regression, "load_checkpoint", return_value=(None, checkpoint, self.targets, None, None, None, None)), \
             patch.object(regression, "ParamRegressionDataset", return_value=dataset) as constructor, \
             patch.object(regression, "evaluate", return_value=metrics), \
             patch.object(regression, "save_metrics"), patch.object(regression, "save_predictions"), \
             patch.object(regression, "save_regression_report"), patch.object(regression, "save_regression_errors"), \
             patch.object(Path, "write_text"), patch("builtins.print"):
            regression.test(args)
        self.assertEqual(constructor.call_args.kwargs["canonical_bbox_mode"], mode)


if __name__ == "__main__":
    unittest.main()
