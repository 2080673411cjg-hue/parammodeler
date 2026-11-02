"""PCL normal estimation and validated, persistent per-cloud features."""

import ctypes
import hashlib
import os
from pathlib import Path
import tempfile
import zipfile

import numpy as np


def rotate_normals_z(normals, angle_deg):
    theta = np.deg2rad(float(angle_deg))
    c, s = np.cos(theta), np.sin(theta)
    rotation = np.array([[c, -s], [s, c]], dtype=np.float32)
    result = normals.copy()
    result[:, :2] = normals[:, :2] @ rotation.T
    return result


class PCLNormals:
    def __init__(self, path=None, dll_dirs=None):
        filename = 'pcl_normals.dll' if os.name == 'nt' else 'libpcl_normals.so'
        path = Path(path or os.environ.get('PTV3_PCL_LIBRARY') or Path(__file__).parent / filename)
        if not path.is_file():
            raise FileNotFoundError(f'PCL normal library missing: {path}. Build pcl_normals first, '
                                    'or set PTV3_PCL_LIBRARY to its absolute path.')
        if dll_dirs is None:
            dll_dirs = [p for p in os.environ.get('PTV3_PCL_DLL_DIR', '').split(os.pathsep) if p]
        self.dll_handles = [os.add_dll_directory(str(Path(p).resolve())) for p in dll_dirs] if os.name == 'nt' else []
        self.library = ctypes.CDLL(str(path.resolve()))
        self.library.pcl_normals_version.restype = ctypes.c_char_p
        self.version = self.library.pcl_normals_version().decode('ascii')
        fp = ctypes.POINTER(ctypes.c_float)
        ip = ctypes.POINTER(ctypes.c_int)
        self.compute = self.library.pcl_normals_compute
        self.compute.argtypes = [fp, ctypes.c_int, ctypes.c_int, ctypes.c_int, fp, fp, ip,
                                 ctypes.POINTER(ctypes.c_char), ctypes.c_int]
        self.compute.restype = ctypes.c_int

    def __call__(self, points, threads=1, diagnostics=False):
        points = np.ascontiguousarray(points, dtype=np.float32)
        if points.ndim != 2 or points.shape[1] != 3 or not len(points):
            raise ValueError('Expected a nonempty Nx3 point cloud')
        if threads < 1 or not np.isfinite(points).all():
            raise ValueError('PCL threads must be positive and coordinates must be finite')
        normals = np.empty_like(points)
        centroid = points.mean(axis=0)
        k = min(16, len(points) - 1)
        neighbors = np.empty((len(points), max(k, 0)), dtype=np.int32) if diagnostics else None
        fp = ctypes.POINTER(ctypes.c_float)
        ip = ctypes.POINTER(ctypes.c_int)
        error = ctypes.create_string_buffer(1024)
        invalid = self.compute(points.ctypes.data_as(fp), len(points), max(k, 3), threads,
                               centroid.ctypes.data_as(fp), normals.ctypes.data_as(fp),
                               neighbors.ctypes.data_as(ip) if neighbors is not None else None,
                               error, len(error))
        if invalid < 0:
            raise RuntimeError(error.value.decode('utf-8', errors='replace'))
        return normals, neighbors, invalid


class NormalCache:
    """Cache normals of normalized source XYZ, before any canonical rotation.

    No sampling or jitter is cached. Input coordinates, algorithm and PCL version
    must match; stale entries are replaced atomically, without modifying TXT.
    """
    algorithm = 'pcl_knn16_exclude_self_centroid_outward_source_v1'

    def __init__(self, data_root, calculator, threads=4):
        self.data_root = Path(data_root).resolve()
        self.root = self.data_root / '.normal_cache' / 'pcl_v1'
        self.calculator = calculator
        self.threads = threads
        self.hits = 0
        self.computed = 0

    def get(self, source, points):
        source = Path(source).resolve()
        relative = source.relative_to(self.data_root)
        target = (self.root / relative).with_suffix('.npz')
        points = np.ascontiguousarray(points, dtype=np.float32)
        digest = hashlib.sha256(points.tobytes()).hexdigest()
        if target.is_file():
            try:
                with np.load(target, allow_pickle=False) as entry:
                    normals = entry['normals']
                    if (str(entry['algorithm']) == self.algorithm and str(entry['xyz_sha256']) == digest
                            and str(entry['pcl_version']) == self.calculator.version
                            and normals.shape == points.shape and normals.dtype == np.float32
                            and np.isfinite(normals).all()):
                        self.hits += 1
                        return normals
            except (OSError, ValueError, KeyError, EOFError, zipfile.BadZipFile):
                pass  # Interrupted or obsolete cache: recompute from source XYZ.
        normals, _, invalid = self.calculator(points, threads=self.threads)
        if invalid or not np.isfinite(normals).all():
            raise ValueError(f'PCL produced {invalid} invalid normals: {source}')
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=target.parent, suffix='.npz', delete=False) as handle:
            temporary = Path(handle.name)
        try:
            np.savez(temporary, normals=normals, xyz_sha256=digest,
                     algorithm=self.algorithm, pcl_version=self.calculator.version)
            os.replace(temporary, target)
        finally:
            temporary.unlink(missing_ok=True)
        self.computed += 1
        return normals
