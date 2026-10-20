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
#include <qgsmapcanvas.h>
#include <qgsmaptoolemitpoint.h>
#include <qgsrubberband.h>
#include <qgsvectorlayer.h>
#include <qgs3dmapcanvas.h>
#include <qgs3dmapsettings.h>

#include <QColor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMatrix4x4>
#include <QMessageBox>
#include <QTemporaryFile>
#include <QTextStream>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <limits>
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

static QVector3D modelGrowthAnchor( ParamModelerDock *dock )
{
    // BuildMesh keeps the semantic growth/rotation anchor at local origin:
    // corner-anchored classes use the footprint corner; centered classes are
    // shifted so the base center is at (0,0,0).
    return modelRotationMatrix( dock ).map( QVector3D( 0.0f, 0.0f, 0.0f ) );
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

    if ( primitiveType == QLatin1String( "IndentedCuboid" ) )
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
  const QString primitiveType = ui->comboPrimitive->currentText();
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
  if ( m_manualTranslateHasSource )
  {
    // Finish the 2D interaction at its own source, with zero translation.
    handleManualTranslateClick( m_manualTranslateSource, Qt::LeftButton );
  }
  MeshData sourceMesh = BuildMesh::build( ui->comboPrimitive->currentText(), this );
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
    const QString type = ui->comboPrimitive->currentText();
    if ( type != QStringLiteral( "Cuboid" ) &&
         type != QStringLiteral( "TruncatedPyramidRoof" ) &&
         type != QStringLiteral( "LHouse" ) &&
         type != QStringLiteral( "IndentedCuboid" ) )
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

void ParamModelerDock::startManualTranslateByClick()
{
  stopManualTranslate3D();
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
    m_manualTranslateBtn->setText( tr( "Pick 2D target..." ) );

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
  enableAnchorLock( AnchorPointMode::GrowthOrigin,
                    QVector3D( static_cast<float>( point.x() ),
                               static_cast<float>( point.y() ),
                               static_cast<float>( poseTranslateZ() ) ) );

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
    m_manualTranslateBtn->setText( tr( "Align in 2D" ) );
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
    const bool tower = ui->comboPrimitive->currentText() == QLatin1String( "FourStageRoundTower" );
    bool useBottomSlice = !tower && bottomSlice.size() >= minSlicePoints;
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
    mIface, displayPath, layerName, this, &errorMessage
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
  if ( m_fitTimer )
    m_fitTimer->stop();
  ParamModelerScene3D::setFitMetricText( mIface, QString() );
}

void ParamModelerDock::scheduleFitEvaluation()
{
  if ( !m_fitTimer )
    return;
  if ( !m_realtimeModelLoaded || !m_pointCloudLayer || m_fitCloudPoints.isEmpty() )
  {
    m_fitTimer->stop();
    ParamModelerScene3D::setFitMetricText( mIface, QString() );
    return;
  }
  if ( !m_fitTimer->isActive() )
    m_fitTimer->start();
}

void ParamModelerDock::updateFitEvaluation()
{
  if ( !m_realtimeModelLoaded || !m_pointCloudLayer || m_fitCloudPoints.isEmpty() )
  {
    scheduleFitEvaluation();
    return;
  }
  const MeshData mesh = BuildMesh::build( ui->comboPrimitive->currentText(), this );
  QMatrix4x4 pose;
  pose.translate( poseTranslateX(), poseTranslateY(), poseTranslateZ() );
  pose.rotate( poseRotateX(), 1, 0, 0 );
  pose.rotate( poseRotateY(), 0, 1, 0 );
  pose.rotate( poseRotateZ(), 0, 0, 1 );
  const auto metrics = ParamModelerFit::evaluate( mesh, m_fitCloudPoints, pose );
  ParamModelerScene3D::setFitMetricText( mIface, metrics.count
    ? tr( "Fit RMSE: %1\nP95: %2\nScene units" ).arg( metrics.rmse, 0, 'f', 3 ).arg( metrics.p95, 0, 'f', 3 )
    : QString() );
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

  const MeshData mesh = BuildMesh::build( ui->comboPrimitive->currentText(), this );
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
