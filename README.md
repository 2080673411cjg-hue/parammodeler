# ParamModeler

ParamModeler 是一个面向 QGIS 的参数化三维建筑基元建模与点云识别插件。插件围绕“从建筑点云识别基元类型，再估计参数化模型，并在 QGIS 3D 中叠加微调”的流程设计，支持建筑基元生成、点云导入、深度学习分类、参数回归、三维叠加显示和多格式导出。

一句话概括：

```text
点云输入 -> 建筑基元分类 -> 参数估计 -> 参数化模型生成 -> QGIS 3D 叠加微调 -> 成果导出
```

项目的核心目标不是只输出一个不可编辑的三角网格，而是把建筑点云转换为结构化、可解释、可编辑、可复现的参数化三维模型。

## v2.4.0（2026-10-20）

本版保存下一轮对齐优化之前的基线，汇总近期分类 v6 接入、圆塔八个实际尺寸参数、曲面线框修复、实时 RMSE/P95 叠加与点云表面着色改进。圆塔的 `middleRise` 为实际隆起高度，不兼容旧 `middleBulge` 系数；v5 圆塔训练结果已取回并参加离线评测，线上实际权重仍需通过服务响应确认，不随插件版本自动切换。

圆塔手动调整基础尺寸时，不再自动改变已设置的檐口尺寸、顶部半径或隆起高度；触及几何边界时限制本次正在调整的参数，相关滑块刻度保持固定。凹陷长方体同样保留当前槽深：例如槽深 6 米时，手动缩小外高最低到 6.01 米，不再偷偷缩短槽深。内槽左下角固定伸缩、预测回填与随机生成的批量更新逻辑保留。

点云上部/侧面着色改用局部平面、法向与上下层支撑判断，不依赖预测模型参数；颜色仅辅助查看，不代表经过真值验证的语义分割。实时拟合残差在既有 Qt3D 叠加层显示，不创建额外原生浮窗。

新增 13 类各 100 份点云的四组离线对照，共 5200 条结果，分开检查真值形状自动对齐、预测形状真值位姿以及两者组合；额外 1300 份未增强点云的真值回建残差均低于 0.000001 米。固定真实类别和 metadata 朝向，复用 v4 canonical 测试预测（圆塔使用 v5），不等同于未知朝向推理或 QGIS 实机验收。结果确认半圆柱/穹顶水平定位、凹陷/截顶屋顶高度参考仍有偏差，下一版再改对齐，本版不声称已解决。

完整结论见 [对齐与形状评测](evaluations/2026-10-20-alignment/analysis.md)，明细与复现工具位于 `evaluations/2026-10-20-alignment/` 和 `scripts/evaluate_alignment.ps1`。本次不重训、不改现有数据或权重，不构建插件 DLL，由用户自行编译部署。下方按日期保留历史记录，当前状态以本节和最新更新日志为准。

### 2026-10-19：圆塔隆起高度新定义

圆塔将旧 `middleBulge` 控制系数替换为实际尺寸 `middleRise`（米），参数总数仍为八个。中段剖面仍用三次贝塞尔表示：径向从下圈半径 R 线性变到上圈半径 r，高度为 `baseHeight + H*t + 4*middleRise*t*(1-t)`。最大隆起位于径向中点，指高出上下连接点直线坡面的高度，不是屋面最高点；上下圈、檐口及尖锥定义不变，穹顶不改。

取消顶部半径相对檐口外半径的 2%～35% 隐藏截断，只要求 `0 < middleTopRadius < eaveRadius`。输入框和滑块随檐口半径更新有效边界，建模直接使用输入值，不再静默替换半径；训练标签检查采用相同几何关系。

随机生成先按原范围采样一座塔，再将八个实际尺寸统一乘以均匀抽取的 `0.5～2.0` 倍率，最后写入输入控件。底座半径理论覆盖约 2.5～24 米、中段高度约 0.5～6 米；尺寸分布不是在这些最终区间内均匀抽样。保留原有高宽比、檐口外挑比例和隆起比例，仅有输入精度带来的舍入偏差。只影响重新生成的数据，不改已有文件或权重。

界面范围随中段高度 H 联动为 `0～H/4`，首次有效高度默认 `0.10H`；之后改变 H 保留合法的实际隆起高度，超限才截到可表示的上限。随机生成在 `0.03H～0.20H` 内采样，输入与新训练目标均为米制尺寸。零隆起是直线坡面，`H/4` 为该曲线保持单调升高的上限，不是建筑规范。

新圆塔必须替换生成原始数据、增强并单独重训回归，不能给旧点云直接改标签或混入旧 `middleBulge` 样本。插件本地/远程入口与新版服务均拒绝把旧鼓包系数解释成隆起高度。现有分类 v6 和其他回归路径不自动切换；圆塔新权重验证后再用 `PTV3_TOWER_LOG` 指定。旧塔权重在新版插件中暂不可用于参数估计，手动建模可用。

已完成实际网格、参数联动、旧标签/权重拒绝及中文界面检查；未构建插件 DLL、生成新数据或运行 GPU 训练。流程见 `scripts/README.md`，独立剖面预览为 `scripts/tower_bulge_profile.html`。

### 2026-10-16：分类 v6 接入

同日新增三维视图左上角的拟合残差叠加显示，显示RMSE、P95与场景单位。初版原生浮窗收到模型/点云不可见反馈后已撤换，当前直接在既有Qt3D叠加层绘制文字纹理，不创建额外窗口、不监听全局窗口事件或更改相机/图层；仅随视口尺寸更新小块屏幕空间几何。加载模型与点云后，形状、平移或旋转调整触发约200毫秒节流更新；固定采样最多2048点，以Eigen BVH计算点到三角形表面的无符号距离，不自动配准或修改参数。保留离群点，因此RMSE会受其影响；无有效模型/点云或清空场景时隐藏指标。单向表面残差不代表完整性或参数准确率；独立GPU合成检查通过，完整QGIS实机仍待验证。

凹陷长方体手动改变外框长宽时，保持内槽实际XY位置和尺寸，自动重新换算偏移比例；外框缩至内槽远端边界后停止。内槽自身长宽仍固定左下角伸缩。预测回填、恢复预测和随机生成继续采用原批量参数，不受手动固定角点约束影响；不改变训练目标。

插件本机分类默认目录与“恢复默认值”改为 `ptv3_cls_v6`，已有默认 v2/v5 配置自动迁移，自定义目录保留。服务脚本默认分类同步为 v6，回归仍为 `_v4_canonical_normals`。v6 测试报告为 1298/1300 正确（99.85%）：两份普通人字形分别误分为双人字形和长方体，两类人字形互相误分均为0；此前的 `GabledRoof/sample_00007.txt` 分类正确。新旧数据有更新，不将与 v5 的准确率差值视为严格对照提升。

远程分类由 Ubuntu 服务的 `PTV3_CLS_LOG`/默认目录决定，不由插件本机目录决定。同步新版 `ptv3_server.py` 并重启服务后，用 `/health` 确认 `cls_log` 指向 v6；显式设置旧环境变量会覆盖默认值。插件未编译，远程 GPU 请求尚待部署验证。

### 2026-10-15：方向回填与几何推理修复

v4 推理结果回填时，有 metadata 的输入会把长宽、屋脊比例和内槽四向边距一起转换回 metadata 的原始朝向，避免只覆盖角度导致屋脊或内槽翻到另一侧；没有 metadata 时保留规范参数与估计角度的配套关系。几何朝向改为稳健外接矩形与顶部屋脊轴联合判断，近正方形不再直接拒绝。700份现有增强测试点云的方向检查全部返回结果，最大轴向误差2.5°；这只是方向检查，尚未重新运行 GPU 回归或 QGIS 实机验证。

参数估计文件选择器保留 Windows 原生样式，跨窗口记住上次文件夹。没有有效历史记录时先使用数据集目录，再回退用户主目录。远程服务不返回实际模型目录时显示“版本未确认”，评估记录不再冒用本机权重路径。无需重新生成数据或重训 v4；部署和几何方向测试命令见 `scripts/README.md`。

### 2026-10-14：canonical 方向规范训练

参数回归训练入口默认使用 metadata 中的 `rz` 规范化训练、验证和测试点云，并基于规范点云重新计算 bbox 辅助特征。GabledRoof、AsymmetricGableHouse 的屋脊比例标签会同步变换；推理按 checkpoint 标记自动执行相应的几何对齐。v4 canonical 全类训练已完成，结果摘要见下文及 `dl-pipeline-log.md`。插件默认请求 `_v4_canonical_normals`；Ubuntu 服务端仍须部署新版 `ptv3_server.py`、`main_reg.py`、`canonical_orientation.py` 并重启，线上才会按请求选择 v4 权重。LHouse、TwoGableHouses 与轴向对称基元暂不旋转规范化，避免改变缺角、房体关系或无意义地处理圆形朝向。QGIS 实际推理与对齐仍待现场验证，旧 checkpoint 推理行为保持不变。

参数估计窗口的“加载点云”在没有历史目录时从配置的数据集目录打开；后续从上一个成功加载文件所在目录打开，关闭窗口后仍会记忆，并保留 Windows 原生文件选择器样式。

v4 测试集（各类100份）显示：屋脊方向规范化后，GabledRoof `ridgeRatio` MAE=0.0120、R²=0.977，AsymmetricGableHouse MAE=0.0119、R²=0.975；凹陷长方体 `innerHeight` MAE=0.1629、R²=0.981。主要弱项为 FourStageRoundTower `middleBulge`（R²=-0.593）、TwoGableHouses `ridgeRatio`（R²=0.219）及 `length1/length2`、LHouse `cutoutWidthRatio`（R²=0.383）；CylinderDome `bulge` 仍有约16.6% MAPE。分类 v5 测试准确率为99.62%（1300份）。以上是合成测试集指标，不等同于真实点云表现。

### 2026-10-13：输入目录与屋脊实验

普通人字形与非对称人字形完成了 v3.3 屋脊对称损失实验，结果文件位于 `ptv3_reg_gabledroof_ridge180_center_trial_normals` 与 `ptv3_reg_asymgable_ridge180_center_trial_normals`。屋脊等价误差较低，但镜像方向仍有歧义，因此不直接替换现有服务权重。

参数估计窗口的“加载点云”优先打开路径设置中的数据集根目录；目录不存在时回退到用户主目录。

非对称屋顶随机生成不再排除中间位置。屋脊比例在0.20–0.80之间连续采样，采用以0.50为峰值的三角分布，让居中及附近位置更常见；屋脊长度仍约为房屋长度的45%–75%，保留短屋脊形状。此改动仅影响编译后新生成的样本。

普通人字形已加入 `ridgeRatio`（0.20–0.80，默认0.50），屋脊仍贯穿整栋房屋，随机生成同样以中间位置更常见。界面、网格、点云特征采样、JSON/数据标签、预测回填和五参数训练配方已贯通；旧四参数权重回填为居中。新增参数需要重新生成并增强 GabledRoof 数据，训练入口拒绝缺少该标签的旧数据。可选 `--gabled_ridge_symmetry_180` 实验仍需部署时处理朝向；服务允许通过 `PTV3_GABLED_LOG` 单独指定人字形权重。

非对称屋顶 `ridge180_trial` 的验证/测试100个标签均与v3一致：等价屋脊MAE分别从0.1250/0.1275降至0.0377/0.0407，测试有方向MAE仍约0.2167，45/100份测试预测更接近反向表示。该结果支持偏移幅度改善，尚不支持直接替换线上权重。

非对称人字屋新增默认关闭的 `--asym_ridge_symmetry_180` 训练实验，仅允许该类的 `ridgeRatio` 在 `q` 和 `1-q` 两种180°等价表示之间选损失。普通参数误差继续输出，实验权重仍需要部署端处理方向选择，当前插件不会自动切换权重。现有 v3 的屋脊诊断见 `ridge-review-20261013.json`，运行方法见 `scripts/README.md`。本轮不编译插件，QGIS 文件选择交互需用户编译后验证。

## 最近更新（2026-10-12，插件 v2.3.15 后续改进）

菜单“帮助 → 插件使用说明”已补充当前工作流程。凹陷长方体在外框自动对齐后单独对齐内槽顶面左下角，仅调整内槽偏移；随后手动改变内槽长宽时，该角点保持固定，只移动另一端，增长到外框边界时停止。几何校正仍是默认关闭的可选参数修正，内槽对齐和固定角点伸缩均不依赖此开关。四段式圆塔改用完整点云轮廓估计水平中心，减少底部遮挡导致的偏心。历史备份与补丁合并至 `archive/`，翻译、脚本和测试目录保留。插件 DLL 已编译，最终 QGIS 画面交互仍需实际点云验证。

PTv3 远程推理和本地 PCT 现作为两个可选入口；首批角锚定模型可按点云左下墙顶/檐口语义角自动对齐。面板估计与对齐代码已拆分，圆锥、穹顶、圆塔和半圆柱屋顶的实时线框缺线已修复，并精简了圆塔底部檐口内圈。具体变更和验证范围见 `dl-pipeline-log.md`。

PTv3 分类默认使用 `ptv3_cls_v6`；插件与仓库服务脚本的回归默认值为 `ptv3_reg_<类别短名>_v4_canonical_normals`。增强集每类 train/val/test 为 800/100/100，共 13000 份；分类 v6 测试准确率为 99.85%（1298/1300）。Ubuntu 线上服务需同步脚本并重启，实际分类版本以 `/health` 的 `cls_log` 为准；QGIS 分类、回归与对齐流程仍需现场验收。

PTv3 服务可在插件“PointNet 路径设置”中切换实验室局域网 `http://192.168.1.113:8008` 与 Tailscale `http://100.70.215.84:8008`，两种模式共用 Ubuntu 上的权重，无需重新训练。切换后下次推理生效；局域网需能访问实验室机器，Tailscale 需两机在线。部署新版服务后，`/health` 默认应显示 `reg_suffix` 为 `_v4_canonical_normals`，单次回归响应中的 `model_log_dir` 用于确认该请求实际加载的目录。仓库内的 `scripts/ptv3_server.py` 是服务端部署副本；Ubuntu 的 `uvicorn` 进程需持续运行。更完整的训练与部署命令见 `scripts/README.md`。

### 2026-09-21

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
- 当前估计窗口使用 PTv3 远程分类/回归，也保留本机 PCT 入口；旧 PointNet 系列代码仍在工程中，不作为当前主入口。
- 支持将回归结果映射并回填插件 UI；PTv3 默认回归后缀为 `_v4_canonical_normals`，远程服务需支持 `model_suffix`，PCT 保留自己的模型配置。
- 支持随机参数生成及按所选基元补齐数据集；当前 PTv3 训练覆盖 13 类、每类 1000 份。TriPrismPyramid 可建模，但未纳入这 13 类训练。
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
| `GabledRoof` | 人字形屋顶房屋 | `length`, `width`, `totalHeight`, `wallRatio`, `ridgeRatio`（旧四参数权重默认屋脊居中） |
| `PyramidRoof` | 金字塔屋顶房屋 | `length`, `width`, `totalHeight`, `wallRatio` |
| `TruncatedPyramidRoof` | 截顶金字塔屋顶 | `bottomLength`, `bottomWidth`, `topLengthRatio`, `topWidthRatio`, `totalHeight`, `wallRatio` |
| `HalfCylinderRoof` | 半圆柱拱顶房屋 | `length`, `width`, `wallHeight`（`roofRadius = width/2` 派生） |
| `CylinderDome` | 圆柱穹顶 | `radius`, `totalHeight`, `cylinderRatio`, `bulge` |
| `IndentedCuboid` | 凹陷长方体 | `outerLength`, `outerWidth`, `outerHeight`, `innerLengthRatio`, `innerWidthRatio`, `innerHeight`, `innerMinXRatio`, `innerMinYRatio` |
| `AsymmetricGableHouse` | 非对称人字形房屋 | `length`, `width`, `totalHeight`, `wallRatio`, `ridgeLength`, `ridgeRatio` |
| `FourStageRoundTower` | 四段式圆塔 | `baseRadius`, `baseHeight`, `middleHeight`, `middleTopRadius`, `middleRise`, `coneHeight`, `eaveRadius`, `eaveHeight`（八个实际尺寸；旧 `middleBulge` 权重不兼容） |
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

当前 PTv3 数据集规模为 **13 类各 1000 份**（train 800 + val 100 + test 100），合计 **13000 份**；TriPrismPyramid 不在训练类表中。

### 3. 点云分类

插件加载外部点云后，通过 `PointNetRunner` 调用分类后端。当前估计窗口可选 PTv3（Ubuntu 远程服务）和 PCT（本机）；旧 PointNet 系列实现仍保留在源码中。

PTv3 默认分类权重为 `ptv3_cls_v6`（远程服务须同步并重启），PCT 有独立的本地权重路径。PTv3 的分类和回归请求由插件通过 HTTP 发送给所选服务；PCT 由本机 Python 进程执行。

分类流程大致为：

```text
点云文件
-> 插件预处理为网络输入
-> PTv3 HTTP 服务或本机 PCT Python predict
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
    "wallRatio": 0.65,
    "ridgeRatio": 0.5
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

Python 解释器、PointNet 根目录、数据集目录、PTv3 服务地址与模型目录可在插件设置中配置；源码提供本机默认值，例如：

```text
E:/mambaforge/envs/pointnet_train/python.exe
E:/pointnet/ptv3_simple/main.py
E:/pointnet/ptv3_simple/logs/ptv3_cls_v6
E:/pointnet/datasets_aug/metadata/sample_params.json
```

PTv3 使用远程服务时，权重目录由 Ubuntu 服务端选择；本机模型目录设置只对本机推理路径生效。

## 当前技术路线

推荐的主线为：

```text
参数化基元定义
-> 随机参数生成
-> BuildMesh（锚点见下：长方体类左下角 / 圆形类底面中心）
-> 网格表面采样点云 + 归一化
-> PTv3 分类 v5（Ubuntu 服务；PCT 可作本机备选）
-> 对应基元的 PTv3 回归 v3（13 类各自选最佳验证轮次）
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

- **局部参数仍难估计**：回归 v3 的凹陷长方体四个边距测试 R² 约为 -0.08 至 0.01，预测偏向平均值；四段塔 `middleBulge` 和非对称人字房屋 `ridgeRatio` 也弱。优先检查标签方向与几何可辨识性，不优先继续全类扩量或增加轮次。
- **PCT 备选模型仍是旧实验**：旧版 `pct_cls_v2` 的 98.92% F1 不等于当前 PTv3 v5 的测试结果；不要混用两者指标。
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
