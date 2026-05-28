# ParamModeler — QGIS 参数化建筑基元建模插件

A QGIS plugin for parametric 3D building primitive modeling, real-time preview, multi-format export, point cloud classification and parameter inversion.

---

## 功能概览

### Tab1：参数化建模与导出

- **13种建筑基元**，覆盖常见建筑形态（长方体、圆柱、L型房屋、各类屋顶等）
- **实时三维预览**：调整参数滑块时预览区域同步刷新，支持鼠标拖拽旋转和滚轮缩放
- **防抖刷新**：滑块停止拖动 1000ms 后才触发网格重建，避免频繁计算
- **位姿参数**：支持平移（tx/ty/tz）和旋转（Omega/Phi/Kappa，ZYX 欧拉角），导出时自动应用变换
- **多格式导出**：OBJ、STL Mesh、JSON 参数文件、PLY 点云
- **一键加载到 QGIS 3D 场景**：将参数化模型直接加载到 QGIS 3D 视图，写入临时 GeoPackage 强制重建渲染管线
- **自动同步开关**：勾选后参数变更自动同步到 QGIS 3D 视图
- **外部点云加载**：支持 PLY（ASCII/Binary）/ LAS / LAZ 点云加载到 3D 场景叠加对比

### Tab2：分类与参数反演

- 加载外部点云（PLY / LAS / LAZ / XYZ / TXT），自动显示在 QGIS 3D 场景中
- 与参数化模型叠加对照
- **自动识别基元类型**：基于曲率关键点提取 + 手工特征（圆度、凸包度、截面一致性等）+ 加权高斯评分，支持 13 种基元，含拒识机制
- **参数反演**：几何初解（包围盒 / RANSAC）+ 模拟退火（SA）精化，最小化点云到模型表面 RMSE
- 反演结果自动填入 UI 参数控件，可直接预览和导出

---

## 支持的建筑基元

| 基元名称 | 标识符 | 参数数 | 说明 |
|---|---|---|---|
| 长方体 | `Cuboid` | 3 | 标准矩形建筑体（长/宽/高） |
| 圆柱 | `Cylinder` | 2 | 圆柱形建筑体（半径/高度） |
| L型房屋 | `LHouse` | 5 | L形平面建筑（主长/主宽/翼长/翼宽/高） |
| 圆锥圆柱 | `ConeCylinder` | 3 | 圆柱+圆锥组合（半径/圆柱高/圆锥高） |
| 人字形屋顶 | `GabledRoof` | 4 | 标准双坡屋顶房屋（长/宽/墙高/顶高） |
| 金字塔屋顶 | `PyramidRoof` | 4 | 四坡锥形屋顶房屋（长/宽/墙高/顶高） |
| 棱台屋顶 | `TruncatedPyramidRoof` | 6 | 截顶四棱锥屋顶（底长/底宽/顶长/顶宽/墙高/顶高） |
| 半圆柱屋顶 | `HalfCylinderRoof` | 4 | 拱形屋顶房屋（长/宽/墙高/半径） |
| 圆柱穹顶 | `CylinderDome` | 4 | 圆柱+贝塞尔穹顶（半径/柱高/穹高/鼓胀度） |
| 凹陷长方体 | `IndentedCuboid` | 8 | 带顶部凹槽的长方体（外长/外宽/外高/内长/内宽/内高/偏移X/偏移Y） |
| 非对称人字形屋顶 | `AsymmetricGableHouse` | 6 | 屋脊可偏移的不对称双坡屋顶（长/宽/墙高/顶高/脊长/脊偏移） |
| 四段式圆塔形 | `FourStageRoundTower` | 6 | 圆柱+贝塞尔过渡+圆锥塔形（底半径/底高/中高/中顶半径/鼓胀/锥高） |
| 双人字屋顶房屋 | `TwoGableHouses` | 6 | 两栋人字形房屋拼接（长1/长2/宽/墙高/顶高/夹角） |

---

## 导出格式

### OBJ
- 共享顶点格式，精度 0.0001
- 自动应用位姿变换（平移 + ZYX 欧拉角旋转）
- 可在 MeshLab、Blender 等软件中打开

### STL Mesh
- 标准 STL ASCII 格式
- 自动应用位姿变换，含三角面法线计算
- 适合 3D 打印或 CAD 软件导入

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

## QGIS 3D 场景集成

### 参数化模型加载
点击「导出 ▼」→「直接加载到 QGIS 3D 场景」，插件将：
1. 根据当前参数生成模型网格
2. 应用位姿变换（平移 + ZYX 旋转）
3. 写入临时 GeoPackage 文件（使用时间戳文件名避免 OGR 缓存）
4. 设置 3D 渲染器（禁用面剔除 + Phong 材质），正确识别 Z 轴高程
5. 将图层加入 QGIS 工程，自动打开 3D 视图窗口
6. 清理上一次临时 GPKG 文件

### 外部点云加载
点击「导出 ▼」→「加载外部点云」，支持以下格式：

| 格式 | 加载方式 | 说明 |
|---|---|---|
| `.ply` | 插件直接读取 ASCII/Binary 数据 | 支持 float/double/int 等多种 property 类型，自动处理大小端 |
| `.las` / `.laz` | 通过 QGIS PDAL 索引 + BFS 八叉树遍历 | 提取 XYZ 坐标，转换为内存 PointZ 图层 |

点云加载后以球体点（半径 0.03）渲染，可与参数化模型在同一 3D 视图中叠加对比。

---

## 分类算法

### 流程
1. **加载点云** → 体素降采样至 5000 点
2. **曲率关键点滤波**（双半径法向量夹角法）→ 保留 2000 个几何语义最丰富的点
3. **特征提取**（15维特征向量）：
   - 足迹圆度（径向距离变异系数）
   - 足迹长宽比
   - 足迹凸包度（2D 凸包面积 / 包围盒面积）
   - PCA 主成分比（pcaRatio12, pcaRatio23）
   - 高度分位数（50%, 80%）
   - 顶面斜率
   - 镜像对称性（X/Y 轴）
   - 截面一致性（10 层高度截面积变异系数）
   - 高度分段数（直方图峰值检测）
   - 屋顶角度指示
   - 顶面线性度（PCA 第一主成分解释率）
   - 垂直分段数（截面半径变化检测）
4. **加权高斯评分**：13 个类型配置文件（TypeProfile），每个包含期望值和权重
5. **拒识机制**：最佳分数 < 0.45 → Unknown；最佳与第二名差距 < 0.05 → 降低置信度

### 调试输出
分类过程输出到：
- VS 输出窗口（OutputDebugStringW）
- 日志文件 `%TEMP%/parammodeler_classify.log`

---

## 参数反演算法

### 流程
1. **几何初解**：根据基元类型选择策略
   - 长方体/棱台等：包围盒直接计算
   - 圆柱/圆锥等：RANSAC 圆拟合
   - 人字顶等：高度直方图分割 + RANSAC 脊线检测
2. **模拟退火（SA）精化**：
   - 初始温度 1.0，冷却系数 0.995，最大迭代 1000 次
   - Metropolis 准则接受/拒绝扰动
   - 扰动幅度与温度和参数量级成正比
   - 目标函数：点云到参数化模型表面的 RMSE
3. **结果写入 UI**：自动填入对应 spinBox 和 slider

### 调试输出
反演过程输出到：
- VS 输出窗口（OutputDebugStringW）
- 日志文件 `%TEMP%/parammodeler_inverse.log`

---

## 调试系统

插件在以下关键节点输出调试信息（通过 `OutputDebugStringW` + 日志文件）：

| 模块 | 输出内容 |
|---|---|
| `parammodeler_dock.cpp` | 基元切换、预览刷新（顶点/面数）、导出操作、3D 加载、点云加载、分类/反演调用 |
| `buildmesh.cpp` | 每次网格构建的基元类型和结果（顶点数/三角面数） |
| `parammodeler_classify.cpp` | 15 维特征向量、识别结果和置信度 |
| `parammodeler_inverse.cpp` | 反演参数（形状/位姿）、SA 精化前后 RMSE 变化 |

在 VS 中以 RelWithDebInfo 模式运行即可在输出窗口查看。所有日志同时写入 `%TEMP%/parammodeler_*.log`。

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
- PDAL（用于 LAS/LAZ 点云加载，随 QGIS 编译）

### 点云生成脚本
```bash
pip install open3d numpy
```

---

## 项目结构

```
parammodeler/
├── parammodeler_dock.h / .cpp   # 主 Dock 窗口（UI 交互、3D 加载、点云导入、导出调度）
├── parammodeler_classify.h/.cpp # 点云基元分类（曲率关键点 + 15维特征 + 加权高斯评分 + 拒识）
├── parammodeler_inverse.h/.cpp  # 参数反演（RANSAC/包围盒初解 + SA 精化，13种基元）
├── buildmesh.h / .cpp           # 网格生成（13 种基元的参数化 Mesh 构建）
├── meshdata.h                   # MeshData 数据结构（顶点 + 索引）
├── exportobj.h / .cpp           # OBJ 导出（共享顶点 + 位姿变换）
├── exportjson.h / .cpp          # JSON 参数文件导出
├── exportpointcloud.h / .cpp    # PLY 点云导出（面积加权采样）
├── previewglwidget.h / .cpp     # OpenGL 实时预览控件
├── parammodeler_dock.ui         # Qt Designer UI 文件
└── tools/
    └── batch_obj_to_ply.py      # 批量点云生成脚本
```

---

## 开发计划

- [x] 13种建筑基元参数化建模
- [x] OpenGL 实时三维预览（防抖 + 鼠标交互）
- [x] OBJ / STL / JSON / PLY 多格式导出
- [x] 位姿六参数支持（平移 + ZYX 欧拉角旋转）
- [x] 批量点云数据生成脚本
- [x] 参数化模型一键加载到 QGIS 3D 场景（GeoPackage 方案）
- [x] 外部点云（PLY ASCII/Binary / LAS / LAZ）加载与三维场景叠加对照
- [x] 基元类型自动分类（曲率关键点 + 15维手工特征 + 加权高斯评分 + 拒识）
- [x] 参数反演（RANSAC / 包围盒初解 + 模拟退火 SA 精化）
- [x] 全模块调试日志系统（OutputDebugStringW + 日志文件）

---

## 作者

Chai — 2025–2026
