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


def align_cloud_xy(cloud):
    """Rotate a single point cloud [N, C] in XY to its minimum-area bounding box frame."""
    if cloud.shape[0] < 4:
        return cloud
    xy = cloud[:, :2]
    center = xy.mean(dim=0, keepdim=True)
    xy_c = xy - center
    # Sweep 90 angles in [0, pi/2) to find minimum-area 1%..99% bounding rectangle
    thetas = torch.linspace(0.0, 0.5 * torch.pi, 91, device=cloud.device, dtype=cloud.dtype)[:-1]
    cos_t = torch.cos(thetas)
    sin_t = torch.sin(thetas)
    rx = xy_c[:, 0:1] * cos_t - xy_c[:, 1:2] * sin_t
    ry = xy_c[:, 0:1] * sin_t + xy_c[:, 1:2] * cos_t
    q = torch.tensor([0.01, 0.99], device=cloud.device, dtype=cloud.dtype)
    qx = torch.quantile(rx, q, dim=0)
    qy = torch.quantile(ry, q, dim=0)
    dx = (qx[1] - qx[0]).clamp_min(1e-6)
    dy = (qy[1] - qy[0]).clamp_min(1e-6)
    best = int(torch.argmin(dx * dy).item())
    theta = thetas[best]
    if dx[best] < dy[best]:
        theta = theta + 0.5 * torch.pi
    c = torch.cos(theta)
    s = torch.sin(theta)
    u = xy_c[:, 0] * c - xy_c[:, 1] * s
    v = xy_c[:, 0] * s + xy_c[:, 1] * c
    # Resolve 180-degree sign ambiguity by placing the denser half on (+u, +v)
    u_mid = 0.5 * (u.amax() + u.amin())
    v_mid = 0.5 * (v.amax() + v.amin())
    sx = 1.0 if int((u >= u_mid).sum().item()) >= int((u < u_mid).sum().item()) else -1.0
    sy = 1.0 if int((v >= v_mid).sum().item()) >= int((v < v_mid).sum().item()) else -1.0
    if sx * sy < 0:
        # Keep a proper rotation (det = +1) so normals/handedness never flip
        sy = sx
    u = u * sx
    v = v * sy
    out = cloud.clone()
    out[:, 0] = u
    out[:, 1] = v
    if cloud.shape[1] >= 6:
        nx = cloud[:, 3]
        ny = cloud[:, 4]
        out[:, 3] = (nx * c - ny * s) * sx
        out[:, 4] = (nx * s + ny * c) * sy
    return out


def align_batch_xy(points, point_counts=None):
    """Apply canonical XY orientation per cloud before voxelization."""
    if point_counts is None:
        return torch.stack([align_cloud_xy(cloud) for cloud in points])
    return torch.cat([align_cloud_xy(cloud) for cloud in points.split(point_counts)], dim=0)


def pool_spatial_pyramid_features(features, coords, batch, batch_size):
    """Pool 2x2 XY quadrants sorted by voxel occupancy + 16D geometric shape summary."""
    feat_dim = features.shape[1]
    out_rows = []
    q_probs = torch.tensor([0.02, 0.98], device=features.device, dtype=features.dtype)
    c_probs = torch.tensor([0.05, 0.95], device=features.device, dtype=features.dtype)
    for index in range(batch_size):
        mask = batch == index
        cloud_feat = features[mask]
        cloud_xyz = coords[mask]
        n_vox = cloud_feat.shape[0]
        if n_vox == 0:
            raise RuntimeError(f"PTv3 encoder lost cloud {index}")
        xy = cloud_xyz[:, :2]
        if n_vox >= 4:
            qxy = torch.quantile(xy, q_probs, dim=0)
            xy_min, xy_max = qxy[0], qxy[1]
        else:
            xy_min, xy_max = xy.amin(dim=0), xy.amax(dim=0)
        span = (xy_max - xy_min).clamp_min(1e-6)
        uv = ((xy - xy_min) / span).clamp(0.0, 1.0)
        u, v = uv[:, 0], uv[:, 1]

        # 4 quadrants in [0, 1]^2
        q_masks = [
            (u >= 0.5) & (v >= 0.5),
            (u < 0.5) & (v >= 0.5),
            (u < 0.5) & (v < 0.5),
            (u >= 0.5) & (v < 0.5),
        ]
        q_counts = [int(m.sum().item()) for m in q_masks]
        order = sorted(range(4), key=lambda i: q_counts[i], reverse=True)
        q_pooled = []
        q_fracs = []
        for idx in order:
            m = q_masks[idx]
            cnt = q_counts[idx]
            q_fracs.append(cnt / float(n_vox))
            if cnt > 0:
                qf = cloud_feat[m]
                q_pooled.append(torch.cat((qf.mean(dim=0), qf.amax(dim=0))))
            else:
                q_pooled.append(cloud_feat.new_zeros(2 * feat_dim))

        # 4 corner vacancy distances (5th nearest encoded voxel to each corner)
        k_idx = min(4, n_vox - 1)
        c_dists = []
        for cx in (0.0, 1.0):
            for cy in (0.0, 1.0):
                d = torch.hypot(u - cx, v - cy)
                c_dists.append(torch.sort(d).values[k_idx])
        c_dists_t = torch.sort(torch.stack(c_dists), descending=True).values

        # Cross-section envelope center shift along u (8 slices) & aspect ratios
        edges = torch.linspace(0.0, 1.0, 9, device=features.device, dtype=features.dtype)
        slice_centers = []
        for s_i in range(8):
            sm = (u >= edges[s_i]) & (u <= edges[s_i + 1])
            if int(sm.sum().item()) >= 3:
                vs = v[sm]
                qv = torch.quantile(vs, c_probs)
                slice_centers.append(0.5 * (qv[0] + qv[1]))
            else:
                slice_centers.append(features.new_tensor(0.5))
        sc_t = torch.stack(slice_centers)
        v_shift = (sc_t.amax() - sc_t.amin()).unsqueeze(0)
        z_span = (cloud_xyz[:, 2].amax() - cloud_xyz[:, 2].amin()).clamp_min(1e-6)
        ratios = torch.stack([
            (span[1] / span[0]).clamp(0.0, 2.0),
            (z_span / span[0]).clamp(0.0, 2.0),
            (sc_t - sc_t.mean()).abs().mean(),
        ])
        q_fracs_t = features.new_tensor(q_fracs)
        geom_16 = torch.cat((c_dists_t, q_fracs_t, v_shift, ratios, sc_t[:4] - 0.5))
        out_rows.append(torch.cat((*q_pooled, geom_16)))
    return torch.stack(out_rows)


def pool_regression_pyramid_features(features, coords, batch, batch_size):
    """Pool 2x2 XY quadrants + 4 Z-height layers + 24D spatial/vertical geometry summary."""
    feat_dim = features.shape[1]
    out_rows = []
    q_probs = torch.tensor([0.02, 0.98], device=features.device, dtype=features.dtype)
    c_probs = torch.tensor([0.05, 0.95], device=features.device, dtype=features.dtype)
    for index in range(batch_size):
        mask = batch == index
        cloud_feat = features[mask]
        cloud_xyz = coords[mask]
        n_vox = cloud_feat.shape[0]
        if n_vox == 0:
            raise RuntimeError(f"PTv3 encoder lost cloud {index}")
        if n_vox >= 4:
            qxyz = torch.quantile(cloud_xyz, q_probs, dim=0)
            xyz_min, xyz_max = qxyz[0], qxyz[1]
        else:
            xyz_min, xyz_max = cloud_xyz.amin(dim=0), cloud_xyz.amax(dim=0)
        span = (xyz_max - xyz_min).clamp_min(1e-6)
        uvw = ((cloud_xyz - xyz_min) / span).clamp(0.0, 1.0)
        u, v, w = uvw[:, 0], uvw[:, 1], uvw[:, 2]

        # 1) 4 fixed XY quadrants (preserve directional order for canonical regression)
        q_masks = [
            (u >= 0.5) & (v >= 0.5),
            (u < 0.5) & (v >= 0.5),
            (u < 0.5) & (v < 0.5),
            (u >= 0.5) & (v < 0.5),
        ]
        q_pooled = []
        q_fracs = []
        for m in q_masks:
            cnt = int(m.sum().item())
            q_fracs.append(cnt / float(n_vox))
            if cnt > 0:
                qf = cloud_feat[m]
                q_pooled.append(torch.cat((qf.mean(dim=0), qf.amax(dim=0))))
            else:
                q_pooled.append(cloud_feat.new_zeros(2 * feat_dim))

        # 2) 4 vertical Z-height slices [0..0.25, 0.25..0.5, 0.5..0.75, 0.75..1.0]
        z_edges = (0.0, 0.25, 0.5, 0.75, 1.01)
        z_pooled = []
        z_fracs = []
        z_radii = []
        xy_c = uvw[:, :2] - 0.5
        r_xy = torch.hypot(xy_c[:, 0], xy_c[:, 1])
        for zi in range(4):
            zm = (w >= z_edges[zi]) & (w < z_edges[zi + 1])
            cnt = int(zm.sum().item())
            z_fracs.append(cnt / float(n_vox))
            if cnt > 0:
                zf = cloud_feat[zm]
                z_pooled.append(torch.cat((zf.mean(dim=0), zf.amax(dim=0))))
                z_radii.append(r_xy[zm].amax())
            else:
                z_pooled.append(cloud_feat.new_zeros(2 * feat_dim))
                z_radii.append(features.new_tensor(0.0))

        # 3) 4 corner vacancy distances (for LShapedHouse / RecessedRoofHouse / AngledGableHouse)
        k_idx = min(4, n_vox - 1)
        c_dists = []
        for cx in (0.0, 1.0):
            for cy in (0.0, 1.0):
                d = torch.hypot(u - cx, v - cy)
                c_dists.append(torch.sort(d).values[k_idx])
        c_dists_t = torch.stack(c_dists)

        # 4) Cross-section center & height profile along u (4 slices)
        u_edges = (0.0, 0.25, 0.5, 0.75, 1.01)
        u_vcenters = []
        u_ztops = []
        for ui in range(4):
            um = (u >= u_edges[ui]) & (u < u_edges[ui + 1])
            if int(um.sum().item()) >= 3:
                qv = torch.quantile(v[um], c_probs)
                u_vcenters.append(0.5 * (qv[0] + qv[1]) - 0.5)
                u_ztops.append(w[um].amax())
            else:
                u_vcenters.append(features.new_tensor(0.0))
                u_ztops.append(features.new_tensor(0.5))

        geom_24 = torch.cat((
            c_dists_t,
            features.new_tensor(q_fracs),
            features.new_tensor(z_fracs),
            torch.stack(z_radii),
            torch.stack(u_vcenters),
            torch.stack(u_ztops),
        ))
        out_rows.append(torch.cat((*q_pooled, *z_pooled, geom_24)))
    return torch.stack(out_rows)


class PTv3Regressor(nn.Module):
    """Official five-stage PTv3 encoder + global mean/max pooling (+ optional 3D spatial pyramid) + parameter head.

    cls_mode=True is upstream's encoder-only switch; no classification loss/head
    is added. Output layout matches main_reg: parameters then geometry auxiliaries.
    """

    def __init__(self, target_dim, aux_dim=0, in_channels=6, grid_size=0.02,
                 patch_size=128, enable_flash=False, dropout=0.1, repo_path=None,
                 spatial_pyramid=False, canonical_align=False):
        super().__init__()
        if grid_size <= 0 or patch_size < 1 or target_dim < 1 or not 0 <= dropout < 1:
            raise ValueError("Invalid PTv3 grid/patch/output/dropout configuration")
        self.grid_size = grid_size
        self.aux_dim = aux_dim
        self.in_channels = in_channels
        self.spatial_pyramid = bool(spatial_pyramid)
        self.canonical_align = bool(canonical_align)
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
        if self.spatial_pyramid:
            out_proj = nn.Linear(256, 1024)
            nn.init.zeros_(out_proj.weight)
            nn.init.zeros_(out_proj.bias)
            self.pyramid_proj = nn.Sequential(
                nn.Linear(8 * 1024 + 24, 256),
                nn.LayerNorm(256),
                nn.GELU(),
                nn.Dropout(dropout),
                out_proj,
            )
        else:
            self.pyramid_proj = None

    def forward(self, points, aux=None):
        if points.ndim != 3 or points.shape[-1] != self.in_channels:
            raise ValueError(f"Expected [B, N, {self.in_channels}] points")
        if self.canonical_align:
            points = align_batch_xy(points)
        voxels = voxelize_batch(points, self.grid_size)
        encoded = self.encoder(voxels)
        features = pool_cloud_features(encoded.feat, encoded.batch, points.shape[0])
        if self.spatial_pyramid and self.pyramid_proj is not None:
            enc_coords = getattr(encoded, "coord", voxels["coord"])
            pyramid_feat = pool_regression_pyramid_features(
                encoded.feat, enc_coords, encoded.batch, points.shape[0])
            features = features + self.pyramid_proj(pyramid_feat)
        if self.aux_dim:
            if aux is None or aux.shape != (points.shape[0], self.aux_dim):
                raise ValueError(f"Expected auxiliary features [B, {self.aux_dim}]")
            features = torch.cat((features, aux), dim=1)
        return self.head(features)

class PTv3Classifier(nn.Module):
    """Official PTv3 encoder + global mean/max pooling (+ optional 2x2 spatial pyramid) + classification head.

    This mirrors the regression adapter above: the upstream PointTransformerV3 is
    used in cls_mode=True so it returns encoded sparse point features, then this
    project owns only the global pooling and task-specific head.
    """

    def __init__(self, num_classes, in_channels=6, grid_size=0.02,
                 patch_size=128, enable_flash=False, dropout=0.1, repo_path=None,
                 spatial_pyramid=False, canonical_align=False):
        super().__init__()
        if grid_size <= 0 or patch_size < 1 or num_classes < 2 or not 0 <= dropout < 1:
            raise ValueError("Invalid PTv3 classifier grid/patch/classes/dropout configuration")
        self.grid_size = grid_size
        self.in_channels = in_channels
        self.num_classes = num_classes
        self.spatial_pyramid = bool(spatial_pyramid)
        self.canonical_align = bool(canonical_align or spatial_pyramid)
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
        if self.spatial_pyramid:
            out_proj = nn.Linear(256, 1024)
            nn.init.zeros_(out_proj.weight)
            nn.init.zeros_(out_proj.bias)
            self.pyramid_proj = nn.Sequential(
                nn.Linear(4 * 1024 + 16, 256),
                nn.LayerNorm(256),
                nn.GELU(),
                nn.Dropout(dropout),
                out_proj,
            )
        else:
            self.pyramid_proj = None

    def forward(self, points, point_counts=None):
        dims = 3 if point_counts is None else 2
        if points.ndim != dims or points.shape[-1] != self.in_channels:
            raise ValueError(f"Expected {dims} dimensions and {self.in_channels} point channels")
        if self.canonical_align:
            points = align_batch_xy(points, point_counts)
        voxels = voxelize_batch(points, self.grid_size, point_counts)
        encoded = self.encoder(voxels)
        batch_size = points.shape[0] if point_counts is None else len(point_counts)
        features = pool_cloud_features(encoded.feat, encoded.batch, batch_size)
        if self.spatial_pyramid and self.pyramid_proj is not None:
            enc_coords = getattr(encoded, "coord", voxels["coord"])
            pyramid_feat = pool_spatial_pyramid_features(
                encoded.feat, enc_coords, encoded.batch, batch_size)
            features = features + self.pyramid_proj(pyramid_feat)
        return self.head(features)

