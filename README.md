# ParamModeler

ParamModeler 是一个面向 QGIS 的参数化三维建筑基元建模与点云识别插件。插件围绕“从建筑点云识别基元类型，再估计参数化模型，并在 QGIS 3D 中叠加微调”的流程设计，支持建筑基元生成、点云导入、深度学习分类、参数回归、三维叠加显示和多格式导出。

一句话概括：

```text
点云输入 -> 建筑基元分类 -> 参数估计 -> 参数化模型生成 -> QGIS 3D 叠加微调 -> 成果导出
```

项目的核心目标不是只输出一个不可编辑的三角网格，而是把建筑点云转换为结构化、可解释、可编辑、可复现的参数化三维模型。

## 最近更新（2026-09-21）

参数估计窗口按钮顺序为“分类 / 估计参数 / 一键完成 / 返回微调”。加载有效点云后，“一键完成”按当前后端依次执行分类、回归（沿用几何校正开关和强度百分比）、加载模型与点云、自动对齐，成功后关闭窗口返回微调。任何一步失败即中止并保留窗口；运行时禁用输入修改和重复点击。原有分步按钮保留，三面拟合角点仍需人工选面，不包含在自动流程中。

几何校正从“完全覆盖”改为可调融合：`0%` 保持 PCT 预测，`100%` 等价于旧版点云几何强修正，默认 `70%` 按 `PCT + 0.7 * (几何测量 - PCT)` 写回参数。当前支持 Cuboid / Cylinder / GabledRoof，并已扩展到第一批 v4-1 四类 HalfCylinderRoof / TruncatedPyramidRoof / LHouse / IndentedCuboid；评估 CSV schema v2 会记录当次推理是否启用校正及强度。

新增辅助虚拟角点对齐（待 QGIS 实测）：点击“3D 对齐”右侧下拉箭头，选择“三面拟合角点...”，依次选同一外角附近的两面墙和一个平顶面。青色十字表示已选面的拟合中心，黄色方框表示最终虚拟角点；确认后点击“应用对齐”才移动模型。直接点击“3D 对齐”仍使用原来的点云点拾取。

第一版仅启用 Cuboid、LHouse、IndentedCuboid；圆形和斜屋顶类不启用。选点应靠近同一个目标角，但落在面内部。邻域半径使用点云坐标单位，修改会清空已选面；“上一步”可重选，“相机导航”可暂停拾取以旋转观察，取消勾选后继续。右键（选面时）、Esc 或取消按钮退出，不移动模型。角点本身不必有采样点，但三面都需要足够的局部点云支持；若选点失败，浮窗和鼠标提示会说明是未点到点云、点数太少、像边线、混到多个面、面方向不符或交点不稳定。

三面拟合已从严格判据改为宽容候选模式：青色十字表示质量较好的拟合面，橙色十字表示弱候选面或被拒候选中心。弱候选也允许继续三面求交，但会提示内点数和 RMS；方向不符、第二墙过平行或三面交点不稳定时，橙色十字会保留最后一次候选位置，方便调整选点和半径。

算法使用局部一致性筛选和 Eigen 平面拟合，拒绝点数不足、退化、方向不符及远距离外推的交点；只修改平移，不自动旋转或改变参数。新增 Eigen3 构建依赖（本机 qgis_dev 已有）。28 项拟合检查、22 项拾取数学检查、6 组标记绘制检查及 5 项翻译测试通过；未完整构建 DLL 或在 QGIS 中验证交互。

若 VS 报插件 `CMakeLists.txt` 自定义生成退出代码 1，应查看前面的 CMake 错误。本机曾因 `Eigen3_DIR-NOTFOUND` 失败：插件现从已配置的 `Qt5_DIR` 对应安装前缀补充 Eigen3 搜索路径，无需在 VS 中激活 conda。独立安装 Eigen3 时仍可通过 CMake 的 `Eigen3_DIR` 指定位置。

### 2026-09-20

基于 v2.3.13 增加当前评估 CSV、整理顶部菜单，并接入简体中文界面。CSV 记录原始预测、几何校正、当前微调值和对齐差值，同时保留实际权重路径；v4-1 仍仅对应四类重训模型，不代表全类别升级。

用户已反馈中文界面可用。开发侧通过 4 个 C++ 文件的语法检查及 5 项离屏翻译测试；尚未完成 CSV 导出的完整 QGIS 流程验收。本轮仅修改插件，QGIS 本机只补充语言包，没有修改核心源码。具体变更与验证范围见 `dl-pipeline-log.md`。

## 主要功能

- 支持简体中文界面，随 QGIS 界面语言在插件启动时加载。基元类型名、参数键、模型路径和 CSV 字段保持英文原值；切换语言需重启 QGIS。

- 顶部菜单按 `Scene`（场景）、`Export`（导出）、`Dataset`（数据集）分组。导出区分模型采样点云与输入点云转换；无有效输入文件时禁用转换，无插件场景内容时禁用清除。
- 支持从导出菜单选择 `Current evaluation CSV...`，保存原始预测（映射到 UI 参数语义）、几何校正结果、当前微调参数和位姿。记录实际回归权重路径，区分四类 v4-1 与其他模型；未推理或切换输入/基元后预测列留空。
- CSV 的对齐差值为当前模型世界坐标 bbox 减去当前显示点云的全点云稳健 bbox；`min_delta_z` / `max_delta_z` 对应底部/顶部差值。它们不是真值误差，也不是点到面的拟合误差；缺少匹配的显示点云时不计算。每次导出一个快照，不追加。

- 支持 14 类建筑基元的参数化建模。
- 支持本地 OpenGL 快速预览。
- 支持将当前模型加载到 QGIS 3D 场景。
- 支持 Qt3D 实时预览实体，用于参数拖动时快速更新拟合模型。
- 支持外部点云导入，包括 `.ply`、`.txt`、`.xyz`、`.pts`、`.las`、`.laz`。
- 支持 PointNet、PointNet++、PointNeXt、PCT 四类后端进行点云分类（当前主力：PCT，98.92% F1，v4 分类待重训）
- 支持调用外部 Python 参数回归模型，并将结果回填到插件 UI（当前默认回归后缀 `_v4_normals`）
- 支持随机参数生成和批量深度学习数据集导出（500 样本/类，TwoGableHouses 1000 样本，14 类共 7500）
- 支持 OBJ、STL、JSON、PLY、深度学习 TXT 点云等格式导出。
- 支持 2D 地图画布点选目标点，将当前模型锚点平移到点云指定位置，便于 QGIS 3D 里人工微调。
- 新增 `Align in 3D`（开发中，原按钮名 `Pick 3D target for model anchor`）：白描边红 X 表示模型上部对齐点，黄框表示点云候选点；长方体/L 型/凹槽体取原左下角正上方的顶角，屋顶类取同一墙角的檐口，圆柱取顶面中心，圆顶/锥顶/塔取顶点。左键将这个参考点平移到目标真实 XYZ，同时更新 TX/TY/TZ。红 X 固定屏幕大小、叠加显示；右键、Esc 或再次点击按钮取消。原有生长锚点、2D 点选、bbox 和底部截面对齐保留。屏幕标记已通过离屏绘制测试，仍需重新编译后验证 QGIS 实机效果。
- 主面板按工作流整理：基元区只选类型；点云与对齐区放估计入口、3D/2D 对齐和线框开关；模型参数区放随机参数与恢复预测；位置与旋转放最后。参数区高度随当前基元调整。几何校正开关只放在估计窗口，本次会话内保留状态。
- 高度行新增上下箭头：↑ 固定下端、向上增高；↓ 固定上端、向下增高。只影响人工调整，切换瞬间不移动模型；推理回填、恢复预测、随机参数和数据生成不触发补偿。总高度固定屋脊/顶面，墙高固定檐口，塔楼各段固定该段上端；凹槽深度默认向下（固定上沿），可切到固定凹槽底面。比例参数及 TriPrismPyramid 不调整。

## 支持的建筑基元

| 类型标识 | 说明 | 主要参数 |
|---|---|---|
| `Cuboid` | 长方体 | `length`, `width`, `height` |
| `Cylinder` | 圆柱体 | `radius`, `height` |
| `LHouse` | L 型房屋 | `outerLength`, `outerWidth`, `cutoutLengthRatio`, `cutoutWidthRatio`, `height` |
| `ConeCylinder` | 圆柱 + 圆锥 | `radius`, `totalHeight`, `cylinderRatio` |
| `GabledRoof` | 人字形屋顶房屋 | `length`, `width`, `totalHeight`, `wallRatio` |
| `PyramidRoof` | 金字塔屋顶房屋 | `length`, `width`, `totalHeight`, `wallRatio` |
| `TruncatedPyramidRoof` | 截顶金字塔屋顶 | `bottomLength`, `bottomWidth`, `topLengthRatio`, `topWidthRatio`, `totalHeight`, `wallRatio` |
| `HalfCylinderRoof` | 半圆柱拱顶房屋 | `length`, `width`, `wallHeight`（`roofRadius = width/2` 派生） |
| `CylinderDome` | 圆柱穹顶 | `radius`, `totalHeight`, `cylinderRatio`, `bulge` |
| `IndentedCuboid` | 凹陷长方体 | `outerLength`, `outerWidth`, `outerHeight`, `innerLengthRatio`, `innerWidthRatio`, `innerHeight`, `innerMinXRatio`, `innerMinYRatio` |
| `AsymmetricGableHouse` | 非对称人字形房屋 | `length`, `width`, `totalHeight`, `wallRatio`, `ridgeLength`, `ridgeRatio` |
| `FourStageRoundTower` | 四段式圆塔 | `baseRadius`, `baseHeight`, `middleHeight`, `middleTopRadius`, `middleBulge`, `coneHeight` |
| `TwoGableHouses` | 双人字形房屋 | `length1`, `length2`, `width`, `totalHeight`, `wallRatio`, `angle`, `ridgeRatio` |
| `TriPrismPyramid` | 三棱柱 + 三棱锥 | `leg`, `baseSide`, `totalHeight`, `pyramidRatio` |

`CylinderDome` 是当前正式类型名，代码中仍兼容旧名 `CylinderHemisphere`。

**锚点（原点）按类划分**（v2.3.5 起，方案 B；判定集中在 `BuildMesh::usesCornerAnchor()`）：

| 锚点 | 类 | 行为 |
|---|---|---|
| **底面左下角** | Cuboid / GabledRoof / PyramidRoof / TruncatedPyramidRoof / HalfCylinderRoof / IndentedCuboid / AsymmetricGableHouse / LHouse / TwoGableHouses | 构建时原点即 (0,0,0)，不再居中；长/宽沿 +X/+Y **单边生长**，旋转绕左下角 |
| 底面中心 | Cylinder / ConeCylinder / CylinderDome / FourStageRoundTower / TriPrismPyramid | 走 `centerMeshOnBaseFace()` 平移到 bbox 中心；旋转绕中心 |

锚点 / 生长原点 / 旋转轴**三者绑定，不能拆**（曾试"生长用左下角、旋转留中心"，自洽性破裂）。
详见 `scripts/grow-anchor-design.md`。

## 工作流

### 1. 参数化建模

用户在插件面板中选择建筑基元类型，并通过滑块或数值框调整参数。插件调用 `BuildMesh` 生成三角网格，再将网格送到本地 OpenGL 预览窗口或 QGIS 3D 场景中显示。

### 2. 训练数据生成

插件可以随机生成每类基元的参数，调用网格生成模块得到三维模型，再从模型表面采样点云，导出为深度学习训练用 TXT 文件。

深度学习输入点云通常为固定点数，例如 2048 点，格式为：

```text
x y z
x y z
x y z
...
```

导出时会进行中心化和按最大半径归一化，同时记录 `pointCloudInfo`，包括包围盒、中心、尺度等信息，用于后续参数估计时恢复尺度。

当前标准数据集规模为 **500 样本/类**（train 400 + val 50 + test 50），TwoGableHouses 为 **1000 样本**，14 类共 **7500 样本**。

### 3. 点云分类

插件加载外部点云后，通过 `PointNetRunner` 调用外部 Python 脚本进行分类。目前支持：

- PointNet
- PointNet++
- PointNeXt
- PCT (Point Cloud Transformer) — 当前主力

分类流程大致为：

```text
点云文件
-> 插件预处理为网络输入
-> QProcess 调用 Python predict
-> 解析 JSON 输出
-> 得到 top1 / topK 类别
-> 自动切换 UI 中的基元类型
```

### 4. 参数估计

分类完成后，插件可继续调用对应基元的参数回归模型。回归输出会经过 `pointNetParamsToUiParams()` 映射到插件 UI 参数，再由 `PointNetRunner::applyToUI()` 写回界面。

参数估计流程大致为：

```text
点云文件
-> 参数回归 Python 脚本
-> 输出参数 JSON
-> 插件参数映射
-> 回填 UI
-> 更新参数化模型
```

### 5. QGIS 3D 叠加微调

插件支持将输入点云和估计得到的参数化模型一起加载到 QGIS 3D 中。用户可以通过调整参数，使半透明参数模型与真实点云进一步对齐。

当前 3D 显示有两条路线：

- 正式图层路线：`MeshData -> MultiPolygonZ -> GPKG -> QgsVectorLayer -> QgsVectorLayer3DRenderer`
- 实时预览路线：`MeshData -> Qt3D QBuffer -> QGeometryRenderer -> QEntity`

建议使用方式：

```text
微调阶段：使用 Qt3D 实时实体快速更新
成果阶段：导出为 GPKG / OBJ / STL / JSON / PLY 等格式
```

## 导出格式

### JSON

保存当前基元类型、位姿参数和形状参数。JSON 适合保存参数化结果，因为它可解释、可编辑、可复现。

示例：

```json
{
  "type": "GabledRoof",
  "params": {
    "length": 12.0,
    "width": 8.0,
    "totalHeight": 6.0,
    "wallRatio": 0.65
  }
}
```

### OBJ

导出当前参数化模型的三角网格，适合通用三维软件查看和后处理。

### STL

导出 ASCII STL 网格，适合三维模型交换、打印或其它网格处理流程。

### PLY 点云

从当前参数化模型表面采样点云，默认会跳过底面，使合成点云更接近真实采集场景。

### 深度学习 TXT

导出固定点数的归一化点云，用作 PointNet 系列模型的输入。

## 代码结构

```text
parammodeler/
├── parammodeler.cpp / .h              # QGIS 插件入口，注册菜单、工具栏和 Dock 面板
├── parammodeler_dock.cpp / .h / .ui   # 插件主 UI 和调度中心
├── buildmesh.cpp / .h                 # 14 类建筑基元网格生成（双接口 + 锚点判定 usesCornerAnchor）
├── meshdata.h                         # 网格数据结构 + 所有 Params 结构体
├── parammodeler_params.cpp            # 参数访问器（spinbox → 派生参数）
├── parammodeler_scene3d.cpp / .h      # QGIS 3D 图层加载与 Qt3D 实时预览
├── parammodeler_pcdloader.cpp / .h    # 外部点云读取
├── parammodeler_pcdtypes.h            # 点云数据结构
├── parammodeler_pointnet.cpp / .h     # 外部深度学习模型调用
├── parammodeler_dlutils.cpp / .h      # DL 参数映射 + 元数据 + JSON 辅助
├── parammodeler_datasetgen.cpp / .h   # 数据集批量生成
├── parammodeler_randomizer.cpp / .h   # 参数随机化
├── parammodeler_config.cpp / .h       # 路径配置管理
├── parammodeler_export.cpp / .h       # 统一导出（OBJ/JSON/PLY/STL/GPKG）
├── exportpointcloud.cpp / .h          # PLY 和深度学习 TXT 点云导出
├── CMakeLists.txt                     # 插件构建配置
└── parammodeler.qrc                   # Qt 资源文件
```

## 核心模块说明

### `ParamModelerDock`

插件主面板和当前的总调度中心。主要负责：

- 基元选择
- 参数控件读取和写入
- 随机参数生成
- 本地预览刷新
- 导出菜单响应
- 点云导入
- 深度学习分类和参数估计弹窗
- 参数估计结果回填
- 加载模型和点云到 QGIS 3D

该文件目前承担职责较多，后续适合继续拆分。

### `BuildMesh`

参数化建模核心。入口函数为：

```cpp
MeshData BuildMesh::build( const QString &primitiveType, ParamModelerDock *dock );
```

它根据当前基元类型调用对应的建模函数，并返回统一的 `MeshData`。

### `ParamModelerScene3D`

负责 QGIS 3D 显示。当前同时支持正式图层加载和 Qt3D 实时预览。

正式图层适合保存和成果管理；Qt3D 实时实体适合参数拖动和点云拟合过程中的快速刷新。

### `PointNetRunner`

负责通过 `QProcess` 调用外部 Python 深度学习脚本。它不在 C++ 中实现神经网络，而是作为 QGIS 插件和 Python 推理工程之间的桥。

目前 Python 可执行文件、脚本路径、日志目录和数据集路径仍以本机硬编码路径为主，例如：

```text
E:/mambaforge/envs/pointnet_train/python.exe
E:/pointnet/pointnext_simple/main.py
E:/pointnet/pointnext_simple/main_reg.py
E:/pointnet/datasets_aug/metadata/sample_params.json
```

这些路径后续建议改为插件设置项。

## 当前技术路线

推荐的主线为：

```text
参数化基元定义
-> 随机参数生成
-> BuildMesh（锚点见下：长方体类左下角 / 圆形类底面中心）
-> 网格表面采样点云 + 归一化
-> PCT 分类（98.92% F1）
-> PCT 回归（v4_normals：basic + PCA 法向量，修复数据集上重训）
-> 参数级数据驱动校正（实验：Cuboid / Cylinder / GabledRoof + v4-1 四类）
-> pointNetParamsToUiParams 映射 + applyToUI 回填
-> applyMetadataRz 回填朝向（metadata 的导出真值，不是 DL 预测）
-> alignModelToPointCloud 自动对齐（同锚点语义、旋转后的 bbox 极值对齐）
-> QGIS 3D 叠加点云与模型
-> 人工微调（Ctrl+滚轮精调 + DL 锚点复位 + 2D 点选目标平移）
-> 导出参数化成果
```

传统几何反演模块已移除，深度学习路线为唯一主线。

## 当前已知问题

- 🔴 **参数估计精度不足**：v4 重训后 9/13 类 avg R²>0.6，但仍有 7 个参数属"输入里没有信号"（`middleBulge`、旧 `wingRatio/wingWidthRatio`、旧 `offsetX/offsetY`、`ridgeRatio`、`middleTopRadius`，corr≈0、预测方差只有真值 1/3）。v2.3.10 起先把 LHouse / IndentedCuboid / TruncatedPyramidRoof / HalfCylinderRoof 的训练 target 改成更贴近 footprint 或物理约束的形式，待重训验证。
- ⚠️ **分类尚未在新数据上重训**：`pct_cls_v2` 的 98.92% F1 是修复前的 `datasets_aug` 上的结果，`train_pct_cls_v4.sh` 已就绪但未执行。
- 位姿参数（rx/ry/rz/tx/ty/tz）尚未进入回归训练。auto-align 做平移对齐（v2.3.4 起改用点云 **bbox 极值** 而非采样质心，见下），**朝向 rz 从 v2.3.6 起由 metadata 真值回填**（见下）——但只对"有 metadata 记录的输入"有效，纯推理（无 metadata）时朝向仍是默认值 + 人工微调。
- **rz 无法通过训练补**：它在当前数据形态下不可辨识（圆柱类无定义、矩形类只确定到 mod 180°），`_rot` 实验已因此弃用，详见 `scripts/README.md`。
- 自动对齐仍是几何启发式：点云对齐目标优先取"底部截面 footprint + 1% 分位稳健 bbox"，比全点云硬 bbox 稳，但当回归尺寸本身偏了或可见点云缺角时，仍需要 2D 点选目标平移做最后校正。
- `parammodeler_dock.cpp` 职责仍偏重，但已拆分出 7 个辅助模块（params/dlutils/randomizer/datasetgen/config/export/scene3d）。

## 已解决（v2.3.11）

- ✅ **参数级数据驱动校正最小闭环**：新增 `Enable geometry correction` 开关（默认关闭；主面板和分类/参数估计弹窗里同步显示）。开启后在 PCT 回归后、写 UI 前，对 Cuboid / Cylinder / GabledRoof 加一层点云几何修正；2026-09-21 起增加强度百分比，避免几何测量值完全覆盖 PCT 预测，并扩展到 v4-1 第一批四类。
- ✅ **Cuboid**：用反归一化后的点云、按 metadata rz 逆旋到 canonical 后，从底部 footprint 修正 `length/width`，从 Z range 修正 `height`。
- ✅ **Cylinder**：用稳健 XY 中心和半径分位数修正 `radius`，用 Z range 修正 `height`。
- ✅ **GabledRoof**：用底部 footprint 修正 `length/width`，并尝试从高度剖面估计 `wallRatio`；估不出来时保留 PCT 的墙/屋顶比例。
- ✅ **v4-1 第一批四类**：HalfCylinderRoof 修外框和墙高；TruncatedPyramidRoof 修底面、顶面和墙/屋顶高度比例；LHouse 修外包络和可见缺口比例；IndentedCuboid 修外框并在可估时修内凹尺寸、深度和偏移。弱几何量估不稳时保留 PCT。
- ✅ **实验安全边界**：点云读不到、metadata 缺失或估计失败时直接跳过，不影响原始 PCT 回填；当前不做残差校正模型。

## 已解决（v2.3.10）

- ✅ **回归 target 重参数化**：不推翻 UI / BuildMesh，只改变 dataset metadata 和 DL 回填支持的新标签；旧模型返回旧 key 时仍兼容。
- ✅ **HalfCylinderRoof 去掉 `radius` target**：半圆屋顶半径固定为 `width/2`，训练只保留 `length width wallHeight`。
- ✅ **TruncatedPyramidRoof 顶面改比例**：`topLength/topWidth` 改为 `topLengthRatio/topWidthRatio`，用底面尺寸派生顶面绝对尺寸，降低尺度耦合。
- ✅ **LHouse 改外包络 + 缺口比例**：`outerLength outerWidth cutoutLengthRatio cutoutWidthRatio height`，比旧 `wingRatio/wingWidthRatio` 更直接对应 footprint 缺口。
- ✅ **IndentedCuboid 改 inner 位置/尺寸比例**：`innerLengthRatio innerWidthRatio innerMinXRatio innerMinYRatio`，保留 `innerHeight` 绝对高度；UI 仍显示 inner 长宽和偏移滑块。
- ✅ **FourStageRoundTower / TwoGableHouses 暂不调整**：先减少变量，后续看这一轮重训结果再动。

## 已解决（v2.3.9）

- ✅ **自动对齐目标更贴近底部 footprint**：加载点云时同时统计硬 bbox、1% 分位稳健 bbox、底部截面 bbox。X/Y 优先用底部截面，Z 保留硬底面 `z_min`；底部点数不足或 footprint 太小时退回全点云稳健 bbox。
- ✅ **保留 bbox 兜底**：底部截面不是替代旧方案，而是和稳健 bbox 组合使用；点云没有足够底部截面时仍能自动对齐。
- ✅ **人工点选平移**：新增 `Pick target for model anchor`。点击后在 QGIS 2D 画布显示当前模型锚点红 X，并显示红色 +X / 绿色 +Y 方向线；再点点云目标位置，只更新 `TX/TY`，不改尺寸、高度、旋转。
- ✅ **锚点可视化语义明确**：红 X 是模型的生长/旋转锚点（局部 `(0,0,0)` 经过当前旋转和平移后的位置），不是旋转后的 AABB 左下角。长方体类含 LHouse / TwoGableHouses 用外包络左下角；圆形类用底面中心。
- ✅ **调试日志降噪**：移除高频 `[BuildMesh] ...` 输出，保留 `[PointCloudBBox]`、`[PointCloudFootprint]`、`[Align]`、`[ManualAlign]` 这些定位对齐问题真正有用的日志。

## 已解决（v2.3.7）

- ✅ **离群点不再主导自动对齐**：点云显示 bbox 改为 1% 分位稳健统计，避免少量离群点把左下角参考点拉走。
- ✅ **数据增强坐标系漂移已定位并修复**：`datasets_aug` 早期增强会重新归一化且不映射回导出坐标系，导致显示反归一化和训练尺度都受污染；修复后回归 v4 在干净数据上重训。

## 已解决（v2.2.0 → v2.3.0）

- ✅ 坐标系不统一 → `centerMeshOnBaseFace()` 统一底面中心原点（**v2.3.5 起改为按类划分，见下**）
- ✅ 微调无锚点 → DL 预测值存储 + "↺ Reset to DL prediction" 一键复位
- ✅ 参数 UI 扁平 → 8 类复杂基元加分组标题
- ✅ Ctrl+滚轮精调 → `FineTuneFilter` 事件过滤器
- ✅ auto-align 代码重复 → `alignModelToPointCloud()` 共用函数

## 已解决（v2.3.6）

- ✅ **朝向歪（点云带 rz 时模型不转）** → 新增 `applyMetadataRz()`：从输入文件的 metadata
  记录里读 `params.rz`（导出时 `applyPose` 用的那个角，点云坐标本身就是它转出来的），
  回填到 `spinBoxRKappa`。**不做训练**——rz 在标签层面不可辨识（见 `scripts/README.md`），
  只能取真值。该值同时记入 DL 锚点，`↺ Reset` 会恢复完整朝向。
- ✅ **对齐参考点与点云不同语义** → `alignModelToPointCloud` 改为在**施加 pose 旋转之后**的
  mesh 上算 bbox。点云侧 bbox 是旋转后点集的极值，模型侧必须同样先转再取极值；
  旋转绕 mesh 自身原点（= 锚点），rz=0 时 `posMat` 是单位阵，行为与 v2.3.5 完全一致。
- ⚠️ 仅对**有 metadata 记录的输入**有效（`datasets_aug`）；PLY / 无 `params` 的记录读不到 rz，
  此时保持当前朝向不变（调试日志会记 `<none>`）。

## 已解决（v2.3.5）

- ✅ **调参时模型往两边长、把已对齐的部分推开** → 锚点改为按类划分（方案 B 落地）：
  锚点 / 生长原点 / 旋转轴三者绑定，**长方体类（含 LHouse / TwoGableHouses）左下角锚定**，
  长/宽沿 +X/+Y 单边生长、旋转绕左下角；**圆形类保持底面中心**。
  判定集中在 `BuildMesh::usesCornerAnchor()`，`parammodeler_scene3d.cpp` 无需改动。
- ✅ 高度误差的表现改善：中心锚定下高度预测偏了会**上下各分一半**（既浮起又扎进地里），
  角锚定后误差只往一侧堆。
- ⚠️ 未消除：**旋转（rz）仍未处理**。切到角锚定后对齐目标变成"点云 bbox 左下角"，
  而点云若带 rz，其 bbox 左下角不是模型左下角的对应点（bbox 中心对中心在对称底面上是旋转不变的，
  bbox 角对则不是）——所以**带 rz 的点云在 X/Y 上可能反而更明显**，需等位姿估计落地。
  → **v2.3.6 已解决**：rz 由 metadata 真值回填，且对齐参考点改在旋转后的 mesh 上取，
  两侧恢复同一语义。

## 已解决（v2.3.4）

- ✅ **模型/点云对齐偏移** → 对齐目标从"采样点**质心**"改为"点云 **bbox 中心**"。质心被 60%/40% 的屋顶/墙采样比例顶高（平屋顶类 z≈0.8h），而 mesh 侧用的是 bbox 中心（z=0.5h），直接相减会把模型整体抬高 0.3h；非对称底面（LHouse/TwoGable）X/Y 同样偏。
- ✅ **显示路径与对齐路径各算各的** → `pointCloudMetadataLooksBuggy()` 判据共用；对齐优先取"场景中实际显示点云"的 bbox 中心（`m_displayCloudBBoxCenter`），坏 metadata 走 mesh 估算时两条路径也用同一几何。
- ✅ 坏 metadata（center≈0/scale≈1）不再拿去对齐 → 不再把模型对到世界原点。

## 后续建议

功能优先级：先补“保存/恢复微调会话”（点云路径、基元、参数、位姿和预测记录），再加入微调撤销/重做，随后考虑显示点云到模型表面的距离。以上是后续建议，尚未实现；当前参数 JSON 导出不等于完整会话保存。

1. 🔴 **数据端增强**：法向量通道 + 数据扩量（TwoGable/IndentedCuboid 各 1000+）+ 随机裁切。详见 `dl-pipeline-log.md` 第九章。
2. 加入位姿参数回归（先 rz，再 rx/ry/tx/ty/tz）。
3. 每种基元加小示意图帮助理解参数含义。
4. 稳定 QGIS 3D 中点云和模型的一键加载、透明显示和实时微调体验。
5. 整理论文或项目报告中的实验章节。

## 中文界面

在 `Settings > Options > General` 勾选 `Override System Locale`，将 `User interface translation` 设为简体中文（`zh_CN`），保存后重启 QGIS。不要仅修改数字/日期的 Locale。

- 插件：`i18n/parammodeler_zh_CN.ts` 是翻译源文件，`.qm` 已生成并通过 `parammodeler.qrc` 嵌入 DLL。重新编译 `plugin_parammodeler` 即可，不需要重编 `qgis_3d`。支持 QGIS 的 `zh_CN` 和 `zh-Hans` 简体中文标识，其他语言保留原文。
- QGIS：本机已将其自带 `i18n/qgis_zh-Hans.ts` 编译为 `build/output/i18n/qgis_zh_CN.qm`，只补语言包，未改核心源码。使用 `zh_CN` 文件名可同时匹配本机 Qt 的 `qt_zh_CN.qm` / `qtbase_zh_CN.qm`。其他安装目录需要在实际 `QgsApplication.i18nPath()` 目录部署语言包；本机生成的 QGIS 语言包不属于插件仓库。
- 后续新增文案：运行 `powershell -File scripts/update_translations.ps1 -Extract` 提取，再用 Qt Linguist 编辑 `.ts`，最后运行 `powershell -File scripts/update_translations.ps1` 重新生成 `.qm`，两者一起保存。
- 离屏验证：`E:/mambaforge/envs/qgis_dev/python.exe tests/test_translations.py`，检查嵌入资源、中英文界面、占位符与内部基元标识。

## 项目定位

ParamModeler 不是单纯的 QGIS 建模插件，也不是单纯的 PointNet 分类实验。它的定位是：

> 面向建筑点云的参数化三维建模插件：利用深度学习完成建筑基元分类与参数初估，再在 QGIS 3D 中通过可编辑参数模型进行叠加校正和成果导出。
