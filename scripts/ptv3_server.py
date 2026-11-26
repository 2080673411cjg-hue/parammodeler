from primitive_names import canonical_class_name, regression_slug
from fastapi import FastAPI, Request
from pydantic import BaseModel
from pathlib import Path
from types import SimpleNamespace
import contextlib
import io
import json
import math
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
from typing import Literal, Optional

PTV3_DIR = Path(__file__).resolve().parent
CLS_LOG = PTV3_DIR / os.environ.get("PTV3_CLS_LOG", "logs/ptv3_cls_v16")
REG_SUFFIX = os.environ.get("PTV3_REG_SUFFIX", "_v14_normals").strip()
if not REG_SUFFIX:
    REG_SUFFIX = "_v14_normals"
TOWER_LOG = os.environ.get("PTV3_TOWER_LOG", "").strip()
GABLED_LOG = os.environ.get("PTV3_GABLED_LOG", "").strip()

_REAL_POPEN = subprocess.Popen
_GPU_LOCK = threading.Lock()
_CLS_CACHE = {}
_REG_CACHE = {}

STEM_NAMES = {
    "FlatRoofHouse": "cuboid",
    "RoundFlatRoofHouse": "cylinder",
    "LShapedHouse": "lhouse",
    "ConicalRoofHouse": "conecylinder",
    "GableRoofHouse": "gabledroof",
    "PyramidalRoofHouse": "pyramidroof",
    "FlatTopHipRoofHouse": "truncatedpyramid",
    "BarrelRoofHouse": "halfcylinder",
    "DomeRoofHouse": "cylinderdome",
    "CylinderHemisphere": "cylinderdome",
    "RecessedRoofHouse": "indentedcuboid",
    "ShortRidgeHipRoofHouse": "asymgable",
    "CurvedRoofRoundHouse": "fourstage",
    "AngledGableHouse": "twogable",
}

app = FastAPI()


def regression_log_dir( primitive_type, model_suffix=None ):
    primitive_type = canonical_class_name(primitive_type)
    override = TOWER_LOG if primitive_type == "CurvedRoofRoundHouse" else GABLED_LOG if primitive_type == "GableRoofHouse" else ""
    if override and not model_suffix:
        path = Path(override).expanduser()
        return path if path.is_absolute() else PTV3_DIR / path
    suffix = model_suffix or REG_SUFFIX
    if not re.fullmatch(r"_[A-Za-z0-9_.-]+", suffix):
        raise ValueError("Invalid model_suffix")
    return PTV3_DIR / "logs" / f"ptv3_reg_{regression_slug(primitive_type, suffix)}{suffix}"

class ClassifyReq(BaseModel):
    points_text: str
    topk: int = 3
    num_points: int = 2048

class RegressReq(BaseModel):
    points_text: str
    primitive_type: str
    bbox_x: float
    bbox_y: float
    bbox_z: float
    scale: float
    num_points: int = 2048
    model_suffix: str = ""
    input_coordinates: Optional[Literal["physical", "normalized"]] = None

def _use_in_process() -> bool:
    return os.environ.get("PTV3_IN_PROCESS", "1").strip() != "0" and subprocess.Popen is _REAL_POPEN

@app.get("/health")
def health():
    return {
        "ok": True,
        "ptv3_dir": str(PTV3_DIR),
        "cls_log": str(CLS_LOG),
        "reg_suffix": REG_SUFFIX,
        "tower_reg_log": str(regression_log_dir("CurvedRoofRoundHouse")),
        "gabled_reg_log": str(regression_log_dir("GableRoofHouse")),
        "in_process_gpu": _use_in_process(),
        "cached_cls": len(_CLS_CACHE),
        "cached_reg": len(_REG_CACHE),
    }

def _parse_cmd_options(cmd):
    opts = {}
    i = 2
    while i < len(cmd):
        token = cmd[i]
        if token.startswith("--") and i + 1 < len(cmd):
            opts[token[2:]] = cmd[i + 1]
            i += 2
        else:
            i += 1
    return opts

def _import_ptv3_module(name):
    import importlib
    ptv3_dir = str(PTV3_DIR)
    if ptv3_dir not in sys.path:
        sys.path.insert(0, ptv3_dir)
    return importlib.import_module(name)

def _load_cls_bundle(cls_mod, torch, log_dir):
    log_path = Path(log_dir)
    ckpt_path = log_path / "best_model.pth"
    mtime_ns = ckpt_path.stat().st_mtime_ns
    cache_key = str(log_path.resolve())
    cached = _CLS_CACHE.get(cache_key)
    if cached is not None and cached["mtime_ns"] == mtime_ns:
        return cached

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    ckpt = cls_mod.load_checkpoint(log_path, device)
    class_names = [cls_mod.canonical_class_name(name) for name in ckpt["class_names"]]
    config = ckpt.get("backbone_config", {})
    spatial_pyramid = bool(config.get("spatial_pyramid", False))
    model_args = SimpleNamespace(
        num_points=int(ckpt.get("num_points", 2048)),
        normalize_points=bool(ckpt.get("normalize_points", True)),
        use_normals=bool(ckpt.get("use_normals", False)),
        ptv3_grid_size=float(config.get("grid_size", 0.02)),
        ptv3_patch_size=int(config.get("patch_size", 128)),
        ptv3_flash=bool(config.get("flash", False)),
        ptv3_dropout=float(config.get("dropout", 0.1)),
        spatial_pyramid=spatial_pyramid,
        canonical_align=bool(config.get("canonical_align", False) or spatial_pyramid),
        ptv3_repo=config.get("repo", None),
        use_actual_point_count=False,
        preserve_input_points=None,
        normal_backend=ckpt.get("normal_backend", "numpy"),
        normal_threads=4,
    )
    model = cls_mod.build_model(model_args, len(class_names)).to(device)
    model.load_state_dict(ckpt["state_dict"])
    model.eval()
    bundle = {
        "mtime_ns": mtime_ns,
        "model": model,
        "ckpt": ckpt,
        "class_names": class_names,
        "model_args": model_args,
        "device": device,
    }
    _CLS_CACHE[cache_key] = bundle
    return bundle

def _load_reg_bundle(reg_mod, torch, log_dir):
    log_path = Path(log_dir)
    ckpt_path = log_path / "best_model.pth"
    mtime_ns = ckpt_path.stat().st_mtime_ns
    cache_key = str(log_path.resolve())
    cached = _REG_CACHE.get(cache_key)
    if cached is not None and cached["mtime_ns"] == mtime_ns:
        return cached

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    load_args = SimpleNamespace(
        backbone="ptv3",
        circular_targets=[],
        ptv3_grid_size=0.02,
        ptv3_patch_size=128,
        ptv3_flash=False,
        ptv3_dropout=0.1,
        ptv3_repo=None,
    )
    loaded = reg_mod.load_checkpoint(log_path, device, load_args)
    bundle = {
        "mtime_ns": mtime_ns,
        "loaded": loaded,
        "device": device,
    }
    _REG_CACHE[cache_key] = bundle
    return bundle

def _run_cls_in_process(opts, request=None):
    import numpy as np
    import torch
    cls_mod = _import_ptv3_module("main")
    with _GPU_LOCK:
        if request is not None:
            from anyio import from_thread
            if from_thread.run(request.is_disconnected):
                return {"ok": False, "canceled": True, "error": "Prediction canceled by client."}
        bundle = _load_cls_bundle(cls_mod, torch, opts["log_dir"])
        margs = bundle["model_args"]
        ckpt = bundle["ckpt"]
        points = cls_mod.load_txt_points(Path(opts["input"]))
        source_points = points
        if margs.normalize_points:
            points = cls_mod.normalize_points(points)
        normals = cls_mod.predict_normals(source_points, points, margs, ckpt)
        num_points = int(ckpt.get("num_points", int(opts.get("num_points", 2048))))
        sampling_mode = cls_mod.resolve_sampling_mode(margs, ckpt)
        points = cls_mod.sample_classification_points(points, num_points, False, 0.0, False, normals, sampling_mode)
        tensor = torch.from_numpy(points).unsqueeze(0).to(bundle["device"]).float()
        with torch.no_grad():
            prob = torch.softmax(bundle["model"](tensor), dim=1)[0].cpu().numpy()
        order = np.argsort(-prob)
        topk = max(1, int(opts.get("topk", 3)))
        class_names = bundle["class_names"]
        return {"ok": True, "result": [{"class": class_names[i], "prob": float(prob[i])} for i in order[:topk]]}

def _run_reg_in_process(opts, request=None):
    import torch
    reg_mod = _import_ptv3_module("main_reg")
    pred_args = SimpleNamespace(
        log_dir=opts["log_dir"],
        input=opts["input"],
        num_points=int(opts.get("num_points", 2048)),
        bbox_x=float(opts["bbox_x"]),
        bbox_y=float(opts["bbox_y"]),
        bbox_z=float(opts["bbox_z"]),
        scale=float(opts["scale"]),
        input_coordinates=opts.get("input_coordinates"),
        cpu=False,
        metadata="",
        data_root="",
        canonical_align=False,
        normal_backend=None,
        normal_threads=4,
    )
    with _GPU_LOCK:
        if request is not None:
            from anyio import from_thread
            if from_thread.run(request.is_disconnected):
                return {"ok": False, "canceled": True, "error": "Prediction canceled by client."}
        bundle = _load_reg_bundle(reg_mod, torch, opts["log_dir"])
        buf = io.StringIO()
        orig_load_ckpt = reg_mod.load_checkpoint
        orig_load_meta = reg_mod.load_metadata
        try:
            reg_mod.load_checkpoint = lambda ld, dev, a: bundle["loaded"]
            reg_mod.load_metadata = lambda path: []
            with contextlib.redirect_stdout(buf):
                reg_mod.predict(pred_args)
        finally:
            reg_mod.load_checkpoint = orig_load_ckpt
            reg_mod.load_metadata = orig_load_meta
    return {"ok": True, "result": json.loads(buf.getvalue())}

def _run_subprocess_command(cmd, timeout=180, request=None):
    if request is not None:
        from anyio import from_thread

    p = subprocess.Popen(cmd, cwd=str(PTV3_DIR), text=True,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    deadline = time.monotonic() + timeout
    try:
        while True:
            # Sync FastAPI routes run in an AnyIO worker; inspect the ASGI
            # connection on its event loop, not from the worker itself.
            if request is not None and from_thread.run(request.is_disconnected):
                return {"ok": False, "canceled": True, "error": "Prediction canceled by client."}
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return {"ok": False, "error": "Prediction timed out."}
            try:
                stdout, stderr = p.communicate(timeout=min(.1, remaining))
                break
            except subprocess.TimeoutExpired:
                pass
    finally:
        if p.poll() is None:
            p.kill()
        p.communicate()
    if p.returncode != 0:
        return {"ok": False, "error": stderr}
    try:
        return {"ok": True, "result": json.loads(stdout)}
    except json.JSONDecodeError:
        return {"ok": False, "error": "Failed to parse JSON output", "stdout": stdout, "stderr": stderr}

def run_json_command(cmd, timeout=180, request=None):
    if _use_in_process() and len(cmd) >= 2:
        script_name = Path(cmd[1]).name
        if script_name in ("main.py", "main_reg.py"):
            if request is not None:
                from anyio import from_thread
                if from_thread.run(request.is_disconnected):
                    return {"ok": False, "canceled": True, "error": "Prediction canceled by client."}
            opts = _parse_cmd_options(cmd)
            try:
                if script_name == "main.py":
                    return _run_cls_in_process(opts, request=request)
                return _run_reg_in_process(opts, request=request)
            except Exception as exc:
                return {"ok": False, "error": str(exc)}
    return _run_subprocess_command(cmd, timeout=timeout, request=request)

def write_temp_points(points_text: str):
    f = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False)
    try:
        f.write(points_text)
        return f.name
    finally:
        f.close()

@app.post("/classify")
def classify(req: ClassifyReq, request: Request = None):
    input_path = write_temp_points(req.points_text)
    try:
        cmd = [
            sys.executable, str(PTV3_DIR / "main.py"),
            "--mode", "predict",
            "--input", input_path,
            "--log_dir", str(CLS_LOG),
            "--num_points", str(req.num_points),
            "--topk", str(req.topk),
        ]
        response = run_json_command(cmd, request=request)
        if response.get("ok"):
            response["model_log_dir"] = str(CLS_LOG)
        return response
    finally:
        os.remove(input_path)

@app.post("/regress")
def regress(req: RegressReq, request: Request = None):
    prim = canonical_class_name(req.primitive_type)
    stem = STEM_NAMES.get(prim)
    if not stem:
        return {"ok": False, "error": f"Unsupported primitive_type: {req.primitive_type}"}

    try:
        log_dir = regression_log_dir(prim, req.model_suffix.strip())
    except ValueError as exc:
        return {"ok": False, "error": str(exc)}
    if not (log_dir / "best_model.pth").is_file():
        return {"ok": False, "error": f"Missing regression checkpoint: {log_dir}/best_model.pth"}

    input_path = write_temp_points(req.points_text)
    try:
        cmd = [
            sys.executable, str(PTV3_DIR / "main_reg.py"),
            "--mode", "predict",
            "--input", input_path,
            "--log_dir", str(log_dir),
            "--num_points", str(req.num_points),
            "--bbox_x", str(req.bbox_x),
            "--bbox_y", str(req.bbox_y),
            "--bbox_z", str(req.bbox_z),
            "--scale", str(req.scale),
        ]
        if getattr(req, "input_coordinates", None) is not None:
            cmd += ["--input_coordinates", req.input_coordinates]
        response = run_json_command(cmd, request=request)
        if response.get("ok"):
            if prim == "CurvedRoofRoundHouse":
                params = response.get("result", {}).get("params", {})
                rise = params.get("middleRise")
                if "middleBulge" in params or isinstance(rise, bool) or not isinstance(rise, (int, float)) or not math.isfinite(rise):
                    return {"ok": False, "error": "Incompatible tower checkpoint: middleRise in metres is required. Regenerate tower data and retrain; middleBulge is not a physical height.", "model_log_dir": str(log_dir)}
            response["model_log_dir"] = str(log_dir)
        return response
    finally:
        os.remove(input_path)
