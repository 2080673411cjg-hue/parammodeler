#!/usr/bin/env bash
# ============================================================================
# ParamModeler — PCT 分类训练 + 测试脚本
#
# 使用方法:
#   1. 确保 pct_simple/ 和 datasets_aug/ 已搬到 Ubuntu
#   2. bash train_pct_cls.sh
#
# 与 PointNeXt 分类 (train_cls.sh) 的区别:
#   - 入口: pct_simple/main.py（PCT backbone）
#   - 模型: PCTClassifier，~1.3M 参数（PointNeXt 的 1/4）
#   - 超参: --pct_d_model 256 --pct_heads 4 --pct_blocks 4
# ============================================================================
set -e

# ========== 路径配置（按需修改） ==========
cd /home/xubo/pointnet/pct_simple
source /home/xubo/pointnet_envs/pointnet_gpu/bin/activate

DATA_ROOT=/home/xubo/pointnet/datasets_aug
LOG_DIR=logs/pct_cls_v1
NUM_POINTS=1024
EPOCHS=80
BATCH_SIZE=32
LR=5e-4
JITTER=0.005

echo "============================================================"
echo " PCT Classification Training"
echo " data: ${DATA_ROOT}"
echo " log:  ${LOG_DIR}"
echo " model: PCTClassifier (~1.48M params)"
echo " lr: ${LR}  jitter: ${JITTER}"
echo "============================================================"

python main.py --mode train \
  --data_root "$DATA_ROOT" \
  --log_dir "$LOG_DIR" \
  --num_points "$NUM_POINTS" \
  --epochs "$EPOCHS" \
  --batch_size "$BATCH_SIZE" \
  --lr "$LR" \
  --train_jitter_sigma "$JITTER"

echo "============================================================"
echo " Classification Test (test split)"
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
echo "  classification_report_test.csv"
echo "  confusion_matrix_test.csv"
