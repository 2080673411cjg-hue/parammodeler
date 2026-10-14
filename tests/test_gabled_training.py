"""CPU checks for five-target gabled data and independent model selection."""

import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from test_tower_training import runner, server


class GabledTrainingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "metadata").mkdir()
        self.recipe = next(r for r in runner.RECIPES if r[0] == "GabledRoof")

    def tearDown(self):
        self.temp.cleanup()

    def validate(self, params):
        (self.root / "metadata/sample_params.json").write_text(
            json.dumps([{"type": "GabledRoof", "file": "train/GabledRoof/sample.txt", "params": params}]), encoding="utf-8")
        runner.validate_gabled_targets(self.root, [self.recipe])

    def test_new_target_and_legacy_data_guard(self):
        self.assertEqual(self.recipe[2].split()[-1], "ridgeRatio")
        self.assertEqual(len(self.recipe[2].split()), 5)
        for ratio in (.2, .5, .8):
            self.validate({"ridgeRatio": ratio})
        for params in ({}, {"ridgeRatio": .9}, {"ridgeRatio": float("nan")}, {"ridgeRatio": True}):
            with self.assertRaises(ValueError):
                self.validate(params)

    def test_gabled_flag_only_applies_to_gabled_recipe(self):
        args = SimpleNamespace(log_root="logs", run_tag="trial", data_root="data", batch_size=4,
                               seed=42, epochs=100, grid_size=.02, patch_size=128, ptv3_repo=None,
                               flash=False, gabled_ridge_symmetry_180=True)
        _, command, _ = runner.commands(args, self.recipe)
        self.assertIn("--gabled_ridge_symmetry_180", command)
        _, other, _ = runner.commands(args, runner.RECIPES[0])
        self.assertNotIn("--gabled_ridge_symmetry_180", other)

    def test_server_override_does_not_change_other_classes(self):
        with patch.object(server, "GABLED_LOG", "logs/ptv3_reg_gabledroof_trial_normals"):
            self.assertEqual(server.regression_log_dir("GabledRoof").name, "ptv3_reg_gabledroof_trial_normals")
            self.assertEqual(server.regression_log_dir("Cuboid").name,
                             "ptv3_reg_cuboid_v4_canonical_normals")
            self.assertEqual(server.health()["gabled_reg_log"], str(server.regression_log_dir("GabledRoof")))


if __name__ == "__main__":
    unittest.main()
