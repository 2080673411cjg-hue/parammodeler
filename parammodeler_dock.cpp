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

#include <limits>
#include <algorithm>
#include <cmath>

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
#include <QEvent>
#include <QWheelEvent>
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

// ============================================================
// metadata 记录明显是"从归一化坐标存下来的坏记录"：center ≈ 0 且 scale ≈ 1。
// 显示路径和对齐路径必须用同一判据，否则会出现"点云画在 A、模型对到 B"。
// ============================================================
static bool pointCloudMetadataLooksBuggy( const QVector3D &center, double scale )
{
  return scale < 2.0 && center.lengthSquared() < 0.01;
}

// ============================================================
// 自动对齐：模型参考点 ↔ 点云参考点
//
// 参考点按锚点类型二选一（锚点 / 生长原点 / 旋转轴三者绑定）：
//   左下角锚定（长方体类，BuildMesh::usesCornerAnchor）→ 两边都取 bbox **左下角**
//   底面中心锚定（圆形类 / TriPrismPyramid）          → XY 取 bbox 中心，Z 取 bbox 底面
//
// 绝不能用 pointCloudInfo.center（那是采样点质心）：采样分布屋顶 60% / 墙 40%
// （exportpointcloud.cpp），质心 z 与 bbox 中心 z 能差 1–3 m（实测），
// 非对称底面 X/Y 也偏。必须用 bbox 极值，两侧同一语义才能对上。
//
// 参考点必须在**施加 pose 旋转之后**的 mesh 上算：点云坐标导出时已被 applyPose 转过 rz，
// 它的 bbox 是旋转后点集的 bbox；模型侧若拿未旋转的 mesh bbox，两侧就不是同一个语义。
// 旋转绕 mesh 自身原点（=锚点，见 scene3d 的 T·Rx·Ry·Rz），所以 rz=0 时下面的
// posMat 是单位阵，结果与 v2.3.5 完全一致。
// ============================================================
static QMatrix4x4 modelRotationMatrix( ParamModelerDock *dock )
{
    QMatrix4x4 posMat;
    posMat.setToIdentity();
    posMat.rotate( static_cast<float>( dock->poseRotateX() ), 1, 0, 0 );
    posMat.rotate( static_cast<float>( dock->poseRotateY() ), 0, 1, 0 );
    posMat.rotate( static_cast<float>( dock->poseRotateZ() ), 0, 0, 1 );
    return posMat;
}

static QVector3D modelAlignmentReference( const MeshData &mesh, const QString &primitiveType,
                                          ParamModelerDock *dock )
{
    const QMatrix4x4 posMat = modelRotationMatrix( dock );

    QVector3D vMin( std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max() );
    QVector3D vMax( std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest() );
    for ( const QVector3D &raw : mesh.vertices )
    {
        const QVector3D v = posMat.map( raw );
        if ( v.x() < vMin.x() ) vMin.setX( v.x() );
        if ( v.y() < vMin.y() ) vMin.setY( v.y() );
        if ( v.z() < vMin.z() ) vMin.setZ( v.z() );
        if ( v.x() > vMax.x() ) vMax.setX( v.x() );
        if ( v.y() > vMax.y() ) vMax.setY( v.y() );
        if ( v.z() > vMax.z() ) vMax.setZ( v.z() );
    }

    const bool corner = BuildMesh::usesCornerAnchor( primitiveType );
    return corner
      ? vMin
      : QVector3D( ( vMin.x() + vMax.x() ) * 0.5f,
                   ( vMin.y() + vMax.y() ) * 0.5f,
                   vMin.z() );
}

static QVector3D modelGrowthAnchor( ParamModelerDock *dock )
{
    // BuildMesh keeps the semantic growth/rotation anchor at local origin:
    // corner-anchored classes use the footprint corner; centered classes are
    // shifted so the base center is at (0,0,0).
    return modelRotationMatrix( dock ).map( QVector3D( 0.0f, 0.0f, 0.0f ) );
}

static void alignModelToPointCloud( const MeshData &mesh, const QVector3D &pcRef,
                                     const QString &primitiveType,
                                     ParamModelerDock *dock, const QString &context )
{
    if ( mesh.vertices.isEmpty() )
    {
        DEBUG_LOG( QString( "[Align] %1 mesh empty, skipped\n" ).arg( context ).toStdWString().c_str() );
        return;
    }

    const bool corner = BuildMesh::usesCornerAnchor( primitiveType );
    // 模型侧参考点：角锚定 → 旋转后 bbox 左下角；中心锚定 → 旋转后底面中心
    // （圆形底面各向同性，绕中心转后 XY 的 bbox 中心仍在原点；角锚定类 rz≠0 时
    //   锚点本身已不再是 bbox 极值，所以必须用旋转后的 bbox —— 见上面的注释）
    const QVector3D mRef = modelAlignmentReference( mesh, primitiveType, dock );

    const double tx = static_cast<double>( pcRef.x() ) - static_cast<double>( mRef.x() );
    const double ty = static_cast<double>( pcRef.y() ) - static_cast<double>( mRef.y() );
    const double tz = static_cast<double>( pcRef.z() ) - static_cast<double>( mRef.z() );
    dock->setPoseTranslate( tx, ty, tz );

    DEBUG_LOG( QString( "[Align] %1 anchor=%2 rz=%3 pcRef=(%4,%5,%6) modelRef=(%7,%8,%9) → tx=%10 ty=%11 tz=%12\n" )
                 .arg( context )
                 .arg( corner ? QStringLiteral( "corner" ) : QStringLiteral( "center" ) )
                 .arg( dock->poseRotateZ(), 0, 'f', 2 )
                 .arg( pcRef.x(), 0, 'f', 2 ).arg( pcRef.y(), 0, 'f', 2 ).arg( pcRef.z(), 0, 'f', 2 )
                 .arg( mRef.x(), 0, 'f', 2 ).arg( mRef.y(), 0, 'f', 2 ).arg( mRef.z(), 0, 'f', 2 )
                 .arg( tx, 0, 'f', 2 ).arg( ty, 0, 'f', 2 ).arg( tz, 0, 'f', 2 )
                 .toStdWString().c_str() );
}

// ============================================================
// 缓存输入文件的 metadata：
//   center / scale → 反归一化（p*scale + center）
//   bbox 极值      → 模型对齐（角锚定用 bboxMin，圆类用 XY 中心 + Z 底面；
//                    不能拿 center 去对齐，见 alignModelToPointCloud）
//   rz             → 导出朝向，回填到 pose（见 applyMetadataRz）
// ============================================================
void ParamModelerDock::cacheInputMetadata( const QString &filePath )
{
  QVector3D metadataBBoxMin, metadataBBoxSize;
  double metadataRz = qQNaN();
  m_hasMetadata = metadataPointCloudInfoForInput( filePath, &metadataBBoxMin, &metadataBBoxSize,
                                                 &m_metadataCenter, &m_metadataScale,
                                                 &metadataRz );
  // 坏记录（从归一化坐标存下来的 center≈0 / scale≈1）不能拿去对齐，否则模型被对到世界原点
  m_hasMetadataBBox = m_hasMetadata &&
                      !pointCloudMetadataLooksBuggy( m_metadataCenter, m_metadataScale );
  if ( m_hasMetadataBBox )
  {
    m_metadataBBoxMin    = metadataBBoxMin;
    m_metadataBBoxCenter = metadataBBoxMin + metadataBBoxSize * 0.5f;
  }
  // rz 只有 JSON metadata 有（PLY / 兜底路径给 NaN），且坏记录不算数
  m_hasMetadataRz = m_hasMetadataBBox && !qIsNaN( metadataRz );
  if ( m_hasMetadataRz )
    m_metadataRz = metadataRz;

  if ( m_hasMetadata )
  {
    DEBUG_LOG( QString( "[Meta] center=(%1,%2,%3) scale=%4 metadataMin=(%5,%6,%7) metadataCenter=(%8,%9,%10) rz=%11 usableBBox=%12\n" )
                 .arg( m_metadataCenter.x(), 0, 'f', 2 )
                 .arg( m_metadataCenter.y(), 0, 'f', 2 )
                 .arg( m_metadataCenter.z(), 0, 'f', 2 )
                 .arg( m_metadataScale, 0, 'f', 2 )
                 .arg( m_metadataBBoxMin.x(), 0, 'f', 2 )
                 .arg( m_metadataBBoxMin.y(), 0, 'f', 2 )
                 .arg( m_metadataBBoxMin.z(), 0, 'f', 2 )
                 .arg( m_metadataBBoxCenter.x(), 0, 'f', 2 )
                 .arg( m_metadataBBoxCenter.y(), 0, 'f', 2 )
                 .arg( m_metadataBBoxCenter.z(), 0, 'f', 2 )
                 .arg( m_hasMetadataRz ? QString::number( m_metadataRz, 'f', 2 )
                                       : QStringLiteral( "<none>" ) )
                 .arg( m_hasMetadataBBox ? QStringLiteral( "yes" ) : QStringLiteral( "no" ) )
                 .toStdWString().c_str() );
  }
}

// ============================================================
// 把 metadata 记录的导出朝向回填到 pose。
//
// 为什么是 metadata 而不是 DL 预测：rz 在训练标签里是**不可辨识**的，模型学不到
// （圆柱类绕轴旋转点云不变 → rz 无定义；矩形底面类只确定到 mod 180°，近正方到 mod 90°；
//  `_rot` 实验已因此弃用，见 scripts/README.md）。metadata 里存的却是导出真值，
// 点云坐标本身就是 applyPose(..., rz) 转出来的，所以直接回填同一个角最准。
//
// 必须早于 alignModelToPointCloud：对齐参考点按旋转后的 mesh 算。
// ============================================================
bool ParamModelerDock::applyMetadataRz()
{
  if ( !m_hasMetadataRz )
    return false;

  ui->spinBoxRKappa->setValue( m_metadataRz );
  return true;
}

// ============================================================
// 对齐目标解析：优先"场景里实际显示的点云"的包围盒（ground truth），
// 没有时退回 metadata 记录的 bbox。
// 这样显示路径和对齐路径用的是同一个几何，不会再出现"点云画在 A、模型对到 B"。
// ============================================================
bool ParamModelerDock::pointCloudAlignTarget( QVector3D &out ) const
{
  // 参考点语义必须和模型侧一致（见 alignModelToPointCloud）
  const bool corner = BuildMesh::usesCornerAnchor( ui->comboPrimitive->currentText() );

  // 场景里显示的正好就是当前输入文件 → 用实测的显示 bbox（最可靠）
  if ( m_hasDisplayCloudBBox && !m_inputDataPath.isEmpty() &&
       QFileInfo( m_displayCloudSourcePath ).absoluteFilePath().compare(
         QFileInfo( m_inputDataPath ).absoluteFilePath(), Qt::CaseInsensitive ) == 0 )
  {
    out = corner ? m_displayCloudBBoxMin : m_displayCloudBBoxCenter;
    out.setZ( m_displayCloudBBoxMin.z() );
    DEBUG_LOG( QString( "[AlignTarget] source=display anchor=%1 pcRef=(%2,%3,%4) displayMin=(%5,%6,%7) displayCenter=(%8,%9,%10)\n" )
                 .arg( corner ? QStringLiteral( "corner" ) : QStringLiteral( "base-center" ) )
                 .arg( out.x(), 0, 'f', 2 ).arg( out.y(), 0, 'f', 2 ).arg( out.z(), 0, 'f', 2 )
                 .arg( m_displayCloudBBoxMin.x(), 0, 'f', 2 )
                 .arg( m_displayCloudBBoxMin.y(), 0, 'f', 2 )
                 .arg( m_displayCloudBBoxMin.z(), 0, 'f', 2 )
                 .arg( m_displayCloudBBoxCenter.x(), 0, 'f', 2 )
                 .arg( m_displayCloudBBoxCenter.y(), 0, 'f', 2 )
                 .arg( m_displayCloudBBoxCenter.z(), 0, 'f', 2 )
                 .toStdWString().c_str() );
    return true;
  }
  if ( m_hasMetadataBBox )
  {
    out = corner ? m_metadataBBoxMin : m_metadataBBoxCenter;
    out.setZ( m_metadataBBoxMin.z() );
    DEBUG_LOG( QString( "[AlignTarget] source=metadata anchor=%1 pcRef=(%2,%3,%4) metadataMin=(%5,%6,%7) metadataCenter=(%8,%9,%10)\n" )
                 .arg( corner ? QStringLiteral( "corner" ) : QStringLiteral( "base-center" ) )
                 .arg( out.x(), 0, 'f', 2 ).arg( out.y(), 0, 'f', 2 ).arg( out.z(), 0, 'f', 2 )
                 .arg( m_metadataBBoxMin.x(), 0, 'f', 2 )
                 .arg( m_metadataBBoxMin.y(), 0, 'f', 2 )
                 .arg( m_metadataBBoxMin.z(), 0, 'f', 2 )
                 .arg( m_metadataBBoxCenter.x(), 0, 'f', 2 )
                 .arg( m_metadataBBoxCenter.y(), 0, 'f', 2 )
                 .arg( m_metadataBBoxCenter.z(), 0, 'f', 2 )
                 .toStdWString().c_str() );
    return true;
  }
  return false;
}

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

  initUiControls();
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

    // GabledRoof (4 rows): Body(L,W) → Roof(Total height, Wall ratio)
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

    // FourStageRoundTower (6 rows): Base(R,H) → Middle → Top
    if ( QFormLayout *f = fmt( ui->pageFourStageRoundTower ) )
    {
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

void ParamModelerDock::initConnections()
{
  auto syncHCRRadius = [this]() {
    ui->spinBoxHCRRadius->setValue( ui->spinBoxHCRWidth->value() / 2.0 );
  };
  syncHCRRadius();
  connect( ui->spinBoxHCRWidth, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [syncHCRRadius]( double ) { syncHCRRadius(); } );

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
  connect( ui->actClear3D, &QAction::triggered, this, [this]() {
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
  connect( ui->btnToggleInversion, &QPushButton::clicked, this, &ParamModelerDock::onOpenPointCloudEstimateDialog );

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
  ui->frameInversion->setVisible( false );
  ui->btnToggleInversion->setCheckable( false );
  ui->btnToggleInversion->setFlat( false );
  ui->btnToggleInversion->setStyleSheet( QString() );
  ui->btnToggleInversion->setText( tr( "Point cloud classification and parameter estimation" ) );
  ui->btnPointNetClassify->setText( tr( "Classify" ) );
  ui->btnInverseParams->setText( tr( "Estimate parameters" ) );
  ui->formLayoutPrimitive->addRow( tr( "Point cloud:" ), ui->btnToggleInversion );

  // 线框模式复选框：微调参数时只显示模型边线，不遮挡点云
  mWireframeModeCheckBox = new QCheckBox( tr( "Wireframe mode (edges only)" ), this );
  mWireframeModeCheckBox->setChecked( false );
  mWireframeModeCheckBox->setToolTip( tr( "Show only model edges so the point cloud is fully visible during fine-tuning." ) );
  ui->formLayoutPrimitive->addRow( tr( "3D model:" ), mWireframeModeCheckBox );
  connect( mWireframeModeCheckBox, &QCheckBox::toggled, this, [this]( bool checked ) {
    ParamModelerScene3D::setWireframeMode( checked );
    if ( m_realtimeModelLoaded )
      onUpdatePreview();
  } );

  // DL预测值复位按钮：一键将所有参数恢复到深度学习推理结果
  m_resetAnchorBtn = new QPushButton( tr( "↺ Reset to DL prediction" ), this );
  m_resetAnchorBtn->setToolTip( tr( "Restore all shape parameters to the last deep-learning inference result." ) );
  m_resetAnchorBtn->setEnabled( false );  // 初始禁用，等推理成功后再启用
  m_resetAnchorBtn->setStyleSheet(
      "QPushButton { color: #1a73e8; border: 1px solid #1a73e8; border-radius: 3px; padding: 3px 8px; }"
      "QPushButton:hover { background: #e8f0fe; }"
      "QPushButton:disabled { color: #999; border-color: #ccc; }"
  );
  ui->formLayoutPrimitive->addRow( tr( "DL anchor:" ), m_resetAnchorBtn );
  connect( m_resetAnchorBtn, &QPushButton::clicked, this, &ParamModelerDock::resetToDlAnchor );

  m_geometryCorrectionCheckBox = new QCheckBox( tr( "Enable geometry correction" ), this );
  m_geometryCorrectionCheckBox->setToolTip(
    tr( "Experimental: after PCT regression, adjust strong geometric parameters from the current point cloud for Cuboid, Cylinder, and GabledRoof." )
  );
  m_geometryCorrectionCheckBox->setChecked( false );
  ui->formLayoutPrimitive->addRow( tr( "Param correction:" ), m_geometryCorrectionCheckBox );

  m_manualTranslateBtn = new QPushButton( tr( "Pick target for model anchor" ), this );
  m_manualTranslateBtn->setToolTip( tr( "Shows the current model anchor as a red X in the 2D map canvas. Click the point-cloud target position to translate the model there. Only TX/TY are changed." ) );
  ui->formLayoutPrimitive->addRow( tr( "Manual align:" ), m_manualTranslateBtn );
  connect( m_manualTranslateBtn, &QPushButton::clicked, this, &ParamModelerDock::startManualTranslateByClick );
}

void ParamModelerDock::startManualTranslateByClick()
{
  if ( !mIface || !mIface->mapCanvas() )
  {
    QMessageBox::warning( this, tr( "Manual align" ), tr( "QGIS map canvas is unavailable." ) );
    return;
  }

  const QString prim = ui->comboPrimitive->currentText();
  const MeshData mesh = BuildMesh::build( prim, this );
  if ( mesh.isEmpty() )
  {
    QMessageBox::warning( this, tr( "Manual align" ), tr( "Current model has no geometry data." ) );
    return;
  }

  const QVector3D localRef = modelGrowthAnchor( this );
  m_manualTranslateSource = QgsPointXY(
    static_cast<double>( localRef.x() ) + poseTranslateX(),
    static_cast<double>( localRef.y() ) + poseTranslateY()
  );
  m_manualTranslateHasSource = true;

  const QMatrix4x4 rotMat = modelRotationMatrix( this );
  QVector3D xDir = rotMat.mapVector( QVector3D( 1.0f, 0.0f, 0.0f ) );
  QVector3D yDir = rotMat.mapVector( QVector3D( 0.0f, 1.0f, 0.0f ) );
  QVector3D meshMin = mesh.vertices.first();
  QVector3D meshMax = mesh.vertices.first();
  for ( const QVector3D &v : mesh.vertices )
  {
    if ( v.x() < meshMin.x() ) meshMin.setX( v.x() );
    if ( v.y() < meshMin.y() ) meshMin.setY( v.y() );
    if ( v.z() < meshMin.z() ) meshMin.setZ( v.z() );
    if ( v.x() > meshMax.x() ) meshMax.setX( v.x() );
    if ( v.y() > meshMax.y() ) meshMax.setY( v.y() );
    if ( v.z() > meshMax.z() ) meshMax.setZ( v.z() );
  }
  const QVector3D meshSize = meshMax - meshMin;
  const double guideLength = std::max( 1.0, std::max( static_cast<double>( meshSize.x() ),
                                                      static_cast<double>( meshSize.y() ) ) * 0.18 );
  auto normalizeXY = []( const QVector3D &v ) -> QgsPointXY
  {
    const double len = std::hypot( static_cast<double>( v.x() ), static_cast<double>( v.y() ) );
    if ( len < 1e-9 )
      return QgsPointXY( 1.0, 0.0 );
    return QgsPointXY( static_cast<double>( v.x() ) / len, static_cast<double>( v.y() ) / len );
  };
  const QgsPointXY xUnit = normalizeXY( xDir );
  const QgsPointXY yUnit = normalizeXY( yDir );
  const QgsPointXY xEnd( m_manualTranslateSource.x() + xUnit.x() * guideLength,
                         m_manualTranslateSource.y() + xUnit.y() * guideLength );
  const QgsPointXY yEnd( m_manualTranslateSource.x() + yUnit.x() * guideLength,
                         m_manualTranslateSource.y() + yUnit.y() * guideLength );

  QgsMapCanvas *canvas = mIface->mapCanvas();
  if ( !m_manualTranslateTool )
  {
    m_manualTranslateTool = new QgsMapToolEmitPoint( canvas );
    connect( m_manualTranslateTool, &QgsMapToolEmitPoint::canvasClicked,
             this, &ParamModelerDock::handleManualTranslateClick );
  }

  if ( canvas->mapTool() != m_manualTranslateTool )
    m_previousMapTool = canvas->mapTool();

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
  m_manualTranslateSourceMarker = new QgsRubberBand( canvas, Qgis::GeometryType::Point );
  m_manualTranslateSourceMarker->setIcon( QgsRubberBand::ICON_X );
  m_manualTranslateSourceMarker->setIconSize( 16 );
  m_manualTranslateSourceMarker->setWidth( 3 );
  m_manualTranslateSourceMarker->setColor( QColor( 220, 30, 30, 220 ) );
  m_manualTranslateSourceMarker->addPoint( m_manualTranslateSource );
  m_manualTranslateXAxisMarker = new QgsRubberBand( canvas, Qgis::GeometryType::Line );
  m_manualTranslateXAxisMarker->setColor( QColor( 220, 30, 30, 220 ) );
  m_manualTranslateXAxisMarker->setWidth( 3 );
  m_manualTranslateXAxisMarker->addPoint( m_manualTranslateSource );
  m_manualTranslateXAxisMarker->addPoint( xEnd );
  m_manualTranslateYAxisMarker = new QgsRubberBand( canvas, Qgis::GeometryType::Line );
  m_manualTranslateYAxisMarker->setColor( QColor( 30, 170, 60, 220 ) );
  m_manualTranslateYAxisMarker->setWidth( 3 );
  m_manualTranslateYAxisMarker->addPoint( m_manualTranslateSource );
  m_manualTranslateYAxisMarker->addPoint( yEnd );

  canvas->setMapTool( m_manualTranslateTool );
  if ( m_manualTranslateBtn )
    m_manualTranslateBtn->setText( tr( "Click target point..." ) );

  DEBUG_LOG( QString( "[ManualAlign] started prim=%1 source=(%2,%3) localRef=(%4,%5,%6) +X=(%7,%8) +Y=(%9,%10); click target on the 2D map canvas\n" )
               .arg( prim )
               .arg( m_manualTranslateSource.x(), 0, 'f', 3 )
               .arg( m_manualTranslateSource.y(), 0, 'f', 3 )
               .arg( localRef.x(), 0, 'f', 3 )
               .arg( localRef.y(), 0, 'f', 3 )
               .arg( localRef.z(), 0, 'f', 3 )
               .arg( xUnit.x(), 0, 'f', 3 )
               .arg( xUnit.y(), 0, 'f', 3 )
               .arg( yUnit.x(), 0, 'f', 3 )
               .arg( yUnit.y(), 0, 'f', 3 )
               .toStdWString().c_str() );
}

void ParamModelerDock::handleManualTranslateClick( const QgsPointXY &point, Qt::MouseButton button )
{
  if ( button != Qt::LeftButton )
    return;

  if ( !m_manualTranslateHasSource )
  {
    DEBUG_LOG( L"[ManualAlign] ignored click without a source anchor\n" );
    return;
  }

  const double dx = point.x() - m_manualTranslateSource.x();
  const double dy = point.y() - m_manualTranslateSource.y();
  const double oldTx = poseTranslateX();
  const double oldTy = poseTranslateY();
  const double oldTz = poseTranslateZ();
  setPoseTranslate( oldTx + dx, oldTy + dy, oldTz );

  DEBUG_LOG( QString( "[ManualAlign] target=(%1,%2) delta=(%3,%4) tx=%5→%6 ty=%7→%8 tz=%9\n" )
               .arg( point.x(), 0, 'f', 3 )
               .arg( point.y(), 0, 'f', 3 )
               .arg( dx, 0, 'f', 3 )
               .arg( dy, 0, 'f', 3 )
               .arg( oldTx, 0, 'f', 3 )
               .arg( poseTranslateX(), 0, 'f', 3 )
               .arg( oldTy, 0, 'f', 3 )
               .arg( poseTranslateY(), 0, 'f', 3 )
               .arg( oldTz, 0, 'f', 3 )
               .toStdWString().c_str() );

  m_manualTranslateHasSource = false;
  if ( m_manualTranslateBtn )
    m_manualTranslateBtn->setText( tr( "Pick target for model anchor" ) );
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

  QgsMapCanvas *canvas = mIface ? mIface->mapCanvas() : nullptr;
  if ( canvas && canvas->mapTool() == m_manualTranslateTool && m_previousMapTool )
    canvas->setMapTool( m_previousMapTool );
  else if ( canvas && canvas->mapTool() == m_manualTranslateTool )
    canvas->unsetMapTool( m_manualTranslateTool );

  onUpdatePreview();
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
  comboModel->addItem( QStringLiteral( "PCT" ) );
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
  auto *chkGeometryCorrection = new QCheckBox( tr( "Enable geometry correction" ), &dialog );
  chkGeometryCorrection->setToolTip(
    tr( "Experimental: after PCT regression, adjust strong geometric parameters from the current point cloud for Cuboid, Cylinder, and GabledRoof." )
  );
  chkGeometryCorrection->setChecked( m_geometryCorrectionCheckBox && m_geometryCorrectionCheckBox->isChecked() );
  connect( chkGeometryCorrection, &QCheckBox::toggled, this, [this]( bool checked ) {
    if ( m_geometryCorrectionCheckBox )
      m_geometryCorrectionCheckBox->setChecked( checked );
  } );
  if ( m_geometryCorrectionCheckBox )
  {
    connect( m_geometryCorrectionCheckBox, &QCheckBox::toggled, chkGeometryCorrection, [chkGeometryCorrection]( bool checked ) {
      if ( chkGeometryCorrection->isChecked() != checked )
        chkGeometryCorrection->setChecked( checked );
    } );
  }

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
  mainLayout->addWidget( chkGeometryCorrection );
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
    cacheInputMetadata( filePath );   // 对话框流程也要缓存，否则对齐没有退化备选
    updateInputInfo();
  } );

  auto selectedBackend = [&]() {
    const QString modelName = comboModel->currentText();
    if ( modelName == QStringLiteral( "PointNet" ) )
      return PointNetBackend::PointNet;
    if ( modelName == QStringLiteral( "PointNeXt" ) )
      return PointNetBackend::PointNeXt;
    if ( modelName == QStringLiteral( "PCT" ) )
      return PointNetBackend::PCT;
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

    QMap<QString, double> params = pointNetParamsToUiParams( prim, regression.params );
    applyDataDrivenParamCorrections( prim, params );
    if ( params.isEmpty() )
    {
      QMessageBox::warning( &dialog, tr( "Parameter estimation failed" ), tr( "No parameters returned." ) );
      return;
    }

    PointNetRunner::applyToUI( this, params );

    // 保存 DL 预测值作为微调锚点（对话框流程也需要）
    m_dlAnchorParams = params;
    m_hasDlAnchor = true;
    if ( m_resetAnchorBtn )
      m_resetAnchorBtn->setEnabled( true );

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
        onLoadToQGIS3D( true );
        if ( !loadPointCloudToQGIS3D( m_inputDataPath, false ) )
          QMessageBox::warning( &dialog, tr( "Point cloud load failed" ), tr( "The model was loaded, but the point cloud was not loaded into QGIS 3D." ) );

        // --- 朝向：先回填 metadata 里的导出 rz（点云已转过这个角），再对齐 ---
        if ( !applyMetadataRz() )
          DEBUG_LOG( QString( "[Align] no metadata rz for %1, keeping current pose rotation\n" ).arg( m_inputDataPath ).toStdWString().c_str() );

        // --- Auto-align：必须在点云显示**之后**做，对齐目标 = 实际显示点云的 bbox 极值 ---
        QVector3D alignTarget;
        if ( pointCloudAlignTarget( alignTarget ) )
        {
          alignModelToPointCloud( BuildMesh::build( ui->comboPrimitive->currentText(), this ),
                                  alignTarget, ui->comboPrimitive->currentText(),
                                  this, QStringLiteral( "dialog" ) );
          onUpdatePreview();  // 把新位姿推给 Qt3D 实时实体（模型图层已加载）
        }
        else
        {
          DEBUG_LOG( QString( "[Align] no point cloud bbox for %1, skipping\n" ).arg( m_inputDataPath ).toStdWString().c_str() );
        }
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
void ParamModelerDock::onExportDLDatasetClicked()
{
  generateFullDataset( this );
}

void ParamModelerDock::onExportCurrentPrimitiveDLDatasetClicked()
{
  generateSinglePrimitiveDataset( this );
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


// ============================================================
// 稳健包围盒：XY 取分位数，Z 底部取硬 min。
//
// 为什么必须这样：点云里的离群点（真实扫描的误匹配点，或 datasets_aug 里
// add_outliers 撒的点）数量很少但离得远，硬 min/max 会被它们完全主导。
// 实测（datasets_aug，每朵云约 1% 离群点、撒在归一化空间 [-1.15,1.15]^3）
// 角锚定点被拉偏 3–7 m —— 模型对齐就落不到建筑的左下角。
//
// 为什么 XY 分位数不会削掉建筑本体：建筑在水平轴上的极值是被一整条边/面（长方体）
// 或平方根聚集的点列（圆柱）逼近的，1% 分位与真实极值几乎重合。
// 但 Z 不能用 1% 分位：底面通常不采样，墙脚点也可能很稀，分位数会把模型整体垫高。
// 所以 Z 的 min 保留硬最小值；显示路径已把反归一化后的负 Z clamp 到 0。
// 点数 <100 时退回硬 min/max（样本太少，分位数没有意义）。
// ============================================================
static void hardBBoxFromPoints( const QVector<QVector3D> &pts,
                                QVector3D &outMin, QVector3D &outMax )
{
  outMin = QVector3D();
  outMax = QVector3D();
  if ( pts.isEmpty() )
    return;

  outMin = pts.first();
  outMax = pts.first();
  for ( const QVector3D &p : pts )
  {
    if ( p.x() < outMin.x() ) outMin.setX( p.x() );
    if ( p.y() < outMin.y() ) outMin.setY( p.y() );
    if ( p.z() < outMin.z() ) outMin.setZ( p.z() );
    if ( p.x() > outMax.x() ) outMax.setX( p.x() );
    if ( p.y() > outMax.y() ) outMax.setY( p.y() );
    if ( p.z() > outMax.z() ) outMax.setZ( p.z() );
  }
}

static void robustBBoxFromPoints( const QVector<QVector3D> &pts,
                                  QVector3D &outMin, QVector3D &outMax )
{
  outMin = QVector3D();
  outMax = QVector3D();
  const int n = pts.size();
  if ( n == 0 )
    return;

  int lo = 0;
  int hi = n - 1;
  if ( n >= 100 )
  {
    lo = static_cast<int>( std::floor( 0.01 * n ) );
    hi = n - 1 - lo;
  }

  QVector<float> col;
  col.resize( n );
  float mn[3] = { pts[0].x(), pts[0].y(), pts[0].z() };
  float mx[3] = { pts[0].x(), pts[0].y(), pts[0].z() };
  for ( int a = 0; a < 3; ++a )
  {
    for ( int i = 0; i < n; ++i )
      col[i] = pts[i][a];
    std::sort( col.begin(), col.end() );
    if ( a < 2 )
    {
      mn[a] = col[lo];
      mx[a] = col[hi];
    }
    else
    {
      mn[a] = col.first();
      mx[a] = col.last();
    }
  }
  outMin = QVector3D( mn[0], mn[1], mn[2] );
  outMax = QVector3D( mx[0], mx[1], mx[2] );
}

static bool loadRestoredPointsForGeometryCorrection( const QString &filePath,
                                                     QVector<QVector3D> &points )
{
  points.clear();
  if ( filePath.isEmpty() )
    return false;

  QVector3D metadataCenter;
  double metadataScale = 1.0;
  const bool hasMetadata = metadataPointCloudInfoForInput( filePath, nullptr, nullptr,
                                                           &metadataCenter, &metadataScale );
  const PointCloud pc = PointCloudLoader::load( filePath );
  if ( pc.points.isEmpty() )
    return false;

  points.reserve( pc.points.size() );
  const bool restoreFromDlNormalization = hasMetadata && pointCloudLooksNormalizedForDisplay( pc );
  for ( const QVector3D &p : pc.points )
  {
    QVector3D q = restoreFromDlNormalization
      ? p * static_cast<float>( metadataScale ) + metadataCenter
      : p;
    if ( q.z() < 0.0f && restoreFromDlNormalization )
      q.setZ( 0.0f );
    points.append( q );
  }
  return !points.isEmpty();
}

static QVector<QVector3D> unrotatePointsAroundZ( const QVector<QVector3D> &points, double rzDeg )
{
  QVector<QVector3D> out;
  out.reserve( points.size() );
  constexpr double pi = 3.14159265358979323846;
  const double a = -rzDeg * pi / 180.0;
  const double c = std::cos( a );
  const double s = std::sin( a );
  for ( const QVector3D &p : points )
  {
    const double x = static_cast<double>( p.x() );
    const double y = static_cast<double>( p.y() );
    out.append( QVector3D( static_cast<float>( x * c - y * s ),
                           static_cast<float>( x * s + y * c ),
                           p.z() ) );
  }
  return out;
}

static bool footprintBBoxForGeometryCorrection( const QVector<QVector3D> &points,
                                                QVector3D &outMin, QVector3D &outMax,
                                                QString &source )
{
  if ( points.isEmpty() )
    return false;

  QVector3D hardMin, hardMax;
  hardBBoxFromPoints( points, hardMin, hardMax );
  QVector3D fullMin, fullMax;
  robustBBoxFromPoints( points, fullMin, fullMax );

  const double height = static_cast<double>( hardMax.z() - hardMin.z() );
  const double sliceTop = static_cast<double>( hardMin.z() ) + std::max( 0.5, height * 0.12 );
  QVector<QVector3D> bottom;
  bottom.reserve( points.size() );
  for ( const QVector3D &p : points )
  {
    if ( static_cast<double>( p.z() ) <= sliceTop )
      bottom.append( p );
  }

  const int minSlicePoints = std::max( 50, static_cast<int>( std::ceil( points.size() * 0.05 ) ) );
  if ( bottom.size() >= minSlicePoints )
  {
    QVector3D sliceMin, sliceMax;
    robustBBoxFromPoints( bottom, sliceMin, sliceMax );
    const QVector3D fullRange = fullMax - fullMin;
    const QVector3D sliceRange = sliceMax - sliceMin;
    const bool xReasonable = fullRange.x() <= 1e-6f || sliceRange.x() >= fullRange.x() * 0.25f;
    const bool yReasonable = fullRange.y() <= 1e-6f || sliceRange.y() >= fullRange.y() * 0.25f;
    if ( xReasonable && yReasonable )
    {
      outMin = QVector3D( sliceMin.x(), sliceMin.y(), hardMin.z() );
      outMax = QVector3D( sliceMax.x(), sliceMax.y(), hardMax.z() );
      source = QStringLiteral( "bottom" );
      return true;
    }
  }

  outMin = QVector3D( fullMin.x(), fullMin.y(), hardMin.z() );
  outMax = QVector3D( fullMax.x(), fullMax.y(), hardMax.z() );
  source = QStringLiteral( "full" );
  return true;
}

static bool estimateGabledWallRatioFromProfile( const QVector<QVector3D> &points,
                                                double baseWidth,
                                                double zMin,
                                                double height,
                                                double &outRatio )
{
  if ( points.size() < 100 || baseWidth <= 1e-6 || height <= 1e-6 )
    return false;

  constexpr int bins = 24;
  QVector<QVector<float>> yByBin;
  yByBin.resize( bins );
  for ( const QVector3D &p : points )
  {
    const double t = ( static_cast<double>( p.z() ) - zMin ) / height;
    const int idx = std::max( 0, std::min( bins - 1, static_cast<int>( std::floor( t * bins ) ) ) );
    yByBin[idx].append( p.y() );
  }

  for ( int i = 2; i < bins - 2; ++i )
  {
    QVector<float> col = yByBin[i];
    if ( col.size() < 10 )
      continue;
    std::sort( col.begin(), col.end() );
    const int lo = static_cast<int>( std::floor( col.size() * 0.05 ) );
    const int hi = std::max( lo, static_cast<int>( std::ceil( col.size() * 0.95 ) ) - 1 );
    const double yRange = static_cast<double>( col[hi] - col[lo] );
    if ( yRange < baseWidth * 0.88 )
    {
      outRatio = std::max( 0.2, std::min( 0.9, ( i + 0.5 ) / bins ) );
      return true;
    }
  }
  return false;
}

void ParamModelerDock::applyDataDrivenParamCorrections( const QString &primitiveType,
                                                        QMap<QString, double> &uiParams ) const
{
  if ( !m_geometryCorrectionCheckBox || !m_geometryCorrectionCheckBox->isChecked() )
    return;

  const QString prim = primitiveType == QStringLiteral( "CylinderHemisphere" )
                         ? QStringLiteral( "CylinderDome" )
                         : primitiveType;
  if ( prim != QStringLiteral( "Cuboid" ) &&
       prim != QStringLiteral( "Cylinder" ) &&
       prim != QStringLiteral( "GabledRoof" ) )
    return;

  QVector<QVector3D> points;
  if ( !loadRestoredPointsForGeometryCorrection( m_inputDataPath, points ) )
  {
    DEBUG_LOG( QString( "[ParamCorrection] %1 skipped: no readable points\n" ).arg( prim ).toStdWString().c_str() );
    return;
  }

  double correctionRz = m_hasMetadataRz ? m_metadataRz : qQNaN();
  if ( !std::isfinite( correctionRz ) )
  {
    double metadataRz = qQNaN();
    metadataPointCloudInfoForInput( m_inputDataPath, nullptr, nullptr, nullptr, nullptr, &metadataRz );
    if ( std::isfinite( metadataRz ) )
      correctionRz = metadataRz;
  }
  const bool hasCorrectionRz = std::isfinite( correctionRz );

  const QVector<QVector3D> canonicalPoints = hasCorrectionRz
    ? unrotatePointsAroundZ( points, correctionRz )
    : points;

  QVector3D hardMin, hardMax;
  hardBBoxFromPoints( canonicalPoints, hardMin, hardMax );
  const double height = static_cast<double>( hardMax.z() - hardMin.z() );
  if ( height <= 1e-6 )
    return;

  QVector3D fpMin, fpMax;
  QString fpSource;
  if ( !footprintBBoxForGeometryCorrection( canonicalPoints, fpMin, fpMax, fpSource ) )
    return;
  const double fpLength = static_cast<double>( fpMax.x() - fpMin.x() );
  const double fpWidth = static_cast<double>( fpMax.y() - fpMin.y() );

  if ( prim == QStringLiteral( "Cuboid" ) )
  {
    if ( fpLength > 1e-6 ) uiParams.insert( QStringLiteral( "length" ), fpLength );
    if ( fpWidth > 1e-6 ) uiParams.insert( QStringLiteral( "width" ), fpWidth );
    uiParams.insert( QStringLiteral( "height" ), height );
    DEBUG_LOG( QString( "[ParamCorrection] Cuboid source=%1 length=%2 width=%3 height=%4 rz=%5\n" )
                 .arg( fpSource )
                 .arg( fpLength, 0, 'f', 3 )
                 .arg( fpWidth, 0, 'f', 3 )
                 .arg( height, 0, 'f', 3 )
                 .arg( hasCorrectionRz ? QString::number( correctionRz, 'f', 2 ) : QStringLiteral( "<none>" ) )
                 .toStdWString().c_str() );
    return;
  }

  if ( prim == QStringLiteral( "Cylinder" ) )
  {
    QVector3D robustMin, robustMax;
    robustBBoxFromPoints( canonicalPoints, robustMin, robustMax );
    const double cx = ( static_cast<double>( robustMin.x() ) + static_cast<double>( robustMax.x() ) ) * 0.5;
    const double cy = ( static_cast<double>( robustMin.y() ) + static_cast<double>( robustMax.y() ) ) * 0.5;
    QVector<double> radii;
    radii.reserve( canonicalPoints.size() );
    for ( const QVector3D &p : canonicalPoints )
      radii.append( std::hypot( static_cast<double>( p.x() ) - cx,
                                static_cast<double>( p.y() ) - cy ) );
    std::sort( radii.begin(), radii.end() );
    const int idx = std::max( 0, std::min( radii.size() - 1, static_cast<int>( std::floor( radii.size() * 0.90 ) ) ) );
    const double radius = radii[idx];
    if ( radius > 1e-6 ) uiParams.insert( QStringLiteral( "radius" ), radius );
    uiParams.insert( QStringLiteral( "cylHeight" ), height );
    DEBUG_LOG( QString( "[ParamCorrection] Cylinder radius=%1 height=%2 n=%3\n" )
                 .arg( radius, 0, 'f', 3 )
                 .arg( height, 0, 'f', 3 )
                 .arg( canonicalPoints.size() )
                 .toStdWString().c_str() );
    return;
  }

  if ( prim == QStringLiteral( "GabledRoof" ) )
  {
    if ( fpLength > 1e-6 ) uiParams.insert( QStringLiteral( "grLength" ), fpLength );
    if ( fpWidth > 1e-6 ) uiParams.insert( QStringLiteral( "grWidth" ), fpWidth );

    double wallRatio = 0.0;
    if ( estimateGabledWallRatioFromProfile( canonicalPoints, fpWidth, hardMin.z(), height, wallRatio ) )
    {
      uiParams.insert( QStringLiteral( "grWallHeight" ), height * wallRatio );
      uiParams.insert( QStringLiteral( "grRoofHeight" ), height * ( 1.0 - wallRatio ) );
      DEBUG_LOG( QString( "[ParamCorrection] GabledRoof source=%1 length=%2 width=%3 height=%4 wallRatio=%5 rz=%6\n" )
                   .arg( fpSource )
                   .arg( fpLength, 0, 'f', 3 )
                   .arg( fpWidth, 0, 'f', 3 )
                   .arg( height, 0, 'f', 3 )
                   .arg( wallRatio, 0, 'f', 3 )
                   .arg( hasCorrectionRz ? QString::number( correctionRz, 'f', 2 ) : QStringLiteral( "<none>" ) )
                   .toStdWString().c_str() );
    }
    else
    {
      DEBUG_LOG( QString( "[ParamCorrection] GabledRoof source=%1 length=%2 width=%3 height=%4 wallRatio=<kept PCT>\n" )
                   .arg( fpSource )
                   .arg( fpLength, 0, 'f', 3 )
                   .arg( fpWidth, 0, 'f', 3 )
                   .arg( height, 0, 'f', 3 )
                   .toStdWString().c_str() );
    }
  }
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
  const bool hasMetadata = metadataPointCloudInfoForInput( filePath, nullptr, nullptr, &metadataCenter, &metadataScale );

  // Detect buggy metadata (center ≈ 0, scale ≈ 1  →  was saved from
  // normalised coords by the old exportOccludedTXT / exportLabeledTXT).
  const bool metadataLooksBuggy = pointCloudMetadataLooksBuggy( metadataCenter, metadataScale );

  // 量"最终显示的点集"的包围盒 —— 模型对齐的目标就是它的极值/中心。
  // 显示什么就对到什么，两条路径共用同一几何，不再各算各的。
  // 注意用 robustBBoxFromPoints（分位数）而不是硬 min/max：离群点会把
  // 包围盒极值整个拉走，对齐就落不到建筑上（见该函数注释）。
  QVector3D displayMin, displayMax;
  bool hasDisplayBBox = false;
  auto setDisplayBBoxFromPoints = [&]( const QVector<QVector3D> &points, const QString &source )
  {
    if ( points.isEmpty() )
      return;

    QVector3D hardMin, hardMax;
    hardBBoxFromPoints( points, hardMin, hardMax );
    QVector3D fullRobustMin, fullRobustMax;
    robustBBoxFromPoints( points, fullRobustMin, fullRobustMax );

    const double fullHeight = static_cast<double>( hardMax.z() - hardMin.z() );
    const double sliceHeight = std::max( 0.5, fullHeight * 0.12 );
    const double sliceTop = static_cast<double>( hardMin.z() ) + sliceHeight;
    QVector<QVector3D> bottomSlice;
    bottomSlice.reserve( points.size() );
    for ( const QVector3D &p : points )
    {
      if ( static_cast<double>( p.z() ) <= sliceTop )
        bottomSlice.append( p );
    }

    QVector3D sliceMin, sliceMax;
    const int minSlicePoints = std::max( 50, static_cast<int>( std::ceil( points.size() * 0.05 ) ) );
    bool useBottomSlice = bottomSlice.size() >= minSlicePoints;
    QString sliceReason = useBottomSlice ? QStringLiteral( "ok" ) : QStringLiteral( "too-few-points" );
    if ( useBottomSlice )
    {
      robustBBoxFromPoints( bottomSlice, sliceMin, sliceMax );
      const QVector3D fullRange = fullRobustMax - fullRobustMin;
      const QVector3D sliceRange = sliceMax - sliceMin;
      const bool xReasonable = fullRange.x() <= 1e-6f || sliceRange.x() >= fullRange.x() * 0.25f;
      const bool yReasonable = fullRange.y() <= 1e-6f || sliceRange.y() >= fullRange.y() * 0.25f;
      useBottomSlice = xReasonable && yReasonable;
      if ( !useBottomSlice )
        sliceReason = QStringLiteral( "tiny-footprint" );
    }

    displayMin = useBottomSlice
      ? QVector3D( sliceMin.x(), sliceMin.y(), hardMin.z() )
      : QVector3D( fullRobustMin.x(), fullRobustMin.y(), hardMin.z() );
    displayMax = useBottomSlice
      ? QVector3D( sliceMax.x(), sliceMax.y(), fullRobustMax.z() )
      : fullRobustMax;
    hasDisplayBBox = true;

    const QVector3D fullRobustShift = fullRobustMin - hardMin;
    DEBUG_LOG( QString( "[PointCloudBBox] source=%1 n=%2 hardMin=(%3,%4,%5) hardMax=(%6,%7,%8) fullRobustMin=(%9,%10,%11) fullRobustMax=(%12,%13,%14) fullMinShift=(%15,%16,%17)\n" )
                 .arg( source )
                 .arg( points.size() )
                 .arg( hardMin.x(), 0, 'f', 2 ).arg( hardMin.y(), 0, 'f', 2 ).arg( hardMin.z(), 0, 'f', 2 )
                 .arg( hardMax.x(), 0, 'f', 2 ).arg( hardMax.y(), 0, 'f', 2 ).arg( hardMax.z(), 0, 'f', 2 )
                 .arg( fullRobustMin.x(), 0, 'f', 2 ).arg( fullRobustMin.y(), 0, 'f', 2 ).arg( fullRobustMin.z(), 0, 'f', 2 )
                 .arg( fullRobustMax.x(), 0, 'f', 2 ).arg( fullRobustMax.y(), 0, 'f', 2 ).arg( fullRobustMax.z(), 0, 'f', 2 )
                 .arg( fullRobustShift.x(), 0, 'f', 2 ).arg( fullRobustShift.y(), 0, 'f', 2 ).arg( fullRobustShift.z(), 0, 'f', 2 )
                 .toStdWString().c_str() );
    DEBUG_LOG( QString( "[PointCloudFootprint] source=%1 slice=%2 n=%3/%4 z=[%5,%6] alignMin=(%7,%8,%9) alignMax=(%10,%11,%12) reason=%13\n" )
                 .arg( source )
                 .arg( useBottomSlice ? QStringLiteral( "bottom" ) : QStringLiteral( "full-coarse" ) )
                 .arg( bottomSlice.size() )
                 .arg( points.size() )
                 .arg( hardMin.z(), 0, 'f', 2 )
                 .arg( sliceTop, 0, 'f', 2 )
                 .arg( displayMin.x(), 0, 'f', 2 ).arg( displayMin.y(), 0, 'f', 2 ).arg( displayMin.z(), 0, 'f', 2 )
                 .arg( displayMax.x(), 0, 'f', 2 ).arg( displayMax.y(), 0, 'f', 2 ).arg( displayMax.z(), 0, 'f', 2 )
                 .arg( sliceReason )
                 .toStdWString().c_str() );
  };

  if ( hasMetadata )
  {
    const PointCloud normalizedCloud = PointCloudLoader::load( filePath );
    if ( !normalizedCloud.points.isEmpty() && pointCloudLooksNormalizedForDisplay( normalizedCloud ) )
    {
      QVector3D denormCenter = metadataCenter;
      double    denormScale  = metadataScale;

      if ( metadataLooksBuggy )
      {
        // Fallback: estimate real-world centre / scale from the current
        // model mesh instead of trusting the broken metadata record.
        const QString prim = ui->comboPrimitive->currentText();
        const MeshData mesh = BuildMesh::build( prim, this );
        if ( !mesh.isEmpty() )
        {
          QVector3D meshMin = mesh.vertices.first();
          QVector3D meshMax = mesh.vertices.first();
          for ( const QVector3D &v : mesh.vertices )
          {
            if ( v.x() < meshMin.x() ) meshMin.setX( v.x() );
            if ( v.y() < meshMin.y() ) meshMin.setY( v.y() );
            if ( v.z() < meshMin.z() ) meshMin.setZ( v.z() );
            if ( v.x() > meshMax.x() ) meshMax.setX( v.x() );
            if ( v.y() > meshMax.y() ) meshMax.setY( v.y() );
            if ( v.z() > meshMax.z() ) meshMax.setZ( v.z() );
          }
          denormCenter = ( meshMin + meshMax ) * 0.5f;
          const QVector3D meshSize = meshMax - meshMin;
          denormScale = std::max( { static_cast<double>( meshSize.x() ),
                                    static_cast<double>( meshSize.y() ),
                                    static_cast<double>( meshSize.z() ) } ) * 0.8;
          denormScale = std::max( denormScale, 1.0 );
          DEBUG_LOG( QString( "[PointCloud] buggy metadata override: using model mesh center=(%1,%2,%3) scale=%4\n" )
                       .arg( denormCenter.x(), 0, 'f', 2 )
                       .arg( denormCenter.y(), 0, 'f', 2 )
                       .arg( denormCenter.z(), 0, 'f', 2 )
                       .arg( denormScale, 0, 'f', 2 )
                       .toStdWString().c_str() );
        }
      }

      if ( denormalizedFile.open() )
      {
        QVector<QVector3D> restoredPoints;
        restoredPoints.reserve( normalizedCloud.points.size() );
        for ( const QVector3D &p : normalizedCloud.points )
        {
          QVector3D restored = p * static_cast<float>( denormScale ) + denormCenter;
          if ( restored.z() < 0.0f )
            restored.setZ( 0.0f );
          restoredPoints.append( restored );
        }

        // 稳健 bbox：离群点不能主导对齐目标（见 robustBBoxFromPoints）
        setDisplayBBoxFromPoints( restoredPoints, QStringLiteral( "restored" ) );

        QTextStream out( &denormalizedFile );
        for ( const QVector3D &p : restoredPoints )
          out << p.x() << ' ' << p.y() << ' ' << p.z() << '\n';
        out.flush();
        denormalizedFile.flush();
        displayPath = denormalizedFile.fileName();
        DEBUG_LOG( QString( "[PointCloud] display point cloud restored from DL normalization: %1\n" ).arg( displayPath ).toStdWString().c_str() );
      }
    }
    else if ( !normalizedCloud.points.isEmpty() )
    {
      DEBUG_LOG( QString( "[PointCloud] metadata matched, input appears already in display scale: %1\n" ).arg( filePath ).toStdWString().c_str() );
      setDisplayBBoxFromPoints( normalizedCloud.points, QStringLiteral( "display-scale" ) );
    }
  }

  // 到这里还没量到 bbox（无 metadata / 坏记录且没走还原分支）→ 显示的就是文件本身，直接读它
  if ( !hasDisplayBBox )
  {
    const PointCloud asIs = PointCloudLoader::load( displayPath );
    setDisplayBBoxFromPoints( asIs.points, QStringLiteral( "as-is" ) );
  }

  if ( hasDisplayBBox )
  {
    m_displayCloudBBoxMin    = displayMin;
    m_displayCloudBBoxCenter = ( displayMin + displayMax ) * 0.5f;
    m_hasDisplayCloudBBox    = true;
    m_displayCloudSourcePath = filePath;
    DEBUG_LOG( QString( "[PointCloud] align bbox min=(%1,%2,%3) center=(%4,%5,%6) → align target\n" )
                 .arg( m_displayCloudBBoxMin.x(), 0, 'f', 2 )
                 .arg( m_displayCloudBBoxMin.y(), 0, 'f', 2 )
                 .arg( m_displayCloudBBoxMin.z(), 0, 'f', 2 )
                 .arg( m_displayCloudBBoxCenter.x(), 0, 'f', 2 )
                 .arg( m_displayCloudBBoxCenter.y(), 0, 'f', 2 )
                 .arg( m_displayCloudBBoxCenter.z(), 0, 'f', 2 )
                 .toStdWString().c_str() );
  }
  else
  {
    m_hasDisplayCloudBBox = false;
    m_displayCloudSourcePath.clear();
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

  m_pointCloudLayer = loadedLayer;

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

  // 缓存元数据：center（质心）用于反归一化，bbox 极值/底面用于模型对齐 —— 两者语义不同，别混用
  cacheInputMetadata( filePath );

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
  applyDataDrivenParamCorrections( prim, params );

  if ( !params.isEmpty() )
  {
    DEBUG_LOG( QString( "[PointNet] parameter regression done, returned %1 parameters\n" ).arg( params.size() ).toStdWString().c_str() );
    PointNetRunner::applyToUI( this, params );

    // 保存 DL 预测值作为微调锚点，供 resetToDlAnchor() 一键复位
    m_dlAnchorParams = params;
    m_hasDlAnchor = true;
    if ( m_resetAnchorBtn )
      m_resetAnchorBtn->setEnabled( true );

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

  // --- 朝向：DL 不预测 rz（标签不可辨识，见 applyMetadataRz），回填 metadata 的导出真值 ---
  // applyToUI 已在上面执行 → metadata 的 rz 覆盖模型可能给出的值；
  // rz 同时记进 DL 锚点并补一行表格，这样"一键复位"恢复的是完整的自动估计结果（含朝向）。
  if ( applyMetadataRz() )
  {
    if ( m_hasDlAnchor )
    {
      m_dlAnchorParams.insert( QStringLiteral( "poseRotateZ" ), m_metadataRz );
      const int row = ui->tableInverseParams->rowCount();
      ui->tableInverseParams->setRowCount( row + 1 );
      ui->tableInverseParams->setItem( row, 0, new QTableWidgetItem( QStringLiteral( "poseRotateZ (metadata)" ) ) );
      ui->tableInverseParams->setItem( row, 1, new QTableWidgetItem( QString::number( m_metadataRz, 'f', 2 ) ) );
    }
  }
  else
  {
    DEBUG_LOG( QString( "[Align] no metadata rz for %1, keeping current pose rotation\n" ).arg( m_inputDataPath ).toStdWString().c_str() );
  }

  // --- Auto-align：模型参考点 ↔ 点云参考点（bbox 极值，不是质心，见 alignModelToPointCloud） ---
  QVector3D alignTarget;
  if ( pointCloudAlignTarget( alignTarget ) )
    alignModelToPointCloud( BuildMesh::build( ui->comboPrimitive->currentText(), this ),
                            alignTarget, ui->comboPrimitive->currentText(),
                            this, QStringLiteral( "onInverseParams" ) );
  else
    DEBUG_LOG( QString( "[Align] no point cloud bbox for %1, skipping\n" ).arg( m_inputDataPath ).toStdWString().c_str() );

  onUpdatePreview();
}

// ============================================================
// 一键复位：将所有参数恢复到 DL 推理的预测值
// ============================================================
void ParamModelerDock::resetToDlAnchor()
{
  if ( !m_hasDlAnchor || m_dlAnchorParams.isEmpty() )
    return;

  PointNetRunner::applyToUI( this, m_dlAnchorParams );
  schedulePreviewUpdate();

  DEBUG_LOG( L"[DL Anchor] parameters reset to DL prediction\n" );
}
