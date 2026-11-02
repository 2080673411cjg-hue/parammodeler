"""Summarize the remote sample without mixing extra failures into balanced metrics."""
import csv
import json
from pathlib import Path
import statistics
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "evaluations/v8_remote_tailscale_20261026"
LOGS = Path(r"E:\pointnet\ptv3_simple\logs")
DATA = Path(r"E:\pointnet\datasets_aug")
sys.path.insert(0, str(ROOT / "scripts"))
from train_ptv3_reg import RECIPES
from compare_ptv3_reg import read_json, write_csv


def main():
    manifest = read_json(OUT / "manifest.json")
    if manifest["status"] != "complete":
        raise SystemExit("Remote inference is not finished")
    records = [row for row in read_json(DATA / "metadata/sample_params.json")
               if row.get("split") == "test"]
    responses = {row["file"]: row for path in (OUT / "responses").glob("*/*.json")
                 if (row := read_json(path))}
    assert len(responses) == manifest["planned"]
    balanced = set()
    summaries = []
    lines = ["# v8 接入及真实远程补测（2026-10-26）", "",
             "十三类均已核对真实 /regress 响应的 model_log_dir 为对应 v8 目录。请求显式传入 _v8_normals；远程服务当时的默认后缀仍为 v4，默认值不代表本次实际模型。", "",
             "样本为每类小、中、大各10份，共390份；另外加入全部CPU朝向异常和5份既有L形样本，去重后401份。两类结果分开统计，额外异常不会混入分尺度抽样均值。此处不冒充完整2600份网络测试。", "",
             "全部2600份CPU朝向补查另见 ../v8_orientation_cpu_20261026/summary.md；HTTP成功返回也不能代表全部形状贴合通过。", "",
             "|类别|分尺度样本成功/计划|MAE：参数名与实际远程误差|", "|---|---:|---|"]
    for name, stem, _, _ in RECIPES:
        selected = set()
        for band in (0, 1, 2):
            group = sorted([row for row in records if row["type"] == name and
                            row["horizontalScaleBand"] == band], key=lambda row: row["file"])
            selected.update(group[index]["file"] for index in np.linspace(0, len(group)-1, 10, dtype=int))
        balanced.update(selected)
        good = [responses[key] for key in sorted(selected) if responses[key]["ok"]]
        with (LOGS / f"ptv3_reg_{stem}_v8_normals/regression_predictions_test.csv").open(encoding="utf-8-sig", newline="") as stream:
            metadata = {"/".join(row["file"].replace("\\", "/").split("/")[-3:]): row
                        for row in csv.DictReader(stream)}
        targets = read_json(LOGS / f"ptv3_reg_{stem}_v8_normals/regression_config.json")["targets"]
        display = []
        for target in targets:
            errors = [abs(row["prediction"][target] - row["truth"][target]) for row in good]
            old_errors = [abs(float(metadata[row["file"]]["pred_"+target]) -
                              float(metadata[row["file"]]["true_"+target])) for row in good]
            mae = statistics.mean(errors) if errors else None
            summaries.append({"class": name, "target": target, "expected": len(selected),
                              "accepted": len(good), "failed": len(selected)-len(good),
                              "v8_metadata_mae_same_files": statistics.mean(old_errors) if old_errors else None,
                              "remote_geometry_mae": mae,
                              "remote_geometry_p95": float(np.percentile(errors, 95)) if errors else None})
            display.append(f"{target}={mae:.5f}" if mae is not None else f"{target}=无有效预测")
        lines.append(f'|{name}|{len(good)}/{len(selected)}|'+"；".join(display)+"|")
    assert len(balanced) == 390
    write_csv(OUT / "stratified_summary.csv", summaries)
    failures = [row for row in responses.values() if not row["ok"]]
    success = sum(row["ok"] for row in responses.values())
    extra = sorted(set(responses)-balanced)
    lines += ["", f"全部HTTP补测：{len(responses)}份，{success}份成功返回，{len(failures)}份失败；分尺度390份之外另有{len(extra)}份。",
              f'耗时 {manifest["elapsed_seconds"]/60:.1f} 分钟，单请求中位数 {manifest["request_median_seconds"]:.2f} 秒，两并发。', "",
              "尺寸单位为米，比例和曲线系数无量纲，角度为度。stratified_summary.csv 将同一批文件在已知metadata朝向下的v8误差与真实远程几何朝向误差并列；两者使用各自相同坐标约定的真值，不能把不同坐标的参数直接相减。单云API与原批量测试的批次及随机状态不同，小幅差异不单独归因于朝向处理。", "",
              "**额外异常与失败**", ""]
    outliers = read_json(ROOT / "evaluations/v8_orientation_cpu_20261026/axis_outliers_gt10.json")
    for anomaly in outliers:
        row = responses[anomaly["file"]]
        params = row.get("prediction", {})
        targets = [target for target in ("ridgeRatio", "ridgeLength", "length", "width", "wallHeight") if target in params]
        errors = "; ".join(f'{target}: true={row["truth"][target]:.5f}, pred={params[target]:.5f}' for target in targets)
        lines.append(f'- `{row["file"]}`：CPU轴误差{anomaly["axis_error_deg"]:.3f}°；HTTP成功={row["ok"]}；{errors}')
    for row in failures:
        lines.append(f'- `{row["file"]}`：{row["error"]}')
    lines += ["", "所有成功响应均已验证实际v8目录；canonical_alignment.applied只代表流程实施了规范旋转，不保证旋转角正确。近正方形长宽轴交换可能几何等价，屋顶的90°错轴则须结合屋脊方向判定。", "",
              "本轮只接入版本并测评，没有修改朝向算法、参数预测、几何校正、训练数据或旧权重，也没有编译插件DLL或重启Ubuntu服务。", "",
              "后续优先处理：非对称人字形/半圆柱的少数错轴，L形sample_00978的判向失败；双人字形未参与canonical判向，不能把SKIPPED_CLASS误认为方向正确。"]
    (OUT / "summary.md").write_text("\n".join(lines)+"\n", encoding="utf-8")
    (OUT / "sampling.json").write_text(json.dumps({"stratified_files": sorted(balanced),
        "additional_files": extra}, indent=2), encoding="utf-8")
    print(json.dumps({"planned": len(responses), "succeeded": success, "failed": len(failures),
                      "stratified": len(balanced), "additional": len(extra),
                      "report": str(OUT / "summary.md")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
