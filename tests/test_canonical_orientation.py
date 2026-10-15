"""CPU checks for canonical geometry and directional target labels."""

import importlib.util
from pathlib import Path
import unittest

import numpy as np


spec = importlib.util.spec_from_file_location(
    "canonical_orientation", Path(__file__).resolve().parents[1] / "scripts/canonical_orientation.py")
canonical = importlib.util.module_from_spec(spec)
spec.loader.exec_module(canonical)


def rectangle(length=2.0, width=0.7):
    t = np.linspace(-0.5, 0.5, 128, dtype=np.float32)
    xy = np.vstack((np.column_stack((t * length, np.full_like(t, width / 2))),
                    np.column_stack((t * length, np.full_like(t, -width / 2))),
                    np.column_stack((np.full_like(t, length / 2), t * width)),
                    np.column_stack((np.full_like(t, -length / 2), t * width))))
    return np.column_stack((xy, np.zeros(len(xy), dtype=np.float32)))


class CanonicalOrientationTests(unittest.TestCase):
    def test_rectangle_axes_at_histogram_boundary(self):
        for angle in (0, 45, 89, 90, 120, 180):
            with self.subTest(angle=angle):
                points = canonical.rotate_points_z(rectangle(), angle)
                result = canonical.canonical_orientation(points, "Cuboid")
                self.assertTrue(result["reliable"], result["reason"])
                error = abs(canonical.fold_axis_degrees(result["canonical_angle_deg"] - angle))
                self.assertLess(error, 1.0)

    def test_near_square_is_not_rejected_as_circular(self):
        points = canonical.rotate_points_z(rectangle(width=1.9), 37)
        result = canonical.canonical_orientation(points, "IndentedCuboid")
        self.assertTrue(result["reliable"], result["reason"])
        self.assertLess(abs(canonical.fold_axis_degrees(result["canonical_angle_deg"] - 37)), 1.0)

    def test_square_reports_axis_ambiguity_but_has_rectangle_frame(self):
        result = canonical.canonical_orientation(rectangle(width=2.0), "Cuboid")
        self.assertTrue(result["reliable"], result["reason"])
        self.assertEqual(result["verdict"], "WEAK")

    def test_roof_uses_ridge_axis_even_when_width_is_longer(self):
        footprint = rectangle(length=1.0, width=2.0)
        ridge = np.column_stack((np.linspace(-0.5, 0.5, 128),
                                 np.full(128, 0.3), np.ones(128)))
        points = canonical.rotate_points_z(np.vstack((footprint, ridge)).astype(np.float32), 89)
        result = canonical.canonical_orientation(points, "AsymmetricGableHouse")
        self.assertTrue(result["reliable"], result["reason"])
        self.assertLess(abs(canonical.fold_axis_degrees(result["canonical_angle_deg"] - 89)), 1.0)

    def test_folded_roof_ratio_preserves_physical_ridge_side(self):
        for angle in (0, 90, 120, 180):
            record = {"params": {"rz": angle}}
            theta = canonical.canonical_angle_from_metadata(record, "GabledRoof")
            q = canonical.canonical_target_values(np.array([0.3]), ["ridgeRatio"], record,
                                                   "GabledRoof", theta)[0]
            original = np.array([0, (0.3 - 0.5) * 10])
            restored = np.array([0, (q - 0.5) * 10])
            def rotate(point, yaw):
                a = np.deg2rad(yaw)
                return np.array([[np.cos(a), -np.sin(a)], [np.sin(a), np.cos(a)]]) @ point
            np.testing.assert_allclose(rotate(original, angle), rotate(restored, theta), atol=1e-6)

    def test_directional_labels_allow_small_geometric_angle_error(self):
        values = canonical.canonical_target_values(
            np.array([0.3]), ["ridgeRatio"], {"params": {"rz": 120}}, "GabledRoof", -55)
        self.assertAlmostEqual(float(values[0]), 0.7, places=6)

    def test_unsupported_class_remains_unrotated(self):
        result = canonical.canonical_orientation(rectangle(), "TwoGableHouses")
        self.assertEqual(result["status"], "SKIPPED_CLASS")
        np.testing.assert_array_equal(result["aligned_points"], rectangle())


if __name__ == "__main__":
    unittest.main()
