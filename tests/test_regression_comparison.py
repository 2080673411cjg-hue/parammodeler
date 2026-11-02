"""Test paired comparison and output isolation without CUDA or model inference."""

import csv
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("comparison", ROOT / "scripts/compare_ptv3_reg.py")
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


class RegressionComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "tests")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self):
        data, logs, output = self.root / "data", self.root / "logs", self.root / "evaluation"
        (data / "metadata").mkdir(parents=True)
        records = []
        for class_name, slug, old_tag in comparison.RECIPES:
            for i in range(100):
                relative = f"test/{class_name}/sample_{i + 1:05d}.txt"
                source = data / relative
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_text("0 0 0\n1 1 1\n", encoding="utf-8")
                records.append({"type": class_name, "split": "test", "file": relative,
                                "horizontalScaleBand": i % 3, "params": {"size": i + 1}})
            for tag in (old_tag, "v7"):
                directory = logs / f"ptv3_reg_{slug}_{tag}_normals"
                directory.mkdir(parents=True)
                (directory / "best_model.pth").write_bytes(b"unchanged checkpoint")
                (directory / "regression_config.json").write_text(json.dumps({
                    "class_name": class_name, "canonical_align": True, "targets": ["size"]}), encoding="utf-8")
        records.append({"type": "TriPrismPyramid", "split": "test", "file": "ignored.txt"})
        (data / "metadata/sample_params.json").write_text(json.dumps(records), encoding="utf-8")
        return data, logs, output

    def test_plan_and_snapshot_select_only_fixed_three_test_classes(self):
        data, logs, output = self.fixture()
        source_before = (data / "metadata/sample_params.json").read_bytes()
        records, models = comparison.prepare_plan(data, logs, output)
        self.assertEqual(len(records), 300)
        self.assertFalse(output.exists())
        dataset, manifest = comparison.snapshot_dataset(data, output, records, models)
        self.assertEqual(len(list(dataset.glob("test/*/*.txt"))), 300)
        self.assertEqual(len(manifest["test_files"]), 300)
        self.assertEqual(len(comparison.read_json(dataset / "metadata/sample_params.json")), 300)
        self.assertEqual((data / "metadata/sample_params.json").read_bytes(), source_before)
        self.assertFalse((dataset / "train").exists())
        self.assertFalse((dataset / "test/TriPrismPyramid").exists())
        for pair in models.values():
            for model in pair.values():
                self.assertEqual((Path(model["directory"]) / "best_model.pth").read_bytes(), b"unchanged checkpoint")

    def test_existing_evaluation_and_source_dataset_output_are_rejected(self):
        data, logs, output = self.fixture()
        output.mkdir()
        (output / "keep.txt").write_text("keep", encoding="utf-8")
        for destination in (output, data / "new-evaluation"):
            with self.assertRaises(ValueError):
                comparison.prepare_plan(data, logs, destination)
        self.assertEqual((output / "keep.txt").read_text(encoding="utf-8"), "keep")

    def test_missing_checkpoint_or_wrong_targets_fail_before_creating_output(self):
        data, logs, output = self.fixture()
        config = logs / "ptv3_reg_cuboid_v7_normals/regression_config.json"
        value = comparison.read_json(config)
        value["targets"] = ["wrong"]
        config.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Target order differs"):
            comparison.prepare_plan(data, logs, output)
        value["targets"] = ["size"]
        config.write_text(json.dumps(value), encoding="utf-8")
        (logs / "ptv3_reg_cuboid_v7_normals/best_model.pth").unlink()
        with self.assertRaises(FileNotFoundError):
            comparison.prepare_plan(data, logs, output)
        self.assertFalse(output.exists())

    def test_invalid_duplicate_or_missing_scale_records_are_rejected(self):
        data, logs, output = self.fixture()
        metadata = data / "metadata/sample_params.json"
        original = comparison.read_json(metadata)
        for change in ("duplicate", "path", "band"):
            records = json.loads(json.dumps(original))
            if change == "duplicate":
                records.append(records[0])
            elif change == "path":
                records[0]["file"] = "test/Cuboid/../../escape.txt"
            else:
                records[0].pop("horizontalScaleBand")
            metadata.write_text(json.dumps(records), encoding="utf-8")
            with self.assertRaises(ValueError):
                comparison.prepare_plan(data, logs, output)
        self.assertFalse(output.exists())

    def test_both_models_use_identical_test_options_and_never_train(self):
        for tag in ("v4", "v7"):
            command = comparison.test_command(self.root / "dataset", self.root / tag,
                                              self.root / f"out-{tag}", "Cuboid", "metadata")
            self.assertEqual(command[command.index("--mode") + 1], "test")
            self.assertEqual(command[command.index("--seed") + 1], "42")
            self.assertEqual(command[command.index("--num_points") + 1], "2048")
            self.assertEqual(command[command.index("--data_root") + 1], str(self.root / "dataset"))
            self.assertIn("--eval_output_dir", command)
            self.assertNotIn("--epochs", command)

    def paired_fixture(self):
        expected = {f"test/Cuboid/{i}.txt": {"horizontalScaleBand": i % 3} for i in range(4)}
        old = {file: {"size": (10., 12.)} for file in expected}
        new = {file: {"size": (10., 11.)} for file in expected}
        return expected, old, new

    def test_paired_metrics_win_rate_and_scale_groups(self):
        expected, old, new = self.paired_fixture()
        rows, failures = comparison.compare_predictions("Cuboid", "metadata", ["size"], expected, old, new)
        overall = rows[0]
        self.assertEqual(overall["old_mae"], 2.)
        self.assertEqual(overall["v7_p95"], 1.)
        self.assertEqual(overall["v7_mape_percent"], 10.)
        self.assertEqual(overall["v7_win_percent"], 100.)
        self.assertEqual([row["expected"] for row in rows], [4, 2, 1, 1])
        self.assertFalse(failures)

    def test_geometry_failures_are_visible_and_only_common_clouds_are_paired(self):
        expected, old, new = self.paired_fixture()
        old.pop("test/Cuboid/0.txt")
        new.pop("test/Cuboid/1.txt")
        new["test/Cuboid/2.txt"] = old["test/Cuboid/2.txt"]
        rows, failures = comparison.compare_predictions("Cuboid", "geometry", ["size"], expected, old, new)
        self.assertEqual(rows[0]["paired"], 2)
        self.assertEqual(rows[0]["old_failed"], 1)
        self.assertEqual(rows[0]["v7_failed"], 1)
        self.assertEqual(rows[0]["v7_win_percent"], 50.)
        self.assertEqual(rows[0]["tie_percent"], 50.)
        self.assertEqual(len(failures), 2)
        self.assertEqual(rows[1]["paired"], 1)
        with self.assertRaisesRegex(ValueError, "must predict every cloud"):
            comparison.compare_predictions("Cuboid", "metadata", ["size"], expected, old, new)

    def test_zero_paired_group_is_not_reported_as_zero_error(self):
        expected, _, _ = self.paired_fixture()
        rows, failures = comparison.compare_predictions("Cuboid", "geometry", ["size"], expected, {}, {})
        self.assertIsNone(rows[0]["v7_mae"])
        self.assertIsNone(rows[0]["v7_win_percent"])
        self.assertEqual(rows[0]["paired"], 0)
        self.assertEqual(len(failures), 4)

    def test_different_ground_truth_or_duplicate_predictions_are_rejected(self):
        expected, old, new = self.paired_fixture()
        new["test/Cuboid/0.txt"]["size"] = (11., 12.)
        with self.assertRaisesRegex(ValueError, "Ground truth differs"):
            comparison.compare_predictions("Cuboid", "metadata", ["size"], expected, old, new)
        path = self.root / "predictions.csv"
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["file", "true_size", "pred_size"])
            writer.writerows([["/data/test/Cuboid/0.txt", 10, 11]] * 2)
        with self.assertRaisesRegex(ValueError, "duplicate prediction"):
            comparison.load_predictions(path, ["size"], expected)

    def test_serial_runner_generates_comparison_without_touching_source_reports(self):
        data, logs, output = self.fixture()
        for directory in logs.iterdir():
            (directory / "regression_report_test.csv").write_bytes(b"keep original report")

        def fake_test(command, **kwargs):
            self.assertEqual(command[command.index("--mode") + 1], "test")
            snapshot = Path(command[command.index("--metadata") + 1])
            records = comparison.read_json(snapshot)
            class_name = command[command.index("--class_name") + 1]
            destination = Path(command[command.index("--eval_output_dir") + 1])
            destination.mkdir()
            orientation = command[command.index("--test_orientation") + 1]
            suffix = "test_geometry" if orientation == "geometry" else "test"
            is_new = Path(command[command.index("--log_dir") + 1]).name.endswith("_v7_normals")
            predictions = [{"file": str(snapshot.parents[1] / record["file"]),
                            "true_size": record["params"]["size"],
                            "pred_size": record["params"]["size"] + (1 if is_new else 2)}
                           for record in records if record["type"] == class_name]
            comparison.write_csv(destination / f"regression_predictions_{suffix}.csv", predictions)

        argv = ["compare_ptv3_reg.py", "--data_root", str(data), "--log_root", str(logs),
                "--output_root", str(output)]
        fake_torch = SimpleNamespace(cuda=SimpleNamespace(is_available=lambda: True))
        with patch("sys.argv", argv), patch.dict("sys.modules", {"torch": fake_torch}), \
             patch.object(comparison.importlib.util, "find_spec", return_value=object()), \
             patch.object(comparison.subprocess, "run", side_effect=fake_test) as run, patch("builtins.print"):
            comparison.main()
        self.assertEqual(run.call_count, 12)
        self.assertEqual(comparison.read_json(output / "manifest.json")["status"], "complete")
        with (output / "comparison.csv").open(encoding="utf-8-sig", newline="") as stream:
            summaries = list(csv.DictReader(stream))
        self.assertEqual(len(summaries), 24)
        self.assertTrue(all(float(row["v7_win_percent"]) == 100 for row in summaries))
        for directory in logs.iterdir():
            self.assertEqual((directory / "regression_report_test.csv").read_bytes(), b"keep original report")


if __name__ == "__main__":
    unittest.main()
