"""Validate precision, unbiased frozen selection, resume and classifier provenance."""

import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("real_screening", ROOT / "scripts/screen_real_clouds.py")
screening = importlib.util.module_from_spec(spec)
spec.loader.exec_module(screening)
sys.path.pop(0)


class ScreeningTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "tests")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def test_large_coordinates_keep_sub_float32_grid_detail(self):
        xyz = np.array([[538089.53, 6584200.03, 33.73], [538089.55, 6584200.05, 33.74],
                        [538089.57, 6584200.07, 33.76]], dtype=np.float64)
        text = io.StringIO(); np.savetxt(text, xyz, fmt="%.4f")
        local, stats = screening.prepare_xyz(text.getvalue().encode())
        np.testing.assert_allclose(local + stats["origin"], xyz, atol=1e-9, rtol=0)
        self.assertEqual(len(np.unique(local.astype(np.float32), axis=0)), 3)
        self.assertGreater(np.max(np.abs(xyz.astype(np.float32).astype(float) - xyz)), .01)
        self.assertAlmostEqual(float(np.ptp(local[:, 1])), .04, places=7)

    def test_exact_duplicates_invalid_xyz_and_extra_columns(self):
        local, stats = screening.prepare_xyz(b"1 2 3 4 5 6 7 8\n1 2 3 9 9 9 9 9\n2 4 6 1 1 1 1 1\nnan 3 4 0 0 0 0 0\n")
        self.assertEqual(local.shape, (2, 3))
        self.assertEqual(stats["original_points"], 4)
        self.assertEqual(stats["duplicate_points"], 1)
        self.assertEqual(stats["invalid_points"], 1)
        np.testing.assert_allclose(stats["bbox_span"], [1, 2, 3])
        self.assertTrue(any("2048" in flag for flag in stats["flags"]))

    def test_frozen_random_selection_and_changed_settings_rejected(self):
        source, output = self.directory / "source", self.directory / "gallery"
        source.mkdir(); output.mkdir()
        for i in range(40):
            (source / f"{i}.xyz").write_text("0 0 0\n1 1 1\n")
        first = screening.select_pilot(source, output, 20, 42)
        self.assertEqual(len(first["files"]), 20)
        self.assertEqual(len(set(first["files"])), 20)
        (source / "new.xyz").write_text("0 0 0\n")
        self.assertEqual(screening.select_pilot(source, output, 20, 42), first)
        with self.assertRaisesRegex(ValueError, "different selection"):
            screening.select_pilot(source, output, 20, 43)

    def test_source_unchanged_and_resume_reuses_preparation(self):
        source, output = self.directory / "source", self.directory / "gallery"
        source.mkdir()
        original = b"538089.53 6584200.03 33.73\n538089.55 6584200.05 34.74\n"
        file = source / "1.xyz"; file.write_bytes(original)
        args = SimpleNamespace(input=str(source), output=str(output), count=1, seed=42, server=None)
        with patch.object(screening, "render_views", side_effect=lambda points, path: path.write_bytes(b"test image")) as render:
            self.assertEqual(screening.run(args), 0)
            first = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(screening.run(args), 0)
            self.assertEqual(render.call_count, 1)
        self.assertEqual(file.read_bytes(), original)
        self.assertEqual(first["records"][0]["sha256"], hashlib.sha256(original).hexdigest())
        local = np.loadtxt(first["records"][0]["input_path"])
        self.assertAlmostEqual(float(local[1, 1]-local[0, 1]), .02, places=7)
        # Changing a source invalidates both its preparation and any cached classifier result.
        file.write_bytes(original + b"538090.00 6584201.00 35.00\n")
        with patch.object(screening, "render_views", side_effect=lambda points, path: path.write_bytes(b"new image")) as render:
            screening.run(args)
            self.assertEqual(render.call_count, 1)
        changed = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
        self.assertNotEqual(first["records"][0]["review_id"], changed["records"][0]["review_id"])

    def test_output_cannot_be_inside_source(self):
        with self.assertRaisesRegex(ValueError, "outside the source"):
            screening.run(SimpleNamespace(input=str(self.directory), output=str(self.directory / "nested")))

    def test_connection_failure_resumes_without_reclassifying_successful_samples(self):
        source, output = self.directory / "source", self.directory / "gallery"
        source.mkdir()
        for name in ("1.xyz", "2.xyz"):
            (source / name).write_text("0 0 0\n1 1 1\n")
        args = SimpleNamespace(input=str(source), output=str(output), count=2, seed=42, server="http://example.test")
        model = "logs/ptv3_cls_v10"
        result = {"status": "ok", "model_dir": model, "predictions": [{"class": "GableRoofHouse", "prob": .9}]}
        with patch.object(screening, "render_views", side_effect=lambda points, path: path.write_bytes(b"image")), \
             patch.object(screening, "RemoteClassifier") as constructor:
            constructor.return_value.model_dir = model
            constructor.return_value.classify.side_effect = [result, screening.urllib.error.URLError("network down")]
            self.assertEqual(screening.run(args), 2)
            records = json.loads((output / "manifest.json").read_text(encoding="utf-8"))["records"]
            self.assertEqual([r["classification"]["status"] for r in records], ["ok", "error"])
            constructor.return_value.classify.reset_mock(side_effect=True)
            constructor.return_value.classify.return_value = result
            self.assertEqual(screening.run(args), 0)
            self.assertEqual(constructor.return_value.classify.call_count, 1)

    def test_partial_render_failure_is_not_ready_or_reused(self):
        source, output = self.directory / "source", self.directory / "gallery"
        source.mkdir()
        (source / "1.xyz").write_text("0 0 0\n1 1 1\n")
        args = SimpleNamespace(input=str(source), output=str(output), count=1, seed=42, server=None)
        def fail_render(points, path):
            path.write_bytes(b"partial image")
            raise OSError("interrupted image write")
        with patch.object(screening, "render_views", side_effect=fail_render):
            self.assertEqual(screening.run(args), 1)
        failed = json.loads((output / "manifest.json").read_text(encoding="utf-8"))["records"][0]
        self.assertEqual(failed["status"], "error")
        with patch.object(screening, "render_views", side_effect=lambda points, path: path.write_bytes(b"fixed")) as render:
            self.assertEqual(screening.run(args), 0)
            self.assertEqual(render.call_count, 1)

    def test_gallery_embedded_data_cannot_close_script(self):
        selection = {"settings": {"source_root": "test"}, "files": [], "available_files": 0}
        screening.render_gallery(self.directory, selection, [{"error": "</script><script>bad()</script>"}])
        page = (self.directory / "index.html").read_text(encoding="utf-8")
        self.assertNotIn("<script>bad()", page)
        self.assertIn("\\u003c/script>", page)

    def test_classifier_requires_v10_and_result_version(self):
        class Response(io.BytesIO):
            def __init__(self, data):
                super().__init__(json.dumps(data).encode())
        model = "/home/xubo/pointnet/ptv3_simple/logs/ptv3_cls_v10"
        good = {"ok": True, "model_log_dir": model, "result": [{"class": "GabledRoof", "prob": .9}]}
        input_file = self.directory / "input.xyz"; input_file.write_text("0 0 0\n1 1 1\n")
        for response, accepted in ((good, True), ({**good, "model_log_dir": "old"}, False),
                                   ({**good, "result": [{"class": "Unknown", "prob": .9}]}, False)):
            with self.subTest(response=response), patch.object(screening.urllib.request, "build_opener") as builder:
                builder.return_value.open.side_effect = [Response({"cls_log": model}), Response(response)]
                classifier = screening.RemoteClassifier("http://example.test")
                if accepted:
                    actual = classifier.classify(input_file)
                    self.assertEqual(actual["predictions"][0]["class"], "GableRoofHouse")
                else:
                    with self.assertRaises(ValueError):
                        classifier.classify(input_file)
        with patch.object(screening.urllib.request, "build_opener") as builder:
            builder.return_value.open.return_value = Response({"cls_log": "logs/ptv3_cls_v9"})
            with self.assertRaisesRegex(ValueError, "Expected ptv3_cls_v10"):
                screening.RemoteClassifier("http://example.test")


if __name__ == "__main__":
    unittest.main()
