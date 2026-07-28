/***************************************************************************
  parammodeler_randomizer.cpp
  Random parameter generation for all primitive types
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "parammodeler_randomizer.h"

#include "parammodeler_dock.h"
#include "ui_parammodeler_dock.h"

#include <QRandomGenerator>
#include <QDoubleSpinBox>
#include <cmath>

void randomizePrimitiveParams( ParamModelerDock *dock,
                               bool refreshPreview,
                               bool randomizePose )
{
  auto rnd = []( double minVal, double maxVal, double step = 0.1 ) {
    const double raw = minVal + QRandomGenerator::global()->generateDouble() * ( maxVal - minVal );
    return std::round( raw / step ) * step;
  };
  auto rndAwayFromCenter = [&rnd]( double lowMin, double lowMax, double highMin, double highMax, double step = 0.01 ) {
    if ( QRandomGenerator::global()->bounded( 2 ) == 0 )
      return rnd( lowMin, lowMax, step );
    return rnd( highMin, highMax, step );
  };
  auto set = []( QDoubleSpinBox *spin, double value ) {
    if ( spin )
      spin->setValue( value );
  };

  const QString prim = dock->ui->comboPrimitive->currentText();

  if ( prim == "Cuboid" )
  {
    set( dock->ui->spinBoxCLength, rnd( 6.0, 18.0 ) );
    set( dock->ui->spinBoxCWidth, rnd( 4.0, 12.0 ) );
    set( dock->ui->spinBoxCHeight, rnd( 2.5, 8.0 ) );
  }
  else if ( prim == "Cylinder" )
  {
    set( dock->ui->spinBoxCylRadius, rnd( 2.0, 8.0 ) );
    set( dock->ui->spinBoxCylHeight, rnd( 3.0, 14.0 ) );
  }
  else if ( prim == "LHouse" )
  {
    const double totalLength    = rnd( 15.0, 41.0 );
    const double wingRatio      = rnd( 0.25, 0.65 );
    const double totalWidth     = rnd( 6.0, 14.0 );
    double wingWidthRatio       = rnd( 0.35, 0.60 );
    const double mainLength     = totalLength * ( 1.0 - wingRatio );
    const double wingLength     = totalLength * wingRatio;
    for ( int attempt = 0; attempt < 20; ++attempt )
    {
      wingWidthRatio = rnd( 0.35, 0.60 );
      const double wingWidth = totalWidth * wingWidthRatio;
      const double missingRatio = wingLength * ( totalWidth - wingWidth ) / ( ( mainLength + wingLength ) * totalWidth );
      if ( missingRatio >= 0.16 )
        break;
    }
    set( dock->ui->spinBoxLTotalLength, totalLength );
    set( dock->ui->spinBoxLWingRatio, wingRatio );
    set( dock->ui->spinBoxLTotalWidth, totalWidth );
    set( dock->ui->spinBoxLWingWidthRatio, wingWidthRatio );
    set( dock->ui->spinBoxLHeight, rnd( 2.5, 7.0 ) );
  }
  else if ( prim == "ConeCylinder" )
  {
    set( dock->ui->spinBoxConeCylRadius, rnd( 2.0, 8.0 ) );
    set( dock->ui->spinBoxConeCylCylHeight, rnd( 4.5, 14.0 ) );
    set( dock->ui->spinBoxConeCylConeHeight, rnd( 0.45, 0.80, 0.01 ) );
  }
  else if ( prim == "GabledRoof" )
  {
    set( dock->ui->spinBoxGRLength, rnd( 8.0, 24.0 ) );
    set( dock->ui->spinBoxGRWidth, rnd( 5.0, 14.0 ) );
    set( dock->ui->spinBoxGRHeightWall, rnd( 4.5, 12.0 ) );
    set( dock->ui->spinBoxGRHeightRoof, rnd( 0.55, 0.82, 0.01 ) );
  }
  else if ( prim == "PyramidRoof" )
  {
    set( dock->ui->spinBoxPRLength, rnd( 7.0, 20.0 ) );
    set( dock->ui->spinBoxPRWidth, rnd( 5.0, 16.0 ) );
    set( dock->ui->spinBoxPRHeightWall, rnd( 4.5, 12.0 ) );
    set( dock->ui->spinBoxPRHeightRoof, rnd( 0.55, 0.82, 0.01 ) );
  }
  else if ( prim == "TruncatedPyramidRoof" )
  {
    const double bottomLength = rnd( 10.0, 24.0 );
    const double bottomWidth = rnd( 8.0, 18.0 );
    set( dock->ui->spinBoxTPRBottomLength, bottomLength );
    set( dock->ui->spinBoxTPRBottomWidth, bottomWidth );
    set( dock->ui->spinBoxTPRTopLength, rnd( bottomLength * 0.35, bottomLength * 0.80 ) );
    set( dock->ui->spinBoxTPRTopWidth, rnd( bottomWidth * 0.35, bottomWidth * 0.80 ) );
    set( dock->ui->spinBoxTPRHeightWall, rnd( 4.5, 12.0 ) );
    set( dock->ui->spinBoxTPRHeightRoof, rnd( 0.55, 0.82, 0.01 ) );
  }
  else if ( prim == "HalfCylinderRoof" )
  {
    set( dock->ui->spinBoxHCRLength, rnd( 8.0, 24.0 ) );
    set( dock->ui->spinBoxHCRWidth, rnd( 4.0, 14.0 ) );
    set( dock->ui->spinBoxHCRHeightWall, rnd( 2.5, 7.0 ) );
  }
  else if ( prim == "CylinderDome" || prim == "CylinderHemisphere" )
  {
    set( dock->ui->spinBoxCylHemiRadius, rnd( 3.0, 9.0 ) );
    set( dock->ui->spinBoxCylHemiHeight, rnd( 4.5, 13.0 ) );
    set( dock->ui->spinBoxCylHemiDomeHeight, rnd( 0.50, 0.85, 0.01 ) );
    set( dock->ui->spinBoxCylHemiBulge, rnd( 0.15, 0.65, 0.01 ) );
  }
  else if ( prim == "IndentedCuboid" )
  {
    const double outerLength = rnd( 10.0, 24.0 );
    const double outerWidth = rnd( 8.0, 18.0 );
    const double outerHeight = rnd( 4.0, 12.0 );
    const double innerLength = rnd( outerLength * 0.25, outerLength * 0.60 );
    const double innerWidth = rnd( outerWidth * 0.25, outerWidth * 0.60 );
    set( dock->ui->spinBoxICLength, outerLength );
    set( dock->ui->spinBoxICWidth, outerWidth );
    set( dock->ui->spinBoxICHeight, outerHeight );
    set( dock->ui->spinBoxICInnerLength, innerLength );
    set( dock->ui->spinBoxICInnerWidth, innerWidth );
    set( dock->ui->spinBoxICInnerHeight, rnd( outerHeight * 0.25, outerHeight * 0.75 ) );
    set( dock->ui->spinBoxICOffsetX, rnd( 0.0, 1.0, 0.01 ) );
    set( dock->ui->spinBoxICOffsetY, rnd( 0.0, 1.0, 0.01 ) );
  }
  else if ( prim == "AsymmetricGableHouse" )
  {
    const double length = rnd( 10.0, 24.0 );
    const double width = rnd( 5.0, 14.0 );
    set( dock->ui->spinBoxAGHLength, length );
    set( dock->ui->spinBoxAGHWidth, width );
    set( dock->ui->spinBoxAGHHeightWall, rnd( 4.5, 12.0 ) );
    set( dock->ui->spinBoxAGHRoofHeight, rnd( 0.55, 0.82, 0.01 ) );
    set( dock->ui->spinBoxAGHRidgeLength, rnd( length * 0.45, length * 0.75 ) );
    set( dock->ui->spinBoxAGHRidgeOffset, rndAwayFromCenter( 0.2, 0.38, 0.62, 0.8, 0.01 ) );
  }
  else if ( prim == "FourStageRoundTower" )
  {
    set( dock->ui->spinBoxFTBaseRadius, rnd( 5.0, 12.0 ) );
    set( dock->ui->spinBoxFTBaseHeight, rnd( 1.2, 3.5 ) );
    set( dock->ui->spinBoxFTMiddleHeight, rnd( 1.0, 3.0 ) );
    set( dock->ui->spinBoxFTMiddleTopRadius, rnd( 0.5, 1.6 ) );
    set( dock->ui->spinBoxFTMiddleBulge, rnd( 0.15, 0.45, 0.01 ) );
    set( dock->ui->spinBoxFTConeHeight, rnd( 0.8, 2.0 ) );
  }
  else if ( prim == "TwoGableHouses" )
  {
    set( dock->ui->spinBoxTGLength1, rnd( 10.0, 24.0 ) );
    set( dock->ui->spinBoxTGLength2, rnd( 7.0, 18.0 ) );
    set( dock->ui->spinBoxTGWidth, rnd( 5.0, 12.0 ) );
    set( dock->ui->spinBoxTGHeightWall, rnd( 4.5, 12.0 ) );
    set( dock->ui->spinBoxTGRoofHeight, rnd( 0.55, 0.82, 0.01 ) );
    set( dock->ui->spinBoxTGAngle, rnd( 135.0, 165.0, 1.0 ) );
    set( dock->ui->spinBoxTGRidgeRatio, rnd( 0.3, 0.7, 0.01 ) );
  }
  else if ( prim == "TriPrismPyramid" )
  {
    // 先随机底边和腰长，确保等腰三角形存在（腰 > 底边/2）
    const double baseSide = rnd( 4.0, 14.0 );
    const double minLeg = baseSide / 2.0 + 0.5;
    const double leg = rnd( minLeg, minLeg + 12.0 );
    set( dock->ui->spinBoxTPPBase, baseSide );
    set( dock->ui->spinBoxTPPLeg, leg );
    set( dock->ui->spinBoxTPPHeight, rnd( 4.5, 14.0 ) );
    set( dock->ui->spinBoxTPPRatio, rnd( 0.25, 0.70, 0.01 ) );
  }

  // --- 训练数据生成：随机水平朝向 ---
  if ( randomizePose )
  {
    dock->ui->spinBoxROmega->setValue( 0.0 );
    dock->ui->spinBoxRPhi->setValue( 0.0 );
    dock->ui->spinBoxRKappa->setValue( rnd( -180.0, 180.0, 1.0 ) );
    dock->ui->spinBoxTX->setValue( 0.0 );
    dock->ui->spinBoxTY->setValue( 0.0 );
    dock->ui->spinBoxTZ->setValue( 0.0 );
  }

  if ( refreshPreview )
    dock->onUpdatePreview();
}
