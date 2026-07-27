#!/usr/bin/env bash
# ============================================================================
# ParamModeler — PCT (Point Cloud Transformer) 回归训练脚本
# Li & Shan (2025) 风格 offset-attention backbone
#
# 使用方法:
#   1. 把 Windows 上生成的 datasets_aug/ 整个目录搬到 Ubuntu
#      scp -r E:/pointnet/datasets_aug xubo@ubuntu:/home/xubo/pointnet/datasets_aug/
#   2. 把 pct_simple/ 代码也搬过去
#      scp -r E:/pointnet/pct_simple xubo@ubuntu:/home/xubo/pointnet/pct_simple/
#   3. 修改下面路径配置（如果需要）
#   4. bash train_pct_reg.sh
#
# 与 PointNeXt 脚本的区别:
#   - 入口: pct_simple/main_reg.py（独立脚本，不依赖 pointnext_simple）
#   - 模型: PCT  offset-attention，~1.5M 参数（PointNeXt 的 1/3）
#   - 超参: --pct_variant basic|neighbor, --pct_d_model 256 等
#   - 目标: pure shape params（不含 rz），与 _aux 实验一致
# ============================================================================
set -e

# ========== 路径配置（按需修改） ==========
cd /home/xubo/pointnet/pct_simple
source /home/xubo/pointnet_envs/pointnet_gpu/bin/activate

DATA_ROOT=/home/xubo/pointnet/datasets_aug
METADATA=${DATA_ROOT}/metadata/sample_params.json
NUM_POINTS=2048
EPOCHS=100
BATCH_SIZE=16
LR=1e-3

# ── PCT 特定超参 ──
PCT_VARIANT="basic"          # basic=PCTRegressor, neighbor=PCTNeighborRegressor
PCT_D_MODEL=256              # 特征维度
PCT_HEADS=4                  # 注意力头数
PCT_BLOCKS=4                 # offset-attention 层数
PCT_DROPOUT=0.1

# ── 通用训练参数 ──
JITTER_SIGMA=0.003
SMOOTH_L1_BETA=1.0
WEIGHT_DECAY=1e-4
AUX_FEATURES="bbox_x bbox_y bbox_z scale"   # 辅助特征

echo "============================================================"
echo " PCT Regression Training (Li & Shan 2025 style)"
echo " variant:  ${PCT_VARIANT}"
echo " data:     ${DATA_ROOT}"
echo " code:     $(pwd)"
echo " epochs:   ${EPOCHS}  batch: ${BATCH_SIZE}  points: ${NUM_POINTS}"
echo "============================================================"

# ========== 训练函数 ==========
run_pct_reg () {
  CLASS_NAME="$1"
  LOG_DIR="$2"
  shift 2
  TARGETS="$@"

  echo ""
  echo "============================================================"
  echo "  ${CLASS_NAME}"
  echo "  Targets: ${TARGETS}"
  echo "  Log:     ${LOG_DIR}"
  echo "============================================================"

  python main_reg.py --mode train \
    --data_root "$DATA_ROOT" \
    --metadata "$METADATA" \
    --class_name "$CLASS_NAME" \
    --targets $TARGETS \
    --log_dir "$LOG_DIR" \
    --num_points "$NUM_POINTS" \
    --epochs "$EPOCHS" \
    --batch_size "$BATCH_SIZE" \
    --lr "$LR" \
    --weight_decay "$WEIGHT_DECAY" \
    --smooth_l1_beta "$SMOOTH_L1_BETA" \
    --train_jitter_sigma "$JITTER_SIGMA" \
    --aux_features $AUX_FEATURES \
    --pct_variant "$PCT_VARIANT" \
    --pct_d_model "$PCT_D_MODEL" \
    --pct_heads "$PCT_HEADS" \
    --pct_blocks "$PCT_BLOCKS" \
    --pct_dropout "$PCT_DROPOUT"

  echo "------------------------------------------------------------"
  echo "  Testing ${CLASS_NAME} (test split)"
  echo "------------------------------------------------------------"

  python main_reg.py --mode test \
    --data_root "$DATA_ROOT" \
    --metadata "$METADATA" \
    --class_name "$CLASS_NAME" \
    --split test \
    --log_dir "$LOG_DIR" \
    --num_points "$NUM_POINTS" \
    --batch_size "$BATCH_SIZE"
}

# ========== 13 类逐个训练（纯形状参数，不含 rz） ==========
run_pct_reg Cuboid               logs/pct_reg_cuboid_v1               length width height
run_pct_reg Cylinder             logs/pct_reg_cylinder_v1             radius height
run_pct_reg LHouse               logs/pct_reg_lhouse_v1               totalLength wingRatio totalWidth wingWidthRatio height
run_pct_reg ConeCylinder         logs/pct_reg_conecylinder_v1         radius totalHeight cylinderRatio
run_pct_reg GabledRoof           logs/pct_reg_gabledroof_v1           length width totalHeight wallRatio
run_pct_reg PyramidRoof          logs/pct_reg_pyramidroof_v1          length width totalHeight wallRatio
run_pct_reg TruncatedPyramidRoof logs/pct_reg_truncatedpyramid_v1     bottomLength bottomWidth topLength topWidth totalHeight wallRatio
run_pct_reg HalfCylinderRoof     logs/pct_reg_halfcylinder_v1         length width wallHeight radius
run_pct_reg CylinderDome         logs/pct_reg_cylinderdome_v1         radius totalHeight cylinderRatio bulge
run_pct_reg IndentedCuboid       logs/pct_reg_indentedcuboid_v1       outerLength outerWidth outerHeight innerLength innerWidth innerHeight offsetX offsetY
run_pct_reg AsymmetricGableHouse logs/pct_reg_asymgable_v1            length width totalHeight wallRatio ridgeLength ridgeRatio
run_pct_reg FourStageRoundTower  logs/pct_reg_fourstage_v1            baseRadius baseHeight middleHeight middleTopRadius middleBulge coneHeight
run_pct_reg TwoGableHouses       logs/pct_reg_twogable_v1             length1 length2 width totalHeight wallRatio angle ridgeRatio

echo ""
echo "============================================================"
echo " All 13 classes trained with PCT (${PCT_VARIANT}). Done!"
echo ""
echo " Results summary:"
echo " ============================================================"
for d in logs/pct_reg_*_v1; do
  cfg="$d/regression_config.json"
  if [ -f "$cfg" ]; then
    cls=$(python -c "import json; print(json.load(open('$cfg','r',encoding='utf-8'))['class_name'])")
    metrics="$d/regression_metrics_test.csv"
    if [ -f "$metrics" ]; then
      echo "  $cls  →  $d"
    else
      echo "  $cls  →  $d  (training done, test metrics pending)"
    fi
  fi
done
echo "============================================================"
