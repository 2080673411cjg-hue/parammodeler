# ParamModeler - QGIS 参数化建筑基元建模插件

ParamModeler 是一个 QGIS 插件，用于参数化三维建筑基元建模、实时预览、多格式导出、点云加载、基元分类和参数反演。插件当前以内置规则分类和几何反演为主，同时预留了深度学习点云输入导出接口，后续可接入 PointNet、PointNet++、DGCNN、PointTransformer 等外部 Python 模型。

---

## 功能概览

### 参数化建模

- 支持 13 种建筑基元，包括长方体、圆柱、L 型房屋、人字屋顶、圆柱穹顶、四段式圆塔、双人字型房屋等。
- 支持 OpenGL 实时三维预览。
- 支持平移 `tx/ty/tz` 和旋转 `Omega/Phi/Kappa` 位姿参数。
- 支持将当前模型加载到 QGIS 3D 场景。
- 支持加载外部 PLY / LAS / LAZ 点云到 QGIS 3D 场景，与参数化模型叠加对比。

### 导出能力

- OBJ 网格导出。
- STL Mesh 导出。
- JSON 参数导出。
- PLY 点云导出。
- 深度学习输入点云 TXT 导出，默认 2048 点，中心化并按最大半径归一化。

### 点云分类与反演

- 支持外部点云输入。
- 当前内置分类器基于点云预处理、曲率关键点筛选、15 维几何特征和类型 profile 加权评分。
- 支持根据分类结果进行参数反演，并把估计参数写回 UI。
- 当前分类/反演可作为后续深度学习方案的 baseline 或 fallback。

---

## 支持的建筑基元

| 中文名称 | 类型标识 | 参数数 | 说明 |
|---|---:|---:|---|
| 长方体 | `Cuboid` | 3 | 长、宽、高 |
| 圆柱 | `Cylinder` | 2 | 半径、高度 |
| L 型房屋 | `LHouse` | 5 | 主体长宽、侧翼长宽、高度 |
| 圆锥圆柱 | `ConeCylinder` | 3 | 圆柱 + 圆锥 |
| 人字形屋顶房屋 | `GabledRoof` | 4 | 长、宽、墙高、屋顶高 |
| 金字塔屋顶房屋 | `PyramidRoof` | 4 | 四坡屋顶 |
| 截顶金字塔屋顶 | `TruncatedPyramidRoof` | 6 | 底面尺寸、顶面尺寸、墙高、屋顶高 |
| 半圆柱屋顶 | `HalfCylinderRoof` | 4 | 半圆拱顶屋，拱顶半径由宽度一半派生 |
| 圆柱穹顶 | `CylinderDome` | 4 | 圆柱 + 可调贝塞尔穹顶，兼容旧名 `CylinderHemisphere` |
| 凹陷长方体 | `IndentedCuboid` | 8 | 顶部凹槽长方体 |
| 非对称人字屋顶 | `AsymmetricGableHouse` | 6 | 屋脊可偏移的不对称双坡屋顶 |
| 四段式圆塔 | `FourStageRoundTower` | 6 | 短圆柱底座 + 低坡弧面大屋顶 + 顶部小尖锥 |
| 双人字型房屋 | `TwoGableHouses` | 6 | 两栋人字屋顶房屋转角拼接，夹角范围 90-180 度 |

---

## 3D 场景模块

当前 3D 场景加载逻辑已经从 `ParamModelerDock` 中拆出到 `ParamModelerScene3D`：

- `ParamModelerDock` 负责 UI 事件、文件选择、当前参数读取。
- `ParamModelerScene3D` 负责把参数化模型和外部点云加载到 QGIS 3D 视图。
- 模型加载仍使用临时 GeoPackage，保证 QGIS 3D 渲染管线完整刷新。
- 点云加载支持 PLY / LAS / LAZ，并创建 QGIS 3D 点符号图层。
- 自动同步开关仍通过 `onLoadToQGIS3D(false)` 刷新模型图层，不跳转相机视角。

该拆分是后续“真实点云 + 反演模型叠加微调”工作流的基础。

---

## 导出格式

### OBJ

- 导出当前参数化模型的 OBJ 网格。
- 导出时应用当前位姿变换。

### STL Mesh

- 导出 ASCII STL。
- 按三角面写入法线和顶点。
- 导出时应用当前位姿变换。

### JSON

- 保存当前基元类型、位姿参数和形状参数。
- `CylinderDome` 作为新的正式类型名导出。

### PLY 点云

- 从当前 mesh 表面按三角面面积加权随机采样。
- 默认采样 50000 点。
- 默认跳过底面，使导出点云更接近真实采集情况。
- 导出时应用当前位姿变换。

### 深度学习点云 TXT

菜单入口：`导出 / 加载 -> 导出深度学习点云 TXT (*.txt)`

当前规则：

- 从当前参数化模型表面采样。
- 默认跳过底面。
- 固定采样 2048 点。
- 输出三列 `x y z`。
- 点云中心化。
- 按最大半径归一化，范围大致在 `[-1, 1]`。

该文件可作为外部 PointNet / DGCNN / PointTransformer 推理脚本的输入。插件暂不内置深度学习模型。

---

## 深度学习接入建议

推荐后续采用外部 Python 调用：

```text
点云或参数化模型
  -> 插件导出标准化 TXT
  -> Python 模型推理
  -> 输出 result.json
  -> 插件读取类别和置信度
  -> 参数反演
  -> 模型加载到 QGIS 3D 场景
  -> 与真实点云叠加微调
```

建议返回格式：

```json
{
  "type": "FourStageRoundTower",
  "confidence": 0.93,
  "topK": [
    { "type": "FourStageRoundTower", "score": 0.93 },
    { "type": "CylinderDome", "score": 0.05 }
  ]
}
```

数据集真实化处理建议放在 Python 端完成，例如删除底面、模拟视角遮挡、制造孔洞、加入噪声、模拟点密度不均匀等。

---

## 点云分类算法

当前内置分类器流程：

1. 加载点云。
2. 降采样到约 5000 点。
3. 使用双半径曲率关键点筛选，保留约 2000 个点。
4. 提取 15 维几何特征，包括足迹圆度、长宽比、凸包度、PCA 比例、高度分位数、顶面坡度、对称性、截面一致性、高度分段数等。
5. 与每类 `TypeProfile` 进行加权评分。
6. 输出识别类型和置信度。

调试输出：

- Visual Studio 输出窗口：`OutputDebugStringW`
- 分类日志：`%TEMP%/parammodeler_classify.log`

---

## 参数反演算法

反演流程：

1. 根据分类结果选择基元类型。
2. 使用包围盒、RANSAC、直方图分割等方法估计初值。
3. 使用模拟退火进行参数精化。
4. 目标函数为点云到参数化模型表面的近似 RMSE。
5. 将反演结果写回 UI 参数控件。

调试输出：

- Visual Studio 输出窗口：`OutputDebugStringW`
- 反演日志：`%TEMP%/parammodeler_inverse.log`

---

## 项目结构

```text
parammodeler/
├─ parammodeler_dock.h / .cpp      # 主 Dock 窗口，负责 UI 调度
├─ parammodeler_dock.ui            # Qt Designer UI
├─ parammodeler_scene3d.h / .cpp   # QGIS 3D 模型/点云加载模块
├─ buildmesh.h / .cpp              # 13 类建筑基元 mesh 生成
├─ meshdata.h                      # MeshData 数据结构
├─ previewglwidget.h / .cpp        # OpenGL 实时预览
├─ exportobj.h / .cpp              # OBJ 导出
├─ exportjson.h / .cpp             # JSON 参数导出
├─ exportpointcloud.h / .cpp       # PLY 点云和深度学习 TXT 点云导出
├─ parammodeler_pcdloader.h / .cpp # PLY / LAS / LAZ 点云加载
├─ parammodeler_pcdfeatures.h/.cpp # 点云预处理和几何特征提取
├─ parammodeler_classify.h / .cpp  # 基元分类
├─ parammodeler_inverse.h / .cpp   # 参数反演
└─ tools/
   └─ batch_obj_to_ply.py          # 批量点云生成脚本
```

---

## 开发计划

- [x] 13 种建筑基元参数化建模
- [x] OpenGL 实时三维预览
- [x] OBJ / STL / JSON / PLY 多格式导出
- [x] 深度学习 TXT 点云输入导出
- [x] QGIS 3D 场景加载模块化
- [x] 外部 PLY / LAS / LAZ 点云加载
- [x] 规则分类器和几何特征提取
- [x] 参数反演
- [ ] 外部 Python 深度学习推理调用
- [ ] 点云真实化增强脚本
- [ ] 批量生成深度学习训练数据集
- [ ] 点云和反演模型成对显示/微调工作流

---

## 环境依赖

- QGIS 3.x，当前开发环境为 QGIS 3.44
- Qt 5.x
- OpenGL
- PDAL，随 QGIS 用于 LAS / LAZ 点云加载

Python 数据集脚本可按需安装：

```bash
pip install numpy open3d torch
```

---

## 作者

Chai, 2025-2026
