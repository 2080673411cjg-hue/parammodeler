"""Check production LShapedHouse point budgets with stubbed existing geometry helpers."""

import csv
import io
import math
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LHouseSamplingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("g++") or shutil.which("clang++")
        if compiler is None:
            raise unittest.SkipTest("A C++ compiler is required for the sampling block")
        cls.source = (ROOT / "exportpointcloud.cpp").read_text(encoding="utf-8")
        start = cls.source.index('  else if ( primitiveType == QStringLiteral( "LShapedHouse" ) )')
        cls.block = cls.source[start:cls.source.index(
            '  else if ( primitiveType == QStringLiteral( "FlatTopHipRoofHouse" ) )', start)]
        body = cls.block[cls.block.index("{") + 1:cls.block.rfind("}")]
        harness = r"""
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>
template<typename T> using QVector = std::vector<T>;
struct QVector3D { double value; };
struct MeshData { QVector<QVector3D> vertices; QVector<int> indices; };
double triangleArea(const QVector3D &a, const QVector3D &, const QVector3D &) { return a.value; }
int qRound(double value) { return int(std::floor(value + .5)); }
void classifyLHouseTriangles(const MeshData &, QVector<int> &top, QVector<int> &outer, QVector<int> &notch) {
  top = {0}; outer = {1}; notch = {2};
}
QVector<QVector3D> sampleGroup(const MeshData &, const QVector<int> &tris, int count) {
  return QVector<QVector3D>(count, QVector3D{double(tris.front())});
}
QVector<QVector3D> &operator<<(QVector<QVector3D> &a, const QVector<QVector3D> &b) {
  a.insert(a.end(), b.begin(), b.end()); return a;
}
int main() {
  const double shapes[][4] = {{20, 10, .4, .5}, {15, 40, .25, .35},
                              {40, 6, .65, .60}, {40, 40, .50, .50}};
  std::cout << std::setprecision(12);
  for (const auto &shape : shapes) {
    const double innerArea = (shape[0] * shape[2] + shape[1] * (1 - shape[3])) * 5;
    const double outerArea = 2 * (shape[0] + shape[1]) * 5 - innerArea;
    const MeshData mesh{{{1}, {1}, {1}, {outerArea}, {outerArea}, {outerArea},
                         {innerArea}, {innerArea}, {innerArea}}, {0, 1, 2, 3, 4, 5, 6, 7, 8}};
    for (int pointCount : {1, 17, 2048, 8192}) {
      QVector<QVector3D> roofPts, wallPts;
""" + body + r"""
      int top = 0, inner = 0;
      for (const auto &p : roofPts) { if (p.value == 0) ++top; else if (p.value == 2) ++inner; else return 1; }
      for (const auto &p : wallPts) if (p.value != 1) return 2;
      std::cout << pointCount << ',' << top << ',' << inner << ',' << wallPts.size()
                << ',' << outerArea << ',' << innerArea << '\n';
    }
  }
}
"""
        with tempfile.TemporaryDirectory(prefix="lhouse-surface-", dir=ROOT / "tests") as temp:
            cpp, exe = Path(temp) / "sampling.cpp", Path(temp) / "sampling.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-O2", str(cpp), "-o", str(exe)],
                           check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=30)
        cls.rows = [tuple(map(float, row)) for row in csv.reader(io.StringIO(result.stdout))]

    def test_total_counts_and_normal_top_wall_budget(self):
        self.assertEqual(len(self.rows), 16)
        for count, top, inner, outer, _, _ in self.rows:
            walls = int(count * .40)
            self.assertEqual(top, count - walls)
            self.assertEqual(inner + outer, walls)
            self.assertEqual(top + inner + outer, count)

    def test_inner_wall_budget_follows_area_not_a_fixed_quota(self):
        fractions = set()
        for count, _, inner, outer, outer_area, inner_area in self.rows:
            expected = math.floor(int(count * .40) * inner_area / (inner_area + outer_area) + .5)
            self.assertEqual(inner, expected)
            if count == 2048:
                self.assertGreater(inner, 0)
                self.assertGreater(outer, 0)
                self.assertNotEqual(inner, int(count * .24))
                fractions.add(inner)
        self.assertGreater(len(fractions), 1)

    def test_no_forced_edge_points_and_final_resampling_remains(self):
        self.assertNotIn("sampleLHouseNotchEdgePoints", self.source)
        self.assertNotIn("notchEdgeCount", self.block)
        self.assertIn("resampleToFixedCount( combined, pointCount );", self.source)


if __name__ == "__main__":
    unittest.main()
