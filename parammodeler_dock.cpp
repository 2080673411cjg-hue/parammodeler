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
// 1. 本地项目自定义头文件 (核心业务逻辑)
#include "parammodeler_dock.h"
#include "ui_parammodeler_dock.h"
#include "previewglwidget.h"
#include "buildmesh.h"
#include "exportjson.h"
#include "exportobj.h"
#include "exportpointcloud.h"
#include "parammodeler_classify.h"
#include "parammodeler_inverse.h"
#include "parammodeler_scene3d.h"

// 2. Qt 核心与界面框架 (文件、内存、基本 UI)
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector3D>
#include <QMatrix4x4>
#include <QMessageBox>
#include <QFileDialog>
#include <QMenu>
#include <QAction>
#include <QVBoxLayout>
#include <QAbstractSpinBox>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

// 3. QGIS 基础与地理要素处理
#include <qgis.h>
#include <qgisinterface.h>
#include <QgsProject.h>
#include <QgsFeature.h>
#include <QgsGeometry.h>
#include <QgsPoint.h>
#include <QgsLineString.h>
#include <QgsPolygon.h>
#include <qgsmultipolygon.h>

// 4. QGIS 图层与渲染系统 (2D/3D)
#include <QgsVectorLayer.h>
#include <qgspointcloudlayer.h>       // 专门用于处理 LAS/LAZ 点云
#include <qgssymbol.h>               // 用于创建点/面符号
#include <QgsSingleSymbolRenderer.h>  // 用于设置图层渲染样式
#include <QgsPolygon3DSymbol.h>      // 用于 3D 材质属性
#include <QgsVectorLayer3DRenderer.h>// 3D 渲染器
#include <Qgs3DMapCanvas.h>          // 3D 画布交互
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
#include <windows.h>
#define DEBUG_LOG(msg) OutputDebugStringW(msg)

// ======================slider和spinBox的绑定函数：==================================
//根据倍率（乘数）同步数值，确保滑动条和数字输入框显示的参数一致
static void bindSliderSpin(QSlider* slider, QDoubleSpinBox* spin, double multiplier, double maxVal = 100.0, double minVal = 0.0)
{
    if (!slider || !spin) return;
    // 设置 SpinBox 的范围
    spin->setRange(minVal, maxVal);
    spin->setSingleStep(1.0 / multiplier);
    // 设置 Slider 范围以匹配倍率和最大值
    slider->setRange(static_cast<int>(minVal * multiplier), static_cast<int>(maxVal * multiplier));
    QObject::connect(slider, &QSlider::valueChanged, spin, [spin, multiplier](int v) {
        double val = static_cast<double>(v) / multiplier;
        if (std::abs(spin->value() - val) > 0.0001) {
            spin->setValue(val);
        }
    });
    QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), slider, [slider, multiplier](double v) {
        int val = static_cast<int>(v * multiplier);
        if (slider->value() != val) {
            slider->setValue(val);
        }
    });
}
// ===========================构造函数=================================
//绑定所有基元的UI控件、创建导出菜单、初始化OpenGL预览窗口，并设置1000ms的防抖定时器（m_previewTimer）以优化性能。
ParamModelerDock::ParamModelerDock( QgisInterface *iface, QWidget *parent )
  : QDockWidget( parent )// 调用父类构造函数
  , ui( new Ui::ParamModelerDock )// 创建UI对象
  , mIface( iface )// 保存QGIS接口指针
{
  ui->setupUi( this );// 初始化UI界面
  m_currentPrimitive = ui->comboPrimitive->currentText(); //初始化一下m_currentPrimitive
  setWindowTitle( tr( "Parametric Modeler" ) );// 设置窗口标题
		
  // slider和spinBox的每个基元的绑定
  bindSliderSpin( ui->sliderCLength, ui->spinBoxCLength, 100.0, 50.0 );  // 长方体
  bindSliderSpin( ui->sliderCWidth, ui->spinBoxCWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCHeight, ui->spinBoxCHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCylRadius, ui->spinBoxCylRadius, 100.0, 50.0 );  // 圆柱
  bindSliderSpin( ui->sliderCylHeight, ui->spinBoxCylHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLMainLength, ui->spinBoxLMainLength, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLMainWidth, ui->spinBoxLMainWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLWingLength, ui->spinBoxLWingLength, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLWingWidth, ui->spinBoxLWingWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLHeight, ui->spinBoxLHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderConeCylRadius, ui->spinBoxConeCylRadius, 100.0, 50.0 );  // 圆锥体 (ConeCylinder)
  bindSliderSpin( ui->sliderConeCylCylHeight, ui->spinBoxConeCylCylHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderConeCylConeHeight, ui->spinBoxConeCylConeHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderGRLength,      ui->spinBoxGRLength,100,50 );  // 人字形屋顶 (GabledRoof) 绑定
  bindSliderSpin( ui->sliderGRWidth, ui->spinBoxGRWidth, 100, 50 );
  bindSliderSpin( ui->sliderGRHeightWall, ui->spinBoxGRHeightWall, 100.0, 50.0 );
  bindSliderSpin( ui->sliderGRHeightRoof, ui->spinBoxGRHeightRoof, 100.0, 50.0 );
  bindSliderSpin( ui->sliderPRLength, ui->spinBoxPRLength, 100, 50 ); // --- PyramidRoof (金字塔房屋) 绑定 ---
  bindSliderSpin( ui->sliderPRWidth, ui->spinBoxPRWidth, 100, 50 );
	bindSliderSpin(ui->sliderPRHeightWall, ui->spinBoxPRHeightWall, 100.0, 50.0 );
	bindSliderSpin(ui->sliderPRHeightRoof, ui->spinBoxPRHeightRoof, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTPRBottomLength, ui->spinBoxTPRBottomLength, 100.0, 50.0 );		// --- TPRoof (棱台房屋) 绑定 ---
  bindSliderSpin( ui->sliderTPRBottomWidth, ui->spinBoxTPRBottomWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTPRTopLength, ui->spinBoxTPRTopLength, 100, 50 );
  bindSliderSpin( ui->sliderTPRTopWidth, ui->spinBoxTPRTopWidth, 100, 50 );
  bindSliderSpin( ui->sliderTPRHeightWall, ui->spinBoxTPRHeightWall, 100, 50 );
  bindSliderSpin( ui->sliderTPRHeightRoof, ui->spinBoxTPRHeightRoof, 100, 50 );
  bindSliderSpin( ui->sliderHCRLength, ui->spinBoxHCRLength, 100, 50 ); // --- HalfCylinderRoof (半圆柱屋顶) 绑定 ---
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
  connect( ui->spinBoxHCRWidth, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
           this, [syncHCRRadius]( double ) { syncHCRRadius(); } );
  bindSliderSpin( ui->sliderICLength, ui->spinBoxICLength, 100, 50 ); // --- IndentedCuboid (凹陷长方体) 绑定 ---
  bindSliderSpin( ui->sliderICWidth, ui->spinBoxICWidth, 100, 50 );
  bindSliderSpin( ui->sliderICHeight, ui->spinBoxICHeight, 100, 50 );
  bindSliderSpin( ui->sliderICInnerLength, ui->spinBoxICInnerLength, 100, 50 );
  bindSliderSpin( ui->sliderICInnerWidth, ui->spinBoxICInnerWidth, 100, 50 );
  bindSliderSpin( ui->sliderICInnerHeight, ui->spinBoxICInnerHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderICOffsetX, ui->spinBoxICOffsetX, 100, 50 );
  bindSliderSpin( ui->sliderICOffsetY, ui->spinBoxICOffsetY, 100, 50 );
  bindSliderSpin( ui->sliderAGHLength, ui->spinBoxAGHLength, 100, 50 ); // --- AsymmetricGableHouse (非对称人字形屋顶房屋) 绑定 ---
  bindSliderSpin( ui->sliderAGHWidth, ui->spinBoxAGHWidth, 100, 50 );
  bindSliderSpin( ui->sliderAGHHeightWall, ui->spinBoxAGHHeightWall, 100, 50 );
  bindSliderSpin( ui->sliderAGHRoofHeight, ui->spinBoxAGHRoofHeight, 100, 50 );
  bindSliderSpin( ui->sliderAGHRidgeLength, ui->spinBoxAGHRidgeLength, 100.0, 50.0 );
  bindSliderSpin( ui->sliderAGHRidgeOffset, ui->spinBoxAGHRidgeOffset, 100.0, 0.8, 0.2 );
  bindSliderSpin( ui->sliderCylHemiRadius, ui->spinBoxCylHemiRadius, 100, 50 ); // --- CylinderDome (圆柱穹顶) 绑定 ---
  bindSliderSpin( ui->sliderCylHemiHeight, ui->spinBoxCylHemiHeight, 100, 50 );
  bindSliderSpin( ui->sliderCylHemiDomeHeight, ui->spinBoxCylHemiDomeHeight, 100, 50 );
  bindSliderSpin( ui->sliderCylHemiBulge, ui->spinBoxCylHemiBulge, 100.0, 1.0 );
  bindSliderSpin( ui->sliderFTBaseRadius, ui->spinBoxFTBaseRadius, 100.0, 50.0 ); // --- FourStageRoundTower (四段式圆塔形) 绑定 ---
  bindSliderSpin( ui->sliderFTBaseHeight, ui->spinBoxFTBaseHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderFTMiddleHeight, ui->spinBoxFTMiddleHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderFTMiddleTopRadius, ui->spinBoxFTMiddleTopRadius, 100.0, 50.0 );
  bindSliderSpin( ui->sliderFTMiddleBulge, ui->spinBoxFTMiddleBulge, 100.0, 0.6 );
  bindSliderSpin( ui->sliderFTConeHeight, ui->spinBoxFTConeHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGLength1, ui->spinBoxTGLength1, 100.0, 50.0 ); // --- TwoGableHouses (双人字屋顶房屋) 绑定 ---
  bindSliderSpin( ui->sliderTGLength2, ui->spinBoxTGLength2, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGWidth, ui->spinBoxTGWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGHeightWall, ui->spinBoxTGHeightWall, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGRoofHeight, ui->spinBoxTGRoofHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGAngle,      ui->spinBoxTGAngle,      10.0, 180.0, 135.0 );
  bindSliderSpin( ui->sliderTGRidgeRatio, ui->spinBoxTGRidgeRatio, 100.0, 0.8, 0.2 );
	//位姿旋转三参数的输入绑定
	bindSliderSpin(ui->sliderROmega, ui->spinBoxROmega, 10.0, 180.0, -180.0);
	bindSliderSpin(ui->sliderRPhi,   ui->spinBoxRPhi,   10.0, 180.0, -180.0);
	bindSliderSpin(ui->sliderRKappa, ui->spinBoxRKappa, 10.0, 180.0, -180.0);
  // ====================== 创建导出菜单 ======================
		// 换成直接连接 UI 文件里定义好的 action
		connect( ui->actOBJ,    &QAction::triggered, this, &ParamModelerDock::onExportOBJClicked );
		connect( ui->actJSON,   &QAction::triggered, this, &ParamModelerDock::onExportJSONClicked );
		connect( ui->actPLY,    &QAction::triggered, this, &ParamModelerDock::onExportPLYClicked );
		connect( ui->actDLPointCloud, &QAction::triggered, this, &ParamModelerDock::onExportDLPointCloudClicked );
		connect( ui->actMesh,   &QAction::triggered, this, &ParamModelerDock::onExportMeshClicked );
		connect( ui->actTo3D,   &QAction::triggered, this, &ParamModelerDock::onLoadToQGIS3D );
		connect( ui->actLoadPC, &QAction::triggered, this, &ParamModelerDock::onLoadExternalPointCloud );

  // 预览 Widget 初始化
  m_previewWidget = ui->previewWidget;
		ui->checkBoxAutoSync->setChecked(false); //设置实时同步默认不开启
  // 防抖 Timer：slider 停止拖动 1000ms 后触发刷新
  m_previewTimer = new QTimer( this );
  m_previewTimer->setSingleShot( true );
  m_previewTimer->setInterval( 1000 );
  connect( m_previewTimer, &QTimer::timeout, this, &ParamModelerDock::onUpdatePreview );
  // ---- 连接所有 slider 的 valueChanged → 启动防抖 Timer ----
  auto schedulePreview = [this]( int ) { m_previewTimer->start(); };
	//旋转三参数，左侧预览图就刷新，需要连接信号
  auto schedulePreviewD = [this]( double ) { m_previewTimer->start(); };
  connect( ui->spinBoxROmega, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, schedulePreviewD );
  connect( ui->spinBoxRPhi, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, schedulePreviewD );
  connect( ui->spinBoxRKappa, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, schedulePreviewD );
  connect( ui->sliderCLength,  &QSlider::valueChanged, this, schedulePreview );  // 长方体
  connect( ui->sliderCWidth,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylRadius, &QSlider::valueChanged, this, schedulePreview );  // 圆柱
  connect( ui->sliderCylHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLMainLength,  &QSlider::valueChanged, this, schedulePreview );  // L型房子
  connect( ui->sliderLMainWidth,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLWingLength,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLWingWidth,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLHeight,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderConeCylRadius,    &QSlider::valueChanged, this, schedulePreview ); // 圆锥圆柱
  connect( ui->sliderConeCylCylHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderConeCylConeHeight,&QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRLength,      &QSlider::valueChanged, this, schedulePreview ); // 人字形屋顶
  connect( ui->sliderGRWidth,      &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRHeightRoof, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderPRLength,      &QSlider::valueChanged, this, schedulePreview );  // 金字塔屋顶
  connect( ui->sliderPRWidth,      &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderPRHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderPRHeightRoof, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRBottomLength, &QSlider::valueChanged, this, schedulePreview );  // 棱台屋顶
  connect( ui->sliderTPRBottomWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRTopLength,    &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRTopWidth,    &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRHeightWall,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRHeightRoof,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderHCRLength,      &QSlider::valueChanged, this, schedulePreview );  // 半圆柱屋顶
  connect( ui->sliderHCRWidth,      &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderHCRHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHemiRadius,    &QSlider::valueChanged, this, schedulePreview );  // 圆柱穹顶
  connect( ui->sliderCylHemiHeight,    &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHemiDomeHeight,&QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHemiBulge,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICLength,       &QSlider::valueChanged, this, schedulePreview );  // 凹陷长方体
  connect( ui->sliderICWidth,       &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICHeight,      &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICInnerLength,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICInnerWidth,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICInnerHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICOffsetX,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICOffsetY,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHLength,       &QSlider::valueChanged, this, schedulePreview );  // 非对称人字形屋顶
  connect( ui->sliderAGHWidth,       &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHHeightWall,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHRoofHeight,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHRidgeLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHRidgeOffset, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderFTBaseRadius,     &QSlider::valueChanged, this, schedulePreview );  // 四段式圆塔形
  connect( ui->sliderFTBaseHeight,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderFTMiddleHeight,   &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderFTMiddleTopRadius,&QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderFTMiddleBulge,    &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderFTConeHeight,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGLength1,     &QSlider::valueChanged, this, schedulePreview );  // 双人字屋顶房屋
  connect( ui->sliderTGLength2,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGWidth,      &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGRoofHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGAngle,      &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGRidgeRatio, &QSlider::valueChanged, this, schedulePreview );
  // 切换基元时切换参数页面
  connect( ui->comboPrimitive, &QComboBox::currentTextChanged, this, &ParamModelerDock::onPrimitiveChanged );
  // 切换基元时立刻刷新预览
  connect( ui->comboPrimitive, &QComboBox::currentTextChanged, this, [this]( const QString & ) { onUpdatePreview(); } );
  connect( ui->btnRandomParams, &QPushButton::clicked, this, &ParamModelerDock::onRandomizeCurrentPrimitive );
		// ====================== 点云反演折叠面板 ======================
		ui->frameInversion->setVisible( false );  // 默认收起

		connect( ui->btnToggleInversion, &QPushButton::toggled, this, [this]( bool checked ) {
						ui->frameInversion->setVisible( checked );
						ui->btnToggleInversion->setText( checked ? "▼ 点云反演" : "▶ 点云反演" );
		} );

		connect( ui->btnLoadPointCloud, &QPushButton::clicked,  this, &ParamModelerDock::onLoadInputData );
		connect( ui->btnClassifyPrimitive,       &QPushButton::clicked,  this, &ParamModelerDock::onClassifyPrimitive );
		connect( ui->btnInverseParams,        &QPushButton::clicked,  this, &ParamModelerDock::onInverseParams );

		// 反演按钮初始禁用，加载数据后才启用
		ui->btnClassifyPrimitive->setEnabled( false );
		ui->btnInverseParams->setEnabled(  false );

	DEBUG_LOG( L"\n[ParamModelerDock] 初始化完成，当前基元: " );
	DEBUG_LOG( m_currentPrimitive.toStdWString().c_str() );
	DEBUG_LOG( L"\n" );
}
// ===========================析构函数=================================
ParamModelerDock::~ParamModelerDock()
{
	 m_modelLayer = nullptr; // 不 delete，图层归 QgsProject 所有
  delete ui;
}
// ==========================切换参数页基元函数================================
void ParamModelerDock::onPrimitiveChanged( const QString &prim )
{
  // 调试：记录基元切换
  QString dbg = QString( "[ParamModeler] 切换基元: %1 → %2\n" ).arg( m_currentPrimitive ).arg( prim );
  DEBUG_LOG( dbg.toStdWString().c_str() );

  // 1. 离开前保存旧基元的位姿
  if ( !m_currentPrimitive.isEmpty() )
  {
    m_poseMap[m_currentPrimitive] = {
      ui->lineEditTX->text().toDouble(),
      ui->lineEditTY->text().toDouble(),
      ui->lineEditTZ->text().toDouble(),
      ui->spinBoxROmega->value(),
      ui->spinBoxRPhi->value(),
      ui->spinBoxRKappa->value()
    };
  }

  // 2. QHash 替代 if-else 链，新增基元只需在此加一行
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

  // 3. 恢复新基元的位姿，没有记录就清零
  if ( m_poseMap.contains( prim ) )
  {
    const auto &p = m_poseMap[prim];
    ui->lineEditTX->setText( QString::number( p[0] ) );
    ui->lineEditTY->setText( QString::number( p[1] ) );
    ui->lineEditTZ->setText( QString::number( p[2] ) );
    ui->spinBoxROmega->setValue( p[3] );
    ui->spinBoxRPhi->setValue( p[4] );
    ui->spinBoxRKappa->setValue( p[5] );
  }
  else
  {
    ui->lineEditTX->setText( "0" );
    ui->lineEditTY->setText( "0" );
    ui->lineEditTZ->setText( "0" );
    ui->spinBoxROmega->setValue( 0 );
    ui->spinBoxRPhi->setValue( 0 );
    ui->spinBoxRKappa->setValue( 0 );
  }

  m_currentPrimitive = prim;

  // 调试：输出当前位姿
  QString poseDbg = QString( "[ParamModeler] 位姿: tx=%1 ty=%2 tz=%3 omega=%4 phi=%5 kappa=%6\n" )
    .arg( poseTranslateX(), 0, 'f', 2 )
    .arg( poseTranslateY(), 0, 'f', 2 )
    .arg( poseTranslateZ(), 0, 'f', 2 )
    .arg( poseRotateX(), 0, 'f', 2 )
    .arg( poseRotateY(), 0, 'f', 2 )
    .arg( poseRotateZ(), 0, 'f', 2 );
  DEBUG_LOG( poseDbg.toStdWString().c_str() );
}
// =======================导出前检验===================
// 在导出前检查当前参数是否能生成有效的几何网格
// ======================= Random Parameters ===================
void ParamModelerDock::onRandomizeCurrentPrimitive()
{
  auto rnd = []( double minVal, double maxVal, double step = 0.1 ) {
    const double raw = minVal + QRandomGenerator::global()->generateDouble() * ( maxVal - minVal );
    return std::round( raw / step ) * step;
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
    set( ui->spinBoxLMainLength, mainLength );
    set( ui->spinBoxLMainWidth, mainWidth );
    set( ui->spinBoxLWingLength, rnd( mainLength * 0.35, mainLength * 0.75 ) );
    set( ui->spinBoxLWingWidth, rnd( mainWidth * 0.35, mainWidth * 0.75 ) );
    set( ui->spinBoxLHeight, rnd( 2.5, 7.0 ) );
  }
  else if ( prim == "ConeCylinder" )
  {
    set( ui->spinBoxConeCylRadius, rnd( 2.0, 8.0 ) );
    set( ui->spinBoxConeCylCylHeight, rnd( 2.5, 9.0 ) );
    set( ui->spinBoxConeCylConeHeight, rnd( 1.5, 6.0 ) );
  }
  else if ( prim == "GabledRoof" )
  {
    set( ui->spinBoxGRLength, rnd( 8.0, 24.0 ) );
    set( ui->spinBoxGRWidth, rnd( 5.0, 14.0 ) );
    set( ui->spinBoxGRHeightWall, rnd( 2.5, 7.0 ) );
    set( ui->spinBoxGRHeightRoof, rnd( 1.2, 5.0 ) );
  }
  else if ( prim == "PyramidRoof" )
  {
    set( ui->spinBoxPRLength, rnd( 7.0, 20.0 ) );
    set( ui->spinBoxPRWidth, rnd( 5.0, 16.0 ) );
    set( ui->spinBoxPRHeightWall, rnd( 2.5, 7.0 ) );
    set( ui->spinBoxPRHeightRoof, rnd( 1.5, 6.0 ) );
  }
  else if ( prim == "TruncatedPyramidRoof" )
  {
    const double bottomLength = rnd( 10.0, 24.0 );
    const double bottomWidth = rnd( 8.0, 18.0 );
    set( ui->spinBoxTPRBottomLength, bottomLength );
    set( ui->spinBoxTPRBottomWidth, bottomWidth );
    set( ui->spinBoxTPRTopLength, rnd( bottomLength * 0.35, bottomLength * 0.80 ) );
    set( ui->spinBoxTPRTopWidth, rnd( bottomWidth * 0.35, bottomWidth * 0.80 ) );
    set( ui->spinBoxTPRHeightWall, rnd( 2.5, 7.0 ) );
    set( ui->spinBoxTPRHeightRoof, rnd( 1.2, 5.0 ) );
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
    set( ui->spinBoxCylHemiHeight, rnd( 2.5, 9.0 ) );
    set( ui->spinBoxCylHemiDomeHeight, rnd( 1.0, 5.0 ) );
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
    set( ui->spinBoxICOffsetX, rnd( 0.0, outerLength - innerLength ) );
    set( ui->spinBoxICOffsetY, rnd( 0.0, outerWidth - innerWidth ) );
  }
  else if ( prim == "AsymmetricGableHouse" )
  {
    const double length = rnd( 10.0, 24.0 );
    const double width = rnd( 5.0, 14.0 );
    set( ui->spinBoxAGHLength, length );
    set( ui->spinBoxAGHWidth, width );
    set( ui->spinBoxAGHHeightWall, rnd( 2.5, 7.0 ) );
    set( ui->spinBoxAGHRoofHeight, rnd( 1.2, 5.0 ) );
    set( ui->spinBoxAGHRidgeLength, rnd( length * 0.40, length * 0.90 ) );
    set( ui->spinBoxAGHRidgeOffset, rnd( 0.3, 0.7, 0.01 ) );
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
    set( ui->spinBoxTGHeightWall, rnd( 2.5, 7.0 ) );
    set( ui->spinBoxTGRoofHeight, rnd( 1.2, 4.5 ) );
    set( ui->spinBoxTGAngle, rnd( 135.0, 180.0, 1.0 ) );
    set( ui->spinBoxTGRidgeRatio, rnd( 0.3, 0.7, 0.01 ) );
  }

  onUpdatePreview();
}

// ======================= Export Validation ===================
static bool checkMeshValid( const QString &primitiveType, ParamModelerDock *dock )
{
    MeshData mesh = BuildMesh::build( primitiveType, dock );
    if ( mesh.isEmpty() )
    {
        QMessageBox::warning( dock, QObject::tr( "无法导出" ),
            QObject::tr( "当前参数无法生成有效模型，请先调整参数直到预览区域显示出模型再导出。" ) );
        return false;
    }
    return true;
}
// ========================导出 OBJ====================
void ParamModelerDock::onExportOBJClicked()
{
    QString primitiveType = ui->comboPrimitive->currentText();
    DEBUG_LOG( QString( "[Export] OBJ 导出开始, 基元: %1\n" ).arg( primitiveType ).toStdWString().c_str() );
    if ( !checkMeshValid( primitiveType, this ) ) return;
    // 1. 选择保存路径
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("保存 OBJ 文件"),
        "",
        tr("OBJ Files (*.obj)")
    );
    if (fileName.isEmpty())
        return;
    // 2. 调用模块化 OBJ 导出
    bool ok = ExportOBJ::exportOBJ(fileName, primitiveType, this);
    DEBUG_LOG( QString( "[Export] OBJ 导出%1: %2\n" ).arg( ok ? "成功" : "失败" ).arg( fileName ).toStdWString().c_str() );
    // 3. 提示信息
    if (ok)
    {
        QMessageBox::information(
            this,
            tr("导出成功"),
            tr("OBJ 文件已保存到：\n%1").arg(fileName)
        );
    }
    else
    {
        QMessageBox::critical(
            this,
            tr("导出失败"),
            tr("OBJ 文件未能成功导出，请检查参数或路径。")
        );
    }
}
// ========================导出 JSON===================
void ParamModelerDock::onExportJSONClicked()
{
    QString primitiveType = ui->comboPrimitive->currentText();
    DEBUG_LOG( QString( "[Export] JSON 导出开始, 基元: %1\n" ).arg( primitiveType ).toStdWString().c_str() );
    if ( !checkMeshValid( primitiveType, this ) ) return;
    ExportJSON::writeJSON(this);
    DEBUG_LOG( L"[Export] JSON 导出完成\n" );
}
// ========================导出点云====================
void ParamModelerDock::onExportPLYClicked()
{
    QString primitiveType = ui->comboPrimitive->currentText();
    DEBUG_LOG( QString( "[Export] PLY 导出开始, 基元: %1\n" ).arg( primitiveType ).toStdWString().c_str() );
    if ( !checkMeshValid( primitiveType, this ) ) return;

    QString fileName = QFileDialog::getSaveFileName(
        this, tr("保存点云文件"), "", tr("PLY Files (*.ply)") );
    if ( fileName.isEmpty() ) return;

    ExportPointCloud::exportPLY( fileName, primitiveType, this );
}
// ========================导出深度学习点云====================
void ParamModelerDock::onExportDLPointCloudClicked()
{
    QString primitiveType = ui->comboPrimitive->currentText();
    DEBUG_LOG( QString( "[Export] DL TXT export start, primitive: %1\n" ).arg( primitiveType ).toStdWString().c_str() );
    if ( !checkMeshValid( primitiveType, this ) ) return;

    QString fileName = QFileDialog::getSaveFileName(
        this, tr("保存深度学习输入点云"), "", tr("TXT Files (*.txt)") );
    if ( fileName.isEmpty() ) return;

    bool ok = ExportPointCloud::exportDLInputTXT( fileName, primitiveType, this, 2048 );
    if ( ok )
    {
        QMessageBox::information(
            this,
            tr("导出成功"),
            tr("深度学习输入点云已保存到:\n%1").arg( fileName )
        );
    }
}
// ========================导出 Mesh====================
void ParamModelerDock::onExportMeshClicked()
{
    DEBUG_LOG( L"[Export] STL Mesh 导出开始\n" );
    // 1. 获取保存路径
    QString fileName = QFileDialog::getSaveFileName(
        this, tr("保存 Mesh 文件"), "", tr("STL Files (*.stl)") );
    if ( fileName.isEmpty() ) return;
    // 2. 使用已有的 BuildMesh 获取当前生成的网格数据
    QString prim = ui->comboPrimitive->currentText();
    // 这里完全复用了核心生成逻辑
    MeshData mesh = BuildMesh::build(prim, this); 
    if (mesh.isEmpty()) {
        QMessageBox::warning(this, tr("导出失败"), tr("当前模型没有几何数据。"));
        return;
    }
    // ★ 补上位姿变换，和 onLoadToQGIS3D 保持一致
    QMatrix4x4 mat;
    mat.setToIdentity();
    mat.translate( poseTranslateX(), poseTranslateY(), poseTranslateZ() );
    mat.rotate( poseRotateX(), 1, 0, 0 ); // ω
    mat.rotate( poseRotateY(), 0, 1, 0 ); // φ
    mat.rotate( poseRotateZ(), 0, 0, 1 ); // κ
    // 3. 写入 STL 格式 (标准的 Mesh 文件格式)
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    out << "solid ParamModelerMesh\n";
    // 利用你 MeshData 中的索引进行面片遍历
    const int safeCount = ( mesh.indices.size() / 3 ) * 3; // 同时修复 Fix2 的越界
    for ( int i = 0; i < safeCount; i += 3 )
    {
        QVector3D v1 = mat.map( mesh.vertices[mesh.indices[i]] );     
        QVector3D v2 = mat.map( mesh.vertices[mesh.indices[i + 1]] );
        QVector3D v3 = mat.map( mesh.vertices[mesh.indices[i + 2]] ); 
        // 计算法线以保证 3D 软件正常显示
        QVector3D normal = QVector3D::crossProduct(v2 - v1, v3 - v1).normalized();
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
    QMessageBox::information(this, tr("导出成功"), tr("Mesh 已导出为 STL。"));
}

// ================将模型加载同步到QGIS 3D视图=========
// 每次写入临时 GeoPackage 文件再加载新图层，强制 QGIS 完全重建 3D 渲染实体，
// 避免 Qt3D 包围盒缓存导致的面丢失（面缺失）问题。
// 使用独立文件名避免文件缓存，并显式刷新 3D 画布保证同步更新。
void ParamModelerDock::onLoadToQGIS3D( bool zoomToLayer )
{
  if ( m_isUpdating )
    return;
  m_isUpdating = true;

  // 1. 构建新网格
  QString primitiveType = ui->comboPrimitive->currentText();
  DEBUG_LOG( QString( "[3D] 加载模型到 QGIS 3D, 基元: %1\n" ).arg( primitiveType ).toStdWString().c_str() );
  MeshData mesh = BuildMesh::build( primitiveType, this );
  if ( mesh.isEmpty() )
  {
    DEBUG_LOG( L"[3D] 网格生成失败\n" );
    if ( zoomToLayer )
      QMessageBox::warning( this, tr( "错误" ), tr( "无法生成模型，请检查参数" ) );
    m_isUpdating = false;
    return;
  }

  // 2. 应用位姿变换
  ParamModelerPose pose;
  pose.tx = poseTranslateX();
  pose.ty = poseTranslateY();
  pose.tz = poseTranslateZ();
  pose.rx = poseRotateX();
  pose.ry = poseRotateY();
  pose.rz = poseRotateZ();

  ParamModelerModelLoadResult loadResult = ParamModelerScene3D::loadModelMesh(
    mIface, mesh, pose, m_modelLayer, m_lastGpkgPath, zoomToLayer, this );

  if ( !loadResult.layer )
  {
    if ( zoomToLayer )
      QMessageBox::warning( this, tr( "错误" ), loadResult.errorMessage );
    m_isUpdating = false;
    return;
  }

  m_modelLayer = loadResult.layer;
  m_lastGpkgPath = loadResult.gpkgPath;
  connect( m_modelLayer, &QgsMapLayer::willBeDeleted, this, [this]() {
    m_modelLayer = nullptr;
  } );

  if ( zoomToLayer )
    QMessageBox::information( this, tr( "加载成功" ), tr( "模型已加载！\n三角面数：%1" ).arg( loadResult.triangleCount ) );

  m_isUpdating = false;
}
// =================将外部点云加载到QGIS 3D视图======================================
//针对 LAS/LAZ 使用了 BFS 广度优先搜索遍历八叉树索引，将海量点云高效转化为 QGIS 的 3D 点符号图层，以便与参数化模型进行重叠对比。
void ParamModelerDock::onLoadExternalPointCloud()
{
  QString filePath = QFileDialog::getOpenFileName(
    this, tr( "选择点云文件" ), "", tr( "点云文件 (*.ply *.las *.laz)" )
  );

  if ( filePath.isEmpty() )
    return;

  QFileInfo fi( filePath );//提取文件名
  QString layerName = QString( "外部点云 - %1" ).arg( fi.fileName() );
  QString suffix = fi.suffix().toLower();

  DEBUG_LOG( QString( "[PointCloud] 加载外部点云: %1 (格式: %2)\n" ).arg( filePath ).arg( suffix ).toStdWString().c_str() );

  QString errorMessage;
  QgsMapLayer *loadedLayer = ParamModelerScene3D::loadExternalPointCloud(
    mIface, filePath, layerName, this, &errorMessage );
  if ( !loadedLayer )
  {
    QMessageBox::warning( this, tr( "加载失败" ), errorMessage );
    return;
  }

  QMessageBox::information( this, tr( "加载成功" ), tr( "点云已加载！\n图层：%1\n\n可在3D视图中与模型叠加对比。" ).arg( layerName ) );
  DEBUG_LOG( QString( "[PointCloud] load success, layer: %1\n" ).arg( layerName ).toStdWString().c_str() );
}
// ===============================主刷新函数=============================
// 根据当前参数重建网格，并根据开关决定是否同步到QGIS
void ParamModelerDock::onUpdatePreview()
{
    if ( !m_previewWidget ) return;

    // 1. 刷新左侧 OpenGL 预览
    QString prim = ui->comboPrimitive->currentText();
    MeshData mesh = BuildMesh::build( prim, this );
    m_previewWidget->setMesh( mesh );

    // 2. 联动刷新 QGIS 3D (检查右下角开关)
    if ( ui->checkBoxAutoSync && ui->checkBoxAutoSync->isChecked() )
    {
        // 调用加载函数，传 false 表示只更新模型，不跳动相机视角
        this->onLoadToQGIS3D( false );
    }
}
// ========================参数访问====================================
//读取 UI 控件（如 spinBox 数字框或 lineEdit 文本框）里的值，并将其转换为 double交给模型构建函数使用
//位姿变换参数
double ParamModelerDock::poseTranslateX() const { return ui->lineEditTX->text().toDouble(); }
double ParamModelerDock::poseTranslateY() const { return ui->lineEditTY->text().toDouble(); }
double ParamModelerDock::poseTranslateZ() const { return ui->lineEditTZ->text().toDouble(); }
// 找到这三个函数，用新的 spinBox 替换掉报错的 lineEdit
double ParamModelerDock::poseRotateX() const { return ui->spinBoxROmega->value(); } // 指向新控件：Omega
double ParamModelerDock::poseRotateY() const { return ui->spinBoxRPhi->value(); } // 指向新控件：Phi
double ParamModelerDock::poseRotateZ() const { return ui->spinBoxRKappa->value(); } // 指向新控件：Kappa

//几何体形状参数
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
double ParamModelerDock::coneCylCylHeight() const { return ui->spinBoxConeCylCylHeight->value(); }
double ParamModelerDock::coneCylConeHeight() const { return ui->spinBoxConeCylConeHeight->value(); }
double ParamModelerDock::gabledRoofLength() const { return ui->spinBoxGRLength->value(); }
double ParamModelerDock::gabledRoofWidth() const { return ui->spinBoxGRWidth->value(); }
double ParamModelerDock::gabledRoofWallHeight() const { return ui->spinBoxGRHeightWall->value(); }
double ParamModelerDock::gabledRoofRoofHeight() const { return ui->spinBoxGRHeightRoof->value(); }
double ParamModelerDock::pyramidLength() const { return ui->spinBoxPRLength->value(); }
double ParamModelerDock::pyramidWidth() const { return ui->spinBoxPRWidth->value(); }
double ParamModelerDock::pyramidWallHeight() const { return ui->spinBoxPRHeightWall->value(); }
double ParamModelerDock::pyramidRoofHeight() const { return ui->spinBoxPRHeightRoof->value(); }
double ParamModelerDock::tpBottomLength() const { return ui->spinBoxTPRBottomLength->value(); }
double ParamModelerDock::tpBottomWidth() const { return ui->spinBoxTPRBottomWidth->value(); }
double ParamModelerDock::tpTopLength() const { return ui->spinBoxTPRTopLength->value(); }
double ParamModelerDock::tpTopWidth() const { return ui->spinBoxTPRTopWidth->value(); }
double ParamModelerDock::tpWallHeight() const { return ui->spinBoxTPRHeightWall->value(); }
double ParamModelerDock::tpRoofHeight() const { return ui->spinBoxTPRHeightRoof->value(); }
double ParamModelerDock::hcrLength() const { return ui->spinBoxHCRLength->value(); }
double ParamModelerDock::hcrWidth() const { return ui->spinBoxHCRWidth->value(); }
double ParamModelerDock::hcrWallHeight() const { return ui->spinBoxHCRHeightWall->value(); }
double ParamModelerDock::hcrRadius() const { return ui->spinBoxHCRRadius->value(); }
double ParamModelerDock::icOuterLength() const { return ui->spinBoxICLength->value(); }
double ParamModelerDock::icOuterWidth() const { return ui->spinBoxICWidth->value(); }
double ParamModelerDock::icOuterHeight() const { return ui->spinBoxICHeight->value(); }
double ParamModelerDock::icInnerLength() const { return ui->spinBoxICInnerLength->value(); }
double ParamModelerDock::icInnerWidth() const { return ui->spinBoxICInnerWidth->value(); }
double ParamModelerDock::icInnerHeight() const { return ui->spinBoxICInnerHeight->value(); }
double ParamModelerDock::icOffsetX() const { return ui->spinBoxICOffsetX->value(); }
double ParamModelerDock::icOffsetY() const { return ui->spinBoxICOffsetY->value(); }
double ParamModelerDock::aghLength() const { return ui->spinBoxAGHLength->value(); }
double ParamModelerDock::aghWidth() const { return ui->spinBoxAGHWidth->value(); }
double ParamModelerDock::aghWallHeight() const { return ui->spinBoxAGHHeightWall->value(); }
double ParamModelerDock::aghRoofHeight() const { return ui->spinBoxAGHRoofHeight->value(); }
double ParamModelerDock::aghRidgeLength() const { return ui->spinBoxAGHRidgeLength->value(); }
double ParamModelerDock::aghRidgeOffset() const { return ( aghRidgeRatio() - 0.5 ) * aghWidth(); }
double ParamModelerDock::aghRidgeRatio() const { return ui->spinBoxAGHRidgeOffset->value(); }
double ParamModelerDock::cylHemiRadius() const { return ui->spinBoxCylHemiRadius->value(); }
double ParamModelerDock::cylHemiHeight() const { return ui->spinBoxCylHemiHeight->value(); }
double ParamModelerDock::cylHemiDomeHeight() const { return ui->spinBoxCylHemiDomeHeight->value(); }
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
double ParamModelerDock::tgWallHeight() const { return ui->spinBoxTGHeightWall->value(); }
double ParamModelerDock::tgRoofHeight() const { return ui->spinBoxTGRoofHeight->value(); }
double ParamModelerDock::tgAngle() const { return ui->spinBoxTGAngle->value(); }
double ParamModelerDock::tgRidgeRatio() const { return ui->spinBoxTGRidgeRatio->value(); }

// ========================================================================
// Tab2：加载点云 / OBJ 数据
// ========================================================================
void ParamModelerDock::onLoadInputData()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, tr("导入点云"), "",
        tr("点云文件 (*.ply *.las *.laz *.xyz *.txt)") );
    if ( filePath.isEmpty() ) return;

    m_inputDataPath = filePath;
    QFileInfo fi( filePath );

    DEBUG_LOG( QString( "[Tab2] 加载输入数据: %1\n" ).arg( filePath ).toStdWString().c_str() );

    ui->labelInputInfo->setText( tr("已加载：%1").arg( fi.fileName() ) );
    ui->labelPrimitiveType->setText( tr("识别结果：-") );
    ui->tableInverseParams->setRowCount( 0 );

    // 加载完才能识别，识别完才能反演
    ui->btnClassifyPrimitive->setEnabled( true );
    ui->btnInverseParams->setEnabled(  false );
}
// ========================================================================
// 基元类型自动识别（调用分类模块）
// ========================================================================
void ParamModelerDock::onClassifyPrimitive()
{
    if ( m_inputDataPath.isEmpty() ) return;

    DEBUG_LOG( QString( "[Tab2] 开始基元分类, 文件: %1\n" ).arg( m_inputDataPath ).toStdWString().c_str() );
    PcdResult result = PrimitiveClassifier::classify( m_inputDataPath );

    DEBUG_LOG( QString( "[Tab2] 分类完成: %1, 置信度: %2%\n" )
      .arg( result.primitiveType )
      .arg( result.confidence * 100, 0, 'f', 1 )
      .toStdWString().c_str() );

    ui->labelPrimitiveType->setText(
      tr( "识别结果：%1（%2%）" )
        .arg( result.primitiveType )
        .arg( result.confidence * 100, 0, 'f', 0 )
    ); // 0位小数

    ui->comboPrimitive->setCurrentText( result.primitiveType );

    ui->btnInverseParams->setEnabled( true );
}
// ========================================================================
// 参数反演（调用反演模块）
// ========================================================================
void ParamModelerDock::onInverseParams()
{
    if ( m_inputDataPath.isEmpty() ) return;

    ui->progressInversion->setVisible( true );
    ui->progressInversion->setValue( 0 );

    QString prim = ui->comboPrimitive->currentText();

    DEBUG_LOG( QString( "[Tab2] 开始参数反演, 基元: %1, 文件: %2\n" ).arg( prim ).arg( m_inputDataPath ).toStdWString().c_str() );
    QMap<QString, double> params = ParamInverter::invert( prim, m_inputDataPath );

    if ( !params.isEmpty() )
    {
        DEBUG_LOG( QString( "[Tab2] 反演完成, 返回 %1 个参数\n" ).arg( params.size() ).toStdWString().c_str() );
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

    onUpdatePreview();
}

