#!/usr/bin/env bash
# ============================================================================
# ParamModeler — 快速验证脚本 (1 类, 5 epoch)
# 跑通确保数据、环境、代码都没问题后，再跑全量 train_reg_with_rot.sh
# ============================================================================
set -e

cd /home/xubo/pointnet/pointnext_simple
source /home/xubo/pointnet_envs/pointnet_gpu/bin/activate

DATA_ROOT=/home/xubo/pointnet/datasets_rot
METADATA=${DATA_ROOT}/metadata/sample_params.json

echo "============================================================"
echo " Quick sanity check: Cuboid with rotation, 5 epochs"
echo "============================================================"

python main_reg.py --mode train \
  --data_root "$DATA_ROOT" \
  --metadata "$METADATA" \
  --class_name Cuboid \
  --targets length width height rx ry rz \
  --epochs 5 \
  --batch_size 8 \
  --log_dir logs/reg_sanity_check

echo ""
echo "If you see decreasing loss and valid MAE, everything works!"
echo "Now run:  bash train_reg_with_rot.sh"
