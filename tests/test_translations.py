"""Run with the qgis_dev Python environment; no running QGIS is required."""
import os
import re
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PyQt5 import QtCore, QtWidgets, uic

ROOT = Path(__file__).resolve().parents[1]
QT_BIN = Path(os.environ.get("PARAMMODELER_QT_BIN", "E:/mambaforge/envs/qgis_dev/Library/bin"))
PRIMITIVES = ["Cuboid", "Cylinder", "LHouse", "ConeCylinder", "GabledRoof",
              "PyramidRoof", "TruncatedPyramidRoof", "HalfCylinderRoof", "CylinderDome",
              "IndentedCuboid", "AsymmetricGableHouse", "FourStageRoundTower",
              "TwoGableHouses", "TriPrismPyramid"]


class TranslationsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.temp = tempfile.TemporaryDirectory(prefix="parammodeler-i18n-")
        cls.resource = str(Path(cls.temp.name) / "plugin.rcc")
        subprocess.run([str(QT_BIN / "rcc.exe"), "-binary", str(ROOT / "parammodeler.qrc"),
                        "-o", cls.resource], check=True)
        if not QtCore.QResource.registerResource(cls.resource):
            raise AssertionError("Cannot register plugin resources")

    @classmethod
    def tearDownClass(cls):
        QtCore.QResource.unregisterResource(cls.resource)
        cls.temp.cleanup()

    def setUp(self):
        self.translator = QtCore.QTranslator()
        self.assertTrue(self.translator.load(":/parammodeler/i18n/parammodeler_zh_CN.qm"))

    def tearDown(self):
        self.app.removeTranslator(self.translator)

    def test_all_messages_finished_and_placeholders_preserved(self):
        tree = ET.parse(ROOT / "i18n/parammodeler_zh_CN.ts")
        for context in tree.findall("context"):
            for message in context.findall("message"):
                source = message.findtext("source")
                translated = message.find("translation")
                with self.subTest(context=context.findtext("name"), source=source):
                    self.assertNotIn(translated.get("type"), ("unfinished", "obsolete", "vanished"))
                    self.assertTrue(translated.text)
                    self.assertEqual(sorted(re.findall(r"%L?\d+|%n", source)),
                                     sorted(re.findall(r"%L?\d+|%n", translated.text)))
                    self.assertEqual(re.findall(r"\*\.\w+", source),
                                     re.findall(r"\*\.\w+", translated.text))
                    self.assertEqual(self.translator.translate(context.findtext("name"), source.encode("utf-8")), translated.text)

    def test_chinese_ui_and_internal_primitive_ids(self):
        self.app.installTranslator(self.translator)
        dock = uic.loadUi(str(ROOT / "parammodeler_dock.ui"))
        try:
            self.assertEqual([a.text() for a in dock.menuBar.actions()], ["场景", "导出", "数据集"])
            self.assertEqual(dock.actEvaluationCSV.text(), "当前评估 CSV...")
            self.assertEqual(dock.actCompleteSelectedDLDatasets.text(), "补齐所选基元数据集...")
            self.assertEqual(dock.labelGRRidgeRatio.text(), "屋脊比例：")
            self.assertEqual(dock.spinBoxGRRidgeRatio.value(), .5)
            self.assertEqual(dock.spinBoxGRRidgeRatio.minimum(), .2)
            self.assertEqual(dock.spinBoxGRRidgeRatio.maximum(), .8)
            self.assertEqual(dock.labelFTMiddleRise.text(), "中段隆起高度（米）：")
            self.assertEqual(dock.spinBoxFTMiddleRise.decimals(), 3)
            self.assertEqual([dock.comboPrimitive.itemText(i) for i in range(dock.comboPrimitive.count())], PRIMITIVES)
        finally:
            dock.close()
            dock.deleteLater()

    def test_fit_residual_label_fits_three_lines(self):
        source = "Fit RMSE: %1\nP95: %2\nScene units"
        for chinese in (False, True):
            if chinese:
                self.app.installTranslator(self.translator)
            text = QtCore.QCoreApplication.translate("ParamModelerDock", source)
            label = QtWidgets.QLabel(text.replace("%1", "123.456").replace("%2", "234.567"))
            label.setWordWrap(True)
            label.setContentsMargins(6, 2, 6, 2)
            height = label.fontMetrics().lineSpacing() * 3 + 8
            label.setFixedHeight(height)
            for width in (320, 640):
                self.assertLessEqual(label.heightForWidth(width), height)
            if chinese:
                self.app.removeTranslator(self.translator)

    def test_english_fallback(self):
        dock = uic.loadUi(str(ROOT / "parammodeler_dock.ui"))
        try:
            self.assertEqual([a.text() for a in dock.menuBar.actions()], ["Scene", "Export", "Dataset"])
            self.assertEqual(dock.actCompleteSelectedDLDatasets.text(), "Complete selected primitive datasets...")
            self.assertEqual([dock.comboPrimitive.itemText(i) for i in range(dock.comboPrimitive.count())], PRIMITIVES)
        finally:
            dock.close()
            dock.deleteLater()

    def test_dynamic_tooltips(self):
        self.app.installTranslator(self.translator)
        self.assertEqual(QtCore.QCoreApplication.translate("ParamModelerPick3D", "No point-cloud point within 12 pixels. Zoom in and click directly on a visible point."),
                         "鼠标 12 像素范围内没有点云点。请放大后直接点击可见点。")
        self.assertEqual(QtCore.QCoreApplication.translate("ParamModelerHeightControl", "Height unchanged: translation limit reached."),
                         "已达平移范围限制，高度未改变。")
        for source, translated in (("Train", "训练"), ("Validation", "验证"), ("Test", "测试")):
            self.assertEqual(QtCore.QCoreApplication.translate("QObject", source), translated)
        self.assertEqual(
            QtCore.QCoreApplication.translate(
                "QObject", "%1 has no records in the selected dataset. Select a dataset containing this primitive."
            ),
            "所选数据集中没有 %1 的记录。请选择包含该基元的数据集。",
        )
        self.assertEqual(
            QtCore.QCoreApplication.translate("QObject", "Additional samples for %1:"),
            "%1 新增样本数：",
        )
        self.assertIn(
            "旧文件将被删除",
            QtCore.QCoreApplication.translate(
                "QObject",
                "Replace %1 in %2? Existing files for this primitive will be deleted.\n"
                "Existing: train=%3  val=%4  test=%5.\nNew samples: %6.",
            ),
        )

    def test_internal_primitive_items_are_not_translatable(self):
        tree = ET.parse(ROOT / "parammodeler_dock.ui")
        items = tree.findall(".//widget[@name='comboPrimitive']/item/property/string")
        self.assertEqual([item.text for item in items], PRIMITIVES)
        self.assertTrue(all(item.get("notr") == "true" for item in items))


if __name__ == "__main__":
    unittest.main()
