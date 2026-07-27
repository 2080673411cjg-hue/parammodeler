#!/usr/bin/env bash
# ============================================================================
# ParamModeler — PCT 快速验证脚本
# 分类 5 epoch + 回归 5 epoch，确保数据/环境/PCT代码都没问题
# 通过后再跑全量 train_pct_cls.sh 和 train_pct_reg.sh
# ============================================================================
set -e

cd /home/xubo/pointnet/pct_simple
source /home/xubo/pointnet_envs/pointnet_gpu/bin/activate

DATA_ROOT=/home/xubo/pointnet/datasets_aug
METADATA=${DATA_ROOT}/metadata/sample_params.json

# ============================================================================
# 1. 分类验证
# ============================================================================
echo "============================================================"
echo " [1/2] PCT Classification Sanity Check — 5 epochs"
echo "============================================================"

python main.py --mode train \
  --data_root "$DATA_ROOT" \
  --log_dir logs/pct_cls_sanity \
  --num_points 1024 \
  --epochs 5 \
  --batch_size 8

echo ""
echo "Classification test:"
python main.py --mode test \
  --data_root "$DATA_ROOT" \
  --log_dir logs/pct_cls_sanity \
  --split test \
  --batch_size 8

echo ""
echo "=== Classification OK ==="

# ============================================================================
# 2. 回归验证 (basic variant)
# ============================================================================
echo ""
echo "============================================================"
echo " [2/2] PCT Regression Sanity Check — Cuboid, 5 epochs"
echo " variant: basic (PCTRegressor)"
echo "============================================================"

python main_reg.py --mode train \
  --data_root "$DATA_ROOT" \
  --metadata "$METADATA" \
  --class_name Cuboid \
  --targets length width height \
  --epochs 5 \
  --batch_size 8 \
  --num_points 2048 \
  --aux_features bbox_x bbox_y bbox_z scale \
  --pct_variant basic \
  --log_dir logs/pct_reg_sanity

echo ""
echo "Regression test:"
python main_reg.py --mode test \
  --data_root "$DATA_ROOT" \
  --metadata "$METADATA" \
  --class_name Cuboid \
  --split test \
  --log_dir logs/pct_reg_sanity \
  --batch_size 8

echo ""
echo "============================================================"
echo " All sanity checks passed!"
echo ""
echo " Next steps:"
echo "   bash train_pct_cls.sh       # 全量分类训练"
echo "   bash train_pct_reg.sh       # 全量回归训练"
echo ""
echo " Optional: try neighbor variant"
echo "   python main_reg.py --mode train ... --pct_variant neighbor \\"
echo "       --log_dir logs/pct_reg_sanity_neighbor"
echo "============================================================"
