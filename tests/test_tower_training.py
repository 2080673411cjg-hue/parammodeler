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
with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub),
                              "pydantic": SimpleNamespace(BaseModel=object)}):
    server = load_script("ptv3_server")


class TowerTrainingTest(unittest.TestCase):
    def setUp(self):
        self.recipe = next(r for r in runner.RECIPES if r[0] == "FourStageRoundTower")
        self.params = dict(baseRadius=8, baseHeight=2, middleHeight=2,
                           middleTopRadius=1, middleRise=.2, coneHeight=1.5,
                           eaveRadius=8.96, eaveHeight=.4)
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "metadata").mkdir()
        self.addCleanup(self.temp.cleanup)

    def validate(self, params):
        records = [{"type": "FourStageRoundTower", "file": "train/FourStageRoundTower/sample_00001.txt",
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

    def test_unconfigured_server_uses_canonical_default(self):
        with patch.object(server, "TOWER_LOG", ""):
            self.assertEqual(server.regression_log_dir("FourStageRoundTower").name,
                             "ptv3_reg_fourstage_v4_canonical_normals")

    def test_only_tower_uses_override(self):
        with patch.object(server, "TOWER_LOG", "logs/ptv3_reg_fourstage_v4_eaves_normals"):
            self.assertEqual(server.regression_log_dir("FourStageRoundTower"),
                             server.PTV3_DIR / "logs/ptv3_reg_fourstage_v4_eaves_normals")
            self.assertEqual(server.regression_log_dir("FourStageRoundTower", "_v3_normals"),
                             server.PTV3_DIR / "logs/ptv3_reg_fourstage_v4_eaves_normals")
            self.assertEqual(server.regression_log_dir("Cuboid").name,
                             "ptv3_reg_cuboid_v4_canonical_normals")
            self.assertEqual(server.health()["tower_reg_log"], str(server.regression_log_dir("FourStageRoundTower")))

    def test_explicit_missing_override_does_not_fall_back(self):
        with patch.object(server, "TOWER_LOG", str(self.root / "missing")):
            response = server.regress(SimpleNamespace(primitive_type="FourStageRoundTower", model_suffix=""))
            self.assertFalse(response["ok"])
            self.assertIn("missing", response["error"])

    def test_server_rejects_legacy_or_invalid_tower_output(self):
        request = SimpleNamespace(primitive_type="FourStageRoundTower", model_suffix="",
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

    def test_classifier_override_keeps_regression_default(self):
        with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub),
                                      "pydantic": SimpleNamespace(BaseModel=object)}), \
             patch.dict("os.environ", {"PTV3_CLS_LOG": "logs/ptv3_cls_v6", "PTV3_TOWER_LOG": "",
                                       "PTV3_REG_SUFFIX": "_v3_normals"}):
            configured = load_script("ptv3_server")
        self.assertEqual(configured.CLS_LOG, configured.PTV3_DIR / "logs/ptv3_cls_v6")
        self.assertEqual(configured.regression_log_dir("Cuboid").name, "ptv3_reg_cuboid_v3_normals")

    def test_default_classifier_is_v6_without_changing_regression(self):
        with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub),
                                      "pydantic": SimpleNamespace(BaseModel=object)}), \
             patch.dict("os.environ", {}, clear=True):
            configured = load_script("ptv3_server")
        self.assertEqual(configured.CLS_LOG, configured.PTV3_DIR / "logs/ptv3_cls_v6")
        self.assertEqual(configured.health()["cls_log"], str(configured.CLS_LOG))
        self.assertEqual(configured.REG_SUFFIX, "_v4_canonical_normals")

    def test_classifier_custom_path_is_preserved(self):
        with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub),
                                      "pydantic": SimpleNamespace(BaseModel=object)}), \
             patch.dict("os.environ", {"PTV3_CLS_LOG": "logs/ptv3_cls_custom"}):
            configured = load_script("ptv3_server")
        self.assertEqual(configured.CLS_LOG, configured.PTV3_DIR / "logs/ptv3_cls_custom")

    def test_regression_suffix_can_select_canonical_models(self):
        with patch.dict(sys.modules, {"fastapi": SimpleNamespace(FastAPI=AppStub),
                                      "pydantic": SimpleNamespace(BaseModel=object)}), \
             patch.dict("os.environ", {"PTV3_REG_SUFFIX": "_v4_canonical_normals"}):
            configured = load_script("ptv3_server")
        self.assertEqual(configured.health()["reg_suffix"], "_v4_canonical_normals")
        self.assertEqual(configured.regression_log_dir("GabledRoof").name,
                         "ptv3_reg_gabledroof_v4_canonical_normals")

    def test_request_suffix_is_used_and_cannot_escape_model_root(self):
        self.assertEqual(server.regression_log_dir("GabledRoof", "_v3_normals").name,
                         "ptv3_reg_gabledroof_v3_normals")
        with self.assertRaisesRegex(ValueError, "Invalid model_suffix"):
            server.regression_log_dir("GabledRoof", "../../outside")


if __name__ == "__main__":
    unittest.main()
