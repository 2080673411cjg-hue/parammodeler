"""Verify that all exported primitives share the camera visibility step."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "exportpointcloud.cpp").read_text(encoding="utf-8")


class LHouseSamplingTests(unittest.TestCase):
    def test_export_filters_all_point_groups_before_pose(self):
        start = SOURCE.index("bool ExportPointCloud::exportOccludedTXT(")
        export = SOURCE[start:]
        call = "applyCameraVisibility( mesh, roofPts, roofSamples, wallPts, wallSamples, recessPts );"
        self.assertEqual(export.count(call), 1)
        self.assertLess(export.index(call), export.index("removeRoofPatch("))
        self.assertLess(export.index(call), export.index("applyPose("))
        self.assertIn("combined.size() > pointCount", export)

    def test_lhouse_uses_the_flat_roof_sampling_budget(self):
        start = SOURCE.index("bool ExportPointCloud::exportOccludedTXT(")
        export = SOURCE[start:]
        self.assertNotIn('"LShapedHouse"', export)
        self.assertIn("int( pointCount * 0.40 )", export)
        self.assertIn("roofPts = sampleGroup( mesh, horzTris, horzCount );", export)


if __name__ == "__main__":
    unittest.main()
