#!/usr/bin/env bash
# ============================================================================
# ParamModeler — 带旋转标签的回归训练脚本 (PointNeXt)
# 使用方法:
#   1. 先把 Windows 上生成的 datasets/ 整个目录搬到 Ubuntu
#      scp -r E:/path/to/datasets xubo@ubuntu:/home/xubo/pointnet/datasets_rot/
#   2. 修改下面 DATA_ROOT 指向你的数据目录
#   3. bash train_reg_with_rot.sh
# ============================================================================
set -e

# ========== 路径配置（按需修改） ==========
cd /home/xubo/pointnet/pointnext_simple
source /home/xubo/pointnet_envs/pointnet_gpu/bin/activate

DATA_ROOT=/home/xubo/pointnet/datasets_rot          # <-- 改成你的数据目录
METADATA=${DATA_ROOT}/metadata/sample_params.json
EPOCHS=100
BATCH_SIZE=32

# ========== 训练函数 ==========
run_reg () {
  CLASS_NAME="$1"
  LOG_DIR="$2"
  shift 2
  TARGETS="$@"

  echo "============================================================"
  echo "Training ${CLASS_NAME}  (with rotation)"
  echo "Targets: ${TARGETS}"
  echo "Log dir: ${LOG_DIR}"
  echo "============================================================"

  python main_reg.py --mode train \
    --data_root "$DATA_ROOT" \
    --metadata "$METADATA" \
    --class_name "$CLASS_NAME" \
    --targets $TARGETS \
    --epochs "$EPOCHS" \
    --batch_size "$BATCH_SIZE" \
    --log_dir "$LOG_DIR"

  echo "------------------------------------------------------------"
  echo "Testing ${CLASS_NAME}"
  echo "------------------------------------------------------------"

  python main_reg.py --mode test \
    --data_root "$DATA_ROOT" \
    --metadata "$METADATA" \
    --split test \
    --log_dir "$LOG_DIR" \
    --batch_size "$BATCH_SIZE"
}

# ========== 13 类逐个训练（每个 target 加了 rx ry rz） ==========
run_reg Cuboid               logs/pointnext_reg_cuboid_rot               length width height rx ry rz
run_reg Cylinder             logs/pointnext_reg_cylinder_rot             radius height rx ry rz
run_reg LHouse               logs/pointnext_reg_lhouse_rot               totalLength wingRatio totalWidth wingWidthRatio height rx ry rz
run_reg ConeCylinder         logs/pointnext_reg_conecylinder_rot         radius totalHeight cylinderRatio rx ry rz
run_reg GabledRoof           logs/pointnext_reg_gabledroof_rot           length width totalHeight wallRatio rx ry rz
run_reg PyramidRoof          logs/pointnext_reg_pyramidroof_rot          length width totalHeight wallRatio rx ry rz
run_reg TruncatedPyramidRoof logs/pointnext_reg_truncatedpyramid_rot     bottomLength bottomWidth topLength topWidth totalHeight wallRatio rx ry rz
run_reg HalfCylinderRoof     logs/pointnext_reg_halfcylinder_rot         length width wallHeight radius rx ry rz
run_reg CylinderDome         logs/pointnext_reg_cylinderdome_rot         radius totalHeight cylinderRatio bulge rx ry rz
run_reg IndentedCuboid       logs/pointnext_reg_indentedcuboid_rot       outerLength outerWidth outerHeight innerLength innerWidth innerHeight offsetX offsetY rx ry rz
run_reg AsymmetricGableHouse logs/pointnext_reg_asymgable_rot            length width totalHeight wallRatio ridgeLength ridgeRatio rx ry rz
run_reg FourStageRoundTower  logs/pointnext_reg_fourstage_rot            baseRadius baseHeight middleHeight middleTopRadius middleBulge coneHeight rx ry rz
run_reg TwoGableHouses       logs/pointnext_reg_twogable_rot             length1 length2 width totalHeight wallRatio angle ridgeRatio rx ry rz

echo ""
echo "============================================================"
echo " All 13 classes trained with rotation targets. Done!"
echo "============================================================"
