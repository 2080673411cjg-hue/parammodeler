# ParamModeler

ParamModeler 是一个面向 QGIS 的参数化三维建筑基元建模与点云识别插件。插件围绕“从建筑点云识别基元类型，再估计参数化模型，并在 QGIS 3D 中叠加微调”的流程设计，支持建筑基元生成、点云导入、深度学习分类、参数回归、三维叠加显示和多格式导出。

一句话概括：

```text
点云输入 -> 建筑基元分类 -> 参数估计 -> 参数化模型生成 -> QGIS 3D 叠加微调 -> 成果导出
```

项目的核心目标不是只输出一个不可编辑的三角网格，而是把建筑点云转换为结构化、可解释、可编辑、可复现的参数化三维模型。

## 主要功能

- 支持 13 类建筑基元的参数化建模。
- 支持本地 OpenGL 快速预览。
- 支持将当前模型加载到 QGIS 3D 场景。
- 支持 Qt3D 实时预览实体，用于参数拖动时快速更新拟合模型。
- 支持外部点云导入，包括 `.ply`、`.txt`、`.xyz`、`.pts`、`.las`、`.laz`。
- 支持 PointNet、PointNet++、PointNeXt 三类后端进行点云分类（当前主力：PointNeXt）
- 支持调用外部 Python 参数回归模型，并将结果回填到插件 UI
- 支持随机参数生成和批量深度学习数据集导出（500 样本/类，13 类共 6500）
- 支持 OBJ、STL、JSON、PLY、深度学习 TXT 点云等格式导出。

## 支持的建筑基元

| 类型标识 | 说明 | 主要参数 |
|---|---|---|
| `Cuboid` | 长方体 | `length`, `width`, `height` |
| `Cylinder` | 圆柱体 | `radius`, `height` |
| `LHouse` | L 型房屋 | `mainLength`, `mainWidth`, `wingLength`, `wingWidth`, `height` |
| `ConeCylinder` | 圆柱 + 圆锥 | `radius`, `totalHeight`, `cylinderRatio` |
| `GabledRoof` | 人字形屋顶房屋 | `length`, `width`, `totalHeight`, `wallRatio` |
| `PyramidRoof` | 金字塔屋顶房屋 | `length`, `width`, `totalHeight`, `wallRatio` |
| `TruncatedPyramidRoof` | 截顶金字塔屋顶 | `bottomLength`, `bottomWidth`, `topLength`, `topWidth`, `totalHeight`, `wallRatio` |
| `HalfCylinderRoof` | 半圆柱拱顶房屋 | `length`, `width`, `wallHeight`, `roofRadius` |
| `CylinderDome` | 圆柱穹顶 | `radius`, `totalHeight`, `cylinderRatio`, `bulge` |
| `IndentedCuboid` | 凹陷长方体 | `outerLength`, `outerWidth`, `outerHeight`, `innerLength`, `innerWidth`, `innerHeight`, `offsetX`, `offsetY` |
| `AsymmetricGableHouse` | 非对称人字形房屋 | `length`, `width`, `totalHeight`, `wallRatio`, `ridgeLength`, `ridgeRatio` |
| `FourStageRoundTower` | 四段式圆塔 | `baseRadius`, `baseHeight`, `middleHeight`, `middleTopRadius`, `middleBulge`, `coneHeight` |
| `TwoGableHouses` | 双人字形房屋 | `length1`, `length2`, `width`, `totalHeight`, `wallRatio`, `angle`, `ridgeRatio` |

`CylinderDome` 是当前正式类型名，代码中仍兼容旧名 `CylinderHemisphere`。

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

当前标准数据集规模为 **500 样本/类**（train 400 + val 50 + test 50），13 类共 6500 样本。

### 3. 点云分类

插件加载外部点云后，通过 `PointNetRunner` 调用外部 Python 脚本进行分类。目前支持：

- PointNet
- PointNet++
- PointNeXt

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
├── buildmesh.cpp / .h                 # 13 类建筑基元的网格生成
├── meshdata.h                         # 网格数据结构，保存顶点和三角面索引
├── parammodeler_scene3d.cpp / .h      # QGIS 3D 图层加载与 Qt3D 实时预览
├── parammodeler_pcdloader.cpp / .h    # 外部点云读取
├── parammodeler_pcdtypes.h            # 点云数据结构
├── parammodeler_pointnet.cpp / .h     # 外部深度学习模型调用

├── exportjson.cpp / .h                # JSON 参数导出
├── exportobj.cpp / .h                 # OBJ 网格导出
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
-> 网格表面采样点云
-> 点云增强
-> PointNeXt / PointNet++ 分类
-> PointNeXt / PointNet++ 参数估计
-> 插件回填参数
-> QGIS 3D 叠加点云与模型
-> 人工微调
-> 导出参数化成果
```

传统几何反演模块仍保留，但更适合作为 baseline、fallback 或对照实验，不建议作为当前主线。

## 当前已知问题

- `parammodeler_dock.cpp` 职责较重，后续建议拆出数据集导出、估计弹窗、参数映射和配置管理模块。
- Python 环境、模型脚本、日志目录和 metadata 路径已配置化（`parammodeler_config.h`），可通过设置对话框修改，不需要手动改源码。
- 🔴 **参数估计精度不足**：当前 PointNeXt `_aux` 模型对局部几何参数（`bulge` R²=-0.08、`middleBulge` R²=-0.85、`wallRatio` R²=0.06、`innerWidth` R²=0.21）几乎学不到。根因是 PointNeXt 的 set abstraction + max-pooling 对细粒度曲面形变不敏感。详见 `dl-pipeline-log.md` 第九章模型升级路线图。
- 点云与模型的初始配准仍是微调体验的关键，需要确保归一化、尺度恢复、包围盒、坐标系和 QGIS 3D 位姿一致。
- 位姿参数（rx/ry/rz/tx/ty/tz）尚未进入回归训练，当前仅自动对齐平移（auto-align）。

## 后续建议

1. 🔴 **升级模型架构**：从 PointNeXt 迁移到 Swin3D 或 PTv3，重点解决局部几何参数的回归精度问题。详见 `dl-pipeline-log.md` 第九章。
2. 增加训练数据增强：在线随机旋转、随机裁切、法向量通道。
3. 拆分 `ParamModelerDock`，降低主 UI 文件复杂度。
4. 稳定 QGIS 3D 中点云和模型的一键加载、透明显示和实时微调体验。
5. 加入位姿参数回归（先 rz，再 rx/ry/tx/ty/tz）。
6. 整理论文或项目报告中的实验章节，包括数据集生成、增强策略、分类结果、参数回归结果和误差分析。

## 项目定位

ParamModeler 不是单纯的 QGIS 建模插件，也不是单纯的 PointNet 分类实验。它的定位是：

> 面向建筑点云的参数化三维建模插件：利用深度学习完成建筑基元分类与参数初估，再在 QGIS 3D 中通过可编辑参数模型进行叠加校正和成果导出。
