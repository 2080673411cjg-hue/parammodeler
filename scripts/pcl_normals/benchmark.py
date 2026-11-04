"""Compare PCL normals with the unchanged production NumPy implementation."""
import argparse
import ast
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import time

import numpy as np
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from pcl_normals import PCLNormals
from primitive_names import class_directory


SCRIPT_DIR = Path(__file__).resolve().parent
CLASSES = ('FlatRoofHouse', 'RoundFlatRoofHouse', 'LShapedHouse', 'ConicalRoofHouse', 'GableRoofHouse',
           'PyramidalRoofHouse', 'FlatTopHipRoofHouse', 'BarrelRoofHouse', 'DomeRoofHouse',
           'RecessedRoofHouse', 'ShortRidgeHipRoofHouse', 'CurvedRoofRoundHouse', 'AngledGableHouse')


def production_functions():
    source = SCRIPT_DIR.parent / 'main_reg.py'
    names = {'load_txt_points', 'normalize_points', 'estimate_normals'}
    tree = ast.parse(source.read_text(encoding='utf-8-sig'))
    tree.body = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    namespace = {'np': np, 'Path': Path}
    exec(compile(tree, str(source), 'exec'), namespace)
    return namespace, hashlib.sha256(source.read_bytes()).hexdigest()


def compare(points, reference, candidate, neighbors):
    k = min(16, len(points) - 1)
    diff = points[:, None, :] - points[None, :, :]
    squared = np.einsum('ijk,ijk->ij', diff, diff)
    np.fill_diagonal(squared, np.inf)
    expected = np.argpartition(squared, k, axis=1)[:, :k]
    if np.any(neighbors == np.arange(len(points))[:, None]):
        raise AssertionError('PCL neighborhood includes the query index')
    # Duplicate points may exchange indices while describing identical neighborhoods.
    _, identities = np.unique(points, axis=0, return_inverse=True)
    same = np.all(np.sort(identities[expected], axis=1) ==
                  np.sort(identities[neighbors], axis=1), axis=1)
    local = points[expected].astype(np.float64)
    local -= local.mean(axis=1, keepdims=True)
    eigenvalues = np.linalg.eigvalsh(np.einsum('nki,nkj->nij', local, local))
    stable = (eigenvalues[:, 1] - eigenvalues[:, 0]) / np.maximum(eigenvalues.sum(axis=1), 1e-20) > 1e-5
    finite = np.isfinite(candidate).all(axis=1)
    a = reference.astype(np.float64)
    b = candidate.astype(np.float64)
    a /= np.maximum(np.linalg.norm(a, axis=1, keepdims=True), 1e-20)
    b /= np.maximum(np.linalg.norm(b, axis=1, keepdims=True), 1e-20)
    dots = np.clip(np.einsum('ij,ij->i', a, b), -1, 1)
    angles = np.degrees(np.arccos(dots))
    valid_angles = angles[finite & stable]
    return {
        'points': len(points), 'invalid_normals': int((~finite).sum()),
        'self_excluded': True, 'different_neighborhoods': int((~same).sum()),
        'degenerate_points': int((~stable).sum()),
        'sign_flips': int(np.sum((dots < 0) & finite)),
        'angle_median_deg': float(np.median(valid_angles)),
        'angle_p95_deg': float(np.quantile(valid_angles, .95)),
        'angle_max_deg': float(np.max(valid_angles)),
        'over_one_degree': int(np.sum(valid_angles > 1)),
    }, valid_angles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--dll-dir', action='append', default=[])
    parser.add_argument('--data-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--per-class', type=int, default=10)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--threads', type=int, nargs='+', default=[1, 2, 4])
    args = parser.parse_args()
    if min(args.per_class, args.repeats, *args.threads) < 1:
        parser.error('Sample counts, repeats and thread counts must be positive')
    if args.output.exists() and any(args.output.iterdir()):
        parser.error('Output directory is not empty; choose a new directory')
    functions, source_hash = production_functions()
    native = PCLNormals(args.library, args.dll_dir)
    reference = functions['estimate_normals']
    print(f'PCL {native.version}; NumPy {np.__version__}; loading real test clouds', flush=True)
    clouds = []
    started = time.perf_counter()
    for name in CLASSES:
        paths = sorted(class_directory(args.data_root, 'test', name).glob('*.txt'))
        if len(paths) < args.per_class:
            raise ValueError(f'{name}: not enough test clouds')
        indices = np.linspace(0, len(paths)-1, args.per_class, dtype=int)
        for index in indices:
            path = paths[index]
            points = functions['normalize_points'](functions['load_txt_points'](path))
            if not np.isfinite(points).all() or len(points) < 4:
                raise ValueError(f'Invalid test cloud: {path}')
            clouds.append((name, path, points, hashlib.sha256(path.read_bytes()).hexdigest()))
    preparation = time.perf_counter() - started
    # Warm both implementations before timed passes; all inputs stay immutable.
    reference(clouds[0][2])
    for threads in args.threads:
        native(clouds[0][2], threads)
    methods = [('numpy', None)] + [(f'pcl_{t}_threads', t) for t in args.threads]
    timings = {name: [] for name, _ in methods}
    rng = np.random.default_rng(42)
    for repeat in range(args.repeats):
        order = rng.permutation(len(clouds))
        for method_index in np.roll(np.arange(len(methods)), repeat):
            label, threads = methods[method_index]
            started = time.perf_counter()
            for index in order:
                points = clouds[index][2]
                if threads is None:
                    reference(points)
                else:
                    _, _, invalid = native(points, threads)
                    if invalid:
                        raise RuntimeError(f'{label}: {invalid} invalid normals')
            elapsed = time.perf_counter() - started
            timings[label].append(elapsed)
            print(f'Pass {repeat+1}/{args.repeats} {label}: {elapsed:.3f}s / {len(clouds)} clouds', flush=True)
    records, all_angles = [], []
    for index, (name, path, points, fingerprint) in enumerate(clouds):
        original = reference(points)
        candidate, neighbors, _ = native(points, args.threads[-1], diagnostics=True)
        metrics, angles = compare(points, original, candidate, neighbors)
        # Multiple native thread counts must produce identical outputs.
        single, _, _ = native(points, 1)
        if not np.array_equal(single, candidate, equal_nan=True):
            raise AssertionError(f'Native thread-count mismatch: {path}')
        records.append(dict(model=name, file=str(path), sha256=fingerprint, **metrics))
        all_angles.append(angles)
        if index == 0 or (index+1) % 20 == 0 or index+1 == len(clouds):
            print(f'Consistency {index+1}/{len(clouds)}', flush=True)
    angles = np.concatenate(all_angles)
    median_times = {label: float(np.median(values)) for label, values in timings.items()}
    result = {
        'platform': platform.platform(), 'pcl_version': native.version, 'numpy_version': np.__version__,
        'source_sha256': source_hash, 'library_sha256': hashlib.sha256(args.library.read_bytes()).hexdigest(),
        'clouds': len(clouds), 'k': 16, 'repeats': args.repeats, 'read_normalize_seconds': preparation,
        'timing_scope': 'In-memory single Python process; includes FFI, output allocation, tree build and normal computation; excludes initial file reading, DLL loading and warmup',
        'seconds_per_pass': timings, 'median_seconds': median_times,
        'speedup_vs_numpy': {label: median_times['numpy']/seconds for label, seconds in median_times.items()},
        'invalid_normals': sum(row['invalid_normals'] for row in records),
        'different_neighborhoods': sum(row['different_neighborhoods'] for row in records),
        'sign_flips': sum(row['sign_flips'] for row in records),
        'degenerate_points': sum(row['degenerate_points'] for row in records),
        'angle_median_deg': float(np.median(angles)), 'angle_p95_deg': float(np.quantile(angles,.95)),
        'angle_max_deg': float(np.max(angles)), 'over_one_degree': int(np.sum(angles>1)),
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'result.json').write_text(json.dumps(result, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    with (args.output/'per_cloud.csv').open('w', encoding='utf-8-sig', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)
    print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)


if __name__ == '__main__':
    main()
