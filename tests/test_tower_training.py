"""CPU-only tests for tower training preflight and per-class model selection."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from primitive_names import PRIMITIVES


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AppStub:
    def get(self, path):
        return lambda fn: fn

    post = get


runner = load_script("train_ptv3_reg")
# Only path selection is under test; no HTTP framework or GPU is required.
with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub, Request=object),
                              "pydantic": SimpleNamespace(BaseModel=object)}):
    server = load_script("ptv3_server")


class TowerTrainingTest(unittest.TestCase):
    def setUp(self):
        self.recipe = next(r for r in runner.RECIPES if r[0] == "CurvedRoofRoundHouse")
        self.params = dict(baseRadius=8, baseHeight=2, middleHeight=2,
                           middleTopRadius=1, middleRise=.2, coneHeight=1.5,
                           eaveRadius=8.96, eaveHeight=.4)
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "metadata").mkdir()
        self.addCleanup(self.temp.cleanup)

    def validate(self, params):
        records = [{"type": "CurvedRoofRoundHouse", "file": "train/CurvedRoofRoundHouse/sample_00001.txt",
                    "params": params}]
        (self.root / "metadata/sample_params.json").write_text(json.dumps(records), encoding="utf-8")
        runner.validate_tower_targets(self.root, [self.recipe])

    def test_recipe_predicts_eight_physical_dimensions(self):
        targets = self.recipe[2].split()
        self.assertEqual(len(targets), 8)
        self.assertIn("middleRise", targets)
        self.assertNotIn("middleBulge", targets)
        self.assertEqual(targets[-2:], ["eaveRadius", "eaveHeight"])
        self.assertEqual(len(self.recipe[3].split()), len(targets))

    def test_current_dimensions_are_valid(self):
        self.validate(self.params)

    def test_scaled_dimensions_remain_valid(self):
        for scale in (.5, 1, 2):
            params = {key: value * scale for key, value in self.params.items()}
            with self.subTest(scale=scale):
                self.validate(params)
                self.assertAlmostEqual(params["middleRise"] / params["middleHeight"], .1)
                self.assertAlmostEqual(params["eaveRadius"] / params["baseRadius"], 1.12)

    def test_top_radius_has_no_percentage_limits(self):
        for radius in (.01, 5, self.params["eaveRadius"] - .01):
            self.validate({**self.params, "middleTopRadius": radius})
        for radius in (0, -.01, self.params["eaveRadius"], self.params["eaveRadius"] + 1):
            with self.subTest(radius=radius), self.assertRaises(ValueError):
                self.validate({**self.params, "middleTopRadius": radius})

    def test_legacy_bulge_is_not_a_rise_in_metres(self):
        params = {k: v for k, v in self.params.items() if k != "middleRise"}
        with self.assertRaisesRegex(ValueError, "Regenerate"):
            self.validate({**params, "middleBulge": .3})
        with self.assertRaisesRegex(ValueError, "legacy"):
            self.validate({**self.params, "middleBulge": .3})

    def test_rise_range_depends_on_middle_height(self):
        for rise in (0, .5):
            self.validate({**self.params, "middleRise": rise})
        for key, value in (("middleRise", -.001), ("middleRise", .501),
                           ("middleRise", float("nan")), ("middleRise", True),
                           ("middleHeight", 0)):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                self.validate({**self.params, key: value})

    def test_legacy_six_target_data_is_rejected(self):
        params = {k: v for k, v in self.params.items() if not k.startswith("eave")}
        with self.assertRaisesRegex(ValueError, "Regenerate"):
            self.validate(params)

    def test_invalid_geometry_is_rejected(self):
        for key, value in (("eaveRadius", 8), ("eaveHeight", 2), ("baseHeight", 0)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.validate({**self.params, key: value})

    def test_other_classes_do_not_require_tower_labels(self):
        runner.validate_tower_targets(self.root, [runner.RECIPES[0]])

    def test_tall_base_accepts_thin_eave(self):
        self.validate({**self.params, "baseHeight": 20, "eaveHeight": .4})

    def test_manual_sizes_outside_sampling_range_are_valid(self):
        self.validate({**self.params, "eaveRadius": 12, "eaveHeight": 1})

    def test_unconfigured_server_uses_v10_default(self):
        with patch.object(server, "TOWER_LOG", ""):
            self.assertEqual(server.regression_log_dir("CurvedRoofRoundHouse").name,
                             "ptv3_reg_curvedround_v10_normals")

    def test_only_tower_uses_override(self):
        with patch.object(server, "TOWER_LOG", "logs/ptv3_reg_fourstage_v4_eaves_normals"):
            self.assertEqual(server.regression_log_dir("CurvedRoofRoundHouse"),
                             server.PTV3_DIR / "logs/ptv3_reg_fourstage_v4_eaves_normals")
            self.assertEqual(server.regression_log_dir("CurvedRoofRoundHouse", "_v3_normals"),
                             server.PTV3_DIR / "logs/ptv3_reg_fourstage_v3_normals")
            self.assertEqual(server.regression_log_dir("FlatRoofHouse").name,
                             "ptv3_reg_flatroof_v10_normals")
            self.assertEqual(server.health()["tower_reg_log"], str(server.regression_log_dir("CurvedRoofRoundHouse")))

    def test_explicit_missing_override_does_not_fall_back(self):
        with patch.object(server, "TOWER_LOG", str(self.root / "missing")):
            response = server.regress(SimpleNamespace(primitive_type="CurvedRoofRoundHouse", model_suffix=""))
            self.assertFalse(response["ok"])
            self.assertIn("missing", response["error"])

    def test_v9_request_wins_over_old_class_overrides(self):
        with patch.object(server, "TOWER_LOG", "logs/old_tower"), \
             patch.object(server, "GABLED_LOG", "logs/old_gabled"):
            for name, stem in server.STEM_NAMES.items():
                with self.subTest(primitive=name):
                    self.assertEqual(server.regression_log_dir(name, "_v9_normals").name,
                                     f"ptv3_reg_{stem}_v9_normals")
            with self.assertRaisesRegex(ValueError, "Invalid model_suffix"):
                server.regression_log_dir("CurvedRoofRoundHouse", "../../outside")

    def test_missing_v9_checkpoint_does_not_fall_back_to_class_override(self):
        with patch.object(server, "TOWER_LOG", "logs/old_tower"), \
             patch.object(Path, "is_file", return_value=False), \
             patch.object(server, "run_json_command") as run:
            response = server.regress(SimpleNamespace(primitive_type="CurvedRoofRoundHouse",
                                                     model_suffix="_v9_normals"))
        self.assertFalse(response["ok"])
        self.assertIn("ptv3_reg_fourstage_v9_normals", response["error"])
        run.assert_not_called()

    def test_v9_regression_requests_use_selected_checkpoint_for_all_classes(self):
        legacy_stems = ("cuboid", "cylinder", "lhouse", "conecylinder", "gabledroof", "pyramidroof", "truncatedpyramid", "halfcylinder", "cylinderdome", "indentedcuboid", "asymgable", "fourstage", "twogable")
        for (name, _, _, _), stem in zip(runner.RECIPES, legacy_stems):
            request = SimpleNamespace(primitive_type=name, model_suffix="_v9_normals",
                                      input_coordinates="normalized", points_text="0 0 0\n",
                                      num_points=2048, bbox_x=20, bbox_y=10, bbox_z=5, scale=12)
            with self.subTest(primitive=name), patch.object(Path, "is_file", return_value=True), \
                 patch.object(server, "run_json_command", return_value={"ok": True, "result": {
                     "params": {"middleRise": .2}}}) as run:
                response = server.regress(request)
                self.assertTrue(response["ok"])
                command = run.call_args.args[0]
                self.assertEqual(Path(command[command.index("--log_dir") + 1]).name,
                                 f"ptv3_reg_{stem}_v9_normals")
                self.assertEqual(command[command.index("--input_coordinates") + 1], "normalized")

    def test_server_rejects_legacy_or_invalid_tower_output(self):
        request = SimpleNamespace(primitive_type="CurvedRoofRoundHouse", model_suffix="",
                                  points_text="0 0 0\n", num_points=2048,
                                  bbox_x=18, bbox_y=18, bbox_z=6, scale=10)
        outputs = [({"middleBulge": .3}, False), ({}, False),
                   ({"middleRise": float("nan")}, False), ({"middleRise": True}, False),
                   ({"middleRise": .2, "middleBulge": .3}, False),
                   ({"middleRise": .2}, True), ({"middleRise": 0}, True)]
        for params, expected in outputs:
            with self.subTest(params=params), patch.object(Path, "is_file", return_value=True), \
                 patch.object(server, "run_json_command", return_value={"ok": True, "result": {"params": params}}):
                response = server.regress(request)
                self.assertEqual(response["ok"], expected)
                if not expected:
                    self.assertIn("middleRise", response["error"])

    def test_v10_requests_accept_old_and_new_names_and_ignore_old_overrides(self):
        for old, new, _, slug, _ in PRIMITIVES:
            for alias in (old, new):
                request = SimpleNamespace(primitive_type=alias, model_suffix="_v10_normals",
                                          input_coordinates="normalized", points_text="0 0 0\n",
                                          num_points=2048, bbox_x=20, bbox_y=10, bbox_z=5, scale=12)
                with self.subTest(primitive=alias), patch.object(server, "TOWER_LOG", "logs/old_tower"), \
                     patch.object(server, "GABLED_LOG", "logs/old_gabled"), \
                     patch.object(Path, "is_file", return_value=True), \
                     patch.object(server, "run_json_command", return_value={"ok": True, "result": {
                         "params": {"middleRise": .2}}}) as run:
                    response = server.regress(request)
                self.assertTrue(response["ok"])
                command = run.call_args.args[0]
                expected = server.PTV3_DIR / "logs" / f"ptv3_reg_{slug}_v10_normals"
                self.assertEqual(command[command.index("--log_dir") + 1], str(expected))
                self.assertEqual(response["model_log_dir"], str(expected))

    def test_missing_v10_checkpoint_does_not_fall_back_to_v9(self):
        with patch.object(Path, "is_file", return_value=False), \
             patch.object(server, "run_json_command") as run:
            response = server.regress(SimpleNamespace(primitive_type="LHouse", model_suffix="_v10_normals"))
        self.assertFalse(response["ok"])
        self.assertIn("ptv3_reg_lshaped_v10_normals", response["error"])
        run.assert_not_called()

    def test_classifier_override_keeps_regression_default(self):
        with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub, Request=object),
                                      "pydantic": SimpleNamespace(BaseModel=object)}), \
             patch.dict("os.environ", {"PTV3_CLS_LOG": "logs/ptv3_cls_v6", "PTV3_TOWER_LOG": "",
                                       "PTV3_REG_SUFFIX": "_v3_normals"}):
            configured = load_script("ptv3_server")
        self.assertEqual(configured.CLS_LOG, configured.PTV3_DIR / "logs/ptv3_cls_v6")
        self.assertEqual(configured.regression_log_dir("FlatRoofHouse").name, "ptv3_reg_cuboid_v3_normals")

    def test_default_classifier_and_regression_are_v10(self):
        with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub, Request=object),
                                      "pydantic": SimpleNamespace(BaseModel=object)}), \
             patch.dict("os.environ", {}, clear=True):
            configured = load_script("ptv3_server")
        self.assertEqual(configured.CLS_LOG, configured.PTV3_DIR / "logs/ptv3_cls_v10")
        self.assertEqual(configured.health()["cls_log"], str(configured.CLS_LOG))
        self.assertEqual(configured.REG_SUFFIX, "_v10_normals")
        for old, new, _, slug, _ in PRIMITIVES:
            for alias in (old, new):
                self.assertEqual(configured.regression_log_dir(alias).name,
                                 f"ptv3_reg_{slug}_v10_normals")

    def test_classifier_request_uses_v10_and_removes_temporary_input(self):
        with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub, Request=object),
                                      "pydantic": SimpleNamespace(BaseModel=object)}), \
             patch.dict("os.environ", {}, clear=True):
            configured = load_script("ptv3_server")
        expected = {"ok": True, "result": [{"class": "LShapedHouse", "prob": .99}]}
        request = SimpleNamespace(points_text="0 0 0\n1 1 1\n", num_points=2048, topk=3)
        with patch.object(configured, "run_json_command", return_value=expected) as run:
            response = configured.classify(request)
        self.assertEqual(response["result"], expected["result"])
        self.assertEqual(response["model_log_dir"], str(configured.CLS_LOG))
        command = run.call_args.args[0]
        self.assertEqual(command[command.index("--log_dir") + 1],
                         str(configured.PTV3_DIR / "logs/ptv3_cls_v10"))
        self.assertEqual(command[command.index("--topk") + 1], "3")
        self.assertFalse(Path(command[command.index("--input") + 1]).exists())

    def test_classifier_reports_actual_custom_model_directory(self):
        request = SimpleNamespace(points_text="0 0 0\n", num_points=2048, topk=3)
        selected = server.PTV3_DIR / "logs/ptv3_cls_custom"
        with patch.object(server, "CLS_LOG", selected), \
             patch.object(server, "run_json_command", return_value={"ok": True, "result": []}):
            response = server.classify(request)
        self.assertEqual(response["model_log_dir"], str(selected))

    def test_failed_classification_does_not_report_a_loaded_model(self):
        request = SimpleNamespace(points_text="0 0 0\n", num_points=2048, topk=3)
        with patch.object(server, "run_json_command", return_value={"ok": False, "error": "load failed"}):
            response = server.classify(request)
        self.assertFalse(response["ok"])
        self.assertNotIn("model_log_dir", response)

    def test_blank_regression_suffix_uses_v10_default(self):
        with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub, Request=object),
                                      "pydantic": SimpleNamespace(BaseModel=object)}), \
             patch.dict("os.environ", {"PTV3_REG_SUFFIX": "  "}, clear=True):
            configured = load_script("ptv3_server")
        self.assertEqual(configured.REG_SUFFIX, "_v10_normals")

    def test_v4_directory_name_keeps_canonical_training(self):
        args = SimpleNamespace(log_root="logs", run_tag="v4", data_root="data", batch_size=4,
                               seed=42, epochs=100, grid_size=.02, patch_size=128, ptv3_repo=None,
                               flash=False, canonical_align=True)
        legacy_stems = ("cuboid", "cylinder", "lhouse", "conecylinder", "gabledroof", "pyramidroof", "truncatedpyramid", "halfcylinder", "cylinderdome", "indentedcuboid", "asymgable", "fourstage", "twogable")
        for recipe, legacy_stem in zip(runner.RECIPES, legacy_stems):
            with self.subTest(primitive=recipe[0]):
                log, command, _ = runner.commands(args, recipe)
                self.assertEqual(log.name, f"ptv3_reg_{legacy_stem}_v4_normals")
                self.assertIn("--canonical_align", command)

    def test_lhouse_trial_keeps_targets_and_loss_and_only_enables_lhouse(self):
        args = SimpleNamespace(log_root="logs", run_tag="axis_trial", data_root="data", batch_size=4,
                               seed=42, epochs=100, grid_size=.02, patch_size=128, ptv3_repo=None,
                               flash=False, canonical_align=True, lhouse_canonical=False)
        recipe = next(r for r in runner.RECIPES if r[0] == "LShapedHouse")
        _, baseline, _ = runner.commands(args, recipe)
        args.lhouse_canonical = True
        log, trial, test = runner.commands(args, recipe)
        self.assertEqual(log.name, "ptv3_reg_lshaped_axis_trial_normals")
        units_index = trial.index("--canonical_aux_units")
        self.assertEqual(trial[units_index + 1], "physical")
        without_units = trial[:units_index] + trial[units_index + 2:]
        self.assertEqual([arg for arg in without_units if arg != "--lhouse_canonical"], baseline)
        self.assertIn("--lhouse_canonical", test)
        _, other, _ = runner.commands(args, runner.RECIPES[0])
        self.assertNotIn("--lhouse_canonical", other)

    def test_server_forwards_declared_coordinate_units_only_when_present(self):
        for coordinates in (None, "physical", "normalized"):
            request = SimpleNamespace(primitive_type="LShapedHouse", model_suffix="", input_coordinates=coordinates,
                                      points_text="0 0 0\n", num_points=2048,
                                      bbox_x=20, bbox_y=10, bbox_z=5, scale=12)
            with self.subTest(coordinates=coordinates), patch.object(Path, "is_file", return_value=True), \
                 patch.object(server, "run_json_command", return_value={"ok": True}) as run:
                self.assertTrue(server.regress(request)["ok"])
            command = run.call_args.args[0]
            if coordinates is None:
                self.assertNotIn("--input_coordinates", command)
            else:
                self.assertEqual(command[command.index("--input_coordinates") + 1], coordinates)

    def test_lhouse_corner_trial_changes_only_training_loss(self):
        args = SimpleNamespace(log_root="logs", run_tag="corner_trial", data_root="data", batch_size=4,
                               seed=42, epochs=100, grid_size=.02, patch_size=128, ptv3_repo=None,
                               flash=False, canonical_align=True, lhouse_canonical=True, lhouse_corner_loss_weight=0)
        recipe = next(r for r in runner.RECIPES if r[0] == "LShapedHouse")
        _, baseline, baseline_test = runner.commands(args, recipe)
        args.lhouse_corner_loss_weight = .2
        log, trial, test = runner.commands(args, recipe)
        self.assertEqual(log.name, "ptv3_reg_lshaped_corner_trial_normals")
        index = trial.index("--lhouse_corner_loss_weight")
        self.assertEqual(trial[index + 1], "0.2")
        self.assertEqual(trial[:index] + trial[index + 2:], baseline)
        self.assertEqual(test, baseline_test)
        _, other, _ = runner.commands(args, runner.RECIPES[0])
        self.assertNotIn("--lhouse_corner_loss_weight", other)

    def test_robust_bbox_trial_changes_only_lhouse_training_features(self):
        args = SimpleNamespace(log_root="logs", run_tag="robust_bbox_trial", data_root="data", batch_size=4,
                               seed=42, epochs=100, grid_size=.02, patch_size=128, ptv3_repo=None,
                               flash=False, canonical_align=True, lhouse_canonical=True,
                               lhouse_corner_loss_weight=0, lhouse_robust_bbox=False)
        recipe = next(r for r in runner.RECIPES if r[0] == "LShapedHouse")
        _, baseline, baseline_test = runner.commands(args, recipe)
        args.lhouse_robust_bbox = True
        log, trial, test = runner.commands(args, recipe)
        self.assertEqual(log.name, "ptv3_reg_lshaped_robust_bbox_trial_normals")
        self.assertEqual([arg for arg in trial if arg != "--lhouse_robust_bbox"], baseline)
        self.assertEqual(test, baseline_test)
        self.assertNotIn("--lhouse_corner_loss_weight", trial)
        _, other, _ = runner.commands(args, runner.RECIPES[0])
        self.assertNotIn("--lhouse_robust_bbox", other)

    def test_runner_rejects_robust_bbox_without_lhouse_or_with_corner_loss(self):
        for options in (("--classes", "LShapedHouse"),
                        ("--classes", "LShapedHouse", "--lhouse_canonical", "--lhouse_corner_loss_weight", ".2"),
                        ("--classes", "FlatRoofHouse", "--lhouse_canonical")):
            with self.subTest(options=options), patch.object(sys, "argv", ["train_ptv3_reg.py", "--dry_run", "--lhouse_robust_bbox", *options]), \
                 patch("sys.stderr"), self.assertRaises(SystemExit):
                runner.main()

    def test_lhouse_request_selects_physical_scale_trial(self):
        suffix = "_axis_scale_trial_normals"
        expected = server.PTV3_DIR / "logs/ptv3_reg_lshaped_axis_scale_trial_normals"
        self.assertEqual(server.regression_log_dir("LShapedHouse", suffix), expected)
        self.assertEqual(server.regression_log_dir("FlatRoofHouse").name, "ptv3_reg_flatroof_v10_normals")
        request = SimpleNamespace(primitive_type="LShapedHouse", model_suffix=suffix, input_coordinates="normalized",
                                  points_text="0 0 0\n", num_points=2048,
                                  bbox_x=20, bbox_y=10, bbox_z=5, scale=12)
        with patch.object(Path, "is_file", return_value=True), \
             patch.object(server, "run_json_command", return_value={"ok": True}) as run:
            response = server.regress(request)
        command = run.call_args.args[0]
        self.assertEqual(command[command.index("--log_dir") + 1], str(expected))
        self.assertEqual(command[command.index("--input_coordinates") + 1], "normalized")
        self.assertEqual(response["model_log_dir"], str(expected))

    def test_lhouse_request_selects_robust_bbox_trial(self):
        suffix = "_robust_bbox_trial_normals"
        expected = server.PTV3_DIR / "logs/ptv3_reg_lshaped_robust_bbox_trial_normals"
        self.assertEqual(server.regression_log_dir("LShapedHouse", suffix), expected)
        request = SimpleNamespace(primitive_type="LShapedHouse", model_suffix=suffix, input_coordinates="normalized",
                                  points_text="0 0 0\n", num_points=2048,
                                  bbox_x=20, bbox_y=10, bbox_z=5, scale=12)
        with patch.object(Path, "is_file", return_value=True), \
             patch.object(server, "run_json_command", return_value={"ok": True}) as run:
            response = server.regress(request)
        command = run.call_args.args[0]
        self.assertEqual(command[command.index("--log_dir") + 1], str(expected))
        self.assertEqual(command[command.index("--input_coordinates") + 1], "normalized")
        self.assertEqual(response["model_log_dir"], str(expected))
        self.assertEqual(server.REG_SUFFIX, "_v10_normals")

    def test_missing_lhouse_robust_checkpoint_does_not_fall_back(self):
        request = SimpleNamespace(primitive_type="LShapedHouse", model_suffix="_robust_bbox_trial_normals")
        with patch.object(Path, "is_file", return_value=False), \
             patch.object(server, "run_json_command") as run:
            response = server.regress(request)
        self.assertFalse(response["ok"])
        self.assertIn("ptv3_reg_lshaped_robust_bbox_trial_normals", response["error"])
        run.assert_not_called()

    def test_classifier_custom_path_is_preserved(self):
        with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub, Request=object),
                                      "pydantic": SimpleNamespace(BaseModel=object)}), \
             patch.dict("os.environ", {"PTV3_CLS_LOG": "logs/ptv3_cls_custom"}):
            configured = load_script("ptv3_server")
        self.assertEqual(configured.CLS_LOG, configured.PTV3_DIR / "logs/ptv3_cls_custom")

    def test_regression_suffix_can_select_canonical_models(self):
        with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub, Request=object),
                                      "pydantic": SimpleNamespace(BaseModel=object)}), \
             patch.dict("os.environ", {"PTV3_REG_SUFFIX": "_v4_canonical_normals"}):
            configured = load_script("ptv3_server")
        self.assertEqual(configured.health()["reg_suffix"], "_v4_canonical_normals")
        self.assertEqual(configured.regression_log_dir("GableRoofHouse").name,
                         "ptv3_reg_gabledroof_v4_canonical_normals")

    def test_request_suffix_is_used_and_cannot_escape_model_root(self):
        self.assertEqual(server.regression_log_dir("GableRoofHouse", "_v3_normals").name,
                         "ptv3_reg_gabledroof_v3_normals")
        with self.assertRaisesRegex(ValueError, "Invalid model_suffix"):
            server.regression_log_dir("GableRoofHouse", "../../outside")


if __name__ == "__main__":
    unittest.main()
