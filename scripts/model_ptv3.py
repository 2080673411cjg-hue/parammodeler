"""Classification and regression adapters for the official standalone Point Transformer V3.

Upstream: https://github.com/Pointcept/PointTransformerV3 (MIT).
The upstream encoder is loaded lazily so data tools and CPU contract tests can run
without the sparse CUDA dependencies.
"""

import hashlib
import importlib
import os
from pathlib import Path
import sys
import types

import torch
from torch import nn


def load_official_encoder(repo_path=None):
    root = Path(repo_path or os.environ.get("PTV3_ROOT") or
                Path(__file__).parent / "third_party" / "PointTransformerV3").resolve()
    if not (root / "model.py").is_file() or not (root / "serialization").is_dir():
        raise RuntimeError(f"Missing official PTv3 source at {root}. Run setup_ptv3.sh first, "
                           "or pass --ptv3_repo /path/to/PointTransformerV3.")
    # A package namespace preserves upstream's relative serialization import and
    # avoids shadowing this project's existing model.py.
    package = "_official_ptv3_" + hashlib.sha256(str(root).encode()).hexdigest()[:12]
    if package not in sys.modules:
        module = types.ModuleType(package)
        module.__path__ = [str(root)]
        sys.modules[package] = module
    try:
        return importlib.import_module(package + ".model").PointTransformerV3
    except ImportError as exc:
        raise RuntimeError("PTv3 dependencies are missing/incompatible. Run setup_ptv3.sh "
                           "in the Ubuntu CUDA environment. Original error: " + str(exc)) from exc


def voxelize_batch(points, grid_size, point_counts=None):
    """Return unique sparse voxels, preserving cloud boundaries and feature gradients."""
    if grid_size <= 0:
        raise ValueError("grid_size must be positive")
    if not torch.isfinite(points).all():
        raise ValueError("Point cloud contains non-finite coordinates/features")
    if point_counts is None:
        if points.ndim != 3 or points.shape[0] == 0 or points.shape[1] == 0 or points.shape[2] < 3:
            raise ValueError("Expected nonempty points shaped [batch, points, channels>=3]")
        batch_size, count, channels = points.shape
        xyz = points[..., :3]
        relative_xyz = (xyz - xyz.amin(dim=1, keepdim=True)).reshape(-1, 3)
        batch = torch.arange(batch_size, device=points.device).repeat_interleave(count)
        points = points.reshape(-1, channels)
    else:
        if (points.ndim != 2 or points.shape[1] < 3 or not point_counts
                or min(point_counts) <= 0 or sum(point_counts) != len(points)):
            raise ValueError("Expected packed points with a positive count for each cloud")
        xyz = points[:, :3]
        origins = torch.stack([cloud.amin(dim=0) for cloud in xyz.split(point_counts)])
        counts = torch.tensor(point_counts, device=points.device)
        cloud_indices = torch.arange(len(point_counts), device=points.device)
        batch = cloud_indices.repeat_interleave(counts, output_size=len(points))
        relative_xyz = xyz - origins[batch]
    grid = torch.floor(relative_xyz / grid_size).long()
    if grid.max() >= 65536:
        raise ValueError("PTv3 grid exceeds 16-bit serialization. Normalize points or increase grid size.")
    keys = torch.cat((batch[:, None], grid), dim=1)
    unique, inverse, counts = torch.unique(keys, dim=0, sorted=True,
                                           return_inverse=True, return_counts=True)
    features = points.new_zeros((len(unique), points.shape[1]))
    features.index_add_(0, inverse, points)
    features = features / counts[:, None].to(points.dtype)
    return {"coord": features[:, :3].contiguous(), "feat": features.contiguous(),
            "grid_coord": unique[:, 1:].int().contiguous(),
            "batch": unique[:, 0].long().contiguous()}


def pool_cloud_features(features, batch, batch_size):
    pooled = []
    for index in range(batch_size):
        cloud = features[batch == index]
        if cloud.shape[0] == 0:
            raise RuntimeError(f"PTv3 encoder lost cloud {index}")
        pooled.append(torch.cat((cloud.mean(dim=0), cloud.amax(dim=0))))
    return torch.stack(pooled)


class PTv3Regressor(nn.Module):
    """Official five-stage PTv3 encoder + global mean/max pooling + parameter head.

    cls_mode=True is upstream's encoder-only switch; no classification loss/head
    is added. Output layout matches main_reg: parameters then geometry auxiliaries.
    """

    def __init__(self, target_dim, aux_dim=0, in_channels=6, grid_size=0.02,
                 patch_size=128, enable_flash=False, dropout=0.1, repo_path=None):
        super().__init__()
        if grid_size <= 0 or patch_size < 1 or target_dim < 1 or not 0 <= dropout < 1:
            raise ValueError("Invalid PTv3 grid/patch/output/dropout configuration")
        self.grid_size = grid_size
        self.aux_dim = aux_dim
        self.in_channels = in_channels
        encoder = load_official_encoder(repo_path)
        self.encoder = encoder(
            in_channels=in_channels, cls_mode=True,
            stride=(2, 2, 2, 2), enc_depths=(2, 2, 2, 6, 2),
            enc_channels=(32, 64, 128, 256, 512), enc_num_head=(2, 4, 8, 16, 32),
            enc_patch_size=(patch_size,) * 5,
            enable_flash=enable_flash, upcast_attention=not enable_flash,
            upcast_softmax=not enable_flash, shuffle_orders=False,
            drop_path=0.1,
        )
        self.head = nn.Sequential(
            nn.Linear(1024 + aux_dim, 512), nn.LayerNorm(512), nn.GELU(),
            nn.Dropout(dropout), nn.Linear(512, 256), nn.LayerNorm(256),
            nn.GELU(), nn.Dropout(dropout), nn.Linear(256, target_dim),
        )

    def forward(self, points, aux=None):
        if points.ndim != 3 or points.shape[-1] != self.in_channels:
            raise ValueError(f"Expected [B, N, {self.in_channels}] points")
        encoded = self.encoder(voxelize_batch(points, self.grid_size))
        features = pool_cloud_features(encoded.feat, encoded.batch, points.shape[0])
        if self.aux_dim:
            if aux is None or aux.shape != (points.shape[0], self.aux_dim):
                raise ValueError(f"Expected auxiliary features [B, {self.aux_dim}]")
            features = torch.cat((features, aux), dim=1)
        return self.head(features)

class PTv3Classifier(nn.Module):
    """Official PTv3 encoder + global mean/max pooling + classification head.

    This mirrors the regression adapter above: the upstream PointTransformerV3 is
    used in cls_mode=True so it returns encoded sparse point features, then this
    project owns only the global pooling and task-specific head.
    """

    def __init__(self, num_classes, in_channels=6, grid_size=0.02,
                 patch_size=128, enable_flash=False, dropout=0.1, repo_path=None):
        super().__init__()
        if grid_size <= 0 or patch_size < 1 or num_classes < 2 or not 0 <= dropout < 1:
            raise ValueError("Invalid PTv3 classifier grid/patch/classes/dropout configuration")
        self.grid_size = grid_size
        self.in_channels = in_channels
        self.num_classes = num_classes
        encoder = load_official_encoder(repo_path)
        self.encoder = encoder(
            in_channels=in_channels, cls_mode=True,
            stride=(2, 2, 2, 2), enc_depths=(2, 2, 2, 6, 2),
            enc_channels=(32, 64, 128, 256, 512), enc_num_head=(2, 4, 8, 16, 32),
            enc_patch_size=(patch_size,) * 5,
            enable_flash=enable_flash, upcast_attention=not enable_flash,
            upcast_softmax=not enable_flash, shuffle_orders=False,
            drop_path=0.1,
        )
        self.head = nn.Sequential(
            nn.Linear(1024, 512), nn.LayerNorm(512), nn.GELU(),
            nn.Dropout(dropout), nn.Linear(512, 256), nn.LayerNorm(256),
            nn.GELU(), nn.Dropout(dropout), nn.Linear(256, num_classes),
        )

    def forward(self, points, point_counts=None):
        dims = 3 if point_counts is None else 2
        if points.ndim != dims or points.shape[-1] != self.in_channels:
            raise ValueError(f"Expected {dims} dimensions and {self.in_channels} point channels")
        encoded = self.encoder(voxelize_batch(points, self.grid_size, point_counts))
        batch_size = points.shape[0] if point_counts is None else len(point_counts)
        features = pool_cloud_features(encoded.feat, encoded.batch, batch_size)
        return self.head(features)
