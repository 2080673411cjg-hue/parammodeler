"""Run production randomizer blocks with spin-box and footprint test doubles."""

from collections import Counter
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
HORIZONTAL = {
    "FlatRoofHouse": "CLength CWidth",
    "RoundFlatRoofHouse": "CylRadius",
    "LShapedHouse": "LTotalLength LTotalWidth",
    "ConicalRoofHouse": "ConeCylRadius",
    "GableRoofHouse": "GRLength GRWidth",
    "PyramidalRoofHouse": "PRLength PRWidth",
    "FlatTopHipRoofHouse": "TPRBottomLength TPRBottomWidth TPRTopLength TPRTopWidth",
    "BarrelRoofHouse": "HCRLength HCRWidth",
    "DomeRoofHouse": "CylHemiRadius",
    "CylinderHemisphere": "CylHemiRadius",
    "RecessedRoofHouse": "ICLength ICWidth ICInnerLength ICInnerWidth",
    "ShortRidgeHipRoofHouse": "AGHLength AGHWidth AGHRidgeLength",
    "CurvedRoofRoundHouse": "FTBaseRadius FTEaveRadius FTMiddleTopRadius",
    "AngledGableHouse": "TGLength1 TGLength2 TGWidth",
    "TriPrismPyramid": "",
}


class ScaleSamplingTests(unittest.TestCase):
    def test_production_blocks_cover_bands_without_changing_other_parameters(self):
        compiler = shutil.which("g++") or shutil.which("clang++")
        if compiler is None:
            self.skipTest("A C++ compiler is required")
        source = (ROOT / "parammodeler_randomizer.cpp").read_text(encoding="utf-8")
        helpers = source[source.index("  auto rnd ="):source.index("  const QString prim")]
        base = source[source.index('  if ( prim == "FlatRoofHouse" )'):source.index("  QVector<QDoubleSpinBox *> horizontalControls;")]
        scaling = source[source.index("  QVector<QDoubleSpinBox *> horizontalControls;"):source.index("  // ---", source.index("horizontalControls;"))]
        names = sorted(set(re.findall(r"dock->ui->(spinBox\w+)", helpers + base + scaling)))
        widgets = {widget.get("name"): widget for widget in ET.parse(ROOT / "parammodeler_dock.ui").iter("widget")}
        decimals = {name: widgets[name].findtext('./property[@name="decimals"]/number', '2') for name in names}
        fields = "\n".join(f"  QDoubleSpinBox {name}Storage, *{name} = &{name}Storage;" for name in names)
        initialize = "\n".join(
            f"  ui.{name}->digits = {decimals[name]};"
            for name in names
        )
        pointers = ", ".join(f"ui.{name}" for name in names)
        cases = "\n".join(
            '    {"' + prim + '", {' + ", ".join(f"ui.spinBox{name}" for name in fields.split()) + "}},"
            for prim, fields in HORIZONTAL.items()
        )
        harness = r"""
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
using QString = std::string;
template<class T> struct QVector : std::vector<T> {
  using std::vector<T>::vector;
  bool isEmpty() const { return this->empty(); }
  T first() const { return this->front(); }
};
struct QVector3D {
  double a, b;
  double x() const { return a; } double y() const { return b; }
  void setX(double v) { a = v; } void setY(double v) { b = v; }
};
struct MeshData { QVector<QVector3D> vertices; bool isEmpty() const { return vertices.isEmpty(); } };
struct QDoubleSpinBox {
  double v = 0; int digits = 2;
  double value() const { return v; }
  void setValue(double n) { const double s = std::pow(10, digits); v = std::round(n * s) / s; }
};
class QRandomGenerator {
  std::mt19937 engine{42};
public:
  static QRandomGenerator *global() { static QRandomGenerator rng; return &rng; }
  double generateDouble() { return std::generate_canonical<double, 53>(engine); }
  int bounded(int low, int high) { return std::uniform_int_distribution<int>(low, high - 1)(engine); }
  int bounded(int high) { return bounded(0, high); }
};
struct Controls {
""" + fields + r"""
};
struct Dock { Controls *ui; };
struct BuildMesh {
  static MeshData build(const QString &prim, Dock *dock) {
    auto &u = *dock->ui;
    double x = 0, y = 0;
    if (prim == "FlatRoofHouse") { x = u.spinBoxCLength->value(); y = u.spinBoxCWidth->value(); }
    if (prim == "LShapedHouse") { x = u.spinBoxLTotalLength->value(); y = u.spinBoxLTotalWidth->value(); }
    if (prim == "GableRoofHouse") { x = u.spinBoxGRLength->value(); y = u.spinBoxGRWidth->value(); }
    if (prim == "PyramidalRoofHouse") { x = u.spinBoxPRLength->value(); y = u.spinBoxPRWidth->value(); }
    if (prim == "FlatTopHipRoofHouse") { x = u.spinBoxTPRBottomLength->value(); y = u.spinBoxTPRBottomWidth->value(); }
    if (prim == "BarrelRoofHouse") { x = u.spinBoxHCRLength->value(); y = u.spinBoxHCRWidth->value(); }
    if (prim == "RecessedRoofHouse") { x = u.spinBoxICLength->value(); y = u.spinBoxICWidth->value(); }
    if (prim == "ShortRidgeHipRoofHouse") { x = u.spinBoxAGHLength->value(); y = u.spinBoxAGHWidth->value(); }
    if (prim == "RoundFlatRoofHouse") x = y = 2 * u.spinBoxCylRadius->value();
    if (prim == "ConicalRoofHouse") x = y = 2 * u.spinBoxConeCylRadius->value();
    if (prim == "DomeRoofHouse" || prim == "CylinderHemisphere") x = y = 2 * u.spinBoxCylHemiRadius->value();
    if (prim == "CurvedRoofRoundHouse") x = y = 2 * u.spinBoxFTEaveRadius->value();
    if (prim == "AngledGableHouse") {
      const double turn = (180 - u.spinBoxTGAngle->value()) * std::acos(-1) / 180;
      x = u.spinBoxTGLength1->value() + u.spinBoxTGLength2->value() * std::cos(turn);
      y = u.spinBoxTGWidth->value() + u.spinBoxTGLength2->value() * std::sin(turn);
    }
    return MeshData{{{0, 0}, {x, y}}};
  }
};
void sampleBase(Dock *dock, const QString &prim) {
""" + helpers + base + "\n}\nvoid sampleScale(Dock *dock, const QString &prim, int scaleBand) {\n" + helpers + scaling + r"""
}
void require(bool condition, const QString &message) { if (!condition) throw std::runtime_error(message); }
int main() {
  Controls ui;
""" + initialize + "\n  Dock storage{&ui};\n  Dock *dock = &storage;\n  std::vector<QDoubleSpinBox *> all{" + pointers + r"""};
  struct Case { QString prim; std::vector<QDoubleSpinBox *> horizontal; };
  const std::vector<Case> cases{
""" + cases + r"""
  };
  for (const auto &c : cases) {
    for (int requested = -1; requested < 3; ++requested) {
      int counts[3] = {};
      int spread[3] = {};
      for (int i = 0; i < 900; ++i) {
        sampleBase(dock, c.prim);
        std::vector<double> before;
        for (auto *spin : all) before.push_back(spin->value());
        const auto oldMesh = BuildMesh::build(c.prim, dock);
        const double oldSpan = std::max(oldMesh.vertices[1].x(), oldMesh.vertices[1].y());
        sampleScale(dock, c.prim, requested);
        const auto mesh = BuildMesh::build(c.prim, dock);
        const double span = std::max(mesh.vertices[1].x(), mesh.vertices[1].y());
        const int band = span < 15 ? 0 : span < 25 ? 1 : 2;
        if (!c.horizontal.empty()) {
          require(span >= 6 && span <= 40, c.prim + " outside scale range");
          require(requested < 0 || band == requested, c.prim + " in wrong band");
          ++counts[band];
          if (requested >= 0) {
            const double bounds[] = {6, 15, 25, 40};
            const int part = std::min(2, int(3 * (span - bounds[band]) / (bounds[band + 1] - bounds[band])));
            ++spread[part];
          }
        }
        for (size_t j = 0; j < all.size(); ++j) {
          const bool horizontal = std::find(c.horizontal.begin(), c.horizontal.end(), all[j]) != c.horizontal.end();
          if (horizontal) {
            require(std::abs(all[j]->value() - before[j] * span / oldSpan) < .025,
                    c.prim + " distorted a horizontal proportion");
            require(all[j]->value() > 0 && all[j]->value() < 50, c.prim + " hit input bounds");
          } else {
            require(all[j]->value() == before[j], c.prim + " changed a non-horizontal parameter");
          }
        }
      }
      if (requested < 0 && !c.horizontal.empty())
        for (int count : counts) require(count > 240 && count < 360, c.prim + " random band imbalance");
      if (requested >= 0 && !c.horizontal.empty())
        for (int count : spread) require(count > 225 && count < 375, c.prim + " concentrated within a band");
    }
  }
  std::cout << "54000 randomizer cases passed\n";
}
"""
        with tempfile.TemporaryDirectory(prefix="scale-sampling-", dir=ROOT / "tests") as temp:
            cpp, exe = Path(temp) / "sampling.cpp", Path(temp) / "sampling.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-O2", str(cpp), "-o", str(exe)],
                           check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=30)
        self.assertIn("54000 randomizer cases passed", result.stdout)

    def test_batch_rotation_balances_splits_and_records_the_band(self):
        source = (ROOT / "parammodeler_datasetgen.cpp").read_text(encoding="utf-8")
        self.assertIn("const int scaleBand = ( fileIdx - 1 ) % 3;", source)
        self.assertIn("const int scaleBand = i % 3;", source)
        self.assertEqual(source.count("randomizeCurrentPrimitiveParams( false, true, scaleBand )"), 2)
        self.assertEqual(source.count('item[QStringLiteral( "horizontalScaleBand" )] = scaleBand;'), 2)
        self.assertEqual(source.count('item[QStringLiteral( "scaleSamplingVersion" )]'), 2)
        for start, count in ((0, 800), (800, 100), (900, 100), (0, 1000), (587, 213)):
            bands = Counter((start + i) % 3 for i in range(count))
            self.assertLessEqual(max(bands.values()) - min(bands.values()), 1)


if __name__ == "__main__":
    unittest.main()
