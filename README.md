# ParamModeler — QGIS 参数化建模插件

A QGIS plugin for parametric 3D building primitive modeling, real-time preview, and multi-format export.

---

## 功能概览

### Tab1：参数化建模

- **13种建筑基元**，覆盖常见建筑形态
- **实时三维预览**：调整参数滑块时预览区域同步刷新，支持鼠标拖拽旋转和滚轮缩放
- **位姿参数**：支持平移（tx/ty/tz）和旋转（rx/ry/rz，ZYX欧拉角），导出时自动应用变换
- **多格式导出**：OBJ、JSON 参数文件、PLY 点云

### Tab2：分类与参数反演（开发中）

- 加载点云 / OBJ 数据
- 自动识别基元类型（开发中）
- 参数反演（开发中）
- 反演结果导出

---

## 支持的建筑基元

| 基元名称 | 标识符 | 说明 |
|---|---|---|
| 长方体 | `Cuboid` | 标准矩形建筑体 |
| 圆柱 | `Cylinder` | 圆柱形建筑体 |
| L型房屋 | `LHouse` | L形平面建筑 |
| 圆锥圆柱 | `ConeCylinder` | 圆柱+圆锥组合 |
| 人字形屋顶 | `GabledRoof` | 标准双坡屋顶房屋 |
| 金字塔屋顶 | `PyramidRoof` | 四坡锥形屋顶房屋 |
| 棱台屋顶 | `TruncatedPyramidRoof` | 截顶四棱锥屋顶房屋 |
| 半圆柱屋顶 | `HalfCylinderRoof` | 拱形屋顶房屋 |
| 穹顶圆柱 | `穹顶圆柱` | 圆柱+贝塞尔穹顶 |
| 凹陷长方体 | `凹陷长方体` | 带顶部凹槽的长方体 |
| 非对称人字形屋顶房屋 | `非对称人字形屋顶房屋` | 屋脊可偏移的不对称双坡屋顶 |
| 四段式圆塔形 | `四段式圆塔形` | 圆柱+贝塞尔过渡+圆锥塔形 |
| 双人字屋顶房屋 | `双人字屋顶房屋` | 两栋人字形房屋L形连接（开发中） |

---

## 导出格式

### OBJ
- 共享顶点格式，精度 0.0001
- 自动应用位姿变换（平移 + ZYX 欧拉角旋转）
- 可在 MeshLab、Blender 等软件中打开

### JSON
```json
{
  "type": "GabledRoof",
  "transform": {
    "tx": 0, "ty": 0, "tz": 0,
    "rx": 0, "ry": 0, "rz": 0
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
- ASCII PLY 格式
- 按三角面面积加权随机采样（默认 50000 点）
- 自动应用位姿变换

---

## 点云数据生成脚本

`tools/batch_obj_to_ply.py` 提供批量 OBJ 转点云功能，用于生成训练/测试数据集：

```
input/
  cuboid/        ← 每个子文件夹对应一个基元类别
    model_01.obj
    model_02.obj
  cylinder/
    ...
```

```bash
python batch_obj_to_ply.py --input E:\data\input --output E:\data\output
```

**主要参数：**

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--points` | 50000 | 采样点数 |
| `--keep-bottom` | 否 | 保留底面（默认删除） |
| `--sigma` | 0.003 | 高斯噪声强度 |
| `--outliers` | 0.02 | 离群点比例 |

脚本使用**模拟扫描视角**的方式删除侧面：随机选取一个水平扫描方向角，删除背对扫描仪的面，使生成的点云更贴近真实激光扫描数据。

---

## 环境依赖

### QGIS 插件
- QGIS 3.x（开发环境：QGIS 3.44）
- Qt 5.x
- OpenGL（用于实时预览）

### 点云生成脚本
```bash
pip install open3d numpy
```

---

## 项目结构

```
parammodeler/
├── parammodeler_dock.h / .cpp   # 主 Dock 窗口
├── buildmesh.h / .cpp           # 网格生成（所有基元）
├── meshdata.h                   # MeshData 数据结构
├── exportobj.h / .cpp           # OBJ 导出
├── exportjson.h / .cpp          # JSON 导出
├── exportpointcloud.h / .cpp    # PLY 点云导出
├── previewglwidget.h / .cpp     # OpenGL 实时预览控件
├── parammodeler_dock.ui         # Qt Designer UI 文件
└── tools/
    └── batch_obj_to_ply.py      # 批量点云生成脚本
```

---

## 开发计划

- [x] 13种建筑基元参数化建模
- [x] OpenGL 实时三维预览
- [x] OBJ / JSON / PLY 多格式导出
- [x] 位姿六参数支持
- [x] 批量点云数据生成脚本
- [ ] 点云导入与三维场景叠加对照
- [ ] 基元类型自动分类
- [ ] 参数反演

---

## 作者

Chai — 2025–2026