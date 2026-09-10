# Deep Learning Pipeline 完整日志

> 最后更新: 2026-09-10  
> 插件版本: **v2.3.5**（对齐修复 bbox 极值 + 生长锚点落地：长方体类左下角；含 v2.3.3 回归切换 v3_normals）  
> 当前模型: **PCT**（Point Cloud Transformer）— Li & Shan 2025 风格 offset-attention  
> 分类模型: `pct_cls_v2` — 98.92% F1（保持 v2；`pct_cls_v3` 99.23% 仅噪音级提升，且分类与法向量无关）  
> 回归模型: 13 类（TriPrismPyramid 无需回归），**`pct_reg_*_v3_normals` (basic + PCA 法向量)，已接入生产**  
> 法向量实验: 阶段 0/1/2 全部完成，bulge 转正、middleBulge 大幅改善但仍≈0（详见第七章）  
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

### Auto-Align（平移对齐）— v2.3.4 改 bbox 极值 / v2.3.5 按锚点类型取角或中心

`alignModelToPointCloud()` — `parammodeler_dock.cpp`（共用函数，`onInverseParams` 和对话框均调用）

```
corner = BuildMesh::usesCornerAnchor(primitiveType)
modelRef = corner ? meshBBoxMin            : meshBBoxCenter   // 角锚定 → 左下角(0,0,0)；圆类 → 中心
pcRef    = corner ? pcBBoxMin              : pcBBoxCenter     // 两侧必须同一语义
setPoseTranslate( pcRef - modelRef )
```

⚠️ **对齐目标绝不能用 `pointCloudInfo.center`**：那是采样点**质心**，采样比例是屋顶 60% / 墙 40%
（`exportpointcloud.cpp`，三处一致），底面又被剔除。实测（`sample_params.json` 7502 条）
"质心 − bbox 中心"的 Z 偏差逐类统计：Cylinder **+3.19 m**、HalfCylinderRoof +3.17、
IndentedCuboid +2.84、TruncatedPyramidRoof +2.58、LHouse +1.83、ConeCylinder **−1.60** ……
XY 偏差 0.4–1.7 m。**必须用 bbox 极值**（所有类 `bboxMin.z ≈ 0`，故 bbox 中心 z 严格等于 h/2）。

`pointCloudAlignTarget()` 的优先级（`parammodeler_dock.cpp`）：

1. `m_displayCloudBBoxMin/Center` —— **场景中实际显示的点云**的 bbox 极值（`loadPointCloudToQGIS3D` 里
   对着"真正写进显示用的那些点"累积 min/max 得到）。仅当显示的文件 == 当前 `m_inputDataPath` 时才用。
   显示什么就对到什么，彻底消除"点云画在 A、模型对到 B"。
2. `m_metadataBBoxMin/Center`（点云没显示时的退化备选，来自 metadata 的 `bboxMin` / `bboxMin+bboxSize/2`）。

坏 metadata（`pointCloudMetadataLooksBuggy()`：scale<2 且 center≈0，即从归一化坐标存下来的旧记录）
判据由显示路径和对齐路径**共用**，且坏记录不参与对齐（否则模型被对到世界原点）。
`metadataPointCloudInfoForInput()` 增加了 `bboxSize` 出参（JSON 里本来就有，只是没读）。

注意：**只对齐平移，不处理旋转**。因为模型不预测 rz，模型朝向保持默认。
旋转待做时有一个必须注意的点：rz 非 0 后，模型自己的 AABB 中心会随旋转漂移（非对称底面尤其明显），
参考点必须在**应用 rx/ry/rz 之后**的 mesh 上算；现在 rz 恒为 0，这个坑还没暴露。
另：角锚定下 bbox 角对 bbox 角不是旋转不变的（中心对中心在对称底面上是），
所以带 rz 的点云切换后 X/Y 残余可能更明显，要等位姿估计解决。

### 点云显示反归一化（尺度还原）— 解决"点云很小 / 缩不上去"

**问题现象**：加载点云到 QGIS 3D 后点云极小；放大到一定程度就上不去了。

**根因**：DL 导出把点云归一化成单位球（`normalizeForDL`：中心化 + 最大半径归一化），坐标变成直径约 1~2 的无量纲尺度，而模型 mesh 是米级（十几米）。早期显示路径直接把归一化坐标渲染，没还原尺度 → 点云显得极小。
"缩不上去"是连带效应：3D 视图 extent 从点云 bbox 推（`padded3DViewExtent` / `setExtent` / `setViewFrom2DExtent`），云越小 extent 越小、相机近裁剪面越近，缩放空间被卡死。

**解决（反归一化链路，两个 commit）**：

| commit | 内容 |
|--------|------|
| `441b726` (2026-07-01) | 引入反归一化：`pointCloudLooksNormalizedForDisplay()` 判断是否归一化（bbox 最大边 ≤3.5 或最大半径 ≤1.5）→ `metadataPointCloudInfoForInput()` 取原始 center/scale（JSON `pointCloudInfo.center/scale` → PLY `denorm_center/denorm_scale` 注释 → 兜底自算）→ `restored = p*scale + center` 还原后写临时文件再显示 |
| `7d30106` (2026-07-24, v2.1.2) | 补坏元数据兜底：`metadataLooksBuggy`（scale<2 且 center≈0）检测旧导出的坏记录；坏时改用当前模型 mesh 估算 center/scale（mesh bbox 中心 + 尺寸×0.8） |

关键函数：`loadPointCloudToQGIS3D`（dock.cpp:1438）、`metadataPointCloudInfoForInput`（dlutils.cpp:500）、`pointCloudLooksNormalizedForDisplay`（dlutils.cpp:398）、`denormInfoFromPlyComment`（dlutils.cpp:420）。

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

### ✅ 法向量通道实验（阶段 2 全量 13 类，2026-09-10 完成）

根因：bulge/middleBulge 曲率参数学不动（R²≈0），2048 个纯 XYZ 点缺曲率信息。解法：**PCA 估计法向量**（kNN 局部平面拟合，不用 mesh 真值）作为额外点通道（3→6 通道），训练/推理用同一 `estimate_normals`。全程结论：

- 阶段 0（小集 A/B）：basic+normals 胜出（bulge +0.265，middleBulge +0.073）
- 阶段 1（neighbor A/B）：neighbor+normals 对 middleBulge 负迁移，判死路
- **阶段 2（全量 13 类，basic+normals，`pct_reg_*_v3_normals/`）** ← 最终结果

**整体 mean R²（test，13 类平均）**：

| 变体 | mean R² | 说明 |
|---|---:|---|
| v2 (basic，当前生产) | 0.468 | — |
| v2_neighbor | 0.655 | kNN 图 6 通道边特征 |
| **v3_normals (basic + 法向量)** | **0.658** | ✅ 新候选 |

**核心曲率参数**：

| 参数 | v2 (basic) | v2_neighbor | **v3_normals** | 判定 |
|---|---:|---:|---:|---|
| **bulge** (CylinderDome) | -0.686 | -0.226 | **+0.599** | ✅ 彻底转正（MAPE 36.8%→22.5%） |
| **middleBulge** (FourStage) | -0.851 | -0.236 | **-0.057** | ⚠️ 大幅改善但仍≈0（MAPE 46.4%→26.0%） |

**连带大赢**：FourStage baseHeight/middleHeight 0.313/0.312→0.762/0.813；PyramidRoof width −0.192→+0.460、wallRatio −0.104→+0.831；TwoGable width −0.531→+0.850、angle −0.358→+0.487、ridgeRatio −0.201→+0.600；IndentedCuboid outerHeight 0.588→0.964；ConeCylinder cylinderRatio 0.271→0.802。

**回归项（诚实记录）**：cylinder height 0.981→0.957（仍>0.95）；halfcylinder width 0.930→0.843、radius 0.936→0.821；**truncatedpyramid bottomWidth 0.425→−0.479（崩塌）**。

**结论/建议**：v3_normals 整体≈v2_neighbor，但**唯一解掉 bulge**，且 basic 无 kNN 图、推理更快更简、PCA 法向量在合成+真实扫描两场景都成立。**建议 basic+normals 作为单一生产变体**；追求每类最优可做 per-class 混合（v3 用于 cylinderdome/fourstage/halfcylinder/lhouse/pyramidroof/conecylinder/indentedcuboid，neighbor 用于 asymgable/cuboid/cylinder/gabledroof/truncatedpyramid/twogable）。分类 `pct_cls_v3` F1 0.9923 vs `pct_cls_v2` 0.9892（边际提升）。

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

### v2.3.3 (2026-09-10) — 回归模型切换 v3_normals（法向量）+ 点云连续加载崩溃修复

**回归模型接入 v3_normals（basic + PCA 法向量）**
- 阶段 0/1/2 法向量实验全部完成：bulge（CylinderDome）R² -0.686→+0.599 彻底转正；middleBulge（FourStage）-0.851→-0.057 大幅改善但仍≈0
- 整体 mean R²（test，13 类）：v2 (basic) 0.468 → v2_neighbor 0.655 → **v3_normals 0.658**
- 13 类统一 `pct_reg_*_v3_normals`，删掉 `pctBestSuffix` 例外表（原来 CylinderDome/HalfCylinder/LHouse 指 `_v2`）
- 分类保持 `pct_cls_v2`（法向量只加给回归，分类与法向量无关；v2→v3 仅噪音级提升）
- 关键机制：`main_reg.py` predict 从 checkpoint 自动读 `use_normals`，推理时用 PCA 从 XYZ 现算法向量，插件推理命令零改动

**点云连续加载崩溃修复**
- 症状：加载一个点云到 3D 后再加载另一个 → 0xC0000005 读取 0xFFFFFFFFFFFFFFFF
- 根因：`loadExternalPointCloud` 先删旧 layer 再遍历 3D settings，残留悬空指针
- 修复：删除旧 layer 前先清 3D settings，删除后只追加新 layer

### v2.3.4 (2026-09-10) — 对齐修复：对齐目标改为点云 bbox 中心

用户反馈"模型和点云对齐不太好"（上下浮 + 水平错 + 调参往两边长）。定位到两个独立根因并修复前两个，
第三个（旋转）与方案 B（左下角锚点）留待后续。

**根因 1：对齐目标用错了统计量（可量化，必然发生）**
- 点云侧 `pointCloudInfo.center` 是**采样点质心**（`exportpointcloud.cpp` 的 `computeDLPointCloudInfo`），
  采样比例屋顶 60% / 墙 40% 且底面被剔除 → 平屋顶类质心 z ≈ 0.8h
- 模型侧 `alignModelToPointCloud` 用的是 mesh **bbox 中心** z = 0.5h
- 两者相减 → 模型整体抬高 **0.3h**（h=6m 即 1.8m）；非对称底面 X/Y 同样错位
- 修复：新增 `bboxSize` 出参（JSON 里本来就有），对齐统一用 `bboxMin + bboxSize/2`

**根因 2：显示路径和对齐路径各算各的**
- `metadataPointCloudInfoForInput` 三条返回路径的 `center` 语义不同：JSON/PLY → 质心；都没命中 → 归一化后点云的 bbox 中心 ≈ (0,0,0)
- 显示路径在坏记录时改用模型 mesh 估算 center/scale，对齐路径却直接用 ≈0 → 点云画在 A、模型对到 B
- 修复：抽 `pointCloudMetadataLooksBuggy()` 共用判据；坏记录不参与对齐；
  `loadPointCloudToQGIS3D` 对着**真正显示的点集**累积 bbox → `m_displayCloudBBoxCenter`，
  `pointCloudAlignTarget()` 优先用它（且只在显示文件==当前输入文件时）；
  对话框流程改为**先显示点云再对齐**，对齐后 `onUpdatePreview()` 推位姿

**未做（下一步）**
- 旋转：DL 不预测 rz、align 也不估位姿，点云带 rz 时朝向仍是歪的
- 方案 B（左下角锚定）：调参对称生长导致已对齐部分被推开（见 `scripts/grow-anchor-design.md`）

**改动文件**：`parammodeler_dlutils.{h,cpp}`（bboxSize 出参）、`parammodeler_dock.{h,cpp}`（对齐目标解析、显示 bbox 累积、对话框顺序）、`README.md`

### v2.3.5 (2026-09-10) — 生长锚点落地（方案 B）：长方体类左下角锚定

用户反馈"高度啥的对齐的也一般"。中心锚定下预测高度偏了会**上下各分一半**（既浮起又扎进地里），
角锚定后误差只往一侧堆 —— 这是方案 B 的对齐侧收益（真正的尺寸误差仍需回归精度解决）。

**锚点判定集中到一处**：`BuildMesh::usesCornerAnchor()`（`buildmesh.cpp/h`）

| 锚点 | 类 |
|---|---|
| 左下角（原点即 (0,0,0)，跳过居中） | Cuboid / GabledRoof / PyramidRoof / TruncatedPyramidRoof / HalfCylinderRoof / IndentedCuboid / AsymmetricGableHouse / LHouse / TwoGableHouses |
| 底面中心（`centerMeshOnBaseFace`） | Cylinder / ConeCylinder / CylinderDome(Hemisphere) / FourStageRoundTower / TriPrismPyramid |

**逐类核实**（跳过居中的前提）：9 个角锚定类的构建代码都以 (0,0,0) 为原点、footprint 全在 +X/+Y 象限、
左下角是真实直角 —— LHouse 缺口在右上 (`buildLHouse`)、HalfCylinderRoof 弧在竖向底部仍是完整矩形、
IndentedCuboid 外底是完整矩形、**TwoGableHouses 从 A(0,0) 起沿 +X/+Y 展开**（`buildTwoGableHouses`：
`dir=(cos(turnRad), sin(turnRad))`，turnRad∈[0°,45°] 故两分量均 ≥0，minX/minY 都在 A）。
→ 上次悬着的 TwoGableHouses 锚点据此定案：**用左下角**（凹角在两屋交接的 B/C 处，不在锚点）。

**对齐侧**：`alignModelToPointCloud(mesh, pcRef, primitiveType, ...)` 按类型取 bbox 左下角或中心；
`pointCloudAlignTarget()` 同步（`m_metadataBBoxMin` / `m_displayCloudBBoxMin` 新增）。
`parammodeler_scene3d.cpp` **不用改** —— 变换是 `T·Rx·Ry·Rz`（先绕自身原点转再平移），
两种锚点都兼容，只是角锚定下旋转轴变成左下角（已确认是期望行为）。

**连带影响（已核对，无破坏）**：导出/采样走同一个 `BuildMesh::build`，所以新导出的合成点云
原点也变成左下角。训练无影响 —— 点云喂网络前按自身质心+最大半径归一化，平移无关；
`bboxSize`/`scale` 也是平移无关量；老数据集（居中坐标系）的 metadata 与显示/对齐自洽读取，照常可用。

**未做**：旋转（rz）。且注意角锚定下"bbox 角对 bbox 角"**不是旋转不变的**（中心对中心在对称底面上是），
所以带 rz 的点云在切换后 X/Y 残余可能更明显 —— 位姿估计要紧接着做。

**改动文件**：`buildmesh.{h,cpp}`（usesCornerAnchor + 跳过居中）、`parammodeler_dock.{h,cpp}`（对齐参考点按类）、`README.md`
