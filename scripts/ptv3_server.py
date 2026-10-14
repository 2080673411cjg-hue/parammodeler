from fastapi import FastAPI
from pydantic import BaseModel
from pathlib import Path
import subprocess
import tempfile
import json
import os
import re
import sys

PTV3_DIR = Path(__file__).resolve().parent
CLS_LOG = PTV3_DIR / os.environ.get("PTV3_CLS_LOG", "logs/ptv3_cls_v5")
REG_SUFFIX = os.environ.get("PTV3_REG_SUFFIX", "_v4_canonical_normals").strip()
if not REG_SUFFIX:
    REG_SUFFIX = "_v4_canonical_normals"
TOWER_LOG = os.environ.get("PTV3_TOWER_LOG", "").strip()
GABLED_LOG = os.environ.get("PTV3_GABLED_LOG", "").strip()

STEM_NAMES = {
    "Cuboid": "cuboid",
    "Cylinder": "cylinder",
    "LHouse": "lhouse",
    "ConeCylinder": "conecylinder",
    "GabledRoof": "gabledroof",
    "PyramidRoof": "pyramidroof",
    "TruncatedPyramidRoof": "truncatedpyramid",
    "HalfCylinderRoof": "halfcylinder",
    "CylinderDome": "cylinderdome",
    "CylinderHemisphere": "cylinderdome",
    "IndentedCuboid": "indentedcuboid",
    "AsymmetricGableHouse": "asymgable",
    "FourStageRoundTower": "fourstage",
    "TwoGableHouses": "twogable",
}

app = FastAPI()


def regression_log_dir( primitive_type, model_suffix=None ):
    override = TOWER_LOG if primitive_type == "FourStageRoundTower" else GABLED_LOG if primitive_type == "GabledRoof" else ""
    if override:
        path = Path(override).expanduser()
        return path if path.is_absolute() else PTV3_DIR / path
    suffix = model_suffix or REG_SUFFIX
    if not re.fullmatch(r"_[A-Za-z0-9_.-]+", suffix):
        raise ValueError("Invalid model_suffix")
    return PTV3_DIR / "logs" / f"ptv3_reg_{STEM_NAMES[primitive_type]}{suffix}"

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

@app.get("/health")
def health():
    return {
        "ok": True,
        "ptv3_dir": str(PTV3_DIR),
        "cls_log": str(CLS_LOG),
        "reg_suffix": REG_SUFFIX,
        "tower_reg_log": str(regression_log_dir("FourStageRoundTower")),
        "gabled_reg_log": str(regression_log_dir("GabledRoof")),
    }

def run_json_command(cmd, timeout=180):
    p = subprocess.run(
        cmd,
        cwd=str(PTV3_DIR),
        text=True,
        capture_output=True,
        timeout=timeout,
    )
    if p.returncode != 0:
        return {"ok": False, "error": p.stderr}
    try:
        return {"ok": True, "result": json.loads(p.stdout)}
    except json.JSONDecodeError:
        return {"ok": False, "error": "Failed to parse JSON output", "stdout": p.stdout, "stderr": p.stderr}

def write_temp_points(points_text: str):
    f = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False)
    try:
        f.write(points_text)
        return f.name
    finally:
        f.close()

@app.post("/classify")
def classify(req: ClassifyReq):
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
        return run_json_command(cmd)
    finally:
        os.remove(input_path)

@app.post("/regress")
def regress(req: RegressReq):
    prim = "CylinderDome" if req.primitive_type == "CylinderHemisphere" else req.primitive_type
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
        response = run_json_command(cmd)
        if response.get("ok"):
            response["model_log_dir"] = str(log_dir)
        return response
    finally:
        os.remove(input_path)
