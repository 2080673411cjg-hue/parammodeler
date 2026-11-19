"""Persistent cache invalidation, native PCL features and checkpoint compatibility."""

import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np
import torch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
with patch.dict(sys.modules, {'model_ptv3': SimpleNamespace(PTv3Regressor=object, PTv3Classifier=object)}):
    import main_reg as regression
    import main as classification
    from pcl_normals.__main__ import main as prepare_cache
from pcl_normals import NormalCache, PCLNormals, rotate_normals_z
sys.path.pop(0)


class CacheTests(unittest.TestCase):
    def test_cache_reuses_and_invalidates_coordinates_algorithm_version_and_corruption(self):
        class Calculator:
            version = 'test'
            calls = 0

            def __call__(self, points, threads):
                self.calls += 1
                return np.ones_like(points), None, 0

        with tempfile.TemporaryDirectory(dir=ROOT / 'tests') as folder:
            source = Path(folder) / 'train/FlatRoofHouse/a.txt'
            calc = Calculator()
            points = np.zeros((8, 3), np.float32)
            cache = NormalCache(folder, calc)
            a = cache.get(source, points)
            np.testing.assert_array_equal(cache.get(source, points), a)
            self.assertEqual((cache.hits, calc.calls), (1, 1))
            changed = points.copy()
            changed[0, 0] = .1
            cache.get(source, changed)
            self.assertEqual(calc.calls, 2)
            calc.version = 'changed'
            cache.get(source, changed)
            self.assertEqual(calc.calls, 3)
            cache.algorithm = 'changed-algorithm'
            cache.get(source, changed)
            self.assertEqual(calc.calls, 4)
            entry = next(cache.root.rglob('*.npz'))
            entry.write_bytes(b'interrupted write')
            cache.get(source, changed)
            self.assertEqual(calc.calls, 5)
            with np.load(entry, allow_pickle=False) as saved:
                self.assertEqual(str(saved['algorithm']), 'changed-algorithm')

    def test_old_checkpoint_cannot_be_silently_switched_by_cli(self):
        args = SimpleNamespace(normal_backend='pcl', normal_threads=2)
        self.assertEqual(regression.normal_kwargs(args, {})['normal_backend'], 'numpy')
        points = np.random.default_rng(1).normal(size=(24, 3)).astype(np.float32)
        expected = regression.estimate_normals(points)
        actual = regression.predict_normals(points + 50, points, args, {'use_normals': True})
        np.testing.assert_array_equal(actual, expected)
        self.assertIsNone(regression.predict_normals(points, points, args, {'use_normals': False}))
        with self.assertRaisesRegex(ValueError, 'Unsupported'):
            regression.normal_kwargs(args, {'normal_backend': 'unknown'})

    def test_rotation_changes_direction_without_translating_normals(self):
        normals = np.array([[1, 0, 0], [1, 0, 0]], np.float32)
        result = rotate_normals_z(normals, 90)
        np.testing.assert_allclose(result, [[0, 1, 0], [0, 1, 0]], atol=1e-7)
        np.testing.assert_array_equal(normals, [[1, 0, 0], [1, 0, 0]])


LIBRARY = Path(os.environ.get('PTV3_PCL_LIBRARY', ROOT / 'evaluations/pcl_normals_20261030/pcl_normals.dll'))


@unittest.skipUnless(LIBRARY.is_file(), 'Native PCL library not available')
class NativePipelineTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / 'tests')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        env = {'PTV3_PCL_LIBRARY': str(LIBRARY)}
        if os.name == 'nt':
            env['PTV3_PCL_DLL_DIR'] = 'E:/mambaforge/envs/polyfit_env/Library/bin'
        self.environment = patch.dict(os.environ, env)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.engine = PCLNormals()
        self.records = []
        for split, count in (('train', 2), ('val', 1), ('test', 1)):
            for i in range(count):
                relative = f'{split}/GableRoofHouse/sample_{i}.txt'
                path = self.root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                points = np.random.default_rng(i).normal(size=(48, 3)).astype(np.float32)
                # Additional semantic columns must not be interpreted as normals.
                np.savetxt(path, np.column_stack((points, np.ones(len(points)))))
                self.records.append({'type': 'GableRoofHouse', 'split': split, 'file': relative,
                    'params': {'length': 12, 'width': 10, 'totalHeight': 8,
                               'wallRatio': .6, 'ridgeRatio': .3, 'rz': 37},
                    'pointCloudInfo': {'bboxSize': [12, 10, 8], 'scale': 7}})
        (self.root / 'metadata').mkdir()
        self.metadata = self.root / 'metadata/sample_params.json'
        self.metadata.write_text(json.dumps(self.records), encoding='utf-8')

    def test_offline_cache_classifier_canonical_regression_and_inference_agree(self):
        hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.root.rglob('*.txt')}
        with patch.object(sys, 'argv', ['pcl_normals', '--data_root', str(self.root)]), \
             contextlib.redirect_stdout(io.StringIO()) as stream:
            prepare_cache()
        self.assertIn('hit=0 computed=4', stream.getvalue())
        with contextlib.redirect_stdout(io.StringIO()) as stream:
            cls = classification.PointCloudClassificationDataset(self.root, 'train', ['GableRoofHouse'], 32,
                use_normals=True, normal_backend='pcl', normal_workers=2)
            ds = regression.ParamRegressionDataset(self.root, self.metadata, 'GableRoofHouse', 'train',
                ['length', 'width'], 32, use_normals=True, normal_backend='pcl', canonical_align=True)
        self.assertEqual(stream.getvalue().count('hit=2 computed=0'), 2)
        self.assertIn('workers=0', stream.getvalue())
        for path, _, _, _, theta in ds.samples:
            raw = regression.load_txt_points(path)
            base = regression.normalize_points(raw)
            direct, _, invalid = self.engine(base, threads=1)
            self.assertEqual(invalid, 0)
            np.testing.assert_array_equal(cls._normals_cache[str(path)], direct)
            rotated = rotate_normals_z(direct, -theta)
            np.testing.assert_array_equal(ds._normals_cache[str(path)], rotated)
            actual = regression.predict_normals(raw, ds._points_cache[str(path)], SimpleNamespace(),
                {'use_normals': True, 'normal_backend': 'pcl'}, -theta)
            np.testing.assert_array_equal(actual, rotated)
            self.assertFalse(ds._normals_cache[str(path)].flags.writeable)
            # Sample indices and rotations must be shared by all six feature channels.
            np.random.seed(17)
            sampled = regression.sample_points(base, 24, True, 0., True, direct)
            np.random.seed(17)
            indices = np.random.choice(len(base), 24, replace=False)
            angle = np.degrees(np.random.uniform(0, 2 * np.pi))
            np.testing.assert_allclose(sampled[:, 3:], rotate_normals_z(direct[indices], angle), atol=1e-7)
        self.assertEqual(hashes, {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in hashes})
        path = self.root / self.records[0]['file']
        raw = regression.load_txt_points(path)
        raw[0, 0] += .2
        np.savetxt(path, raw)
        with patch.object(sys, 'argv', ['pcl_normals', '--data_root', str(self.root)]), \
             contextlib.redirect_stdout(io.StringIO()) as stream:
            prepare_cache()
        self.assertIn('hit=3 computed=1', stream.getvalue())

    def test_native_error_has_no_numpy_fallback(self):
        points = np.zeros((8, 3), np.float32)
        points[0, 0] = np.nan
        with self.assertRaisesRegex(ValueError, 'finite'):
            self.engine(points)
        with patch.dict(os.environ, {'PTV3_PCL_LIBRARY': str(self.root / 'missing.dll')}):
            with self.assertRaisesRegex(FileNotFoundError, 'Build pcl_normals'):
                regression.predict_normals(np.zeros((8, 3), np.float32), points, SimpleNamespace(),
                    {'use_normals': True, 'normal_backend': 'pcl'})

    def test_real_small_training_records_backend_and_predict_follows_checkpoint(self):
        class TinyClassifier(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.head = torch.nn.Linear(6, 2)

            def forward(self, points, counts=None):
                means = points.mean(dim=1) if counts is None else torch.stack([p.mean(dim=0) for p in points.split(counts)])
                return self.head(means)

        for split in ('train', 'val', 'test'):
            for path in (self.root / split / 'GableRoofHouse').glob('*.txt'):
                dest = path.parent.parent / 'FlatRoofHouse' / path.name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(path.read_bytes())
        log_dir = self.root / 'logs'
        argv = ['main.py', '--data_root', str(self.root), '--log_dir', str(log_dir),
                '--use_normals', '--normal_backend', 'pcl', '--epochs', '1', '--num_points', '24',
                '--batch_size', '2', '--cpu', '--seed', '42', '--normal_workers', '0']
        with patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()):
            args = classification.parse_args()
            with patch.object(classification, 'build_model', side_effect=lambda *_: TinyClassifier()):
                classification.train(args)
                ckpt = classification.load_checkpoint(log_dir, torch.device('cpu'))
                self.assertEqual(ckpt['normal_backend'], 'pcl')
                config = json.loads((log_dir / 'classification_config.json').read_text(encoding='utf-8'))
                self.assertEqual(config['normal_backend'], 'pcl')
                args.input = str(self.root / 'test/GableRoofHouse/sample_0.txt')
                args.normal_backend = 'numpy'  # The saved backend must take precedence.
                with patch.object(regression, 'estimate_normals', side_effect=AssertionError('wrong backend')):
                    classification.predict(args)


if __name__ == '__main__':
    unittest.main()
