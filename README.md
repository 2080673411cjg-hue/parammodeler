# ParamModeler - QGIS 参数化建筑基元建模插件

ParamModeler 是一个面向 QGIS 的参数化三维建筑基元建模插件，支持实时预览、多格式导出、点云加载、规则分类和参数反演。当前版本也预留了深度学习点云输入导出能力，方便后续接入 PointNet、PointNet++、DGCNN、PointTransformer 等外部模型。

---

## 功能概览

### 参数化建模与导出

- 支持 13 种建筑基元，包括长方体、圆柱、L 型房屋、人字屋顶、穹顶圆柱、四段式圆塔、双人字型房屋等。
- 支持 OpenGL 实时三维预览，参数调整后自动刷新。
- 支持位姿参数：平移 `tx/ty/tz` 和旋转 `Omega/Phi/Kappa`。
- 支持导出 OBJ、STL Mesh、JSON 参数、PLY 点云。
- 支持导出深度学习输入点云 TXT：固定点数、中心化、尺度归一化。
- 支持一键加载当前参数化模型到 QGIS 3D 场景。
- 支持加载外部 PLY / LAS / LAZ 点云到 QGIS 3D 视图进行对比。

### 点云分类与参数反演

- 支持加载外部点云并进行建筑基元识别。
- 当前分类方法为规则评分分类：点云预处理、曲率关键点筛选、15 维几何特征提取、类型 profile 加权评分。
- 支持参数反演：根据识别出的基元类型估计模型参数，并自动写回 UI。
- 当前反演主要用于几何近似和参数初值估计，后续可与深度学习分类结果结合。

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

## 导出格式

### OBJ

- 共享顶点的 OBJ 网格格式。
- 导出时应用当前位姿变换。
- 可在 MeshLab、Blender 等软件中打开。

### STL Mesh

- ASCII STL 格式。
- 按三角面导出，适合外部三维工具或网格检查。

### JSON

- 保存当前基元类型、位姿参数和形状参数。
- `CylinderDome` 作为新的正式类型名导出。

示例：

```json
{
  "type": "GabledRoof",
  "transform": {
    "tx": 0,
    "ty": 0,
    "tz": 0,
    "rx": 0,
    "ry": 0,
    "rz": 0
  },
  "params": {
    "width": 10.0,
    "depth": 8.0,
    "wallHeight": 3.0,
    "roofHeight": 2.5
  }
}
```

### PLY 点云

- ASCII PLY 格式。
- 从当前参数化 mesh 表面按三角面面积加权随机采样。
- 默认采样 50000 点。
- 默认跳过底面，使生成点云更接近真实采集数据。
- 导出时应用当前位姿变换。

### 深度学习点云 TXT

菜单入口：`导出 / 加载 -> 导出深度学习点云 TXT (*.txt)`

该功能用于给外部点云深度学习模型准备输入数据。当前导出规则：

- 从当前参数化模型表面采样。
- 默认跳过底面。
- 固定采样 2048 点。
- 每行输出一个点：`x y z`。
- 对点云做中心化。
- 按最大半径归一化，归一化后点云大致位于 `[-1, 1]` 范围内。

示例：

```txt
-0.15423891 0.28499120 0.37651813
0.42219344 -0.10188231 0.20844705
...
```

该 TXT 文件可作为 PointNet、PointNet++、DGCNN、PointTransformer 等 Python 推理脚本的输入。插件暂时不内置深度学习模型，后续建议通过 `QProcess` 调用外部 Python 脚本，并用 JSON 返回分类结果。

---

## 深度学习接入建议

当前插件侧推荐采用“外部模型调用”方式：

```text
当前模型或外部点云
  -> 插件导出标准化点云 TXT
  -> Python 脚本读取 TXT
  -> PointNet / DGCNN / PointTransformer 推理
  -> 输出 result.json
  -> 插件读取类别和置信度
  -> 进入参数反演
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

数据集真实化处理建议放在 Python 端完成，例如：

- 删除底面。
- 按相机或扫描方向保留可见面。
- 随机制造孔洞和遮挡。
- 加入噪声。
- 模拟点密度不均匀。
- 统一采样到 2048 / 4096 / 8192 点。

---

## 点云分类算法

当前内置分类器是规则评分分类器，可作为后续深度学习方法的 baseline 或 fallback。

流程：

1. 加载点云。
2. 随机降采样到约 5000 点。
3. 使用双半径曲率关键点筛选，保留约 2000 个点。
4. 提取 15 维几何特征：
   - 足迹圆度
   - 足迹长宽比
   - 足迹凸包度
   - PCA 主成分比例
   - 高度分位数
   - 顶面坡度
   - X/Y 对称性
   - 截面一致性
   - 高度分段数
   - 屋顶角度指标
   - 顶面线性度
   - 垂直分段数
5. 与每类 `TypeProfile` 进行加权评分。
6. 输出识别类型和置信度。

调试输出：

- Visual Studio 输出窗口：`OutputDebugStringW`
- 分类日志：`%TEMP%/parammodeler_classify.log`

---

## 参数反演算法

反演流程：

1. 根据分类结果选择对应基元。
2. 使用包围盒、RANSAC、直方图分割等方法估计几何初值。
3. 使用模拟退火进行参数精化。
4. 目标函数为点云到参数化模型表面的近似 RMSE。
5. 将反演结果写回 UI 参数控件。

调试输出：

- Visual Studio 输出窗口：`OutputDebugStringW`
- 反演日志：`%TEMP%/parammodeler_inverse.log`

---

## QGIS 3D 场景集成

插件支持将当前参数化模型直接加载到 QGIS 3D 场景：

1. 根据当前参数生成 mesh。
2. 应用平移和旋转。
3. 写入临时 GeoPackage。
4. 创建 QGIS 图层并设置 3D 渲染器。
5. 自动打开或刷新 3D 视图。
6. 清理上一次临时文件。

外部点云加载支持：

| 格式 | 方式 | 说明 |
|---|---|---|
| `.ply` | 插件直接读取 | 支持 ASCII / Binary |
| `.las` / `.laz` | QGIS PDAL 点云索引 | 提取 XYZ 坐标并显示到 3D 场景 |

---

## 项目结构

```text
parammodeler/
├─ parammodeler_dock.h / .cpp      # 主 Dock 窗口、UI 交互、导出调度、3D 加载
├─ parammodeler_dock.ui            # Qt Designer UI
├─ buildmesh.h / .cpp              # 13 类建筑基元 mesh 生成
├─ meshdata.h                      # MeshData 数据结构
├─ previewglwidget.h / .cpp        # OpenGL 实时预览
├─ exportobj.h / .cpp              # OBJ 导出
├─ exportjson.h / .cpp             # JSON 参数导出
├─ exportpointcloud.h / .cpp       # PLY 点云导出和深度学习 TXT 点云导出
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
- [x] 位姿参数支持
- [x] 参数化模型加载到 QGIS 3D 场景
- [x] 外部 PLY / LAS / LAZ 点云加载
- [x] 规则分类器和几何特征提取
- [x] 参数反演
- [ ] 外部 Python 深度学习推理调用
- [ ] 点云真实化增强脚本
- [ ] 批量生成深度学习训练数据集
- [ ] 分类 top-K 与反演误差联合选择

---

## 环境依赖

### QGIS 插件

- QGIS 3.x，当前开发环境为 QGIS 3.44
- Qt 5.x
- OpenGL
- PDAL，随 QGIS 用于 LAS / LAZ 点云加载

### Python 数据集脚本

```bash
pip install numpy open3d
```

后续接入深度学习模型时，可根据模型选择安装：

```bash
pip install torch
```

---

## 作者

Chai, 2025-2026
