# Deep Learning Pipeline 完整日志

> 最后更新: 2026-08-06  
> 插件版本: **v2.3.2**（TwoGable 回归增强 + 角度映射修复 + 3D 场景清除）  
> 当前模型: **PCT**（Point Cloud Transformer）— Li & Shan 2025 风格 offset-attention  
> 分类模型: `pct_cls_v2` — 98.92% F1，8/14 类满分  
> 回归模型: 13 类（TriPrismPyramid 无需回归），`pct_reg_*_v2` (basic) + `pct_reg_*_v2_neighbor` (neighbor)，混合部署  
> 数据集: **500 样本/类**（train 400 + val 50 + test 50），TwoGableHouses **1000 样本**（仅扩充数据量，无额外增强），14 类共 7500 样本  
> 后端: PCT（PointNeXt 保留但不再使用）

---

## 一、整体流程图

```
┌────────────────────────────────────────────────────────────────────┐
│  1. 数据生成 (QGIS 插件)                                            │
│     randomize params → BuildMesh → 表面采样 → 旋转 rz → 归一化     │
│     → 输出 TXT + sample_params.json                                │
├────────────────────────────────────────────────────────────────────┤
│  2. 数据增强 (外部脚本，Ubuntu)                                      │
│     用户自行处理（旋转增强、jitter 等）                              │
├────────────────────────────────────────────────────────────────────┤
│  3. 训练 (Ubuntu, Python)                                          │
│     main.py (分类) / main_reg.py (回归)                             │
│     → best_model.pth + regression_config.json + 评估 CSV            │
├────────────────────────────────────────────────────────────────────┤
│  4. 部署到插件                                                      │
│     复制 logs/ 目录到 Windows → 插件 QProcess 调用 Python 预测       │
│     → 结果写回 UI → auto-align 平移 → 3D 预览                       │
└────────────────────────────────────────────────────────────────────┘
```

---

## 二、数据生成（插件端）

### 入口函数

| 功能 | 函数 | 文件 |
|------|------|------|
| 批量生成全部 14 类 | `onExportDLDatasetClicked()` | `parammodeler_dock.cpp:1748` |
| 生成当前单个基元 | `onExportCurrentPrimitiveDLDatasetClicked()` | `parammodeler_dock.cpp:1893` |
| 导出单样本 TXT | `ExportPointCloud::exportDLInputTXT()` | `exportpointcloud.cpp:267` |

### 参数随机化

`randomizeCurrentPrimitiveParams(false, true)` — `parammodeler_dock.cpp:685`

- 形状参数：各类各自随机范围（如 Cuboid length 6-18m）
- **Pose（仅 rz）**：Omega=0, Phi=0, Kappa=random(-180, 180)，tx=ty=tz=0

```cpp
// 第 826-833 行
ui->spinBoxROmega->setValue( 0.0 );
ui->spinBoxRPhi->setValue( 0.0 );
ui->spinBoxRKappa->setValue( rnd( -180.0, 180.0, 1.0 ) );
ui->spinBoxTX->setValue( 0.0 );
ui->spinBoxTY->setValue( 0.0 );
ui->spinBoxTZ->setValue( 0.0 );
```

### 点云生成流程

`exportpointcloud.cpp:92-187` — `sampleCurrentPrimitive()`

```
BuildMesh::build(primitiveType, dock)
    ↓
按面积加权随机采样三角面 → 2048 个点
    ↓  跳过底面（法向量 n.z < -0.7 的三角面）
    ↓  顶面 ~40%，侧面 ~60%
    ↓
applyPose(p, 0,0,0, 0,0,rz)   // 绕 Z 旋转点云
    ↓
normalizeForDL(points)          // 中心化 + 最大半径归一化到单位球
    ↓
写入 TXT（每行 x y z，8 位小数）
```

### 归一化公式

```
center = mean(points)
maxRadius = max(||p - center||)
p_normalized = (p - center) / maxRadius
```

### 元数据结构

写入 `{datasetsBaseDir}/metadata/sample_params.json`：

```json
{
  "file": "train/Cuboid/sample_00001.txt",
  "type": "Cuboid",
  "split": "train",
  "pointCount": 2048,
  "params": {
    "length": 12.5, "width": 7.3, "height": 5.1,
    "rz": 45.0
  },
  "pointCloudInfo": {
    "bboxMin": [-0.8, -0.5, -0.3],
    "bboxMax": [0.8, 0.5, 0.7],
    "bboxSize": [1.6, 1.0, 1.0],
    "center": [0.0, 0.0, 0.2],
    "scale": 1.0
  }
}
```

参数命名由 `currentPrimitiveParamsObject()` (`parammodeler_dock.cpp` 约 1600-1745 行) 决定。

### 数据集目录结构

```
E:/pointnet/datasets_aug/
├── train/
│   ├── Cuboid/sample_00001.txt ... sample_00400.txt
│   ├── Cylinder/...
│   └── ...（14 类）
├── val/
│   └── ...（每类 50 样本）
├── test/
│   └── ...（每类 50 样本）
└── metadata/
    ├── class_names.txt
    └── sample_params.json
```

训练集 : 验证集 : 测试集 = **8 : 1 : 1**（每类 500 样本：400 + 50 + 50）

---

## 三、训练（Ubuntu, Python）

### 路径约定（训练时）

| 项 | 路径 |
|----|------|
| **PCT 代码** | `E:/pointnet/pct_simple/` |
| 分类入口 | `main.py` → `PCTClassifier` |
| 回归入口 | `main_reg.py` → `PCTRegressor` / `PCTNeighborRegressor` |
| 数据集根目录 | `--data_root E:/pointnet/datasets_aug` |
| 元数据 | `--metadata E:/pointnet/datasets_aug/metadata/sample_params.json` |
| PointNeXt 代码 (legacy) | `E:/pointnet/pointnext_simple/` |

### PCT 分类训练

```bash
cd /home/xubo/pointnet/pct_simple
source /home/xubo/pointnet_envs/pointnet_gpu/bin/activate
python main.py --mode train \
  --data_root /home/xubo/pointnet/datasets_aug \
  --log_dir logs/pct_cls_v2 \
  --num_points 1024 --epochs 80 --batch_size 32 --lr 5e-4 --train_jitter_sigma 0.005
```

输出到 `logs/pct_cls_v2/`：
- `best_model.pth`（~1.48M 参数）
- `classes.txt`（14 类名）
- `classification_report_test.csv`
- `confusion_matrix_test.csv`

### PCT 回归训练（2 变体 × 14 类 = 26 个模型）

```bash
# basic 变体 — 全局 offset-attention
python main_reg.py --mode train \
  --data_root /home/xubo/pointnet/datasets_aug \
  --metadata /home/xubo/pointnet/datasets_aug/metadata/sample_params.json \
  --class_name Cuboid --targets length width height \
  --log_dir logs/pct_reg_cuboid_v2 \
  --num_points 2048 --epochs 100 --batch_size 16 --lr 1e-3 \
  --aux_features bbox_x bbox_y bbox_z scale \
  --pct_variant basic --pct_d_model 256 --pct_heads 4 --pct_blocks 4

# neighbor 变体 — offset-attention + kNN 局部增强
python main_reg.py --mode train \
  ... \
  --log_dir logs/pct_reg_cuboid_v2_neighbor \
  --pct_variant neighbor --pct_d_model 256 --pct_heads 4 --pct_blocks 4
```

**PCT 特有超参：**

| 参数 | 含义 |
|------|------|
| `--pct_variant` | `basic`（全局 attention）或 `neighbor`（+kNN 局部图） |
| `--pct_d_model` | 特征维度，默认 256 |
| `--pct_heads` | 多头注意力头数，默认 4 |
| `--pct_blocks` | offset-attention 层数，默认 4 |
| `--pct_dropout` | Dropout，默认 0.1 |

**通用参数：**（与 PointNeXt 训练相同，见之前文档）

### 回归输出文件

```
logs/pct_reg_{primitive}_v2{_neighbor}/
├── best_model.pth              # 模型权重（~1.5M 参数，PointNeXt 的 1/3）
├── regression_config.json      # 训练配置 + target/aux 的 mean/std
├── train_history.csv           # epoch, train_loss, val_mae_mean, val_rmse_mean
├── regression_metrics_test.csv # target, mae, rmse, mape_percent, r2
├── regression_errors_test.csv  # 每个测试样本的误差
├── regression_predictions_test.csv  # 预测值 vs 真值
├── regression_report_test.csv  # 汇总报告
├── regression_metrics_val.csv  # 同上（验证集）
├── regression_errors_val.csv
├── regression_predictions_val.csv
└── regression_report_val.csv
```

### 当前已训练的模型

| 实验 | 目录 | 说明 |
|------|------|------|
| **PCT 分类 v2** | `pct_cls_v2` | **当前使用**，98.92% F1 |
| **PCT 回归 basic v2 ×13** | `pct_reg_*_v2` | basic 变体，3 类最优 |
| **PCT 回归 neighbor v2 ×13** | `pct_reg_*_v2_neighbor` | neighbor 变体，10 类最优 |
| PointNeXt 分类 v2 | `pointnext_cls_v2` | 旧实验，79.9% F1（**不用**） |
| PointNeXt 回归 aux ×13 | `pointnext_reg_*_aux` | 旧实验（**不用**） |
| PointNeXt 回归 rot ×13 | `pointnext_reg_*_rot` | 旧实验（**不用**） |

---

## 四、插件调用预测

### 模块职责

| 文件 | 职责 |
|------|------|
| `parammodeler_config.h/cpp` | 可配置路径（pythonExe, pointnetBaseDir, datasetsBaseDir） |
| `parammodeler_pointnet.h/cpp` | QProcess 调用 Python 脚本，解析 JSON 输出 |
| `parammodeler_dock.cpp` | UI 交互：分类按钮 → 回归按钮 → 结果写回 UI → auto-align |

### 分类调用链

```
用户点击 "PointNet Classify"
    ↓
onPointNetClassify() - dock.cpp:2503
    ↓
PointNetRunner::predict(inputTxt, 2048, topK=3)
    ↓
backendConfig(PCT)
    → script: pct_simple/main.py
    → logDir: pct_simple/logs/pct_cls_v2
    ↓
QProcess: python main.py --mode predict --input X --log_dir Y
         --num_points 2048 --topk 3 --cpu
    ↓
JSON 输出 → 解析 top1 → 自动切换 comboPrimitive
```

### 回归调用链

```
用户点击 "Inverse Params"
    ↓
onInverseParams() - dock.cpp:2552
    ↓
PointNetRunner::predictParams(inputTxt, primitiveType, 2048)
    ↓
regressionConfig(PCT, primitiveType)
    → script: pct_simple/main_reg.py
    → logDir: pct_simple/logs/pct_reg_{primitive}_v2{_neighbor}  (per-class variant)
    ↓
QProcess: python main_reg.py --mode predict
         --input X --log_dir Y --num_points 2048 --cpu
         --data_root ... --metadata ...
         --bbox_x .. --bbox_y .. --bbox_z .. --scale ..
    ↓
JSON 输出 → pointNetParamsToUiParams() → PointNetRunner::applyToUI()
    ↓
auto-align: 计算 pcCenter - modelCenter → setPoseTranslate(tx,ty,tz)
```

### 插件传给 Python 的参数（预测模式）

Python 预测脚本 (`main_reg.py`) 的处理：
1. 读取 `--input` 点云，自动 `normalize_points()`（中心化+最大半径归一化）
2. 读取 `--log_dir/regression_config.json`，获取 target_mean/std、aux_mean/std
3. 用 aux_mean/std 归一化 bbox/size 输入，用 target_mean/std 反归一化输出
4. 输出 JSON: `{"class": "Cuboid", "params": {"length": 12.5, "width": 7.3, ...}}`

### 网络输出 → UI 参数映射

`pointNetParamsToUiParams()` — `parammodeler_dock.cpp:117`

网络输出的参数名和 UI 控件名**不完全一致**，需要转换：

| 网络输出 | UI 控件 | 说明 |
|----------|---------|------|
| `length`, `width`, `height` | 同名 | Cuboid 直接映射 |
| `totalHeight` + `wallRatio` | `*WallHeight` + `*RoofHeight` | 所有带屋顶的基元：`totalHeight * wallRatio` / `totalHeight * (1-wallRatio)` |
| `totalHeight` + `cylinderRatio` | `*CylHeight` + `*UpperHeight` | ConeCylinder / CylinderDome |
| `radius` + `height` | `*Radius` / `cylHeight` | Cylinder |
| `mainLength`, `mainWidth`, `wingLength`, `wingWidth` | `lMainL`, `lMainW`, `lWingL`, `lWingW` | LHouse |

### Auto-Align（平移对齐）— v2.3.0 精简

`alignModelToPointCloud()` — `parammodeler_dock.cpp:154`（共用函数，`onInverseParams` 和对话框均调用）

```
modelCenter = meshBboxCenter(mesh)   // v2.2.0 起 mesh 已居中，X/Y ≈ 0
tx = metadataCenter.x - modelCenter.x
ty = metadataCenter.y - modelCenter.y
tz = metadataCenter.z - modelCenter.z
setPoseTranslate(tx, ty, tz)
```

注意：**只对齐平移，不处理旋转**。因为模型不预测 rz，模型朝向保持默认。
v2.2.0 坐标系居中后 modelCenter X/Y ≈ 0，tx/ty 直接等于点云中心的 X/Y。

---

## 五、路径配置

### 可配置路径（QgsSettings）

| 设置项 | 默认值 | 说明 |
|--------|--------|------|
| `parammodeler/pythonExe` | `E:/mambaforge/envs/pointnet_train/python.exe` | Python 解释器 |
| `parammodeler/pointnetBase` | `E:/pointnet` | PointNet 根目录 |
| `parammodeler/datasetsBase` | `E:/pointnet/datasets_aug` | 数据集根目录 |

### 派生路径（自动计算）

```
分类脚本: {pointnetBase}/pct_simple/main.py
回归脚本: {pointnetBase}/pct_simple/main_reg.py
分类日志: {pointnetBase}/pct_simple/logs/pct_cls_v2
回归日志: {pointnetBase}/pct_simple/logs/pct_reg_{primitive}_v2{_neighbor}
         (CylinderDome/HalfCylinderRoof/LHouse → _v2, 其余 10 类 → _v2_neighbor)
元数据:   {datasetsBase}/metadata/sample_params.json
数据根:   {datasetsBase}
```

设置对话框：`ParamModelerConfig::showSettingsDialog()` (`parammodeler_config.cpp:131`)

---

## 六、关键文件速查表

| 文件 | 行数 | 核心内容 |
|------|------|----------|
| `parammodeler_dock.cpp` | 2642 | 数据生成入口 (1748/1893)、参数映射 (117)、分类 (2503)、回归 (2552)、auto-align (2589) |
| `exportpointcloud.cpp` | ~300 | 表面采样 (92)、归一化 (169)、DL TXT 导出 (267) |
| `parammodeler_pointnet.cpp` | 471 | 分类预测 (301/306)、回归预测 (383/390)、模型目录映射 (92/107) |
| `parammodeler_config.cpp` | 219 | 路径设置、设置对话框 |
| `parammodeler_config.h` | 41 | 配置接口声明 |
| `parammodeler_pointnet.h` | 55 | 数据结构 (PointNetPrediction, PointNetRegressionResult) |

---

## 七、当前模型质量速查

> **数据规模**: 500 样本/类（400 train + 50 val + 50 test），14 类共 6500 样本  
> **模型**: PCT (Point Cloud Transformer)，~1.48M 参数（分类）/ ~1.5M 参数（回归）  
> **变体部署策略**: 10 类用 neighbor，3 类用 basic

### 分类 (pct_cls_v2) — 🟢 基本毕业

| 指标 | PointNeXt `cls_v2` | **PCT `cls_v2`** | 提升 |
|------|:---:|:---:|:---:|
| Overall F1（macro avg） | 79.9% | **98.92%** | **+19%** |
| 满分（100%）类 | ~4/13 | **8/13** | 翻倍 |
| 总错误数（/650） | ~130 | **4** | 97% 减少 |

**逐类 F1：**

| 类 | PointNeXt | PCT |
|----|:---:|:---:|
| Cuboid | ~70% | **100%** ✅ |
| Cylinder | ~80% | **100%** ✅ |
| CylinderDome | ~80% | **100%** ✅ |
| FourStageRoundTower | ~85% | **100%** ✅ |
| HalfCylinderRoof | ~90% | **100%** ✅ |
| IndentedCuboid | ~70% | **100%** ✅ |
| LHouse | ~80% | **100%** ✅ |
| PyramidRoof | ~80% | **100%** ✅ |
| TruncatedPyramidRoof | ~75% | **100%** ✅ |
| AsymmetricGableHouse | ~78% | 97.96% |
| ConeCylinder | ~80% | 98.04% |
| TwoGableHouses | ~70% | 95.05% |
| GabledRoof | ~65% | 94.95% |

**残留混淆（仅 4/650 错误）：**
- AsymmetricGableHouse：1→GabledRoof、1→TwoGableHouses
- GabledRoof：1→ConeCylinder、2→TwoGableHouses
- TwoGableHouses：1→ConeCylinder、1→GabledRoof

PointNeXt 时代的主混淆（Cuboid↔Cylinder↔IndentedCuboid）**全部消除**。

### 回归 — PCT 2 变体混合部署

**变体选择：neighbor 10/13 胜出**

| 类 | 参数数 | basic avg R² | neighbor avg R² | 部署变体 |
|----|:---:|:---:|:---:|:---:|
| Cylinder | 2 | 0.985 | **0.989** | neighbor |
| ConeCylinder | 3 | 0.743 | **0.953** | neighbor |
| HalfCylinderRoof | 4 | **0.844** | 0.729 | **basic** |
| Cuboid | 3 | 0.697 | **0.778** | neighbor |
| GabledRoof | 4 | 0.718 | **0.739** | neighbor |
| PyramidRoof | 4 | 0.334 | **0.625** | neighbor |
| CylinderDome | 4 | **0.584** | 0.520 | **basic** |
| AsymmetricGableHouse | 6 | 0.328 | **0.468** | neighbor |
| LHouse | 5 | **0.414** | 0.389 | **basic** |
| TruncatedPyramidRoof | 6 | 0.323 | **0.415** | neighbor |
| FourStageRoundTower | 6 | 0.014 | **0.414** | neighbor |
| IndentedCuboid | 8 | 0.172 | **0.316** | neighbor |
| TwoGableHouses | 7 | 0.014 | **0.090** | neighbor |

**分档：**

| 可用 (avg R²>0.6) | 部分可用 (0.3-0.6) | 🔴 困难 (<0.3) |
|---|---|---|
| Cylinder (0.989) | PyramidRoof (0.625) | **IndentedCuboid** (0.316) |
| ConeCylinder (0.953) | CylinderDome (0.584) | |
| HalfCylinderRoof (0.844) | AsymmetricGableHouse (0.468) | |
| Cuboid (0.778) | TruncatedPyramid (0.415) | |
| GabledRoof (0.739) | FourStageRoundTower (0.414) | |
| **TwoGableHouses (0.811)** | LHouse (0.414) | |

### 🔴 历史灾难参数 — PCT vs PointNeXt

| 参数 | PointNeXt R² | PCT 最优 | Δ | 评估 |
|---|---|---|---|---|
| bulge (CylinderDome) | -0.08 | -0.004 (basic) | ≈持平 | ❌ 依然困难 — 2048 点不足以捕捉曲率变化 |
| middleBulge (FourStage) | -0.85 | **-0.363** (neighbor) | **+0.49** | ✅ 大幅改善但仍负 — 数据量不足 |
| wallRatio (TwoGable) | 0.06 | -0.271 (basic) | -0.33 | ❌ 恶化 — 需排查过拟合 |
| innerWidth (IndentedCuboid) | 0.21 | 0.050 (neighbor) | -0.16 | ⚠️ 略降 — neighbor 勉强正 |
| ridgeLength (AsymmetricGable) | 0.52 | **0.608** (neighbor) | **+0.09** | ✅ 小幅改善 |

### 🔴 问题诊断

**IndentedCuboid 过拟合**：

| 类 | train loss | val loss | test loss | 诊断 |
|----|:---:|:---:|:---:|---|
| TwoGableHouses (neighbor) | — | — | — | ✅ **已解决**：扩量至 1000 样本，R² 0.090→0.811 |
| IndentedCuboid (neighbor) | 0.149 | 1.604 | 1.995 | 严重过拟合 |
| FourStageRoundTower (neighbor) | 0.130 | 0.236 | 0.293 | 正常 |

根因：500 样本不足以支撑 7-8 个参数的回归。TwoGable 扩量 1000 后已解决，IndentedCuboid 待扩量。

**bulge 参数（CylinderDome）** R² 三个模型（PointNeXt、PCT basic、PCT neighbor）都在 0 附近 —— 不是架构问题，而是 2048 个 XYZ 坐标点缺乏曲率信息。需要法向量通道。

### 后续改进方向

见 [第九章](#九模型升级路线图)。

---

## 八、修改指南

### 场景 1：新增基元类型

1. **插件端**：`parammodeler_dock.cpp` — 添加 UI 控件 + `randomizeCurrentPrimitiveParams()` 加随机化逻辑 + `pointNetParamsToUiParams()` 加映射
2. **数据生成**：重新跑 `onExportDLDatasetClicked()` 生成新数据
3. **训练**：复制 reg config 跑新的 PCT `main_reg.py --class_name NewPrimitive`（推荐 neighbor 变体）
4. **部署**：`parammodeler_pointnet.cpp:92` — `stemNames` 加新条目 + `pctBestSuffix` 可选添加变体偏好，`parammodeler_config.cpp` — 如有新脚本路径需更新

### 场景 2：更换模型版本 / 变体

通过设置对话框（或直接改代码）：
- 分类：修改设置对话框「分类模型名」（如 `pct_cls_v3`）
- 回归默认变体：修改设置对话框「PCT 回归默认后缀」（如 `_v3_neighbor` → 全部类默认用 v3 neighbor）
- 每类变体覆盖：修改 `parammodeler_pointnet.cpp:129` — `pctBestSuffix` 映射表控制例外类（当前 CylinderDome/HalfCylinder/LHouse 用 basic）
- 设置对话框路径：`设置 → PointNet 路径设置`

**注意**：改对话框设置后需重启插件生效。

### 场景 3：重新训练

PCT 重新训练命令参考 `scripts/train_pct_reg.sh` 和 `scripts/train_pct_cls.sh`：

```bash
# 回归 — neighbor 变体（推荐，10/14 类最优）
python main_reg.py --mode train \
  --data_root ... --metadata ... \
  --class_name Cuboid --targets length width height \
  --log_dir logs/pct_reg_cuboid_v3_neighbor \
  --num_points 2048 --epochs 100 --batch_size 16 --lr 1e-3 \
  --aux_features bbox_x bbox_y bbox_z scale \
  --pct_variant neighbor --pct_d_model 256 --pct_heads 4 --pct_blocks 4

# 对于 CylinderDome / HalfCylinder / LHouse，用 basic 变体
# --pct_variant basic
```

然后更新 `pctBestSuffix` map 指向新版本目录。

---


## 九、模型升级路线图

> ✅ **PCT (Li & Shan 2025) 已完成** — 分类 98.92% F1，回归 6/14 类 avg R^2>0.6  
> 🔴 剩余困难：bulge（曲率不敏感）、TwoGable/IndentedCuboid（过拟合）  
> 下一步优先级：数据端增强 > 法向量通道 > 架构升级

### 已完成

| 方案 | 状态 | 结果 |
|------|:---:|------|
| **PCT (Li & Shan 2025)** | ✅ 完成 | 分类 98.92%、回归 avg R^2 大幅改善（详见第七章） |
| basic + neighbor 双变体对比 | ✅ 完成 | neighbor 10/13 胜出，basic 3/13 胜出 |

### 下一步优先级

#### 🥇 优先级 1：数据端 — 法向量 + 增强 + 扩量（立即可做，无需重训模型架构）

**1a. 加入法向量通道**（解决 bulge R^2≈0 问题）：

```
当前: 每点 (x, y, z)  3 通道
改进: 每点 (x, y, z, nx, ny, nz)  6 通道

额外信息量: 法向量直接编码曲面弯曲程度 ->
             bulge/middleBulge 类参数的回归信号增强数倍
```

改动量：`exportpointcloud.cpp:92` — `sampleCurrentPrimitive()` 同时输出法向量 + PCT `main_reg.py` 增加通道数。

**1b. 数据增强（减少 TwoGable/IndentedCuboid 过拟合）：**

| 增强 | 当前状态 | 建议 |
|------|---------|------|
| 随机裁切 | 未用 | 模拟遮挡，裁掉 10-30% 点 -> 强制模型用局部线索推理 |
| Mixup / CutMix | 未用 | 点云 mixup 可提升泛化，特别适合多参数空间 |
| Jitter | 已用 sigma=0.003 | 扩大到 0.005-0.01 |

**1c. 扩量**：TwoGableHouses 和 IndentedCuboid 从 500->1000+ 样本/类（train loss 0.125 vs val loss 2.046 是教科书过拟合）。

#### 🥈 优先级 2：架构升级（如果数据端不解决问题）

| 候选 | 核心优势 | 匹配度 | 迁移难度 |
|------|---------|:---:|:---:|
| **Swin3D** (Microsoft, CVPR 2024) | cRSE 感知局部几何差异；预训练可用 | (5/5) | 中 |
| **Point Transformer V3** (Wu et al., 2024) | 推理极快；NoKSR backbone | (4/5) | 中 |
| **Equivariant Diffusion** (TPAMI 2025) | SO(3)等变；联合位姿+形状 | (3/5) | 高 |

**推荐首选 Swin3D**：
- 迁移量 ~200-300 行（替换 PCT 的 backbone）
- 保留 PCT 的 aux_features 和双变体机制
- 预训练权重可从 Structured3D 加载
- 代价：推理速度比 PCT 慢 30-50%

#### 🥉 优先级 3：分类+回归联合训练

合并到一个模型共享 backbone，两个 head 分别输出类别和参数。消除分类误差->回归误差的传导（当前 TwoGableHouses 分类 95% 还有 5% 分错，错分后回归结果无意义）。

### 评估基准（更新）

| 参数 | PointNeXt R^2 | PCT R^2 | 目标 R^2 | 备注 |
|------|:---:|:---:|:---:|---|
| bulge | -0.08 | -0.004 | >0.6 | 需法向量 |
| middleBulge | -0.85 | -0.363 | >0.5 | 已改善 0.49，需数据增强 |
| wallRatio | 0.06 | -0.271 | >0.7 | PCT 反而更差，过拟合 |
| innerWidth | 0.21 | 0.050 | >0.6 | 过拟合，需扩量 |
| ridgeLength | 0.52 | 0.608 | >0.8 | 小幅改善 |

---

## 十、插件版本变更日志

### v2.3.1 (2026-08-05) — 模型路径统一 v2 + PCT 超参修复 + 数据集追加模式

**模型路径**
- `regressionModelSuffix()` 默认值从 `_aux` 改为 `_v2`（4 处：config 默认值、设置对话框复位、.h 注释、pointnet.cpp 回退）
- 策略不变：分类 `pct_cls_v2`，回归默认 `_v2_neighbor`（10 类）+ `_v2`（CylinderDome / HalfCylinderRoof / LHouse）

**PCT 超参修复（main.py / main_reg.py）**
- `--pct_d_model` 默认值 384 → 256
- `--pct_heads` 默认值 8 → 4
- `--pct_blocks` 默认值 6 → 4
- 修复原因：checkpoint 用 256/4/4 训练，argparse 默认值被误改为 384/8/6，导致 `load_state_dict` 尺寸不匹配

**训练脚本修复（train_pct_cls.sh / train_pct_reg.sh / train_pct_all.sh）**
- PCT_D_MODEL/HEDS/BLOCKS 恢复为 256/4/4
- LOG_DIR 统一使用 v2 命名：`pct_cls_v2`、`pct_reg_*_v2`、`pct_reg_*_v2_neighbor`
- 三个脚本均添加 `export PYTHONUNBUFFERED=1`

**数据集生成：追加模式 + 单类替换**
- `generateFullDataset`：检测已有 `sample_params.json` → 显示各类现有数量 → 可选「追加」（只补差额，文件续号，metadata 合并）或「覆盖」（全删重建）
- `generateSinglePrimitiveDataset`：可选「替换」（删旧+重建+合并 metadata）或「追加」（保留旧数据，续号添加）
- 回归只覆盖 13 类：TriPrismPyramid 无需回归（仅分类），`parammodeler_pointnet.cpp` stemNames map 中不含

**TwoGableHouses 扩量 + 增强 + 遮挡修复**
- 单类追加 500 → 1000 样本（train 800 + val 100 + test 100）
- 增强：仅归一化+采样，不加孔洞/噪声（保留顶面完整性）
- 从 `isBoxLike` 移除：6 面 L/V 形墙被 4 面 box 遮挡假设破坏，导出点云严重失真
- ⚠️ LHouse 和 IndentedCuboid 也在 `isBoxLike` 中，可能有类似遮挡问题，待后续验证
- 重新训练 `pct_reg_twogable_v2_neighbor`（100 epoch，neighbor 变体）
- 修复 `QJsonArray::append` 嵌套 bug（val/test metadata 被吞）

### v2.3.0 (2026-07-29) — 坐标系居中 + 微调体验升级

**坐标系**
- `centerMeshOnBaseFace()`：14 类基元统一底面中心原点
- 旋转绕自身中心，tx/ty = 建筑中心世界坐标
- auto-align 代码精简：`alignModelToPointCloud()` 共用函数（90行→30行）

**微调体验**
- Ctrl+滚轮 10x 精调：`FineTuneFilter` 事件过滤器
- DL 预测值锚点 + "↺ Reset to DL prediction" 一键复位按钮
- 参数分组标题：8 类复杂基元的 QFormLayout 加粗分隔线

### v2.2.0 (2026-07-27) — TriPrismPyramid + 纯数据双接口

- 新增 TriPrismPyramid 基元（三棱柱+三棱锥）
- 14 类基元全部支持纯数据接口（`BuildMesh::build*(const XxxParams&)`）
- 姿态归一化闭环：auto-align 从 metadata center 计算 tx/ty/tz

### v2.1.4 — PCT 模型部署

- PCT 分类 98.92% F1
- PCT 回归 neighbor + basic 混合部署
- 3D 预览消失修复 + 底面颜色修复

### v2.3.2 (2026-08-06) — TwoGable 回归增强 + 角度映射修复 + 3D 场景清除

**TwoGableHouses 回归模型重新训练**
- 仅扩充数据量至 1000 样本（train 800 + val 100 + test 100），无额外增强（不删侧面、不加孔洞/噪声）
- 仅重新训练参数回归（分类模型不变）
- 结果：`pct_reg_twogable_v2_neighbor` MAE 3.15→1.70（-46%），R² 0.41→0.81，从「困难」升入「可用」档
- 数据量翻倍有效解决了 500 样本→7 参数回归的过拟合问题
- 其他 12 类回归模型不变（两次训练间仅有随机波动）

**角度映射 Bug 修复**
- **根因**：`applyToUI()` 硬编码 `slider->setValue(v * 100)`，但 `spinBoxTGAngle` 的 slider 通过 `bindSliderSpin` 用了 multiplier=10（因为角度范围 135-180°）
- **Bug 链路**：预测角度 139.40 → slider 获值 13940 → clamp 到上限 1800 → 双向绑定回写 `1800/10=180.0` 覆盖 spinBox
- **修复**：`parammodeler_pointnet.cpp` — `applyToUI()` 中移除所有手动 `slider->setValue()` 调用。`bindSliderSpin` 的双向绑定会在 spinBox 值变更时自动用正确 multiplier 同步 slider
- 影响范围：`set` lambda + `setTotalAndWallRatio` lambda + `setTotalAndCylinderRatio` lambda + IC offset 两处

**3D 场景清除功能**
- `menuLoad3D` 菜单新增 "Clear 3D scene" 选项（`actClear3D`）
- 实现 `ParamModelerScene3D::clearAll3DEntities()`：清除 Qt3D 实时预览实体 + legacy 模型图层
- dock 端直接通过 `m_pointCloudLayer` 指针清除点云：先清 3D Map Settings layers，再从 QgsProject 删除，确保 3D 场景正确刷新
- dock.h 新增 `QgsMapLayer *m_pointCloudLayer` 缓存已加载的点云图层指针
