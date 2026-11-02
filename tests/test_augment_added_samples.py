"""Regression tests for selected-class append and frozen validation/test records."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("incremental_augmentation", ROOT / "scripts/augment_added_samples.py")
augmentation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(augmentation)
CLASS = "TruncatedPyramidRoof"


class AppendAugmentationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "tests")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.raw, self.out = self.root / "raw", self.root / "aug"
        for root in (self.raw, self.out):
            (root / "metadata").mkdir(parents=True)
        self.old = [{"type": CLASS, "split": split, "file": f"{split}/{CLASS}/old.txt", "params": {"height": 1}}
                    for split in ("train", "val", "test")]
        # The ignored fourteenth class may already have stale metadata without clouds.
        self.old.append({"type": "TriPrismPyramid", "split": "train", "file": "train/TriPrismPyramid/old.txt"})
        self.new = [{"type": CLASS, "split": split, "file": f"{split}/{CLASS}/new.txt", "params": {"height": 2}}
                    for split in ("train", "val", "test")]
        self.records = self.old + self.new + [{"type": "Cuboid", "split": "train", "file": "train/Cuboid/new.txt"}]
        for record in self.records:
            path = self.raw / record["file"]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("0 0 0\n1 1 1\n", encoding="utf-8")
        for record in self.old:
            if record["type"] == CLASS:
                path = self.out / record["file"]
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"keep original augmented cloud\n")
        self.raw_meta = self.raw / "metadata/sample_params.json"
        self.aug_meta = self.out / "metadata/sample_params.json"
        self.raw_meta.write_text(json.dumps(self.records), encoding="utf-8")
        self.aug_meta.write_text(json.dumps(self.old), encoding="utf-8")
        (self.out / "metadata/augment_config.json").write_text(json.dumps({
            "num_points": 2048, "seed": 42, "drop_bottom": False, "bottom_height": .015,
            "noise": True, "noise_sigma": .006, "outliers": True,
            "outlier_ratio": .003, "outlier_extent": .03}), encoding="utf-8")
        self.engine = self.root / "engine.py"
        self.engine.write_text(
            'def load_points(path): return "new augmented cloud\\n"\n'
            'def augment_points(points, args):\n'
            '    assert args.noise_sigma == .006 and args.outlier_ratio == .003\n'
            '    assert args.outlier_extent == .03 and not args.drop_bottom\n'
            '    return points\n'
            'def write_points(path, points): path.write_text(points, encoding="utf-8")\n', encoding="utf-8")

    def run_script(self, *extra, classes=(CLASS,)):
        argv = ["augment_added_samples.py", "--input", str(self.raw), "--output", str(self.out),
                "--augment-module", str(self.engine), *extra]
        if classes is not None:
            argv += ["--classes", *classes]
        output = io.StringIO()
        with patch.object(sys, "argv", argv), contextlib.redirect_stdout(output):
            augmentation.main()
        return output.getvalue()

    def test_train_only_recovery_reuses_created_files_and_preserves_holdout(self):
        for record in self.new:
            (self.out / record["file"]).write_bytes(b"already enhanced before metadata failure\n")
        # Metadata-only recovery still imports the specified module, but calls no engine functions.
        self.engine.write_text('def augment_points(*args): raise AssertionError("do not rerun")\n', encoding="utf-8")
        before = {path.relative_to(self.out): path.read_bytes() for path in self.out.rglob("*.txt")}
        raw_before = self.raw_meta.read_bytes()
        output = self.run_script("--splits", "train", "--holes")
        self.assertIn("New samples to augment: 0", output)
        self.assertIn("Metadata records to append: 1", output)
        result = augmentation.read_records(self.aug_meta)
        self.assertEqual(result, self.old + [self.new[0]])
        self.assertEqual({path.relative_to(self.out): path.read_bytes() for path in self.out.rglob("*.txt")}, before)
        self.assertEqual(self.raw_meta.read_bytes(), raw_before)
        self.assertFalse((self.out / "train/TriPrismPyramid").exists())
        selected = augmentation.read_records(self.out / f"metadata/sample_params_{CLASS}.json")
        self.assertEqual(len(selected), 4)
        again = self.run_script("--splits", "train")
        self.assertIn("Metadata records to append: 0", again)

    def test_only_selected_training_cloud_is_augmented_with_existing_settings(self):
        old_before = {record["file"]: (self.out / record["file"]).read_bytes()
                      for record in self.old if record["type"] == CLASS}
        self.run_script("--splits", "train", "--holes")
        self.assertEqual((self.out / self.new[0]["file"]).read_text(encoding="utf-8"), "new augmented cloud\n")
        for record in self.new[1:]:
            self.assertFalse((self.out / record["file"]).exists())
        for relative, content in old_before.items():
            self.assertEqual((self.out / relative).read_bytes(), content)
        self.assertFalse((self.out / "train/Cuboid/new.txt").exists())

    def test_selected_class_check_does_not_require_unselected_clouds(self):
        self.run_script()
        result = augmentation.read_records(self.aug_meta)
        self.assertEqual(len(result), len(self.old) + len(self.new))
        self.assertNotIn(self.records[-1], result)
        self.assertFalse((self.out / "train/TriPrismPyramid/old.txt").exists())

    def test_default_classes_skip_fourteenth_class(self):
        self.run_script(classes=None)
        self.assertFalse((self.out / "train/TriPrismPyramid/old.txt").exists())
        self.assertTrue((self.out / "train/Cuboid/new.txt").exists())

    def test_dry_run_changes_nothing(self):
        before = {path.relative_to(self.out): path.read_bytes() for path in self.out.rglob("*") if path.is_file()}
        self.run_script("--splits", "train", "--dry-run")
        after = {path.relative_to(self.out): path.read_bytes() for path in self.out.rglob("*") if path.is_file()}
        self.assertEqual(before, after)

    def test_changed_existing_labels_are_rejected_before_writing(self):
        records = json.loads(json.dumps(self.records))
        records[0]["params"]["height"] = 99
        self.raw_meta.write_text(json.dumps(records), encoding="utf-8")
        before = self.aug_meta.read_bytes()
        with self.assertRaisesRegex(ValueError, "labels changed"):
            self.run_script("--splits", "train")
        self.assertEqual(self.aug_meta.read_bytes(), before)
        self.assertFalse((self.out / self.new[0]["file"]).exists())

    def test_missing_frozen_holdout_is_not_recreated_in_train_only_mode(self):
        path = self.out / self.old[1]["file"]
        path.unlink()
        before = self.aug_meta.read_bytes()
        with self.assertRaisesRegex(RuntimeError, "Selected classes still have"):
            self.run_script("--splits", "train")
        self.assertFalse(path.exists())
        self.assertEqual(self.aug_meta.read_bytes(), before)

    def test_duplicate_and_unsafe_paths_are_rejected(self):
        for record in ({**self.old[0]}, {**self.new[0], "file": "train/TruncatedPyramidRoof/../../escape.txt"}):
            with self.subTest(record=record), self.assertRaises(ValueError):
                augmentation.records_by_path(self.old + [record])


if __name__ == "__main__":
    unittest.main()
