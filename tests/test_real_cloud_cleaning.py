"""Conservative isolation must preserve walls, valid components and coordinates."""

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import clean_real_clouds as cleaning
sys.path.pop(0)


class CleaningTests(unittest.TestCase):
    def setUp(self):
        x, y = np.meshgrid(np.linspace(0, 3, 30), np.linspace(0, 3, 30))
        self.roof = np.c_[x.ravel(), y.ravel(), np.full(x.size, 2.)]

    def test_extreme_singleton_is_separated_but_sparse_walls_and_attachment_are_kept(self):
        wall = np.c_[np.zeros(12), np.linspace(0, 3, 12), np.full(12, .5)]
        attachment = np.array([[1.5, 1.5, 3.7]])
        points = np.vstack((self.roof, wall, attachment, [50, 50, 50]))
        removed, review, audit = cleaning.isolation_masks(points)
        np.testing.assert_array_equal(np.flatnonzero(removed), [len(points)-1])
        self.assertFalse(removed[len(self.roof):-1].any())
        self.assertTrue(review[-2])
        self.assertFalse(audit["automatic_removal_held"])

    def test_all_large_components_are_kept_not_only_the_largest(self):
        points = np.vstack((self.roof, self.roof[:400] + [30, 30, 0]))
        removed, _, _ = cleaning.isolation_masks(points)
        self.assertFalse(removed.any())

    def test_dense_high_attachment_is_reviewed_without_automatic_semantic_removal(self):
        attachment=self.roof[:6] * [.1,.1,1] + [1,1,1]
        removed,review,_=cleaning.isolation_masks(np.vstack((self.roof,attachment)))
        self.assertFalse(removed.any())
        self.assertTrue(review[-len(attachment):].all())

    def test_excessive_removal_is_held_for_manual_review(self):
        outliers = np.c_[50+20*np.arange(10), np.full(10, 50), np.full(10, 30)]
        removed, review, audit = cleaning.isolation_masks(np.vstack((self.roof, outliers)))
        self.assertTrue(audit["automatic_removal_held"])
        self.assertFalse(removed.any())
        self.assertTrue(review[-10:].all())

    def test_rigid_coordinates_and_unit_scale_do_not_change_masks(self):
        points = np.vstack((self.roof, [50, 50, 50]))
        expected = cleaning.isolation_masks(points)[:2]
        theta = .73
        rotation = np.array([[np.cos(theta),-np.sin(theta),0], [np.sin(theta),np.cos(theta),0], [0,0,1]])
        for scale in (.001, 1, 1000):
            actual = cleaning.isolation_masks((points @ rotation.T)*scale+[100,-200,20])[:2]
            for a,b in zip(actual,expected):
                np.testing.assert_array_equal(a,b)

    def test_too_sparse_or_invalid_points_are_not_silently_deleted(self):
        removed, review, _ = cleaning.isolation_masks(np.array([[0,0,0],[5,5,5]]))
        self.assertFalse(removed.any());self.assertTrue(review.all())
        with self.assertRaises(ValueError):
            cleaning.isolation_masks(np.array([[0,np.nan,1]]))

    def test_cumulative_exports_deduplicate_and_outputs_are_reversible(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tests") as temp:
            root=Path(temp);source=root/"source.xyz"
            points=np.vstack((self.roof,[50,50,50]))
            np.savetxt(source,points,fmt="%.8f");original=source.read_bytes()
            record={"id":"one","name":"7541.xyz","input_path":str(source),"source_path":"original.xyz",
                    "sha256":"original-source-hash","origin":[500000,6500000,30],
                    "human_review":{"state":"selected","human_class":"GableRoofHouse","quality":"facades_missing"}}
            exports=[]
            for number in range(2):
                path=root/f"export{number}.json"
                path.write_text(json.dumps({"source_root":"source","exported_utc":f"2026-11-06T10:0{number}:00Z","records":[record]}),encoding="utf-8")
                exports.append(str(path))
            records,_=cleaning.load_selections(exports);self.assertEqual(len(records),1)
            with patch.object(cleaning,"render_comparison",side_effect=lambda points,removed,review,path:path.write_bytes(b"image")):
                manifest=cleaning.run(exports,root/"output")
            row=manifest["records"][0]
            self.assertEqual(source.read_bytes(),original)
            self.assertEqual(Path(row["files"]["original"]).read_bytes(),original)
            cleaned=np.loadtxt(row["files"]["cleaned"])
            separated=np.loadtxt(row["files"]["separated"],ndmin=2)
            self.assertEqual(len(cleaned)+len(separated),len(points))
            np.testing.assert_allclose(cleaned,points[:-1],rtol=0,atol=5.1e-9)
            self.assertEqual(row["separated_input_rows_zero_based"],[len(points)-1])
            with self.assertRaises(FileExistsError):
                cleaning.run(exports,root/"output")


if __name__ == "__main__":
    unittest.main()
