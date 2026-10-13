# ParamModeler 深度学习完整流程

## 2026-10-13 屋脊偏移诊断与独立实验

### v3.3 两类屋顶实验约定

普通人字形与非对称人字形下一轮统一使用模型实验版本 `v3.3`，结果目录为 `logs/ptv3_reg_gabledroof_v3.3_normals` 和 `logs/ptv3_reg_asymgable_v3.3_normals`。两类各重新生成800/100/100，增强时只处理这两类，并同步三个split目录、对应metadata及新版训练脚本。普通人字形新增第五个目标 `ridgeRatio`；非对称屋顶保留六目标，随机生成不再排除中线，采用中心偏重分布。

训练分别启用对应屋顶的180°等价损失，并同时保留普通误差和等价误差。v3.3 是后续实验命名，尚未获得训练结果。已完成的 `ridge180_trial` 只证明偏移幅度改善；推理阶段依据实际点云选择 `q`/`1-q` 候选的方向判别仍待实现，当前服务权重保持原配置。

### 普通人字形五参数训练

GabledRoof 已增加 `ridgeRatio`，旧权重在插件中按0.50回填。新训练前须用编译后的插件重新生成该类原始数据（800/100/100），仅“补齐”已有1000份数据不会更新旧标签。再次增强该类，然后将增强后的 GabledRoof 三个split及元数据、`main_reg.py`、`train_ptv3_reg.py` 同步到Ubuntu。

```cmd
python E:\pointnet\ptv3_simple\augment_dataset.py --input E:\pointnet\datasets --output E:\pointnet\datasets_aug --classes GabledRoof --overwrite --no-drop_bottom --noise_sigma 0.006 --outlier_ratio 0.003 --outlier_extent 0.03
```

```bash
nohup python -u train_ptv3_reg.py --classes GabledRoof --data_root /home/xubo/pointnet/datasets_aug --run_tag gabled_ridge_trial --gabled_ridge_symmetry_180 --expected_split_counts 800 100 100 > logs/ptv3_reg_gabledroof_gabled_ridge_trial_train.log 2>&1 &
```

新对称模型仍需选定朝向才可安全部署。服务脚本已支持 `PTV3_GABLED_LOG=logs/ptv3_reg_gabledroof_gabled_ridge_trial_normals` 单独选择该类，`/health` 返回 `gabled_reg_log`；默认继续使用旧v3权重。若重新生成非对称屋顶的新分布数据，也必须重新增强并保持元数据和点云一致。分类v5仍能运行，但其真实分类表现需要在新分布上重新评估，再决定是否更新分类权重。

以下是已完成的非对称屋顶 `ridge180_trial` 实验的复现说明，使用当时的旧增强数据，区别于需要重生成两类数据的下一轮v3.3。仓库 `main_reg.py` 和 `train_ptv3_reg.py` 是 PTv3 训练部署副本。

```bash
nohup python -u train_ptv3_reg.py --classes AsymmetricGableHouse --data_root /home/xubo/pointnet/datasets_aug --run_tag ridge180_trial --asym_ridge_symmetry_180 --expected_split_counts 800 100 100 > logs/ptv3_reg_asymgable_ridge180_trial_train.log 2>&1 &
```

该开关默认关闭，批量运行时只作用于 AsymmetricGableHouse，不改变双人字屋的损失。实验保持六个原参数与普通误差 CSV，选模使用带权标准化的180°等价误差，测试另存 `ridge_symmetry_test.json`。在两种损失相等时选择确定的分支，避免预测0.5时梯度抵消。

这项实验用于检验方向标签冲突能否解释偏移收缩；等价误差降低不代表原方向已经预测正确。权重部署前必须结合朝向确定 `q` 或 `1-q`。当前权重和服务配置不自动切换。双人字屋包含分支身份及角度，不能直接套用该变换。

已有权重的 CPU 诊断不需要 CUDA：

```cmd
python review_ridge_predictions.py --log_root E:\pointnet\ptv3_simple\logs --run_tag v3 --output ridge-review.json
```

脚本分别报告有方向误差、偏离中线幅度和预测靠近中线的数量；为普通及非对称人字形报告180°等价误差，不把双人字屋套用为同一种对称规则。旧四参数人字形输出会跳过屋脊诊断。可用 `--run_tag ridge180_trial` 检查已完成实验的结果，或在v3.3完成后检查该版本。本机基准和实验报告分别为插件根目录的 `ridge-review-20261013.json` 与 `ridge-review-trial-20261013.json`。

## v3：全部 13 类补齐到 800/100/100

在 QGIS 的 **Dataset → Complete selected primitive datasets...** 中点 **Select all trainable primitives**，保持 Train 800、Validation 100、Test 100，再选择原始数据集文件夹 `E:/pointnet/datasets`。三棱柱未纳入这 13 类训练。确认后只生成九类各 500 个缺失样本，合计 4500 个，不覆盖旧点云。

本次 v3 在 Windows CMD 中运行 `E:/pointnet/ptv3_simple/augment_dataset.py`，对全部 13 类重建增强集：

```cmd
python E:\pointnet\ptv3_simple\augment_dataset.py --input E:\pointnet\datasets --output E:\pointnet\datasets_aug --overwrite --no-drop_bottom --noise_sigma 0.006 --outlier_ratio 0.003 --outlier_extent 0.03
```

不写 `--classes` 即处理全部类别；`--overwrite` 会重建整个 `datasets_aug`，旧增强点云也会重新生成，孔洞沿用脚本默认设置。本次输出 success=13000、failed=0，bbox-min 漂移最大 0.0465。若以后只想增强新追加的样本，仓库另有 `scripts/augment_added_samples.py` 可先用 `--dry-run` 检查；它不是本次 v3 使用的命令。

增强集补齐后同步到 Ubuntu，在 `ptv3_train` 环境下从 `ptv3_simple` 目录启动 v3（普通损失，不加 `--indented_symmetry_180`）：

```bash
nohup python -u train_ptv3_reg.py --data_root /home/xubo/pointnet/datasets_aug --run_tag v3 --expected_split_counts 800 100 100 > logs/ptv3_reg_v3_train.log 2>&1 &
```

回归完成后再训练分类 v5，避免两项任务在同一 GPU 上抢显存；分类成功后自动跑测试集：

```bash
nohup sh -c 'python -u main.py --mode train --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v5 --use_normals --seed 42 && python -u main.py --mode test --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v5 --split test' > logs/ptv3_cls_v5_train.log 2>&1 &
```

服务端副本在 `scripts/ptv3_server.py`，部署到 Ubuntu 的 `ptv3_simple/ptv3_server.py` 后，在 `ptv3_train` 环境中启动 `python -m uvicorn ptv3_server:app --host 0.0.0.0 --port 8008`。部署前先比较服务端文件，避免覆盖其未同步的改动。用 `curl -s http://127.0.0.1:8008/health` 核对 `cls_log` 为 v5、`reg_suffix` 为 `_v3_normals`；Windows 端再通过插件选定的 LAN/Tailscale 地址检查 `/health`。切换网络地址不需要重新训练，但实际分类/回归仍需在 QGIS 中验证。

## 圆塔檐口独立参数（2026-10-12）

新生成的圆塔标签和训练目标均为实际尺寸：`eaveRadius` 是檐口外半径，`eaveHeight` 是檐口厚度。只在随机生成时采用温和的造型先验：外挑量为底座半径的 5%–20%，厚度在实际尺寸 0.1 到 `min(底座高度×0.25, 0.9)` 内采样；生成底座高度仍为 1.2–3.5。手动输入、网格和训练检查只要求外半径大于底座半径、厚度为正且小于底座高度，不再硬性施加随机采样范围。底座高度保留原 0–50 输入范围；手动尺寸输入保留技术上限（外半径 60、厚度 50），同时按几何关系更新有效边界。

初次有效底座默认采用 1.12 倍外半径和 `min(底座高度×0.20, 0.9)` 厚度；之后手改底座时保留仍合法的实际檐口尺寸。底座为零时清零并禁用对应控件。应单独生成并增强新版圆塔数据，避免把缺少尺寸标签的旧样本或过渡比例标签直接混入新训练。

`train_ptv3_reg.py` 的圆塔配方已更新为八个实际尺寸目标，并在启动 GPU 前检查新标签及檐口尺寸范围；其他类别的配方不变。当前 v3 权重仍是六输出。仓库内 `scripts/train_ptv3_reg.py` 与 `scripts/ptv3_server.py` 是部署副本，已同步至 Windows 的 `E:/pointnet/ptv3_simple`；Ubuntu 训练前需同步这两个脚本。

数据必须重新生成：按用户要求沿用旧目录。在主面板选 FourStageRoundTower，再选“数据集 → 生成当前基元数据集... → 替换”，数量填 1000，目录选择 `E:/pointnet/datasets`。仅重建圆塔的 train/val/test = 800/100/100 并更新 metadata，其他类别保留。不要使用“生成全部基元”或在旧圆塔目录里“补齐到 1000”。

生成后在 Windows CMD 中增强新圆塔数据（尚未执行）：

```cmd
python E:\pointnet\ptv3_simple\augment_dataset.py --input E:\pointnet\datasets --output E:\pointnet\datasets_aug --classes FourStageRoundTower --overwrite --no-drop_bottom --noise_sigma 0.006 --outlier_ratio 0.003 --outlier_extent 0.03
```

向 Ubuntu 的现有 datasets_aug 复制并替换三个圆塔子目录 `train/FourStageRoundTower`、`val/FourStageRoundTower`、`test/FourStageRoundTower` 以及整个 `metadata/`，Windows 副本保留。同步 ptv3_simple 下的 `train_ptv3_reg.py`、`ptv3_server.py`。其他类别已经在 Ubuntu 时无需再次传输；`main.py`、`main_reg.py`、`ptv3_model.py`、`check_ptv3.py` 等现有依赖保持原样。

在 Ubuntu 的 ptv3_train 环境、ptv3_simple 目录只训练圆塔回归；自动跑 test，权重保存到独立的 `ptv3_reg_fourstage_v4_eaves_normals`，不会覆盖 v3（尚未执行）：

```bash
nohup python -u train_ptv3_reg.py --classes FourStageRoundTower --data_root /home/xubo/pointnet/datasets_aug --run_tag v4_eaves --expected_split_counts 800 100 100 > logs/ptv3_reg_fourstage_v4_eaves_train.log 2>&1 &
```

回归结束后再训练分类，避免同一 GPU 同时跑两项。分类是统一的 13 类模型，仍使用全部类别的数据，只有圆塔数据被更新；不应只用圆塔一个类别训练分类。新分类保存为 v6 并自动跑 test（尚未执行）：

```bash
nohup sh -c 'python -u main.py --mode train --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v6 --use_normals --seed 42 && python -u main.py --mode test --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v6 --split test' > logs/ptv3_cls_v6_train.log 2>&1 &
```

确认训练和测试完成后，关闭旧服务并通过专用环境变量选择分类 v6 与圆塔新目录：

```bash
PTV3_CLS_LOG=logs/ptv3_cls_v6 PTV3_TOWER_LOG=logs/ptv3_reg_fourstage_v4_eaves_normals python -m uvicorn ptv3_server:app --host 0.0.0.0 --port 8008
```

`/health` 的 `cls_log` 与 `tower_reg_log` 会显示实际路径；`reg_suffix` 仍为 `_v3_normals`，其他类别回归保持原权重。不设置变量则分类继续使用 v5、圆塔继续使用 v3；显式设置的圆塔目录缺少 checkpoint 时会报错，不静默回退。

插件本地 PTv3 推理也识别 `PTV3_TOWER_LOG`。Windows CMD 可设置 `set PTV3_TOWER_LOG=E:\pointnet\ptv3_simple\logs\ptv3_reg_fourstage_v4_eaves_normals`，再从该终端启动 QGIS；本地必须已有新权重目录。此变量只影响圆塔。使用远程推理时，以 Ubuntu 服务的环境变量为准。

## 历史 PointNeXt 流程（归档）

以下步骤保留旧实验记录，涉及 500 样本/类、`pointnext_simple` 和 `_aux`。它们不是当前 PTv3 v3/v5 的操作说明；当前命令见上文。

### 旧流程总览

```
Windows (QGIS)                    Windows (Python)               Ubuntu (GPU)
─────────────────                 ─────────────────              ─────────────────
QGIS插件生成纯净数据集              augment_dataset.py 增强        分类训练 + 测试
  ↓                                  ↓                           参数回归训练 + 测试
datasets/  ─────────────────→  datasets_aug/  ─────────────→  logs/ 拉回 Windows
  train/val/test/                  train/val/test/              更新 C++ 路径
  metadata/sample_params.json      metadata/sample_params.json
```

---

## 第 1 步：Windows — QGIS 插件生成数据集

在 QGIS 中打开 ParamModeler 面板 → 点击 **"Export DL Dataset (All Classes)"**
→ 选择输出目录 → 当时标准: 500 样本/类（train 400 + val 50 + test 50） → 等待完成

输出:
```
E:/pointnet/datasets/
  train/{Cuboid, Cylinder, ...}/sample_00001.txt ... sample_00400.txt
  val/{...}/（每类 50）
  test/{...}/（每类 50）
  metadata/
    class_names.txt
    sample_params.json       <-- 每条 params 含形状参数 + rz（不含 rx/ry/tx/ty/tz）
```

验证:
```bash
python -c "
import json
data = json.load(open('E:/pointnet/datasets/metadata/sample_params.json'))
r = data[0]
print('type:', r['type'], 'params keys:', list(r['params'].keys()))
print('rx:', r['params'].get('rx'), 'ry:', r['params'].get('ry'), 'rz:', r['params'].get('rz'))
"
```

---

## 第 2 步：Windows — 数据增强

```bash
cd E:/pointnet/pointnet_simple

# 全量增强（所有 13 类）
python augment_dataset.py \
  --input E:/pointnet/datasets \
  --output E:/pointnet/datasets_aug \
  --num_points 2048 \
  --seed 42 \
  --overwrite

# 如果只想增强特定类（比如增量添加）：
# python augment_dataset.py --input E:/pointnet/datasets --output E:/pointnet/datasets_aug --classes Cylinder Cuboid --overwrite
```

增强内容包括：底部点丢弃、侧面遮挡（模拟拍照盲区）、随机挖洞、高斯噪声、离群点。

> **历史说明**：当时增强基于原始 500 样本/类；当前 PTv3 v3 使用顶部记录的 1000 样本/类和完整重建命令。

---

## 第 3 步：搬运到 Ubuntu

```bash
# === 在 Windows Git Bash 上执行 ===
# 搬运增强后的数据
scp -r E:/pointnet/datasets_aug xubo@<ubuntu-ip>:/home/xubo/pointnet/datasets_aug

# 搬运训练脚本（首次）
scp E:/pointnet/pointnext_simple/main.py E:/pointnet/pointnext_simple/main_reg.py E:/pointnet/pointnext_simple/model.py \
    xubo@<ubuntu-ip>:/home/xubo/pointnet/pointnext_simple/

# 搬运本次新增的脚本
scp scripts/train_reg_with_rot.sh scripts/train_cls.sh scripts/test_all.sh \
    xubo@<ubuntu-ip>:/home/xubo/pointnet/pointnext_simple/
```

---

## 第 4 步：Ubuntu — 分类训练

```bash
ssh xubo@<ubuntu-ip>
cd /home/xubo/pointnet/pointnext_simple
source /home/xubo/pointnet_envs/pointnet_gpu/bin/activate

# 分类训练（PointNeXt）
python main.py --mode train \
  --data_root /home/xubo/pointnet/datasets_aug \
  --log_dir logs/pointnext_cls_v2 \
  --num_points 1024 \
  --epochs 100 \
  --batch_size 32

# 分类测试
python main.py --mode test \
  --data_root /home/xubo/pointnet/datasets_aug \
  --log_dir logs/pointnext_cls_v2 \
  --split test \
  --num_points 1024 \
  --batch_size 32

# 输出: logs/pointnext_cls_v2/ 下有 best_model.pth, cls_report_test.csv 等
```

---

## 第 5 步：Ubuntu — 参数回归训练

当时的 PointNeXt 主力模型为 `_aux`（纯形状参数，不含 pose）。

```bash
# 先快速验证（可选）
bash quick_sanity_check.sh

# 全量训练 13 类（历史 _aux 版本）
bash train_reg_aux.sh

# 如果需要含旋转标签版本（实验性，形状参数精度更低）：
# bash train_reg_with_rot.sh
```

每类输出:
```
logs/pointnext_reg_cuboid_aux/
  best_model.pth
  regression_config.json
  regression_report_test.csv
  regression_errors_test.csv
  train_history.csv
```

### 模型版本说明

| 版本 | 后缀 | 回归目标 | 状态 |
|------|------|---------|------|
| `_aux` | 当时主力 | 纯形状参数（length, width, height, ratio, bulge, wallRatio...） | 历史对照 |
| `_rot` | 已弃用 | 形状参数 + rz | ❌ 形状参数精度明显更差，不建议使用 |

> `_rot` 版本虽然能预测旋转角，但形状参数精度下降严重。**根因不是数据量**：rz 这个标签在
> 当前数据形态下是**不可辨识**的，加数据也修不好。
>
> - **圆柱类**（Cylinder / ConeCylinder / CylinderDome / FourStageRoundTower）：底面是圆，
>   绕自身轴旋转点云**完全不变** → rz 根本没有定义，标签就是纯噪声。模型学不到，
>   那一维的梯度还会污染共享 backbone。
> - **矩形/多边形底面类**（Cuboid / GabledRoof / PyramidRoof / TruncatedPyramidRoof /
>   IndentedCuboid / LHouse / HalfCylinderRoof / TwoGableHouses / AsymmetricGableHouse）：
>   底面是矩形 → rz 只确定到 **mod 180°**；底面接近正方形时（PyramidRoof /
>   TruncatedPyramidRoof）只确定到 **mod 90°**。目标本身多值，MSE 把它拉向"平均"。
>
> 注意**归一化不背这个锅**：`normalizeForDL`（`exportpointcloud.cpp`）是质心平移 +
> 单一标量 maxRadius 各向同性缩放，两者都与旋转可交换 → 归一化后 rz 在 TXT 里完整保留。
> 真正会碾平朝向的是**逐轴**归一化（分别除以 bboxSize.x/y/z），当前没有做。
>
> 所以当前策略是：回归只用形状参数，**朝向不从模型来** —— 直接回填 metadata 里的
> 导出真值 `params.rz`（见 `dl-pipeline-log.md` 的 rz 回填一节）。要让模型预测 rz，
> 必须先做对称性改造（canonical pose / 把 rz 折叠到 [0°, 90°) 再回归），否则重蹈 `_rot` 覆辙。

---

## 第 6 步：Ubuntu — 批量测试

```bash
# 回归模型批量测试（使用 test 集，_aux 版本）
DATA_ROOT=/home/xubo/pointnet/datasets_aug \
SPLIT=test \
  bash test_all_reg.sh

# 手动逐类测试：
for cls in Cuboid Cylinder LHouse ConeCylinder GabledRoof PyramidRoof \
           TruncatedPyramidRoof HalfCylinderRoof CylinderDome IndentedCuboid \
           AsymmetricGableHouse FourStageRoundTower TwoGableHouses; do
  python main_reg.py --mode test \
    --data_root /home/xubo/pointnet/datasets_aug \
    --metadata /home/xubo/pointnet/datasets_aug/metadata/sample_params.json \
    --split test \
    --class_name "$cls" \
    --log_dir "logs/pointnext_reg_${cls,,}_aux" \
    --num_points 2048 --batch_size 32
done
```

---

## 第 7 步：拉回 logs 到 Windows

```bash
# === 在 Windows Git Bash 上执行 ===
scp -r xubo@<ubuntu-ip>:/home/xubo/pointnet/pointnext_simple/logs/pointnext_cls_v2 \
    E:/pointnet/pointnext_simple/logs/

scp -r xubo@<ubuntu-ip>:/home/xubo/pointnet/pointnext_simple/logs/pointnext_reg_*_aux \
    E:/pointnet/pointnext_simple/logs/
```

---

## 第 8 步：更新 C++ 插件推理路径

通过 QGIS 插件设置对话框修改（**推荐**，无需改源码）：
- 插件面板 → PointNet 路径设置
- 回归模型后缀：`_aux`（或新训练的版本如 `_v3`）

或直接改 QgsSettings 注册表 / 配置文件。详细配置项见 `parammodeler_config.cpp`。

---

## 附录 A：各脚本用途

| 脚本 | 在哪运行 | 用途 |
|------|---------|------|
| `augment_dataset.py` | Windows | 纯净数据 → 增强数据 |
| `main.py --mode train` | Ubuntu | 分类训练 |
| `main.py --mode test` | Ubuntu | 分类测试 |
| `main_reg.py --mode train` | Ubuntu | 回归训练（每类单独） |
| `main_reg.py --mode test` | Ubuntu | 回归测试 |
| `train_reg_aux.sh` | Ubuntu | 13 类 PointNeXt 回归一键训练（`_aux`，历史） |
| `train_reg_with_rot.sh` | Ubuntu | 13 类回归训练（`_rot`，已弃用） |
| `test_all_reg.sh` | Ubuntu | 13 类回归批量测试 |
| `quick_sanity_check.sh` | Ubuntu | 1 类 5 epoch 快速验证 |

## 附录 B：关键参数一览

| 参数 | 分类 | 回归 (`_aux`) |
|------|------|------|
| `--num_points` | 1024 | 2048 |
| `--epochs` | 100 | 100 |
| `--batch_size` | 32 | 32 |
| `--targets` | — | 纯形状参数（length, width, height, ratio, bulge, wallRatio...，各基元不同） |
| `--random_rotate` | — | **不传**（`_aux` 不预测旋转） |
| `--aux_features` | — | `bbox_x bbox_y bbox_z scale` |
| 数据集规模 | 500/类 | 500/类（train 400 + val 50 + test 50） |

### v2.3.10 target 重参数化

以下 4 类的回归 target 已改成更适合数据驱动学习的形式；旧模型输出仍由插件兼容映射。

| 类 | 新 target |
|---|---|
| `HalfCylinderRoof` | `length width wallHeight`（`radius = width/2` 派生） |
| `TruncatedPyramidRoof` | `bottomLength bottomWidth topLengthRatio topWidthRatio totalHeight wallRatio` |
| `LHouse` | `outerLength outerWidth cutoutLengthRatio cutoutWidthRatio height` |
| `IndentedCuboid` | `outerLength outerWidth outerHeight innerLengthRatio innerWidthRatio innerHeight innerMinXRatio innerMinYRatio` |

### 未来模型升级参考

当前 PointNeXt 对局部几何参数精度不足，建议下一版升级到 Swin3D 或 PTv3。详见主项目 `dl-pipeline-log.md` 第九章。
