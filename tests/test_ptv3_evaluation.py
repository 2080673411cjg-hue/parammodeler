"""Verify that batch evaluation never trains or overwrites model reports."""

import csv
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
spec = importlib.util.spec_from_file_location("v10_evaluation", ROOT / "scripts/evaluate_ptv3_reg.py")
evaluation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evaluation)
sys.path.pop(0)


class EvaluationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "tests")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self, recipes):
        data, logs, output = (self.root / name for name in ("data", "logs", "output"))
        (data / "metadata").mkdir(parents=True)
        records = []
        for name, stem, _, _ in recipes:
            for index in range(200):
                relative = f"test/{name}/{index:05d}.txt"
                path = data / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("0 0 0\n", encoding="utf-8")
                records.append({"file": relative, "split": "test", "type": name})
            model = logs / f"ptv3_reg_{stem}_v10_normals"
            model.mkdir(parents=True)
            (model / "best_model.pth").write_bytes(b"checkpoint fixture")
            (model / "regression_config.json").write_text(json.dumps({
                "class_name": name, "canonical_align": True}), encoding="utf-8")
            (model / "regression_metrics_test_geometry.csv").write_bytes(b"keep original report")
        (data / "metadata/sample_params.json").write_text(json.dumps(records), encoding="utf-8")
        return data, logs, output

    def test_all_thirteen_classes_are_selected_without_triangular_prism(self):
        data, logs, output = self.fixture(evaluation.RECIPES)
        records, jobs = evaluation.prepare_plan(data, logs, output, "v10")
        self.assertEqual(len(jobs), 13)
        self.assertEqual(len(records), 2600)
        self.assertNotIn("TriPrismPyramid", {job["class"] for job in jobs})
        self.assertTrue(all(Path(job["directory"]).name.endswith("_v10_normals") for job in jobs))
        self.assertFalse(output.exists())

    def test_existing_output_is_not_overwritten(self):
        data, logs, output = self.fixture(evaluation.RECIPES[:1])
        output.mkdir()
        (output / "keep.txt").write_text("keep")
        with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
            evaluation.prepare_plan(data, logs, output, "v10")
        self.assertEqual((output / "keep.txt").read_text(), "keep")

    def test_missing_checkpoint_fails_before_creating_output(self):
        recipes = evaluation.RECIPES[:1]
        data, logs, output = self.fixture(recipes)
        next(logs.glob("*/best_model.pth")).unlink()
        with patch.object(evaluation, "RECIPES", recipes), self.assertRaises(FileNotFoundError):
            evaluation.prepare_plan(data, logs, output, "v10")
        self.assertFalse(output.exists())

    def test_serial_geometry_tests_keep_source_reports(self):
        recipes = evaluation.RECIPES[:1]
        data, logs, output = self.fixture(recipes)

        def fake_test(command, **kwargs):
            self.assertEqual(command[command.index("--mode") + 1], "test")
            self.assertEqual(command[command.index("--test_orientation") + 1], "geometry")
            destination = Path(command[command.index("--eval_output_dir") + 1])
            destination.mkdir()
            (destination / "orientation_test_geometry.json").write_text(json.dumps({
                "accepted": 199, "failed": 1, "alignment_attempted": 200}), encoding="utf-8")
            evaluation.write_csv(destination / "regression_metrics_test_geometry.csv",
                                 [{"target": "length", "mae": .2}])

        argv = ["evaluate_ptv3_reg.py", "--data_root", str(data), "--log_root", str(logs),
                "--output_root", str(output)]
        fake_torch = SimpleNamespace(cuda=SimpleNamespace(is_available=lambda: True))
        with patch.object(evaluation, "RECIPES", recipes), patch("sys.argv", argv), \
             patch.dict(sys.modules, {"torch": fake_torch}), \
             patch.object(evaluation.importlib.util, "find_spec", return_value=object()), \
             patch.object(evaluation.subprocess, "run", side_effect=fake_test), patch("builtins.print"):
            evaluation.main()
        manifest = evaluation.read_json(output / "manifest.json")
        self.assertEqual(manifest["status"], "complete")
        self.assertEqual(manifest["completed"], ["FlatRoofHouse"])
        with (output / "summary.csv").open(encoding="utf-8-sig", newline="") as stream:
            row = next(csv.DictReader(stream))
        self.assertEqual(row["failed"], "1")
        self.assertEqual(next(logs.glob("*/regression_metrics_test_geometry.csv")).read_bytes(),
                         b"keep original report")

    def test_failed_test_records_failure_and_dry_run_writes_nothing(self):
        recipes = evaluation.RECIPES[:1]
        data, logs, output = self.fixture(recipes)
        argv = ["evaluate_ptv3_reg.py", "--data_root", str(data), "--log_root", str(logs),
                "--output_root", str(output)]
        with patch.object(evaluation, "RECIPES", recipes), patch("sys.argv", argv + ["--dry_run"]), \
             patch("builtins.print"), patch.object(evaluation.subprocess, "run") as run:
            evaluation.main()
            run.assert_not_called()
        self.assertFalse(output.exists())
        fake_torch = SimpleNamespace(cuda=SimpleNamespace(is_available=lambda: True))
        with patch.object(evaluation, "RECIPES", recipes), patch("sys.argv", argv), \
             patch.dict(sys.modules, {"torch": fake_torch}), \
             patch.object(evaluation.importlib.util, "find_spec", return_value=object()), \
             patch.object(evaluation.subprocess, "run", side_effect=evaluation.subprocess.CalledProcessError(1, ["test"])), \
             patch("sys.stderr"), self.assertRaises(SystemExit):
            evaluation.main()
        self.assertEqual(evaluation.read_json(output / "manifest.json")["status"], "failed")


if __name__ == "__main__":
    unittest.main()
