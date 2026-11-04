# 建筑类别改名与 v10 操作流程

本轮延续 v9，训练保存为 v10。改名不改变几何、参数顺序、采样和损失。插件源码的改名前版本已保存为 Git 标签 v2.4.1。

| 旧类别 | 新类别 ID / 新数据目录 | 中文显示名 | 新回归短名 |
|---|---|---|---|
| Cuboid | FlatRoofHouse | 矩形平顶房屋 | flatroof |
| Cylinder | RoundFlatRoofHouse | 圆形平顶房屋 | roundflat |
| LHouse | LShapedHouse | L形平顶房屋 | lshaped |
| ConeCylinder | ConicalRoofHouse | 圆锥顶圆屋 | conicalroof |
| GabledRoof | GableRoofHouse | 双坡顶房屋 | gableroof |
| PyramidRoof | PyramidalRoofHouse | 四坡尖顶房屋 | pyramidalroof |
| TruncatedPyramidRoof | FlatTopHipRoofHouse | 平顶四坡房屋 | flattophip |
| HalfCylinderRoof | BarrelRoofHouse | 筒拱顶房屋 | barrelroof |
| CylinderDome | DomeRoofHouse | 穹顶圆屋 | domeroof |
| IndentedCuboid | RecessedRoofHouse | 内凹式平顶房屋 | recessedroof |
| AsymmetricGableHouse | ShortRidgeHipRoofHouse | 短脊四坡顶房屋 | shortridgehip |
| FourStageRoundTower | CurvedRoofRoundHouse | 带檐曲面顶圆屋 | curvedround |
| TwoGableHouses | AngledGableHouse | 转折式双坡顶房屋 | angledgable |

## 1. 编译与重新生成

用户自行编译插件。在完整批量生成功能中选择覆盖旧数据，生成以上十三类，每类2000份，按1600/200/200划分。三棱柱不参与批量生成、增强、法向准备和训练。

检查 `E:\pointnet\datasets\metadata\class_names.txt` 与 train/val/test 使用新名称；同一个划分不应同时存在同一类别的新旧别名目录。旧权重和旧 logs 无须为了生成新数据而移动。

## 2. Windows CMD 增强

在 pointnet_train 环境运行原增强入口，参数沿用此前配置：

```bat
python E:\pointnet\ptv3_simple\augment_dataset.py --input E:\pointnet\datasets --output E:\pointnet\datasets_aug --overwrite --no-drop_bottom --noise_sigma 0.006 --outlier_ratio 0.003 --outlier_extent 0.03
```

## 3. 同步 Ubuntu

同步整个增强数据集的 train、val、test、metadata，清理 Ubuntu 对应目标数据目录中的旧类别目录，避免新旧目录混放。不要把新数据混入旧测试集后继续解释为相同测试样本比较。

将 Windows `E:\pointnet\ptv3_simple` 下这些文件同步到 `/home/xubo/pointnet/ptv3_simple/`：

```text
primitive_names.py
main.py
main_reg.py
train_ptv3_reg.py
canonical_orientation.py
ptv3_server.py
predict_ptv3_pipeline_v2.py
augment_dataset.py
compare_ptv3_reg.py
evaluate_ptv3_reg.py
review_ridge_predictions.py
pcl_normals/__main__.py
pcl_normals/benchmark.py
```

`primitive_names.py` 是新增加的必需文件。PCL 的 C++ 库及 PTv3 网络结构不变，无须因改名重新构建它们。现有 logs 保留，后续训练写入新目录。

## 4. Ubuntu 生成法向缓存

```bash
conda activate ptv3_train
cd /home/xubo/pointnet/ptv3_simple
python -m pcl_normals --data_root /home/xubo/pointnet/datasets_aug --threads 4
```

缓存按新样本路径和坐标哈希生成，训练时会复用。保持与原来相同的 PCL 版本和计算设置。

## 5. 回归和分类串行训练 v10

在已激活 ptv3_train、已进入脚本目录的终端运行，全部阶段使用 PCL 缓存。分类继续 FP32：

```bash
nohup sh -c 'python -u train_ptv3_reg.py --data_root /home/xubo/pointnet/datasets_aug --run_tag v10 --epochs 100 --batch_size 4 --seed 42 --num_workers 2 --normal_backend pcl --normal_threads 4 --lhouse_canonical --lhouse_robust_bbox --truncated_scale_balanced --expected_split_counts 1600 200 200 && python -u main.py --mode train --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v10 --epochs 100 --batch_size 8 --num_points 2048 --use_normals --normal_backend pcl --normal_threads 4 --seed 42 --num_workers 2 --no-amp && python -u main.py --mode test --data_root /home/xubo/pointnet/datasets_aug --log_dir logs/ptv3_cls_v10 --split test --batch_size 8 --seed 42 --num_workers 2 --normal_threads 4' > logs/ptv3_v10_train.log 2>&1 &
```

查看日志：

```bash
tail -f /home/xubo/pointnet/ptv3_simple/logs/ptv3_v10_train.log
```

训练将产生 `logs/ptv3_cls_v10` 和 `logs/ptv3_reg_<新短名>_v10_normals`。L形等专用训练开关保留原拼写，保持已有训练配方。

## 当前模型兼容

2026-11-03，v10 已完成训练，插件、服务和串联推理入口的默认值切到分类 `ptv3_cls_v10`、回归 `_v10_normals`。插件已有设置一次性迁移后，手动改版本仍然生效。旧分类权重按保存的 class_names 顺序逐项改名，不排序；旧回归 class_name 在读取时转换，原文件不改写。v1～v9 后缀使用旧目录短名，v10 后缀使用新短名。PCT 保持旧目录与请求类别名。

用户自行编译插件，同步并重启 Ubuntu 服务。具体同步文件、重启命令和 health 核对值见 [v10 部署](README.md#v10-部署2026-11-03)。
