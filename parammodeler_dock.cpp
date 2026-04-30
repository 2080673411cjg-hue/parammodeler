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
#include <algorithm>

// 3. QGIS 基础与地理要素处理
#include <qgis.h>
#include <qgisinterface.h>
#include <QgsProject.h>
#include <QgsFeature.h>
#include <QgsGeometry.h>
#include <QgsPoint.h>
#include <QgsLineString.h>
#include <QgsPolygon.h>

// 4. QGIS 图层与渲染系统 (2D/3D)
#include <QgsVectorLayer.h>
#include <qgspointcloudlayer.h>       // 专门用于处理 LAS/LAZ 点云
#include <qgssymbol.h>               // 用于创建点/面符号
#include <QgsSingleSymbolRenderer.h>  // 用于设置图层渲染样式
#include <QgsPolygon3DSymbol.h>      // 用于 3D 材质属性
#include <QgsVectorLayer3DRenderer.h>// 3D 渲染器
#include <Qgs3DMapCanvas.h>          // 3D 画布交互
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
  bindSliderSpin( ui->sliderCWidth, ui->spinBoxCWidth, 100.0, 50.0 );  // 长方体
  bindSliderSpin( ui->sliderCDepth, ui->spinBoxCDepth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCHeight, ui->spinBoxCHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCylRadius, ui->spinBoxCylRadius, 100.0, 50.0 );  // 圆柱
  bindSliderSpin( ui->sliderCylHeight, ui->spinBoxCylHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLMainWidth, ui->spinBoxLMainWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLMainDepth, ui->spinBoxLMainDepth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLWingWidth, ui->spinBoxLWingWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLWingDepth, ui->spinBoxLWingDepth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLHeight, ui->spinBoxLHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderConeCylRadius, ui->spinBoxConeCylRadius, 100.0, 50.0 );  // 圆锥体 (ConeCylinder)
  bindSliderSpin( ui->sliderConeCylCylHeight, ui->spinBoxConeCylCylHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderConeCylConeHeight, ui->spinBoxConeCylConeHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderGRWidth,      ui->spinBoxGRWidth,100,50 );  // 人字形屋顶 (GabledRoof) 绑定
  bindSliderSpin( ui->sliderGRDepth, ui->spinBoxGRDepth, 100, 50 );
  bindSliderSpin( ui->sliderGRHeightWall, ui->spinBoxGRHeightWall, 100.0, 50.0 );
  bindSliderSpin( ui->sliderGRHeightRoof, ui->spinBoxGRHeightRoof, 100.0, 50.0 );
  bindSliderSpin( ui->sliderPRWidth, ui->spinBoxPRWidth, 100, 50 ); // --- PyramidRoof (金字塔房屋) 绑定 ---
  bindSliderSpin( ui->sliderPRDepth, ui->spinBoxPRDepth, 100, 50 );
	bindSliderSpin(ui->sliderPRHeightWall, ui->spinBoxPRHeightWall, 100.0, 50.0 );
	bindSliderSpin(ui->sliderPRHeightRoof, ui->spinBoxPRHeightRoof, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTPRBottomWidth, ui->spinBoxTPRBottomWidth, 100.0, 50.0 );		// --- TPRoof (棱台房屋) 绑定 ---
  bindSliderSpin( ui->sliderTPRBottomDepth, ui->spinBoxTPRBottomDepth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTPRTopWidth, ui->spinBoxTPRTopWidth, 100, 50 );
  bindSliderSpin( ui->sliderTPRTopDepth, ui->spinBoxTPRTopDepth, 100, 50 );
  bindSliderSpin( ui->sliderTPRHeightWall, ui->spinBoxTPRHeightWall, 100, 50 );
  bindSliderSpin( ui->sliderTPRHeightRoof, ui->spinBoxTPRHeightRoof, 100, 50 );
  bindSliderSpin( ui->sliderHCRWidth, ui->spinBoxHCRWidth, 100, 50 ); // --- HalfCylinderRoof (半圆柱屋顶) 绑定 ---
  bindSliderSpin( ui->sliderHCRDepth, ui->spinBoxHCRDepth, 100, 50 );
  bindSliderSpin( ui->sliderHCRHeightWall, ui->spinBoxHCRHeightWall, 100.0, 50.0 );
  bindSliderSpin( ui->sliderHCRRadius, ui->spinBoxHCRRadius, 100, 50 );
  bindSliderSpin( ui->sliderICWidth, ui->spinBoxICWidth, 100, 50 ); // --- IndentedCuboid (凹陷长方体) 绑定 ---
  bindSliderSpin( ui->sliderICDepth, ui->spinBoxICDepth, 100, 50 );
  bindSliderSpin( ui->sliderICHeight, ui->spinBoxICHeight, 100, 50 );
  bindSliderSpin( ui->sliderICInnerWidth, ui->spinBoxICInnerWidth, 100, 50 );
  bindSliderSpin( ui->sliderICInnerDepth, ui->spinBoxICInnerDepth, 100, 50 );
  bindSliderSpin( ui->sliderICInnerHeight, ui->spinBoxICInnerHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderICOffsetX, ui->spinBoxICOffsetX, 100, 50 );
  bindSliderSpin( ui->sliderICOffsetY, ui->spinBoxICOffsetY, 100, 50 );
  bindSliderSpin( ui->sliderAGHWidth, ui->spinBoxAGHWidth, 100, 50 ); // --- AsymmetricGableHouse (非对称人字形屋顶房屋) 绑定 ---
  bindSliderSpin( ui->sliderAGHDepth, ui->spinBoxAGHDepth, 100, 50 );
  bindSliderSpin( ui->sliderAGHHeightWall, ui->spinBoxAGHHeightWall, 100, 50 );
  bindSliderSpin( ui->sliderAGHRoofHeight, ui->spinBoxAGHRoofHeight, 100, 50 );
  bindSliderSpin( ui->sliderAGHRidgeLength, ui->spinBoxAGHRidgeLength, 100.0, 50.0 );
  bindSliderSpin( ui->sliderAGHRidgeOffset, ui->spinBoxAGHRidgeOffset, 100, 50.0, -50.0 );
  bindSliderSpin( ui->sliderCylHemiRadius, ui->spinBoxCylHemiRadius, 100, 50 ); // --- CylinderHemisphere (穹顶圆柱) 绑定 ---
  bindSliderSpin( ui->sliderCylHemiHeight, ui->spinBoxCylHemiHeight, 100, 50 );
  bindSliderSpin( ui->sliderCylHemiDomeHeight, ui->spinBoxCylHemiDomeHeight, 100, 50 );
  bindSliderSpin( ui->sliderCylHemiBulge, ui->spinBoxCylHemiBulge, 100.0, 1.0 );
  bindSliderSpin( ui->sliderFTBaseRadius, ui->spinBoxFTBaseRadius, 100.0, 50.0 ); // --- FourStageRoundTower (四段式圆塔形) 绑定 ---
  bindSliderSpin( ui->sliderFTBaseHeight, ui->spinBoxFTBaseHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderFTMiddleHeight, ui->spinBoxFTMiddleHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderFTMiddleTopRadius, ui->spinBoxFTMiddleTopRadius, 100.0, 50.0 );
  bindSliderSpin( ui->sliderFTMiddleBulge, ui->spinBoxFTMiddleBulge, 100.0, 0.6 );
  bindSliderSpin( ui->sliderFTConeHeight, ui->spinBoxFTConeHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGWidth1, ui->spinBoxTGWidth1, 100.0, 50.0 ); // --- TwoGableHouses (双人字屋顶房屋) 绑定 ---
  bindSliderSpin( ui->sliderTGWidth2, ui->spinBoxTGWidth2, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGDepth, ui->spinBoxTGDepth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGHeightWall, ui->spinBoxTGHeightWall, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGRoofHeight, ui->spinBoxTGRoofHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTGAngle,      ui->spinBoxTGAngle,      10.0, 179.0 );
	//位姿旋转三参数的输入绑定
	bindSliderSpin(ui->sliderROmega, ui->spinBoxROmega, 10.0, 180.0, -180.0);
	bindSliderSpin(ui->sliderRPhi,   ui->spinBoxRPhi,   10.0, 180.0, -180.0);
	bindSliderSpin(ui->sliderRKappa, ui->spinBoxRKappa, 10.0, 180.0, -180.0);
  // ====================== 创建导出菜单 ======================
		// 换成直接连接 UI 文件里定义好的 action
		connect( ui->actOBJ,    &QAction::triggered, this, &ParamModelerDock::onExportOBJClicked );
		connect( ui->actJSON,   &QAction::triggered, this, &ParamModelerDock::onExportJSONClicked );
		connect( ui->actPLY,    &QAction::triggered, this, &ParamModelerDock::onExportPLYClicked );
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
  connect( ui->sliderCWidth,  &QSlider::valueChanged, this, schedulePreview );  // 长方体
  connect( ui->sliderCDepth,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylRadius, &QSlider::valueChanged, this, schedulePreview );  // 圆柱
  connect( ui->sliderCylHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLMainWidth,  &QSlider::valueChanged, this, schedulePreview );  // L型房子
  connect( ui->sliderLMainDepth,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLWingWidth,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLWingDepth,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLHeight,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderConeCylRadius,    &QSlider::valueChanged, this, schedulePreview ); // 圆锥圆柱
  connect( ui->sliderConeCylCylHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderConeCylConeHeight,&QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRWidth,      &QSlider::valueChanged, this, schedulePreview ); // 人字形屋顶
  connect( ui->sliderGRDepth,      &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRHeightRoof, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderPRWidth,      &QSlider::valueChanged, this, schedulePreview );  // 金字塔屋顶
  connect( ui->sliderPRDepth,      &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderPRHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderPRHeightRoof, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRBottomWidth, &QSlider::valueChanged, this, schedulePreview );  // 棱台屋顶
  connect( ui->sliderTPRBottomDepth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRTopWidth,    &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRTopDepth,    &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRHeightWall,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPRHeightRoof,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderHCRWidth,      &QSlider::valueChanged, this, schedulePreview );  // 半圆柱屋顶
  connect( ui->sliderHCRDepth,      &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderHCRHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderHCRRadius,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHemiRadius,    &QSlider::valueChanged, this, schedulePreview );  // 穹顶圆柱
  connect( ui->sliderCylHemiHeight,    &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHemiDomeHeight,&QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHemiBulge,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICWidth,       &QSlider::valueChanged, this, schedulePreview );  // 凹陷长方体
  connect( ui->sliderICDepth,       &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICHeight,      &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICInnerWidth,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICInnerDepth,  &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICInnerHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICOffsetX,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderICOffsetY,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderAGHWidth,       &QSlider::valueChanged, this, schedulePreview );  // 非对称人字形屋顶
  connect( ui->sliderAGHDepth,       &QSlider::valueChanged, this, schedulePreview );
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
  connect( ui->sliderTGWidth1,     &QSlider::valueChanged, this, schedulePreview );  // 双人字屋顶房屋
  connect( ui->sliderTGWidth2,     &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGDepth,      &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGRoofHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTGAngle,      &QSlider::valueChanged, this, schedulePreview );
  // 切换基元时切换参数页面
  connect( ui->comboPrimitive, &QComboBox::currentTextChanged, this, &ParamModelerDock::onPrimitiveChanged );
  // 切换基元时立刻刷新预览
  connect( ui->comboPrimitive, &QComboBox::currentTextChanged, this, [this]( const QString & ) { onUpdatePreview(); } );
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
}
// =======================导出前检验===================
// 在导出前检查当前参数是否能生成有效的几何网格
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
    if ( !checkMeshValid( primitiveType, this ) ) return;
    ExportJSON::writeJSON(this);
}
// ========================导出点云====================
void ParamModelerDock::onExportPLYClicked()
{
    QString primitiveType = ui->comboPrimitive->currentText();
    if ( !checkMeshValid( primitiveType, this ) ) return;

    QString fileName = QFileDialog::getSaveFileName(
        this, tr("保存点云文件"), "", tr("PLY Files (*.ply)") );
    if ( fileName.isEmpty() ) return;

    ExportPointCloud::exportPLY( fileName, primitiveType, this );
}
// ========================导出Mesh====================
void ParamModelerDock::onExportMeshClicked()
{
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
//如果图层已存在且面数未变，直接通过changeGeometry修改要素几何，不涉及图层，实现平滑更新；
void ParamModelerDock::onLoadToQGIS3D( bool zoomToLayer )
{
  if ( m_isUpdating )
    return;
  m_isUpdating = true;

  // 1. 构建新网格
  QString primitiveType = ui->comboPrimitive->currentText();
  MeshData mesh = BuildMesh::build( primitiveType, this );
  if ( mesh.isEmpty() )
  {
    if ( zoomToLayer )
      QMessageBox::warning( this, tr( "错误" ), tr( "无法生成模型，请检查参数" ) );
    m_isUpdating = false;
    return;
  }

  // 2. 应用位姿变换
  QMatrix4x4 mat;
  mat.setToIdentity();
  mat.translate( poseTranslateX(), poseTranslateY(), poseTranslateZ() );
  // 改后（X→Y→Z，即ω→φ→κ，摄影测量标准）
  mat.rotate( poseRotateX(), 1, 0, 0 ); // ω，绕X
  mat.rotate( poseRotateY(), 0, 1, 0 ); // φ，绕Y
  mat.rotate( poseRotateZ(), 0, 0, 1 ); // κ，绕Z

  // 3. 构造 Feature 列表
  QgsFeatureList features;
  int triCount = mesh.indices.size() / 3;
  for ( int i = 0; i < triCount; i++ )
  {
    QVector3D v0 = mat.map( mesh.vertices[mesh.indices[i * 3]] );
    QVector3D v1 = mat.map( mesh.vertices[mesh.indices[i * 3 + 1]] );
    QVector3D v2 = mat.map( mesh.vertices[mesh.indices[i * 3 + 2]] );

    QgsPolygon *poly = new QgsPolygon();
    QgsLineString *ring = new QgsLineString();
    ring->setPoints( QgsPointSequence() << QgsPoint( v0.x(), v0.y(), v0.z() ) << QgsPoint( v1.x(), v1.y(), v1.z() ) << QgsPoint( v2.x(), v2.y(), v2.z() ) << QgsPoint( v0.x(), v0.y(), v0.z() ) );
    poly->setExteriorRing( ring );
    QgsFeature feat;
    feat.setGeometry( QgsGeometry( poly ) );
    features.append( feat );
  }

  // 4. 判断是否可以复用已有图层
  //    检查：图层指针有效 & 仍在项目中 & 面数相同（基元未切换）
  bool canReuse = ( m_modelLayer != nullptr )
                  && ( QgsProject::instance()->mapLayer( m_modelLayer->id() ) != nullptr )
                  && ( m_modelLayer->featureCount() == triCount );

  if ( canReuse )
  {
    // ★ 核心路径：原地修改，不删不建，最轻量
    m_modelLayer->startEditing();

    // 拿到所有旧 Feature 的 ID，逐个替换几何体
    QgsFeatureIterator it = m_modelLayer->getFeatures();
    QgsFeature oldFeat;
    int idx = 0;
    while ( it.nextFeature( oldFeat ) && idx < features.size() )
    {
      QgsGeometry geom = features[idx].geometry(); //先存成局部变量再传入：
      m_modelLayer->changeGeometry( oldFeat.id(), geom );
      idx++;
    }

    m_modelLayer->commitChanges();
    // 通知 3D 渲染器数据已更新
    emit m_modelLayer->repaintRequested();
  }
  else
  {
    // ★ 降级路径：第一次加载，或切换了基元（面数不同），重建图层
    QString layerName = "ParamModeler_Model";

    QgsVectorLayer *layer = new QgsVectorLayer( "PolygonZ?crs=EPSG:3857", layerName, "memory" );
    if ( !layer || !layer->isValid() )
    {
      if ( zoomToLayer )
        QMessageBox::warning( this, tr( "错误" ), tr( "创建内存图层失败" ) );
      delete layer;
      m_isUpdating = false;
      return;
    }

    layer->dataProvider()->addFeatures( features );

    QgsPolygon3DSymbol *symbol3D = new QgsPolygon3DSymbol();
    symbol3D->setAltitudeClamping( Qgis::AltitudeClamping::Absolute );
    symbol3D->setAltitudeBinding( Qgis::AltitudeBinding::Vertex );
    symbol3D->setCullingMode( Qgs3DTypes::NoCulling );
    QgsVectorLayer3DRenderer *renderer3D = new QgsVectorLayer3DRenderer();
    renderer3D->setSymbol( symbol3D );
    layer->setRenderer3D( renderer3D );

    // 先加新图层，再清理旧图层（避免空窗口期）
    QgsProject::instance()->addMapLayer( layer );
    if ( m_modelLayer )
      removeLayerByName( "ParamModeler_Model", layer->id() );

    // 缓存新图层指针
    m_modelLayer = layer;

    // 监听图层被外部删除的情况（比如用户手动从图层树删除）
    connect( m_modelLayer, &QgsMapLayer::willBeDeleted, this, [this]() {
      m_modelLayer = nullptr;
    } );

    if ( mIface->mapCanvases3D().isEmpty() )
      mIface->createNewMapCanvas3D( tr( "ParamModeler 3D" ) );

    if ( zoomToLayer )
    {
      mIface->mapCanvas()->setExtent( layer->extent() );
      mIface->mapCanvas()->refresh();
      QMessageBox::information( this, tr( "加载成功" ), tr( "模型已加载！\n三角面数：%1" ).arg( triCount ) );
    }
  }

  m_isUpdating = false;
}
// =================将外部点云加载到QGIS 3D视图======================================
//针对 LAS/LAZ 使用了 BFS 广度优先搜索遍历八叉树索引，将海量点云高效转化为 QGIS 的 3D 点符号图层，以便与参数化模型进行重叠对比。
void ParamModelerDock::onLoadExternalPointCloud()
{
  QString filePath = QFileDialog::getOpenFileName(
    this, tr( "选择点云文件" ), "", tr( "点云文件 (*.ply *.las *.laz *.xyz *.txt)" )
  );

  if ( filePath.isEmpty() )
    return;

  QFileInfo fi( filePath );//提取文件名
  QString layerName = QString( "外部点云 - %1" ).arg( fi.fileName() );
  QString suffix = fi.suffix().toLower();

  QgsMapLayer *pcLayer = nullptr;

  if ( suffix == "ply" )
{
  // ===== PLY：支持 ASCII 和 Binary（little/big endian）=====
  QFile plyFile( filePath );
  if ( !plyFile.open( QIODevice::ReadOnly ) )  // ← 不加 Text，统一用 ReadOnly
  {
    QMessageBox::warning( this, tr("错误"), tr("无法打开PLY文件") );
    return;
  }

  // ---------- 1. 解析 Header ----------
  enum class PlyFormat { Unknown, Ascii, BinaryLE, BinaryBE };
  PlyFormat plyFormat = PlyFormat::Unknown;
  int vertexCount = 0;

  // property: name -> (byteOffset, byteSize)
  // 我们只关心 x/y/z 三个 property 的偏移和类型
  struct PropInfo { int offset; int size; bool isDouble; };
  QMap<QString, PropInfo> propMap;
  int currentOffset = 0;  // 在一个 record 里累计字节偏移

  // 逐行读 header（header 一定是纯 ASCII）
  while ( true )
  {
    // 手动按行读，兼容 \r\n 和 \n
    QByteArray lineBA;
    char c;
    while ( plyFile.getChar(&c) )
    {
      if ( c == '\n' ) break;
      if ( c != '\r' ) lineBA.append(c);
    }
    QString line = QString::fromLatin1( lineBA ).trimmed();

    if ( line.startsWith("format") )
    {
      if      ( line.contains("ascii") )                  plyFormat = PlyFormat::Ascii;
      else if ( line.contains("binary_little_endian") )   plyFormat = PlyFormat::BinaryLE;
      else if ( line.contains("binary_big_endian") )      plyFormat = PlyFormat::BinaryBE;
    }
    else if ( line.startsWith("element vertex") )
    {
      vertexCount = line.split(' ', Qt::SkipEmptyParts).last().toInt();
    }
    else if ( line.startsWith("property") )
    {
      // 格式：property <type> <name>
      // 只处理 vertex 的 property（element vertex 之后，element face 之前）
      QStringList tok = line.split(' ', Qt::SkipEmptyParts);
      if ( tok.size() >= 3 )
      {
        QString typeName = tok[1];
        QString propName = tok[2];

        int sz = 4;
        bool isDbl = false;
        if      ( typeName == "double" || typeName == "float64" ) { sz = 8; isDbl = true; }
        else if ( typeName == "float"  || typeName == "float32" ) { sz = 4; isDbl = false; }
        else if ( typeName == "int"    || typeName == "int32"   ||
                  typeName == "uint"   || typeName == "uint32"  ) { sz = 4; isDbl = false; }
        else if ( typeName == "short"  || typeName == "int16"   ||
                  typeName == "ushort" || typeName == "uint16"  ) { sz = 2; isDbl = false; }
        else if ( typeName == "char"   || typeName == "uchar"   ||
                  typeName == "int8"   || typeName == "uint8"   ) { sz = 1; isDbl = false; }

        propMap[propName] = { currentOffset, sz, isDbl };
        currentOffset += sz;
      }
    }
    else if ( line == "end_header" )
    {
      break;
    }

    if ( plyFile.atEnd() ) break;
  }
  // currentOffset 此时就是一个顶点 record 的总字节数
  int recordSize = currentOffset;

  // ---------- 2. 校验 ----------
  if ( plyFormat == PlyFormat::Unknown || vertexCount == 0 ||
       !propMap.contains("x") || !propMap.contains("y") || !propMap.contains("z") )
  {
    QMessageBox::warning( this, tr("错误"),
      tr("PLY文件格式不支持或缺少 x/y/z 属性\nformat=%1  vertices=%2")
        .arg( (int)plyFormat ).arg( vertexCount ) );
    plyFile.close();
    return;
  }

  // ---------- 3. 创建图层 + 3D 渲染器（和原来完全一样）----------
  QgsVectorLayer *vl = new QgsVectorLayer(
    "PointZ?crs=EPSG:3857", layerName, "memory" );

  QgsPoint3DSymbol *symbol3D = new QgsPoint3DSymbol();
  symbol3D->setAltitudeClamping( Qgis::AltitudeClamping::Absolute );
  symbol3D->setShape( Qgis::Point3DShape::Sphere );
  QVariantMap props;
  props["radius"] = 0.03;
  symbol3D->setShapeProperties( props );

  QgsPhongMaterialSettings material;
  QColor pointColor( 30, 100, 255, 255 );
  material.setAmbient( pointColor );
  material.setDiffuse( pointColor );
  material.setSpecular( Qt::black );
  material.setShininess( 0 );
  symbol3D->setMaterialSettings( material.clone() );

  QgsVectorLayer3DRenderer *renderer3D = new QgsVectorLayer3DRenderer();
  renderer3D->setSymbol( symbol3D );
  vl->setRenderer3D( renderer3D );

  // ---------- 4. 读取顶点数据 ----------
  const PropInfo &px = propMap["x"];
  const PropInfo &py = propMap["y"];
  const PropInfo &pz = propMap["z"];

  // 把 QByteArray 里某偏移的数据转成 double 的 lambda
  // 支持 float/double，支持字节序翻转
  auto readVal = [&]( const QByteArray &rec, const PropInfo &pi, bool bigEndian ) -> double
  {
    if ( pi.isDouble )
    {
      double v;
      memcpy( &v, rec.constData() + pi.offset, 8 );
      if ( bigEndian )
      {
        char *b = reinterpret_cast<char*>(&v);
        std::reverse( b, b + 8 );
      }
      return v;
    }
    else
    {
      float v;
      memcpy( &v, rec.constData() + pi.offset, 4 );
      if ( bigEndian )
      {
        char *b = reinterpret_cast<char*>(&v);
        std::reverse( b, b + 4 );
      }
      return static_cast<double>(v);
    }
  };

  bool isBigEndian = ( plyFormat == PlyFormat::BinaryBE );

  QgsFeatureList features;
  features.reserve( 1000 );
  int count = 0;

  if ( plyFormat == PlyFormat::Ascii )
  {
    // ASCII 路径：和原来一样，但现在按 property 顺序确定列索引
    // 找 x/y/z 在 property 列表里的列号
    int xCol = -1, yCol = -1, zCol = -1;
    int col = 0;
    for ( auto it = propMap.begin(); it != propMap.end(); ++it, ++col )
    {
      if ( it.key() == "x" ) xCol = col;
      if ( it.key() == "y" ) yCol = col;
      if ( it.key() == "z" ) zCol = col;
    }
    // QMap 是按 key 字母序排的，不能直接用来确定列号
    // 需要在 header 解析时记录 property 插入顺序，这里用更简单的方式：
    // Open3D 的 ASCII PLY 总是 x y z 在前三列，直接用 parts[0/1/2] 最安全
    // 但为了通用性，改用偏移最小的三个 property 的顺序
    // ---- 简化处理：ASCII 时重新用 parts[xCol/yCol/zCol] ----
    // 因为 QMap 字母序和 header 顺序不同，重建一个顺序表
    QStringList propOrder;
    {
      // 重新过一遍 header 拿顺序（复用已解析的 propMap，按 offset 排序）
      QList<QPair<int,QString>> offsetList;
      for ( auto it = propMap.begin(); it != propMap.end(); ++it )
        offsetList.append( { it.value().offset, it.key() } );
      std::sort( offsetList.begin(), offsetList.end() );
      for ( auto &p : offsetList ) propOrder << p.second;
    }
    xCol = propOrder.indexOf("x");
    yCol = propOrder.indexOf("y");
    zCol = propOrder.indexOf("z");

    while ( !plyFile.atEnd() && count < vertexCount )
    {
      QByteArray lineBA = plyFile.readLine().trimmed();
      if ( lineBA.isEmpty() ) continue;
      QList<QByteArray> parts = lineBA.split(' ');
      // 过滤空串（多余空格）
      parts.removeAll( QByteArray() );
      int need = std::max( { xCol, yCol, zCol } ) + 1;
      if ( parts.size() < need ) continue;

      double x = parts[xCol].toDouble();
      double y = parts[yCol].toDouble();
      double z = parts[zCol].toDouble();

      QgsFeature feat;
      feat.setGeometry( QgsGeometry( new QgsPoint( x, y, z ) ) );
      features.append( feat );
      count++;

      if ( features.size() >= 1000 )
      {
        vl->dataProvider()->addFeatures( features );
        features.clear();
      }
    }
  }
  else
  {
    // Binary 路径：按 recordSize 逐块读取
    for ( int i = 0; i < vertexCount; i++ )
    {
      QByteArray rec = plyFile.read( recordSize );
      if ( rec.size() < recordSize )
        break;  // 文件提前结束

      double x = readVal( rec, px, isBigEndian );
      double y = readVal( rec, py, isBigEndian );
      double z = readVal( rec, pz, isBigEndian );

      QgsFeature feat;
      feat.setGeometry( QgsGeometry( new QgsPoint( x, y, z ) ) );
      features.append( feat );
      count++;

      if ( features.size() >= 1000 )
      {
        vl->dataProvider()->addFeatures( features );
        features.clear();
      }
    }
  }

  if ( !features.isEmpty() )
    vl->dataProvider()->addFeatures( features );

  plyFile.close();
  pcLayer = vl;
}
  else if ( suffix == "las" || suffix == "laz" )
  {
    // ===== LAS/LAZ：通过QGIS点云索引读取XYZ，转成内存PointZ图层 =====

    // 先用QgsPointCloudLayer加载，拿到index
    QgsPointCloudLayer *tmpLayer = new QgsPointCloudLayer( filePath, "tmp", "pdal" );
    if ( !tmpLayer || !tmpLayer->isValid() )
    {
      QMessageBox::warning( this, tr( "错误" ), tr( "无法加载LAS文件：%1" ).arg( filePath ) );
      delete tmpLayer;
      return;
    }

    QgsPointCloudIndex index = tmpLayer->dataProvider()->index();
    if ( !index.isValid() )
    {
      QMessageBox::warning( this, tr( "错误" ), tr( "点云索引无效" ) );
      delete tmpLayer;
      return;
    }

    // 创建内存PointZ图层
    QgsVectorLayer *vl = new QgsVectorLayer(
      "PointZ?crs=EPSG:3857", layerName, "memory"
    );

    // 设置3D渲染器（和PLY一样）
    QgsPoint3DSymbol *symbol3D = new QgsPoint3DSymbol();
    symbol3D->setAltitudeClamping( Qgis::AltitudeClamping::Absolute );
    symbol3D->setShape( Qgis::Point3DShape::Sphere );
    QVariantMap props;
    props["radius"] = 0.03;
    symbol3D->setShapeProperties( props );
				
    QgsPhongMaterialSettings material;
    QColor pointColor( 30, 100, 255, 255 );
    material.setAmbient( pointColor );
    material.setDiffuse( pointColor );
    material.setSpecular( Qt::black );
    material.setShininess( 0 );
    symbol3D->setMaterialSettings( material.clone() );

    QgsVectorLayer3DRenderer *renderer3D = new QgsVectorLayer3DRenderer();
    renderer3D->setSymbol( symbol3D );
    vl->setRenderer3D( renderer3D );

    // 准备请求：只读XYZ
    QgsPointCloudAttributeCollection attrs;
    attrs.push_back( QgsPointCloudAttribute( "X", QgsPointCloudAttribute::Int32 ) );
    attrs.push_back( QgsPointCloudAttribute( "Y", QgsPointCloudAttribute::Int32 ) );
    attrs.push_back( QgsPointCloudAttribute( "Z", QgsPointCloudAttribute::Int32 ) );
    QgsPointCloudRequest request;
    request.setAttributes( attrs );

    // scale和offset用于把整数坐标转成真实坐标
    QgsVector3D scale = index.scale();
    QgsVector3D offset = index.offset();

    // BFS遍历八叉树所有节点
    QgsFeatureList features;
    features.reserve( 1000 );
    QList<QgsPointCloudNodeId> queue;
    queue.append( index.root() );

    while ( !queue.isEmpty() )
    {
      QgsPointCloudNodeId nodeId = queue.takeFirst();

      // 加入子节点
      QgsPointCloudNode node = index.getNode( nodeId );
      for ( const QgsPointCloudNodeId &child : node.children() )
        queue.append( child );

      // 读取节点数据
      std::unique_ptr<QgsPointCloudBlock> block = index.nodeData( nodeId, request );
      if ( !block )
        continue;

      const char *data = block->data();
      int pointCount = block->pointCount();
      int recordSize = block->pointRecordSize();

      // 解析每个点的XYZ（Int32）
      for ( int i = 0; i < pointCount; i++ )
      {
        const char *ptr = data + i * recordSize;

        qint32 ix = *reinterpret_cast<const qint32 *>( ptr );
        qint32 iy = *reinterpret_cast<const qint32 *>( ptr + 4 );
        qint32 iz = *reinterpret_cast<const qint32 *>( ptr + 8 );

        double x = ix * scale.x() + offset.x();
        double y = iy * scale.y() + offset.y();
        double z = iz * scale.z() + offset.z();

        QgsFeature feat;
        feat.setGeometry( QgsGeometry( new QgsPoint( x, y, z ) ) );
        features.append( feat );

        if ( features.size() >= 1000 )
        {
          vl->dataProvider()->addFeatures( features );
          features.clear();
        }
      }
    }

    if ( !features.isEmpty() )
      vl->dataProvider()->addFeatures( features );

    delete tmpLayer;
    pcLayer = vl;
  }
  else
  {
    QMessageBox::warning( this, tr( "不支持" ), tr( "暂不支持该格式：%1" ).arg( suffix ) );
    return;
  }

  if ( !pcLayer || !pcLayer->isValid() )
  {
    QMessageBox::warning( this, tr( "加载失败" ), tr( "图层无效，路径：%1" ).arg( filePath ) );
    if ( pcLayer )
      delete pcLayer;
    return;
  }
  removeLayerByName( layerName );
  QgsProject::instance()->addMapLayer( pcLayer );

  // 让2D地图缩放到点云范围，3D相机跟过去
  mIface->mapCanvas()->setExtent( pcLayer->extent() );
  mIface->mapCanvas()->refresh();

  // 自动打开3D视图
  if ( mIface->mapCanvases3D().isEmpty() )
  {
    Qgs3DMapCanvas *canvas3D = mIface->createNewMapCanvas3D( "ParamModeler 3D" );
    if ( canvas3D )
    {
      QDockWidget *dock = qobject_cast<QDockWidget *>( canvas3D->parent() );
      if ( dock )
      {
        dock->setFloating( true );
        dock->resize( 800, 600 );
      }
    }
  }

QMessageBox::information( this, tr( "加载成功" ), tr( "点云已加载！\n图层：%1\n\n可在3D视图中与模型叠加对比。" ).arg( layerName ) );
}
// ================================清理图层============================
// 在加载新模型前，根据名称添加并删除旧图层，通过excludeId确保不会误删当前正在使用的新图层。
void ParamModelerDock::removeLayerByName( const QString &name, const QString &excludeId )
{
    QStringList toRemove;
    const auto layers = QgsProject::instance()->mapLayers();
    for ( auto it = layers.cbegin(); it != layers.cend(); ++it )
    {
        if ( it.value()->name() == name && it.key() != excludeId )
            toRemove << it.key();
    }
    for ( const QString &id : toRemove )
        QgsProject::instance()->removeMapLayer( id );
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
double ParamModelerDock::cuboidWidth() const { return ui->spinBoxCWidth->value(); }
double ParamModelerDock::cuboidDepth() const { return ui->spinBoxCDepth->value(); }
double ParamModelerDock::cuboidHeight() const { return ui->spinBoxCHeight->value(); }
double ParamModelerDock::cylinderRadius() const { return ui->spinBoxCylRadius->value(); }
double ParamModelerDock::cylinderHeight() const { return ui->spinBoxCylHeight->value(); }
double ParamModelerDock::LMainWidth() const { return ui->spinBoxLMainWidth->value(); }
double ParamModelerDock::LMainDepth() const { return ui->spinBoxLMainDepth->value(); }
double ParamModelerDock::LWingWidth() const { return ui->spinBoxLWingWidth->value(); }
double ParamModelerDock::LWingDepth() const { return ui->spinBoxLWingDepth->value(); }
double ParamModelerDock::LHeight() const { return ui->spinBoxLHeight->value(); }
double ParamModelerDock::coneCylRadius() const { return ui->spinBoxConeCylRadius->value(); }
double ParamModelerDock::coneCylCylHeight() const { return ui->spinBoxConeCylCylHeight->value(); }
double ParamModelerDock::coneCylConeHeight() const { return ui->spinBoxConeCylConeHeight->value(); }
double ParamModelerDock::gabledRoofWidth() const { return ui->spinBoxGRWidth->value(); }
double ParamModelerDock::gabledRoofDepth() const { return ui->spinBoxGRDepth->value(); }
double ParamModelerDock::gabledRoofWallHeight() const { return ui->spinBoxGRHeightWall->value(); }
double ParamModelerDock::gabledRoofRoofHeight() const { return ui->spinBoxGRHeightRoof->value(); }
double ParamModelerDock::pyramidWidth() const { return ui->spinBoxPRWidth->value(); }
double ParamModelerDock::pyramidDepth() const { return ui->spinBoxPRDepth->value(); }
double ParamModelerDock::pyramidWallHeight() const { return ui->spinBoxPRHeightWall->value(); }
double ParamModelerDock::pyramidRoofHeight() const { return ui->spinBoxPRHeightRoof->value(); }
double ParamModelerDock::tpBottomWidth() const { return ui->spinBoxTPRBottomWidth->value(); }
double ParamModelerDock::tpBottomDepth() const { return ui->spinBoxTPRBottomDepth->value(); }
double ParamModelerDock::tpTopWidth() const { return ui->spinBoxTPRTopWidth->value(); }
double ParamModelerDock::tpTopDepth() const { return ui->spinBoxTPRTopDepth->value(); }
double ParamModelerDock::tpWallHeight() const { return ui->spinBoxTPRHeightWall->value(); }
double ParamModelerDock::tpRoofHeight() const { return ui->spinBoxTPRHeightRoof->value(); }
double ParamModelerDock::hcrWidth() const { return ui->spinBoxHCRWidth->value(); }
double ParamModelerDock::hcrDepth() const { return ui->spinBoxHCRDepth->value(); }
double ParamModelerDock::hcrWallHeight() const { return ui->spinBoxHCRHeightWall->value(); }
double ParamModelerDock::hcrRadius() const { return ui->spinBoxHCRRadius->value(); }
double ParamModelerDock::icOuterWidth() const { return ui->spinBoxICWidth->value(); }
double ParamModelerDock::icOuterDepth() const { return ui->spinBoxICDepth->value(); }
double ParamModelerDock::icOuterHeight() const { return ui->spinBoxICHeight->value(); }
double ParamModelerDock::icInnerWidth() const { return ui->spinBoxICInnerWidth->value(); }
double ParamModelerDock::icInnerDepth() const { return ui->spinBoxICInnerDepth->value(); }
double ParamModelerDock::icInnerHeight() const { return ui->spinBoxICInnerHeight->value(); }
double ParamModelerDock::icOffsetX() const { return ui->spinBoxICOffsetX->value(); }
double ParamModelerDock::icOffsetY() const { return ui->spinBoxICOffsetY->value(); }
double ParamModelerDock::aghWidth() const { return ui->spinBoxAGHWidth->value(); }
double ParamModelerDock::aghDepth() const { return ui->spinBoxAGHDepth->value(); }
double ParamModelerDock::aghWallHeight() const { return ui->spinBoxAGHHeightWall->value(); }
double ParamModelerDock::aghRoofHeight() const { return ui->spinBoxAGHRoofHeight->value(); }
double ParamModelerDock::aghRidgeLength() const { return ui->spinBoxAGHRidgeLength->value(); }
double ParamModelerDock::aghRidgeOffset() const { return ui->spinBoxAGHRidgeOffset->value(); }
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
double ParamModelerDock::tgWidth1() const { return ui->spinBoxTGWidth1->value(); }
double ParamModelerDock::tgWidth2() const { return ui->spinBoxTGWidth2->value(); }
double ParamModelerDock::tgDepth() const { return ui->spinBoxTGDepth->value(); }
double ParamModelerDock::tgWallHeight() const { return ui->spinBoxTGHeightWall->value(); }
double ParamModelerDock::tgRoofHeight() const { return ui->spinBoxTGRoofHeight->value(); }
double ParamModelerDock::tgAngle() const { return ui->spinBoxTGAngle->value(); }

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

    ui->labelInputInfo->setText( tr("已加载：%1").arg( fi.fileName() ) );
    ui->labelPrimitiveType->setText( tr("识别结果：-") );
    ui->tableInverseParams->setRowCount( 0 );

    // 加载完才能识别，识别完才能反演
    ui->btnClassifyPrimitive->setEnabled( true );
    ui->btnInverseParams->setEnabled(  false );
}
// ========================================================================
// 基元类型自动识别（第一版：规则 + 几何特征）
// ========================================================================
void ParamModelerDock::onClassifyPrimitive()
{
    if ( m_inputDataPath.isEmpty() ) return;

    // TODO: 真实特征提取替换这里的占位逻辑
    // 现阶段保留占位，流程跑通后再替换
    QString detectedType = "Cylinder";   // 占位
    double  confidence   = 0.87;         // 占位

    ui->labelPrimitiveType->setText(
        tr("识别结果：%1（%.0f%%）").arg( detectedType ).arg( confidence * 100 ) );

    // 直接同步到 Tab1 的基元选择，用户可以看到参数页切换
    ui->comboPrimitive->setCurrentText( detectedType );

    ui->btnInverseParams->setEnabled( true );  // 识别完才开放反演
}
// ========================================================================
// Tab2：参数反演（Mock版）
// ========================================================================
void ParamModelerDock::onInverseParams()
{
    if ( m_inputDataPath.isEmpty() ) return;

    ui->progressInversion->setVisible( true );
    ui->progressInversion->setValue( 0 );

    // TODO: 真实反演替换占位值
    QString prim = ui->comboPrimitive->currentText();

    if ( prim == "Cylinder" )
    {
        double radius = 5.0;   // 占位
        double height = 10.0;  // 占位

        ui->spinBoxCylRadius->setValue( radius );
        ui->spinBoxCylHeight->setValue( height );

        ui->tableInverseParams->setRowCount( 2 );
        ui->tableInverseParams->setItem( 0, 0, new QTableWidgetItem( tr("半径") ) );
        ui->tableInverseParams->setItem( 0, 1, new QTableWidgetItem( QString::number( radius ) ) );
        ui->tableInverseParams->setItem( 1, 0, new QTableWidgetItem( tr("高度") ) );
        ui->tableInverseParams->setItem( 1, 1, new QTableWidgetItem( QString::number( height ) ) );
    }
    // else if 其他基元 ...

    ui->progressInversion->setValue( 100 );
    ui->progressInversion->setVisible( false );

    // 反演完直接触发预览刷新
    onUpdatePreview();
}

