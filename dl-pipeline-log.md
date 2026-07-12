# Deep Learning Pipeline 完整日志

> 最后更新: 2026-07-12  
> 当前模型: PointNeXt `_aux`（纯形状参数，不含 rz）  
> 分类模型: `pointnext_cls_v2`

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
| 批量生成全部 13 类 | `onExportDLDatasetClicked()` | `parammodeler_dock.cpp:1748` |
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
│   ├── Cuboid/sample_00001.txt ... sample_00040.txt
│   ├── Cylinder/...
│   └── ...（13 类）
├── val/
│   └── ...（同上结构）
├── test/
│   └── ...（同上结构）
└── metadata/
    ├── class_names.txt
    └── sample_params.json
```

训练集 : 验证集 : 测试集 = **8 : 1 : 1**

---

## 三、训练（Ubuntu, Python）

### 路径约定（训练时）

| 项 | 路径 |
|----|------|
| PointNeXt 代码 | `E:/pointnet/pointnext_simple/` |
| 分类入口 | `main.py` → `PointNet2Classifier` |
| 回归入口 | `main_reg.py` → `PointNeXtRegressor` |
| 数据集根目录 | `--data_root E:/pointnet/datasets_aug` |
| 元数据 | `--metadata E:/pointnet/datasets_aug/metadata/sample_params.json` |

### 分类训练

```bash
python main.py --mode train \
  --data_root E:/pointnet/datasets_aug \
  --log_dir logs/pointnext_cls_v2 \
  --num_points 1024 --epochs 80 --batch_size 8
```

输出到 `logs/pointnext_cls_v2/`：
- `best_model.pth`
- `classes.txt`（13 类名）
- `classification_report_test.csv`
- `confusion_matrix_test.csv`

### 回归训练（每个基元独立训练）

```bash
python main_reg.py --mode train \
  --data_root E:/pointnet/datasets_aug \
  --metadata E:/pointnet/datasets_aug/metadata/sample_params.json \
  --class_name Cuboid \
  --targets length width height \
  --log_dir logs/pointnext_reg_cuboid_aux \
  --num_points 2048 --epochs 100 --batch_size 16 \
  --aux_features bbox_x bbox_y bbox_z scale
```

**关键参数说明：**

| 参数 | 含义 |
|------|------|
| `--targets` | 回归目标参数名（必须和 sample_params.json 中 params 的 key 一致） |
| `--aux_features` | 辅助特征（来自 metadata 的 pointCloudInfo 字段） |
| `--random_rotate` | 训练时随机旋转增强（`_rot` 实验用，`_aux` 不用） |
| `--train_jitter_sigma` | 训练时加噪标准差，默认 0.003 |
| `--smooth_l1_beta` | SmoothL1 loss 的 beta 参数，默认 1.0 |

### 回归输出文件

```
logs/pointnext_reg_{primitive}_aux/
├── best_model.pth              # 模型权重
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
| 分类 v2 | `pointnext_cls_v2` | 当前使用的分类模型，79.9% F1 (85/类) |
| 分类 aug_250 | `pointnext_cls_aug_250` | 旧实验，数据增强版（**不用**） |
| 分类 coordfix | `pointnext_cls_coordfix_v1` | 旧实验（**不用**） |
| 回归 aux ×13 | `pointnext_reg_*_aux` | **当前使用**，纯形状参数 |
| 回归 rot ×13 | `pointnext_reg_*_rot` | 含 rz 预测，数据量大但形状参数更差（**不用**） |

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
backendConfig(PointNeXt)
    → script: pointnext_simple/main.py
    → logDir: pointnext_simple/logs/pointnext_cls_v2
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
regressionConfig(PointNeXt, primitiveType)
    → script: pointnext_simple/main_reg.py
    → logDir: pointnext_simple/logs/pointnext_reg_{primitive}_aux
    ↓
QProcess: python main_reg.py --mode predict
         --input X --log_dir Y --num_points 2048 --cpu
         --data_root ... --metadata ...
         --bbox_x .. --bbox_y .. --bbox_z .. --scale ..
    ↓
JSON 输出 → pointNetParamsToUiParams() → ParamInverter::applyToUI()
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

### Auto-Align（平移对齐）

`onInverseParams()` 末尾 — `parammodeler_dock.cpp:2589-2641`

```
modelCenter = (modelBBoxMin + modelBBoxMax) / 2
tx = metadataCenter.x - modelCenter.x
ty = metadataCenter.y - modelCenter.y
tz = metadataCenter.z - modelCenter.z
setPoseTranslate(tx, ty, tz)
```

注意：**只对齐平移，不处理旋转**。因为 `_aux` 模型不预测 rz，模型朝向保持默认。

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
分类脚本: {pointnetBase}/pointnext_simple/main.py
回归脚本: {pointnetBase}/pointnext_simple/main_reg.py
分类日志: {pointnetBase}/pointnext_simple/logs/pointnext_cls_v2
回归日志: {pointnetBase}/pointnext_simple/logs/pointnext_reg_{primitive}_aux
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

### 分类 (pointnext_cls_v2)

总体 F1: **79.9%**（85 样本/类，需要提升）

🔴 主混淆：Cuboid↔Cylinder(9)、Cuboid↔IndentedCuboid(10)、GabledRoof→TwoGableHouses(8)

### 回归 (_aux)

| 可用 (R²>0.9) | 部分可用 | 需要重新训练 |
|---------------|----------|-------------|
| Cylinder (radius 0.96, height 0.89) | GabledRoof (length 0.90) | CylinderDome (bulge -0.08) |
| HalfCylinderRoof (length 0.97) | ConeCylinder (totalHeight 0.90) | FourStageRoundTower (middleBulge -0.85) |
| LHouse (mainLength 0.94) | Cuboid (length 0.84) | TwoGableHouses (wallRatio 0.06) |
| PyramidRoof (length 0.94) | TruncatedPyramid (bottomLength 0.78) | IndentedCuboid (innerWidth 0.21) |
| | AsymmetricGableHouse (ridgeLength 0.52) | |

---

## 八、修改指南

### 场景 1：新增基元类型

1. **插件端**：`parammodeler_dock.cpp` — 添加 UI 控件 + `randomizeCurrentPrimitiveParams()` 加随机化逻辑 + `pointNetParamsToUiParams()` 加映射
2. **数据生成**：重新跑 `onExportDLDatasetClicked()` 生成新数据
3. **训练**：复制 reg config 跑新的 `main_reg.py --class_name NewPrimitive`
4. **部署**：`parammodeler_pointnet.cpp:92` — `pointnextDirs` 加新条目，`parammodeler_config.cpp` — 如有新脚本路径需更新

### 场景 2：更换模型版本

只需改 `parammodeler_pointnet.cpp`:
- 分类：修改 `classifyLogDir()` 返回新目录
- 回归：修改 `pointnextDirs` 映射表

### 场景 3：重新训练（等有训练机器后）

建议用 `_rot` 实验的数据量 + `_aux` 实验的 target（不加 rz）：
```bash
python main_reg.py --mode train \
  --data_root ... --metadata ... \
  --class_name Cuboid \
  --targets length width height \    # 不加 rz
  --log_dir logs/pointnext_reg_cuboid_v3 \
  --num_points 2048 --epochs 150 \
  --aux_features bbox_x bbox_y bbox_z scale
```

然后更新 `pointnextDirs` 指向 `_v3` 目录。
