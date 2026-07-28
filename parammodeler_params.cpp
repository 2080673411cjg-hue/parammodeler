/***************************************************************************
  parammodeler_params.cpp
  ParamModeler parameter accessors (moved from dock to reduce file size)
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "parammodeler_dock.h"
#include "ui_parammodeler_dock.h"

#include <algorithm>

// ===== pose =====

double ParamModelerDock::poseTranslateX() const { return ui->spinBoxTX->value(); }
double ParamModelerDock::poseTranslateY() const { return ui->spinBoxTY->value(); }
double ParamModelerDock::poseTranslateZ() const { return ui->spinBoxTZ->value(); }

void ParamModelerDock::setPoseTranslate( double tx, double ty, double tz )
{
  ui->spinBoxTX->setValue( tx );
  ui->spinBoxTY->setValue( ty );
  ui->spinBoxTZ->setValue( tz );
}

double ParamModelerDock::poseRotateX() const { return ui->spinBoxROmega->value(); }
double ParamModelerDock::poseRotateY() const { return ui->spinBoxRPhi->value(); }
double ParamModelerDock::poseRotateZ() const { return ui->spinBoxRKappa->value(); }

// ===== Cuboid =====

double ParamModelerDock::cuboidLength() const { return ui->spinBoxCLength->value(); }
double ParamModelerDock::cuboidWidth() const { return ui->spinBoxCWidth->value(); }
double ParamModelerDock::cuboidHeight() const { return ui->spinBoxCHeight->value(); }

// ===== Cylinder =====

double ParamModelerDock::cylinderRadius() const { return ui->spinBoxCylRadius->value(); }
double ParamModelerDock::cylinderHeight() const { return ui->spinBoxCylHeight->value(); }

// ===== LHouse =====

double ParamModelerDock::LTotalLength() const { return ui->spinBoxLTotalLength->value(); }
double ParamModelerDock::LTotalWidth() const { return ui->spinBoxLTotalWidth->value(); }
double ParamModelerDock::LWingRatio() const { return ui->spinBoxLWingRatio->value(); }
double ParamModelerDock::LWingWidthRatio() const { return ui->spinBoxLWingWidthRatio->value(); }
double ParamModelerDock::LHeight() const { return ui->spinBoxLHeight->value(); }

// ===== ConeCylinder =====

double ParamModelerDock::coneCylRadius() const { return ui->spinBoxConeCylRadius->value(); }
double ParamModelerDock::coneCylCylHeight() const
{
  const double totalH = ui->spinBoxConeCylCylHeight->value();
  const double cylRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxConeCylConeHeight->value() ) );
  return totalH * cylRatio;
}
double ParamModelerDock::coneCylConeHeight() const
{
  const double totalH = ui->spinBoxConeCylCylHeight->value();
  const double cylRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxConeCylConeHeight->value() ) );
  return totalH * ( 1.0 - cylRatio );
}

// ===== GabledRoof =====

double ParamModelerDock::gabledRoofLength() const { return ui->spinBoxGRLength->value(); }
double ParamModelerDock::gabledRoofWidth() const { return ui->spinBoxGRWidth->value(); }
double ParamModelerDock::gabledRoofWallHeight() const
{
  const double totalH = ui->spinBoxGRHeightWall->value();
  const double wallRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxGRHeightRoof->value() ) );
  return totalH * wallRatio;
}
double ParamModelerDock::gabledRoofRoofHeight() const
{
  const double totalH = ui->spinBoxGRHeightWall->value();
  const double wallRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxGRHeightRoof->value() ) );
  return totalH * ( 1.0 - wallRatio );
}

// ===== PyramidRoof =====

double ParamModelerDock::pyramidLength() const { return ui->spinBoxPRLength->value(); }
double ParamModelerDock::pyramidWidth() const { return ui->spinBoxPRWidth->value(); }
double ParamModelerDock::pyramidWallHeight() const
{
  const double totalH = ui->spinBoxPRHeightWall->value();
  const double wallRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxPRHeightRoof->value() ) );
  return totalH * wallRatio;
}
double ParamModelerDock::pyramidRoofHeight() const
{
  const double totalH = ui->spinBoxPRHeightWall->value();
  const double wallRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxPRHeightRoof->value() ) );
  return totalH * ( 1.0 - wallRatio );
}

// ===== TruncatedPyramidRoof =====

double ParamModelerDock::tpBottomLength() const { return ui->spinBoxTPRBottomLength->value(); }
double ParamModelerDock::tpBottomWidth() const { return ui->spinBoxTPRBottomWidth->value(); }
double ParamModelerDock::tpTopLength() const { return ui->spinBoxTPRTopLength->value(); }
double ParamModelerDock::tpTopWidth() const { return ui->spinBoxTPRTopWidth->value(); }
double ParamModelerDock::tpWallHeight() const
{
  const double totalH = ui->spinBoxTPRHeightWall->value();
  const double wallRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxTPRHeightRoof->value() ) );
  return totalH * wallRatio;
}
double ParamModelerDock::tpRoofHeight() const
{
  const double totalH = ui->spinBoxTPRHeightWall->value();
  const double wallRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxTPRHeightRoof->value() ) );
  return totalH * ( 1.0 - wallRatio );
}

// ===== HalfCylinderRoof =====

double ParamModelerDock::hcrLength() const { return ui->spinBoxHCRLength->value(); }
double ParamModelerDock::hcrWidth() const { return ui->spinBoxHCRWidth->value(); }
double ParamModelerDock::hcrWallHeight() const { return ui->spinBoxHCRHeightWall->value(); }
double ParamModelerDock::hcrRadius() const { return hcrWidth() / 2.0; }

// ===== IndentedCuboid =====

double ParamModelerDock::icOuterLength() const { return ui->spinBoxICLength->value(); }
double ParamModelerDock::icOuterWidth() const { return ui->spinBoxICWidth->value(); }
double ParamModelerDock::icOuterHeight() const { return ui->spinBoxICHeight->value(); }
double ParamModelerDock::icInnerLength() const { return ui->spinBoxICInnerLength->value(); }
double ParamModelerDock::icInnerWidth() const { return ui->spinBoxICInnerWidth->value(); }
double ParamModelerDock::icInnerHeight() const { return ui->spinBoxICInnerHeight->value(); }
double ParamModelerDock::icOffsetX() const
{
  const double movable = std::max( 0.0, icOuterLength() - icInnerLength() );
  const double ratio = std::max( 0.0, std::min( 1.0, ui->spinBoxICOffsetX->value() ) );
  return movable * ratio;
}
double ParamModelerDock::icOffsetY() const
{
  const double movable = std::max( 0.0, icOuterWidth() - icInnerWidth() );
  const double ratio = std::max( 0.0, std::min( 1.0, ui->spinBoxICOffsetY->value() ) );
  return movable * ratio;
}

// ===== AsymmetricGableHouse =====

double ParamModelerDock::aghLength() const { return ui->spinBoxAGHLength->value(); }
double ParamModelerDock::aghWidth() const { return ui->spinBoxAGHWidth->value(); }
double ParamModelerDock::aghWallHeight() const
{
  const double totalH = ui->spinBoxAGHHeightWall->value();
  const double wallRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxAGHRoofHeight->value() ) );
  return totalH * wallRatio;
}
double ParamModelerDock::aghRoofHeight() const
{
  const double totalH = ui->spinBoxAGHHeightWall->value();
  const double wallRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxAGHRoofHeight->value() ) );
  return totalH * ( 1.0 - wallRatio );
}
double ParamModelerDock::aghRidgeLength() const { return ui->spinBoxAGHRidgeLength->value(); }
double ParamModelerDock::aghRidgeOffset() const { return ( aghRidgeRatio() - 0.5 ) * aghWidth(); }
double ParamModelerDock::aghRidgeRatio() const { return ui->spinBoxAGHRidgeOffset->value(); }

// ===== CylinderDome =====

double ParamModelerDock::cylHemiRadius() const { return ui->spinBoxCylHemiRadius->value(); }
double ParamModelerDock::cylHemiHeight() const
{
  const double totalH = ui->spinBoxCylHemiHeight->value();
  const double cylRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxCylHemiDomeHeight->value() ) );
  return totalH * cylRatio;
}
double ParamModelerDock::cylHemiDomeHeight() const
{
  const double totalH = ui->spinBoxCylHemiHeight->value();
  const double cylRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxCylHemiDomeHeight->value() ) );
  return totalH * ( 1.0 - cylRatio );
}
double ParamModelerDock::cylHemiBulge() const { return ui->spinBoxCylHemiBulge->value(); }

// ===== FourStageRoundTower =====

double ParamModelerDock::ftBaseRadius() const { return ui->spinBoxFTBaseRadius->value(); }
double ParamModelerDock::ftBaseHeight() const { return ui->spinBoxFTBaseHeight->value(); }
double ParamModelerDock::ftMiddleHeight() const { return ui->spinBoxFTMiddleHeight->value(); }
double ParamModelerDock::ftMiddleTopRadius() const { return ui->spinBoxFTMiddleTopRadius->value(); }
double ParamModelerDock::ftMiddleBulge() const { return ui->spinBoxFTMiddleBulge->value(); }
double ParamModelerDock::ftConeHeight() const { return ui->spinBoxFTConeHeight->value(); }

// ===== TwoGableHouses =====

double ParamModelerDock::tgLength1() const { return ui->spinBoxTGLength1->value(); }
double ParamModelerDock::tgLength2() const { return ui->spinBoxTGLength2->value(); }
double ParamModelerDock::tgWidth() const { return ui->spinBoxTGWidth->value(); }
double ParamModelerDock::tgWallHeight() const
{
  const double totalH = ui->spinBoxTGHeightWall->value();
  const double wallRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxTGRoofHeight->value() ) );
  return totalH * wallRatio;
}
double ParamModelerDock::tgRoofHeight() const
{
  const double totalH = ui->spinBoxTGHeightWall->value();
  const double wallRatio = std::max( 0.05, std::min( 0.95, ui->spinBoxTGRoofHeight->value() ) );
  return totalH * ( 1.0 - wallRatio );
}
double ParamModelerDock::tgAngle() const { return ui->spinBoxTGAngle->value(); }
double ParamModelerDock::tgRidgeRatio() const { return ui->spinBoxTGRidgeRatio->value(); }

// ===== TriPrismPyramid =====

double ParamModelerDock::triPrismPyramidLeg() const { return ui->spinBoxTPPLeg->value(); }
double ParamModelerDock::triPrismPyramidBase() const { return ui->spinBoxTPPBase->value(); }
double ParamModelerDock::triPrismPyramidHeight() const { return ui->spinBoxTPPHeight->value(); }

double ParamModelerDock::triPrismPyramidRatio() const
{
    return std::max( 0.05, std::min( 0.95, ui->spinBoxTPPRatio->value() ) );
}
