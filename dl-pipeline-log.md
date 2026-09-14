# Deep Learning Pipeline 完整日志

> 最后更新: 2026-09-14
> 插件版本: **v2.3.9**（2D 点选目标平移 + 底部截面 footprint 对齐；含 v2.3.8 回归 `v4_normals` 默认 / v2.3.7 稳健 bbox / v2.3.6 rz 回填）
> ⚠️ **待办**：分类重训（`train_pct_cls_v4.sh`）**尚未执行**，下方分类指标仍是旧数据上 `pct_cls_v2` 的结果
> 当前模型: **PCT**（Point Cloud Transformer）— Li & Shan 2025 风格 offset-attention
> 分类模型: `pct_cls_v2` — 98.92% F1（旧数据；v4 重训待跑）
> 回归模型: 13 类（TriPrismPyramid 无需回归），**`pct_reg_*_v4_normals` (basic + PCA 法向量)，已接入生产**（v2.3.8 起为插件默认后缀）
> 法向量实验: 阶段 0/1/2 全部完成，bulge 转正（v4 corr=0.578）、middleBulge 仍≈0（详见第七章）
> 数据集: **500 样本/类**（train 400 + val 50 + test 50），TwoGableHouses **1000 样本**（仅扩充数据量，无额外增强），14 类共 7500 样本；**v4 起用修复后的 `datasets_aug`**（离群点 extent 0.08 + 坐标系不漂移）
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

### PCT 回归训练（v4 起：basic + 法向量单一变体，13 类）

生产用脚本是 `train_pct_reg_normals_v4.sh`（内部就是下面这条命令跑 13 遍 + test）。
下面留一类的单跑示例；`--pct_variant neighbor`（kNN 局部增强）是 v2 时代的旧路径，v4 不再用。

```bash
# 当前生产 — basic + PCA 法向量
python main_reg.py --mode train \
  --data_root /home/xubo/pointnet/datasets_aug \
  --metadata /home/xubo/pointnet/datasets_aug/metadata/sample_params.json \
  --class_name Cuboid --targets length width height \
  --log_dir logs/pct_reg_cuboid_v4_normals \
  --num_points 2048 --epochs 100 --batch_size 4 --lr 3e-4 \
  --weight_decay 1e-4 --smooth_l1_beta 1.0 --train_jitter_sigma 0.003 \
  --aux_features bbox_x bbox_y bbox_z scale bbox_volume aspect_ratio_xy \
  --pct_variant basic --pct_d_model 256 --pct_heads 4 --pct_blocks 4 --pct_dropout 0.1 \
  --use_normals

# 旧路径（v2 时代，已不用）— neighbor 变体
#   --pct_variant neighbor  （无 --use_normals）
```

⚠️ test 模式**必须显式传 `--pct_variant`**，否则 `main_reg.py:796` 打印的版本名是 CLI 默认值
`neighbor`，与实际加载的 checkpoint 不符（模型本身走 `load_checkpoint()`，指标不受影响，只是日志骗人）。
正式脚本里 test 段没传这个参数，所以 `v4_reg_train.log` 里每段 test 都印着 `(neighbor)`。

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
| **PCT 分类 v2** | `pct_cls_v2` | **当前使用**，98.92% F1（旧数据；v4 重训待跑） |
| **PCT 回归 v4_normals ×13** | `pct_reg_*_v4_normals` | **当前使用**（v2.3.8 默认后缀），basic + 法向量 |
| PCT 回归 v3_normals ×13 | `pct_reg_*_v3_normals` | 旧数据上同类变体，保留作对照 |
| PCT 回归 basic v2 ×13 | `pct_reg_*_v2` | 旧数据，basic 变体 |
| PCT 回归 neighbor v2 ×13 | `pct_reg_*_v2_neighbor` | 旧数据，neighbor 变体，10 类最优（v4 起不再用） |
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

### Auto-Align（平移对齐 + 朝向回填）— v2.3.4 bbox 极值 / v2.3.5 按锚点取角或中心 / v2.3.6 旋转后取极值

`alignModelToPointCloud()` — `parammodeler_dock.cpp`（共用函数，`onInverseParams` 和对话框均调用）

```
corner   = BuildMesh::usesCornerAnchor(primitiveType)
posMat   = Rx(rx) · Ry(ry) · Rz(rz)            // 与 scene3d / STL 导出同约定（只取旋转部分）
meshBBox = bbox( posMat.map(v) for v in mesh.vertices )   // v2.3.6：在**旋转后**的顶点上取
modelRef = corner ? meshBBoxMin            : meshBBoxCenter   // 角锚定 → 左下角；圆类 → 中心
pcRef    = corner ? pcBBoxMin              : pcBBoxCenter     // 两侧必须同一语义
setPoseTranslate( pcRef - modelRef )
```

**为什么必须在旋转后的 mesh 上取极值**（v2.3.6）：点云坐标导出时已被 `applyPose(..., rz)` 转过，
它的 `bboxMin/bboxSize` 是**旋转后点集**的极值。模型侧若拿未旋转的 mesh bbox，两侧就不是同一语义。
旋转绕 mesh 自身原点（= 锚点，见 scene3d 的 `T·Rx·Ry·Rz`），所以 rz=0 时 `posMat` 是单位阵，
结果与 v2.3.5 逐位一致——这次改动对无旋转输入是零行为变化。
注意角锚定类在 rz≠0 后**锚点本身已不再是 bbox 极值**（把矩形绕左下角转 45°，min x 会跑到负值），
所以"角对角落对齐"必须两边都用旋转后的 AABB，不能拿构建原点当极值。

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

### 点云对齐 bbox 的当前实现（v2.3.9）— 底部截面 + 稳健 bbox 组合

`loadPointCloudToQGIS3D()` 现在不是单纯拿全点云硬 min/max 对齐，而是同时算三套量：

| 统计量 | 用途 |
|---|---|
| 硬 bbox | 只保留 `z_min`，确保模型落到点云底部 |
| 全点云稳健 bbox | X/Y 用 1% 分位数，避免少量离群点把对齐角拉走 |
| 底部截面 footprint | 优先作为 X/Y 对齐目标，更接近建筑底面外轮廓 |

底部截面取 `z_min` 到 `z_min + max(0.5m, 高度*0.12)`。只有同时满足：

- 截面点数不少于 `max(50, 总点数*5%)`
- 截面 X/Y 范围至少达到全点云稳健范围的 25%

才使用底部 footprint；否则退回全点云稳健 bbox。也就是说，v2.3.9 是"底部截面优先，
稳健 bbox 兜底"，不是把旧 bbox 方案删掉。

调试日志对应：

| 日志 | 含义 |
|---|---|
| `[PointCloudBBox]` | 硬 bbox、全点云稳健 bbox、离群点造成的 min 偏移 |
| `[PointCloudFootprint]` | 本次用 bottom 还是 full-coarse，以及底部截面点数/退回原因 |
| `[PointCloud] align bbox ...` | 最终缓存给 `pointCloudAlignTarget()` 的对齐 bbox |

### 手动点选目标平移（v2.3.9）— 用 QGIS 2D 画布做最后一跳

按钮：`Manual align: Pick target for model anchor`。

工作流：

1. 点击按钮后，插件计算当前模型的**生长/旋转锚点**：局部 `(0,0,0)` 经过当前旋转和平移后的 XY。
2. QGIS 2D 画布上显示红色 X；红色短线表示模型局部 +X，绿色短线表示模型局部 +Y。
3. 用户在点云上点击目标位置。
4. 插件只更新 `TX/TY`：`delta = target - source`；`TZ`、旋转、形状参数全部保持不变。

注意：红 X 表示**模型语义锚点**，不是"旋转后 AABB 的左下角"。
角锚定类（Cuboid / GabledRoof / PyramidRoof / TruncatedPyramidRoof / HalfCylinderRoof /
IndentedCuboid / AsymmetricGableHouse / LHouse / TwoGableHouses）红 X 是外包络左下角；
圆形类是底面中心。LHouse 的缺口在右上，所以红 X 不是 L 的内拐角。

### 朝向 rz 回填（v2.3.6）— metadata 真值，不是 DL 预测

`applyMetadataRz()` — `parammodeler_dock.cpp`，在两个对齐调用点（`onInverseParams` / 对话框
`btnFinish`）里**先于** `alignModelToPointCloud` 执行。

```
m_metadataRz / m_hasMetadataRz ← cacheInputMetadata() 读 metadata 的 params.rz
applyMetadataRz() → ui->spinBoxRKappa->setValue(m_metadataRz)
```

**为什么不做训练**（关键结论，纠正了我此前"DL 不预测 rz"的含糊表述——数据里一直有 rz 真值）：

| 环节 | 位置 | 状态 |
|---|---|---|
| UI → JSON 写 `rz` | `parammodeler_dlutils.cpp` `currentPrimitiveParamsObject` | ✅ 一直有 |
| 采样时把 rz **烘进**点云坐标 | `exportpointcloud.cpp` `applyPose(..., rz)` | ✅ 一直有 |
| DL 输出 `rz` → `poseRotateZ` 映射 | `parammodeler_dlutils.cpp` `pointNetParamsToUiParams` | ✅ 早已写好，但**永不触发** |
| PCT 回归 target 列表含 rz | `train_pct_reg.sh` | ❌ 注释明写"纯形状参数，不含 rz" |

映射永不触发的原因不是工程缺口，而是 **rz 在标签层面不可辨识**：

- **圆柱类**（Cylinder / ConeCylinder / CylinderDome / FourStageRoundTower）：底面是圆，
  绕自身轴旋转点云**完全不变** → rz 没有定义，标签是纯噪声，还会污染共享 backbone。
- **矩形底面类**：rz 只确定到 **mod 180°**；底面近正方形时（PyramidRoof / TruncatedPyramidRoof）
  只确定到 **mod 90°** → 目标多值，MSE 收敛到"平均"。

`_rot` 实验（`train_reg_with_rot.sh`，把 `rx ry rz` 追加到 13 类 target）实测"形状参数精度明显更差"
（`scripts/README.md`）——**该文档原先把这个归因于"数据量翻倍、每类有效样本变少"，是错的**：
对称性导致的多值标签加数据也修不好。v2.3.6 已把归因改正。

**归一化不背这个锅**（容易被误判的一环）：`normalizeForDL`（`exportpointcloud.cpp`）是
质心平移 + 单一标量 `maxRadius` **各向同性**缩放，两者都与旋转可交换
（`|R(p−c)| == |p−c|`）→ 归一化后 rz 在 TXT 里**完整保留**。
真正会碾平朝向的是**逐轴**归一化（分别除以 `bboxSize.x/y/z`），当前没有做。
辅助特征里的 `bbox_x bbox_y bbox_z` 更是主动提供朝向线索。

**把 metadata 的 rz 记进 DL 锚点**：`onInverseParams` 里 `m_dlAnchorParams.insert("poseRotateZ", m_metadataRz)`
并在 `tableInverseParams` 补一行 `poseRotateZ (metadata)`，使 `↺ Reset` 恢复的是完整自动估计结果（含朝向）。

**未覆盖**：PLY 输入 / 没有 `params` 字段的记录读不到 rz（`metadataPointCloudInfoForInput` 返回 NaN，
调试日志记 `<none>`），此时保持当前朝向不变——需要纯推理场景（无 metadata）时只能靠人工微调，
或先做对称性改造再训 rz。

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

> **数据规模**: 500 样本/类（400 train + 50 val + 50 test），14 类共 7500 样本
> **模型**: PCT (Point Cloud Transformer)，~1.48M 参数（分类）/ ~1.5M 参数（回归）
> **变体部署策略**: v4 起**统一 basic + 法向量**（v2.3.8 生产默认）；下表 v2/v3 的双变体数字保留作历史对照

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

### 回归 v4_normals — 🟢 当前生产（2026-09-14）

`datasets_aug` 用修复后的 `augment_dataset.py` 重生成后重训（脚本 `train_pct_reg_normals_v4.sh`），
产物 `pct_reg_*_v4_normals/`，**v2.3.8 起是插件默认后缀**。超参与 v3 完全一致
（basic + 法向量 / 2048 点 / 100 epoch / batch 4 / lr 3e-4），**唯一变量是数据**。

**整体（62 个参数，test split）**：

| 指标 | v3（旧数据） | **v4（修复后）** | Δ |
|---|---:|---:|---:|
| 13 类 Mean MAE 的平均 | 0.770 | **0.709** | **−8%** |
| 62 个参数的平均 R² | 0.599 | **0.637** | **+0.038** |
| 逐参数 R² 改善 >0.02 | — | 28 个 | |
| 逐参数 R² 变差 >0.02 | — | 19 个 | |
| 持平 | — | 15 个 | |

**逐类平均 R² / Mean MAE**：

| 类 | v3 R² | **v4 R²** | ΔR² | v3 MAE | v4 MAE |
|---|---:|---:|---:|---:|---:|
| TruncatedPyramidRoof | 0.305 | **0.708** | **+0.403** | 1.463 | 0.942 |
| GabledRoof | 0.767 | **0.866** | +0.099 | 0.659 | 0.557 |
| AsymmetricGableHouse | 0.479 | **0.574** | +0.095 | 0.849 | 0.827 |
| IndentedCuboid | 0.384 | **0.428** | +0.044 | 1.604 | 1.495 |
| FourStageRoundTower | 0.410 | **0.453** | +0.043 | 0.237 | 0.194 |
| PyramidRoof | 0.722 | **0.758** | +0.035 | 1.003 | 0.861 |
| Cuboid | 0.773 | **0.787** | +0.013 | 0.866 | 0.779 |
| HalfCylinderRoof | 0.860 | 0.858 | −0.002 | 0.667 | 0.671 |
| ConeCylinder | 0.907 | 0.891 | −0.016 | 0.230 | 0.268 |
| Cylinder | 0.969 | 0.950 | −0.020 | 0.331 | 0.426 |
| CylinderDome | 0.823 | 0.786 | −0.037 | 0.214 | 0.175 |
| TwoGableHouses | 0.719 | 0.663 | −0.056 | 1.287 | 1.339 |
| LHouse | 0.437 | **0.207** | **−0.230** | 0.602 | 0.687 |

**分档（v4，按 avg R²）**：

| 🟢 可用 (>0.6) 9 类 | 🟡 部分可用 (0.3–0.6) | 🔴 困难 (<0.3) |
|---|---|---|
| Cylinder 0.950 / ConeCylinder 0.891 / GabledRoof 0.866 | AsymmetricGableHouse 0.574 | **LHouse 0.207** |
| HalfCylinderRoof 0.858 / Cuboid 0.787 / CylinderDome 0.786 | FourStageRoundTower 0.453 | |
| PyramidRoof 0.758 / TruncatedPyramidRoof 0.708 / TwoGableHouses 0.663 | IndentedCuboid 0.428 | |

对比 v3 只有 6 类 >0.6，v4 是 9 类。

**结论 1：旧数据上"某参数学不动"的判断确实作废。** 被离群点毁掉的那批全部翻身，且幅度很大：

| 参数 | v3 R² | v4 R² | Δ |
|---|---:|---:|---:|
| TruncatedPyramidRoof.bottomWidth | **−0.479** | +0.658 | **+1.137** |
| AsymmetricGableHouse.ridgeRatio | −0.553 | +0.023 | +0.576 |
| TruncatedPyramidRoof.wallRatio | 0.016 | +0.531 | +0.515 |
| TruncatedPyramidRoof.topWidth | 0.223 | +0.641 | +0.418 |
| FourStageRoundTower.coneHeight | 0.125 | +0.413 | +0.288 |
| FourStageRoundTower.middleTopRadius | −0.147 | +0.142 | +0.289 |
| GabledRoof.width | 0.688 | +0.884 | +0.195 |
| PyramidRoof.wallRatio | 0.831 | +0.945 | +0.113 |

**结论 2：退步的要分两类看，不要只看 R²。**

- `Cylinder.radius` 0.981 → 0.940（MAE 0.201 → 0.357）**数字难看但模型是好的**：
  corr(true,pred) = **+0.990**、pred_std/true_std = 1.07，预测与真值几乎一比一在动。
  这是 50 个测试样本上的小波动，不是坏掉。`Cylinder.height` 同理。
- `LHouse`（−0.230）和 `TwoGableHouses`（−0.056）是**真退步**：`wingWidthRatio` −0.287 → −0.927、
  `totalWidth` 0.641 → 0.415；TwoGable 的 `length1/length2/ridgeRatio/width` 全线小幅下滑。

**结论 3：有一批参数换干净数据也救不回来，且部分比 v3 更差。** 用 corr + 预测分布宽度当判据
（比 R² 更能说明"有没有信号"，数值均为 v4）：

| 参数 | corr(true,pred) | pred_std/true_std | v3 R² → v4 R² | 判定 |
|---|---:|---:|---|---|
| FourStage.middleBulge | **−0.241** | 0.34 | −0.057 → −0.436 | 反向 |
| LHouse.wingWidthRatio | **+0.028** | 0.35 | −0.287 → −0.927 | 完全没信号 |
| IndentedCuboid.offsetY | +0.184 | 0.27 | −0.073 → −0.140 | 完全没信号 |
| AsymmetricGableHouse.ridgeRatio | +0.243 | 0.42 | −0.553 → +0.023 | 无信号 |
| LHouse.wingRatio | +0.305 | 0.55 | −0.063 → −0.341 | 弱 |
| IndentedCuboid.offsetX | +0.384 | 0.38 | +0.123 → −0.076 | 弱 |
| FourStage.middleTopRadius | +0.407 | 0.41 | −0.147 → +0.142 | 弱 |

对照能读出来的：`CylinderDome.radius` corr 0.994、`FourStage.baseRadius` corr 0.993、
`Cuboid.height` corr 0.978、`GabledRoof.length` corr 0.959。

**⚠️ 读 R² 的方法**：`pred_std/true_std ≈ 0.3` 说明模型基本在输出一个常数，
此时 corr ≈ 0，R² 完全由"那个常数的均值偏差"决定 —— 在 50 个测试样本上偏一点就能把 R² 压成负数。
所以这一档参数的负 R² **是"没信号"的签名，不是"拟合比均值还差"**。判据应该看 corr + p/t，不要只看 R²。

**共同点：能读出来的全是绝对尺寸和整体形状，读不出来的全是"建筑内部找参照"的相对量/偏移量**
（翼从哪里起 / 凹口往哪偏 / 鼓肚多少）。这批不是数据量问题 —— 干净数据反而更差，
说明是任务本身在"归一化 + 随机姿态"这个输入表示下病态。

`IndentedCuboid.offsetX/offsetY` 大概率与 rz 同源：偏移方向在导出坐标系里被 rz 混合，
而 rz 本身不可辨识（见《朝向 rz 回填》），在含未知 rz 的坐标系里回归方向性偏移，
标签本身就是多值的。

**⚠️ 日志陷阱（不影响指标，别被误导）**：`v4_reg_train.log` 里 test 段打印的是
`=== PCT Regression (neighbor) | mode=test ===`，但实际用的**是 basic**。
原因：训练段脚本显式传了 `--pct_variant basic`，test 段没传，`main_reg.py:796` 那行只是打印
`args.pct_variant`（CLI 默认值 "neighbor"）；真正建模型走 `load_checkpoint()`，
从 checkpoint 里的 `pct_variant` 字段读（`main_reg.py:606`）。所以指标是对的，
只有那行日志文案不可信。v3 脚本同样如此。

### 回归历史：PCT 2 变体混合部署（v2/v3，旧数据）

> ⚠️ 以下表格**全部基于修复前的 `datasets_aug`**，仅作对照，不要作为选型依据。

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

### 🔴 历史灾难参数 — PointNeXt → v3 → v4

| 参数 | PointNeXt R² | v3 R² | **v4 R²** | 评估 |
|---|---:|---:|---:|---|
| bulge (CylinderDome) | −0.08 | +0.599 | **+0.322** | ⚠️ 保持在正区间但退步；corr 0.578 有信号，MAPE 22.5%→28.5% |
| middleBulge (FourStage) | −0.85 | −0.057 | **−0.436** | ❌ 仍不可辨识；corr −0.241、p/t 0.34（详见下方结论 3） |
| wallRatio (TwoGable) | 0.06 | 0.762 | **+0.749** | ✅ 已彻底解决，不再是灾难参数 |
| innerWidth (IndentedCuboid) | 0.21 | 0.177 | **+0.355** | ⚠️ 改善但仍弱；corr 0.658、p/t 0.61，预测方差不足 |
| ridgeLength (AsymmetricGable) | 0.52 | 0.607 | **+0.581** | ✅ 持平 |

### ✅ 法向量通道实验（阶段 2 全量 13 类，2026-09-10 完成）

根因：bulge/middleBulge 曲率参数学不动（R²≈0），2048 个纯 XYZ 点缺曲率信息。解法：**PCA 估计法向量**（kNN 局部平面拟合，不用 mesh 真值）作为额外点通道（3→6 通道），训练/推理用同一 `estimate_normals`。全程结论：

- 阶段 0（小集 A/B）：basic+normals 胜出（bulge +0.265，middleBulge +0.073）
- 阶段 1（neighbor A/B）：neighbor+normals 对 middleBulge 负迁移，判死路
- **阶段 2（全量 13 类，basic+normals，`pct_reg_*_v3_normals/`）** ← 当时的最终结果

> ⚠️ **本节数字全部基于修复前的 `datasets_aug`。** 结论中"法向量通道解掉 bulge"成立，
> 但具体指标已被 v4 取代（见《回归 v4_normals》）：`bulge` v3 0.599 → **v4 0.322**（退步但仍为正），
> `PyramidRoof.wallRatio` 0.831 → 0.945，`TruncatedPyramidRoof.bottomWidth` −0.479 → 0.658。
> 选型结论（basic + 法向量作为单一生产变体）不变。

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

**IndentedCuboid 过拟合**（末 epoch 的 train_loss / val_MAE / val_RMSE，v4 值取自 `train_history.csv`）：

| 类 | train loss | val MAE | val RMSE | 诊断 |
|----|:---:|:---:|:---:|---|
| TwoGableHouses (v4) | 0.211 | 1.440 | 1.757 | ✅ 扩量至 1000 样本后已缓解（avg R² 0.663） |
| IndentedCuboid (v4) | 0.280 | 1.649 | 2.031 | ❌ 仍严重过拟合 |
| FourStageRoundTower (v4) | 0.248 | 0.226 | 0.269 | 正常 |
| Cuboid (v4) | 0.148 | 0.822 | 1.176 | 正常 |

根因：500 样本不足以支撑 7-8 个参数的回归。TwoGable 扩量 1000 后已解决，IndentedCuboid 待扩量。
注意 IndentedCuboid 的问题**不只是过拟合** —— 它的 `offsetX/offsetY` 属结论 3 的"无信号"档，
扩量也救不了（见上）。

**bulge / 曲率类参数**：`CylinderDome.bulge` 已由法向量通道转正（v3 +0.599 / v4 +0.322，
corr 0.578 有信号）；`FourStage.middleBulge` 仍是唯一真正负值的参数，corr −0.241、p/t 0.34，
属"无信号"而非"拟合不好"。2048 点 + 归一化后，局部鼓肚这种二阶量确实在噪声水平。

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
- 分类：修改设置对话框「分类模型名」（当前 `pct_cls_v2`，v4 重训后改成 `pct_cls_v4`）
- 回归默认后缀：修改设置对话框「PCT 回归默认后缀」（**当前默认 `_v4_normals`**）
- 设置对话框路径：`设置 → PointNet 路径设置`

> 版本 ≥ v2.3.3 后回归是**单变体**（basic + 法向量），不再有 `pctBestSuffix` 每类覆盖表
> （`parammodeler_pointnet.cpp:129` 的注释保留了重新加回的提示，仅在将来确实需要 per-class 时才加）。

**注意**：改对话框设置后需重启插件生效。已有 `QgsSettings` 配置**不会**跟着默认值走 ——
老配置里存着 `_v3_normals` 的要在对话框里手动改，或点「恢复默认值」。

### 场景 3：重新训练

**回归 13 类**（一条命令跑完，含前置数据检查）：

```bash
cd /home/xubo/pointnet/pct_simple
nohup bash train_pct_reg_normals_v4.sh > logs/v4_reg_train.log 2>&1 &
```

**分类**：

```bash
cd /home/xubo/pointnet/pct_simple
nohup bash train_pct_cls_v4.sh > logs/v4_cls_train.log 2>&1 &
```

两者超参（2048 点 / 100 epoch / batch 4 / lr 3e-4 / basic + 法向量；分类 1024 点 / 80 epoch / batch 32 / lr 5e-4）
都写死在校本里，改版本号只需把输出目录后缀递增（`_v4_normals` → `_v5_normals`）并
同步 `pctRegressionSuffix()` 的默认值。

**重训前必须先确认数据**：两个脚本都会检查 `datasets_aug/metadata/augment_config.json` 里有没有
`outlier_extent` 字段（只有修复后的 `augment_dataset.py` 才会写），没有就直接退出。
在旧数据上跑完 13 类是 5 小时，这个检查值 5 分钟。

---


## 九、模型升级路线图

> ✅ **PCT (Li & Shan 2025) 已完成** — 分类 98.92% F1（旧数据），回归 v4 有 **9/13 类 avg R²>0.6**
> 🔴 剩余困难：`LHouse` 0.207；7 个"无信号"参数（见第七章结论 3，扩量也救不了）
> 🚧 **下一步：① 分类 v4 重训（脚本已就绪）→ ② canonical 对齐整套重训（最小验证：只做 Cuboid 一类）→ ③ IndentedCuboid 扩量**
> ✅ 已完成：法向量通道（阶段 0/1/2）、数据端修复（离群点 + 坐标系）

### 已完成

| 方案 | 状态 | 结果 |
|------|:---:|------|
| **PCT (Li & Shan 2025)** | ✅ 完成 | 分类 98.92%、回归 v4 有 9/13 类 avg R²>0.6（详见第七章） |
| basic + neighbor 双变体对比 | ✅ 完成 | neighbor 10/13 胜出，basic 3/13 胜出（旧数据结论） |
| **法向量通道（basic + PCA 法向量）** | ✅ 完成 | 解掉 bulge；v4 起为唯一生产变体 |
| **数据端修复（离群点 extent 0.08 + 坐标系不漂移）** | ✅ 完成 | 回归 avg R² 0.599→0.637，9 类转绿（v2.3.7/v2.3.8） |

### 下一步优先级

#### 🥇 优先级 1：分类 v4 重训（脚本已就绪，一条命令）

```bash
cd /home/xubo/pointnet/pct_simple && nohup bash train_pct_cls_v4.sh > logs/v4_cls_train.log 2>&1 &
```

`main.py` 同样按 max 半径归一化，同样受旧数据的离群点影响，所以 v2 的 98.92% 在新数据上不再成立。
预期：分类对这类扰动不敏感，持平或小升；若反而下降说明另有问题。跑完把 `logs/pct_cls_v4` 同步回来，
并更新第七章分类一节。

#### 🥈 优先级 2：canonical 对齐「整套」重训（当前最有价值的未知项）

**背景**：aux 特征里的 `bbox_x/bbox_y/bbox_volume/aspect_ratio_xy` 用的是**旋转后点云的 AABB**，
被 rz 撑大；rz≈45° 时 AABB 接近正方形，携带的 length/width 信息几乎为零。实例
`Cuboid/sample_00001`：params `14.5 × 6.8 × 7.2`、rz=142°，而 `bboxSize = 15.128 × 14.019 × 7.200`。
这解释了两件事：为什么 `Cuboid.width`（0.539）明显差于 `length`（0.875）；以及
`TruncatedPyramidRoof` 的 bottom/top 长度宽度类参数长期最难。

**为什么之前判"净效果为零"不算数**：`ab_canonical_align.py` 的 A/B 只测了**半套做法** ——
推理时把点云转到 canonical 姿态、aux 仍用原姿态的 AABB。该脚本自己的注释就写明这会
让 `(云, aux)` 组合落到训练流形之外，并明确说真正的做法是"训练也 canonical 化 + aux 用旋转后
点云重算"，当时因成本高搁置。另外那个 A/B 跑在 **2026-09-11 20:00，即损坏数据上**，本身也该重测。

**最小验证（建议先做这个，成本 ~30 分钟）**：
只挑 `Cuboid` 一类，导出时/训练前把点云 canonical 旋转、aux 用旋转后重算的 bbox，重训一次
（100 epoch）。判据：`width` 的 R² 是否从 0.54 跳到 0.85 量级。
- 跳 → 这条路成立，推全类重训，`[parammodeler_pointnet.cpp:464]` 那行 `--canonical_align` 也要恢复
- 不跳 → 彻底排除，以后不再纠结 aux 的旋转污染

#### 🥉 优先级 3：数据端增强 + 扩量

**3a. 随机裁切 / Mixup**（减少 IndentedCuboid 过拟合）：

| 增强 | 当前状态 | 建议 |
|------|---------|------|
| 随机裁切 | 未用 | 模拟遮挡，裁掉 10-30% 点 -> 强制模型用局部线索推理 |
| Mixup / CutMix | 未用 | 点云 mixup 可提升泛化，特别适合多参数空间 |
| Jitter | 已用 sigma=0.003 | 扩大到 0.005-0.01 |

**3b. 扩量**：IndentedCuboid 保持 500 样本（train loss 0.280 vs val RMSE 2.031 仍是教科书过拟合）。
注意：扩量只可能改善它的 6 个尺寸参数，`offsetX/offsetY` 属"无信号"档，扩量无用。

#### 优先级 4：架构升级（如果数据端不解决问题）

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

#### 优先级 5：分类+回归联合训练

合并到一个模型共享 backbone，两个 head 分别输出类别和参数。消除分类误差->回归误差的传导（当前 TwoGableHouses 分类 95% 还有 5% 分错，错分后回归结果无意义）。

### 评估基准（v4，2026-09-14）

| 参数 | PointNeXt R² | v3 R² | **v4 R²** | 目标 R² | 状态 |
|------|:---:|:---:|:---:|:---:|---|
| bulge (CylinderDome) | −0.08 | 0.599 | **0.322** | >0.6 | ⚠️ 有信号但退步，corr 0.578 |
| middleBulge (FourStage) | −0.85 | −0.057 | **−0.436** | >0.5 | ❌ 无信号档，扩量无用 |
| wallRatio (TwoGable) | 0.06 | 0.762 | **0.749** | >0.7 | ✅ 达标 |
| innerWidth (IndentedCuboid) | 0.21 | 0.177 | **0.355** | >0.6 | ⚠️ 预测方差不足（p/t 0.61） |
| ridgeLength (AsymmetricGable) | 0.52 | 0.607 | **0.581** | >0.8 | ⚠️ 持平，距目标仍远 |
| wingWidthRatio (LHouse) | — | −0.287 | **−0.927** | >0.5 | ❌ 无信号档（corr 0.028） |

> 判定口径见第七章结论 3：**corr ≈ 0 且 pred_std/true_std ≈ 0.3 的，是"输入里没有这个信号"，
> 不要靠加数据/换架构去追**。当前 7 个这类参数：
> `FourStage.middleBulge`、`LHouse.wingWidthRatio`、`IndentedCuboid.offsetY`、
> `AsymmetricGableHouse.ridgeRatio`、`LHouse.wingRatio`、`IndentedCuboid.offsetX`、
> `FourStage.middleTopRadius`。

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
  → **v2.3.6 已解决**：`applyMetadataRz()` 回填 metadata 的 rz 真值，对齐参考点改在旋转后取
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
→ **v2.3.6 已解决**，见上面《朝向 rz 回填》一节：rz 从 metadata 真值回填（不训练），
对齐参考点在**旋转后**的 mesh 上取，两侧恢复同一语义。

**改动文件**：`buildmesh.{h,cpp}`（usesCornerAnchor + 跳过居中）、`parammodeler_dock.{h,cpp}`（对齐参考点按类）、`README.md`

### v2.3.7 (2026-09-11) — 对齐目标改稳健 bbox + datasets_aug 增强破坏坐标系（离群点污染）

用户反馈"参数估计完返回界面微调时，模型和点云的左下角没对齐"，且"最简单的长方体预测也不理想"。
定位到**两个独立根因**，都在点云统计量层面，与对齐公式本身无关。

**先说没问题的**：`alignModelToPointCloud` 的公式是精确的 —— 点云 = `R·M + T`，两侧都取
"旋转后点集的 AABB 极值"，逐分量 min 对常数平移可交换，所以 `pcRef - mRef ≡ T`（导出平移）。
参数正确时角对角必然重合。问题出在 `pcRef` 用的是**被污染的点云 bbox**。

#### 根因 A：显示 bbox 被离群点主导（插件侧）

- `loadPointCloudToQGIS3D` 的 `accumulateDisplayPoint` 对**所有**显示点取硬 min/max
- `datasets_aug` 每朵云含约 1% 离群点（见根因 B），硬 min/max 被它们完全接管
- `pointCloudAlignTarget()` 又**优先**用这个 `m_displayCloudBBoxMin`
- 实测（每朵云 10–14 个离群点，占 0.5–0.7%）：角锚定点被拉偏

| 样本 | rz | 全体 bboxMin（归一化） | 去离群后 bboxMin | 偏移 |
|---|---:|---|---|---:|
| `Cuboid/sample_00487` | 26° | (−1.049, −0.725, −0.987) | (−0.600, −0.725, −0.718) | 3.35 m |
| `Cuboid/sample_00451` | −180° | (−0.754, −1.122, −1.148) | (−0.754, −0.599, −0.529) | 6.82 m |
| `PyramidRoof/sample_00484` | 1° | (−0.961, −0.938, −0.726) | (−0.828, −0.305, −0.726) | 6.59 m |
| `GabledRoof/sample_00467` | 13° | (−1.102, −1.000, −1.080) | (−0.894, −0.802, −0.816) | 4.08 m |

**注意**：这也解释了为什么"理论上该对得很准"的 `sample_00487`（rz=26°，参数误差放大系数很小）同样不对 ——
参考点本身就错了，与参数误差无关。

**修复**：新增 `robustBBoxFromPoints()`，逐轴取 **1% 分位数**代替硬 min/max
（`n<100` 退回硬 min/max）。分位数不会削掉建筑本体：建筑在某轴上的极值是被一整条边/面（长方体）
或平方根聚集的点列（圆柱）逼近的，1% 分位与真实极值几乎重合。
三处 bbox 累积点全部改用它。

#### 根因 B：`augment_dataset.py` 破坏导出坐标系（数据侧，影响训练）

`augment_points()` 里的 `normalize_points(points)` 是**多余的重新归一化**，且**做完不映射回去**。

关键事实：导出的 `.txt` 点云**不是**以自身 mean/半径归一化的。实测 `datasets/test/Cuboid/sample_00487.txt`：
`mean = (−0.049, 0.018, 0.308)`、`max半径 = 1.094` —— 但 `p*scale + center` 能精确还原原始坐标
（bboxMin 的 x/y 分毫不差）。也就是导出时的 `center`/`scale` 是对**另一批点**算的。

所以那次重新归一化让整朵云平移+缩放，**实测漂移 1.6–1.8 m**，插件的 `p*scale+center` 反归一化跟着漂。

另外两个副作用：
- `add_outliers(extent=1.15)` 的离群点撒在归一化空间 `[-1.15,1.15]³`，最远到半径 1.99 ——
  比建筑自身半径（1.0）还远。真实扫描离群点应是建筑跨度的几个百分点，不是 115%
- 加完离群点**不再归一化**，存盘点云 `max|p|` 变成 1.0–2.0 随机
  → `main_reg.py` / `main.py` 的 `normalize_points` 按 max 半径归一化，
  每个样本被**随机缩放了最多 2 倍**，绝对尺度信息被打乱

**可复用的自洽性判据**（本次靠它定位）：
```
归一化点云的 逐轴 range × metadata.scale  应当恒等于  metadata.bboxSize
```
| 数据集 | x 比 | y 比 | z 比 | max\|p\| |
|---|---:|---:|---:|---:|
| `datasets`（原始） | 0.999 | 1.000 | 0.983 | 0.998 ✓ |
| `datasets_aug`（修复前） | 1.240 | 1.246 | **2.754** | 1.526 ✗ |

**修复**（`augment_dataset.py`）：
- `extent` 1.15 → **0.08**，提成 CLI 参数 `--outlier_extent`（占建筑跨度 8%，对 10 m 建筑约 ±1.4 m）
- `add_outliers` 加**半径钳制**（`‖p‖ ≤ 1`）—— 让"存盘云不出建筑半径"这个不变式**结构上成立**，不靠调常数
- `augment_points` 改为：记住输入的 `(m, r)` → 在自归一化坐标系里做增强 →
  **`pts * r + m` 映射回导出坐标系** → 再采样
- 新增坐标系漂移遥测，>5% 告警

**修复效果**（400 个测试样本）：

| 指标 | 修复前 | 修复后 |
|---|---:|---:|
| XY 对齐偏差 | ~1.5–1.8 m | 中位 **0.149** / p90 0.322 / max 2.298 m |
| Z 对齐偏差 | — | 中位 0.227 m（= skipBottom 固有值，非 bug） |
| 坐标系漂移 | — | 中位 0.0087 / p99 0.0223 / max 0.0290（阈值 0.05） |

#### 顺带确认

- `datasets_aug/metadata/sample_params.json` 是 `datasets` 的**逐字节拷贝**（7500/7500 条记录 JSON 全同，
  两文件同为 8,779,879 字节）。`copy_metadata()` 只做 `shutil.copy2`，不重算 ——
  修复后（离群点变小 + 坐标系保住）这份拷贝对 aux 是**正确**的（方案 A：aux 用建筑 bbox，不受噪点影响）
- `pointCloudInfo.skipBottom` 7500 条全为 `true` → C++ 导出已跳过底面，
  Python 的 `drop_bottom_face(0.015)` 是冗余兜底，只会再啃掉墙脚 0.015，
  造成 ~0.2 m 的系统性 Z 偏移。重生成时建议 `--no-drop_bottom`

#### ⚠️ 待办：必须重训

点云变了 → 现有模型作废。**分类和回归都要重训**（`train_pct_cls.sh` 和 `train_pct_reg_normals.sh`
的 `DATA_ROOT` 都是 `datasets_aug`；`main.py` 同样用 max 半径归一化，一样被离群点影响）。

**重训流程**（详见《八、修改指南》）：
```bash
# 1) 先空跑一个小类验证遥测（drift 应全部 < 0.05）
python augment_dataset.py --input /home/xubo/pointnet/datasets \
    --output /home/xubo/pointnet/datasets_aug --classes Cuboid \
    --outlier_extent 0.08 --no-drop_bottom --overwrite
# 2) 确认无误后全量
python augment_dataset.py --input /home/xubo/pointnet/datasets \
    --output /home/xubo/pointnet/datasets_aug \
    --outlier_extent 0.08 --no-drop_bottom --overwrite
# 3) 重训（分类 + 回归）
bash train_pct_cls.sh
bash train_pct_reg_normals.sh
```

**重训后需要复盘的结论**：第七章里"哪些参数学不动（corr≈0）"的分析建立在被污染的归一化上，
需要重做 —— 那几个参数（wingRatio / ridgeRatio / middleBulge / middleTopRadius / offsetX/Y）
未必真的不可辨识。

> ✅ **已在 v2.3.8 完成复盘**：其中 `ridgeRatio` / `middleTopRadius` 确实翻身了，
> 但 `wingRatio` / `wingWidthRatio` / `middleBulge` / `offsetX/Y` 在干净数据上**仍然不可辨识**
> （有些还更差）—— 它们不是被数据质量埋掉的，是输入里真的没信号。详见 v2.3.8 与第七章结论 3。

**改动文件**：`parammodeler_dock.cpp`（`robustBBoxFromPoints` + 三处 bbox 累积）、
`augment_dataset.py`（outlier extent/钳制/坐标系映射/遥测）
（训练侧脚本 `train_pct_simple/augment_dataset.py`，不在插件仓库内）

---

### v2.3.8 (2026-09-14) — 回归 v4 重训完成 + 切默认后缀 + canonical 对齐线索

v2.3.7 的待办（"回归与分类都要重训"）里，**回归这一半已完成**，产物 `logs/pct_reg_*_v4_normals/`（13 类），
插件默认后缀切到 `_v4_normals`。分类那一半**尚未执行**。

#### 改动

| 位置 | 改动 |
|---|---|
| `parammodeler_config.cpp:92` | `pctRegressionSuffix()` 默认值 `_v3_normals` → **`_v4_normals`** |
| `parammodeler_config.cpp:233` | 设置对话框提示文案同步（并说明 v4 用的是修复后数据） |
| `parammodeler_config.cpp:258` | 「恢复默认值」按钮同步 |
| `parammodeler_config.h:32` / `parammodeler_pointnet.cpp:127` | 注释里的版本名同步 |

用户已有的 `QgsSettings` 值不受影响（改的只是**默认值**）；老配置里存着 `_v3_normals` 的
需要在设置里手动改，或点「恢复默认值」。

#### 训练结果

训练脚本 `train_pct_reg_normals_v4.sh`（= v3 脚本换输出目录），超参与 v3 完全一致
（basic + 法向量 / 2048 点 / 100 epoch / batch 4 / lr 3e-4 / jitter 0.003），**唯一变量是数据**。
完整数据、逐类逐参数对比、以及"读 R² 的正确方法"见**第七章《回归 v4_normals》**。

一句话：**13 类 Mean MAE 0.770 → 0.709（−8%），62 个参数平均 R² 0.599 → 0.637；
可用类（avg R²>0.6）从 6 个涨到 9 个；被离群点毁掉的那批参数全部翻身**
（`TruncatedPyramidRoof.bottomWidth` −0.479 → +0.658 是最典型的一个）。

#### 三条要记住的结论

1. **旧数据上"某参数学不动"的判断作废。** `ridgeRatio` / `bottomWidth` / `wallRatio` /
   `middleTopRadius` / `coneHeight` 这批在干净数据上全部转正或大幅改善。
2. **看 R² 会误判。** `pred_std/true_std ≈ 0.3` 时模型基本在输出常数，corr ≈ 0，
   此时 R² 由"常数偏了多少"决定，在 50 个测试样本上偏一点就能变成负数 ——
   负 R² 是"没信号"的签名，不是"比均值还差"。判据用 **corr + pred_std/true_std**。
   `Cylinder.radius` 的 R² 从 0.981 掉到 0.940 看着像退步，实际 corr = 0.990、p/t = 1.07，
   模型完全正常。
3. **有一批参数是"输入里真的没有信号"，扩量和换架构都救不了**：7 个
   （`middleBulge` / `wingWidthRatio` / `offsetX,offsetY` / `ridgeRatio` / `wingRatio` /
   `middleTopRadius`），共同点是**"建筑内部找参照"的相对量/偏移量**，而能读出来的全是绝对尺寸。
   `IndentedCuboid.offsetX/offsetY` 大概率与 rz 同源（偏移方向在导出坐标系里被 rz 混合）。

#### 顺带确认（不是 bug）

- `v4_reg_train.log` 的 test 段打印 `=== PCT Regression (neighbor) | mode=test ===`，
  **实际用的是 basic** —— 那行只打印 `args.pct_variant`（CLI 默认 neighbor），
  建模型走 `load_checkpoint()` 从 checkpoint 读 `pct_variant`。指标可信，日志文案不可信（v3 同）。
- 插件侧 `robustBBoxFromPoints()`（1% 分位）**保持不动**：它防的是实际加载的真实点云里的离群点，
  和训练数据修没修无关。

#### 下一步

**canonical 对齐「整套」重训**是本项目当前最有价值的未知项，详见第九章优先级 2。
要点：aux 的 `bbox_x/bbox_y/aspect_ratio_xy` 是**旋转后**点云的 AABB，被 rz 撑大，
rz≈45° 时接近正方形（实例 `Cuboid/sample_00001`：`14.5×6.8` 的真值对应
`15.128×14.019` 的 AABB）。原先"净效果为零"的 A/B 只测了半套做法（转云不转 aux），
且跑在损坏数据上，不能作为否证。建议先只做 `Cuboid` 一类的 30 分钟最小验证。

---

### v2.3.9 (2026-09-14) — 对齐微调体验：底部截面 footprint + 2D 点选平移

这版不是改 DL 模型，而是解决 QGIS 3D 可视化里"自动对齐大致可以，但角点还是差一截"的问题：
自动部分继续保留 bbox 逻辑，同时给人工校正加一个明确的锚点和一次点击平移。

#### 改动

| 位置 | 改动 |
|---|---|
| `parammodeler_dock.cpp` | 点云显示 bbox 改为硬 bbox + 全点云稳健 bbox + 底部截面 footprint 组合 |
| `parammodeler_dock.cpp/h` | 新增 `Pick target for model anchor`：2D 画布显示红 X 锚点和 +X/+Y 方向线，点击目标后平移 `TX/TY` |
| `buildmesh.cpp` | 移除高频 `[BuildMesh] ...` 输出，保留对齐诊断日志 |
| `README.md` / `dl-pipeline-log.md` | 同步当前对齐策略和人工微调工作流 |

#### 自动对齐的当前口径

- 点云显示仍**不依赖 aux**；显示尺度来自原始点文件 + metadata 的 `center/scale`。
- 模型效果仍**依赖 aux**；PCT 回归推理会传 `bbox_x/y/z`、`scale`、`bbox_volume`、`aspect_ratio_xy`。
- 对齐目标优先用实际显示点云的 bbox，而不是 metadata bbox；显示什么就对什么。
- X/Y 优先用底部截面 footprint；底部截面不可靠时退回全点云稳健 bbox；Z 用硬 `z_min`。

#### 手动平移的当前口径

- 红 X 是模型的语义锚点：局部 `(0,0,0)` 经过当前旋转和平移后的 XY。
- 角锚定类的红 X 是外包络左下角；LHouse 不是内拐角，TwoGableHouses 是第一屋 A 点。
- 圆形类红 X 是底面中心；TriPrismPyramid 暂不作为当前重点。
- 点击目标点后只改 `TX/TY`，不改 `TZ`、`rx/ry/rz`、形状参数；因此适合作为自动对齐后的最后一步人工修正。

#### 未验证

本轮未跑 MSBuild / cl / cmake 编译验证。改动涉及 QGIS UI 类、QgsMapToolEmitPoint、QgsRubberBand
以及若干调试日志，下一次正常编译时重点看 `parammodeler_dock.cpp/h` 的新增成员和 include。
