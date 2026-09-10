# 参数生长锚点设计讨论（中心 vs 左下角）

> 状态：**已实施（2026-09-10，v2.3.5）**
> 日期：2026-08-24 起讨论，2026-08-30 定案，2026-09-10 落地
> 归属：ParamModeler 插件微调体验优化
>
> 实施方式：锚点判定集中在 `BuildMesh::usesCornerAnchor()`（`buildmesh.cpp`），
> `BuildMesh::build` 对非角锚定类才调 `centerMeshOnBaseFace`；
> 对齐侧 `alignModelToPointCloud` / `pointCloudAlignTarget` 按类型取 bbox 左下角或中心；
> `parammodeler_scene3d.cpp` 未改动。详见 `dl-pipeline-log.md` v2.3.5。

---

## 1. 问题起点

原状是"底面中心锚定"（v2.2.0 起由 `centerMeshOnBaseFace()` 刻意统一；v2.3.5 已按方案 B 改掉）。表现为：

- 长/宽变化时 **两端对称动**（中心固定）
- 高度变化时 **底面固定、顶面生长**

用户希望改成 **"左下角对齐"**：模型左下角钉住，沿 +X / +Y / +Z 三个正方向单边生长，调参数时只动一侧、不破坏已对齐的部分。

## 2. 关键结论

**锚点、生长原点、旋转轴三者绑定，不能拆。**

曾尝试"生长用左下角、旋转轴留底面中心"的分离方案，但自洽性破裂：若锚点是左下角而旋转轴是底面中心，旋转后已对齐的左下角会再次偏移。

因此只有两个**自洽方案**，二选一：

| 方案 | 锚点 | 生长原点 | 旋转轴 | auto-align |
|------|------|---------|--------|-----------|
| **A 中心锚定**（v2.2.0–v2.3.4） | 底面中心 | 中心 | 中心 | 中心 → 点云中心 |
| **B 角锚定** | 左下角 | 左下角 | 左下角 | 左下角 → 点云左下角 |

> 角锚定下"绕左下角旋转"是**正确行为**（角钉死，像门绕铰链转），不是缺陷。

## 3. 方案 B 的唯一实质代价

点云"左下角"是边界点，**不可靠**：

- 一个离群点就改变包围盒角
- 建筑底角易被遮挡/噪声影响

而中心（质心）是所有点的统计量，对噪声、局部遮挡、密度不均都鲁棒。

结论：**B 在干净点云（尤其合成点云）下更好；真实扫描下"角对角"容易对歪。**

## 4. 适用类划分（若做 B）

- **圆形类保持中心**（无角）：Cylinder / ConeCylinder / CylinderDome / FourStageRoundTower
- **适合角锚的矩形类（8 个）**：Cuboid / GabledRoof / PyramidRoof / TruncatedPyramidRoof / IndentedCuboid / AsymmetricGableHouse / HalfCylinderRoof / LHouse
  - LHouse 左下角 (0,0) 是**实角**：主体 [0,Aw]×[0,Ad]，翼部 [Aw,Aw+Bw]×[0,Bd]，缺口在**右上** [Aw,Aw+Bw]×[Bd,Ad]，不影响左下角锚点（已核实 buildmesh.cpp buildLHouse）。
- ✅ **TwoGableHouses 已定：左下角**（2026-09-10）。先前"底面是凹多边形所以待定"的顾虑不成立：
  `buildTwoGableHouses` 的 `dir = (cos(turnRad), sin(turnRad))`，`turnRad = (180° − angle) ∈ [0°, 45°]`，
  两个分量都 ≥ 0 → footprint 从 `A(0,0)` 沿 +X/+Y 展开，`minX`（在 A、D）和 `minY`（在 A、B）都在 A，
  且 A 是 house 1 的 90° 直角顶点。凹角在两屋交接的 B/C 处，不在锚点。
- **排除**：TriPrismPyramid（不参与分类 / 参数估计）

## 5. 落地改动点（方案 B，已探明）

| 文件 | 改动 |
|------|------|
| `buildmesh.cpp` `BuildMesh::build` | 白名单矩形类跳过 `centerMeshOnBaseFace` |
| `dock.cpp` `pointCloudAlignTarget()` | 对齐目标从"点云 **bbox 中心**"改成"点云**左下角**"（**主要改动**，v2.3.4 后对齐统一走这里） |
| `dock.cpp` `alignModelToPointCloud` | 由"bbox 中心对 bbox 中心"改成"角对角" |
| `scene3d.cpp` | **不用改**（旋转自然绕 mesh 原点 = 左下角） |

> 注意：auto-align 改动会同时影响两条路径 —— DL 回归后的自动对齐（`onInverseParams`）和对话框加载流程（先显示点云再对齐），动手前需一起过一遍。
> 另注：点云左下角是**边界点**（对离群点/遮挡敏感），合成数据可靠，真实扫描需评估。

## 6. 决策（2026-08-30 已定）

> 推理点云**基本是合成（干净）数据** → 方案 B 直接落地，**不加可切换开关**。
> 最终划分见第 4 节。

## 7. 实施记录（2026-09-10，v2.3.5）

已按第 5 节落地。补充两条实施时才发现的事实：

1. **TwoGableHouses 定为左下角**（第 4 节的"待定"项已消，依据见该节）。
2. **角锚定让对齐对旋转更敏感**：中心对中心在中心对称底面上是旋转不变的（rz 变了仍能对上），
   而 bbox 角对 bbox 角不是 —— 点云带 rz 时，其 bbox 左下角不是模型左下角的对应点。
   所以本改动落地后，**带 rz 的点云在 X/Y 上可能反而更明显**，必须紧接着做位姿（rz）估计。
   另：rz 非 0 后模型自身 AABB 中心会随旋转漂移，模型侧参考点要在**旋转后**的 mesh 上算
   （现在 rz 恒为 0，这个坑还没暴露）。
3. **连带影响无破坏**：导出/采样共用 `BuildMesh::build`，新导出的点云原点也变左下角；
   但点云喂网络前按自身质心归一化（平移无关），metadata 与显示/对齐自洽读取，
   老数据集照常可用，训练不受影响。
