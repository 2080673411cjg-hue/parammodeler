/***************************************************************************
  parammodeler_dock.cpp
  ParamModeler Dock Widget
  -------------------
         begin                : Nov. 2025
         copyright            : (C) 2025 by Chai
         email                : 2080673411@qq.com

 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "parammodeler_dock.h"
#include "ui_parammodeler_dock.h"
#include "previewglwidget.h"
#include "buildmesh.h"
#include "exportjson.h"
#include "exportobj.h"
#include "exportpointcloud.h"
#include "parammodeler_inverse.h"
#include "parammodeler_pcdloader.h"
#include "parammodeler_pointnet.h"
#include "parammodeler_scene3d.h"

#include <limits>

#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QVector3D>
#include <QMatrix4x4>
#include <QMap>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QDialog>
#include <QObject>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QProgressDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSizePolicy>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <QJsonArray>


#include <qgis.h>
#include <qgisinterface.h>
#include <QgsProject.h>
#include <QgsFeature.h>
#include <QgsGeometry.h>
#include <QgsPoint.h>
#include <QgsLineString.h>
#include <QgsPolygon.h>
#include <qgsmultipolygon.h>


#include <QgsVectorLayer.h>
#include <qgspointcloudlayer.h>
#include <qgssymbol.h>
#include <QgsSingleSymbolRenderer.h>
#include <QgsPolygon3DSymbol.h>
#include <QgsVectorLayer3DRenderer.h>
#include <Qgs3DMapCanvas.h>
#include <qgs3dmapsettings.h>
#include <QgsPoint3DSymbol.h>
#include "qgscoordinatereferencesystem.h"
#include "qgspointcloud3dsymbol.h"
#include "qgspointcloudlayer3drenderer.h"
#include "qgsmapcanvas.h"
#include "qgspointcloudindex.h"
#include "qgspointcloudblock.h"
#include "qgspointcloudattribute.h"
#include "qgspointclouddataprovider.h"
#include <qgsphongmaterialsettings.h>
#include <qgs3dtypes.h>
#include <qgsvectorfilewriter.h>
#include <QDateTime>
#include "parammodeler_config.h"
#include <windows.h>
#define DEBUG_LOG( msg ) OutputDebugStringW( msg )

static bool metadataPointCloudInfoForInput( const QString &filePath,
                                            QVector3D *bboxMin,
                                            QVector3D *center,
                                            double *scale );

static QMap<QString, double> pointNetParamsToUiParams( const QString &primitiveType, const QMap<QString, double> &nn )
{
  QMap<QString, double> uiParams;
  const auto has = [&]( const QString &key ) { return nn.contains( key ); };
  const auto val = [&]( const QString &key, double fallback = 0.0 ) { return nn.value( key, fallback ); };
  const auto put = [&]( const QString &from, const QString &to ) {
    if ( has( from ) )
      uiParams.insert( to, val( from ) );
  };
  const auto putWallRatio = [&]( const QString &wallKey, const QString &roofKey ) {
    if ( has( QStringLiteral( "totalHeight" ) ) && has( QStringLiteral( "wallRatio" ) ) )
    {
      const double totalHeight = val( QStringLiteral( "totalHeight" ) );
      const double wallRatio = std::max( 0.0, std::min( 1.0, val( QStringLiteral( "wallRatio" ) ) ) );
      uiParams.insert( wallKey, totalHeight * wallRatio );
      uiParams.insert( roofKey, totalHeight * ( 1.0 - wallRatio ) );
    }
  };
  const auto putCylinderRatio = [&]( const QString &cylKey, const QString &upperKey ) {
    if ( has( QStringLiteral( "totalHeight" ) ) && has( QStringLiteral( "cylinderRatio" ) ) )
    {
      const double totalHeight = val( QStringLiteral( "totalHeight" ) );
      const double cylinderRatio = std::max( 0.0, std::min( 1.0, val( QStringLiteral( "cylinderRatio" ) ) ) );
      uiParams.insert( cylKey, totalHeight * cylinderRatio );
      uiParams.insert( upperKey, totalHeight * ( 1.0 - cylinderRatio ) );
    }
  };

  const QString prim = primitiveType == QStringLiteral( "CylinderHemisphere" )
                         ? QStringLiteral( "CylinderDome" )
                         : primitiveType;

  if ( prim == QStringLiteral( "Cuboid" ) )
  {
    put( QStringLiteral( "length" ), QStringLiteral( "length" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "width" ) );
    put( QStringLiteral( "height" ), QStringLiteral( "height" ) );
  }
  else if ( prim == QStringLiteral( "Cylinder" ) )
  {
    put( QStringLiteral( "radius" ), QStringLiteral( "radius" ) );
    put( QStringLiteral( "height" ), QStringLiteral( "cylHeight" ) );
  }
  else if ( prim == QStringLiteral( "LHouse" ) )
  {
    put( QStringLiteral( "mainLength" ), QStringLiteral( "lMainL" ) );
    put( QStringLiteral( "mainWidth" ), QStringLiteral( "lMainW" ) );
    put( QStringLiteral( "wingLength" ), QStringLiteral( "lWingL" ) );
    put( QStringLiteral( "wingWidth" ), QStringLiteral( "lWingW" ) );
    put( QStringLiteral( "height" ), QStringLiteral( "lHeight" ) );
  }
  else if ( prim == QStringLiteral( "ConeCylinder" ) )
  {
    put( QStringLiteral( "radius" ), QStringLiteral( "ccRadius" ) );
    putCylinderRatio( QStringLiteral( "ccCylHeight" ), QStringLiteral( "ccConeHeight" ) );
  }
  else if ( prim == QStringLiteral( "GabledRoof" ) )
  {
    put( QStringLiteral( "length" ), QStringLiteral( "grLength" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "grWidth" ) );
    putWallRatio( QStringLiteral( "grWallHeight" ), QStringLiteral( "grRoofHeight" ) );
  }
  else if ( prim == QStringLiteral( "PyramidRoof" ) )
  {
    put( QStringLiteral( "length" ), QStringLiteral( "prLength" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "prWidth" ) );
    putWallRatio( QStringLiteral( "prWallHeight" ), QStringLiteral( "prRoofHeight" ) );
  }
  else if ( prim == QStringLiteral( "TruncatedPyramidRoof" ) )
  {
    put( QStringLiteral( "bottomLength" ), QStringLiteral( "tpBottomLength" ) );
    put( QStringLiteral( "bottomWidth" ), QStringLiteral( "tpBottomWidth" ) );
    put( QStringLiteral( "topLength" ), QStringLiteral( "tpTopLength" ) );
    put( QStringLiteral( "topWidth" ), QStringLiteral( "tpTopWidth" ) );
    putWallRatio( QStringLiteral( "tpWallHeight" ), QStringLiteral( "tpRoofHeight" ) );
  }
  else if ( prim == QStringLiteral( "HalfCylinderRoof" ) )
  {
    put( QStringLiteral( "length" ), QStringLiteral( "hcrLength" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "hcrWidth" ) );
    put( QStringLiteral( "wallHeight" ), QStringLiteral( "hcrWallHeight" ) );
  }
  else if ( prim == QStringLiteral( "CylinderDome" ) )
  {
    put( QStringLiteral( "radius" ), QStringLiteral( "chRadius" ) );
    putCylinderRatio( QStringLiteral( "chCylHeight" ), QStringLiteral( "chDomeHeight" ) );
    put( QStringLiteral( "bulge" ), QStringLiteral( "chBulge" ) );
  }
  else if ( prim == QStringLiteral( "IndentedCuboid" ) )
  {
    put( QStringLiteral( "outerLength" ), QStringLiteral( "icOuterL" ) );
    put( QStringLiteral( "outerWidth" ), QStringLiteral( "icOuterW" ) );
    put( QStringLiteral( "outerHeight" ), QStringLiteral( "icOuterH" ) );
    put( QStringLiteral( "innerLength" ), QStringLiteral( "icInnerL" ) );
    put( QStringLiteral( "innerWidth" ), QStringLiteral( "icInnerW" ) );
    put( QStringLiteral( "innerHeight" ), QStringLiteral( "icInnerH" ) );
    put( QStringLiteral( "offsetX" ), QStringLiteral( "icOffsetX" ) );
    put( QStringLiteral( "offsetY" ), QStringLiteral( "icOffsetY" ) );
  }
  else if ( prim == QStringLiteral( "AsymmetricGableHouse" ) )
  {
    put( QStringLiteral( "length" ), QStringLiteral( "aghLength" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "aghWidth" ) );
    putWallRatio( QStringLiteral( "aghWallHeight" ), QStringLiteral( "aghRoofHeight" ) );
    put( QStringLiteral( "ridgeLength" ), QStringLiteral( "aghRidgeLen" ) );
    put( QStringLiteral( "ridgeRatio" ), QStringLiteral( "aghRidgeRatio" ) );
  }
  else if ( prim == QStringLiteral( "FourStageRoundTower" ) )
  {
    put( QStringLiteral( "baseRadius" ), QStringLiteral( "ftBaseR" ) );
    put( QStringLiteral( "baseHeight" ), QStringLiteral( "ftBaseH" ) );
    put( QStringLiteral( "middleHeight" ), QStringLiteral( "ftMidH" ) );
    put( QStringLiteral( "middleTopRadius" ), QStringLiteral( "ftMidTopR" ) );
    put( QStringLiteral( "middleBulge" ), QStringLiteral( "ftMidBulge" ) );
    put( QStringLiteral( "coneHeight" ), QStringLiteral( "ftConeH" ) );
  }
  else if ( prim == QStringLiteral( "TwoGableHouses" ) )
  {
    put( QStringLiteral( "length1" ), QStringLiteral( "tgLength1" ) );
    put( QStringLiteral( "length2" ), QStringLiteral( "tgLength2" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "tgWidth" ) );
    putWallRatio( QStringLiteral( "tgWallHeight" ), QStringLiteral( "tgRoofHeight" ) );
    put( QStringLiteral( "angle" ), QStringLiteral( "tgAngle" ) );
    put( QStringLiteral( "ridgeRatio" ), QStringLiteral( "tgRidgeRatio" ) );
  }

  // 水平朝向（所有基元通用）
  put( QStringLiteral( "rz" ), QStringLiteral( "poseRotateZ" ) );

  return uiParams;
}


static void bindSliderSpin( QSlider *slider, QDoubleSpinBox *spin, double multiplier, double maxVal = 100.0, double minVal = 0.0 )
{
  if ( !slider || !spin )
    return;

  spin->setRange( minVal, maxVal );
  spin->setSingleStep( 1.0 / multiplier );

  slider->setRange( static_cast<int>( minVal * multiplier ), static_cast<int>( maxVal * multiplier ) );
  QObject::connect( slider, &QSlider::valueChanged, spin, [spin, multiplier]( int v ) {
    double val = static_cast<double>( v ) / multiplier;
    if ( std::abs( spin->value() - val ) > 0.0001 )
    {
      spin->setValue( val );
    }
  } );
  QObject::connect( spin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), slider, [slider, multiplier]( double v ) {
    int val = static_cast<int>( v * multiplier );
    if ( slider->value() != val )
    {
      slider->setValue( val );
    }
  } );
}


ParamModelerDock::ParamModelerDock( QgisInterface *iface, QWidget *parent )
  : QDockWidget( parent )
  , ui( new Ui::ParamModelerDock )
  , mIface( iface )
{
  ui->setupUi( this );
  m_currentPrimitive = ui->comboPrimitive->currentText();
  setWindowTitle( tr( "Parametric Modeler" ) );

  ui->widgetInversionPanel->setVisible( false );
  ui->widgetInversionPanel->setMinimumWidth( 0 );
  ui->widgetInversionPanel->setMaximumWidth( 0 );
  ui->splitterParams->setStretchFactor( 0, 1 );
  ui->splitterParams->setStretchFactor( 1, 0 );
  ui->splitterParams->setSizes( QList<int>() << 1000 << 0 );
  ui->groupBoxParameters->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  ui->stackedWidgetParams->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );


  bindSliderSpin( ui->sliderCLength, ui->spinBoxCLength, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCWidth, ui->spinBoxCWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCHeight, ui->spinBoxCHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCylRadius, ui->spinBoxCylRadius, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCylHeight, ui->spinBoxCylHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLMainLength, ui->spinBoxLMainLength, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLMainWidth, ui->spinBoxLMainWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLWingLength, ui->spinBoxLWingLength, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLWingWidth, ui->spinBoxLWingWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLHeight, ui->spinBoxLHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderConeCylRadius, ui->spinBoxConeCylRadius, 100.0, 50.0 );
  bindSliderSpin( ui->sliderConeCylCylHeight, ui->spinBoxConeCylCylHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderConeCylConeHeight, ui->spinBoxConeCylConeHeight, 100.0, 0.90, 0.20 );
  bindSliderSpin( ui->sliderGRLength, ui->spinBoxGRLength, 100, 50 );
  bindSliderSpin( ui->sliderGRWidth, ui->spinBoxGRWidth, 100, 50 );
  bindSliderSpin( ui->sliderGRHeightWall, ui->spinBoxGRHeightWall, 100.0, 50.0 );
  bindSliderSpin( ui->sliderGRHeightRoof, ui->spinBoxGRHeightRoof, 100.0, 0.90, 0.20 );
  bindSliderSpin( ui->sliderPRLength, ui->spinBoxPRLength, 100, 50 );
  bindSliderSpin( ui->sliderPRWidth, ui->spinBoxPRWidth, 100, 50 );
  bindSliderSpin( ui->sliderPRHeightWall, ui->spinBoxPRHeightWall, 100.0, 50.0 );
  bindSliderSpin( ui->sliderPRHeightRoof, ui->spinBoxPRHeightRoof, 100.0, 0.90, 0.20 );
  ui->labelPRHeightWall->setText( tr( "Total height:" ) );
  ui->labelPRHeightRoof->setText( tr( "Wall ratio:" ) );
  bindSliderSpin( ui->sliderTPRBottomLength, ui->spinBoxTPRBottomLength, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTPRBottomWidth, ui->spinBoxTPRBottomWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTPRTopLength, ui->spinBoxTPRTopLength, 100, 50 );
  bindSliderSpin( ui->sliderTPRTopWidth, ui->spinBoxTPRTopWidth, 100, 50 );
  bindSliderSpin( ui->sliderTPRHeightWall, ui->spinBoxTPRHeightWall, 100, 50 );
  bindSliderSpin( ui->sliderTPRHeightRoof, ui->spinBoxTPRHeightRoof, 100.0, 0.90, 0.20 );
  bindSliderSpin( ui->sliderHCRLength, ui->spinBoxHCRLength, 100, 50 );
  bindSliderSpin( ui->sliderHCRWidth, ui->spinBoxHCRWidth, 100, 50 );
  bindSliderSpin( ui->sliderHCRHeightWall, ui->spinBoxHCRHeightWall, 100.0, 50.0 );
  ui->sliderHCRRadius->hide();
  ui->spinBoxHCRRadius->setReadOnly( true );
  ui->spinBoxHCRRadius->setButtonSymbols( QAbstractSpinBox::NoButtons );
  ui->spinBoxHCRRadius->setRange( 0.0, 25.0 );
  auto syncHCRRadius = [this]() {
    ui->spinBoxHCRRadius->setValue( ui->spinBoxHCRWidth->value() / 2.0 );
  };
  syncHCRRadius();
  connect( ui->spinBoxHCRWidth, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [syncHCRRadius]( double ) { syncHCRRadius(); } );
  bindSliderSpin( ui->sliderICLength, ui->spinBoxICLength, 100, 50 );
  bindSliderSpin( ui->sliderICWidth, ui->spinBoxICWidth, 100, 50 );
  bindSliderSpin( ui->sliderICHeight, ui->spinBoxICHeight, 100, 50 );
  bindSliderSpin( ui->sliderICInnerLength, ui->spinBoxICInnerLength, 100, 50 );
  bindSliderSpin( ui->sliderICInnerWidth, ui->spinBoxICInnerWidth, 100, 50 );
  bindSliderSpin( ui->sliderICInnerHeight, ui->spinBoxICInnerHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderICOffsetX, ui->spinBoxICOffsetX, 100.0, 1.0, 0.0 );
  bindSliderSpin( ui->sliderICOffsetY, ui->spinBoxICOffsetY, 100.0, 1.0, 0.0 );
  bindSliderSpin( ui->sliderAGHLength, ui->spinBoxAGHLength, 100, 50 );
  bindSliderSpin( ui->sliderAGHWidth, ui->spinBoxAGHWidth, 100, 50 );
  bindSliderSpin( ui->sliderAGHHeightWall, ui->spinBoxAGHHeightWall, 100, 50 );
  bindSliderSpin( ui->sliderAGHRoofHeight, ui->spinBoxAGHRoofHeight, 100.0, 0.90, 0.20 );
  bindSliderSpin( ui->sliderAGHRidgeLength, ui->spinBoxAGHRidgeLength, 100.0, 50.0 );
  bindSliderSpin( ui->sliderAGHRidgeOffset, ui->spinBoxAGHRidgeOffset, 100.0, 0.8, 0.2 );
  bindSliderSpin( ui->sliderCylHemiRadius, ui->spinBoxCylHemiRadius, 100, 50 );
  bindSliderSpin( ui->sliderCylHemiHeight, ui->spinBoxCylHemiHeight, 100, 50 );
  bindSliderSpin( ui->sliderCylHemiDomeHeight, ui->spinBoxCylHemiDomeHeight, 100.0, 0.90, 0.20 );
  bindSliderSpin( ui->sliderCylHemiBulge, ui->spinBoxCylHemiBulge, 100.0, 1.0 );
  bindSliderSpin( ui->sliderFTBaseRadius, ui->spinBoxFTBaseRadius, 100.0, 50.0 );
  bindSliderSpin( ui->sliderFTBaseHeight, ui->spinBoxFTBaseHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderFTMiddleHeight, ui->spinBoxFTMiddleHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderFTMiddleTopRadius, ui->spinBoxFTMiddleTopRadius, 100.0, 50.0 );
  bindSliderSpin( ui->sliderFTMiddleBulge, ui->spinBoxFTMiddleBulge, 100.0, 0.6 );
  bindSliderSpin( ui->sliderFTConeHeight, ui->spinBoxFTConeHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGLength1, ui->spinBoxTGLength1, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGLength2, ui->spinBoxTGLength2, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGWidth, ui->spinBoxTGWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGHeightWall, ui->spinBoxTGHeightWall, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGRoofHeight, ui->spinBoxTGRoofHeight, 100.0, 0.90, 0.20 );
  bindSliderSpin( ui->sliderTGAngle, ui->spinBoxTGAngle, 10.0, 180.0, 135.0 );
  bindSliderSpin( ui->sliderTGRidgeRatio, ui->spinBoxTGRidgeRatio, 100.0, 0.8, 0.2 );

  bindSliderSpin( ui->sliderROmega, ui->spinBoxROmega, 10.0, 180.0, -180.0 );
  bindSliderSpin( ui->sliderRPhi, ui->spinBoxRPhi, 10.0, 180.0, -180.0 );
  bindSliderSpin( ui->sliderRKappa, ui->spinBoxRKappa, 10.0, 180.0, -180.0 );

  bindSliderSpin( ui->sliderTX, ui->spinBoxTX, 10.0, 100.0, -100.0 );
  bindSliderSpin( ui->sliderTY, ui->spinBoxTY, 10.0, 100.0, -100.0 );
  bindSliderSpin( ui->sliderTZ, ui->spinBoxTZ, 10.0, 100.0, -100.0 );

  ui->labelWidth->setText( tr( "Width:" ) );
  ui->labelHeight->setText( tr( "Height:" ) );
  ui->labelLength->setText( tr( "Length:" ) );
  ui->labelCylRadius->setText( tr( "Radius:" ) );
  ui->labelCylHeight->setText( tr( "Height:" ) );
  ui->labelLMainLength->setText( tr( "Main length:" ) );
  ui->labelLMainWidth->setText( tr( "Main width:" ) );
  ui->labelLWingLength->setText( tr( "Wing length:" ) );
  ui->labelLWingWidth->setText( tr( "Wing width:" ) );
  ui->labelLHeight->setText( tr( "Height:" ) );
  ui->labelConeCylRadius->setText( tr( "Radius:" ) );
  ui->labelConeCylCylHeight->setText( tr( "Total height:" ) );
  ui->labelConeCylConeHeight->setText( tr( "Cylinder ratio:" ) );
  ui->labelGRLength->setText( tr( "Length:" ) );
  ui->labelGRWidth->setText( tr( "Width:" ) );
  ui->labelGRHeightWall->setText( tr( "Total height:" ) );
  ui->labelGRHeightRoof->setText( tr( "Wall ratio:" ) );
  ui->labelPRLength->setText( tr( "Length:" ) );
  ui->labelPRWidth->setText( tr( "Width:" ) );
  ui->labelPRHeightWall->setText( tr( "Total height:" ) );
  ui->labelPRHeightRoof->setText( tr( "Wall ratio:" ) );
  ui->labelTPRBottomLength->setText( tr( "Bottom length:" ) );
  ui->labelTPRBottomWidth->setText( tr( "Bottom width:" ) );
  ui->labelTPRTopLength->setText( tr( "Top length:" ) );
  ui->labelTPRTopWidth->setText( tr( "Top width:" ) );
  ui->labelTPRHeightWall->setText( tr( "Total height:" ) );
  ui->labelTPRHeightRoof->setText( tr( "Wall ratio:" ) );
  ui->labelHCRLength->setText( tr( "Length:" ) );
  ui->labelHCRWidth->setText( tr( "Width:" ) );
  ui->labelHCRHeightWall->setText( tr( "Wall height:" ) );
  ui->labelHCRRadius->setText( tr( "Roof radius:" ) );
  ui->labelCylHemiRadius->setText( tr( "Radius:" ) );
  ui->labelCylHemiHeight->setText( tr( "Total height:" ) );
  ui->labelCylHemiDomeHeight->setText( tr( "Cylinder ratio:" ) );
  ui->labelCylHemiBulge->setText( tr( "Bulge:" ) );
  ui->labelICLength->setText( tr( "Outer length:" ) );
  ui->labelICWidth->setText( tr( "Outer width:" ) );
  ui->labelICHeight->setText( tr( "Outer height:" ) );
  ui->labelICInnerLength->setText( tr( "Inner length:" ) );
  ui->labelICInnerWidth->setText( tr( "Inner width:" ) );
  ui->labelICInnerHeight->setText( tr( "Inner height:" ) );
  ui->labelICOffsetX->setText( tr( "Offset X ratio:" ) );
  ui->labelICOffsetY->setText( tr( "Offset Y ratio:" ) );
  ui->labelAGHLength->setText( tr( "Length:" ) );
  ui->labelAGHWidth->setText( tr( "Width:" ) );
  ui->labelAGHHeightWall->setText( tr( "Total height:" ) );
  ui->labelAGHRoofHeight->setText( tr( "Wall ratio:" ) );
  ui->labelAGHRidgeLength->setText( tr( "Ridge length:" ) );
  ui->labelAGHRidgeOffset->setText( tr( "Ridge ratio:" ) );
  ui->labelFTBaseRadius->setText( tr( "Base radius:" ) );
  ui->labelFTBaseHeight->setText( tr( "Base height:" ) );
  ui->labelFTMiddleHeight->setText( tr( "Middle height:" ) );
  ui->labelFTMiddleTopRadius->setText( tr( "Middle top radius:" ) );
  ui->labelFTMiddleBulge->setText( tr( "Middle bulge:" ) );
  ui->labelFTConeHeight->setText( tr( "Cone height:" ) );
  ui->labelTGLength1->setText( tr( "House 1 length:" ) );
  ui->labelTGLength2->setText( tr( "House 2 length:" ) );
  ui->labelTGWidth->setText( tr( "Width:" ) );
  ui->labelTGHeightWall->setText( tr( "Total height:" ) );
  ui->labelTGRoofHeight->setText( tr( "Wall ratio:" ) );
  ui->labelTGAngle->setText( tr( "Angle:" ) );
  ui->labelTGRidgeRatio->setText( tr( "Ridge ratio:" ) );


  connect( ui->actOBJ, &QAction::triggered, this, &ParamModelerDock::onExportOBJClicked );
  connect( ui->actJSON, &QAction::triggered, this, &ParamModelerDock::onExportJSONClicked );
  connect( ui->actPLY, &QAction::triggered, this, &ParamModelerDock::onExportPLYClicked );
  connect( ui->actDLPointCloud, &QAction::triggered, this, &ParamModelerDock::onExportDLPointCloudClicked );
  connect( ui->actLoadedDLPointCloud, &QAction::triggered, this, &ParamModelerDock::onExportLoadedDLPointCloudClicked );
  connect( ui->actDLDataset, &QAction::triggered, this, &ParamModelerDock::onExportDLDatasetClicked );
  connect( ui->actCurrentPrimitiveDLDataset, &QAction::triggered, this, &ParamModelerDock::onExportCurrentPrimitiveDLDatasetClicked );
  connect( ui->actMesh, &QAction::triggered, this, &ParamModelerDock::onExportMeshClicked );
  connect( ui->actTo3D, &QAction::triggered, this, [this]() { onLoadToQGIS3D( true ); } );
  connect( ui->actLoadPC, &QAction::triggered, this, &ParamModelerDock::onLoadExternalPointCloud );


  m_previewWidget = ui->previewWidget;
  if ( m_previewWidget )
  {
    QWidget *previewParent = m_previewWidget->parentWidget();
    QVBoxLayout *previewLayout = previewParent ? qobject_cast<QVBoxLayout *>( previewParent->layout() ) : nullptr;
    if ( previewLayout )
    {
      QPushButton *togglePreviewButton = new QPushButton( tr( "Show local preview" ), previewParent );
      togglePreviewButton->setCheckable( true );
      togglePreviewButton->setChecked( false );
      const int previewIndex = previewLayout->indexOf( m_previewWidget );
      previewLayout->insertWidget( previewIndex >= 0 ? previewIndex : previewLayout->count(), togglePreviewButton );
      m_previewWidget->setVisible( false );
      connect( togglePreviewButton, &QPushButton::toggled, this, [this, togglePreviewButton]( bool checked ) {
        if ( m_previewWidget )
          m_previewWidget->setVisible( checked );
        togglePreviewButton->setText( checked ? tr( "Hide local preview" ) : tr( "Show local preview" ) );
        if ( checked )
          onUpdatePreview();
      } );
    }
  }
  ui->checkBoxAutoSync->setChecked( false );
  ui->checkBoxAutoSync->setVisible( false );

  m_previewTimer = new QTimer( this );
  m_previewTimer->setSingleShot( true );
  m_previewTimer->setInterval( 33 );
  connect( m_previewTimer, &QTimer::timeout, this, [this]() {
    if ( !m_previewUpdatePending )
      return;

    onUpdatePreview();
  } );

  auto schedulePreview = [this]( int ) { schedulePreviewUpdate(); };

  auto schedulePreviewD = [this]( double ) { schedulePreviewUpdate(); };
  connect( ui->spinBoxROmega, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, schedulePreviewD );
  connect( ui->spinBoxRPhi, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, schedulePreviewD );
  connect( ui->spinBoxRKappa, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, schedulePreviewD );

  connect( ui->spinBoxTX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schedulePreviewD );
  connect( ui->spinBoxTY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schedulePreviewD );
  connect( ui->spinBoxTZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schedulePreviewD );
  connect( ui->sliderCLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylRadius, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLMainLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLMainWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLWingLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLWingWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderConeCylRadius, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderConeCylCylHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderConeCylConeHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRHeightRoof, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderPRLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderPRWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderPRHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderPRHeightRoof, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRBottomLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRBottomWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRTopLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRTopWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRHeightRoof, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderHCRLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderHCRWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderHCRHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHemiRadius, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHemiHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHemiDomeHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHemiBulge, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICInnerLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICInnerWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICInnerHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICOffsetX, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICOffsetY, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHRoofHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHRidgeLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHRidgeOffset, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderFTBaseRadius, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderFTBaseHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderFTMiddleHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderFTMiddleTopRadius, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderFTMiddleBulge, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderFTConeHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGLength1, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGLength2, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGRoofHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGAngle, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGRidgeRatio, &QSlider::valueChanged, this, schedulePreview );

  connect( ui->comboPrimitive, &QComboBox::currentTextChanged, this, &ParamModelerDock::onPrimitiveChanged );

  connect( ui->comboPrimitive, &QComboBox::currentTextChanged, this, [this]( const QString & ) { onUpdatePreview(); } );
  connect( ui->btnRandomParams, &QPushButton::clicked, this, &ParamModelerDock::onRandomizeCurrentPrimitive );

  ui->frameInversion->setVisible( false );
  ui->btnToggleInversion->setCheckable( false );
  ui->btnToggleInversion->setFlat( false );
  ui->btnToggleInversion->setStyleSheet( QString() );
  ui->btnToggleInversion->setText( tr( "Point cloud classification and parameter estimation" ) );
  ui->btnPointNetClassify->setText( tr( "Classify" ) );
  ui->btnInverseParams->setText( tr( "Estimate parameters" ) );
  ui->formLayoutPrimitive->addRow( tr( "Point cloud:" ), ui->btnToggleInversion );
  connect( ui->btnToggleInversion, &QPushButton::clicked, this, &ParamModelerDock::onOpenPointCloudEstimateDialog );

  // 透明模式复选框：微调参数时让模型半透明，不挡点云
  mGhostModeCheckBox = new QCheckBox( tr( "Ghost mode (see through model)" ), this );
  mGhostModeCheckBox->setChecked( false );
  mGhostModeCheckBox->setToolTip( tr( "Make the 3D model transparent so the point cloud is clearly visible during fine-tuning." ) );
  ui->formLayoutPrimitive->addRow( tr( "3D model:" ), mGhostModeCheckBox );
  connect( mGhostModeCheckBox, &QCheckBox::toggled, this, [this]( bool checked ) {
    ParamModelerScene3D::setGhostMode( checked );
    if ( m_realtimeModelLoaded )
      onUpdatePreview();
  } );

  connect( ui->btnLoadPointCloud, &QPushButton::clicked, this, &ParamModelerDock::onLoadInputData );
  connect( ui->btnPointNetClassify, &QPushButton::clicked, this, &ParamModelerDock::onPointNetClassify );
  connect( ui->btnInverseParams, &QPushButton::clicked, this, &ParamModelerDock::onInverseParams );

  ui->btnPointNetClassify->setEnabled( false );
  ui->btnInverseParams->setEnabled( false );

  DEBUG_LOG( L"\n[ParamModelerDock] initialization complete\n" );
  DEBUG_LOG( m_currentPrimitive.toStdWString().c_str() );
  DEBUG_LOG( L"\n" );
}

ParamModelerDock::~ParamModelerDock()
{
  ParamModelerScene3D::clearRealtimePreviewMesh( mIface );
  m_modelLayer = nullptr;
  delete ui;
}

void ParamModelerDock::onPrimitiveChanged( const QString &prim )
{
  QString dbg = QString( "[ParamModeler] primitive changed: %1 -> %2\n" ).arg( m_currentPrimitive ).arg( prim );
  DEBUG_LOG( dbg.toStdWString().c_str() );


  if ( !m_currentPrimitive.isEmpty() )
  {
    m_poseMap[m_currentPrimitive] = {
      ui->spinBoxTX->value(),
      ui->spinBoxTY->value(),
      ui->spinBoxTZ->value(),
      ui->spinBoxROmega->value(),
      ui->spinBoxRPhi->value(),
      ui->spinBoxRKappa->value()
    };
  }


  static const QHash<QString, QWidget *( Ui::ParamModelerDock::* )> pageMap = {
    { "Cuboid", &Ui::ParamModelerDock::pageCuboid },
    { "Cylinder", &Ui::ParamModelerDock::pageCylinder },
    { "LHouse", &Ui::ParamModelerDock::pageLHouse },
    { "ConeCylinder", &Ui::ParamModelerDock::pageConeCylinder },
    { "GabledRoof", &Ui::ParamModelerDock::pageGabledRoof },
    { "PyramidRoof", &Ui::ParamModelerDock::pagePyramidRoof },
    { "TruncatedPyramidRoof", &Ui::ParamModelerDock::pageTPRoof },
    { "HalfCylinderRoof", &Ui::ParamModelerDock::pageHalfCylinderRoof },
    { "CylinderDome", &Ui::ParamModelerDock::pageCylinderHemisphere },
    { "CylinderHemisphere", &Ui::ParamModelerDock::pageCylinderHemisphere },
    { "IndentedCuboid", &Ui::ParamModelerDock::pageIndentedCuboid },
    { "AsymmetricGableHouse", &Ui::ParamModelerDock::pageAsymmetricGableHouse },
    { "FourStageRoundTower", &Ui::ParamModelerDock::pageFourStageRoundTower },
    { "TwoGableHouses", &Ui::ParamModelerDock::pageTwoGableHouses },
  };

  auto it = pageMap.find( prim );
  if ( it != pageMap.end() )
    ui->stackedWidgetParams->setCurrentWidget( ui->*( it.value() ) );


  if ( m_poseMap.contains( prim ) )
  {
    const auto &p = m_poseMap[prim];
    ui->spinBoxTX->setValue( p[0] );
    ui->spinBoxTY->setValue( p[1] );
    ui->spinBoxTZ->setValue( p[2] );
    ui->spinBoxROmega->setValue( p[3] );
    ui->spinBoxRPhi->setValue( p[4] );
    ui->spinBoxRKappa->setValue( p[5] );
  }
  else
  {
    ui->spinBoxTX->setValue( 0.0 );
    ui->spinBoxTY->setValue( 0.0 );
    ui->spinBoxTZ->setValue( 0.0 );
    ui->spinBoxROmega->setValue( 0 );
    ui->spinBoxRPhi->setValue( 0 );
    ui->spinBoxRKappa->setValue( 0 );
  }

  m_currentPrimitive = prim;


  QString poseDbg = QString( "[ParamModeler] restore pose: tx=%1 ty=%2 tz=%3 omega=%4 phi=%5 kappa=%6\n" )
                      .arg( poseTranslateX(), 0, 'f', 2 )
                      .arg( poseTranslateY(), 0, 'f', 2 )
                      .arg( poseTranslateZ(), 0, 'f', 2 )
                      .arg( poseRotateX(), 0, 'f', 2 )
                      .arg( poseRotateY(), 0, 'f', 2 )
                      .arg( poseRotateZ(), 0, 'f', 2 );
  DEBUG_LOG( poseDbg.toStdWString().c_str() );
}


// ======================= Random Parameters ===================
void ParamModelerDock::onRandomizeCurrentPrimitive()
{
  randomizeCurrentPrimitiveParams( true );
}

void ParamModelerDock::randomizeCurrentPrimitiveParams( bool refreshPreview, bool randomizePose )
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

  const QString prim = ui->comboPrimitive->currentText();

  if ( prim == "Cuboid" )
  {
    set( ui->spinBoxCLength, rnd( 6.0, 18.0 ) );
    set( ui->spinBoxCWidth, rnd( 4.0, 12.0 ) );
    set( ui->spinBoxCHeight, rnd( 2.5, 8.0 ) );
  }
  else if ( prim == "Cylinder" )
  {
    set( ui->spinBoxCylRadius, rnd( 2.0, 8.0 ) );
    set( ui->spinBoxCylHeight, rnd( 3.0, 14.0 ) );
  }
  else if ( prim == "LHouse" )
  {
    const double mainLength = rnd( 10.0, 22.0 );
    const double mainWidth = rnd( 6.0, 14.0 );
    double wingLength = mainLength * 0.55;
    double wingWidth = mainWidth * 0.45;
    for ( int attempt = 0; attempt < 20; ++attempt )
    {
      wingLength = rnd( mainLength * 0.50, mainLength * 0.85 );
      wingWidth = rnd( mainWidth * 0.35, mainWidth * 0.60 );
      const double missingRatio = wingLength * ( mainWidth - wingWidth ) / ( ( mainLength + wingLength ) * mainWidth );
      if ( missingRatio >= 0.16 )
        break;
    }
    set( ui->spinBoxLMainLength, mainLength );
    set( ui->spinBoxLMainWidth, mainWidth );
    set( ui->spinBoxLWingLength, wingLength );
    set( ui->spinBoxLWingWidth, wingWidth );
    set( ui->spinBoxLHeight, rnd( 2.5, 7.0 ) );
  }
  else if ( prim == "ConeCylinder" )
  {
    set( ui->spinBoxConeCylRadius, rnd( 2.0, 8.0 ) );
    set( ui->spinBoxConeCylCylHeight, rnd( 4.5, 14.0 ) );
    set( ui->spinBoxConeCylConeHeight, rnd( 0.45, 0.80, 0.01 ) );
  }
  else if ( prim == "GabledRoof" )
  {
    set( ui->spinBoxGRLength, rnd( 8.0, 24.0 ) );
    set( ui->spinBoxGRWidth, rnd( 5.0, 14.0 ) );
    set( ui->spinBoxGRHeightWall, rnd( 4.5, 12.0 ) );
    set( ui->spinBoxGRHeightRoof, rnd( 0.55, 0.82, 0.01 ) );
  }
  else if ( prim == "PyramidRoof" )
  {
    set( ui->spinBoxPRLength, rnd( 7.0, 20.0 ) );
    set( ui->spinBoxPRWidth, rnd( 5.0, 16.0 ) );
    set( ui->spinBoxPRHeightWall, rnd( 4.5, 12.0 ) );
    set( ui->spinBoxPRHeightRoof, rnd( 0.55, 0.82, 0.01 ) );
  }
  else if ( prim == "TruncatedPyramidRoof" )
  {
    const double bottomLength = rnd( 10.0, 24.0 );
    const double bottomWidth = rnd( 8.0, 18.0 );
    set( ui->spinBoxTPRBottomLength, bottomLength );
    set( ui->spinBoxTPRBottomWidth, bottomWidth );
    set( ui->spinBoxTPRTopLength, rnd( bottomLength * 0.35, bottomLength * 0.80 ) );
    set( ui->spinBoxTPRTopWidth, rnd( bottomWidth * 0.35, bottomWidth * 0.80 ) );
    set( ui->spinBoxTPRHeightWall, rnd( 4.5, 12.0 ) );
    set( ui->spinBoxTPRHeightRoof, rnd( 0.55, 0.82, 0.01 ) );
  }
  else if ( prim == "HalfCylinderRoof" )
  {
    set( ui->spinBoxHCRLength, rnd( 8.0, 24.0 ) );
    set( ui->spinBoxHCRWidth, rnd( 4.0, 14.0 ) );
    set( ui->spinBoxHCRHeightWall, rnd( 2.5, 7.0 ) );
  }
  else if ( prim == "CylinderDome" || prim == "CylinderHemisphere" )
  {
    set( ui->spinBoxCylHemiRadius, rnd( 3.0, 9.0 ) );
    set( ui->spinBoxCylHemiHeight, rnd( 4.5, 13.0 ) );
    set( ui->spinBoxCylHemiDomeHeight, rnd( 0.50, 0.85, 0.01 ) );
    set( ui->spinBoxCylHemiBulge, rnd( 0.15, 0.65, 0.01 ) );
  }
  else if ( prim == "IndentedCuboid" )
  {
    const double outerLength = rnd( 10.0, 24.0 );
    const double outerWidth = rnd( 8.0, 18.0 );
    const double outerHeight = rnd( 4.0, 12.0 );
    const double innerLength = rnd( outerLength * 0.25, outerLength * 0.60 );
    const double innerWidth = rnd( outerWidth * 0.25, outerWidth * 0.60 );
    set( ui->spinBoxICLength, outerLength );
    set( ui->spinBoxICWidth, outerWidth );
    set( ui->spinBoxICHeight, outerHeight );
    set( ui->spinBoxICInnerLength, innerLength );
    set( ui->spinBoxICInnerWidth, innerWidth );
    set( ui->spinBoxICInnerHeight, rnd( outerHeight * 0.25, outerHeight * 0.75 ) );
    set( ui->spinBoxICOffsetX, rnd( 0.0, 1.0, 0.01 ) );
    set( ui->spinBoxICOffsetY, rnd( 0.0, 1.0, 0.01 ) );
  }
  else if ( prim == "AsymmetricGableHouse" )
  {
    const double length = rnd( 10.0, 24.0 );
    const double width = rnd( 5.0, 14.0 );
    set( ui->spinBoxAGHLength, length );
    set( ui->spinBoxAGHWidth, width );
    set( ui->spinBoxAGHHeightWall, rnd( 4.5, 12.0 ) );
    set( ui->spinBoxAGHRoofHeight, rnd( 0.55, 0.82, 0.01 ) );
    set( ui->spinBoxAGHRidgeLength, rnd( length * 0.45, length * 0.75 ) );
    set( ui->spinBoxAGHRidgeOffset, rndAwayFromCenter( 0.2, 0.38, 0.62, 0.8, 0.01 ) );
  }
  else if ( prim == "FourStageRoundTower" )
  {
    set( ui->spinBoxFTBaseRadius, rnd( 5.0, 12.0 ) );
    set( ui->spinBoxFTBaseHeight, rnd( 1.2, 3.5 ) );
    set( ui->spinBoxFTMiddleHeight, rnd( 1.0, 3.0 ) );
    set( ui->spinBoxFTMiddleTopRadius, rnd( 0.5, 1.6 ) );
    set( ui->spinBoxFTMiddleBulge, rnd( 0.15, 0.45, 0.01 ) );
    set( ui->spinBoxFTConeHeight, rnd( 0.8, 2.0 ) );
  }
  else if ( prim == "TwoGableHouses" )
  {
    set( ui->spinBoxTGLength1, rnd( 10.0, 24.0 ) );
    set( ui->spinBoxTGLength2, rnd( 7.0, 18.0 ) );
    set( ui->spinBoxTGWidth, rnd( 5.0, 12.0 ) );
    set( ui->spinBoxTGHeightWall, rnd( 4.5, 12.0 ) );
    set( ui->spinBoxTGRoofHeight, rnd( 0.55, 0.82, 0.01 ) );
    set( ui->spinBoxTGAngle, rnd( 135.0, 165.0, 1.0 ) );
    set( ui->spinBoxTGRidgeRatio, rnd( 0.3, 0.7, 0.01 ) );
  }

  // --- 训练数据生成：随机水平朝向 ---
  if ( randomizePose )
  {
    ui->spinBoxROmega->setValue( 0.0 );
    ui->spinBoxRPhi->setValue( 0.0 );
    ui->spinBoxRKappa->setValue( rnd( -180.0, 180.0, 1.0 ) );
    ui->spinBoxTX->setValue( 0.0 );
    ui->spinBoxTY->setValue( 0.0 );
    ui->spinBoxTZ->setValue( 0.0 );
  }

  if ( refreshPreview )
    onUpdatePreview();
}

void ParamModelerDock::onOpenPointCloudEstimateDialog()
{
  QDialog dialog( this );
  dialog.setWindowTitle( tr( "Point cloud classification and parameter estimation" ) );
  dialog.resize( 560, 520 );

  auto *mainLayout = new QVBoxLayout( &dialog );
  auto *inputTitle = new QLabel( tr( "1. Input point cloud" ), &dialog );
  inputTitle->setStyleSheet( QStringLiteral( "font-weight: bold;" ) );
  auto *inputInfo = new QLabel( &dialog );
  inputInfo->setWordWrap( true );
  inputInfo->setStyleSheet( QStringLiteral( "color: #555;" ) );
  auto *btnLoad = new QPushButton( tr( "Load point cloud" ), &dialog );

  auto *processTitle = new QLabel( tr( "2. Classification and parameter estimation" ), &dialog );
  processTitle->setStyleSheet( QStringLiteral( "font-weight: bold; margin-top: 8px;" ) );
  auto *modelLayout = new QHBoxLayout();
  auto *modelLabel = new QLabel( tr( "Model:" ), &dialog );
  auto *comboModel = new QComboBox( &dialog );
  comboModel->addItem( QStringLiteral( "PointNeXt" ) );
  comboModel->addItem( QStringLiteral( "PointNet++" ) );
  comboModel->addItem( QStringLiteral( "PointNet" ) );
  comboModel->setFixedWidth( 160 );
  modelLayout->addWidget( modelLabel );
  modelLayout->addWidget( comboModel );

  auto *btnSettings = new QPushButton( tr( "⚙" ), &dialog );
  btnSettings->setFixedSize( 28, 28 );
  btnSettings->setToolTip( tr( "PointNet path settings" ) );
  btnSettings->setCursor( Qt::PointingHandCursor );
  btnSettings->setFlat( true );
  connect( btnSettings, &QPushButton::clicked, &dialog, [&dialog]() {
    ParamModelerConfig::showSettingsDialog( &dialog );
  } );
  modelLayout->addWidget( btnSettings );

  modelLayout->addStretch();
  auto *resultLabel = new QLabel( tr( "Result: -" ), &dialog );
  resultLabel->setWordWrap( true );
  resultLabel->setStyleSheet( QStringLiteral( "font-weight: bold; padding: 4px 0;" ) );

  auto *progress = new QProgressBar( &dialog );
  progress->setVisible( false );
  progress->setTextVisible( false );

  auto *buttonLayout = new QHBoxLayout();
  auto *btnClassify = new QPushButton( tr( "Classify" ), &dialog );
  auto *btnInverse = new QPushButton( tr( "Estimate parameters" ), &dialog );
  auto *btnFinish = new QPushButton( tr( "Return to fine tuning" ), &dialog );
  btnClassify->setMinimumHeight( 28 );
  btnInverse->setMinimumHeight( 28 );
  btnFinish->setMinimumHeight( 28 );
  buttonLayout->addWidget( btnClassify );
  buttonLayout->addWidget( btnInverse );
  buttonLayout->addWidget( btnFinish );

  auto *table = new QTableWidget( &dialog );
  table->setColumnCount( 2 );
  table->setHorizontalHeaderLabels( QStringList() << tr( "Parameter" ) << tr( "Value" ) );
  table->horizontalHeader()->setStretchLastSection( true );
  table->verticalHeader()->setVisible( false );
  table->setAlternatingRowColors( true );
  table->setSelectionBehavior( QAbstractItemView::SelectRows );
  table->setEditTriggers( QAbstractItemView::NoEditTriggers );
  table->setMinimumHeight( 160 );
  bool parametersApplied = false;

  mainLayout->addWidget( inputTitle );
  mainLayout->addWidget( btnLoad );
  mainLayout->addWidget( inputInfo );
  mainLayout->addWidget( processTitle );
  mainLayout->addLayout( modelLayout );
  mainLayout->addWidget( resultLabel );
  mainLayout->addWidget( progress );
  mainLayout->addLayout( buttonLayout );
  mainLayout->addWidget( table );

  auto updateInputInfo = [&]() {
    if ( m_inputDataPath.isEmpty() )
    {
      inputInfo->setText( tr( "No point cloud loaded" ) );
      btnClassify->setEnabled( false );
      btnInverse->setEnabled( false );
      return;
    }

    const QFileInfo fi( m_inputDataPath );
    PointCloud pc = PointCloudLoader::load( m_inputDataPath );
    if ( pc.points.isEmpty() )
    {
      inputInfo->setText( tr( "Selected: %1\nFailed to read point cloud or point count is 0." ).arg( fi.fileName() ) );
      btnClassify->setEnabled( false );
      btnInverse->setEnabled( false );
      return;
    }

    inputInfo->setText(
      tr( "Loaded: %1\nPoints: %2\nX: [%3, %4]\nY: [%5, %6]\nZ: [%7, %8]" )
        .arg( fi.fileName() )
        .arg( pc.points.size() )
        .arg( pc.bboxMin.x(), 0, 'f', 3 )
        .arg( pc.bboxMax.x(), 0, 'f', 3 )
        .arg( pc.bboxMin.y(), 0, 'f', 3 )
        .arg( pc.bboxMax.y(), 0, 'f', 3 )
        .arg( pc.bboxMin.z(), 0, 'f', 3 )
        .arg( pc.bboxMax.z(), 0, 'f', 3 )
    );
    btnClassify->setEnabled( true );
    btnInverse->setEnabled( false );
    resultLabel->setText( tr( "Result: -" ) );
    table->setRowCount( 0 );
    parametersApplied = false;
  };

  connect( btnLoad, &QPushButton::clicked, &dialog, [&]() {
    const QString filePath = QFileDialog::getOpenFileName(
      &dialog, tr( "Load point cloud" ), "",
      tr( "Point cloud files (*.ply *.las *.laz *.xyz *.txt)" )
    );
    if ( filePath.isEmpty() )
      return;

    m_inputDataPath = filePath;
    updateInputInfo();
  } );

  auto selectedBackend = [&]() {
    const QString modelName = comboModel->currentText();
    if ( modelName == QStringLiteral( "PointNet" ) )
      return PointNetBackend::PointNet;
    if ( modelName == QStringLiteral( "PointNeXt" ) )
      return PointNetBackend::PointNeXt;
    return PointNetBackend::PointNet2;
  };

  connect( btnClassify, &QPushButton::clicked, &dialog, [&]() {
    if ( m_inputDataPath.isEmpty() )
      return;

    progress->setRange( 0, 0 );
    progress->setVisible( true );
    resultLabel->setText( tr( "Classifying..." ) );
    btnClassify->setEnabled( false );
    btnInverse->setEnabled( false );
    QApplication::processEvents();

    PointNetPredictResult result = PointNetRunner::predict( m_inputDataPath, selectedBackend(), 2048, 3 );
    progress->setRange( 0, 100 );
    progress->setValue( 100 );
    progress->setVisible( false );
    btnClassify->setEnabled( true );

    if ( !result.errorMessage.isEmpty() )
    {
      QMessageBox::warning( &dialog, tr( "Classification failed" ), result.errorMessage );
      resultLabel->setText( tr( "Result: -" ) );
      return;
    }
    if ( result.predictions.isEmpty() )
    {
      QMessageBox::warning( &dialog, tr( "Classification failed" ), tr( "No prediction returned." ) );
      resultLabel->setText( tr( "Result: -" ) );
      return;
    }

    const PointNetPrediction top1 = result.predictions.first();
    resultLabel->setText(
      tr( "Result: %1\nConfidence: %2%\nSwitched to corresponding primitive. You can estimate parameters and return to fine tuning." )
        .arg( top1.className )
        .arg( top1.probability * 100.0, 0, 'f', 1 )
    );
    ui->comboPrimitive->setCurrentText( top1.className );
    btnInverse->setEnabled( true );
  } );

  connect( btnInverse, &QPushButton::clicked, &dialog, [&]() {
    if ( m_inputDataPath.isEmpty() )
      return;

    progress->setRange( 0, 0 );
    progress->setVisible( true );
    btnInverse->setEnabled( false );
    QApplication::processEvents();

    const QString prim = ui->comboPrimitive->currentText();
    const PointNetRegressionResult regression = PointNetRunner::predictParams( m_inputDataPath, selectedBackend(), prim, 2048 );

    progress->setRange( 0, 100 );
    progress->setValue( 100 );
    progress->setVisible( false );
    btnInverse->setEnabled( true );

    if ( !regression.errorMessage.isEmpty() )
    {
      QMessageBox::warning( &dialog, tr( "Parameter estimation failed" ), regression.errorMessage );
      return;
    }

    const QMap<QString, double> params = pointNetParamsToUiParams( prim, regression.params );
    if ( params.isEmpty() )
    {
      QMessageBox::warning( &dialog, tr( "Parameter estimation failed" ), tr( "No parameters returned." ) );
      return;
    }

    ParamInverter::applyToUI( this, params );
    parametersApplied = true;
    table->setRowCount( params.size() );
    int row = 0;
    for ( auto it = params.cbegin(); it != params.cend(); ++it, ++row )
    {
      table->setItem( row, 0, new QTableWidgetItem( it.key() ) );
      table->setItem( row, 1, new QTableWidgetItem( QString::number( it.value(), 'f', 2 ) ) );
    }
    onUpdatePreview();
    resultLabel->setText( resultLabel->text() + tr( "\nParameters applied to main panel." ) );
  } );

  connect( btnFinish, &QPushButton::clicked, &dialog, [&]() {
    if ( parametersApplied && !m_inputDataPath.isEmpty() )
    {
      const QMessageBox::StandardButton answer = QMessageBox::question(
        &dialog,
        tr( "Load to QGIS 3D" ),
        tr( "Load the input point cloud and the estimated model into the QGIS 3D scene now?" ),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes
      );
      if ( answer == QMessageBox::Yes )
      {
        // --- Auto-align model translation to point cloud center ---
        QVector3D pcCenter;
        double pcScale = 1.0;
        if ( metadataPointCloudInfoForInput( m_inputDataPath, nullptr, &pcCenter, &pcScale ) )
        {
          // 缓存元数据，供 onLoadToQGIS3D / onUpdatePreview 反归一化模型
          m_metadataCenter = pcCenter;
          m_metadataScale  = pcScale;
          m_hasMetadata    = true;

          const QString prim = ui->comboPrimitive->currentText();
          MeshData previewMesh = BuildMesh::build( prim, this );
          if ( !previewMesh.isEmpty() )
          {
            // Compute model bbox center from vertices (normalized space)
            QVector3D modelMin( std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max() );
            QVector3D modelMax( std::numeric_limits<float>::lowest(),
                                std::numeric_limits<float>::lowest(),
                                std::numeric_limits<float>::lowest() );
            for ( const QVector3D &v : previewMesh.vertices )
            {
              if ( v.x() < modelMin.x() ) modelMin.setX( v.x() );
              if ( v.y() < modelMin.y() ) modelMin.setY( v.y() );
              if ( v.z() < modelMin.z() ) modelMin.setZ( v.z() );
              if ( v.x() > modelMax.x() ) modelMax.setX( v.x() );
              if ( v.y() > modelMax.y() ) modelMax.setY( v.y() );
              if ( v.z() > modelMax.z() ) modelMax.setZ( v.z() );
            }
            const QVector3D modelCenter = ( modelMin + modelMax ) * 0.5f;

            // Both model vertices and point cloud are in meter/projected-meter space.
            // Translation = pcCenter - modelCenter  (no scale factor needed)
            const double tx = static_cast<double>( pcCenter.x() ) - static_cast<double>( modelCenter.x() );
            const double ty = static_cast<double>( pcCenter.y() ) - static_cast<double>( modelCenter.y() );
            const double tz = static_cast<double>( pcCenter.z() ) - static_cast<double>( modelCenter.z() );
            setPoseTranslate( tx, ty, tz );

            DEBUG_LOG( QString( "[Align] pcCenter=(%1,%2,%3) modelCenter=(%4,%5,%6) → tx=%7 ty=%8 tz=%9\n" )
                         .arg( pcCenter.x(), 0, 'f', 2 ).arg( pcCenter.y(), 0, 'f', 2 ).arg( pcCenter.z(), 0, 'f', 2 )
                         .arg( modelCenter.x(), 0, 'f', 2 ).arg( modelCenter.y(), 0, 'f', 2 ).arg( modelCenter.z(), 0, 'f', 2 )
                         .arg( tx, 0, 'f', 2 ).arg( ty, 0, 'f', 2 ).arg( tz, 0, 'f', 2 )
                         .toStdWString().c_str() );
          }
          else
          {
            DEBUG_LOG( L"[Align] mesh build failed, skipping auto-alignment\n" );
          }
        }
        else
        {
          DEBUG_LOG( QString( "[Align] no metadata for %1, skipping auto-alignment\n" )
                       .arg( m_inputDataPath ).toStdWString().c_str() );
        }

        onLoadToQGIS3D( true );
        if ( !loadPointCloudToQGIS3D( m_inputDataPath, false ) )
          QMessageBox::warning( &dialog, tr( "Point cloud load failed" ), tr( "The model was loaded, but the point cloud was not loaded into QGIS 3D." ) );
      }
    }
    dialog.accept();
  } );

  updateInputInfo();
  dialog.exec();
}

// ======================= Export Validation ===================
static bool checkMeshValid( const QString &primitiveType, ParamModelerDock *dock )
{
  MeshData mesh = BuildMesh::build( primitiveType, dock );
  if ( mesh.isEmpty() )
  {
    QMessageBox::warning( dock, QObject::tr( "Cannot export" ), QObject::tr( "Current parameters cannot generate a valid model. Adjust parameters until the preview is visible, then export." ) );
    return false;
  }
  return true;
}

void ParamModelerDock::onExportOBJClicked()
{
  QString primitiveType = ui->comboPrimitive->currentText();
  DEBUG_LOG( QString( "[Export] OBJ export start, primitive: %1\n" ).arg( primitiveType ).toStdWString().c_str() );
  if ( !checkMeshValid( primitiveType, this ) )
    return;

  QString fileName = QFileDialog::getSaveFileName(
    this,
    tr( "Save OBJ file" ),
    "",
    tr( "OBJ Files (*.obj)" )
  );
  if ( fileName.isEmpty() )
    return;

  bool ok = ExportOBJ::exportOBJ( fileName, primitiveType, this );
  DEBUG_LOG( QString( "[Export] OBJ export %1: %2\n" ).arg( ok ? "success" : "failed" ).arg( fileName ).toStdWString().c_str() );

  if ( ok )
  {
    QMessageBox::information(
      this,
      tr( "Export succeeded" ),
      tr( "OBJ file exported to:\n%1" ).arg( fileName )
    );
  }
  else
  {
    QMessageBox::critical(
      this,
      tr( "Export failed" ),
      tr( "OBJ file could not be exported. Please check parameters or output path." )
    );
  }
}

void ParamModelerDock::onExportJSONClicked()
{
  QString primitiveType = ui->comboPrimitive->currentText();
  DEBUG_LOG( QString( "[Export] JSON export start, primitive: %1\n" ).arg( primitiveType ).toStdWString().c_str() );
  if ( !checkMeshValid( primitiveType, this ) )
    return;
  ExportJSON::writeJSON( this );
  DEBUG_LOG( L"[Export] JSON export done\n" );
}

void ParamModelerDock::onExportPLYClicked()
{
  QString primitiveType = ui->comboPrimitive->currentText();
  DEBUG_LOG( QString( "[Export] PLY export start, primitive: %1\n" ).arg( primitiveType ).toStdWString().c_str() );
  if ( !checkMeshValid( primitiveType, this ) )
    return;

  QString fileName = QFileDialog::getSaveFileName(
    this, tr( "Save point cloud file" ), "", tr( "PLY Files (*.ply)" )
  );
  if ( fileName.isEmpty() )
    return;

  ExportPointCloud::exportPLY( fileName, primitiveType, this );
}

void ParamModelerDock::onExportDLPointCloudClicked()
{
  QString primitiveType = ui->comboPrimitive->currentText();
  DEBUG_LOG( QString( "[Export] DL TXT export start, primitive: %1\n" ).arg( primitiveType ).toStdWString().c_str() );
  if ( !checkMeshValid( primitiveType, this ) )
    return;

  QString fileName = QFileDialog::getSaveFileName(
    this, tr( "Save deep learning input point cloud" ), "", tr( "TXT Files (*.txt)" )
  );
  if ( fileName.isEmpty() )
    return;

  bool ok = ExportPointCloud::exportDLInputTXT( fileName, primitiveType, this, 2048 );
  if ( ok )
  {
    QMessageBox::information(
      this,
      tr( "Export succeeded" ),
      tr( "Deep learning input point cloud saved to:\n%1" ).arg( fileName )
    );
  }
}

static void normalizeDLPoints( QVector<QVector3D> &points )
{
  if ( points.isEmpty() )
    return;

  QVector3D center( 0, 0, 0 );
  for ( const QVector3D &p : points )
    center += p;
  center /= float( points.size() );

  float maxRadius = 0.0f;
  for ( const QVector3D &p : points )
    maxRadius = std::max( maxRadius, ( p - center ).length() );
  if ( maxRadius <= 1e-8f )
    maxRadius = 1.0f;

  for ( QVector3D &p : points )
    p = ( p - center ) / maxRadius;
}

static QVector<QVector3D> sampleFixedPointCount( const QVector<QVector3D> &source, int pointCount )
{
  QVector<QVector3D> sampled;
  if ( source.isEmpty() || pointCount <= 0 )
    return sampled;

  sampled.reserve( pointCount );
  for ( int i = 0; i < pointCount; ++i )
  {
    const int index = QRandomGenerator::global()->bounded( source.size() );
    sampled.append( source[index] );
  }
  return sampled;
}

static bool writeDLPointTXT( const QString &fileName, QVector<QVector3D> points )
{
  normalizeDLPoints( points );

  QFile file( fileName );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
    return false;

  QTextStream out( &file );
  out.setRealNumberNotation( QTextStream::FixedNotation );
  out.setRealNumberPrecision( 8 );
  for ( const QVector3D &p : points )
    out << p.x() << " " << p.y() << " " << p.z() << "\n";
  return true;
}

void ParamModelerDock::onExportLoadedDLPointCloudClicked()
{
  if ( m_inputDataPath.isEmpty() )
  {
    QMessageBox::warning( this, tr( "Export failed" ), tr( "Please load an input point cloud first." ) );
    return;
  }

  const QString fileName = QFileDialog::getSaveFileName(
    this, tr( "Save PointNet Input TXT" ), "", tr( "TXT Files (*.txt)" )
  );
  if ( fileName.isEmpty() )
    return;

  PointCloud pc = PointCloudLoader::load( m_inputDataPath );
  if ( pc.points.isEmpty() )
  {
    QMessageBox::warning( this, tr( "Export failed" ), tr( "Cannot read points from the loaded input file." ) );
    return;
  }

  const int pointCount = 2048;
  QVector<QVector3D> sampled = sampleFixedPointCount( pc.points, pointCount );
  if ( !writeDLPointTXT( fileName, sampled ) )
  {
    QMessageBox::critical( this, tr( "Export failed" ), tr( "Cannot write PointNet input TXT file." ) );
    return;
  }

  QMessageBox::information(
    this,
    tr( "Export complete" ),
    tr( "PointNet input TXT saved.\nSource points: %1\nExported points: %2\nFile: %3" )
      .arg( pc.points.size() )
      .arg( pointCount )
      .arg( fileName )
  );
}
// ========================Batch DL Dataset====================
static QString safeClassDirName( const QString &primitiveType )
{
  QString safe = primitiveType;
  safe.replace( QRegularExpression( "[^A-Za-z0-9_\\-]" ), "_" );
  return safe;
}

static QString datasetRootFromSelectedFolder( const QString &selectedPath )
{
  const QFileInfo selectedInfo( selectedPath );
  if ( selectedInfo.fileName().compare( QStringLiteral( "datasets" ), Qt::CaseInsensitive ) == 0 )
    return selectedPath;

  return QDir( selectedPath ).filePath( QStringLiteral( "datasets" ) );
}

static bool writeJsonDocumentChecked( const QString &path, const QJsonDocument &doc, QString *errorMessage )
{
  const QByteArray bytes = doc.toJson( QJsonDocument::Indented );

  QSaveFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "Cannot open %1 for writing: %2" ).arg( path, file.errorString() );
    return false;
  }

  const qint64 written = file.write( bytes );
  if ( written != bytes.size() )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "Incomplete JSON write: %1/%2 bytes." ).arg( written ).arg( bytes.size() );
    file.cancelWriting();
    return false;
  }

  if ( !file.commit() )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "Cannot commit %1: %2" ).arg( path, file.errorString() );
    return false;
  }

  QFile verifyFile( path );
  if ( !verifyFile.open( QIODevice::ReadOnly ) )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "Cannot reopen %1 for verification: %2" ).arg( path, verifyFile.errorString() );
    return false;
  }

  QJsonParseError parseError;
  QJsonDocument::fromJson( verifyFile.readAll(), &parseError );
  if ( parseError.error != QJsonParseError::NoError )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "Written JSON verification failed at offset %1: %2" )
                        .arg( parseError.offset )
                        .arg( parseError.errorString() );
    return false;
  }

  return true;
}

static QJsonArray vectorToJsonArray( const QVector3D &v )
{
  QJsonArray arr;
  arr.append( v.x() );
  arr.append( v.y() );
  arr.append( v.z() );
  return arr;
}

static QJsonObject pointCloudInfoToJson( const DLPointCloudInfo &info )
{
  QJsonObject obj;
  obj["bboxMin"] = vectorToJsonArray( info.bboxMin );
  obj["bboxMax"] = vectorToJsonArray( info.bboxMax );
  obj["bboxSize"] = vectorToJsonArray( info.bboxSize );
  obj["center"] = vectorToJsonArray( info.center );
  obj["scale"] = info.scale;
  obj["normalization"] = QStringLiteral( "centered_by_mean_and_scaled_by_max_radius" );
  obj["skipBottom"] = true;
  return obj;
}

static bool vectorFromJsonArray( const QJsonValue &value, QVector3D &out )
{
  if ( !value.isArray() )
    return false;
  const QJsonArray arr = value.toArray();
  if ( arr.size() < 3 )
    return false;
  out = QVector3D( arr.at( 0 ).toDouble(), arr.at( 1 ).toDouble(), arr.at( 2 ).toDouble() );
  return true;
}

static double maxAbsComponent( const QVector3D &v )
{
  return std::max( { std::abs( static_cast<double>( v.x() ) ),
                     std::abs( static_cast<double>( v.y() ) ),
                     std::abs( static_cast<double>( v.z() ) ) } );
}

static bool pointCloudLooksNormalizedForDisplay( const PointCloud &pc )
{
  if ( pc.points.isEmpty() )
    return false;

  const QVector3D bboxSize = pc.bboxMax - pc.bboxMin;
  const double maxDim = maxAbsComponent( bboxSize );
  if ( maxDim <= 3.5 )
    return true;

  QVector3D center( 0, 0, 0 );
  for ( const QVector3D &p : pc.points )
    center += p;
  center /= static_cast<float>( pc.points.size() );

  double maxRadius = 0.0;
  for ( const QVector3D &p : pc.points )
    maxRadius = std::max( maxRadius, static_cast<double>( ( p - center ).length() ) );

  return maxRadius <= 1.5;
}

static bool denormInfoFromPlyComment( const QString &filePath, QVector3D &center, double &scale )
{
  if ( QFileInfo( filePath ).suffix().compare( QStringLiteral( "ply" ), Qt::CaseInsensitive ) != 0 )
    return false;

  QFile file( filePath );
  if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
    return false;

  bool hasCenter = false;
  bool hasScale = false;
  while ( !file.atEnd() )
  {
    const QString line = QString::fromLatin1( file.readLine() ).trimmed();
    if ( line == QStringLiteral( "end_header" ) )
      break;

    if ( line.startsWith( QStringLiteral( "comment denorm_center " ) ) ||
         line.startsWith( QStringLiteral( "comment center " ) ) )
    {
      const QStringList parts = line.split( ' ', Qt::SkipEmptyParts );
      if ( parts.size() >= 5 )
      {
        bool okX = false;
        bool okY = false;
        bool okZ = false;
        const double x = parts.at( 2 ).toDouble( &okX );
        const double y = parts.at( 3 ).toDouble( &okY );
        const double z = parts.at( 4 ).toDouble( &okZ );
        if ( okX && okY && okZ )
        {
          center = QVector3D( x, y, z );
          hasCenter = true;
        }
      }
    }
    else if ( line.startsWith( QStringLiteral( "comment denorm_scale " ) ) ||
              line.startsWith( QStringLiteral( "comment scale " ) ) )
    {
      const QStringList parts = line.split( ' ', Qt::SkipEmptyParts );
      if ( parts.size() >= 3 )
      {
        bool ok = false;
        const double s = parts.at( 2 ).toDouble( &ok );
        if ( ok && s > 1e-9 )
        {
          scale = s;
          hasScale = true;
        }
      }
    }
  }

  return hasCenter && hasScale;
}

static QString metadataRelativePathForPointCloud( const QString &filePath )
{
  QString normalized = QDir::fromNativeSeparators( QFileInfo( filePath ).absoluteFilePath() );
  const QString lower = normalized.toLower();
  QString rel;

  const QString previewMarker = QStringLiteral( "/ply_preview/datasets_aug/" );
  const int previewIdx = lower.indexOf( previewMarker );
  if ( previewIdx >= 0 )
    rel = normalized.mid( previewIdx + previewMarker.size() );

  const QString datasetMarker = QStringLiteral( "/datasets_aug/" );
  const int datasetIdx = lower.indexOf( datasetMarker );
  if ( rel.isEmpty() && datasetIdx >= 0 )
    rel = normalized.mid( datasetIdx + datasetMarker.size() );

  if ( rel.isEmpty() || rel.startsWith( QStringLiteral( "metadata/" ), Qt::CaseInsensitive ) )
    return QString();

  QFileInfo relInfo( rel );
  const QString dir = relInfo.path() == QStringLiteral( "." ) ? QString() : relInfo.path() + QStringLiteral( "/" );
  return dir + relInfo.completeBaseName() + QStringLiteral( ".txt" );
}

static bool metadataPointCloudInfoForInput( const QString &filePath,
                                            QVector3D *bboxMin,
                                            QVector3D *center,
                                            double *scale )
{
  const QString rel = metadataRelativePathForPointCloud( filePath );
  if ( !rel.isEmpty() )
  {
    QFile metadataFile( ParamModelerConfig::metadataJsonPath() );
    if ( metadataFile.open( QIODevice::ReadOnly ) )
    {
      QJsonParseError parseError;
      const QJsonDocument doc = QJsonDocument::fromJson( metadataFile.readAll(), &parseError );
      if ( parseError.error == QJsonParseError::NoError && doc.isArray() )
      {
        const QString relLower = rel.toLower();
        const QJsonArray records = doc.array();
        for ( const QJsonValue &value : records )
        {
          const QJsonObject obj = value.toObject();
          if ( obj.value( QStringLiteral( "file" ) ).toString().toLower() != relLower )
            continue;

          const QJsonObject info = obj.value( QStringLiteral( "pointCloudInfo" ) ).toObject();
          bool ok = true;
          if ( bboxMin )
            ok = vectorFromJsonArray( info.value( QStringLiteral( "bboxMin" ) ), *bboxMin ) && ok;
          if ( center )
            ok = vectorFromJsonArray( info.value( QStringLiteral( "center" ) ), *center ) && ok;
          if ( scale )
          {
            *scale = info.value( QStringLiteral( "scale" ) ).toDouble( 1.0 );
            ok = *scale > 1e-9 && ok;
          }
          if ( ok )
            return true;
        }
      }
    }
  }

  QVector3D plyCenter;
  double plyScale = 1.0;
  if ( denormInfoFromPlyComment( filePath, plyCenter, plyScale ) )
  {
    if ( bboxMin )
    {
      const PointCloud pc = PointCloudLoader::load( filePath );
      if ( pc.points.isEmpty() )
        return false;
      *bboxMin = pc.bboxMin * static_cast<float>( plyScale ) + plyCenter;
    }
    if ( center )
      *center = plyCenter;
    if ( scale )
      *scale = plyScale;
    return true;
  }

  // Fallback: compute directly from the point cloud itself (handles LAS/LAZ etc.)
  {
    const PointCloud pc = PointCloudLoader::load( filePath );
    if ( !pc.points.isEmpty() )
    {
      const QVector3D pcCenter = ( pc.bboxMin + pc.bboxMax ) * 0.5f;
      double maxRadius = 0.0;
      for ( const QVector3D &p : pc.points )
        maxRadius = qMax( maxRadius, static_cast<double>( ( p - pcCenter ).length() ) );
      if ( maxRadius < 1e-8 )
        maxRadius = 1.0;

      if ( bboxMin )
        *bboxMin = pc.bboxMin;
      if ( center )
        *center = pcCenter;
      if ( scale )
        *scale = maxRadius;
      return true;
    }
  }

  return false;
}

static QJsonObject currentPrimitiveParamsObject( const QString &prim, const ParamModelerDock *dock )
{
  QJsonObject params;
  if ( prim == "Cuboid" )
  {
    params["length"] = dock->cuboidLength();
    params["width"] = dock->cuboidWidth();
    params["height"] = dock->cuboidHeight();
  }
  else if ( prim == "Cylinder" )
  {
    params["radius"] = dock->cylinderRadius();
    params["height"] = dock->cylinderHeight();
  }
  else if ( prim == "LHouse" )
  {
    params["mainLength"] = dock->LMainLength();
    params["mainWidth"] = dock->LMainWidth();
    params["wingLength"] = dock->LWingLength();
    params["wingWidth"] = dock->LWingWidth();
    params["height"] = dock->LHeight();
  }
  else if ( prim == "ConeCylinder" )
  {
    params["radius"] = dock->coneCylRadius();
    const double cylH = dock->coneCylCylHeight();
    const double coneH = dock->coneCylConeHeight();
    const double totalH = cylH + coneH;
    params["totalHeight"] = totalH;
    params["cylinderRatio"] = totalH > 1e-6 ? cylH / totalH : 0.7;
  }
  else if ( prim == "GabledRoof" )
  {
    params["length"] = dock->gabledRoofLength();
    params["width"] = dock->gabledRoofWidth();
    const double wallH = dock->gabledRoofWallHeight();
    const double roofH = dock->gabledRoofRoofHeight();
    const double totalH = wallH + roofH;
    params["totalHeight"] = totalH;
    params["wallRatio"] = totalH > 1e-6 ? wallH / totalH : 0.7;
  }
  else if ( prim == "PyramidRoof" )
  {
    params["length"] = dock->pyramidLength();
    params["width"] = dock->pyramidWidth();
    const double wallH = dock->pyramidWallHeight();
    const double roofH = dock->pyramidRoofHeight();
    const double totalH = wallH + roofH;
    params["totalHeight"] = totalH;
    params["wallRatio"] = totalH > 1e-6 ? wallH / totalH : 0.7;
  }
  else if ( prim == "TruncatedPyramidRoof" )
  {
    params["bottomLength"] = dock->tpBottomLength();
    params["bottomWidth"] = dock->tpBottomWidth();
    params["topLength"] = dock->tpTopLength();
    params["topWidth"] = dock->tpTopWidth();
    const double wallH = dock->tpWallHeight();
    const double roofH = dock->tpRoofHeight();
    const double totalH = wallH + roofH;
    params["totalHeight"] = totalH;
    params["wallRatio"] = totalH > 1e-6 ? wallH / totalH : 0.7;
  }
  else if ( prim == "HalfCylinderRoof" )
  {
    params["length"] = dock->hcrLength();
    params["width"] = dock->hcrWidth();
    params["wallHeight"] = dock->hcrWallHeight();
    params["radius"] = dock->hcrRadius();
  }
  else if ( prim == "CylinderDome" || prim == "CylinderHemisphere" )
  {
    params["radius"] = dock->cylHemiRadius();
    const double cylH = dock->cylHemiHeight();
    const double domeH = dock->cylHemiDomeHeight();
    const double totalH = cylH + domeH;
    params["totalHeight"] = totalH;
    params["cylinderRatio"] = totalH > 1e-6 ? cylH / totalH : 0.7;
    params["bulge"] = dock->cylHemiBulge();
  }
  else if ( prim == "IndentedCuboid" )
  {
    params["outerLength"] = dock->icOuterLength();
    params["outerWidth"] = dock->icOuterWidth();
    params["outerHeight"] = dock->icOuterHeight();
    params["innerLength"] = dock->icInnerLength();
    params["innerWidth"] = dock->icInnerWidth();
    params["innerHeight"] = dock->icInnerHeight();
    params["offsetX"] = dock->icOffsetX();
    params["offsetY"] = dock->icOffsetY();
  }
  else if ( prim == "AsymmetricGableHouse" )
  {
    params["length"] = dock->aghLength();
    params["width"] = dock->aghWidth();
    const double wallH = dock->aghWallHeight();
    const double roofH = dock->aghRoofHeight();
    const double totalH = wallH + roofH;
    params["totalHeight"] = totalH;
    params["wallRatio"] = totalH > 1e-6 ? wallH / totalH : 0.7;
    params["ridgeLength"] = dock->aghRidgeLength();
    params["ridgeRatio"] = dock->aghRidgeRatio();
  }
  else if ( prim == "FourStageRoundTower" )
  {
    params["baseRadius"] = dock->ftBaseRadius();
    params["baseHeight"] = dock->ftBaseHeight();
    params["middleHeight"] = dock->ftMiddleHeight();
    params["middleTopRadius"] = dock->ftMiddleTopRadius();
    params["middleBulge"] = dock->ftMiddleBulge();
    params["coneHeight"] = dock->ftConeHeight();
  }
  else if ( prim == "TwoGableHouses" )
  {
    params["length1"] = dock->tgLength1();
    params["length2"] = dock->tgLength2();
    params["width"] = dock->tgWidth();
    const double wallH = dock->tgWallHeight();
    const double roofH = dock->tgRoofHeight();
    const double totalH = wallH + roofH;
    params["totalHeight"] = totalH;
    params["wallRatio"] = totalH > 1e-6 ? wallH / totalH : 0.7;
    params["angle"] = dock->tgAngle();
    params["ridgeRatio"] = dock->tgRidgeRatio();
  }
  params["rz"] = dock->poseRotateZ();
  return params;
}

void ParamModelerDock::onExportDLDatasetClicked()
{
  bool ok = false;
  const int samplesPerClass = QInputDialog::getInt(
    this, tr( "Batch dataset generation" ), tr( "Samples per primitive:" ), 50, 1, 10000, 1, &ok
  );
  if ( !ok )
    return;

  const QString selectedPath = QFileDialog::getExistingDirectory( this, tr( "Select PointNet dataset folder" ) );
  if ( selectedPath.isEmpty() )
    return;
  const QString rootPath = datasetRootFromSelectedFolder( selectedPath );

  const QMessageBox::StandardButton confirm = QMessageBox::question(
    this,
    tr( "Overwrite dataset?" ),
    tr( "This will regenerate:\n%1\n\nExisting train, val, test, and metadata folders will be overwritten. Continue?" ).arg( rootPath ),
    QMessageBox::Yes | QMessageBox::No,
    QMessageBox::No
  );
  if ( confirm != QMessageBox::Yes )
    return;

  const int pointCount = 2048;
  const QString originalPrimitive = ui->comboPrimitive->currentText();
  const bool previousUpdating = m_isUpdating;
  m_isUpdating = true;
  const QMap<QString, QVector<double>> savedPoses = m_poseMap;
  if ( m_previewTimer )
    m_previewTimer->stop();

  QStringList primitiveTypes;
  for ( int i = 0; i < ui->comboPrimitive->count(); ++i )
  {
    const QString prim = ui->comboPrimitive->itemText( i );
    if ( !prim.isEmpty() && !primitiveTypes.contains( prim ) && prim != "CylinderHemisphere" )
      primitiveTypes << prim;
  }

  QDir rootDir( rootPath );
  if ( !rootDir.exists() )
    QDir().mkpath( rootPath );
  for ( const QString &entry : QStringList { "train", "val", "test", "metadata" } )
  {
    QDir oldDir( rootDir.filePath( entry ) );
    if ( oldDir.exists() )
      oldDir.removeRecursively();
  }
  rootDir.mkpath( "train" );
  rootDir.mkpath( "val" );
  rootDir.mkpath( "test" );
  rootDir.mkpath( "metadata" );

  QFile classFile( rootDir.filePath( "metadata/class_names.txt" ) );
  if ( classFile.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    QTextStream out( &classFile );
    for ( const QString &prim : primitiveTypes )
      out << prim << "\n";
  }

  QJsonArray metadata;
  QProgressDialog progress( tr( "Generating dataset..." ), tr( "Cancel" ), 0, static_cast<int>( primitiveTypes.size() ) * samplesPerClass, this );
  progress.setWindowModality( Qt::WindowModal );

  int generated = 0;
  int failed = 0;
  for ( const QString &prim : primitiveTypes )
  {
    const QString classDir = safeClassDirName( prim );
    rootDir.mkpath( "train/" + classDir );
    rootDir.mkpath( "val/" + classDir );
    rootDir.mkpath( "test/" + classDir );

    ui->comboPrimitive->setCurrentText( prim );
    for ( int i = 0; i < samplesPerClass; ++i )
    {
      if ( progress.wasCanceled() )
        break;

      randomizeCurrentPrimitiveParams( false, true );
      const QString split = ( i < samplesPerClass * 8 / 10 )   ? "train"
                            : ( i < samplesPerClass * 9 / 10 ) ? "val"
                                                               : "test";
      const QString fileName = QString( "sample_%1.txt" ).arg( i + 1, 5, 10, QChar( '0' ) );
      const QString relativePath = split + "/" + classDir + "/" + fileName;
      const QString fullPath = rootDir.filePath( relativePath );

      DLPointCloudInfo pcInfo;
      if ( ExportPointCloud::exportDLInputTXT( fullPath, prim, this, pointCount, &pcInfo ) )
      {
        QJsonObject item;
        item["file"] = relativePath;
        item["type"] = prim;
        item["split"] = split;
        item["pointCount"] = pointCount;
        item["params"] = currentPrimitiveParamsObject( prim, this );
        item["pointCloudInfo"] = pointCloudInfoToJson( pcInfo );
        metadata.append( item );
        generated++;
      }
      else
      {
        failed++;
      }
      progress.setValue( generated + failed );
    }
    if ( progress.wasCanceled() )
      break;
  }

  QString metadataError;
  const QString metadataPath = rootDir.filePath( "metadata/sample_params.json" );
  const bool metadataOk = writeJsonDocumentChecked( metadataPath, QJsonDocument( metadata ), &metadataError );

  m_poseMap = savedPoses;
  ui->comboPrimitive->setCurrentText( originalPrimitive );
  m_isUpdating = previousUpdating;
  if ( m_previewTimer )
    m_previewTimer->stop();
  onUpdatePreview();

  if ( !metadataOk )
  {
    QMessageBox::critical(
      this,
      tr( "Metadata export failed" ),
      tr( "Dataset point clouds were generated, but sample_params.json was not written correctly.\n\n%1" )
        .arg( metadataError )
    );
    return;
  }

  QMessageBox::information(
    this,
    tr( "Done" ),
    tr( "Dataset generated.\nSuccess: %1\nFailed: %2\nMetadata records: %3\nOutput folder: %4" )
      .arg( generated )
      .arg( failed )
      .arg( metadata.size() )
      .arg( rootPath )
  );
}

void ParamModelerDock::onExportCurrentPrimitiveDLDatasetClicked()
{
  const QString prim = ui->comboPrimitive->currentText();
  if ( prim.isEmpty() )
    return;

  bool ok = false;
  const int samplesPerClass = QInputDialog::getInt(
    this,
    tr( "Generate Current Primitive Dataset" ),
    tr( "Samples for %1:" ).arg( prim ),
    260,
    1,
    10000,
    1,
    &ok
  );
  if ( !ok )
    return;

  const QString selectedPath = QFileDialog::getExistingDirectory( this, tr( "Select PointNet Folder" ) );
  if ( selectedPath.isEmpty() )
    return;
  const QString rootPath = datasetRootFromSelectedFolder( selectedPath );

  const QString classDir = safeClassDirName( prim );
  const int answer = QMessageBox::question(
    this,
    tr( "Replace Current Primitive" ),
    tr( "This will replace existing point-cloud files for %1 under train/val/test in:\n%2\n\nContinue?" )
      .arg( prim, rootPath ),
    QMessageBox::Yes | QMessageBox::No,
    QMessageBox::No
  );
  if ( answer != QMessageBox::Yes )
    return;

  const int pointCount = 2048;
  const QString originalPrimitive = ui->comboPrimitive->currentText();
  const bool previousUpdating = m_isUpdating;
  m_isUpdating = true;
  const QMap<QString, QVector<double>> savedPoses = m_poseMap;
  if ( m_previewTimer )
    m_previewTimer->stop();

  QDir rootDir( rootPath );
  if ( !rootDir.exists() )
    QDir().mkpath( rootPath );
  rootDir.mkpath( "train" );
  rootDir.mkpath( "val" );
  rootDir.mkpath( "test" );
  rootDir.mkpath( "metadata" );

  for ( const QString &split : QStringList { "train", "val", "test" } )
  {
    QDir classPath( rootDir.filePath( split + "/" + classDir ) );
    if ( classPath.exists() )
      classPath.removeRecursively();
    rootDir.mkpath( split + "/" + classDir );
  }

  ui->comboPrimitive->setCurrentText( prim );

  QJsonArray newRecords;
  QProgressDialog progress( tr( "Generating %1 dataset..." ).arg( prim ), tr( "Cancel" ), 0, samplesPerClass, this );
  progress.setWindowModality( Qt::WindowModal );

  int generated = 0;
  int failed = 0;
  for ( int i = 0; i < samplesPerClass; ++i )
  {
    if ( progress.wasCanceled() )
      break;

    randomizeCurrentPrimitiveParams( false, true );
    const QString split = ( i < samplesPerClass * 8 / 10 )   ? "train"
                          : ( i < samplesPerClass * 9 / 10 ) ? "val"
                                                             : "test";
    const QString fileName = QString( "sample_%1.txt" ).arg( i + 1, 5, 10, QChar( '0' ) );
    const QString relativePath = split + "/" + classDir + "/" + fileName;
    const QString fullPath = rootDir.filePath( relativePath );

    DLPointCloudInfo pcInfo;
    if ( ExportPointCloud::exportDLInputTXT( fullPath, prim, this, pointCount, &pcInfo ) )
    {
      QJsonObject item;
      item["file"] = relativePath;
      item["type"] = prim;
      item["split"] = split;
      item["pointCount"] = pointCount;
      item["params"] = currentPrimitiveParamsObject( prim, this );
      item["pointCloudInfo"] = pointCloudInfoToJson( pcInfo );
      newRecords.append( item );
      generated++;
    }
    else
    {
      failed++;
    }
    progress.setValue( generated + failed );
  }

  QString metadataError;
  const QString classMetadataPath = rootDir.filePath( "metadata/sample_params_" + classDir + ".json" );
  const bool classMetadataOk = writeJsonDocumentChecked( classMetadataPath, QJsonDocument( newRecords ), &metadataError );

  bool mergedMetadata = false;
  QString mergeMessage;
  const QString metadataPath = rootDir.filePath( "metadata/sample_params.json" );
  QFile existingMetadataFile( metadataPath );
  if ( existingMetadataFile.exists() && existingMetadataFile.open( QIODevice::ReadOnly ) )
  {
    QJsonParseError parseError;
    const QJsonDocument existingDoc = QJsonDocument::fromJson( existingMetadataFile.readAll(), &parseError );
    if ( parseError.error == QJsonParseError::NoError && existingDoc.isArray() )
    {
      QJsonArray merged;
      const QJsonArray oldRecords = existingDoc.array();
      for ( const QJsonValue &value : oldRecords )
      {
        const QJsonObject obj = value.toObject();
        if ( obj.value( QStringLiteral( "type" ) ).toString() != prim )
          merged.append( obj );
      }
      for ( const QJsonValue &value : newRecords )
        merged.append( value );

      QString mergeError;
      mergedMetadata = writeJsonDocumentChecked( metadataPath, QJsonDocument( merged ), &mergeError );
      if ( !mergedMetadata )
        mergeMessage = tr( "Main sample_params.json merge failed: %1" ).arg( mergeError );
    }
    else
    {
      mergeMessage = tr( "Main sample_params.json is not valid JSON, so it was not overwritten. Use the class metadata file instead." );
    }
  }
  else
  {
    QString mergeError;
    mergedMetadata = writeJsonDocumentChecked( metadataPath, QJsonDocument( newRecords ), &mergeError );
    if ( !mergedMetadata )
      mergeMessage = tr( "Main sample_params.json write failed: %1" ).arg( mergeError );
  }

  m_poseMap = savedPoses;
  ui->comboPrimitive->setCurrentText( originalPrimitive );
  m_isUpdating = previousUpdating;
  if ( m_previewTimer )
    m_previewTimer->stop();
  onUpdatePreview();

  if ( !classMetadataOk )
  {
    QMessageBox::critical(
      this,
      tr( "Metadata export failed" ),
      tr( "Current primitive point clouds were generated, but class metadata was not written correctly.\n\n%1" )
        .arg( metadataError )
    );
    return;
  }

  QString message = tr( "Current primitive dataset generated.\nPrimitive: %1\nSuccess: %2\nFailed: %3\nMetadata records: %4\nClass metadata: %5" )
                      .arg( prim )
                      .arg( generated )
                      .arg( failed )
                      .arg( newRecords.size() )
                      .arg( classMetadataPath );
  if ( mergedMetadata )
    message += tr( "\nMain metadata updated: %1" ).arg( metadataPath );
  else if ( !mergeMessage.isEmpty() )
    message += tr( "\n\n%1" ).arg( mergeMessage );

  QMessageBox::information( this, tr( "Done" ), message );
}

// ========================Export Mesh====================
void ParamModelerDock::onExportMeshClicked()
{
  DEBUG_LOG( L"[Export] STL Mesh export start\n" );

  QString fileName = QFileDialog::getSaveFileName(
    this, tr( "Save Mesh file" ), "", tr( "STL Files (*.stl)" )
  );
  if ( fileName.isEmpty() )
    return;

  QString prim = ui->comboPrimitive->currentText();

  MeshData mesh = BuildMesh::build( prim, this );
  if ( mesh.isEmpty() )
  {
    QMessageBox::warning( this, tr( "Export failed" ), tr( "Current model has no geometry data." ) );
    return;
  }

  QMatrix4x4 mat;
  mat.setToIdentity();
  mat.translate( poseTranslateX(), poseTranslateY(), poseTranslateZ() );
  mat.rotate( poseRotateX(), 1, 0, 0 );
  mat.rotate( poseRotateY(), 0, 1, 0 );
  mat.rotate( poseRotateZ(), 0, 0, 1 );

  QFile file( fileName );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
    return;
  QTextStream out( &file );
  out << "solid ParamModelerMesh\n";

  const int safeCount = ( mesh.indices.size() / 3 ) * 3;
  for ( int i = 0; i < safeCount; i += 3 )
  {
    QVector3D v1 = mat.map( mesh.vertices[mesh.indices[i]] );
    QVector3D v2 = mat.map( mesh.vertices[mesh.indices[i + 1]] );
    QVector3D v3 = mat.map( mesh.vertices[mesh.indices[i + 2]] );

    QVector3D normal = QVector3D::crossProduct( v2 - v1, v3 - v1 ).normalized();
    out << "  facet normal " << normal.x() << " " << normal.y() << " " << normal.z() << "\n";
    out << "    outer loop\n";
    out << "      vertex " << v1.x() << " " << v1.y() << " " << v1.z() << "\n";
    out << "      vertex " << v2.x() << " " << v2.y() << " " << v2.z() << "\n";
    out << "      vertex " << v3.x() << " " << v3.y() << " " << v3.z() << "\n";
    out << "    endloop\n";
    out << "  endfacet\n";
  }
  out << "endsolid ParamModelerMesh\n";
  file.close();
  QMessageBox::information( this, tr( "Export succeeded" ), tr( "Mesh has been exported as STL." ) );
}


void ParamModelerDock::onLoadToQGIS3D( bool zoomToLayer )
{
  if ( m_isUpdating )
    return;
  m_isUpdating = true;


  QString primitiveType = ui->comboPrimitive->currentText();
  DEBUG_LOG( QString( "[3D] load current model to QGIS 3D, primitive: %1\n" ).arg( primitiveType ).toStdWString().c_str() );
  MeshData mesh = BuildMesh::build( primitiveType, this );
  if ( mesh.isEmpty() )
  {
    DEBUG_LOG( L"[3D] model load failed, rolling back layer state\n" );
    if ( zoomToLayer )
      QMessageBox::warning( this, tr( "Load failed" ), tr( "Current parameters cannot generate a valid model." ) );
    m_isUpdating = false;
    return;
  }


  ParamModelerPose pose;
  pose.tx = poseTranslateX();
  pose.ty = poseTranslateY();
  pose.tz = poseTranslateZ();
  pose.rx = poseRotateX();
  pose.ry = poseRotateY();
  pose.rz = poseRotateZ();
  pose.scale = 1.0;  // model vertices are already in meters, no extra scaling needed

  if ( m_modelLayer )
  {
    QgsProject::instance()->removeMapLayer( m_modelLayer->id() );
    m_modelLayer = nullptr;
  }
  ParamModelerScene3D::removeLayerByName( QStringLiteral( "ParamModeler_Model" ) );
  ParamModelerScene3D::removeLayerByName( QStringLiteral( "ParamModeler_Model_Roof" ) );
  ParamModelerScene3D::removeLayerByName( QStringLiteral( "ParamModeler_Model_Edges" ) );
  for ( const QString &path : m_lastGpkgPath.split( '\n' ) )
  {
    const QString trimmed = path.trimmed();
    if ( !trimmed.isEmpty() )
      QFile::remove( trimmed );
  }
  m_lastGpkgPath.clear();

  QString errorMessage;
  // Keep the existing Qt3D preview entity alive so repeated loads and parameter
  // edits can update the same QBuffer instead of rebuilding the scene object.
  if ( !ParamModelerScene3D::updateRealtimePreviewMesh( mIface, mesh, pose, &errorMessage ) )
  {
    if ( zoomToLayer )
      QMessageBox::warning( this, tr( "Load failed" ), errorMessage );
    m_realtimeModelLoaded = false;
    m_isUpdating = false;
    return;
  }

  m_realtimeModelLoaded = true;

  if ( zoomToLayer )
    QMessageBox::information( this, tr( "Loaded to QGIS 3D" ), tr( "Realtime model loaded successfully.\nTriangles: %1" ).arg( mesh.indices.size() / 3 ) );

  m_isUpdating = false;
}


bool ParamModelerDock::loadPointCloudToQGIS3D( const QString &filePath, bool showMessage )
{
  if ( filePath.isEmpty() )
    return false;

  QFileInfo fi( filePath );
  QString layerName = QString( "External point cloud - %1" ).arg( fi.fileName() );
  QString suffix = fi.suffix().toLower();

  DEBUG_LOG( QString( "[PointCloud] load external point cloud: %1 (suffix: %2)\n" ).arg( filePath ).arg( suffix ).toStdWString().c_str() );

  QString displayPath = filePath;
  QTemporaryFile denormalizedFile( QDir::tempPath() + QStringLiteral( "/parammodeler_displaypc_XXXXXX.txt" ) );
  QVector3D metadataCenter;
  double metadataScale = 1.0;
  const bool hasMetadata = metadataPointCloudInfoForInput( filePath, nullptr, &metadataCenter, &metadataScale );
  if ( hasMetadata )
  {
    const PointCloud normalizedCloud = PointCloudLoader::load( filePath );
    if ( !normalizedCloud.points.isEmpty() && pointCloudLooksNormalizedForDisplay( normalizedCloud ) && denormalizedFile.open() )
    {
      QTextStream out( &denormalizedFile );
      for ( const QVector3D &p : normalizedCloud.points )
      {
        const QVector3D restored = p * static_cast<float>( metadataScale ) + metadataCenter;
        out << restored.x() << ' ' << restored.y() << ' ' << restored.z() << '\n';
      }
      out.flush();
      denormalizedFile.flush();
      displayPath = denormalizedFile.fileName();
      DEBUG_LOG( QString( "[PointCloud] display point cloud restored from DL normalization: %1\n" ).arg( displayPath ).toStdWString().c_str() );
    }
    else if ( !normalizedCloud.points.isEmpty() )
    {
      DEBUG_LOG( QString( "[PointCloud] metadata matched, input appears already in display scale: %1\n" ).arg( filePath ).toStdWString().c_str() );
    }
  }

  QString errorMessage;
  QgsMapLayer *loadedLayer = ParamModelerScene3D::loadExternalPointCloud(
    mIface, displayPath, layerName, this, &errorMessage
  );
  if ( !loadedLayer )
  {
    if ( showMessage )
      QMessageBox::warning( this, tr( "Load failed" ), errorMessage );
    return false;
  }

  if ( showMessage )
    QMessageBox::information( this, tr( "Load succeeded" ), tr( "Point cloud loaded successfully.\nLayer: %1\n\nYou can view and adjust it in the 3D scene." ).arg( layerName ) );
  DEBUG_LOG( QString( "[PointCloud] load success, layer: %1\n" ).arg( layerName ).toStdWString().c_str() );
  return true;
}


void ParamModelerDock::onLoadExternalPointCloud()
{
  QString filePath = QFileDialog::getOpenFileName(
    this, tr( "Select point cloud file" ), "", tr( "Point Cloud Files (*.ply *.las *.laz *.xyz *.txt *.pts)" )
  );

  if ( filePath.isEmpty() )
    return;

  loadPointCloudToQGIS3D( filePath, true );
}


void ParamModelerDock::schedulePreviewUpdate()
{
  m_previewUpdatePending = true;

  if ( m_previewUpdateInProgress )
    return;

  if ( m_previewTimer && !m_previewTimer->isActive() )
    m_previewTimer->start();
}


void ParamModelerDock::onUpdatePreview()
{
  m_previewUpdatePending = false;
  m_previewUpdateInProgress = true;

  QString prim = ui->comboPrimitive->currentText();
  MeshData mesh = BuildMesh::build( prim, this );
  if ( m_previewWidget && m_previewWidget->isVisible() )
    m_previewWidget->setMesh( mesh );


  if ( m_realtimeModelLoaded )
  {
    ParamModelerPose pose;
    pose.tx = poseTranslateX();
    pose.ty = poseTranslateY();
    pose.tz = poseTranslateZ();
    pose.rx = poseRotateX();
    pose.ry = poseRotateY();
    pose.rz = poseRotateZ();
    pose.scale = 1.0;  // model vertices are already in meters, no extra scaling needed

    QString errorMessage;
    if ( !ParamModelerScene3D::updateRealtimePreviewMesh( mIface, mesh, pose, &errorMessage ) )
      DEBUG_LOG( QString( "[3D] realtime Qt3D preview update failed: %1\n" ).arg( errorMessage ).toStdWString().c_str() );
  }

  m_previewUpdateInProgress = false;
  if ( m_previewUpdatePending && m_previewTimer && !m_previewTimer->isActive() )
    m_previewTimer->start();
}


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


double ParamModelerDock::cuboidLength() const { return ui->spinBoxCLength->value(); }
double ParamModelerDock::cuboidWidth() const { return ui->spinBoxCWidth->value(); }
double ParamModelerDock::cuboidHeight() const { return ui->spinBoxCHeight->value(); }
double ParamModelerDock::cylinderRadius() const { return ui->spinBoxCylRadius->value(); }
double ParamModelerDock::cylinderHeight() const { return ui->spinBoxCylHeight->value(); }
double ParamModelerDock::LMainLength() const { return ui->spinBoxLMainLength->value(); }
double ParamModelerDock::LMainWidth() const { return ui->spinBoxLMainWidth->value(); }
double ParamModelerDock::LWingLength() const { return ui->spinBoxLWingLength->value(); }
double ParamModelerDock::LWingWidth() const { return ui->spinBoxLWingWidth->value(); }
double ParamModelerDock::LHeight() const { return ui->spinBoxLHeight->value(); }
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
double ParamModelerDock::hcrLength() const { return ui->spinBoxHCRLength->value(); }
double ParamModelerDock::hcrWidth() const { return ui->spinBoxHCRWidth->value(); }
double ParamModelerDock::hcrWallHeight() const { return ui->spinBoxHCRHeightWall->value(); }
double ParamModelerDock::hcrRadius() const { return hcrWidth() / 2.0; }
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
double ParamModelerDock::ftBaseRadius() const { return ui->spinBoxFTBaseRadius->value(); }
double ParamModelerDock::ftBaseHeight() const { return ui->spinBoxFTBaseHeight->value(); }
double ParamModelerDock::ftMiddleHeight() const { return ui->spinBoxFTMiddleHeight->value(); }
double ParamModelerDock::ftMiddleTopRadius() const { return ui->spinBoxFTMiddleTopRadius->value(); }
double ParamModelerDock::ftMiddleBulge() const { return ui->spinBoxFTMiddleBulge->value(); }
double ParamModelerDock::ftConeHeight() const { return ui->spinBoxFTConeHeight->value(); }
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

// ========================================================================

// ========================================================================
void ParamModelerDock::onLoadInputData()
{
  QString filePath = QFileDialog::getOpenFileName(
    this, tr( "Load point cloud" ), "",
    tr( "Point cloud files (*.ply *.las *.laz *.xyz *.txt)" )
  );
  if ( filePath.isEmpty() )
    return;

  m_inputDataPath = filePath;
  QFileInfo fi( filePath );

  // 缓存元数据（center/scale），用于后续模型反归一化
  m_hasMetadata = metadataPointCloudInfoForInput( filePath, nullptr, &m_metadataCenter, &m_metadataScale );
  if ( m_hasMetadata )
  {
    DEBUG_LOG( QString( "[Meta] cached center=(%1,%2,%3) scale=%4\n" )
                 .arg( m_metadataCenter.x(), 0, 'f', 2 )
                 .arg( m_metadataCenter.y(), 0, 'f', 2 )
                 .arg( m_metadataCenter.z(), 0, 'f', 2 )
                 .arg( m_metadataScale, 0, 'f', 2 )
                 .toStdWString().c_str() );
  }

  DEBUG_LOG( QString( "[Tab2] load input data: %1\n" ).arg( filePath ).toStdWString().c_str() );

  ui->labelInputInfo->setText( tr( "Loaded: %1" ).arg( fi.fileName() ) );
  ui->labelPrimitiveType->setText( tr( "Result: -" ) );
  ui->tableInverseParams->setRowCount( 0 );


  ui->btnPointNetClassify->setEnabled( true );
  ui->btnInverseParams->setEnabled( false );
}

void ParamModelerDock::onPointNetClassify()
{
  if ( m_inputDataPath.isEmpty() )
    return;

  ui->progressInversion->setRange( 0, 0 );
  ui->progressInversion->setVisible( true );
  ui->labelPrimitiveType->setText( tr( "Classifying..." ) );
  ui->btnPointNetClassify->setEnabled( false );
  ui->btnInverseParams->setEnabled( false );
  QApplication::processEvents();

  DEBUG_LOG( QString( "[PointNet] predict input: %1\n" ).arg( m_inputDataPath ).toStdWString().c_str() );
  PointNetPredictResult result = PointNetRunner::predict( m_inputDataPath, 2048, 3 );
  ui->progressInversion->setRange( 0, 100 );
  ui->progressInversion->setValue( 100 );
  ui->progressInversion->setVisible( false );
  ui->btnPointNetClassify->setEnabled( true );

  if ( !result.errorMessage.isEmpty() )
  {
    QMessageBox::warning( this, tr( "Classification failed" ), result.errorMessage );
    return;
  }
  if ( result.predictions.isEmpty() )
  {
    QMessageBox::warning( this, tr( "Classification failed" ), tr( "No prediction returned." ) );
    return;
  }

  const PointNetPrediction top1 = result.predictions.first();
  ui->labelPrimitiveType->setText(
    tr( "Result: %1 (%2%)" )
      .arg( top1.className )
      .arg( top1.probability * 100.0, 0, 'f', 1 )
  );

  ui->comboPrimitive->setCurrentText( top1.className );
  ui->btnInverseParams->setEnabled( true );

  DEBUG_LOG( QString( "[PointNet] top1: %1, prob: %2\n" )
               .arg( top1.className )
               .arg( top1.probability )
               .toStdWString()
               .c_str() );
}
// ========================================================================

// ========================================================================
void ParamModelerDock::onInverseParams()
{
  if ( m_inputDataPath.isEmpty() )
    return;

  ui->progressInversion->setVisible( true );
  ui->progressInversion->setValue( 0 );

  QString prim = ui->comboPrimitive->currentText();

  DEBUG_LOG( QString( "[PointNet] parameter regression input: primitive: %1, file: %2\n" ).arg( prim ).arg( m_inputDataPath ).toStdWString().c_str() );
  const PointNetRegressionResult regression = PointNetRunner::predictParams( m_inputDataPath, prim, 2048 );
  if ( !regression.errorMessage.isEmpty() )
  {
    ui->progressInversion->setVisible( false );
    QMessageBox::warning( this, tr( "Parameter estimation failed" ), regression.errorMessage );
    return;
  }
  QMap<QString, double> params = pointNetParamsToUiParams( prim, regression.params );

  if ( !params.isEmpty() )
  {
    DEBUG_LOG( QString( "[PointNet] parameter regression done, returned %1 parameters\n" ).arg( params.size() ).toStdWString().c_str() );
    ParamInverter::applyToUI( this, params );

    ui->tableInverseParams->setRowCount( params.size() );
    int row = 0;
    for ( auto it = params.cbegin(); it != params.cend(); ++it, ++row )
    {
      ui->tableInverseParams->setItem( row, 0, new QTableWidgetItem( it.key() ) );
      ui->tableInverseParams->setItem( row, 1, new QTableWidgetItem( QString::number( it.value(), 'f', 2 ) ) );
    }
  }

  ui->progressInversion->setValue( 100 );
  ui->progressInversion->setVisible( false );

  // --- Auto-align model translation to point cloud center ---
  if ( m_hasMetadata )
  {
    const QString prim = ui->comboPrimitive->currentText();
    MeshData previewMesh = BuildMesh::build( prim, this );
    if ( !previewMesh.isEmpty() )
    {
      QVector3D modelMin( std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max() );
      QVector3D modelMax( std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::lowest() );
      for ( const QVector3D &v : previewMesh.vertices )
      {
        if ( v.x() < modelMin.x() ) modelMin.setX( v.x() );
        if ( v.y() < modelMin.y() ) modelMin.setY( v.y() );
        if ( v.z() < modelMin.z() ) modelMin.setZ( v.z() );
        if ( v.x() > modelMax.x() ) modelMax.setX( v.x() );
        if ( v.y() > modelMax.y() ) modelMax.setY( v.y() );
        if ( v.z() > modelMax.z() ) modelMax.setZ( v.z() );
      }
      const QVector3D modelCenter = ( modelMin + modelMax ) * 0.5f;

      const double tx = static_cast<double>( m_metadataCenter.x() ) - static_cast<double>( modelCenter.x() );
      const double ty = static_cast<double>( m_metadataCenter.y() ) - static_cast<double>( modelCenter.y() );
      const double tz = static_cast<double>( m_metadataCenter.z() ) - static_cast<double>( modelCenter.z() );
      setPoseTranslate( tx, ty, tz );

      DEBUG_LOG( QString( "[Align] pcCenter=(%1,%2,%3) modelCenter=(%4,%5,%6) → tx=%7 ty=%8 tz=%9\n" )
                   .arg( m_metadataCenter.x(), 0, 'f', 2 )
                   .arg( m_metadataCenter.y(), 0, 'f', 2 )
                   .arg( m_metadataCenter.z(), 0, 'f', 2 )
                   .arg( modelCenter.x(), 0, 'f', 2 )
                   .arg( modelCenter.y(), 0, 'f', 2 )
                   .arg( modelCenter.z(), 0, 'f', 2 )
                   .arg( tx, 0, 'f', 2 )
                   .arg( ty, 0, 'f', 2 )
                   .arg( tz, 0, 'f', 2 )
                   .toStdWString().c_str() );
    }
    else
    {
      DEBUG_LOG( L"[Align] regression mesh build failed, skipping auto-alignment\n" );
    }
  }
  else
  {
    DEBUG_LOG( QString( "[Align] no metadata for %1, skipping auto-alignment\n" )
                 .arg( m_inputDataPath ).toStdWString().c_str() );
  }

  onUpdatePreview();
}
