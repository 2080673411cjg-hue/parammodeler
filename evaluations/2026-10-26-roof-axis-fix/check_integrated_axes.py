"""Check the final five-class fix without overwriting the roof-only stage."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("axis_stage_audit", ROOT / "check_axes.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
audit.ROOFS = (*audit.ROOFS, "LHouse", "PyramidRoof")
audit.OUT = ROOT / "integrated"
audit.OUT.mkdir(exist_ok=False)
for split in ("test", "val"):
    audit.audit(split)
