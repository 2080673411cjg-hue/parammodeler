"""Class renaming preserves old model indices, geometry and versioned paths."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

SCRIPTS = Path(__file__).resolve().parents[1] / 'scripts'
sys.path.insert(0, str(SCRIPTS))
from primitive_names import PRIMITIVES, canonical_class_name, class_directory, regression_slug


class PrimitiveNamesTests(unittest.TestCase):
    def test_old_checkpoint_order_is_preserved(self):
        old = ['TwoGableHouses', 'Cuboid', 'AsymmetricGableHouse']
        self.assertEqual([canonical_class_name(n) for n in old],
                         ['AngledGableHouse', 'FlatRoofHouse', 'ShortRidgeHipRoofHouse'])

    def test_cpp_python_names_and_ui_order_match(self):
        root = SCRIPTS.parent
        header = (root / 'parammodeler_primitive_names.h').read_text(encoding='utf-8')
        for old, new, old_slug, new_slug, _ in PRIMITIVES:
            self.assertIn(f'{{ "{old}", "{new}", "{old_slug}", "{new_slug}" }}', header)
        tree = ET.parse(root / 'parammodeler_dock.ui')
        items = tree.findall(".//widget[@name='comboPrimitive']/item/property/string")
        self.assertEqual([item.text for item in items][:13], [r[1] for r in PRIMITIVES])

    def test_all_v9_and_v10_directory_names(self):
        for old, new, old_slug, new_slug, _ in PRIMITIVES:
            for alias in (old, new):
                self.assertEqual(regression_slug(alias, '_v9_normals'), old_slug)
                self.assertEqual(regression_slug(alias, '_v10_normals'), new_slug)
        self.assertEqual(canonical_class_name('CylinderHemisphere'), 'DomeRoofHouse')

    def test_legacy_data_folder_and_duplicate_alias_guard(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            old = root / 'test' / 'Cuboid'
            old.mkdir(parents=True)
            self.assertEqual(class_directory(root, 'test', 'FlatRoofHouse'), old)
            (root / 'test' / 'FlatRoofHouse').mkdir()
            with self.assertRaisesRegex(ValueError, 'Mixed old and renamed'):
                class_directory(root, 'test', 'FlatRoofHouse')

    def test_legacy_metadata_uses_same_canonical_target_frame(self):
        spec = importlib.util.spec_from_file_location('rename_canonical', SCRIPTS / 'canonical_orientation.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        record = {'params': {'rz': 120, 'length': 20, 'width': 10}}
        for old, new in [('GabledRoof', 'GableRoofHouse'), ('AsymmetricGableHouse', 'ShortRidgeHipRoofHouse')]:
            self.assertEqual(module.canonical_angle_from_metadata(record, old),
                             module.canonical_angle_from_metadata(record, new))


if __name__ == '__main__':
    unittest.main()
