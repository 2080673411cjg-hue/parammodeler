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
#include "buildmesh.h"
#include "parammodeler_export.h"
#include "exportpointcloud.h"

#include "parammodeler_pcdloader.h"
#include "parammodeler_datasetgen.h"
#include "parammodeler_dlutils.h"
#include "parammodeler_randomizer.h"
#include "parammodeler_pointnet.h"
#include "parammodeler_scene3d.h"
#include "parammodeler_pick3d.h"
#include "parammodeler_pick3d_math.h"
#include "parammodeler_heightcontrol.h"
#include "qgsmessagelog.h"

#include <limits>
#include <algorithm>
#include <cmath>
#include <functional>

#include <QFileInfo>
#include <QDateTime>
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
#include <QCloseEvent>
#include <QObject>
#include <QEvent>
#include <QWheelEvent>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QProgressDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSizePolicy>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPainter>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStringList>
#include <QJsonArray>
#include <QColor>


#include <qgis.h>
#include <qgsapplication.h>
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
#include "qgsmaptoolemitpoint.h"
#include "qgsmaptool.h"
#include "qgsrubberband.h"
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

// ============================================================
// 分组分隔线：绘制 "———— Roof ————" 风格的虚线分隔标题
// 左右虚线延伸到边缘，文字居中，背后有背景色"断线"留白
// ============================================================
class SectionDivider : public QWidget
{
public:
    explicit SectionDivider( const QString &text, QWidget *parent = nullptr )
        : QWidget( parent ), m_text( text )
    {
        setFixedHeight( 22 );
        setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    }

protected:
    void paintEvent( QPaintEvent * ) override
    {
        QPainter p( this );
        p.setRenderHint( QPainter::Antialiasing, true );

        QFont f = font();
        f.setBold( true );
        f.setPixelSize( 11 );
        p.setFont( f );
        QFontMetrics fm( f );

        const int textW = fm.horizontalAdvance( m_text );
        const int pad   = 12;                                // 文字两侧留白
        const int gapW  = textW + pad * 2;                   // 断线总宽度
        const int y     = height() / 2;

        // 虚线（全宽），文字区域会被背景覆盖形成"断线"效果
        QPen linePen( QColor( 0xaa, 0xaa, 0xaa ), 1, Qt::DashLine );
        p.setPen( linePen );
        p.drawLine( 0, y, width(), y );

        // 用背景色擦除文字背后的线段
        QColor bg = palette().window().color();
        p.setPen( Qt::NoPen );
        p.setBrush( bg );
        const double left = ( width() - gapW ) / 2.0;
        p.drawRect( QRectF( left, y - fm.height() / 2.0, gapW, fm.height() ) );

        // 文字
        p.setPen( QColor( 0x88, 0x88, 0x88 ) );
        p.setBrush( Qt::NoBrush );
        p.drawText( QRectF( 0, 0, width(), height() ), Qt::AlignCenter, m_text );
    }

private:
    QString m_text;
};

// ============================================================
// 微调辅助：Ctrl+滚轮 = 10x 精调（步长缩小为 1/10）
// 安装到所有 QDoubleSpinBox，在微调时提供更细粒度的控制
// ============================================================
class FineTuneFilter : public QObject
{
public:
    explicit FineTuneFilter( QDoubleSpinBox *spin )
        : QObject( spin ), m_spin( spin ) {}

protected:
    bool eventFilter( QObject * /*obj*/, QEvent *event ) override
    {
        if ( event->type() == QEvent::Wheel )
        {
            QWheelEvent *we = static_cast<QWheelEvent *>( event );
            if ( we->modifiers() & Qt::ControlModifier )
            {
                // 10x 精调
                const double fineStep = m_spin->singleStep() * 0.1;
                const int delta = we->angleDelta().y();
                if ( delta > 0 )
                    m_spin->setValue( m_spin->value() + fineStep );
                else if ( delta < 0 )
                    m_spin->setValue( m_spin->value() - fineStep );
                return true;  // 事件已处理，不再传递
            }
        }
        return false;
    }

private:
    QDoubleSpinBox *m_spin;
};

static void bindSliderSpin( QSlider *slider, QDoubleSpinBox *spin, double multiplier, double maxVal = 100.0, double minVal = 0.0 )
{
  if ( !slider || !spin )
    return;

  spin->setRange( minVal, maxVal );
  spin->setSingleStep( 1.0 / multiplier );

  // Ctrl+滚轮精调（10x 精度）
  spin->installEventFilter( new FineTuneFilter( spin ) );

  slider->setRange( static_cast<int>( minVal * multiplier ), static_cast<int>( maxVal * multiplier ) );
  QObject::connect( slider, &QSlider::valueChanged, spin, [spin, multiplier]( int v ) {
    double val = static_cast<double>( v ) / multiplier;
    if ( std::abs( spin->value() - val ) > 0.0001 )
    {
      spin->setValue( val );
    }
  } );
  QObject::connect( spin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), slider, [slider, multiplier]( double v ) {
    int val = qRound( v * multiplier );
    if ( slider->value() != val )
    {
      // Updating the coarse slider must not quantize a fine spin-box edit back.
      const QSignalBlocker blocker( slider );
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

  initUiControls();
  initHeightDirectionControls();
  initConnections();
  initPreview();
  initPointNet();

  ui->btnPointNetClassify->setEnabled( false );
  ui->btnInverseParams->setEnabled( false );

  DEBUG_LOG( L"\n[ParamModelerDock] initialization complete\n" );
  DEBUG_LOG( m_currentPrimitive.toStdWString().c_str() );
  DEBUG_LOG( L"\n" );
}

ParamModelerDock::~ParamModelerDock()
{
  stopManualTranslate3D();
  QgsMapCanvas *canvas = mIface ? mIface->mapCanvas() : nullptr;
  if ( canvas && m_manualTranslateTool && canvas->mapTool() == m_manualTranslateTool )
  {
    if ( m_previousMapTool )
      canvas->setMapTool( m_previousMapTool );
    else
      canvas->unsetMapTool( m_manualTranslateTool );
  }
  if ( m_manualTranslateSourceMarker )
  {
    delete m_manualTranslateSourceMarker;
    m_manualTranslateSourceMarker = nullptr;
  }
  if ( m_manualTranslateXAxisMarker )
  {
    delete m_manualTranslateXAxisMarker;
    m_manualTranslateXAxisMarker = nullptr;
  }
  if ( m_manualTranslateYAxisMarker )
  {
    delete m_manualTranslateYAxisMarker;
    m_manualTranslateYAxisMarker = nullptr;
  }
  m_manualTranslateHasSource = false;
  ParamModelerScene3D::clearRealtimePreviewMesh( mIface );
  m_modelLayer = nullptr;
  delete ui;
}

// ======================= init helpers =======================

// ============================================================
// 参数分组：在复杂基元（4+ 参数）的 QFormLayout 中插入 "———— Title ————" 分隔线
// 使用自定义 SectionDivider widget，绘制全宽虚线 + 居中加粗文字
// QFormLayout::insertRow(widget) 使用 SpanningRole 占满两列，
// 但 SectionDivider 固定高度 22px，不影响其他行的双列宽度计算
// ============================================================
static void insertSectionHeader( QFormLayout *form, int row, const QString &title )
{
    if ( !form || row < 0 || row > form->rowCount() )
        return;
    auto *divider = new SectionDivider( title );
    form->insertRow( row, divider );
}

static void applyParameterGroups( Ui::ParamModelerDock *ui )
{
    auto fmt = [&]( QWidget *page ) -> QFormLayout * {
        return page ? qobject_cast<QFormLayout *>( page->layout() ) : nullptr;
    };

    // 插入顺序：从底部到顶部（避免索引偏移）

    // GabledRoof: Body(L,W), Roof(Total height, Wall ratio, Ridge ratio).
    if ( QFormLayout *f = fmt( ui->pageGabledRoof ) )
    {
        insertSectionHeader( f, 2, ParamModelerDock::tr( "Roof / Height" ) );
        insertSectionHeader( f, 0, ParamModelerDock::tr( "Body" ) );
    }

    // PyramidRoof (4 rows): Body(L,W) → Roof(Total height, Wall ratio)
    if ( QFormLayout *f = fmt( ui->pagePyramidRoof ) )
    {
        insertSectionHeader( f, 2, ParamModelerDock::tr( "Roof / Height" ) );
        insertSectionHeader( f, 0, ParamModelerDock::tr( "Body" ) );
    }

    // CylinderDome (4 rows): Cylinder(R, Total H, Cyl ratio) → Dome(Bulge)
    if ( QFormLayout *f = fmt( ui->pageCylinderHemisphere ) )
    {
        insertSectionHeader( f, 3, ParamModelerDock::tr( "Dome" ) );
        insertSectionHeader( f, 0, ParamModelerDock::tr( "Cylinder" ) );
    }

    // TriPrismPyramid (4 rows): Prism(Leg, Base) → Pyramid(Total H, Pyramid ratio)
    if ( QFormLayout *f = fmt( ui->pageTriPrismPyramid ) )
    {
        insertSectionHeader( f, 2, ParamModelerDock::tr( "Pyramid" ) );
        insertSectionHeader( f, 0, ParamModelerDock::tr( "Prism" ) );
    }

    // LHouse (5 rows)：不插分组标题，因为 Height 横跨 Body/Wing 两组语义

    // TruncatedPyramidRoof (6 rows): BottomL,BottomW → TopL,TopW → Height
    if ( QFormLayout *f = fmt( ui->pageTPRoof ) )
    {
        insertSectionHeader( f, 4, ParamModelerDock::tr( "Height" ) );
        insertSectionHeader( f, 2, ParamModelerDock::tr( "Top" ) );
        insertSectionHeader( f, 0, ParamModelerDock::tr( "Bottom" ) );
    }

    // AsymmetricGableHouse (6 rows): Body(L,W) → Roof(Total H, Wall ratio) → Ridge
    if ( QFormLayout *f = fmt( ui->pageAsymmetricGableHouse ) )
    {
        insertSectionHeader( f, 4, ParamModelerDock::tr( "Ridge" ) );
        insertSectionHeader( f, 2, ParamModelerDock::tr( "Roof" ) );
        insertSectionHeader( f, 0, ParamModelerDock::tr( "Body" ) );
    }

    // FourStageRoundTower: Base -> Middle -> Top -> Eave
    if ( QFormLayout *f = fmt( ui->pageFourStageRoundTower ) )
    {
        insertSectionHeader( f, 6, ParamModelerDock::tr( "Eave" ) );
        insertSectionHeader( f, 5, ParamModelerDock::tr( "Top" ) );
        insertSectionHeader( f, 2, ParamModelerDock::tr( "Middle" ) );
        insertSectionHeader( f, 0, ParamModelerDock::tr( "Base" ) );
    }

    // TwoGableHouses (7 rows): Body(L1,L2,W) → Roof → Orientation
    if ( QFormLayout *f = fmt( ui->pageTwoGableHouses ) )
    {
        insertSectionHeader( f, 5, ParamModelerDock::tr( "Orientation" ) );
        insertSectionHeader( f, 3, ParamModelerDock::tr( "Roof" ) );
        insertSectionHeader( f, 0, ParamModelerDock::tr( "Body" ) );
    }

    // IndentedCuboid (8 rows): Outer(L,W,H) → Inner → Offset
    if ( QFormLayout *f = fmt( ui->pageIndentedCuboid ) )
    {
        insertSectionHeader( f, 6, ParamModelerDock::tr( "Offset" ) );
        insertSectionHeader( f, 3, ParamModelerDock::tr( "Inner" ) );
        insertSectionHeader( f, 0, ParamModelerDock::tr( "Outer" ) );
    }
}

void ParamModelerDock::initUiControls()
{
  bindSliderSpin( ui->sliderCLength, ui->spinBoxCLength, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCWidth, ui->spinBoxCWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCHeight, ui->spinBoxCHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCylRadius, ui->spinBoxCylRadius, 100.0, 50.0 );
  bindSliderSpin( ui->sliderCylHeight, ui->spinBoxCylHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLTotalLength, ui->spinBoxLTotalLength, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLTotalWidth, ui->spinBoxLTotalWidth, 100.0, 50.0 );
  bindSliderSpin( ui->sliderLWingRatio, ui->spinBoxLWingRatio, 100.0, 0.9, 0.2 );
  bindSliderSpin( ui->sliderLWingWidthRatio, ui->spinBoxLWingWidthRatio, 100.0, 0.9, 0.2 );
  bindSliderSpin( ui->sliderLHeight, ui->spinBoxLHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderConeCylRadius, ui->spinBoxConeCylRadius, 100.0, 50.0 );
  bindSliderSpin( ui->sliderConeCylCylHeight, ui->spinBoxConeCylCylHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderConeCylConeHeight, ui->spinBoxConeCylConeHeight, 100.0, 0.90, 0.20 );
  bindSliderSpin( ui->sliderGRLength, ui->spinBoxGRLength, 100, 50 );
  bindSliderSpin( ui->sliderGRWidth, ui->spinBoxGRWidth, 100, 50 );
  bindSliderSpin( ui->sliderGRHeightWall, ui->spinBoxGRHeightWall, 100.0, 50.0 );
  bindSliderSpin( ui->sliderGRHeightRoof, ui->spinBoxGRHeightRoof, 100.0, 0.90, 0.20 );
  bindSliderSpin( ui->sliderGRRidgeRatio, ui->spinBoxGRRidgeRatio, 100.0, 0.80, 0.20 );
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
  bindSliderSpin( ui->sliderICLength, ui->spinBoxICLength, 100, 50 );
  bindSliderSpin( ui->sliderICWidth, ui->spinBoxICWidth, 100, 50 );
  bindSliderSpin( ui->sliderICHeight, ui->spinBoxICHeight, 100, 50 );
  bindSliderSpin( ui->sliderICInnerLength, ui->spinBoxICInnerLength, 100, 50 );
  bindSliderSpin( ui->sliderICInnerWidth, ui->spinBoxICInnerWidth, 100, 50 );
  bindSliderSpin( ui->sliderICInnerHeight, ui->spinBoxICInnerHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderICOffsetX, ui->spinBoxICOffsetX, 100.0, 1.0, 0.0 );
  bindSliderSpin( ui->sliderICOffsetY, ui->spinBoxICOffsetY, 100.0, 1.0, 0.0 );
  ui->spinBoxICOffsetX->setDecimals( 6 );
  ui->spinBoxICOffsetY->setDecimals( 6 );
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
  bindSliderSpin( ui->sliderFTEaveRadius, ui->spinBoxFTEaveRadius, 1000.0, 60.0 );
  bindSliderSpin( ui->sliderFTEaveThickness, ui->spinBoxFTEaveThickness, 1000.0, 50.0 );
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

  bindSliderSpin( ui->sliderTPPLeg, ui->spinBoxTPPLeg, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTPPBase, ui->spinBoxTPPBase, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTPPHeight, ui->spinBoxTPPHeight, 100.0, 50.0 );
  bindSliderSpin( ui->sliderTPPRatio, ui->spinBoxTPPRatio, 100.0, 0.90, 0.20 );

  bindSliderSpin( ui->sliderROmega, ui->spinBoxROmega, 10.0, 180.0, -180.0 );
  bindSliderSpin( ui->sliderRPhi, ui->spinBoxRPhi, 10.0, 180.0, -180.0 );
  bindSliderSpin( ui->sliderRKappa, ui->spinBoxRKappa, 10.0, 180.0, -180.0 );

  bindSliderSpin( ui->sliderTX, ui->spinBoxTX, 10.0, 100.0, -100.0 );
  bindSliderSpin( ui->sliderTY, ui->spinBoxTY, 10.0, 100.0, -100.0 );
  bindSliderSpin( ui->sliderTZ, ui->spinBoxTZ, 10.0, 100.0, -100.0 );
  const auto allowWorldTranslation = [this]( QSlider *slider, QDoubleSpinBox *spin ) {
    spin->setRange( -100000000.0, 100000000.0 );
    QObject::connect( spin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
                      slider, [slider]( double value ) {
      slider->setEnabled( value >= -100.0 && value <= 100.0 );
    } );
  };
  allowWorldTranslation( ui->sliderTX, ui->spinBoxTX );
  allowWorldTranslation( ui->sliderTY, ui->spinBoxTY );
  allowWorldTranslation( ui->sliderTZ, ui->spinBoxTZ );

  ui->labelWidth->setText( tr( "Width:" ) );
  ui->labelHeight->setText( tr( "Height:" ) );
  ui->labelLength->setText( tr( "Length:" ) );
  ui->labelCylRadius->setText( tr( "Radius:" ) );
  ui->labelCylHeight->setText( tr( "Height:" ) );
  ui->labelLTotalLength->setText( tr( "Total length:" ) );
  ui->labelLTotalWidth->setText( tr( "Total width:" ) );
  ui->labelLWingRatio->setText( tr( "Wing length ratio:" ) );
  ui->labelLWingWidthRatio->setText( tr( "Wing width ratio:" ) );
  ui->labelLHeight->setText( tr( "Height:" ) );
  ui->labelConeCylRadius->setText( tr( "Radius:" ) );
  ui->labelConeCylCylHeight->setText( tr( "Total height:" ) );
  ui->labelConeCylConeHeight->setText( tr( "Cylinder ratio:" ) );
  ui->labelGRLength->setText( tr( "Length:" ) );
  ui->labelGRWidth->setText( tr( "Width:" ) );
  ui->labelGRHeightWall->setText( tr( "Total height:" ) );
  ui->labelGRHeightRoof->setText( tr( "Wall ratio:" ) );
  ui->labelGRRidgeRatio->setText( tr( "Ridge ratio:" ) );
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

  ui->labelTPPLeg->setText( tr( "Leg length:" ) );
  ui->labelTPPBase->setText( tr( "Base length:" ) );
  ui->labelTPPHeight->setText( tr( "Total height:" ) );
  ui->labelTPPRatio->setText( tr( "Pyramid ratio:" ) );

  // ---- 参数分组标题：在复杂基元（4+ 参数）的 QFormLayout 中插入加粗分隔线 ----
  applyParameterGroups( ui );
}

void ParamModelerDock::initHeightDirectionControls()
{
  auto add = [this]( QDoubleSpinBox *spin, QSlider *slider, const QString &upper,
                    bool recess = false ) {
    QHBoxLayout *row = nullptr;
    for ( QHBoxLayout *candidate : spin->parentWidget()->findChildren<QHBoxLayout *>() )
      if ( candidate->indexOf( spin ) >= 0 ) { row = candidate; break; }
    if ( !row ) return;
    new ParamModelerHeightControl( spin, slider, row, recess ? -1.0 : 0.0, recess ? 0.0 : 1.0,
      recess, recess ? tr( "recess floor" ) : tr( "lower end" ), upper,
      [this, spin] {
        return !m_suspendHeightCompensation && !m_isUpdating &&
               ui->stackedWidgetParams->currentWidget()->isAncestorOf( spin );
      },
      [this]( double localZ ) {
        const QVector3D delta = modelRotationMatrix( this ).mapVector( QVector3D( 0, 0, localZ ) );
        const double x = poseTranslateX() + delta.x();
        const double y = poseTranslateY() + delta.y();
        const double z = poseTranslateZ() + delta.z();
        if ( x < ui->spinBoxTX->minimum() || x > ui->spinBoxTX->maximum() ||
             y < ui->spinBoxTY->minimum() || y > ui->spinBoxTY->maximum() ||
             z < ui->spinBoxTZ->minimum() || z > ui->spinBoxTZ->maximum() ) return false;
        setPoseTranslate( x, y, z );
        return true;
      } );
  };
  add( ui->spinBoxCHeight, ui->sliderCHeight, tr( "top face" ) );
  add( ui->spinBoxCylHeight, ui->sliderCylHeight, tr( "top face" ) );
  add( ui->spinBoxLHeight, ui->sliderLHeight, tr( "top face" ) );
  add( ui->spinBoxConeCylCylHeight, ui->sliderConeCylCylHeight, tr( "cone apex" ) );
  add( ui->spinBoxGRHeightWall, ui->sliderGRHeightWall, tr( "roof ridge" ) );
  add( ui->spinBoxPRHeightWall, ui->sliderPRHeightWall, tr( "roof apex" ) );
  add( ui->spinBoxTPRHeightWall, ui->sliderTPRHeightWall, tr( "roof top face" ) );
  add( ui->spinBoxHCRHeightWall, ui->sliderHCRHeightWall, tr( "eaves and roof" ) );
  add( ui->spinBoxICHeight, ui->sliderICHeight, tr( "outer top face" ) );
  add( ui->spinBoxICInnerHeight, ui->sliderICInnerHeight, tr( "recess rim" ), true );
  add( ui->spinBoxAGHHeightWall, ui->sliderAGHHeightWall, tr( "roof ridge" ) );
  add( ui->spinBoxCylHemiHeight, ui->sliderCylHemiHeight, tr( "dome apex" ) );
  add( ui->spinBoxFTBaseHeight, ui->sliderFTBaseHeight, tr( "base section top" ) );
  add( ui->spinBoxFTMiddleHeight, ui->sliderFTMiddleHeight, tr( "middle section top" ) );
  add( ui->spinBoxFTConeHeight, ui->sliderFTConeHeight, tr( "cone apex" ) );
  add( ui->spinBoxTGHeightWall, ui->sliderTGHeightWall, tr( "roof ridges" ) );
}

void ParamModelerDock::initConnections()
{
  bindEaveDimension( ui->spinBoxFTBaseRadius, ui->spinBoxFTEaveRadius, ui->sliderFTEaveRadius, true, 60.0 );
  bindEaveDimension( ui->spinBoxFTBaseHeight, ui->spinBoxFTEaveThickness, ui->sliderFTEaveThickness, false, 50.0 );
  bindRecessDepthLimit( ui->spinBoxICHeight, ui->spinBoxICInnerHeight,
                        ui->sliderICInnerHeight, m_suspendHeightCompensation );
  const auto bindFixedInnerCorner = [this]( QDoubleSpinBox *inner, QSlider *slider,
                                           QDoubleSpinBox *outer, QDoubleSpinBox *offset ) {
    inner->setKeyboardTracking( false );
    connect( inner, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this,
      [this, inner, slider, outer, offset, previous = inner->value()]( double value ) mutable {
        const double oldSize = previous;
        previous = value;
        if ( m_suspendHeightCompensation || m_isUpdating ||
             !ui->stackedWidgetParams->currentWidget()->isAncestorOf( inner ) )
          return;

        // Ratios encode position; resize about the existing physical corner.
        const double fixedOffset = offset->value() * std::max( 0.0, outer->value() - oldSize );
        const double maxSize = std::max( 0.0, outer->value() - fixedOffset );
        if ( value > maxSize )
        {
          const double units = std::pow( 10.0, inner->decimals() );
          value = std::floor( maxSize * units + 1e-8 ) / units;
          const QSignalBlocker spinBlocker( inner ), sliderBlocker( slider );
          inner->setValue( value );
          slider->setValue( qRound( inner->value() * 100.0 ) );
          previous = inner->value();
        }
        const double movable = std::max( 0.0, outer->value() - inner->value() );
        offset->setValue( movable > 1e-6 ? qBound( 0.0, fixedOffset / movable, 1.0 ) : 0.0 );
      } );
  };
  bindFixedInnerCorner( ui->spinBoxICInnerLength, ui->sliderICInnerLength,
                       ui->spinBoxICLength, ui->spinBoxICOffsetX );
  bindFixedInnerCorner( ui->spinBoxICInnerWidth, ui->sliderICInnerWidth,
                       ui->spinBoxICWidth, ui->spinBoxICOffsetY );

  auto syncHCRRadius = [this]() {
    ui->spinBoxHCRRadius->setValue( ui->spinBoxHCRWidth->value() / 2.0 );
  };
  syncHCRRadius();
  connect( ui->spinBoxHCRWidth, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [syncHCRRadius]( double ) { syncHCRRadius(); } );

  connect( ui->actOBJ, &QAction::triggered, this, &ParamModelerDock::onExportOBJClicked );
  connect( ui->actJSON, &QAction::triggered, this, &ParamModelerDock::onExportJSONClicked );
  ui->actEvaluationCSV->setIcon( QgsApplication::getThemeIcon( QStringLiteral( "/mActionFileSave.svg" ) ) );
  connect( ui->actEvaluationCSV, &QAction::triggered, this, &ParamModelerDock::onExportEvaluationCsv );
  const auto updateMenuAvailability = [this]() {
    ui->actLoadedDLPointCloud->setEnabled( !m_inputDataPath.isEmpty() && QFileInfo( m_inputDataPath ).isFile() );
    bool hasSceneContent = m_realtimeModelLoaded || !m_pointCloudLayer.isNull();
    const auto layers = QgsProject::instance()->mapLayers();
    for ( QgsMapLayer *layer : layers )
    {
      const QString name = layer->name();
      hasSceneContent = hasSceneContent
        || name == QStringLiteral( "ParamModeler_Model" )
        || name == QStringLiteral( "ParamModeler_Model_Roof" )
        || name == QStringLiteral( "ParamModeler_Model_Edges" )
        || name == QStringLiteral( "ParamModeler_3D_Anchor" )
        || name.startsWith( QStringLiteral( "External point cloud - " ) );
    }
    ui->actClear3D->setEnabled( hasSceneContent );
  };
  connect( ui->menuDataset, &QMenu::aboutToShow, this, updateMenuAvailability );
  connect( ui->menuLoad3D, &QMenu::aboutToShow, this, updateMenuAvailability );
  updateMenuAvailability();
  QMenu *helpMenu = ui->menuBar->addMenu( tr( "Help" ) );
  QAction *usageAction = helpMenu->addAction( tr( "Plugin usage..." ) );
  usageAction->setIcon( QgsApplication::getThemeIcon( QStringLiteral( "/mActionHelpContents.svg" ) ) );
  connect( usageAction, &QAction::triggered, this, [this]() {
    QMessageBox::information(
      this,
      tr( "Plugin usage" ),
      tr( "Basic workflow:\n"
          "1. Load an input point cloud.\n"
          "2. Use Classify and estimate parameters... or Complete all.\n"
          "3. Complete all loads the model and point cloud into 3D and applies automatic alignment.\n"
          "4. Use Align in 3D for point picking, or Fit corner from three faces... for missing-corner cases.\n"
          "5. Fine-tune parameters, position and rotation in the main panel.\n"
          "6. Export model files or Current evaluation CSV from the Export menu.\n\n"
          "Fine tuning:\n"
          "- IndentedCuboid automatically aligns the inner top-left corner after outer alignment; inner length, width and depth keep their estimated values.\n"
          "- Adjusting inner length or width keeps this corner fixed and moves the opposite edge, up to the outer boundary.\n"
          "- Height arrows select the fixed end; the inner recess depth defaults to keeping its top rim fixed.\n\n"
          "Notes:\n"
          "- Geometry correction is optional parameter refinement, disabled by default; automatic alignment works with it disabled.\n"
          "- FourStageRoundTower uses the full point-cloud extent to estimate its horizontal center.\n"
          "- Wireframe helps compare the model with the point cloud.\n"
          "- Reset prediction restores the last deep-learning parameter result.\n"
          "- Three-face fitting moves the model only; it does not resize or rotate it." )
    );
  } );
  connect( ui->actPLY, &QAction::triggered, this, &ParamModelerDock::onExportPLYClicked );
  connect( ui->actDLPointCloud, &QAction::triggered, this, &ParamModelerDock::onExportDLPointCloudClicked );
  connect( ui->actLoadedDLPointCloud, &QAction::triggered, this, &ParamModelerDock::onExportLoadedDLPointCloudClicked );
  connect( ui->actDLDataset, &QAction::triggered, this, &ParamModelerDock::onExportDLDatasetClicked );
  connect( ui->actCurrentPrimitiveDLDataset, &QAction::triggered, this, &ParamModelerDock::onExportCurrentPrimitiveDLDatasetClicked );
  connect( ui->actCompleteSelectedDLDatasets, &QAction::triggered, this, &ParamModelerDock::onCompleteSelectedDLDatasetsClicked );
  connect( ui->actMesh, &QAction::triggered, this, &ParamModelerDock::onExportMeshClicked );
  connect( ui->actTo3D, &QAction::triggered, this, [this]() { onLoadToQGIS3D( true ); } );
  connect( ui->actLoadPC, &QAction::triggered, this, &ParamModelerDock::onLoadExternalPointCloud );
  connect( ui->actClear3D, &QAction::triggered, this, [this]() {
    stopManualTranslate3D();
    // 1. 先清理 3D 场景的 settings layers（在删除 QgsProject 之前做）
    const QList<Qgs3DMapCanvas *> canvases = mIface->mapCanvases3D();
    for ( Qgs3DMapCanvas *canvas : canvases )
    {
      if ( !canvas ) continue;
      Qgs3DMapSettings *s = canvas->mapSettings();
      if ( !s ) continue;
      QList<QgsMapLayer *> layers = s->layers();
      // 移除所有插件相关的图层
      layers.erase(
        std::remove_if( layers.begin(), layers.end(),
                        []( QgsMapLayer *l ) {
                          if ( !l ) return true;
                          const QString name = l->name();
                          return name == QStringLiteral( "ParamModeler_Model" )
                              || name == QStringLiteral( "ParamModeler_Model_Roof" )
                              || name == QStringLiteral( "ParamModeler_Model_Edges" )
                              || name == QStringLiteral( "ParamModeler_3D_Anchor" )
                              || name.startsWith( QStringLiteral( "External point cloud - " ) );
                        } ),
        layers.end() );
      s->setLayers( layers );
    }

    // 2. 清除 Qt3D 预览实体 + 从 QgsProject 移除图层
    ParamModelerScene3D::clearAll3DEntities( mIface );

    // 3. 确保点云图层彻底删除
    if ( m_pointCloudLayer )
    {
      QgsProject::instance()->removeMapLayer( m_pointCloudLayer->id() );
      m_pointCloudLayer = nullptr;
    }

    m_realtimeModelLoaded = false;
    m_modelLayer = nullptr;
  } );

  auto schedulePreview = [this]( int ) { schedulePreviewUpdate(); };
  auto schedulePreviewD = [this]( double ) { schedulePreviewUpdate(); };
  auto schedulePoseTranslationPreview = [this]( double ) {
    if ( !m_anchorLockCompensating )
      retargetAnchorLockToCurrentPosition();
    schedulePreviewUpdate();
  };
  for ( QDoubleSpinBox *spin : ui->stackedWidgetParams->findChildren<QDoubleSpinBox *>() )
    connect( spin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, schedulePreviewD );

  connect( ui->spinBoxROmega, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, schedulePreviewD );
  connect( ui->spinBoxRPhi, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, schedulePreviewD );
  connect( ui->spinBoxRKappa, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, schedulePreviewD );
  connect( ui->spinBoxTX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schedulePoseTranslationPreview );
  connect( ui->spinBoxTY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schedulePoseTranslationPreview );
  connect( ui->spinBoxTZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schedulePoseTranslationPreview );

  connect( ui->sliderCLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylRadius, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderCylHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLTotalLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLTotalWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLWingRatio, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLWingWidthRatio, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderLHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderConeCylRadius, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderConeCylCylHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderConeCylConeHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRLength, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRWidth, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRHeightWall, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRHeightRoof, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderGRRidgeRatio, &QSlider::valueChanged, this, schedulePreview );
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

  connect( ui->sliderTPPLeg, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPPBase, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPPHeight, &QSlider::valueChanged, this, schedulePreview );
  connect( ui->sliderTPPRatio, &QSlider::valueChanged, this, schedulePreview );

  connect( ui->comboPrimitive, &QComboBox::currentTextChanged, this, &ParamModelerDock::onPrimitiveChanged );
  connect( ui->comboPrimitive, &QComboBox::currentTextChanged, this, [this]( const QString & ) { onUpdatePreview(); } );
  connect( ui->btnRandomParams, &QPushButton::clicked, this, &ParamModelerDock::onRandomizeCurrentPrimitive );
  connect( ui->btnWorkflowEstimate, &QPushButton::clicked, this, &ParamModelerDock::onOpenPointCloudEstimateDialog );

  connect( ui->btnLoadPointCloud, &QPushButton::clicked, this, &ParamModelerDock::onLoadInputData );
  connect( ui->btnPointNetClassify, &QPushButton::clicked, this, &ParamModelerDock::onPointNetClassify );
  connect( ui->btnInverseParams, &QPushButton::clicked, this, &ParamModelerDock::onInverseParams );
}

void ParamModelerDock::initPreview()
{
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
}

void ParamModelerDock::initPointNet()
{
  ui->widgetInversionPanel->setVisible( false );
  ui->widgetInversionPanel->setMinimumWidth( 0 );
  ui->widgetInversionPanel->setMaximumWidth( 0 );
  ui->frameInversion->setVisible( false );
  ui->btnWorkflowEstimate->setCheckable( false );
  ui->btnWorkflowEstimate->setFlat( false );
  ui->btnWorkflowEstimate->setStyleSheet( QString() );
  ui->btnWorkflowEstimate->setText( tr( "Classify and estimate parameters..." ) );
  ui->btnWorkflowEstimate->setIcon( QgsApplication::getThemeIcon( QStringLiteral( "/mIconPointCloudLayer.svg" ) ) );
  ui->btnPointNetClassify->setText( tr( "Classify" ) );
  ui->btnInverseParams->setText( tr( "Estimate parameters" ) );
  ui->labelPointCloudWorkflowTitle->setText( tr( "Point Cloud Tools" ) );

  // 线框模式复选框：微调参数时只显示模型边线，不遮挡点云
  mWireframeModeCheckBox = ui->checkBoxWireframe;
  mWireframeModeCheckBox->setText( tr( "Wireframe" ) );
  mWireframeModeCheckBox->setChecked( false );
  mWireframeModeCheckBox->setToolTip( tr( "Show only model edges so the point cloud is fully visible during fine-tuning." ) );
  connect( mWireframeModeCheckBox, &QCheckBox::toggled, this, [this]( bool checked ) {
    ParamModelerScene3D::setWireframeMode( checked );
    if ( m_realtimeModelLoaded )
      onUpdatePreview();
  } );

  // DL预测值复位按钮：一键将所有参数恢复到深度学习推理结果
  m_resetAnchorBtn = new QPushButton( tr( "Reset prediction" ), this );
  m_resetAnchorBtn->setIcon( QgsApplication::getThemeIcon( QStringLiteral( "/mActionUndo.svg" ) ) );
  m_resetAnchorBtn->setToolTip( tr( "Restore all shape parameters to the last deep-learning inference result." ) );
  m_resetAnchorBtn->setEnabled( false );  // 初始禁用，等推理成功后再启用
  const auto randomRow = ui->formLayoutPrimitive->takeRow( ui->btnRandomParams );
  delete randomRow.labelItem;
  delete randomRow.fieldItem;
  ui->btnRandomParams->setText( tr( "Randomize" ) );
  ui->btnRandomParams->setIcon( QgsApplication::getThemeIcon( QStringLiteral( "/algorithms/mAlgorithmSelectRandom.svg" ) ) );
  auto *parameterActions = new QHBoxLayout();
  parameterActions->setSpacing( 6 );
  parameterActions->addWidget( ui->btnRandomParams, 1 );
  parameterActions->addWidget( m_resetAnchorBtn, 1 );
  ui->layoutParameters->insertLayout( 0, parameterActions );
  // Shape parameters are the primary editing surface; pose follows below them.
  ui->verticalLayoutScrollTab1->removeWidget( ui->groupBoxTransform );
  ui->verticalLayoutScrollTab1->insertWidget(
    ui->verticalLayoutScrollTab1->indexOf( ui->splitterParams ) + 1, ui->groupBoxTransform );
  ui->groupBoxTransform->setTitle( tr( "Position and Rotation" ) );
  ui->verticalLayoutScrollTab1->setAlignment( Qt::AlignTop );
  auto fitParameterPage = [this]( int ) {
    QWidget *page = ui->stackedWidgetParams->currentWidget();
    if ( !page ) return;
    const int height = std::max( page->sizeHint().height(), page->minimumSizeHint().height() );
    ui->stackedWidgetParams->setFixedHeight( height + 2 * ui->stackedWidgetParams->frameWidth() );
  };
  connect( ui->stackedWidgetParams, &QStackedWidget::currentChanged, this, fitParameterPage );
  fitParameterPage( ui->stackedWidgetParams->currentIndex() );
  connect( m_resetAnchorBtn, &QPushButton::clicked, this, &ParamModelerDock::resetToDlAnchor );

  m_manualTranslateBtn = ui->btnAlign2D;
  m_manualTranslateBtn->setText( tr( "Align in 2D" ) );
  m_manualTranslateBtn->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
  m_manualTranslateBtn->setIcon( QgsApplication::getThemeIcon( QStringLiteral( "/mActionIdentify.svg" ) ) );
  m_manualTranslateBtn->setToolTip( tr( "Shows the current model anchor as a red X in the 2D map canvas. Click the point-cloud target position to translate the model there. Only TX/TY are changed." ) );
  connect( m_manualTranslateBtn, &QPushButton::clicked, this, &ParamModelerDock::startManualTranslateByClick );

  ui->widgetAlign3DControls->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
  ui->layoutAlignmentButtons->setStretch( 0, 1 );
  ui->layoutAlignmentButtons->setStretch( 1, 1 );
  ui->layoutAlignmentButtons->setStretch( 2, 0 );
  m_manualTranslate3DBtn = ui->btnAlign3D;
  m_manualTranslate3DBtn->setText( tr( "Align in 3D" ) );
  m_manualTranslate3DBtn->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
  auto *align3DMenuBtn = ui->btnAlign3DMenu;
  align3DMenuBtn->setArrowType( Qt::DownArrow );
  align3DMenuBtn->setPopupMode( QToolButton::InstantPopup );
  align3DMenuBtn->setFixedWidth( 24 );
  align3DMenuBtn->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
  align3DMenuBtn->setToolTip( tr( "More 3D alignment options" ) );
  auto *alignMenu = new QMenu( align3DMenuBtn );
  auto *anchorMenu = alignMenu->addMenu( tr( "Anchor point" ) );
  auto *anchorGroup = new QActionGroup( anchorMenu );
  anchorGroup->setExclusive( true );
  auto addAnchorAction = [this, anchorMenu, anchorGroup]( const QString &text, AnchorPointMode mode, bool checked = false ) {
    QAction *action = anchorMenu->addAction( text );
    action->setCheckable( true );
    action->setChecked( checked );
    anchorGroup->addAction( action );
    connect( action, &QAction::triggered, this, [this, mode] {
      m_manualAlign3DAnchorMode = mode;
      if ( m_anchorLockMode != AnchorPointMode::None )
      {
        m_anchorLockMode = mode;
        retargetAnchorLockToCurrentPosition();
      }
    } );
  };
  addAnchorAction( tr( "Upper current anchor" ), AnchorPointMode::UpperOrigin, true );
  addAnchorAction( tr( "Top min X / min Y" ), AnchorPointMode::TopMinMin );
  addAnchorAction( tr( "Top max X / min Y" ), AnchorPointMode::TopMaxMin );
  addAnchorAction( tr( "Top min X / max Y" ), AnchorPointMode::TopMinMax );
  addAnchorAction( tr( "Top max X / max Y" ), AnchorPointMode::TopMaxMax );
  addAnchorAction( tr( "Top center" ), AnchorPointMode::TopCenter );
  alignMenu->addSeparator();
  auto *fitCornerAction = alignMenu->addAction( tr( "Fit corner from three faces..." ) );
  fitCornerAction->setToolTip( tr( "Available for Cuboid, TruncatedPyramidRoof, LHouse and IndentedCuboid. Select two walls and the flat top, then confirm the virtual corner." ) );
  alignMenu->setToolTipsVisible( true );
  align3DMenuBtn->setMenu( alignMenu );
  connect( alignMenu, &QMenu::aboutToShow, this, [this, fitCornerAction] {
    const QString type = ui->comboPrimitive->currentText();
    fitCornerAction->setEnabled( type == QStringLiteral( "Cuboid" ) ||
                                 type == QStringLiteral( "TruncatedPyramidRoof" ) ||
                                 type == QStringLiteral( "LHouse" ) ||
                                 type == QStringLiteral( "IndentedCuboid" ) );
  } );
  connect( fitCornerAction, &QAction::triggered, this, [this] {
    stopManualTranslate3D();
    startTranslate3D( true );
  } );
  m_manualTranslate3DBtn->setIcon( QgsApplication::getThemeIcon( QStringLiteral( "/3d.svg" ) ) );
  m_manualTranslate3DBtn->setToolTip( tr( "Move the upper-corner red X (top center for round models) to a 3D cloud point. Right-click or Esc cancels." ) );
  connect( m_manualTranslate3DBtn, &QPushButton::clicked, this, &ParamModelerDock::startManualTranslate3D );
}

void ParamModelerDock::onPrimitiveChanged( const QString &prim )
{
  stopManualTranslate3D();
  clearAnchorLock();
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
    { "TriPrismPyramid", &Ui::ParamModelerDock::pageTriPrismPyramid },
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

  // 切换基元后 DL 锚点失效（锚点参数与基元类型绑定），禁用复位按钮
  m_hasDlAnchor = false;
  m_dlAnchorParams.clear();
  m_evaluationRaw.clear();
  m_evaluationCorrected.clear();
  if ( m_resetAnchorBtn )
    m_resetAnchorBtn->setEnabled( false );

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
  randomizePrimitiveParams( this, refreshPreview, randomizePose );
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

void ParamModelerDock::onExportEvaluationCsv()
{
  const QString primitive = ui->comboPrimitive->currentText();
  if ( !checkMeshValid( primitive, this ) )
    return;
  const bool hasPrediction = !m_evaluationRaw.isEmpty()
    && m_evaluationPrimitive == primitive
    && m_evaluationInput == QFileInfo( m_inputDataPath ).absoluteFilePath();
  QString path = QFileDialog::getSaveFileName( this, tr( "Export evaluation CSV" ),
    QFileInfo( m_inputDataPath ).completeBaseName() + QStringLiteral( "_evaluation.csv" ),
    tr( "CSV files (*.csv)" ) );
  if ( path.isEmpty() )
    return;
  if ( !path.endsWith( QStringLiteral( ".csv" ), Qt::CaseInsensitive ) )
    path += QStringLiteral( ".csv" );

  // Reuse physical parameter getters; widget values can be ratios instead of lengths.
  QJsonObject physical = ExportJSON::buildParams( this, primitive );
  if ( primitive == QStringLiteral( "TruncatedPyramidRoof" ) )
  {
    physical.insert( QStringLiteral( "bottomLength" ), tpBottomLength() );
    physical.insert( QStringLiteral( "bottomWidth" ), tpBottomWidth() );
  }
  if ( primitive == QStringLiteral( "CylinderDome" ) || primitive == QStringLiteral( "CylinderHemisphere" ) )
    physical.insert( QStringLiteral( "bulge" ), cylHemiBulge() );
  QMap<QString, double> physicalMap;
  for ( auto it = physical.constBegin(); it != physical.constEnd(); ++it )
    physicalMap.insert( it.key(), it.value().toDouble() );
  const auto current = pointNetParamsToUiParams( primitive, physicalMap );

  QSaveFile file( path );
  if ( !file.open( QIODevice::WriteOnly ) )
  {
    QMessageBox::warning( this, tr( "Export failed" ), file.errorString() );
    return;
  }
  QByteArray csv = QByteArray::fromHex( "efbbbf" );
  const auto number = []( double value ) {
    return std::isfinite( value ) ? QString::number( value, 'g', 15 ) : QString();
  };
  const QString timestamp = QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs );
  const auto line = [&]( QStringList cells ) {
    for ( QString &cell : cells )
    {
      cell.replace( QLatin1Char( '"' ), QStringLiteral( "\"\"" ) );
      cell = QLatin1Char( '"' ) + cell + QLatin1Char( '"' );
    }
    csv += cells.join( QLatin1Char( ',' ) ).toUtf8() + "\r\n";
  };
  line( { "schema_version", "export_time_utc", "input_file", "primitive", "model",
          "checkpoint", "correction_enabled_at_inference", "correction_strength_at_inference", "section", "name",
          "raw_prediction", "corrected_prediction", "current_value",
          "correction_delta", "current_minus_corrected" } );
  const auto row = [&]( const QString &section, const QString &name, const QString &raw,
                        const QString &corrected, const QString &value,
                        const QString &correctionDelta = QString(), const QString &editDelta = QString() ) {
    line( { "2", timestamp, m_inputDataPath, primitive,
            hasPrediction ? m_evaluationModel : QString(),
            hasPrediction ? m_evaluationCheckpoint : QString(),
            hasPrediction ? QString::number( m_evaluationCorrectionEnabled ) : QString(),
            hasPrediction ? number( m_evaluationCorrectionStrength ) : QString(),
            section, name, raw, corrected, value, correctionDelta, editDelta } );
  };
  auto keys = current;
  if ( hasPrediction )
    for ( auto it = m_evaluationRaw.cbegin(); it != m_evaluationRaw.cend(); ++it )
      keys.insert( it.key(), it.value() );
  for ( auto it = keys.cbegin(); it != keys.cend(); ++it )
  {
    const QString key = it.key();
    const bool raw = hasPrediction && m_evaluationRaw.contains( key );
    const bool corrected = hasPrediction && m_evaluationCorrected.contains( key );
    row( "parameter_ui_semantics", key,
         raw ? number( m_evaluationRaw.value( key ) ) : QString(),
         corrected ? number( m_evaluationCorrected.value( key ) ) : QString(),
         current.contains( key ) ? number( current.value( key ) ) : QString(),
         raw && corrected ? number( m_evaluationCorrected.value( key ) - m_evaluationRaw.value( key ) ) : QString(),
         corrected && current.contains( key ) ? number( current.value( key ) - m_evaluationCorrected.value( key ) ) : QString() );
  }
  // Include the complete physical model snapshot, including non-regressed parameters.
  for ( auto it = physicalMap.cbegin(); it != physicalMap.cend(); ++it )
    row( "physical_parameter", it.key(), {}, {}, number( it.value() ) );
  const auto metric = [&]( const QString &name, double value ) {
    row( "current_alignment", name, {}, {}, number( value ) );
  };
  metric( "tx", poseTranslateX() );
  metric( "ty", poseTranslateY() );
  metric( "tz", poseTranslateZ() );
  metric( "rx_degrees", poseRotateX() );
  metric( "ry_degrees", poseRotateY() );
  metric( "rz_degrees", poseRotateZ() );
  const MeshData mesh = BuildMesh::build( primitive, this );
  const QMatrix4x4 rotation = modelRotationMatrix( this );
  const QVector3D translation( poseTranslateX(), poseTranslateY(), poseTranslateZ() );
  QVector3D modelMin = rotation.map( mesh.vertices.first() ) + translation;
  QVector3D modelMax = modelMin;
  for ( const QVector3D &vertex : mesh.vertices )
  {
    const QVector3D world = rotation.map( vertex ) + translation;
    for ( int axis = 0; axis < 3; ++axis )
    {
      modelMin[axis] = std::min( modelMin[axis], world[axis] );
      modelMax[axis] = std::max( modelMax[axis], world[axis] );
    }
  }
  const bool hasCloud = m_evaluationCloudAvailable && m_pointCloudLayer && m_hasDisplayCloudBBox
    && QFileInfo( m_displayCloudSourcePath ).absoluteFilePath() == QFileInfo( m_inputDataPath ).absoluteFilePath();
  row( "status", "cloud_bbox_source", {}, {}, hasCloud ? "displayed_cloud_full_robust_bbox" : "unavailable" );
  row( "status", "prediction", {}, {}, hasPrediction ? "available" : "unavailable" );
  row( "status", "ground_truth", {}, {}, "not_evaluated" );
  for ( int axis = 0; axis < 3; ++axis )
  {
    const QString suffix = QStringLiteral( "_" ) + QStringLiteral( "xyz" ).mid( axis, 1 );
    metric( "model_min" + suffix, modelMin[axis] );
    metric( "model_max" + suffix, modelMax[axis] );
    if ( hasCloud )
    {
      metric( "cloud_min" + suffix, m_evaluationCloudMin[axis] );
      metric( "cloud_max" + suffix, m_evaluationCloudMax[axis] );
      metric( "min_delta" + suffix, modelMin[axis] - m_evaluationCloudMin[axis] );
      metric( "max_delta" + suffix, modelMax[axis] - m_evaluationCloudMax[axis] );
      metric( "size_delta" + suffix, ( modelMax[axis] - modelMin[axis] ) - ( m_evaluationCloudMax[axis] - m_evaluationCloudMin[axis] ) );
      metric( "center_delta" + suffix, ( modelMin[axis] + modelMax[axis] - m_evaluationCloudMin[axis] - m_evaluationCloudMax[axis] ) * 0.5 );
    }
  }
  if ( file.write( csv ) != csv.size() || !file.commit() )
    QMessageBox::warning( this, tr( "Export failed" ), file.errorString() );
  else
    QMessageBox::information( this, tr( "Export succeeded" ), tr( "Evaluation CSV saved to:\n%1" ).arg( path ) );
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
void ParamModelerDock::onExportDLDatasetClicked()
{
  generateFullDataset( this );
}

void ParamModelerDock::onExportCurrentPrimitiveDLDatasetClicked()
{
  generateSinglePrimitiveDataset( this );
}

void ParamModelerDock::onCompleteSelectedDLDatasetsClicked()
{
  completeSelectedPrimitiveDatasets( this );
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
  if ( !ParamModelerScene3D::updateRealtimePreviewMesh( mIface, mesh, pose, m_currentPrimitive, &errorMessage ) )
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
  applyAnchorLockCompensation( mesh );

  if ( m_manualTranslate3DTool )
  {
    anchorLocalPoint( m_manualAlign3DAnchorMode, mesh, m_manualTranslate3DLocalAnchor );
    m_manualTranslate3DTool->refreshMarkers();
  }

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
    if ( !ParamModelerScene3D::updateRealtimePreviewMesh( mIface, mesh, pose, m_currentPrimitive, &errorMessage ) )
      DEBUG_LOG( QString( "[3D] realtime Qt3D preview update failed: %1\n" ).arg( errorMessage ).toStdWString().c_str() );
  }

  m_previewUpdateInProgress = false;
  if ( m_previewUpdatePending && m_previewTimer && !m_previewTimer->isActive() )
    m_previewTimer->start();
}
