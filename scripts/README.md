# ParamModeler 深度学习完整流程

## 总览

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
→ 选择输出目录 → 每类建议 200+ 样本 → 等待完成

输出:
```
E:/pointnet/datasets/
  train/{Cuboid, Cylinder, ...}/sample_00001.txt ...
  val/{...}/
  test/{...}/
  metadata/
    class_names.txt
    sample_params.json       <-- 每条 params 含 rx/ry/rz
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

## 第 5 步：Ubuntu — 参数回归训练（含旋转标签）

```bash
# 先快速验证（可选）
bash quick_sanity_check.sh

# 全量训练 13 类
bash train_reg_with_rot.sh
```

每类输出:
```
logs/pointnext_reg_cuboid_rot/
  best_model.pth
  regression_config.json
  regression_report_test.csv
  regression_errors_test.csv
  train_history.csv
```

---

## 第 6 步：Ubuntu — 批量测试

```bash
# 回归模型批量测试（使用 test 集）
DATA_ROOT=/home/xubo/pointnet/datasets_aug \
SPLIT=test \
  bash test_all_reg.sh

# 注意: test_all_reg.sh 用的 log_dir 是旧的无 rotation 路径
# 如果要对新的 *_rot 模型测试，修改变量或直接运行:
for cls in Cuboid Cylinder LHouse ConeCylinder GabledRoof PyramidRoof \
           TruncatedPyramidRoof HalfCylinderRoof CylinderDome IndentedCuboid \
           AsymmetricGableHouse FourStageRoundTower TwoGableHouses; do
  python main_reg.py --mode test \
    --data_root /home/xubo/pointnet/datasets_aug \
    --metadata /home/xubo/pointnet/datasets_aug/metadata/sample_params.json \
    --split test \
    --class_name "$cls" \
    --log_dir "logs/pointnext_reg_${cls,,}_rot" \
    --num_points 2048 --batch_size 32
done
```

---

## 第 7 步：拉回 logs 到 Windows

```bash
# === 在 Windows Git Bash 上执行 ===
scp -r xubo@<ubuntu-ip>:/home/xubo/pointnet/pointnext_simple/logs/pointnext_cls_v2 \
    E:/pointnet/pointnext_simple/logs/

scp -r xubo@<ubuntu-ip>:/home/xubo/pointnet/pointnext_simple/logs/pointnext_reg_*_rot \
    E:/pointnet/pointnext_simple/logs/
```

---

## 第 8 步：更新 C++ 插件推理路径

修改 `parammodeler_pointnet.cpp` 中 `regressionConfig()`:
- `logDir` 指向新的 `*_rot` 目录（或把 best_model.pth 覆盖到旧目录）
- `modelName` 可加版本标识

---

## 附录 A：各脚本用途

| 脚本 | 在哪运行 | 用途 |
|------|---------|------|
| `augment_dataset.py` | Windows | 纯净数据 → 增强数据 |
| `main.py --mode train` | Ubuntu | 分类训练 |
| `main.py --mode test` | Ubuntu | 分类测试 |
| `main_reg.py --mode train` | Ubuntu | 回归训练（每类单独） |
| `main_reg.py --mode test` | Ubuntu | 回归测试 |
| `train_reg_with_rot.sh` | Ubuntu | 13 类回归一键训练+测试 |
| `test_all_reg.sh` | Ubuntu | 13 类回归批量测试 |
| `quick_sanity_check.sh` | Ubuntu | 1 类 5 epoch 快速验证 |

## 附录 B：关键参数一览

| 参数 | 分类 | 回归 |
|------|------|------|
| `--num_points` | 1024 | 2048 |
| `--epochs` | 100 | 100 |
| `--batch_size` | 32 | 32 |
| `--targets` | — | `length width height rx ry rz` (各基元不同) |
| `--random_rotate` | — | **不传**（旋转已 bake 进数据） |
| `--aux_features` | — | `bbox_x bbox_y bbox_z scale` |
