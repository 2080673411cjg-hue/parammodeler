"""Read-only CPU orientation audit; all outputs stay beside this script."""
import csv
import hashlib
import importlib.util
import inspect
import json
import sys
import time
from collections import Counter
from datetime import datetime
from pathlib import Path
from types import ModuleType

import numpy as np


OUT = Path(__file__).resolve().parent
ROOT = OUT.parent.parent
SCRIPTS = ROOT / "scripts"
DATA = Path(r"E:\pointnet\datasets_aug")
LOGS = Path(r"E:\pointnet\ptv3_simple\logs")
METADATA = DATA / "metadata" / "sample_params.json"
CLASSES = ["Cuboid", "Cylinder", "LHouse", "ConeCylinder", "GabledRoof",
           "PyramidRoof", "TruncatedPyramidRoof", "HalfCylinderRoof", "CylinderDome",
           "IndentedCuboid", "AsymmetricGableHouse", "FourStageRoundTower", "TwoGableHouses"]
ROUND = {"Cylinder", "ConeCylinder", "CylinderDome", "FourStageRoundTower"}
RIDGE = {"GabledRoof", "AsymmetricGableHouse", "HalfCylinderRoof"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")


def scalar_return(result):
    # Preserve scalar diagnostics and rotation matrix; omit only the aligned cloud.
    return {key: (value.tolist() if isinstance(value, np.ndarray) else value)
            for key, value in result.items() if key != "aligned_points"}


def main():
    started = time.perf_counter()
    sys.path.insert(0, str(SCRIPTS))
    placeholder = ModuleType("model_ptv3")

    class UnusedModel:
        def __init__(self, *args, **kwargs):
            raise AssertionError("Network construction is forbidden in this CPU orientation audit")

    placeholder.PTv3Regressor = UnusedModel
    previous_model = sys.modules.get("model_ptv3")
    sys.modules["model_ptv3"] = placeholder
    try:
        spec = importlib.util.spec_from_file_location("v8_cpu_real_main_reg", SCRIPTS / "main_reg.py")
        regression = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(regression)
    finally:
        if previous_model is None:
            sys.modules.pop("model_ptv3", None)
        else:
            sys.modules["model_ptv3"] = previous_model
    import canonical_orientation as canonical
    assert Path(canonical.__file__).resolve() == (SCRIPTS / "canonical_orientation.py").resolve()
    assert regression.canonical_orientation is canonical.canonical_orientation
    assert regression._HAS_CANONICAL

    configs = {}
    for path in sorted(LOGS.glob("ptv3_reg_*_v8_normals/regression_config.json")):
        config = json.loads(path.read_text(encoding="utf-8-sig"))
        name = config["class_name"]
        if name in CLASSES:
            assert name not in configs, name
            configs[name] = (path, config)
    assert set(configs) == set(CLASSES), set(CLASSES) - set(configs)
    records = regression.load_metadata(METADATA)
    selected = {name: [r for r in records if r.get("type") == name and r.get("split") == "test"]
                for name in CLASSES}
    for name, rows in selected.items():
        assert len(rows) == 200, (name, len(rows))
        assert len({r["file"] for r in rows}) == 200
    inputs = [SCRIPTS / "main_reg.py", SCRIPTS / "canonical_orientation.py", METADATA]
    inputs += [configs[name][0] for name in CLASSES]
    before = {str(path): sha(path) for path in inputs}
    manifest = {
        "status": "running", "started_local": datetime.now().isoformat(timespec="seconds"),
        "task": "CPU geometry orientation only; no neural network inference or translation/shape fit evaluation",
        "main_reg": str(SCRIPTS / "main_reg.py"), "canonical_orientation": canonical.__file__,
        "constructor_signature": str(inspect.signature(regression.ParamRegressionDataset)),
        "data_root": str(DATA), "metadata": str(METADATA), "classes": CLASSES,
        "expected_test_per_class": 200, "expected_total": 2600,
        "disabled": ["model construction", "weight loading", "normals", "normal precomputation", "point cache"],
        "only_replaced_import": "model_ptv3.PTv3Regressor (unused; raises if constructed)",
        "geometry_instrumentation": "sys.setprofile observes real canonical_orientation returns without replacing any geometry or loader function",
        "axis_error": "Dataset diagnostic: full 360-degree direction difference for LHouse; modulo 180-degree axis difference for other supported classes",
        "roof_wrong_axis_rule": "For ridge-axis supported roof classes, axis_error_deg > 45 means nearer perpendicular than correct axis; >= 80 flags within 10 degrees of 90",
        "skips": "Dataset acceptance is recorded separately. SKIPPED_CLASS is neither successful rotation nor geometry failure. Four round classes have yaw-invariant geometry; TwoGableHouses is outside the rotation whitelist.",
        "raw_return_omission": "aligned_points array omitted from captured function returns; raw dataset orientation_diagnostics preserved verbatim",
        "input_sha256_before": before,
    }
    write_json(OUT / "manifest.json", manifest)
    summary, failures, wrong_axes = [], [], []
    for name in CLASSES:
        class_started = time.perf_counter()
        config_path, config = configs[name]
        kwargs = dict(
            data_root=str(DATA), metadata_path=str(METADATA), class_name=name, split="test",
            targets=config["targets"], num_points=int(config.get("num_points", 2048)),
            train=False, normalize=bool(config.get("normalize_points", True)),
            jitter_sigma=0.0, random_rotate=False, aux_names=list(config.get("aux_names", [])),
            use_normals=False, geom_aux_targets=list(config.get("geom_aux_targets", [])),
            target_mode=config.get("target_mode", "raw"),
            canonical_align=bool(config.get("canonical_align", False)), orientation_source="geometry",
            lhouse_canonical=bool(config.get("lhouse_canonical", False)),
            canonical_aux_units=config.get("canonical_aux_units", "legacy"),
            canonical_bbox_mode=config.get("canonical_bbox_mode", "full"), cache_points=False,
        )
        supported = kwargs["canonical_align"] and (
            name in canonical._ROTATION_ENABLE_CLASSES or (name == "LHouse" and kwargs["lhouse_canonical"]))
        returns = {}
        geometry_code = canonical.canonical_orientation.__code__
        constructor_code = regression.ParamRegressionDataset.__init__.__code__

        def capture(frame, event, result):
            if event == "return" and frame.f_code is geometry_code:
                caller = frame.f_back
                assert caller.f_code is constructor_code
                rel = caller.f_locals["rel"]
                assert rel not in returns
                returns[rel] = scalar_return(result)

        assert sys.getprofile() is None
        if supported:
            sys.setprofile(capture)
        try:
            dataset = regression.ParamRegressionDataset(**kwargs)
        finally:
            if supported:
                sys.setprofile(None)
        assert not dataset._points_cache and not dataset._normals_cache and not dataset.use_normals
        if not supported:
            assert not returns and not dataset.orientation_diagnostics
            # The constructor intentionally bypasses these classes. Read every real cloud
            # and call the real function to record the explicit skip, not a success.
            for record in selected[name]:
                rel = record["file"].replace("\\", "/")
                points = regression.load_txt_points(DATA / rel)
                returns[rel] = scalar_return(canonical.canonical_orientation(points, name, kwargs["lhouse_canonical"]))
        assert len(returns) == 200, (name, len(returns))
        dataset_diags = {row["file"]: row for row in dataset.orientation_diagnostics}
        if supported:
            assert set(returns) == set(dataset_diags)
        audit_rows = []
        for record in selected[name]:
            rel = record["file"].replace("\\", "/")
            returned = returns[rel]
            diag = dataset_diags.get(rel)
            skip = returned["status"] == "SKIPPED_CLASS"
            accepted = bool(diag and diag["applied"] and returned["reliable"] and not skip)
            failed = not skip and not accepted
            error = float(diag["axis_error_deg"]) if diag and "axis_error_deg" in diag else None
            assert error is None or np.isfinite(error)
            audit = {"file": rel, "status": returned["status"], "accepted": accepted,
                     "failed": failed, "skipped": skip, "axis_error_deg": error,
                     "reference_metadata_yaw_deg": canonical.canonical_angle_from_metadata(record, name, kwargs["lhouse_canonical"]),
                     "roof_wrong_axis_gt45": bool(name in RIDGE and error is not None and error > 45.0),
                     "roof_near90_ge80": bool(name in RIDGE and error is not None and error >= 80.0),
                     "canonical_return": returned}
            audit_rows.append(audit)
            if failed:
                failures.append({"class_name": name, "file": rel, "status": returned["status"], "reason": returned["reason"]})
            if audit["roof_wrong_axis_gt45"]:
                wrong_axes.append({"class_name": name, "file": rel, "axis_error_deg": error,
                                   "near90_ge80": audit["roof_near90_ge80"], "reason": returned["reason"]})
        errors = [row["axis_error_deg"] for row in audit_rows if row["accepted"]]
        skipped = sum(row["skipped"] for row in audit_rows)
        row = {"class_name": name, "test_total": 200, "actual_geometry_calls": len(returns),
               "dataset_accepted": len(dataset), "accepted": sum(r["accepted"] for r in audit_rows),
               "failed": sum(r["failed"] for r in audit_rows), "skipped": skipped,
               "skip_category": ("rotation_symmetric" if name in ROUND else "not_whitelisted") if skipped else "",
               "axis_error_count": len(errors), "axis_error_mean_deg": float(np.mean(errors)) if errors else None,
               "axis_error_p95_deg": float(np.percentile(errors, 95)) if errors else None,
               "axis_error_max_deg": float(np.max(errors)) if errors else None,
               "weak_verdict_count": sum(r["canonical_return"]["verdict"] == "WEAK" and r["accepted"] for r in audit_rows),
               "roof_wrong_axis_gt45_count": sum(r["roof_wrong_axis_gt45"] for r in audit_rows) if name in RIDGE else None,
               "roof_near90_ge80_count": sum(r["roof_near90_ge80"] for r in audit_rows) if name in RIDGE else None,
               "canonical_align": kwargs["canonical_align"], "lhouse_canonical": kwargs["lhouse_canonical"],
               "canonical_aux_units": kwargs["canonical_aux_units"], "canonical_bbox_mode": kwargs["canonical_bbox_mode"],
               "elapsed_seconds": round(time.perf_counter() - class_started, 3)}
        assert row["accepted"] + row["failed"] + row["skipped"] == 200
        payload = {"class_name": name, "config_path": str(config_path), "config_sha256": before[str(config_path)],
                   "config_snapshot": config, "constructor_kwargs": kwargs, "summary": row,
                   "dataset_orientation_diagnostics_raw": dataset.orientation_diagnostics,
                   "status_counts": dict(Counter(r["status"] for r in audit_rows)), "samples": audit_rows}
        write_json(OUT / (name + "_diagnostics.json"), payload)
        summary.append(row)
        print(json.dumps(row, ensure_ascii=False), flush=True)
        del dataset

    with (OUT / "summary.csv").open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    write_json(OUT / "failures.json", failures)
    write_json(OUT / "roof_wrong_axes.json", wrong_axes)
    after = {str(path): sha(path) for path in inputs}
    assert before == after, "Input source/config/metadata changed during audit"
    manifest.update(status="complete", elapsed_seconds=round(time.perf_counter() - started, 3),
                    actual_test_clouds=2600, actual_geometry_calls=sum(r["actual_geometry_calls"] for r in summary),
                    accepted=sum(r["accepted"] for r in summary), failed=len(failures),
                    skipped=sum(r["skipped"] for r in summary),
                    roof_wrong_axis_gt45_count=len(wrong_axes),
                    roof_near90_ge80_count=sum(r["near90_ge80"] for r in wrong_axes),
                    input_hashes_unchanged=True, input_sha256_after=after)
    write_json(OUT / "manifest.json", manifest)
    lines = ["# v8 CPU 几何朝向补查（2026-10-26）", "",
             f"实际读取并调用真实几何函数：13 类 × 200 = 2600 份。接受朝向 {manifest['accepted']}，失败 {len(failures)}，明确跳过 {manifest['skipped']}。耗时 {manifest['elapsed_seconds']:.1f} 秒。", "",
             "这只检查朝向预处理，不运行网络、不估计预测精度，也不评价平移定位或形状贴合。metadata 朝向仅用于计算角度误差；几何函数只接收真实点云、类别和 L 形开关。", "",
             "按 v8 配置保留 canonical_align、lhouse_canonical、辅助单位、bbox_mode、目标转换及归一化；关闭法向、法向预计算和点云缓存。仅用进程内未使用的模型导入占位绕开模型依赖；所有几何函数和点云读取函数均为仓库原实现。", "",
             "accepted 指算法标记 reliable 且数据集接受，不能等同于角度正确。SKIPPED_CLASS 单列，不计为旋转成功或失败。dataset_accepted 是构造器保留的样本数，含跳过旋转的类别。", "",
             "误差沿用数据集实现：L 形为完整方向误差，其余支持类按180°等价轴计；P95只统计成功返回角度的样本。屋顶错轴定义为误差>45°，其中≥80°为距90°不超过10°。L形180°错误不应按无向轴消去。", "",
             "|类别|接受|失败|跳过|角度均值°|P95°|屋顶错轴>45°|近90°≥80°|", "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for row in summary:
        fmt = lambda value: "—" if value is None else f"{value:.3f}"
        lines.append(f"|{row['class_name']}|{row['accepted']}|{row['failed']}|{row['skipped']}|{fmt(row['axis_error_mean_deg'])}|{fmt(row['axis_error_p95_deg'])}|{row['roof_wrong_axis_gt45_count'] if row['roof_wrong_axis_gt45_count'] is not None else '—'}|{row['roof_near90_ge80_count'] if row['roof_near90_ge80_count'] is not None else '—'}|")
    lines += ["", "明确跳过：Cylinder、ConeCylinder、CylinderDome、FourStageRoundTower 为绕竖直轴旋转不改变形状的类别；TwoGableHouses 为非白名单类，其方向不能据此宣称通过。", "", "全部失败文件："]
    lines += [f"- `{row['file']}`：{row['reason']}" for row in failures] or ["- 无"]
    lines += ["", f"屋顶错轴共 {len(wrong_axes)} 份，其中近90° {manifest['roof_near90_ge80_count']} 份；完整文件和原因见 roof_wrong_axes.json。", "",
              "逐类 JSON 保留原始 dataset.orientation_diagnostics，并附真实几何函数返回的标量/矩阵诊断（省略 aligned_points 数组）。summary.csv 为逐类汇总，failures.json 为全部失败清单。manifest.json 记录源码、metadata 和13份配置的前后哈希；未修改输入文件，未读取权重、联网、SSH或操作git。"]
    (OUT / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("AUDIT_COMPLETE " + str(OUT), flush=True)


if __name__ == "__main__":
    main()
