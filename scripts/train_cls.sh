#!/usr/bin/env bash
# ============================================================================
# ParamModeler — PointNeXt 分类训练 + 测试
# ============================================================================
set -e

cd /home/xubo/pointnet/pointnext_simple
source /home/xubo/pointnet_envs/pointnet_gpu/bin/activate

DATA_ROOT=/home/xubo/pointnet/datasets_aug
LOG_DIR=logs/pointnext_cls_v2
NUM_POINTS=1024
EPOCHS=100
BATCH_SIZE=32

echo "============================================================"
echo " PointNeXt 分类训练"
echo " data: ${DATA_ROOT}"
echo " log:  ${LOG_DIR}"
echo "============================================================"

python main.py --mode train \
  --data_root "$DATA_ROOT" \
  --log_dir "$LOG_DIR" \
  --num_points "$NUM_POINTS" \
  --epochs "$EPOCHS" \
  --batch_size "$BATCH_SIZE"

echo "============================================================"
echo " 分类测试 (test split)"
echo "============================================================"

python main.py --mode test \
  --data_root "$DATA_ROOT" \
  --log_dir "$LOG_DIR" \
  --split test \
  --num_points "$NUM_POINTS" \
  --batch_size "$BATCH_SIZE"

echo ""
echo "Done! Results: ${LOG_DIR}/"
echo "  best_model.pth"
echo "  cls_report_test.csv"
