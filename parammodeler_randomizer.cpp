/***************************************************************************
  parammodeler_randomizer.cpp
  Random parameter generation for all primitive types
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "parammodeler_randomizer.h"

#include "buildmesh.h"
#include "parammodeler_dock.h"
#include "ui_parammodeler_dock.h"

#include <QRandomGenerator>
#include <QDoubleSpinBox>
#include <QScopedValueRollback>
#include <cmath>
#include <algorithm>

void randomizePrimitiveParams( ParamModelerDock *dock,
                               bool refreshPreview,
                               bool randomizePose,
                               int scaleBand )
{
  const QScopedValueRollback<bool> heightGuard( dock->m_suspendHeightCompensation, true );
  auto rnd = []( double minVal, double maxVal, double step = 0.1 ) {
    const double raw = minVal + QRandomGenerator::global()->generateDouble() * ( maxVal - minVal );
    return std::round( raw / step ) * step;
  };
  auto set = []( QDoubleSpinBox *spin, double value ) {
    if ( spin )
      spin->setValue( value );
  };

  const QString prim = dock->primitiveType();

  if ( prim == "FlatRoofHouse" )
  {
    set( dock->ui->spinBoxCLength, rnd( 6.0, 18.0 ) );
    set( dock->ui->spinBoxCWidth, rnd( 4.0, 12.0 ) );
    set( dock->ui->spinBoxCHeight, rnd( 2.5, 8.0 ) );
  }
  else if ( prim == "RoundFlatRoofHouse" )
  {
    set( dock->ui->spinBoxCylRadius, rnd( 2.0, 8.0 ) );
    set( dock->ui->spinBoxCylHeight, rnd( 3.0, 14.0 ) );
  }
  else if ( prim == "LShapedHouse" )
  {
    const double totalLength = QRandomGenerator::global()->bounded( 150, 401 ) / 10.0;
    const double totalWidth = QRandomGenerator::global()->bounded( 60, 401 ) / 10.0;
    int wingPercent, wingWidthPercent;
    // Resample the pair so every valid 0.01-grid combination has equal probability.
    do
    {
      wingPercent = QRandomGenerator::global()->bounded( 25, 66 );
      wingWidthPercent = QRandomGenerator::global()->bounded( 35, 61 );
    }
    while ( wingPercent * ( 100 - wingWidthPercent ) < 1600 );
    const double wingRatio = wingPercent / 100.0;
    const double wingWidthRatio = wingWidthPercent / 100.0;
    set( dock->ui->spinBoxLTotalLength, totalLength );
    set( dock->ui->spinBoxLWingRatio, wingRatio );
    set( dock->ui->spinBoxLTotalWidth, totalWidth );
    set( dock->ui->spinBoxLWingWidthRatio, wingWidthRatio );
    set( dock->ui->spinBoxLHeight, rnd( 2.5, 7.0 ) );
  }
  else if ( prim == "ConicalRoofHouse" )
  {
    set( dock->ui->spinBoxConeCylRadius, rnd( 2.0, 8.0 ) );
    set( dock->ui->spinBoxConeCylCylHeight, rnd( 4.5, 14.0 ) );
    set( dock->ui->spinBoxConeCylConeHeight, rnd( 0.45, 0.80, 0.01 ) );
  }
  else if ( prim == "GableRoofHouse" )
  {
    set( dock->ui->spinBoxGRLength, rnd( 8.0, 24.0 ) );
    set( dock->ui->spinBoxGRWidth, rnd( 5.0, 14.0 ) );
    set( dock->ui->spinBoxGRHeightWall, rnd( 4.5, 12.0 ) );
    set( dock->ui->spinBoxGRHeightRoof, rnd( 0.55, 0.82, 0.01 ) );
    const double ridgeRatio = 0.2 + 0.3 * ( QRandomGenerator::global()->generateDouble()
                                        + QRandomGenerator::global()->generateDouble() );
    set( dock->ui->spinBoxGRRidgeRatio, std::round( ridgeRatio * 100.0 ) / 100.0 );
  }
  else if ( prim == "PyramidalRoofHouse" )
  {
    set( dock->ui->spinBoxPRLength, rnd( 7.0, 20.0 ) );
    set( dock->ui->spinBoxPRWidth, rnd( 5.0, 16.0 ) );
    set( dock->ui->spinBoxPRHeightWall, rnd( 4.5, 12.0 ) );
    set( dock->ui->spinBoxPRHeightRoof, rnd( 0.55, 0.82, 0.01 ) );
  }
  else if ( prim == "FlatTopHipRoofHouse" )
  {
    dock->m_tpTopPosition = QPointF( 0.5, 0.5 );
    const double bottomLength = rnd( 10.0, 24.0 );
    const double bottomWidth = rnd( 8.0, 18.0 );
    set( dock->ui->spinBoxTPRBottomLength, bottomLength );
    set( dock->ui->spinBoxTPRBottomWidth, bottomWidth );
    set( dock->ui->spinBoxTPRTopLength, rnd( bottomLength * 0.35, bottomLength * 0.80 ) );
    set( dock->ui->spinBoxTPRTopWidth, rnd( bottomWidth * 0.35, bottomWidth * 0.80 ) );
    set( dock->ui->spinBoxTPRHeightWall, rnd( 4.5, 12.0 ) );
    set( dock->ui->spinBoxTPRHeightRoof, rnd( 0.55, 0.82, 0.01 ) );
  }
  else if ( prim == "BarrelRoofHouse" )
  {
    set( dock->ui->spinBoxHCRLength, rnd( 8.0, 24.0 ) );
    set( dock->ui->spinBoxHCRWidth, rnd( 4.0, 14.0 ) );
    set( dock->ui->spinBoxHCRHeightWall, rnd( 2.5, 7.0 ) );
  }
  else if ( prim == "DomeRoofHouse" || prim == "CylinderHemisphere" )
  {
    set( dock->ui->spinBoxCylHemiRadius, rnd( 3.0, 9.0 ) );
    set( dock->ui->spinBoxCylHemiHeight, rnd( 4.5, 13.0 ) );
    set( dock->ui->spinBoxCylHemiDomeHeight, rnd( 0.50, 0.85, 0.01 ) );
    set( dock->ui->spinBoxCylHemiBulge, rnd( 0.15, 0.65, 0.01 ) );
  }
  else if ( prim == "RecessedRoofHouse" )
  {
    const double outerLength = rnd( 10.0, 24.0 );
    const double outerWidth = rnd( 8.0, 18.0 );
    const double outerHeight = rnd( 4.0, 12.0 );
    const double innerLength = rnd( outerLength * 0.25, outerLength * 0.60 );
    const double innerWidth = rnd( outerWidth * 0.25, outerWidth * 0.60 );
    auto insetOffsetRatio = [&]( double outer, double inner ) {
      const double movable = std::max( 0.0, outer - inner );
      if ( movable <= 1e-6 )
        return 0.5;

      const double minMargin = outer * 0.12;
      const double maxOffset = movable - minMargin;
      if ( maxOffset <= minMargin )
        return 0.5;

      const double offset = rnd( minMargin, maxOffset );
      return std::max( 0.0, std::min( 1.0, offset / movable ) );
    };
    set( dock->ui->spinBoxICLength, outerLength );
    set( dock->ui->spinBoxICWidth, outerWidth );
    set( dock->ui->spinBoxICHeight, outerHeight );
    set( dock->ui->spinBoxICInnerLength, innerLength );
    set( dock->ui->spinBoxICInnerWidth, innerWidth );
    set( dock->ui->spinBoxICInnerHeight, rnd( outerHeight * 0.25, outerHeight * 0.75 ) );
    set( dock->ui->spinBoxICOffsetX, insetOffsetRatio( outerLength, innerLength ) );
    set( dock->ui->spinBoxICOffsetY, insetOffsetRatio( outerWidth, innerWidth ) );
  }
  else if ( prim == "ShortRidgeHipRoofHouse" )
  {
    dock->m_aghRidgePositionX = 0.5;
    const double length = rnd( 10.0, 24.0 );
    const double width = rnd( 5.0, 14.0 );
    set( dock->ui->spinBoxAGHLength, length );
    set( dock->ui->spinBoxAGHWidth, width );
    set( dock->ui->spinBoxAGHHeightWall, rnd( 4.5, 12.0 ) );
    set( dock->ui->spinBoxAGHRoofHeight, rnd( 0.55, 0.82, 0.01 ) );
    set( dock->ui->spinBoxAGHRidgeLength, rnd( length * 0.45, length * 0.75 ) );
    // Continuous triangular distribution: near-center roofs are more common.
    const double ridgeRatio = 0.2 + 0.3 * ( QRandomGenerator::global()->generateDouble()
                                        + QRandomGenerator::global()->generateDouble() );
    set( dock->ui->spinBoxAGHRidgeOffset, std::round( ridgeRatio * 100.0 ) / 100.0 );
  }
  else if ( prim == "CurvedRoofRoundHouse" )
  {
    const double baseRadius = rnd( 5.0, 12.0 );
    const double baseHeight = rnd( 1.2, 3.5 );
    const double eaveRadius = baseRadius * ( 1.0 + rnd( 0.05, 0.20, 0.001 ) );
    const double eaveHeight = rnd( 0.1, std::min( baseHeight * 0.25, 0.9 ), 0.001 );
    const double middleHeight = rnd( 1.0, 3.0 );
    const double middleTopRadius = rnd( 0.5, 1.6 );
    const double middleRise = rnd( middleHeight * 0.03, middleHeight * 0.20, 0.001 );
    const double coneHeight = rnd( 0.8, 2.0 );
    // Apply one scale to all physical dimensions before spin-box rounding.
    const double scale = 0.5 + QRandomGenerator::global()->generateDouble() * 1.5;
    set( dock->ui->spinBoxFTBaseRadius, baseRadius * scale );
    set( dock->ui->spinBoxFTBaseHeight, baseHeight * scale );
    set( dock->ui->spinBoxFTEaveRadius, eaveRadius * scale );
    set( dock->ui->spinBoxFTEaveThickness, eaveHeight * scale );
    set( dock->ui->spinBoxFTMiddleHeight, middleHeight * scale );
    set( dock->ui->spinBoxFTMiddleTopRadius, middleTopRadius * scale );
    set( dock->ui->spinBoxFTMiddleRise, middleRise * scale );
    set( dock->ui->spinBoxFTConeHeight, coneHeight * scale );
  }
  else if ( prim == "AngledGableHouse" )
  {
    set( dock->ui->spinBoxTGLength1, rnd( 10.0, 24.0 ) );
    set( dock->ui->spinBoxTGLength2, rnd( 7.0, 18.0 ) );
    set( dock->ui->spinBoxTGWidth, rnd( 5.0, 12.0 ) );
    set( dock->ui->spinBoxTGHeightWall, rnd( 4.5, 12.0 ) );
    set( dock->ui->spinBoxTGRoofHeight, rnd( 0.55, 0.82, 0.01 ) );
    set( dock->ui->spinBoxTGAngle, rnd( 135.0, 165.0, 1.0 ) );
    set( dock->ui->spinBoxTGRidgeRatio, rnd( 0.3, 0.7, 0.01 ) );
  }

  // Scale the whole footprint, including dependent horizontal dimensions.
  QVector<QDoubleSpinBox *> horizontalControls;
  if ( prim == "FlatRoofHouse" )
    horizontalControls = { dock->ui->spinBoxCLength, dock->ui->spinBoxCWidth };
  else if ( prim == "RoundFlatRoofHouse" )
    horizontalControls = { dock->ui->spinBoxCylRadius };
  else if ( prim == "LShapedHouse" )
    horizontalControls = { dock->ui->spinBoxLTotalLength, dock->ui->spinBoxLTotalWidth };
  else if ( prim == "ConicalRoofHouse" )
    horizontalControls = { dock->ui->spinBoxConeCylRadius };
  else if ( prim == "GableRoofHouse" )
    horizontalControls = { dock->ui->spinBoxGRLength, dock->ui->spinBoxGRWidth };
  else if ( prim == "PyramidalRoofHouse" )
    horizontalControls = { dock->ui->spinBoxPRLength, dock->ui->spinBoxPRWidth };
  else if ( prim == "FlatTopHipRoofHouse" )
    horizontalControls = { dock->ui->spinBoxTPRBottomLength, dock->ui->spinBoxTPRBottomWidth,
                           dock->ui->spinBoxTPRTopLength, dock->ui->spinBoxTPRTopWidth };
  else if ( prim == "BarrelRoofHouse" )
    horizontalControls = { dock->ui->spinBoxHCRLength, dock->ui->spinBoxHCRWidth };
  else if ( prim == "DomeRoofHouse" || prim == "CylinderHemisphere" )
    horizontalControls = { dock->ui->spinBoxCylHemiRadius };
  else if ( prim == "RecessedRoofHouse" )
    horizontalControls = { dock->ui->spinBoxICLength, dock->ui->spinBoxICWidth,
                           dock->ui->spinBoxICInnerLength, dock->ui->spinBoxICInnerWidth };
  else if ( prim == "ShortRidgeHipRoofHouse" )
    horizontalControls = { dock->ui->spinBoxAGHLength, dock->ui->spinBoxAGHWidth,
                           dock->ui->spinBoxAGHRidgeLength };
  else if ( prim == "CurvedRoofRoundHouse" )
    horizontalControls = { dock->ui->spinBoxFTBaseRadius, dock->ui->spinBoxFTEaveRadius,
                           dock->ui->spinBoxFTMiddleTopRadius };
  else if ( prim == "AngledGableHouse" )
    horizontalControls = { dock->ui->spinBoxTGLength1, dock->ui->spinBoxTGLength2, dock->ui->spinBoxTGWidth };

  if ( !horizontalControls.isEmpty() )
  {
    const MeshData mesh = BuildMesh::build( prim, dock );
    if ( !mesh.isEmpty() )
    {
      QVector3D low = mesh.vertices.first(), high = low;
      for ( const QVector3D &v : mesh.vertices )
      {
        low.setX( std::min( low.x(), v.x() ) );
        low.setY( std::min( low.y(), v.y() ) );
        high.setX( std::max( high.x(), v.x() ) );
        high.setY( std::max( high.y(), v.y() ) );
      }
      const double span = std::max( high.x() - low.x(), high.y() - low.y() );
      if ( span > 0.0 )
      {
        const int band = scaleBand >= 0 ? scaleBand % 3 : QRandomGenerator::global()->bounded( 3 );
        // Leave 0.1 m inside each boundary for spin-box rounding.
        const int limits[][2] = { { 61, 149 }, { 151, 249 }, { 251, 399 } };
        const double targetSpan = QRandomGenerator::global()->bounded( limits[band][0], limits[band][1] + 1 ) / 10.0;
        const double factor = targetSpan / span;
        // Snapshot targets before linked spin boxes can clamp dependent dimensions.
        QVector<double> scaledValues;
        scaledValues.reserve( horizontalControls.size() );
        for ( QDoubleSpinBox *spin : horizontalControls )
          scaledValues.push_back( spin->value() * factor );
        for ( int i = 0; i < horizontalControls.size(); ++i )
          set( horizontalControls[i], scaledValues[i] );
      }
    }
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
