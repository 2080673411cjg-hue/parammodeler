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


def noisy_roof(class_name, length, width, seed=19):
    """Roof surfaces plus walls, without metadata or explicit ridge-line points."""
    rng = np.random.default_rng(seed)
    xy = rng.uniform(0, 1, (2400, 2)) * [length, width]
    x, y = xy.T
    tent = np.minimum(y / (width * .7), (width - y) / (width * .3))
    if class_name == "HalfCylinderRoof":
        roof = width / 2 * np.sqrt(np.maximum(0, 1 - (2 * y / width - 1) ** 2))
    elif class_name == "AsymmetricGableHouse":
        roof = 2 * np.minimum(tent, np.minimum(x, length - x) / (length * .25))
    else:
        roof = 2 * tent
    points = np.column_stack((xy, 5 + roof))
    walls = rng.uniform(0, 1, (1200, 3)) * [length, width, 5]
    for side in range(4):
        axis = side % 2
        walls[side::4, axis] = 0 if side < 2 else (length, width)[axis]
    points = np.vstack((points, walls))
    points += rng.normal(0, .006 * np.linalg.norm([length, width, 7]), points.shape)
    # Sparse high outliers affect the former z.min/ptp gate and unconstrained PCA.
    outliers = np.column_stack((np.full(10, length / 2),
                                np.linspace(-width * .1, width * 1.1, 10),
                                np.linspace(points[:, 2].max() + .1,
                                            points[:, 2].max() + 2, 10)))
    return np.vstack((points, outliers)).astype(np.float32)


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

    def test_roof_candidates_ignore_sparse_high_outliers(self):
        for class_name in ("GabledRoof", "AsymmetricGableHouse", "HalfCylinderRoof"):
            points = noisy_roof(class_name, length=12, width=10)
            for yaw in (-158, -27, 64, 89):
                with self.subTest(class_name=class_name, yaw=yaw):
                    rotated = canonical.rotate_points_z(points, yaw)
                    original = rotated.copy()
                    result = canonical.canonical_orientation(rotated, class_name)
                    self.assertTrue(result["reliable"], result["reason"])
                    error = abs(canonical.fold_axis_degrees(result["canonical_angle_deg"] - yaw))
                    self.assertLess(error, 2)
                    np.testing.assert_array_equal(rotated, original)
                    np.testing.assert_allclose(result["rotation_matrix"] @ result["rotation_matrix"].T,
                                               np.eye(3), atol=1e-6)

    def test_short_wide_roofs_compare_relative_candidate_spans(self):
        # Absolute top extents alone can favor the width of a short, wide roof.
        for class_name in ("GabledRoof", "AsymmetricGableHouse", "HalfCylinderRoof"):
            points = noisy_roof(class_name, length=6, width=40)
            for yaw in (22, -64):
                with self.subTest(class_name=class_name, yaw=yaw):
                    result = canonical.canonical_orientation(canonical.rotate_points_z(points, yaw), class_name)
                    self.assertTrue(result["reliable"], result["reason"])
                    error = abs(canonical.fold_axis_degrees(result["canonical_angle_deg"] - yaw))
                    self.assertLess(error, 2)

    def test_flat_surface_does_not_fabricate_a_ridge(self):
        for class_name in ("GabledRoof", "AsymmetricGableHouse", "HalfCylinderRoof"):
            with self.subTest(class_name=class_name):
                result = canonical.canonical_orientation(rectangle(), class_name)
                self.assertFalse(result["reliable"])
                self.assertEqual(result["status"], "GEOMETRY_UNCERTAIN")

    def test_indistinguishable_top_axes_do_not_claim_a_reliable_rotation(self):
        top = rectangle(length=1, width=1)
        top[:, 2] = 1
        points = canonical.rotate_points_z(np.vstack((rectangle(width=2), top)), 31)
        self.assertGreater(float(np.ptp(points[:, 2])), 0)
        for class_name in ("GabledRoof", "AsymmetricGableHouse", "HalfCylinderRoof"):
            with self.subTest(class_name=class_name):
                result = canonical.canonical_orientation(points, class_name)
                self.assertFalse(result["reliable"])
                self.assertEqual(result["status"], "GEOMETRY_UNCERTAIN")
                np.testing.assert_array_equal(result["rotation_matrix"], np.eye(3))
                np.testing.assert_array_equal(result["aligned_points"], points)

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

    def test_lhouse_legacy_checkpoint_stays_unrotated(self):
        self.assertIsNone(canonical.canonical_angle_from_metadata({"params": {"rz": 120}}, "LHouse"))
        self.assertEqual(canonical.canonical_orientation(rectangle(), "LHouse")["status"], "SKIPPED_CLASS")

    def test_lhouse_metadata_keeps_the_full_direction_and_targets(self):
        for yaw in (0, 69, 120, 180, -174, 270):
            record = {"params": {"rz": yaw}}
            theta = canonical.canonical_angle_from_metadata(record, "LHouse", lhouse_canonical=True)
            self.assertAlmostEqual((theta - yaw + 180) % 360 - 180, 0)
            values = np.array([20, 10, .4, .6, 5])
            np.testing.assert_array_equal(canonical.canonical_target_values(
                values, ["outerLength", "outerWidth", "cutoutLengthRatio", "cutoutWidthRatio", "height"],
                record, "LHouse", theta), values.astype(np.float32))

    def test_lhouse_geometry_resolves_missing_corner_for_all_directions(self):
        rng = np.random.default_rng(42)
        for length, width in ((20, 10), (10, 20), (10, 10), (15, 40), (40, 6), (40, 40)):
            xy = rng.uniform(0, 1, (8000, 2)) * [length, width]
            xy = xy[(xy[:, 0] <= length * .6) | (xy[:, 1] <= width * .4)]
            top = np.column_stack((xy, np.full(len(xy), 5))).astype(np.float32)
            for yaw in (0, 44, 69, 93, 120, 179, -174, -71):
                with self.subTest(length=length, width=width, yaw=yaw):
                    points = canonical.rotate_points_z(top, yaw)
                    result = canonical.canonical_orientation(points, "LHouse", lhouse_canonical=True)
                    self.assertTrue(result["reliable"], result["reason"])
                    error = abs((result["canonical_angle_deg"] - yaw + 180) % 360 - 180)
                    # Surface-only sampling gives an approximate rectangle, but not a quarter-turn ambiguity.
                    self.assertLess(error, 5)

    def test_lhouse_rejects_rectangle_or_sparse_corner_support(self):
        rng = np.random.default_rng(7)
        full = np.column_stack((rng.uniform(0, 1, (3000, 2)) * [20, 10], np.full(3000, 5)))
        self.assertFalse(canonical.canonical_orientation(full, "LHouse", lhouse_canonical=True)["reliable"])
        self.assertFalse(canonical.canonical_orientation(rectangle(), "LHouse", lhouse_canonical=True)["reliable"])


if __name__ == "__main__":
    unittest.main()
