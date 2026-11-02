"""Execute base LHouse sampling before footprint stratification, with a seeded RNG."""

from collections import Counter
import csv
import io
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LHouseRandomizerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("g++") or shutil.which("clang++")
        if compiler is None:
            raise unittest.SkipTest("A C++ compiler is required for the sampling block")
        source = (ROOT / "parammodeler_randomizer.cpp").read_text(encoding="utf-8")
        helpers = source[source.index("  auto rnd ="):source.index("  const QString prim")]
        start = source.index('  else if ( prim == "LHouse" )')
        block = source[start:source.index('  else if ( prim == "ConeCylinder" )', start)]
        block = block[block.index("{") + 1:block.rfind("}")]
        harness = r"""
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
class QRandomGenerator {
  std::mt19937 engine{42};
public:
  static QRandomGenerator *global() { static QRandomGenerator rng; return &rng; }
  double generateDouble() { return std::generate_canonical<double, 53>(engine); }
  int bounded(int low, int high) { return std::uniform_int_distribution<int>(low, high - 1)(engine); }
};
struct QDoubleSpinBox {
  double value = 0;
  void setValue(double v) { value = v; }
};
struct Controls {
  QDoubleSpinBox values[5];
  QDoubleSpinBox *spinBoxLTotalLength = &values[0], *spinBoxLTotalWidth = &values[1];
  QDoubleSpinBox *spinBoxLWingRatio = &values[2], *spinBoxLWingWidthRatio = &values[3];
  QDoubleSpinBox *spinBoxLHeight = &values[4];
};
struct Dock { Controls *ui; };
int main() {
  Controls ui;
  Dock storage{&ui};
  Dock *dock = &storage;
  std::cout << std::setprecision(10);
""" + helpers + "\n  for (int i = 0; i < 100000; ++i) {\n" + block + r"""
    for (int j = 0; j < 5; ++j) std::cout << ui.values[j].value << (j == 4 ? '\n' : ',');
  }
}
"""
        with tempfile.TemporaryDirectory(prefix="lhouse-sampling-", dir=ROOT / "tests") as temp:
            cpp, exe = Path(temp) / "sampling.cpp", Path(temp) / "sampling.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-O2", str(cpp), "-o", str(exe)],
                           check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=30)
        cls.rows = [tuple(map(float, row)) for row in csv.reader(io.StringIO(result.stdout))]

    def test_ranges_precision_and_missing_area(self):
        self.assertEqual(len(self.rows), 100000)
        for length, width, wing, wing_width, height in self.rows:
            self.assertTrue(15 <= length <= 40 and 6 <= width <= 40)
            self.assertTrue(.25 <= wing <= .65 and .35 <= wing_width <= .60)
            self.assertTrue(2.5 <= height <= 7)
            for value, scale in ((length, 10), (width, 10), (wing, 100), (wing_width, 100), (height, 10)):
                self.assertAlmostEqual(value * scale, round(value * scale), places=8)
            self.assertGreaterEqual(wing * (1 - wing_width), .16 - 1e-12)

    def test_sizes_cover_the_full_grid_without_endpoint_pileups(self):
        for column, low in ((0, 150), (1, 60)):
            counts = Counter(round(row[column] * 10) for row in self.rows)
            self.assertEqual(set(counts), set(range(low, 401)))
            expected = len(self.rows) / len(counts)
            self.assertLess(max(counts.values()), expected * 1.5)
            bands = Counter(min(9, (round(row[column] * 10) - low) * 10 // (401 - low)) for row in self.rows)
            for band, count in bands.items():
                choices = sum(min(9, (value - low) * 10 // (401 - low)) == band for value in counts)
                self.assertLess(abs(count / (expected * choices) - 1), .08)

    def test_ratio_pairs_cover_the_valid_grid_uniformly(self):
        valid = {(a, b) for a in range(25, 66) for b in range(35, 61) if a * (100 - b) >= 1600}
        counts = Counter((round(row[2] * 100), round(row[3] * 100)) for row in self.rows)
        self.assertEqual(set(counts), valid)
        expected = len(self.rows) / len(valid)
        chi_squared = sum((count - expected) ** 2 / expected for count in counts.values())
        self.assertLess(chi_squared / (len(valid) - 1), 1.3)


if __name__ == "__main__":
    unittest.main()
