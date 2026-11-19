#include "parammodeler_dock.h"
#include "ui_parammodeler_dock.h"
#include "buildmesh.h"
#include "parammodeler_alignment_geometry.h"
#include "parammodeler_dlutils.h"
#include "parammodeler_pcdloader.h"
#include "parammodeler_pick3d.h"
#include "parammodeler_pick3d_math.h"
#include "parammodeler_scene3d.h"
#include "parammodeler_fit_metrics.h"
#include "qgsmessagelog.h"

#include <qgisinterface.h>
#include <qgsapplication.h>
#include <qgsvectorlayer.h>
#include <qgs3dmapcanvas.h>
#include <qgs3dmapsettings.h>
#include <qgscameracontroller.h>
#include <Qt3DRender/QCamera>

#include <QAction>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QKeySequence>
#include <QKeyEvent>
#include <QLabel>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMatrix4x4>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTemporaryFile>
#include <QTextStream>
#include <QToolButton>
#include <QToolTip>
#include <QVector3D>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <windows.h>

#define DEBUG_LOG( msg ) OutputDebugStringW( msg )

// ============================================================
// 自动对齐：模型参考点 ↔ 点云参考点
//
// 第一批角锚定模型使用和手动 2D/3D 对齐相同的 UpperOrigin：局部原点正上方
// 的真实顶点。长方体类对应左下顶角，屋顶类对应左下檐口，而不是屋脊/顶点。
// 圆形类和暂未适配的模型仍使用底面中心/旧 bbox 参考点。
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
static QVector3D modelAlignmentReference( const MeshData &mesh, const QString &primitiveType,
                                          ParamModelerDock *dock, bool semanticUpper )
{
    const QMatrix4x4 posMat = modelRotationMatrix( dock );

    QVector3D upperAnchor;
    if ( semanticUpper && usesSemanticUpperAlignment( primitiveType ) &&
         ParamModelerPicking::upperAnchor( mesh.vertices, upperAnchor ) )
      return posMat.map( upperAnchor );

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

void ParamModelerDock::alignModelToPointCloud( const MeshData &mesh, const QVector3D &pcRef,
                                               const QString &primitiveType, const QString &context,
                                               bool semanticUpper )
{
    if ( mesh.vertices.isEmpty() )
    {
        DEBUG_LOG( QString( "[Align] %1 mesh empty, skipped\n" ).arg( context ).toStdWString().c_str() );
        return;
    }

    const bool corner = BuildMesh::usesCornerAnchor( primitiveType );
    // 已适配角锚定类取旋转后的 UpperOrigin，其余类保留旧 bbox/中心语义。
    const QVector3D mRef = modelAlignmentReference( mesh, primitiveType, this, semanticUpper );

    const double tx = static_cast<double>( pcRef.x() ) - static_cast<double>( mRef.x() );
    const double ty = static_cast<double>( pcRef.y() ) - static_cast<double>( mRef.y() );
    const double tz = static_cast<double>( pcRef.z() ) - static_cast<double>( mRef.z() );
    setPoseTranslate( tx, ty, tz );

    if ( primitiveType == QLatin1String( "RecessedRoofHouse" ) )
      alignIndentedInteriorToPointCloud();

    DEBUG_LOG( QString( "[Align] %1 anchor=%2 rz=%3 pcRef=(%4,%5,%6) modelRef=(%7,%8,%9) → tx=%10 ty=%11 tz=%12\n" )
                 .arg( context )
                 .arg( semanticUpper ? QStringLiteral( "upper-origin" )
                                     : ( corner ? QStringLiteral( "corner" ) : QStringLiteral( "center" ) ) )
                 .arg( poseRotateZ(), 0, 'f', 2 )
                 .arg( pcRef.x(), 0, 'f', 2 ).arg( pcRef.y(), 0, 'f', 2 ).arg( pcRef.z(), 0, 'f', 2 )
                 .arg( mRef.x(), 0, 'f', 2 ).arg( mRef.y(), 0, 'f', 2 ).arg( mRef.z(), 0, 'f', 2 )
                 .arg( tx, 0, 'f', 2 ).arg( ty, 0, 'f', 2 ).arg( tz, 0, 'f', 2 )
                 .toStdWString().c_str() );
}

void ParamModelerDock::alignIndentedInteriorToPointCloud()
{
  if ( m_inputDataPath.isEmpty() )
    return;

  const PointCloud cloud = PointCloudLoader::load( m_inputDataPath );
  if ( cloud.points.size() < 100 )
    return;

  const bool normalized = pointCloudLooksNormalizedForDisplay( cloud );
  const bool matchesDisplay = m_hasDisplayCloudBBox &&
    QFileInfo( m_displayCloudSourcePath ).absoluteFilePath().compare(
      QFileInfo( m_inputDataPath ).absoluteFilePath(), Qt::CaseInsensitive ) == 0;
  if ( normalized && !( matchesDisplay && m_displayCloudWasRestored ) &&
       ( !m_hasMetadata || pointCloudMetadataLooksBuggy( m_metadataCenter, m_metadataScale ) ) )
    return;

  const QVector3D restoreCenter = matchesDisplay && m_displayCloudWasRestored
    ? m_displayCloudRestoreCenter : m_metadataCenter;
  const double restoreScale = matchesDisplay && m_displayCloudWasRestored
    ? m_displayCloudRestoreScale : m_metadataScale;
  const QVector3D translation( static_cast<float>( poseTranslateX() ),
                               static_cast<float>( poseTranslateY() ),
                               static_cast<float>( poseTranslateZ() ) );
  bool invertible = false;
  const QMatrix4x4 inverseRotation = modelRotationMatrix( this ).inverted( &invertible );
  if ( !invertible )
    return;

  QVector<QVector3D> localPoints;
  localPoints.reserve( cloud.points.size() );
  for ( const QVector3D &p : cloud.points )
  {
    QVector3D world = normalized ? p * static_cast<float>( restoreScale ) + restoreCenter : p;
    if ( normalized && world.z() < 0.0f )
      world.setZ( 0.0f );
    localPoints.append( inverseRotation.map( world - translation ) );
  }

  QVector3D outerMin, outerMax, hardMin, hardMax;
  robustBBoxFromPoints( localPoints, outerMin, outerMax );
  hardBBoxFromPoints( localPoints, hardMin, hardMax );
  QVector3D innerMin, innerMax;
  double innerHeight = 0.0;
  if ( !estimateIndentedInnerBox( localPoints, outerMin, outerMax, hardMin.z(),
                                  hardMax.z() - hardMin.z(), innerMin, innerMax, innerHeight ) )
    return;

  const double outerLength = icOuterLength();
  const double outerWidth = icOuterWidth();
  const double movableX = std::max( 0.0, outerLength - icInnerLength() );
  const double movableY = std::max( 0.0, outerWidth - icInnerWidth() );
  if ( movableX < 1e-6 || movableY < 1e-6 )
    return;
  const double offsetX = qBound( 0.0, static_cast<double>( innerMin.x() ), movableX );
  const double offsetY = qBound( 0.0, static_cast<double>( innerMin.y() ), movableY );
  ui->spinBoxICOffsetX->setValue( offsetX / movableX );
  ui->spinBoxICOffsetY->setValue( offsetY / movableY );

  DEBUG_LOG( QString( "[AlignInner] top-left=(%1,%2); outer pose and inner size unchanged\n" )
               .arg( offsetX, 0, 'f', 2 ).arg( offsetY, 0, 'f', 2 )
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
  m_evaluationRaw.clear();
  m_evaluationCorrected.clear();
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
bool ParamModelerDock::pointCloudAlignTarget( QVector3D &out, bool *usesSemanticUpper )
{
  if ( usesSemanticUpper )
    *usesSemanticUpper = false;
  const QString primitiveType = this->primitiveType();
  const bool corner = BuildMesh::usesCornerAnchor( primitiveType );
  const bool wantsSemanticUpper = usesSemanticUpperAlignment( primitiveType );
  const MeshData mesh = BuildMesh::build( primitiveType, this );
  QVector3D rotatedUpperOffset;
  bool hasSemanticUpper = false;
  if ( wantsSemanticUpper )
  {
    QVector3D localUpper;
    if ( ParamModelerPicking::upperAnchor( mesh.vertices, localUpper ) )
    {
      hasSemanticUpper = ParamModelerAlignmentGeometry::upperOffsetFromRotatedMesh(
        mesh.vertices, modelRotationMatrix( this ), localUpper, rotatedUpperOffset ) &&
        rotatedUpperOffset.z() > 1e-6f;
    }
  }

  const auto semanticUpperFallbackFromBBox = [&]( const QVector3D &bboxMin,
                                                  const QVector3D &bboxCenter,
                                                  QVector3D &target ) -> bool
  {
    if ( !hasSemanticUpper )
      return false;

    const double cloudHeight = static_cast<double>( bboxCenter.z() - bboxMin.z() ) * 2.0;
    const double upperOffset = static_cast<double>( rotatedUpperOffset.z() );
    if ( cloudHeight > 1e-6 && upperOffset > cloudHeight * 1.5 )
      return false;

    target = bboxMin + rotatedUpperOffset;
    return std::isfinite( target.x() ) &&
           std::isfinite( target.y() ) &&
           std::isfinite( target.z() );
  };

  // 优先把手动对齐默认红叉（UpperOrigin）对到点云的同语义角点。
  // 估计失败时下方 bbox 兜底也保持 upper-origin 语义，不能把上部交点吸到底角。
  if ( wantsSemanticUpper && !m_inputDataPath.isEmpty() && hasSemanticUpper )
  {
    const double rz = m_hasMetadataRz ? m_metadataRz : poseRotateZ();
    const bool useDisplayCoordinates = m_hasDisplayCloudBBox &&
      QFileInfo( m_displayCloudSourcePath ).absoluteFilePath().compare(
        QFileInfo( m_inputDataPath ).absoluteFilePath(), Qt::CaseInsensitive ) == 0;
    QString detail;
    if ( semanticUpperPointCloudTarget( m_inputDataPath, primitiveType, rz,
                                        static_cast<double>( rotatedUpperOffset.z() ),
                                        useDisplayCoordinates, m_displayCloudWasRestored,
                                        m_displayCloudRestoreCenter, m_displayCloudRestoreScale,
                                        out, detail ) )
    {
      if ( usesSemanticUpper )
        *usesSemanticUpper = true;
      DEBUG_LOG( QString( "[AlignTarget] source=semantic-upper primitive=%1 detail=%2 pcRef=(%3,%4,%5)\n" )
                   .arg( primitiveType, detail )
                   .arg( out.x(), 0, 'f', 3 )
                   .arg( out.y(), 0, 'f', 3 )
                   .arg( out.z(), 0, 'f', 3 )
                   .toStdWString().c_str() );
      return true;
    }
    DEBUG_LOG( QString( "[AlignTarget] semantic-upper unavailable for %1; using upper-height bbox fallback when possible\n" )
                 .arg( primitiveType ).toStdWString().c_str() );
  }

  // 场景里显示的正好就是当前输入文件 → 用实测的显示 bbox（最可靠）
  if ( m_hasDisplayCloudBBox && !m_inputDataPath.isEmpty() &&
       QFileInfo( m_displayCloudSourcePath ).absoluteFilePath().compare(
         QFileInfo( m_inputDataPath ).absoluteFilePath(), Qt::CaseInsensitive ) == 0 )
  {
    if ( wantsSemanticUpper && semanticUpperFallbackFromBBox( m_displayCloudBBoxMin, m_displayCloudBBoxCenter, out ) )
    {
      if ( usesSemanticUpper )
        *usesSemanticUpper = true;
      DEBUG_LOG( QString( "[AlignTarget] source=display-upper-fallback primitive=%1 pcRef=(%2,%3,%4) displayMin=(%5,%6,%7) upperOffset=%8\n" )
                   .arg( primitiveType )
                   .arg( out.x(), 0, 'f', 3 )
                   .arg( out.y(), 0, 'f', 3 )
                   .arg( out.z(), 0, 'f', 3 )
                   .arg( m_displayCloudBBoxMin.x(), 0, 'f', 3 )
                   .arg( m_displayCloudBBoxMin.y(), 0, 'f', 3 )
                   .arg( m_displayCloudBBoxMin.z(), 0, 'f', 3 )
                   .arg( rotatedUpperOffset.z(), 0, 'f', 3 )
                   .toStdWString().c_str() );
      return true;
    }

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
    if ( wantsSemanticUpper && semanticUpperFallbackFromBBox( m_metadataBBoxMin, m_metadataBBoxCenter, out ) )
    {
      if ( usesSemanticUpper )
        *usesSemanticUpper = true;
      DEBUG_LOG( QString( "[AlignTarget] source=metadata-upper-fallback primitive=%1 pcRef=(%2,%3,%4) metadataMin=(%5,%6,%7) upperOffset=%8\n" )
                   .arg( primitiveType )
                   .arg( out.x(), 0, 'f', 3 )
                   .arg( out.y(), 0, 'f', 3 )
                   .arg( out.z(), 0, 'f', 3 )
                   .arg( m_metadataBBoxMin.x(), 0, 'f', 3 )
                   .arg( m_metadataBBoxMin.y(), 0, 'f', 3 )
                   .arg( m_metadataBBoxMin.z(), 0, 'f', 3 )
                   .arg( rotatedUpperOffset.z(), 0, 'f', 3 )
                   .toStdWString().c_str() );
      return true;
    }

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

void ParamModelerDock::stopManualTranslate3D()
{
  if ( m_manualTranslate3DTool )
  {
    m_manualTranslate3DTool->picked = {};
    m_manualTranslate3DTool->stopped = {};
    if ( m_manualTranslate3DCanvas && m_manualTranslate3DCanvas->mapTool() == m_manualTranslate3DTool )
      m_manualTranslate3DCanvas->setMapTool( m_previous3DMapTool.data() );
    delete m_manualTranslate3DTool.data();
  }
  m_manualTranslate3DCanvas.clear();
  m_previous3DMapTool.clear();
  if ( m_manualTranslate3DBtn )
    m_manualTranslate3DBtn->setText( tr( "Align in 3D" ) );
}

void ParamModelerDock::startManualTranslate3D()
{
  startTranslate3D( false );
}

void ParamModelerDock::startTranslate3D( bool fitCorner )
{
  if ( m_manualTranslate3DTool )
  {
    stopManualTranslate3D();
    return;
  }
  auto *layer = qobject_cast<QgsVectorLayer *>( m_pointCloudLayer.data() );
  if ( !mIface || !layer || layer->featureCount() <= 0 )
  {
    QMessageBox::warning( this, tr( "3D align" ), tr( "Load a point cloud into the 3D view first." ) );
    return;
  }
  if ( !m_realtimeModelLoaded ) onLoadToQGIS3D( false );
  const auto canvases = mIface->mapCanvases3D();
  // Match ensureRealtimePreviewCanvas(): first initialized 3D canvas.
  Qgs3DMapCanvas *canvas = nullptr;
  for ( Qgs3DMapCanvas *candidate : canvases )
  {
    if ( candidate && candidate->scene() ) { canvas = candidate; break; }
  }
  if ( !m_realtimeModelLoaded || !canvas || !canvas->scene() || !canvas->mapSettings() ||
       !canvas->cameraController() )
  {
    QMessageBox::warning( this, tr( "3D align" ), tr( "The model and point cloud must be loaded into a valid 3D view first." ) );
    return;
  }
  if ( canvas->mapSettings()->crs() != layer->crs() ||
       canvas->mapSettings()->sceneMode() != Qgis::SceneMode::Local )
  {
    QMessageBox::warning( this, tr( "3D align" ), tr( "3D picking requires a local scene using the point cloud's CRS." ) );
    return;
  }
  MeshData sourceMesh = BuildMesh::build( this->primitiveType(), this );
  m_manualTranslate3DLocalAnchor = QVector3D();
  if ( !anchorLocalPoint( m_manualAlign3DAnchorMode, sourceMesh, m_manualTranslate3DLocalAnchor ) )
  {
    QMessageBox::warning( this, tr( "3D align" ), tr( "The model has no valid alignment anchor." ) );
    return;
  }
  auto source = [this] {
    const QVector3D anchor = modelRotationMatrix( this ).map( m_manualTranslate3DLocalAnchor );
    return QgsPoint( poseTranslateX() + anchor.x(), poseTranslateY() + anchor.y(), poseTranslateZ() + anchor.z() );
  };
  auto *tool = new ParamModelerPick3D( canvas, layer, source );
  if ( fitCorner )
  {
    const QString type = this->primitiveType();
    if ( type != QStringLiteral( "FlatRoofHouse" ) &&
         type != QStringLiteral( "FlatTopHipRoofHouse" ) &&
         type != QStringLiteral( "LShapedHouse" ) &&
         type != QStringLiteral( "RecessedRoofHouse" ) )
    {
      delete tool;
      return;
    }
    const auto vertices = sourceMesh.vertices;
    QVector3D minimum = vertices.first(), maximum = minimum;
    for ( const QVector3D &v : vertices )
      for ( int axis = 0; axis < 3; ++axis )
      {
        minimum[axis] = std::min( minimum[axis], v[axis] );
        maximum[axis] = std::max( maximum[axis], v[axis] );
      }
    tool->enableCornerFit( std::max( 0.001, static_cast<double>( ( maximum - minimum ).length() ) * 0.15 ),
                           modelRotationMatrix( this ).mapVector( QVector3D( 0, 0, 1 ) ), window() );
  }
  m_manualTranslate3DTool = tool;
  m_manualTranslate3DCanvas = canvas;
  m_previous3DMapTool = canvas->mapTool();
  tool->stopped = [this] { stopManualTranslate3D(); };
  tool->picked = [this, source]( const QgsPoint &target ) {
    const QgsPoint from = source();
    const double tx = poseTranslateX() + target.x() - from.x();
    const double ty = poseTranslateY() + target.y() - from.y();
    const double tz = poseTranslateZ() + target.z() - from.z();
    if ( tx < ui->spinBoxTX->minimum() || tx > ui->spinBoxTX->maximum() ||
         ty < ui->spinBoxTY->minimum() || ty > ui->spinBoxTY->maximum() ||
         tz < ui->spinBoxTZ->minimum() || tz > ui->spinBoxTZ->maximum() )
    {
      stopManualTranslate3D();
      QMessageBox::warning( this, tr( "3D align" ), tr( "Target exceeds the model translation range." ) );
      return;
    }
    setPoseTranslate( tx, ty, tz );
    enableAnchorLock( m_manualAlign3DAnchorMode,
                      QVector3D( static_cast<float>( target.x() ),
                                 static_cast<float>( target.y() ),
                                 static_cast<float>( target.z() ) ) );
    const QString message = QStringLiteral( "[ManualAlign3D] source=(%1,%2,%3) target=(%4,%5,%6) translation=(%7,%8,%9)" )
      .arg( from.x(), 0, 'f', 3 ).arg( from.y(), 0, 'f', 3 ).arg( from.z(), 0, 'f', 3 )
      .arg( target.x(), 0, 'f', 3 ).arg( target.y(), 0, 'f', 3 ).arg( target.z(), 0, 'f', 3 )
      .arg( poseTranslateX(), 0, 'f', 3 ).arg( poseTranslateY(), 0, 'f', 3 ).arg( poseTranslateZ(), 0, 'f', 3 );
    QgsMessageLog::logMessage( message, QStringLiteral( "ParamModeler" ), Qgis::MessageLevel::Info, false );
    DEBUG_LOG( QString( message + QLatin1Char( '\n' ) ).toStdWString().c_str() );
    stopManualTranslate3D();
    onUpdatePreview();
  };
  connect( tool, &QObject::destroyed, this, [this] {
    if ( m_manualTranslate3DBtn )
      m_manualTranslate3DBtn->setText( tr( "Align in 3D" ) );
  } );
  canvas->setMapTool( tool );
  canvas->requestActivate();
  m_manualTranslate3DBtn->setText( tr( "Cancel 3D" ) );
}

bool ParamModelerDock::loadPointCloudToQGIS3D( const QString &filePath, bool showMessage )
{
  resetFitEvaluation();
  m_evaluationCloudAvailable = false;
  m_hasDisplayCloudBBox = false;
  m_displayCloudSourcePath.clear();
  m_displayCloudWasRestored = false;
  m_displayCloudRestoreCenter = QVector3D();
  m_displayCloudRestoreScale = 1.0;
  stopManualTranslate3D();
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
  // Replacing the old layer emits destroyed(); publish the new sample afterwards.
  QVector<QVector3D> fitCloudPoints;
  bool hasDisplayBBox = false;
  auto setDisplayBBoxFromPoints = [&]( const QVector<QVector3D> &points, const QString &source )
  {
    if ( points.isEmpty() )
      return;

    const int count = std::min( points.size(), 2048 );
    fitCloudPoints.clear();
    fitCloudPoints.reserve( count );
    for ( int i = 0; i < count; ++i )
      fitCloudPoints.append( points[static_cast<qint64>( i ) * points.size() / count] );

    QVector3D hardMin, hardMax;
    hardBBoxFromPoints( points, hardMin, hardMax );
    QVector3D fullRobustMin, fullRobustMax;
    robustBBoxFromPoints( points, fullRobustMin, fullRobustMax );
    m_evaluationCloudMin = fullRobustMin;
    m_evaluationCloudMax = fullRobustMax;

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
    // The tower's short lower cylinder is partly occluded, while its wider
    // eave supplies a nearly complete circle. A bottom-slice bbox shifts its
    // apparent center toward the visible wall arc.
    const QString primitive = this->primitiveType();
    const bool tower = primitive == QLatin1String( "CurvedRoofRoundHouse" );
    const bool round = primitive == QLatin1String( "RoundFlatRoofHouse" ) || primitive == QLatin1String( "ConicalRoofHouse" ) ||
                       primitive == QLatin1String( "DomeRoofHouse" );
    bool useBottomSlice = !tower && !round && bottomSlice.size() >= minSlicePoints;
    QString sliceReason = tower ? QStringLiteral( "tower-full-extent" )
                                : ( useBottomSlice ? QStringLiteral( "ok" ) : QStringLiteral( "too-few-points" ) );
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
    if ( round || tower )
    {
      QPointF center;
      sliceReason = tower ? QStringLiteral( "tower-full-extent" ) : QStringLiteral( "round-full-extent" );
      const bool fitted = tower
        ? ParamModelerAlignmentGeometry::towerCenter( points, fullRobustMin, fullRobustMax, center )
        : ParamModelerAlignmentGeometry::roundBaseCenter( points, fullRobustMin, fullRobustMax, center );
      if ( fitted )
      {
        // Only the alignment-box center changes; evaluation keeps the measured bbox.
        const QVector3D halfSize = ( fullRobustMax - fullRobustMin ) * 0.5f;
        displayMin.setX( center.x() - halfSize.x() );
        displayMin.setY( center.y() - halfSize.y() );
        displayMax.setX( center.x() + halfSize.x() );
        displayMax.setY( center.y() + halfSize.y() );
        sliceReason = tower ? QStringLiteral( "tower-circle-center" ) : QStringLiteral( "round-wall-circle" );
      }
    }
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
        const QString prim = this->primitiveType();
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
        m_displayCloudWasRestored = true;
        m_displayCloudRestoreCenter = denormCenter;
        m_displayCloudRestoreScale = denormScale;

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
    mIface, displayPath, layerName, this, &errorMessage, m_displayFitPoints
  );
  if ( !loadedLayer )
  {
    resetFitEvaluation();
    m_hasDisplayCloudBBox = false;
    m_displayCloudSourcePath.clear();
    m_displayCloudWasRestored = false;
    if ( showMessage )
      QMessageBox::warning( this, tr( "Load failed" ), errorMessage );
    return false;
  }

  m_pointCloudLayer = loadedLayer;
  m_fitCloudPoints.swap( fitCloudPoints );
  m_evaluationCloudAvailable = hasDisplayBBox;
  connect( loadedLayer, &QObject::destroyed, this, [this] {
    if ( !m_pointCloudLayer )
      resetFitEvaluation();
  } );
  scheduleFitEvaluation();

  if ( showMessage )
    QMessageBox::information( this, tr( "Load succeeded" ), tr( "Point cloud loaded successfully.\nLayer: %1\n\nYou can view and adjust it in the 3D scene." ).arg( layerName ) );
  DEBUG_LOG( QString( "[PointCloud] load success, layer: %1\n" ).arg( layerName ).toStdWString().c_str() );
  return true;
}


void ParamModelerDock::resetFitEvaluation()
{
  m_fitCloudPoints.clear();
  m_displayFitPoints.clear();
  m_fitBaseline.clear();
  if ( m_fitTimer )
    m_fitTimer->stop();
  ParamModelerScene3D::setFitMetricText( mIface, QString() );
  ParamModelerScene3D::setPointCloudFitColors( m_pointCloudLayer );
  updateFaceFitView( {}, {} );
  if ( m_fitDetailsLabel ) m_fitDetailsLabel->setText( tr( "Load a model and point cloud to evaluate." ) );
}

using FaceFitControls = std::array<QDoubleSpinBox *, 3>;

static FaceFitControls faceFitControls( const QString &primitive, Ui::ParamModelerDock *ui )
{
  if ( primitive == QLatin1String( "FlatRoofHouse" ) )
    return { ui->spinBoxCLength, ui->spinBoxCWidth, ui->spinBoxCHeight };
  if ( primitive == QLatin1String( "LShapedHouse" ) )
    return { ui->spinBoxLTotalLength, ui->spinBoxLTotalWidth, ui->spinBoxLHeight };
  if ( primitive == QLatin1String( "RecessedRoofHouse" ) )
    return { ui->spinBoxICLength, ui->spinBoxICWidth, ui->spinBoxICHeight };
  if ( primitive == QLatin1String( "GableRoofHouse" ) )
    return { ui->spinBoxGRLength, ui->spinBoxGRWidth, ui->spinBoxGRHeightWall };
  if ( primitive == QLatin1String( "PyramidalRoofHouse" ) )
    return { ui->spinBoxPRLength, ui->spinBoxPRWidth, ui->spinBoxPRHeightWall };
  if ( primitive == QLatin1String( "FlatTopHipRoofHouse" ) )
    return { ui->spinBoxTPRBottomLength, ui->spinBoxTPRBottomWidth, ui->spinBoxTPRHeightWall };
  if ( primitive == QLatin1String( "ShortRidgeHipRoofHouse" ) )
    return { ui->spinBoxAGHLength, ui->spinBoxAGHWidth, ui->spinBoxAGHHeightWall };
  return {};
}

struct FaceFitState
{
  QString primitive;
  std::array<double, 3> dimensions;
  std::array<double, 3> translation;
  std::array<double, 3> rotation;
  double wallFraction;
  double ridgeFraction;
  QSizeF roofTop;
  QPointF roofTopPosition;
  QPointF notch;
  double recessDepth;

  bool operator==( const FaceFitState &other ) const
  {
    return primitive == other.primitive && dimensions == other.dimensions && translation == other.translation &&
           rotation == other.rotation && wallFraction == other.wallFraction && ridgeFraction == other.ridgeFraction &&
           roofTop == other.roofTop && roofTopPosition == other.roofTopPosition && notch == other.notch &&
           recessDepth == other.recessDepth;
  }
};

static QDoubleSpinBox *faceFitWallRatio( const QString &primitive, Ui::ParamModelerDock *ui )
{
  if ( primitive == QLatin1String( "GableRoofHouse" ) ) return ui->spinBoxGRHeightRoof;
  if ( primitive == QLatin1String( "PyramidalRoofHouse" ) ) return ui->spinBoxPRHeightRoof;
  if ( primitive == QLatin1String( "FlatTopHipRoofHouse" ) ) return ui->spinBoxTPRHeightRoof;
  if ( primitive == QLatin1String( "ShortRidgeHipRoofHouse" ) ) return ui->spinBoxAGHRoofHeight;
  return nullptr;
}

static FaceFitState faceFitState( const ParamModelerDock *dock, Ui::ParamModelerDock *ui, const FaceFitControls &controls )
{
  const QString primitive = dock->primitiveType();
  const bool gable = primitive == QLatin1String( "GableRoofHouse" );
  const bool shortRidge = primitive == QLatin1String( "ShortRidgeHipRoofHouse" );
  const bool flatHip = primitive == QLatin1String( "FlatTopHipRoofHouse" );
  const bool recessed = primitive == QLatin1String( "RecessedRoofHouse" );
  const auto *ratio = faceFitWallRatio( primitive, ui );
  return { primitive, { controls[0]->value(), controls[1]->value(), controls[2]->value() },
           { dock->poseTranslateX(), dock->poseTranslateY(), dock->poseTranslateZ() },
           { dock->poseRotateX(), dock->poseRotateY(), dock->poseRotateZ() },
           ratio ? std::clamp( ratio->value(), 0.05, 0.95 ) : 1.0,
           gable ? std::clamp( dock->gabledRoofRidgeRatio(), 0.2, 0.8 ) : shortRidge ? dock->aghRidgeRatio() : 0.5,
           recessed ? QSizeF( dock->icInnerLength(), dock->icInnerWidth() ) :
           flatHip ? QSizeF( dock->tpTopLength(), dock->tpTopWidth() ) : QSizeF( shortRidge ? dock->aghRidgeLength() : 0, 0 ),
           recessed ? QPointF( ui->spinBoxICOffsetX->value(), ui->spinBoxICOffsetY->value() ) :
           flatHip ? dock->tpTopPosition() : QPointF( shortRidge ? dock->aghRidgePositionX() : 0.5, 0.5 ),
           primitive == QLatin1String( "LShapedHouse" ) ? QPointF( dock->LWingRatio(), dock->LWingWidthRatio() ) : QPointF(),
           recessed ? dock->icInnerHeight() : 0.0 };
}

struct FaceFitDirection
{
  int view;
  int side = -1;
};

static std::optional<FaceFitDirection> pickFaceFitDirection( const MeshData &mesh, const QVector3D &nearPoint,
                                                           const QVector3D &farPoint, const std::array<double, 3> &dimensions,
                                                           bool lShaped, bool recessed )
{
  const QVector3D ray = farPoint - nearPoint;
  const QVector3D direction = ray.normalized();
  double closest = ray.length();
  std::optional<FaceFitDirection> result;
  for ( int i = 0; i + 2 < mesh.indices.size(); i += 3 )
  {
    const QVector3D a = mesh.vertices[mesh.indices[i]];
    const QVector3D ab = mesh.vertices[mesh.indices[i + 1]] - a;
    const QVector3D ac = mesh.vertices[mesh.indices[i + 2]] - a;
    const QVector3D normal = QVector3D::crossProduct( ab, ac );
    const QVector3D cross = QVector3D::crossProduct( direction, ac );
    const double determinant = QVector3D::dotProduct( ab, cross );
    if ( std::abs( determinant ) <= normal.length() * 1e-6 ) continue;
    const QVector3D fromA = nearPoint - a;
    const double u = QVector3D::dotProduct( fromA, cross ) / determinant;
    const QVector3D crossFromA = QVector3D::crossProduct( fromA, ab );
    const double v = QVector3D::dotProduct( direction, crossFromA ) / determinant;
    if ( u < -1e-6 || v < -1e-6 || u + v > 1 + 1e-6 ) continue;
    const double distance = QVector3D::dotProduct( ac, crossFromA ) / determinant;
    if ( distance < 0 || distance >= closest ) continue;
    closest = distance;
    if ( std::abs( normal.z() ) > normal.length() * 1e-6 )
    {
      const bool base = a.z() == 0 && ab.z() == 0 && ac.z() == 0;
      result = base ? std::nullopt : std::make_optional( FaceFitDirection{ 0 } );
    }
    else
    {
      const int axis = std::abs( normal.x() ) > std::abs( normal.y() ) ? 0 : 1;
      const QVector3D hit = nearPoint + direction * distance;
      // The walls inside the L's notch face the positive model axes.
      const bool positive = lShaped ? a[axis] > 0 : hit[axis] >= dimensions[axis] / 2;
      const bool insideRecess = recessed && a[axis] > 1e-6 && a[axis] < dimensions[axis] - 1e-6;
      result = insideRecess ? FaceFitDirection{ 0 } : FaceFitDirection{ 2 - axis, positive ? 1 : 0 };
    }
  }
  return result;
}

static std::optional<FaceFitDirection> pickFaceFitScreen( const MeshData &mesh, const std::array<double, 3> &dimensions,
                                                        const QMatrix4x4 &localToClip, const QSize &viewport,
                                                        const QPoint &position, bool lShaped, bool recessed )
{
  if ( viewport.isEmpty() ) return std::nullopt;
  bool invertible = false;
  const QMatrix4x4 clipToLocal = localToClip.inverted( &invertible );
  if ( !invertible ) return std::nullopt;
  const double x = 2.0 * position.x() / viewport.width() - 1.0;
  const double y = 1.0 - 2.0 * position.y() / viewport.height();
  const auto nearPoint = clipToLocal * QVector4D( x, y, -1, 1 );
  const auto farPoint = clipToLocal * QVector4D( x, y, 1, 1 );
  if ( nearPoint.w() == 0 || farPoint.w() == 0 ) return std::nullopt;
  return pickFaceFitDirection( mesh, nearPoint.toVector3DAffine(), farPoint.toVector3DAffine(), dimensions, lShaped, recessed );
}

static double faceFitTopRotation( const QMatrix4x4 &localToCamera )
{
  // In the 2D scene +Y is down. Use the 3D camera's screen-right direction in model XY.
  const auto right = localToCamera.row( 0 );
  const auto up = localToCamera.row( 1 );
  const double radians = std::hypot( right.x(), right.y() ) > 1e-6
                         ? std::atan2( right.y(), right.x() )
                         : std::atan2( -up.x(), up.y() );
  return radians * 180.0 / std::acos( -1.0 );
}

static Qgs3DMapCanvas *faceFitCanvas( QgisInterface *iface )
{
  if ( iface )
    for ( Qgs3DMapCanvas *canvas : iface->mapCanvases3D() )
      if ( canvas && canvas->scene() ) return canvas;
  return nullptr;
}

static double currentFaceFitTopRotation( QgisInterface *iface, const ParamModelerDock *dock )
{
  auto *canvas = faceFitCanvas( iface );
  if ( !canvas || !canvas->cameraController() ) return 0;
  return faceFitTopRotation( canvas->cameraController()->camera()->viewMatrix() * modelRotationMatrix( dock ) );
}

class ParamModelerFaceFitPick : public QObject
{
public:
  explicit ParamModelerFaceFitPick( QDialog *dialog ) : QObject( dialog ), m_dialog( dialog ) {}
  ~ParamModelerFaceFitPick() override { stop(); }

  void start( Qgs3DMapCanvas *canvas )
  {
    stop();
    m_canvas = canvas;
    m_cursor = canvas->cursor();
    canvas->installEventFilter( this );
    canvas->setCursor( Qt::CrossCursor );
    m_canvasDestroyed = connect( canvas, &QObject::destroyed, this, [this] { finish(); } );
    m_dialog->hide();
    canvas->requestActivate();
  }

  std::function<bool( Qgs3DMapCanvas *, const QPoint & )> pick;

protected:
  bool eventFilter( QObject *watched, QEvent *event ) override
  {
    if ( watched != m_canvas ) return false;
    if ( event->type() == QEvent::Hide )
      finish();
    if ( event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>( event )->key() == Qt::Key_Escape )
    {
      finish();
      return true;
    }
    if ( event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease )
    {
      const auto *mouse = static_cast<QMouseEvent *>( event );
      if ( mouse->button() != Qt::LeftButton && mouse->button() != Qt::RightButton ) return false;
      if ( event->type() == QEvent::MouseButtonRelease &&
           ( mouse->button() == Qt::RightButton || pick( m_canvas, mouse->pos() ) ) )
        finish();
      return true;
    }
    return false;
  }

private:
  void stop()
  {
    disconnect( m_canvasDestroyed );
    if ( m_canvas )
    {
      m_canvas->removeEventFilter( this );
      m_canvas->setCursor( m_cursor );
      m_canvas.clear();
    }
    QToolTip::hideText();
  }

  void finish()
  {
    stop();
    QTimer::singleShot( 0, m_dialog, [dialog = m_dialog] {
      dialog->show();
      dialog->raise();
      dialog->activateWindow();
    } );
  }

  QDialog *m_dialog;
  QPointer<Qgs3DMapCanvas> m_canvas;
  QMetaObject::Connection m_canvasDestroyed;
  QCursor m_cursor;
};

class ParamModelerFaceFitView : public QGraphicsView
{
public:
  enum class ResizeTarget { Outline, Wall, RoofTop, Notch, RecessDepth };
  std::function<void( const QRectF &, Qt::Edges, int, std::optional<double>, ResizeTarget )> resizeOutline;

  explicit ParamModelerFaceFitView( QAction *undoAction, QWidget *parent )
    : QGraphicsView( parent ), m_undoAction( undoAction )
  {
    setScene( new QGraphicsScene( this ) );
    setBackgroundBrush( Qt::white );
    setDragMode( ScrollHandDrag );
    setTransformationAnchor( AnchorUnderMouse );
    viewport()->setMouseTracking( true );
  }

  void syncResizeHistory( const FaceFitState &current )
  {
    if ( !m_resizeHistory.isEmpty() && !( current == m_lastResize ) ) clearResizeHistory();
  }

  void recordResize( const FaceFitState &before, const FaceFitState &after )
  {
    if ( before == after ) return;
    syncResizeHistory( before );
    m_resizeHistory.append( before );
    m_lastResize = after;
    m_undoAction->setEnabled( true );
  }

  bool undoResize( const FaceFitState &current, FaceFitState &previous )
  {
    syncResizeHistory( current );
    if ( m_resizeHistory.isEmpty() ) return false;
    previous = m_resizeHistory.takeLast();
    m_lastResize = previous;
    m_undoAction->setEnabled( !m_resizeHistory.isEmpty() );
    m_dragEdges = {};
    m_dragWallFraction.reset();
    return true;
  }

  int selectedSide( const QString &primitive, int face ) const
  {
    return m_roofType == primitive && m_face == face ? m_side : -1;
  }

  void selectSide( const QString &primitive, FaceFitDirection direction )
  {
    if ( m_roofType != primitive || m_face != direction.view || m_side != direction.side )
      clearFaceSample();
    m_roofType = primitive;
    m_face = direction.view;
    m_side = direction.side;
  }

  bool hasFaceSample( const QString &primitive, int face, double band ) const
  {
    return m_faceBaseline && m_roofType == primitive && m_face == face && m_sampleBand == band;
  }

  void clearFaceSample()
  {
    m_faceIndices.clear();
    m_faceSample.clear();
    m_faceBaseline.reset();
  }

  void setFaceSample( QVector<int> indices, const QVector<QVector3D> &cloud, double band,
                      const MeshData &mesh, const QMatrix4x4 &pose )
  {
    m_faceIndices = std::move( indices );
    const int count = std::min( m_faceIndices.size(), 2048 );
    m_faceSample.reserve( count );
    for ( int i = 0; i < count; ++i )
      m_faceSample.append( cloud[m_faceIndices[static_cast<qint64>( i ) * m_faceIndices.size() / count]] );
    m_sampleBand = band;
    m_faceBaseline = std::make_unique<ParamModelerFit::Baseline>( mesh, m_faceSample, pose );
  }

  const QVector<int> &faceIndices() const { return m_faceIndices; }
  const QVector<QVector3D> &faceSample() const { return m_faceSample; }
  const ParamModelerFit::Metrics &initialFaceMetrics( double tolerance ) const
  {
    return m_faceBaseline->metrics( tolerance );
  }

  void setProjection( const QRectF &outline, QVector<QPointF> fitted = {}, QVector<QPointF> missed = {},
                      const QString &roofType = {}, int face = 0, double ridgeFraction = 0.5,
                      double wallFraction = 1.0, int side = -1, double minWallFraction = 1.0, double maxWallFraction = 1.0,
                      const QSizeF &roofTop = QSizeF( 0, 0 ), const QPointF &roofTopPosition = QPointF( 0.5, 0.5 ),
                      const QPointF &notch = {} )
  {
    if ( outline.isEmpty() )
    {
      clearResizeHistory();
      clearFaceSample();
    }
    const bool first = m_outline.isEmpty();
    if ( outline != m_outline || roofType != m_roofType || face != m_face || side != m_side ||
         roofTop != m_roofTop || roofTopPosition != m_roofTopPosition || notch != m_notch )
    {
      m_dragEdges = {};
      m_dragWallFraction.reset();
    }
    m_outline = outline;
    m_roofType = roofType;
    m_face = face;
    m_side = side;
    m_ridgeFraction = ridgeFraction;
    m_wallFraction = wallFraction;
    m_minWallFraction = minWallFraction;
    m_maxWallFraction = maxWallFraction;
    m_roofTop = roofTop;
    m_roofTopPosition = roofTopPosition;
    m_notch = notch;
    m_fitted = std::move( fitted );
    m_missed = std::move( missed );
    const double pad = std::max( 0.5, 0.15 * std::max( outline.width(), outline.height() ) );
    m_focus = outline.adjusted( -pad, -pad, pad, pad );
    QRectF bounds = m_focus;
    for ( const auto &points : { &m_fitted, &m_missed } )
      for ( const auto &point : *points )
        bounds |= QRectF( point.x() - pad, point.y() - pad, 2 * pad, 2 * pad );
    setSceneRect( bounds );
    if ( first && !outline.isEmpty() ) fitOutline();
    viewport()->update();
  }

  void fitOutline()
  {
    m_dragEdges = {};
    m_dragWallFraction.reset();
    if ( !m_outline.isEmpty() )
    {
      QTransform transform;
      transform.rotate( m_viewRotation );
      const QRectF bounds = transform.mapRect( m_focus );
      const double scale = std::min( std::max( 1, viewport()->width() - 4 ) / bounds.width(),
                                     std::max( 1, viewport()->height() - 4 ) / bounds.height() );
      transform.scale( scale, scale );
      setTransform( transform );
      centerOn( m_focus.center() );
    }
    viewport()->update();
  }

  void setViewRotation( double degrees )
  {
    if ( std::abs( std::remainder( degrees - m_viewRotation, 360.0 ) ) < 1e-6 ) return;
    m_viewRotation = degrees;
    fitOutline();
  }

protected:
  void mousePressEvent( QMouseEvent *event ) override
  {
    if ( event->button() == Qt::LeftButton )
    {
      m_dragEdges = resizeHandleAt( event->pos(), m_dragTarget );
      if ( m_dragEdges )
      {
        if ( m_dragTarget == ResizeTarget::Wall || m_dragTarget == ResizeTarget::RecessDepth )
          m_dragWallFraction = m_wallFraction;
        m_dragStart = event->pos();
        m_dragOutline = resizeRect( m_dragTarget );
        event->accept();
        return;
      }
    }
    QGraphicsView::mousePressEvent( event );
  }

  void mouseMoveEvent( QMouseEvent *event ) override
  {
    if ( m_dragEdges )
    {
      previewResize( event->pos() );
      event->accept();
      return;
    }
    QGraphicsView::mouseMoveEvent( event );
    if ( event->buttons() ) return;
    ResizeTarget target;
    const auto edges = resizeHandleAt( event->pos(), target );
    const bool horizontal = bool( edges & ( Qt::LeftEdge | Qt::RightEdge ) );
    const bool vertical = bool( edges & ( Qt::TopEdge | Qt::BottomEdge ) );
    if ( horizontal || vertical )
    {
      const double x = horizontal ? 1 : 0;
      const double y = vertical ? ( horizontal && edges.testFlag( Qt::LeftEdge ) != edges.testFlag( Qt::TopEdge ) ? -1 : 1 ) : 0;
      const QPointF screen = transform().map( QPointF( x, y ) );
      const int orientation = qRound( std::atan2( screen.y(), screen.x() ) * 4 / std::acos( -1.0 ) );
      const Qt::CursorShape cursors[] = { Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor };
      viewport()->setCursor( cursors[( orientation + 4 ) % 4] );
    }
    else
      viewport()->unsetCursor();
  }

  void mouseReleaseEvent( QMouseEvent *event ) override
  {
    if ( m_dragEdges && event->button() == Qt::LeftButton )
    {
      previewResize( event->pos() );
      const auto edges = m_dragEdges;
      const auto wallFraction = m_dragWallFraction;
      m_dragEdges = {};
      m_dragWallFraction.reset();
      const auto initial = resizeRect( m_dragTarget );
      if ( resizeOutline && ( wallFraction || m_dragOutline != initial ) )
        resizeOutline( m_dragOutline, edges, 0, wallFraction, m_dragTarget );
      viewport()->update();
      event->accept();
      return;
    }
    QGraphicsView::mouseReleaseEvent( event );
  }

  void wheelEvent( QWheelEvent *event ) override
  {
    if ( m_dragEdges ) { event->accept(); return; }
    if ( m_outline.isEmpty() || event->angleDelta().y() == 0 )
    {
      QGraphicsView::wheelEvent( event );
      return;
    }
    ResizeTarget target;
    const auto edges = resizeHandleAt( event->position().toPoint(), target );
    if ( edges && resizeOutline )
    {
      if ( edges != m_wheelEdges || target != m_wheelTarget ) m_wheelDelta = 0;
      m_wheelEdges = edges;
      m_wheelTarget = target;
      m_wheelDelta += event->angleDelta().y();
      const int steps = m_wheelDelta / 120;
      m_wheelDelta %= 120;
      if ( steps )
        resizeOutline( m_outline, edges, steps, std::nullopt, target );
      event->accept();
      return;
    }
    m_wheelDelta = 0;
    const double nextScale = viewScale() * std::pow( 1.2, event->angleDelta().y() / 120.0 );
    const double factor = std::clamp( nextScale, 1e-5, 1e6 ) / viewScale();
    scale( factor, factor );
    event->accept();
  }

  void drawForeground( QPainter *painter, const QRectF & ) override
  {
    if ( m_outline.isEmpty() ) return;
    auto drawPoints = [painter]( const QVector<QPointF> &points, const QColor &color ) {
      QPen pen( color, 3, Qt::SolidLine, Qt::RoundCap );
      pen.setCosmetic( true );
      painter->setPen( pen );
      painter->drawPoints( points.constData(), points.size() );
    };
    drawPoints( m_fitted, ParamModelerScene3D::pointCloudFitColor( true ) );
    drawPoints( m_missed, ParamModelerScene3D::pointCloudFitColor( false ) );
    QPen outline( QColor( 34, 75, 151 ), 1, Qt::DashLine );
    outline.setCosmetic( true );
    painter->setPen( outline );
    painter->setBrush( Qt::NoBrush );
    const bool draggingRoofTop = m_dragEdges && m_dragTarget == ResizeTarget::RoofTop;
    const auto &outlineRect = m_dragEdges && m_dragTarget == ResizeTarget::Outline ? m_dragOutline : m_outline;
    const bool notchView = isNotchView();
    if ( notchView )
    {
      const QRectF notch = m_dragEdges && m_dragTarget == ResizeTarget::Notch ? m_dragOutline : notchRect( outlineRect );
      const QPointF start( notch.left(), notch.top() ), end( notch.right(), notch.bottom() ), elbow = notch.bottomLeft();
      const QPointF outer[] = { start, outlineRect.topLeft(), outlineRect.bottomLeft(), outlineRect.bottomRight(), end };
      painter->drawPolyline( outer, 5 );
      QPen notchPen( QColor( 184, 95, 15, 210 ), 1.5 );
      notchPen.setCosmetic( true );
      painter->setPen( notchPen );
      const QPointF inner[] = { start, elbow, end };
      painter->drawPolyline( inner, 3 );
      const double half = 3.5 / viewScale();
      painter->drawRect( QRectF( elbow - QPointF( half, half ), QSizeF( 2 * half, 2 * half ) ) );
      painter->setPen( outline );
    }
    else
      painter->drawRect( outlineRect );
    const QPointF corners[] = { outlineRect.topLeft(), outlineRect.topRight(),
                                outlineRect.bottomLeft(), outlineRect.bottomRight() };
    const bool gable = m_roofType == QLatin1String( "GableRoofHouse" );
    const bool pyramid = m_roofType == QLatin1String( "PyramidalRoofHouse" );
    const bool editableRoof = hasRoofTopHandle();
    if ( m_face == 0 && gable )
    {
      const double y = outlineRect.bottom() - m_ridgeFraction * outlineRect.height();
      painter->drawLine( QPointF( outlineRect.left(), y ), QPointF( outlineRect.right(), y ) );
    }
    else if ( ( m_face == 0 && pyramid ) || editableRoof )
    {
      const QRectF platform = draggingRoofTop ? m_dragOutline : roofTopRect( outlineRect );
      if ( !isRecessed() )
      {
        painter->drawLine( corners[0], platform.topLeft() );
        painter->drawLine( corners[1], platform.topRight() );
        painter->drawLine( corners[2], platform.bottomLeft() );
        painter->drawLine( corners[3], platform.bottomRight() );
      }
      if ( editableRoof )
      {
        QPen roofPen( QColor( 184, 95, 15, 210 ), 1.5 );
        roofPen.setCosmetic( true );
        painter->setPen( roofPen );
        if ( isShortRidge() )
        {
          painter->drawLine( platform.topLeft(), platform.topRight() );
          const double radius = 3.5 / viewScale();
          painter->drawEllipse( platform.topLeft(), radius, radius );
          painter->drawEllipse( platform.topRight(), radius, radius );
        }
        else
          painter->drawRect( platform );
      }
    }
    else if ( isRecessed() && m_face != 0 )
    {
      const auto opening = sideOpening( outlineRect );
      const double floor = wallHandlePosition( outlineRect ).y();
      const QPointF section[] = { QPointF( opening.x(), outlineRect.top() ), QPointF( opening.x(), floor ),
                                  QPointF( opening.y(), floor ), QPointF( opening.y(), outlineRect.top() ) };
      QPen recessPen( QColor( 184, 95, 15, 210 ), 1, Qt::DashLine );
      recessPen.setCosmetic( true );
      painter->setPen( recessPen );
      painter->drawPolyline( section, 4 );
    }
    else if ( hasWallHandle() )
    {
      const double eave = wallHandlePosition( outlineRect ).y();
      const QPointF leftEave( outlineRect.left(), eave ), rightEave( outlineRect.right(), eave );
      painter->drawLine( leftEave, rightEave );
      outline.setStyle( Qt::SolidLine );
      painter->setPen( outline );
      if ( gable && m_face == 1 )
      {
        painter->drawRect( outlineRect );
      }
      else
      {
        const double halfTop = horizontalRoofTop() / 2;
        const double position = m_face == 2 ? m_roofTopPosition.y() : m_roofTopPosition.x();
        const double topCenter = m_roofType == QLatin1String( "FlatTopHipRoofHouse" ) || ( isShortRidge() && m_face == 1 )
                                 ? position * ( outlineRect.width() - 2 * halfTop ) + halfTop
                                 : m_ridgeFraction * outlineRect.width();
        const QPointF peak( outlineRect.left() + topCenter, outlineRect.top() );
        const QPointF profile[] = { leftEave, peak - QPointF( halfTop, 0 ), peak + QPointF( halfTop, 0 ),
                                    rightEave, outlineRect.bottomRight(),
                                    outlineRect.bottomLeft(), leftEave };
        painter->drawPolyline( profile, 7 );
      }
    }
    QColor handleColor = outline.color();
    handleColor.setAlpha( 150 );
    QPen handles( handleColor, 1 );
    handles.setCosmetic( true );
    painter->setPen( handles );
    const double halfSize = 3.5 / viewScale();
    for ( const QPointF &corner : corners )
      if ( !notchView || corner != outlineRect.topRight() )
        painter->drawRect( QRectF( corner - QPointF( halfSize, halfSize ), QSizeF( 2 * halfSize, 2 * halfSize ) ) );
    if ( hasWallHandle() )
    {
      QPen wallPen( QColor( 184, 95, 15, 210 ), 2 );
      wallPen.setCosmetic( true );
      painter->setPen( wallPen );
      painter->drawEllipse( wallHandlePosition( outlineRect ), 1.4 * halfSize, 1.4 * halfSize );
    }
  }

private:
  double viewScale() const { return std::hypot( transform().m11(), transform().m12() ); }

  bool isNotchView() const { return m_face == 0 && m_roofType == QLatin1String( "LShapedHouse" ); }

  QRectF notchRect( const QRectF &outline ) const
  {
    return QRectF( outline.right() - m_notch.x() * outline.width(), outline.top(),
                   m_notch.x() * outline.width(), ( 1 - m_notch.y() ) * outline.height() );
  }

  QRectF resizeRect( ResizeTarget target ) const
  {
    if ( target == ResizeTarget::RoofTop ) return roofTopRect( m_outline );
    if ( target == ResizeTarget::Notch ) return notchRect( m_outline );
    return m_outline;
  }

  bool isShortRidge() const { return m_roofType == QLatin1String( "ShortRidgeHipRoofHouse" ); }
  bool isRecessed() const { return m_roofType == QLatin1String( "RecessedRoofHouse" ); }

  bool hasRoofTopHandle() const
  {
    return m_face == 0 && ( isShortRidge() || isRecessed() || m_roofType == QLatin1String( "FlatTopHipRoofHouse" ) );
  }

  QRectF roofTopRect( const QRectF &outline ) const
  {
    // Project model +Y upwards in the top view.
    const double left = outline.left() + ( outline.width() - m_roofTop.width() ) * m_roofTopPosition.x();
    const double bottom = outline.bottom() - ( outline.height() - m_roofTop.height() ) * m_roofTopPosition.y();
    const QRectF rect( QPointF( left, bottom - m_roofTop.height() ), m_roofTop );
    return rect;
  }

  bool hasWallHandle() const
  {
    return !m_outline.isEmpty() && m_face != 0 &&
           ( m_roofType == QLatin1String( "GableRoofHouse" ) || m_roofType == QLatin1String( "PyramidalRoofHouse" ) ||
             m_roofType == QLatin1String( "FlatTopHipRoofHouse" ) || isShortRidge() || isRecessed() );
  }

  double horizontalRoofTop() const { return m_face == 2 ? m_roofTop.height() : m_roofTop.width(); }

  QPointF sideOpening( const QRectF &rect ) const
  {
    const double size = horizontalRoofTop();
    const double position = m_face == 2 ? m_roofTopPosition.y() : m_roofTopPosition.x();
    const double start = rect.left() + ( rect.width() - size ) * position;
    return QPointF( start, start + size );
  }

  QPointF wallHandlePosition( const QRectF &rect ) const
  {
    const double fraction = m_dragWallFraction.value_or( m_wallFraction );
    const auto opening = sideOpening( rect );
    const double x = isRecessed() ? ( opening.x() + opening.y() ) / 2 : rect.center().x();
    const double floor = isRecessed() ? m_outline.height() * ( 1 - fraction ) : rect.height() * ( 1 - fraction );
    return QPointF( x, rect.top() + floor );
  }

  bool wallHandleAt( const QPoint &position ) const
  {
    return hasWallHandle() && ( mapFromScene( wallHandlePosition( m_outline ) ) - position ).manhattanLength() <= 10;
  }

  void clearResizeHistory()
  {
    m_resizeHistory.clear();
    m_undoAction->setEnabled( false );
  }

  Qt::Edges resizeHandleAt( const QPoint &position, ResizeTarget &target ) const
  {
    target = ResizeTarget::Outline;
    if ( m_outline.isEmpty() ) return {};
    if ( wallHandleAt( position ) )
    {
      target = isRecessed() ? ResizeTarget::RecessDepth : ResizeTarget::Wall;
      return Qt::TopEdge;
    }
    const QPointF scenePosition = mapToScene( position );
    const double hit = 7 / viewScale();
    auto outer = edgesAt( scenePosition, m_outline, hit );
    if ( isNotchView() )
    {
      const QRectF notch = notchRect( m_outline );
      const auto inner = edgesAt( scenePosition, notch, hit ) & ( Qt::LeftEdge | Qt::BottomEdge );
      if ( inner )
      {
        target = ResizeTarget::Notch;
        return inner;
      }
      if ( scenePosition.x() > notch.left() + hit ) outer &= ~Qt::Edges( Qt::TopEdge );
      if ( scenePosition.y() < notch.bottom() - hit ) outer &= ~Qt::Edges( Qt::RightEdge );
    }
    if ( m_face == 0 && isShortRidge() )
    {
      const QRectF ridge = roofTopRect( m_outline );
      if ( std::abs( scenePosition.y() - ridge.top() ) <= hit &&
           scenePosition.x() >= ridge.left() - hit && scenePosition.x() <= ridge.right() + hit )
      {
        target = ResizeTarget::RoofTop;
        const double leftDistance = std::abs( scenePosition.x() - ridge.left() );
        const double rightDistance = std::abs( scenePosition.x() - ridge.right() );
        if ( std::min( leftDistance, rightDistance ) <= hit )
          return leftDistance < rightDistance ? Qt::LeftEdge : Qt::RightEdge;
        return Qt::TopEdge;
      }
    }
    else if ( hasRoofTopHandle() )
    {
      const auto inner = edgesAt( scenePosition, roofTopRect( m_outline ), hit );
      // Keep the outer corners accessible when the two frames meet.
      if ( inner && !( ( outer & ( Qt::LeftEdge | Qt::RightEdge ) ) && ( outer & ( Qt::TopEdge | Qt::BottomEdge ) ) ) )
      {
        target = ResizeTarget::RoofTop;
        return inner;
      }
    }
    return outer;
  }

  static Qt::Edges edgesAt( const QPointF &position, const QRectF &outline, double hit )
  {
    if ( !outline.adjusted( -hit, -hit, hit, hit ).contains( position ) ) return {};
    Qt::Edges edges;
    const bool left = position.x() < outline.center().x();
    const bool top = position.y() < outline.center().y();
    if ( std::abs( position.x() - ( left ? outline.left() : outline.right() ) ) <= hit ) edges |= left ? Qt::LeftEdge : Qt::RightEdge;
    if ( std::abs( position.y() - ( top ? outline.top() : outline.bottom() ) ) <= hit ) edges |= top ? Qt::TopEdge : Qt::BottomEdge;
    return edges;
  }

  void previewResize( const QPoint &position )
  {
    const QPointF delta = mapToScene( position ) - mapToScene( m_dragStart );
    m_dragOutline = m_outline;
    if ( m_dragTarget == ResizeTarget::Notch )
    {
      m_dragOutline = notchRect( m_outline );
      if ( m_dragEdges.testFlag( Qt::LeftEdge ) )
        m_dragOutline.setLeft( std::clamp( m_dragOutline.left() + delta.x(), m_outline.left() + 0.1 * m_outline.width(),
                                           m_outline.left() + 0.8 * m_outline.width() ) );
      if ( m_dragEdges.testFlag( Qt::BottomEdge ) )
        m_dragOutline.setBottom( std::clamp( m_dragOutline.bottom() + delta.y(), m_outline.top() + 0.1 * m_outline.height(),
                                             m_outline.top() + 0.8 * m_outline.height() ) );
      viewport()->update();
      return;
    }
    if ( m_dragTarget == ResizeTarget::RoofTop )
    {
      m_dragOutline = roofTopRect( m_outline );
      if ( isShortRidge() && m_dragEdges.testFlag( Qt::TopEdge ) )
      {
        m_dragOutline.moveTop( std::clamp( m_dragOutline.top() + delta.y(), m_outline.top() + 0.2 * m_outline.height(),
                                          m_outline.bottom() - 0.2 * m_outline.height() ) );
        viewport()->update();
        return;
      }
      if ( m_dragEdges.testFlag( Qt::LeftEdge ) )
        m_dragOutline.setLeft( std::max( m_outline.left(), std::min( m_dragOutline.left() + delta.x(), m_dragOutline.right() - 0.01 ) ) );
      if ( m_dragEdges.testFlag( Qt::RightEdge ) )
        m_dragOutline.setRight( std::min( m_outline.right(), std::max( m_dragOutline.right() + delta.x(), m_dragOutline.left() + 0.01 ) ) );
      if ( m_dragEdges.testFlag( Qt::TopEdge ) )
        m_dragOutline.setTop( std::max( m_outline.top(), std::min( m_dragOutline.top() + delta.y(), m_dragOutline.bottom() - 0.01 ) ) );
      if ( m_dragEdges.testFlag( Qt::BottomEdge ) )
        m_dragOutline.setBottom( std::min( m_outline.bottom(), std::max( m_dragOutline.bottom() + delta.y(), m_dragOutline.top() + 0.01 ) ) );
      viewport()->update();
      return;
    }
    if ( m_dragWallFraction )
    {
      m_dragWallFraction = std::clamp( m_wallFraction - delta.y() / m_outline.height(), m_minWallFraction, m_maxWallFraction );
      viewport()->update();
      return;
    }
    if ( m_dragEdges.testFlag( Qt::LeftEdge ) )
      m_dragOutline.setLeft( std::min( m_outline.left() + delta.x(), m_outline.right() - std::max( 0.01, horizontalRoofTop() ) ) );
    if ( m_dragEdges.testFlag( Qt::RightEdge ) )
      m_dragOutline.setRight( std::max( m_outline.right() + delta.x(), m_outline.left() + std::max( 0.01, horizontalRoofTop() ) ) );
    if ( m_dragEdges.testFlag( Qt::TopEdge ) )
      m_dragOutline.setTop( std::min( m_outline.top() + delta.y(), m_outline.bottom() - minimumOutlineHeight() ) );
    if ( m_dragEdges.testFlag( Qt::BottomEdge ) )
      m_dragOutline.setBottom( std::max( m_outline.bottom() + delta.y(), m_outline.top() + minimumOutlineHeight() ) );
    viewport()->update();
  }

  double minimumOutlineHeight() const
  {
    if ( m_face == 0 ) return std::max( 0.01, m_roofTop.height() );
    return isRecessed() ? m_outline.height() * ( 1 - m_wallFraction ) + 0.01 : 0.01;
  }

  QRectF m_outline;
  double m_viewRotation = 0.0;
  QString m_roofType;
  int m_face = 0;
  int m_side = -1;
  double m_ridgeFraction = 0.5;
  double m_wallFraction = 1.0;
  double m_minWallFraction = 1.0;
  double m_maxWallFraction = 1.0;
  QSizeF m_roofTop{ 0, 0 };
  QPointF m_roofTopPosition{ 0.5, 0.5 };
  QPointF m_notch;
  QRectF m_dragOutline;
  QPoint m_dragStart;
  Qt::Edges m_dragEdges;
  ResizeTarget m_dragTarget = ResizeTarget::Outline;
  std::optional<double> m_dragWallFraction;
  Qt::Edges m_wheelEdges;
  ResizeTarget m_wheelTarget = ResizeTarget::Outline;
  int m_wheelDelta = 0;
  QRectF m_focus;
  QVector<QPointF> m_fitted;
  QVector<QPointF> m_missed;
  QAction *m_undoAction;
  QVector<FaceFitState> m_resizeHistory;
  FaceFitState m_lastResize{};
  QVector<int> m_faceIndices;
  QVector<QVector3D> m_faceSample;
  std::unique_ptr<ParamModelerFit::Baseline> m_faceBaseline;
  double m_sampleBand = 0.0;
};

void ParamModelerDock::showFaceFitView()
{
  if ( m_faceFitView )
  {
    if ( m_faceFitSelection->currentIndex() == 0 )
      m_faceFitView->setViewRotation( currentFaceFitTopRotation( mIface, this ) );
    m_faceFitView->window()->raise();
    m_faceFitView->window()->activateWindow();
    return;
  }
  auto *dialog = new QDialog( this );
  dialog->setAttribute( Qt::WA_DeleteOnClose );
  dialog->setWindowTitle( tr( "2D face fit" ) );
  auto *layout = new QVBoxLayout( dialog );
  auto *undo = new QAction( QgsApplication::getThemeIcon( QStringLiteral( "/mActionUndo.svg" ) ),
                           tr( "Undo last resize" ), dialog );
  undo->setShortcut( QKeySequence::Undo );
  undo->setShortcutContext( Qt::WidgetWithChildrenShortcut );
  undo->setEnabled( false );
  dialog->addAction( undo );
  auto *undoButton = new QToolButton( dialog );
  undoButton->setDefaultAction( undo );
  undoButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
  auto *toolbar = new QHBoxLayout;
  toolbar->addWidget( undoButton );
  auto *pickButton = new QPushButton( tr( "Pick face in 3D" ), dialog );
  toolbar->addWidget( pickButton );
  toolbar->addStretch();
  layout->addLayout( toolbar );
  auto *controls = new QFormLayout;
  auto *selection = new QComboBox( dialog );
  selection->addItem( tr( "Top (X / Y)" ) );
  selection->addItem( tr( "Length side (X / Z)" ) );
  selection->addItem( tr( "Width side (Y / Z)" ) );
  controls->addRow( tr( "Face:" ), selection );
  auto *band = new QDoubleSpinBox( dialog );
  band->setDecimals( 3 );
  band->setRange( 0.001, 1000000 );
  band->setSingleStep( 0.1 );
  band->setValue( 0.5 );
  controls->addRow( tr( "Slice range (± scene units):" ), band );
  layout->addLayout( controls );
  auto *view = new ParamModelerFaceFitView( undo, dialog );
  layout->addWidget( view, 1 );
  auto *status = new QLabel( dialog );
  status->setWordWrap( true );
  layout->addWidget( status );
  auto *dragHint = new QLabel( tr( "Drag an edge or corner to resize, or scroll over it for small steps. Scroll over empty space to zoom." ), dialog );
  dragHint->setWordWrap( true );
  layout->addWidget( dragHint );
  auto *roofHint = new QLabel( dialog );
  roofHint->setWordWrap( true );
  layout->addWidget( roofHint );
  const auto updateRoofHint = [this, selection, roofHint] {
    const QString type = primitiveType();
    const bool top = selection->currentIndex() == 0;
    const bool shortRidge = type == QLatin1String( "ShortRidgeHipRoofHouse" );
    const bool lShaped = type == QLatin1String( "LShapedHouse" );
    const bool recessed = type == QLatin1String( "RecessedRoofHouse" );
    if ( recessed )
      roofHint->setText( top ? tr( "Drag or scroll on the orange frame to resize the recess with the opposite edge fixed." )
                            : tr( "Drag or scroll on the orange circle to adjust recess depth." ) );
    else if ( !top )
      roofHint->setText( tr( "In side view, drag or scroll on the orange circle to adjust wall height with total height fixed." ) );
    else if ( shortRidge )
      roofHint->setText( tr( "Drag or scroll at a ridge endpoint to move it and hold the other fixed, or on the ridge line to move it sideways." ) );
    else if ( lShaped )
      roofHint->setText( tr( "Drag or scroll on an orange edge to adjust the L-shaped notch." ) );
    else
      roofHint->setText( tr( "Drag or scroll on the orange frame to resize the roof platform and hold the opposite edge fixed." ) );
    roofHint->setVisible( recessed || ( top ? lShaped || shortRidge || type == QLatin1String( "FlatTopHipRoofHouse" )
                                          : faceFitWallRatio( type, ui ) != nullptr ) );
  };
  connect( selection, qOverload<int>( &QComboBox::currentIndexChanged ), roofHint, updateRoofHint );
  connect( ui->comboPrimitive, qOverload<int>( &QComboBox::currentIndexChanged ), roofHint, updateRoofHint );
  updateRoofHint();
  auto *fitButton = new QPushButton( tr( "Fit face outline" ), dialog );
  layout->addWidget( fitButton, 0, Qt::AlignRight );
  connect( fitButton, &QPushButton::clicked, view, [view] { view->fitOutline(); } );
  connect( band, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this] { scheduleFitEvaluation(); } );
  connect( selection, qOverload<int>( &QComboBox::currentIndexChanged ), view, [this, view]( int face ) {
    view->setViewRotation( face == 0 ? currentFaceFitTopRotation( mIface, this ) : 0.0 );
    updateFitEvaluation();
    view->fitOutline();
  } );
  auto *facePick = new ParamModelerFaceFitPick( dialog );
  connect( pickButton, &QPushButton::clicked, facePick, [this, dialog, facePick] {
    auto *canvas = faceFitCanvas( mIface );
    if ( !m_realtimeModelLoaded || !m_pointCloudLayer || !faceFitControls( primitiveType(), ui )[0] ||
         !canvas || !canvas->mapSettings() || !canvas->cameraController() ||
         canvas->mapSettings()->sceneMode() != Qgis::SceneMode::Local || canvas->mapSettings()->crs() != m_pointCloudLayer->crs() )
    {
      QMessageBox::information( dialog, tr( "Pick face in 3D" ),
                                tr( "Load a supported model and its point cloud in the local 3D view first." ) );
      return;
    }
    facePick->start( canvas );
    QToolTip::showText( canvas->mapToGlobal( QPoint( 12, 12 ) ),
                        tr( "Click inside a model face. Press Esc to cancel." ) );
  } );
  facePick->pick = [this, selection, view, updateRoofHint]( Qgs3DMapCanvas *canvas, const QPoint &position ) {
    const auto controls = faceFitControls( primitiveType(), ui );
    if ( !m_realtimeModelLoaded || !m_pointCloudLayer || !controls[0] || !canvas->mapSettings() || !canvas->cameraController() )
      return true;
    const auto state = faceFitState( this, ui, controls );
    const MeshData mesh = BuildMesh::build( state.primitive, this );
    const auto &t = state.translation;
    const auto origin = canvas->mapSettings()->mapToWorldCoordinates( QgsVector3D( t[0], t[1], t[2] ) );
    QMatrix4x4 localToWorld;
    localToWorld.translate( origin.x(), origin.y(), origin.z() );
    localToWorld *= modelRotationMatrix( this );
    const auto *camera = canvas->cameraController()->camera();
    const auto picked = pickFaceFitScreen( mesh, state.dimensions, camera->projectionMatrix() * camera->viewMatrix() * localToWorld,
                                          canvas->size(), position, state.primitive == QLatin1String( "LShapedHouse" ),
                                          state.primitive == QLatin1String( "RecessedRoofHouse" ) );
    if ( !picked )
    {
      QToolTip::showText( canvas->mapToGlobal( position ), tr( "No face here. Click inside a wall or roof. Esc cancels." ) );
      return false;
    }
    const QSignalBlocker block( selection );
    selection->setCurrentIndex( picked->view );
    view->selectSide( state.primitive, *picked );
    view->setViewRotation( picked->view == 0 ? currentFaceFitTopRotation( mIface, this ) : 0.0 );
    updateRoofHint();
    updateFitEvaluation();
    view->fitOutline();
    return true;
  };
  const auto applyResize = [this]( const FaceFitState &state, const FaceFitControls &controls ) {
    const QScopedValueRollback<bool> suspend( m_suspendHeightCompensation, true );
    const QScopedValueRollback<bool> anchor( m_anchorLockCompensating, true );
    for ( int i = 0; i < 3; ++i )
      controls[i]->setValue( state.dimensions[i] );
    if ( auto *ratio = faceFitWallRatio( state.primitive, ui ) )
      ratio->setValue( state.wallFraction );
    if ( state.primitive == QLatin1String( "FlatTopHipRoofHouse" ) )
    {
      ui->spinBoxTPRTopLength->setValue( state.roofTop.width() );
      ui->spinBoxTPRTopWidth->setValue( state.roofTop.height() );
      m_tpTopPosition = state.roofTopPosition;
    }
    else if ( state.primitive == QLatin1String( "ShortRidgeHipRoofHouse" ) )
    {
      ui->spinBoxAGHRidgeLength->setValue( state.roofTop.width() );
      ui->spinBoxAGHRidgeOffset->setValue( state.ridgeFraction );
      m_aghRidgePositionX = state.roofTopPosition.x();
    }
    else if ( state.primitive == QLatin1String( "LShapedHouse" ) )
    {
      ui->spinBoxLWingRatio->setValue( state.notch.x() );
      ui->spinBoxLWingWidthRatio->setValue( state.notch.y() );
    }
    else if ( state.primitive == QLatin1String( "RecessedRoofHouse" ) )
    {
      ui->spinBoxICInnerLength->setValue( state.roofTop.width() );
      ui->spinBoxICInnerWidth->setValue( state.roofTop.height() );
      ui->spinBoxICOffsetX->setValue( state.roofTopPosition.x() );
      ui->spinBoxICOffsetY->setValue( state.roofTopPosition.y() );
      ui->spinBoxICInnerHeight->setValue( state.recessDepth );
    }
    setPoseTranslate( state.translation[0], state.translation[1], state.translation[2] );
  };
  const auto refreshResize = [this] {
    retargetAnchorLockToCurrentPosition();
    onUpdatePreview();
    m_fitTimer->stop();
    updateFitEvaluation();
  };
  connect( undo, &QAction::triggered, view, [this, view, applyResize, refreshResize] {
    const auto controls = faceFitControls( primitiveType(), ui );
    if ( !m_realtimeModelLoaded || !m_pointCloudLayer || !controls[0] )
      return;
    FaceFitState previous;
    if ( !view->undoResize( faceFitState( this, ui, controls ), previous ) ) return;
    applyResize( previous, controls );
    refreshResize();
  } );
  using ResizeTarget = ParamModelerFaceFitView::ResizeTarget;
  view->resizeOutline = [this, view, selection, applyResize, refreshResize]( const QRectF &outline, Qt::Edges edges, int wheelSteps,
                                                                        std::optional<double> wallFraction, ResizeTarget target ) {
    const auto dimensions = faceFitControls( primitiveType(), ui );
    if ( !m_realtimeModelLoaded || !m_pointCloudLayer || !dimensions[0] )
      return;
    const int face = selection->currentIndex();
    const int horizontalAxis = face == 2 ? 1 : 0;
    const int verticalAxis = face == 0 ? 1 : 2;
    const auto before = faceFitState( this, ui, dimensions );
    const bool recessed = before.primitive == QLatin1String( "RecessedRoofHouse" );
    const auto resizeRoofAxis = [wheelSteps]( const QDoubleSpinBox *spin, double dragged, double footprint, double position, bool movesMin ) {
      const double size = spin->value();
      const double origin = ( footprint - size ) * position;
      const double fixedEdge = movesMin ? origin + size : origin;
      const double room = std::clamp( movesMin ? fixedEdge : footprint - fixedEdge, 0.0, footprint );
      const double scale = std::pow( 10.0, spin->decimals() );
      const double maximum = std::min( std::floor( room * scale + 1e-8 ) / scale, spin->maximum() );
      const double minimum = std::min( maximum, std::max( spin->minimum(), spin->singleStep() ) );
      const double requested = wheelSteps ? size + wheelSteps * spin->singleStep() : dragged;
      const double nextSize = std::clamp( std::round( requested * scale ) / scale, minimum, maximum );
      if ( nextSize == size ) return QPointF( size, position );
      const double nextOrigin = movesMin ? fixedEdge - nextSize : fixedEdge;
      const double clearance = footprint - nextSize;
      const double nextPosition = clearance > 0 ? std::clamp( nextOrigin / clearance, 0.0, 1.0 ) : position;
      return QPointF( nextSize, nextPosition );
    };
    auto after = before;
    if ( target == ResizeTarget::Wall )
    {
      auto *ratio = faceFitWallRatio( before.primitive, ui );
      if ( face == 0 || !ratio ) return;
      const auto *total = dimensions[2];
      const double requested = wallFraction.value_or( before.wallFraction ) + wheelSteps * total->singleStep() / total->value();
      after.wallFraction = std::clamp( requested, ratio->minimum(), ratio->maximum() );
    }
    else if ( target == ResizeTarget::RecessDepth )
    {
      if ( face == 0 || !recessed ) return;
      const auto *depth = ui->spinBoxICInnerHeight;
      const double requested = wallFraction ? ( 1 - *wallFraction ) * before.dimensions[2] :
                                             before.recessDepth + wheelSteps * depth->singleStep();
      after.recessDepth = std::clamp( requested, std::min( depth->singleStep(), depth->maximum() ), depth->maximum() );
    }
    else if ( target == ResizeTarget::Notch )
    {
      if ( face != 0 || before.primitive != QLatin1String( "LShapedHouse" ) ) return;
      const auto adjust = []( const QDoubleSpinBox *ratio, double notch, const QDoubleSpinBox *span, int steps ) {
        const double requested = steps ? ratio->value() + steps * span->singleStep() / span->value()
                                       : notch / span->value();
        return std::clamp( requested, ratio->minimum(), ratio->maximum() );
      };
      if ( edges.testFlag( Qt::LeftEdge ) )
        after.notch.setX( adjust( ui->spinBoxLWingRatio, outline.width(), dimensions[0], wheelSteps ) );
      if ( edges.testFlag( Qt::BottomEdge ) )
        after.notch.setY( adjust( ui->spinBoxLWingWidthRatio, -outline.bottom(), dimensions[1], -wheelSteps ) );
    }
    else if ( target == ResizeTarget::RoofTop && before.primitive == QLatin1String( "ShortRidgeHipRoofHouse" ) )
    {
      if ( face != 0 ) return;
      if ( edges.testFlag( Qt::TopEdge ) )
      {
        const auto *ratio = ui->spinBoxAGHRidgeOffset;
        const double requested = wheelSteps ? ratio->value() + wheelSteps * ratio->singleStep() :
                                               -outline.top() / before.dimensions[1];
        after.ridgeFraction = std::clamp( requested, ratio->minimum(), ratio->maximum() );
      }
      else
      {
        const QPointF x = resizeRoofAxis( ui->spinBoxAGHRidgeLength, outline.width(), before.dimensions[0],
                                          before.roofTopPosition.x(), edges.testFlag( Qt::LeftEdge ) );
        after.roofTop.setWidth( x.x() );
        after.roofTopPosition.setX( x.y() );
      }
    }
    else if ( target == ResizeTarget::RoofTop )
    {
      if ( face != 0 || ( !recessed && before.primitive != QLatin1String( "FlatTopHipRoofHouse" ) ) ) return;
      if ( edges & ( Qt::LeftEdge | Qt::RightEdge ) )
      {
        const QPointF x = resizeRoofAxis( recessed ? ui->spinBoxICInnerLength : ui->spinBoxTPRTopLength, outline.width(), before.dimensions[0],
                                          before.roofTopPosition.x(), edges.testFlag( Qt::LeftEdge ) );
        after.roofTop.setWidth( x.x() );
        after.roofTopPosition.setX( x.y() );
      }
      if ( edges & ( Qt::TopEdge | Qt::BottomEdge ) )
      {
        const QPointF y = resizeRoofAxis( recessed ? ui->spinBoxICInnerWidth : ui->spinBoxTPRTopWidth, outline.height(), before.dimensions[1],
                                          before.roofTopPosition.y(), edges.testFlag( Qt::BottomEdge ) );
        after.roofTop.setHeight( y.x() );
        after.roofTopPosition.setY( y.y() );
      }
    }
    if ( target != ResizeTarget::Outline )
    {
      if ( before == after ) return;
      applyResize( after, dimensions );
      const auto applied = faceFitState( this, ui, dimensions );
      if ( before == applied ) return;
      view->recordResize( before, applied );
      refreshResize();
      return;
    }
    auto *horizontal = dimensions[horizontalAxis];
    auto *vertical = dimensions[verticalAxis];
    const auto bounded = []( QDoubleSpinBox *spin, double size, double roofTop ) {
      const double minimum = std::max( { spin->minimum(), spin->singleStep(), roofTop } );
      return QString::number( std::clamp( size, minimum, spin->maximum() ), 'f', spin->decimals() ).toDouble();
    };
    const double topWidth = face == 2 ? before.roofTop.height() : before.roofTop.width();
    const double topHeight = face == 0 ? before.roofTop.height() : recessed ? before.recessDepth + vertical->singleStep() : 0;
    const double requestedWidth = wheelSteps ? horizontal->value() + wheelSteps * horizontal->singleStep() : outline.width();
    const double requestedHeight = wheelSteps ? vertical->value() + wheelSteps * vertical->singleStep() : outline.height();
    const double width = edges & ( Qt::LeftEdge | Qt::RightEdge ) ? bounded( horizontal, requestedWidth, topWidth ) : horizontal->value();
    const double height = edges & ( Qt::TopEdge | Qt::BottomEdge ) ? bounded( vertical, requestedHeight, topHeight ) : vertical->value();
    if ( width == horizontal->value() && height == vertical->value() ) return;

    QVector3D originShift;
    if ( edges.testFlag( Qt::LeftEdge ) ) originShift[horizontalAxis] = horizontal->value() - width;
    if ( edges.testFlag( Qt::BottomEdge ) ) originShift[verticalAxis] = vertical->value() - height;
    const QVector3D shift = modelRotationMatrix( this ).mapVector( originShift );
    const double tx = poseTranslateX() + shift.x();
    const double ty = poseTranslateY() + shift.y();
    const double tz = poseTranslateZ() + shift.z();
    if ( tx < ui->spinBoxTX->minimum() || tx > ui->spinBoxTX->maximum() ||
         ty < ui->spinBoxTY->minimum() || ty > ui->spinBoxTY->maximum() ||
         tz < ui->spinBoxTZ->minimum() || tz > ui->spinBoxTZ->maximum() )
    {
      QToolTip::showText( view->mapToGlobal( view->rect().center() ), tr( "Resize exceeds the model translation range." ), view );
      return;
    }
    const QPointF center = view->mapToScene( view->viewport()->rect().center() );
    after.dimensions[horizontalAxis] = width;
    after.dimensions[verticalAxis] = height;
    after.translation = { tx, ty, tz };
    applyResize( after, dimensions );
    view->recordResize( before, faceFitState( this, ui, dimensions ) );
    refreshResize();
    view->centerOn( center - QPointF( originShift[horizontalAxis], -originShift[verticalAxis] ) );
  };
  m_faceFitView = view;
  m_faceFitSelection = selection;
  m_faceFitBand = band;
  m_faceFitStatus = status;
  dialog->resize( 620, 510 );
  dialog->show();
  view->setViewRotation( currentFaceFitTopRotation( mIface, this ) );
  updateFitEvaluation();
}

void ParamModelerDock::updateFaceFitView( const QMatrix4x4 &pose, const QVector<bool> &surfaceFit )
{
  if ( !m_faceFitView ) return;
  const auto controls = faceFitControls( primitiveType(), ui );
  if ( !controls[0] || surfaceFit.isEmpty() )
  {
    m_faceFitView->setProjection( {} );
    m_faceFitStatus->setText( tr( "Load a supported model and its point cloud to fine-tune in 2D." ) );
    return;
  }
  const auto state = faceFitState( this, ui, controls );
  m_faceFitView->syncResizeHistory( state );
  const QMatrix4x4 worldToModel = pose.inverted();
  const double band = m_faceFitBand->value();
  // Keep the selected wall during edits. Re-evaluate when the user enters another view.
  const int face = m_faceFitSelection->currentIndex();
  const int horizontalAxis = face == 2 ? 1 : 0;
  const int verticalAxis = face == 0 ? 1 : 2;
  const int normalAxis = 3 - horizontalAxis - verticalAxis;
  const auto &dimensions = state.dimensions;
  const double wallHeight = dimensions[2] * state.wallFraction;
  const double verticalSpan = dimensions[verticalAxis];
  const bool lShaped = state.primitive == QLatin1String( "LShapedHouse" );
  const double notchX = dimensions[0] * ( 1 - state.notch.x() );
  const double notchY = dimensions[1] * state.notch.y();
  const bool gable = state.primitive == QLatin1String( "GableRoofHouse" );
  const bool flatHip = state.primitive == QLatin1String( "FlatTopHipRoofHouse" );
  const bool shortRidge = state.primitive == QLatin1String( "ShortRidgeHipRoofHouse" );
  const bool recessed = state.primitive == QLatin1String( "RecessedRoofHouse" ) && dimensions[2] > 0;
  const bool hip = flatHip || shortRidge || state.primitive == QLatin1String( "PyramidalRoofHouse" );
  const QSizeF &roofTop = state.roofTop;
  const double xMargin = hip ? 1 - roofTop.width() / dimensions[0] : 0;
  const double yMargin = hip ? 1 - roofTop.height() / dimensions[1] : 0;
  const QPointF topPosition = shortRidge ? QPointF( state.roofTopPosition.x(), state.ridgeFraction ) : state.roofTopPosition;
  const double recessFloor = dimensions[2] - state.recessDepth;
  const QRectF recess( QPointF( ( dimensions[0] - roofTop.width() ) * topPosition.x(),
                               ( dimensions[1] - roofTop.height() ) * topPosition.y() ), roofTop );
  const QRectF recessBand = recess.adjusted( -band, -band, band, band );
  const std::array<double, 4> insets = { xMargin * topPosition.x(), xMargin * ( 1 - topPosition.x() ),
                                         yMargin * topPosition.y(), yMargin * ( 1 - topPosition.y() ) };
  const auto slopeProgress = []( double distance, double inset ) {
    return inset > 0 ? distance / inset : std::numeric_limits<double>::infinity();
  };
  const int lockedSide = face == 0 ? 0 : m_faceFitView->selectedSide( state.primitive, face );
  const bool hasSample = m_faceFitView->hasFaceSample( state.primitive, face, band );
  if ( !hasSample ) m_faceFitView->clearFaceSample();
  const int firstSide = std::max( 0, lockedSide );
  const int lastSide = lockedSide < 0 ? 1 : lockedSide;
  const double wallBand = std::min( band, dimensions[normalAxis] * 0.25 );
  const double alongMargin = std::min( band, dimensions[horizontalAxis] * 0.15 );
  const double wallMargin = std::min( band, wallHeight * 0.15 );
  struct SidePoints
  {
    std::array<int, 3> support{}; // Wall interior, visible roof, then all displayed points.
    QVector<int> indices;
  };
  SidePoints sides[2];
  const int pointsToSlice = hasSample ? 0 : m_displayFitPoints.size();
  for ( int i = 0; i < pointsToSlice; ++i )
  {
    const QVector3D local = worldToModel.map( m_displayFitPoints[i] );
    const double x = local[horizontalAxis];
    const double y = local[verticalAxis];
    const double normal = local[normalAxis];
    if ( !std::isfinite( x ) || !std::isfinite( y ) || !std::isfinite( normal ) ) continue;
    double roofZ = dimensions[2];
    double widthFraction = 0;
    double hipProgress = 0;
    std::array<double, 4> hipSlopes{};
    if ( gable || hip )
    {
      widthFraction = std::clamp( local.y() / dimensions[1], 0.0, 1.0 );
      if ( hip )
      {
        const double lengthFraction = std::clamp( local.x() / dimensions[0], 0.0, 1.0 );
        hipSlopes = { slopeProgress( lengthFraction, insets[0] ), slopeProgress( 1 - lengthFraction, insets[1] ),
                      slopeProgress( widthFraction, insets[2] ), slopeProgress( 1 - widthFraction, insets[3] ) };
        hipProgress = *std::min_element( hipSlopes.begin(), hipSlopes.end() );
      }
      const double roofFraction = gable
        ? std::min( widthFraction / state.ridgeFraction, ( 1 - widthFraction ) / ( 1 - state.ridgeFraction ) )
        : std::min( 1.0, hipProgress );
      roofZ = wallHeight + ( dimensions[2] - wallHeight ) * roofFraction;
    }
    const bool nearRoof = std::abs( local.z() - roofZ ) <= band;
    const bool nearRecess = recessed && recessBand.contains( local.x(), local.y() ) &&
                            local.z() >= recessFloor - band && local.z() <= dimensions[2] + band;
    for ( int side = firstSide; side <= lastSide; ++side )
    {
      bool inSlice = nearRoof || nearRecess;
      bool roofSupport = false;
      bool wallSupport = false;
      if ( face != 0 )
      {
        double wall = dimensions[normalAxis];
        if ( lShaped && x > ( face == 1 ? notchX : notchY ) )
          wall = face == 1 ? notchY : notchX;
        const double wallDistance = side == 0 ? normal : wall - normal;
        const bool nearWall = std::abs( wallDistance ) <= band;
        wallSupport = std::abs( wallDistance ) <= wallBand && x >= alongMargin &&
                      x <= dimensions[horizontalAxis] - alongMargin && y >= wallMargin && y <= wallHeight - wallMargin;
        inSlice = nearWall || nearRecess;
        if ( gable && face == 2 )
        {
          // The gable end includes the triangle above the eave.
          inSlice = nearWall && y <= roofZ + band;
          roofSupport = inSlice && y > wallHeight + wallMargin;
        }
        else if ( gable || hip )
        {
          const double normalFraction = side == 0 ? widthFraction : 1 - widthFraction;
          const bool facingRoof = gable
            ? normalFraction <= ( side == 0 ? state.ridgeFraction : 1 - state.ridgeFraction )
            : hipProgress <= 1.0 && hipSlopes[2 * normalAxis + side] == hipProgress;
          const bool verticalRoof = ( flatHip || shortRidge ) && insets[2 * normalAxis + side] == 0;
          const bool visibleRoof = ( facingRoof && nearRoof && wallDistance >= -band ) ||
                                   ( verticalRoof && nearWall && y <= roofZ + band );
          inSlice = ( nearWall && y <= wallHeight + band ) || visibleRoof;
          roofSupport = visibleRoof && y > wallHeight + wallMargin;
        }
      }
      if ( !inSlice ) continue;
      auto &points = sides[side];
      points.support[0] += wallSupport;
      points.support[1] += roofSupport;
      ++points.support[2];
      points.indices.append( i );
    }
  }
  const int chosenSide = lockedSide >= 0 ? lockedSide : ( sides[1].support > sides[0].support ? 1 : 0 );
  auto &view = *m_faceFitView;
  QString localMetrics;
  if ( hasSample || !sides[chosenSide].indices.isEmpty() )
  {
    const MeshData mesh = BuildMesh::build( state.primitive, this );
    if ( !hasSample )
      view.setFaceSample( std::move( sides[chosenSide].indices ), m_displayFitPoints, band, mesh, pose );
    const auto &initial = view.initialFaceMetrics( m_fitTolerance );
    const auto current = ParamModelerFit::evaluate( mesh, view.faceSample(), pose, m_fitTolerance );
    localMetrics = tr( "Local fit (initial → current, %1 fixed points)\nRMSE %2 → %3 | Within tolerance %4% → %5%" )
                     .arg( current.count ).arg( initial.rmse, 0, 'f', 3 ).arg( current.rmse, 0, 'f', 3 )
                     .arg( initial.percentage( initial.surfaceCount ), 0, 'f', 1 )
                     .arg( current.percentage( current.surfaceCount ), 0, 'f', 1 );
  }
  QVector<QPointF> fitted, missed;
  for ( int i : view.faceIndices() )
  {
    const QVector3D local = worldToModel.map( m_displayFitPoints[i] );
    ( surfaceFit[i] ? fitted : missed ).append( QPointF( local[horizontalAxis], -local[verticalAxis] ) );
  }
  QString status = tr( "Green within 3D fit tolerance, red outside. Face slice: %1 points." ).arg( view.faceIndices().size() );
  if ( face != 0 )
  {
    const QString direction = ( chosenSide == 0 ? QStringLiteral( "−" ) : QStringLiteral( "+" ) ) +
                              ( normalAxis == 0 ? QStringLiteral( "X" ) : QStringLiteral( "Y" ) );
    status += QLatin1Char( ' ' ) + tr( "Viewing side: %1." ).arg( direction );
  }
  if ( !localMetrics.isEmpty() ) status += QLatin1Char( '\n' ) + localMetrics;
  m_faceFitStatus->setText( status );
  const auto *ratio = faceFitWallRatio( state.primitive, ui );
  const double floorLimit = recessed ? std::min( 0.5, ui->spinBoxICInnerHeight->singleStep() / dimensions[2] ) : 1.0;
  view.setProjection( QRectF( 0, -verticalSpan, dimensions[horizontalAxis], verticalSpan ),
                      std::move( fitted ), std::move( missed ), state.primitive, face, state.ridgeFraction,
                      recessed ? recessFloor / dimensions[2] : state.wallFraction,
                      face != 0 && ( lockedSide >= 0 || !view.faceIndices().isEmpty() ) ? chosenSide : -1,
                      recessed ? floorLimit : ratio ? ratio->minimum() : 1.0,
                      recessed ? 1 - floorLimit : ratio ? ratio->maximum() : 1.0, roofTop, topPosition, state.notch );
}

void ParamModelerDock::showFitEvaluationDetails()
{
  if ( m_fitDetailsDialog )
  {
    m_fitDetailsDialog->show();
    m_fitDetailsDialog->raise();
    m_fitDetailsDialog->activateWindow();
    return;
  }
  auto *dialog = new QDialog( this );
  dialog->setAttribute( Qt::WA_DeleteOnClose );
  dialog->setWindowTitle( tr( "Fit statistics" ) );
  auto *layout = new QFormLayout( dialog );
  auto *tolerance = new QDoubleSpinBox( dialog );
  tolerance->setDecimals( 6 );
  tolerance->setRange( 0.000001, 1000000.0 );
  tolerance->setSingleStep( 0.01 );
  tolerance->setValue( m_fitTolerance );
  layout->addRow( tr( "Distance tolerance (scene units):" ), tolerance );
  auto *explanation = new QLabel( tr( "Initial values use the model before fine tuning and remain fixed while editing. Changing tolerance recalculates both initial and current values with the same point sample. Loading a point cloud or a model, changing primitive type, or applying a new prediction starts a new comparison. Surface fit: points within the distance tolerance. Outside/inside describe the current model. These percentages are not parameter accuracy." ), dialog );
  explanation->setWordWrap( true );
  explanation->setMinimumWidth( 360 );
  layout->addRow( explanation );
  auto *details = new QLabel( tr( "Load a model and point cloud to evaluate." ), dialog );
  details->setTextFormat( Qt::PlainText );
  details->setWordWrap( true );
  layout->addRow( details );
  auto *buttons = new QDialogButtonBox( QDialogButtonBox::Close, dialog );
  layout->addRow( buttons );
  connect( buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close );
  connect( tolerance, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]( double value ) {
    m_fitTolerance = value;
    scheduleFitEvaluation();
  } );
  m_fitDetailsDialog = dialog;
  m_fitDetailsLabel = details;
  updateFitEvaluation();
  dialog->show(); // Non-modal: parameters can still be adjusted in the main dock.
}

void ParamModelerDock::scheduleFitEvaluation()
{
  if ( !m_fitTimer )
    return;
  if ( !m_realtimeModelLoaded || !m_pointCloudLayer || m_fitCloudPoints.isEmpty() )
  {
    m_fitTimer->stop();
    updateFaceFitView( {}, {} );
    ParamModelerScene3D::setFitMetricText( mIface, QString() );
    ParamModelerScene3D::setPointCloudFitColors( m_pointCloudLayer );
    return;
  }
  if ( !m_fitBaseline )
  {
    const MeshData mesh = BuildMesh::build( this->primitiveType(), this );
    if ( !mesh.isEmpty() )
    {
      QMatrix4x4 pose;
      pose.translate( poseTranslateX(), poseTranslateY(), poseTranslateZ() );
      pose.rotate( poseRotateX(), 1, 0, 0 );
      pose.rotate( poseRotateY(), 0, 1, 0 );
      pose.rotate( poseRotateZ(), 0, 0, 1 );
      m_fitBaseline = QSharedPointer<ParamModelerFit::Baseline>::create( mesh, m_fitCloudPoints, pose );
    }
  }
  if ( !m_fitTimer->isActive() )
    m_fitTimer->start();
}

void ParamModelerDock::updateFitEvaluation()
{
  if ( !m_realtimeModelLoaded || !m_pointCloudLayer || m_fitCloudPoints.isEmpty() )
  {
    if ( m_fitDetailsLabel ) m_fitDetailsLabel->setText( tr( "Load a model and point cloud to evaluate." ) );
    scheduleFitEvaluation();
    return;
  }
  const MeshData mesh = BuildMesh::build( this->primitiveType(), this );
  QMatrix4x4 pose;
  pose.translate( poseTranslateX(), poseTranslateY(), poseTranslateZ() );
  pose.rotate( poseRotateX(), 1, 0, 0 );
  pose.rotate( poseRotateY(), 0, 1, 0 );
  pose.rotate( poseRotateZ(), 0, 0, 1 );
  QVector<bool> surfaceFit;
  const auto metrics = ParamModelerFit::evaluate( mesh, m_fitCloudPoints, pose, m_fitTolerance,
                                                m_displayFitPoints, &surfaceFit );
  updateFaceFitView( pose, surfaceFit );
  if ( !metrics.count )
  {
    ParamModelerScene3D::setFitMetricText( mIface, QString() );
    ParamModelerScene3D::setPointCloudFitColors( m_pointCloudLayer );
    if ( m_fitDetailsLabel ) m_fitDetailsLabel->setText( tr( "Load a model and point cloud to evaluate." ) );
    return;
  }
  ParamModelerScene3D::setPointCloudFitColors( m_pointCloudLayer, surfaceFit );
  if ( !m_fitBaseline )
    m_fitBaseline = QSharedPointer<ParamModelerFit::Baseline>::create( mesh, m_fitCloudPoints, pose );
  const auto &initial = m_fitBaseline->metrics( m_fitTolerance );
  const QString sides = metrics.solidAvailable
    ? tr( "Current outside: %1% | inside: %2%" ).arg( metrics.percentage( metrics.outsideCount ), 0, 'f', 1 )
        .arg( metrics.percentage( metrics.insideCount ), 0, 'f', 1 )
    : tr( "Inside/outside unavailable: open mesh" );
  QString text = initial.count
    ? tr( "Initial → Current (scene units)\nRMSE %1 → %2\nSurface fit: %3% → %4%\n%5\nTolerance: %6 | N: %7" )
        .arg( initial.rmse, 0, 'f', 3 ).arg( metrics.rmse, 0, 'f', 3 )
        .arg( initial.percentage( initial.surfaceCount ), 0, 'f', 1 )
        .arg( metrics.percentage( metrics.surfaceCount ), 0, 'f', 1 )
        .arg( sides ).arg( m_fitTolerance, 0, 'g', 6 ).arg( metrics.count )
    : tr( "Fit RMSE: %1\nSurface fit: %2%\n%3\nTolerance: %4 | N: %5\nScene units" )
        .arg( metrics.rmse, 0, 'f', 3 ).arg( metrics.percentage( metrics.surfaceCount ), 0, 'f', 1 )
        .arg( sides ).arg( m_fitTolerance, 0, 'g', 6 ).arg( metrics.count );
  text += QLatin1Char( '\n' ) + tr( "Point colors: green within tolerance, red outside tolerance" );
  ParamModelerScene3D::setFitMetricText( mIface, text );
  if ( m_fitDetailsLabel )
    m_fitDetailsLabel->setText( text + tr( "\nCurrent P95: %1\nCurrent surface fit points: %2\nCurrent outside points: %3\nCurrent inside points: %4" )
      .arg( metrics.p95, 0, 'f', 3 ).arg( metrics.surfaceCount )
      .arg( metrics.solidAvailable ? QString::number( metrics.outsideCount ) : tr( "N/A" ) )
      .arg( metrics.solidAvailable ? QString::number( metrics.insideCount ) : tr( "N/A" ) ) );
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


void ParamModelerDock::enableAnchorLock( AnchorPointMode mode, const QVector3D &worldTarget )
{
  if ( mode == AnchorPointMode::None )
  {
    clearAnchorLock();
    return;
  }
  m_anchorLockMode = mode;
  m_anchorLockWorldTarget = worldTarget;
  auto modeName = []( AnchorPointMode mode ) {
    switch ( mode )
    {
      case AnchorPointMode::GrowthOrigin: return QStringLiteral( "growth-origin" );
      case AnchorPointMode::UpperOrigin: return QStringLiteral( "upper-origin" );
      case AnchorPointMode::TopMinMin: return QStringLiteral( "top-min-x-min-y" );
      case AnchorPointMode::TopMaxMin: return QStringLiteral( "top-max-x-min-y" );
      case AnchorPointMode::TopMinMax: return QStringLiteral( "top-min-x-max-y" );
      case AnchorPointMode::TopMaxMax: return QStringLiteral( "top-max-x-max-y" );
      case AnchorPointMode::TopCenter: return QStringLiteral( "top-center" );
      case AnchorPointMode::None: break;
    }
    return QStringLiteral( "none" );
  };
  DEBUG_LOG( QString( "[AnchorLock] enabled mode=%1 target=(%2,%3,%4)\n" )
               .arg( modeName( mode ) )
               .arg( worldTarget.x(), 0, 'f', 3 )
               .arg( worldTarget.y(), 0, 'f', 3 )
               .arg( worldTarget.z(), 0, 'f', 3 )
               .toStdWString().c_str() );
}

void ParamModelerDock::clearAnchorLock()
{
  if ( m_anchorLockMode != AnchorPointMode::None )
    DEBUG_LOG( L"[AnchorLock] cleared\n" );
  m_anchorLockMode = AnchorPointMode::None;
  m_anchorLockWorldTarget = QVector3D();
}

bool ParamModelerDock::anchorLocalPoint( AnchorPointMode mode, const MeshData &mesh, QVector3D &localPoint ) const
{
  if ( mode == AnchorPointMode::GrowthOrigin )
  {
    localPoint = QVector3D( 0.0f, 0.0f, 0.0f );
    return true;
  }
  if ( mode == AnchorPointMode::UpperOrigin )
    return ParamModelerPicking::upperAnchor( mesh.vertices, localPoint );
  if ( mesh.vertices.isEmpty() )
    return false;

  QVector3D minimum = mesh.vertices.first();
  QVector3D maximum = minimum;
  for ( const QVector3D &v : mesh.vertices )
  {
    minimum.setX( std::min( minimum.x(), v.x() ) );
    minimum.setY( std::min( minimum.y(), v.y() ) );
    minimum.setZ( std::min( minimum.z(), v.z() ) );
    maximum.setX( std::max( maximum.x(), v.x() ) );
    maximum.setY( std::max( maximum.y(), v.y() ) );
    maximum.setZ( std::max( maximum.z(), v.z() ) );
  }

  switch ( mode )
  {
    case AnchorPointMode::TopMinMin:
      localPoint = QVector3D( minimum.x(), minimum.y(), maximum.z() );
      return true;
    case AnchorPointMode::TopMaxMin:
      localPoint = QVector3D( maximum.x(), minimum.y(), maximum.z() );
      return true;
    case AnchorPointMode::TopMinMax:
      localPoint = QVector3D( minimum.x(), maximum.y(), maximum.z() );
      return true;
    case AnchorPointMode::TopMaxMax:
      localPoint = QVector3D( maximum.x(), maximum.y(), maximum.z() );
      return true;
    case AnchorPointMode::TopCenter:
      localPoint = QVector3D( ( minimum.x() + maximum.x() ) * 0.5f,
                              ( minimum.y() + maximum.y() ) * 0.5f,
                              maximum.z() );
      return true;
    default:
      break;
  }
  return false;
}

bool ParamModelerDock::anchorLockLocalPoint( const MeshData &mesh, QVector3D &localPoint ) const
{
  return anchorLocalPoint( m_anchorLockMode, mesh, localPoint );
}

void ParamModelerDock::retargetAnchorLockToCurrentPosition()
{
  if ( m_anchorLockMode == AnchorPointMode::None || m_anchorLockCompensating )
    return;

  const MeshData mesh = BuildMesh::build( this->primitiveType(), this );
  QVector3D localPoint;
  if ( !anchorLockLocalPoint( mesh, localPoint ) )
    return;

  const QVector3D rotated = modelRotationMatrix( this ).map( localPoint );
  m_anchorLockWorldTarget = QVector3D(
    static_cast<float>( poseTranslateX() ) + rotated.x(),
    static_cast<float>( poseTranslateY() ) + rotated.y(),
    static_cast<float>( poseTranslateZ() ) + rotated.z() );
  DEBUG_LOG( QString( "[AnchorLock] retargeted target=(%1,%2,%3)\n" )
               .arg( m_anchorLockWorldTarget.x(), 0, 'f', 3 )
               .arg( m_anchorLockWorldTarget.y(), 0, 'f', 3 )
               .arg( m_anchorLockWorldTarget.z(), 0, 'f', 3 )
               .toStdWString().c_str() );
}

void ParamModelerDock::applyAnchorLockCompensation( const MeshData &mesh )
{
  if ( m_anchorLockMode == AnchorPointMode::None || m_anchorLockCompensating )
    return;

  QVector3D localPoint;
  if ( !anchorLockLocalPoint( mesh, localPoint ) )
    return;

  const QVector3D rotated = modelRotationMatrix( this ).map( localPoint );
  const QVector3D currentWorld(
    static_cast<float>( poseTranslateX() ) + rotated.x(),
    static_cast<float>( poseTranslateY() ) + rotated.y(),
    static_cast<float>( poseTranslateZ() ) + rotated.z() );
  const QVector3D delta = m_anchorLockWorldTarget - currentWorld;
  if ( delta.lengthSquared() < 1e-10f )
    return;

  const double tx = poseTranslateX() + static_cast<double>( delta.x() );
  const double ty = poseTranslateY() + static_cast<double>( delta.y() );
  const double tz = poseTranslateZ() + static_cast<double>( delta.z() );
  if ( tx < ui->spinBoxTX->minimum() || tx > ui->spinBoxTX->maximum() ||
       ty < ui->spinBoxTY->minimum() || ty > ui->spinBoxTY->maximum() ||
       tz < ui->spinBoxTZ->minimum() || tz > ui->spinBoxTZ->maximum() )
  {
    DEBUG_LOG( QString( "[AnchorLock] skipped: compensation out of range delta=(%1,%2,%3)\n" )
                 .arg( delta.x(), 0, 'f', 3 )
                 .arg( delta.y(), 0, 'f', 3 )
                 .arg( delta.z(), 0, 'f', 3 )
                 .toStdWString().c_str() );
    return;
  }

  m_anchorLockCompensating = true;
  setPoseTranslate( tx, ty, tz );
  m_anchorLockCompensating = false;
  DEBUG_LOG( QString( "[AnchorLock] compensated delta=(%1,%2,%3) tx=%4 ty=%5 tz=%6\n" )
               .arg( delta.x(), 0, 'f', 3 )
               .arg( delta.y(), 0, 'f', 3 )
               .arg( delta.z(), 0, 'f', 3 )
               .arg( tx, 0, 'f', 3 )
               .arg( ty, 0, 'f', 3 )
               .arg( tz, 0, 'f', 3 )
               .toStdWString().c_str() );
}
