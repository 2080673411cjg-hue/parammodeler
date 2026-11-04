from primitive_names import canonical_class_name, regression_slug
from fastapi import FastAPI, Request
from pydantic import BaseModel
from pathlib import Path
import subprocess
import tempfile
import json
import math
import os
import re
import sys
import time
from typing import Literal, Optional

PTV3_DIR = Path(__file__).resolve().parent
CLS_LOG = PTV3_DIR / os.environ.get("PTV3_CLS_LOG", "logs/ptv3_cls_v10")
REG_SUFFIX = os.environ.get("PTV3_REG_SUFFIX", "_v10_normals").strip()
if not REG_SUFFIX:
    REG_SUFFIX = "_v10_normals"
TOWER_LOG = os.environ.get("PTV3_TOWER_LOG", "").strip()
GABLED_LOG = os.environ.get("PTV3_GABLED_LOG", "").strip()

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

@app.get("/health")
def health():
    return {
        "ok": True,
        "ptv3_dir": str(PTV3_DIR),
        "cls_log": str(CLS_LOG),
        "reg_suffix": REG_SUFFIX,
        "tower_reg_log": str(regression_log_dir("CurvedRoofRoundHouse")),
        "gabled_reg_log": str(regression_log_dir("GableRoofHouse")),
    }

def run_json_command(cmd, timeout=180, request=None):
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
