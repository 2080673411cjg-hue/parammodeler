#include "parammodeler_scene3d.h"
#include "parammodeler_pcdloader.h"

#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QMatrix4x4>
#include <QMessageBox>
#include <QObject>
#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QMap>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#include <qgis.h>
#include <qgisinterface.h>
#include <QgsProject.h>
#include <qgscoordinatereferencesystem.h>
#include <QgsFeature.h>
#include <QgsGeometry.h>
#include <qgsmaplayer.h>
#include <QgsLineString.h>
#include <QgsPoint.h>
#include <QgsPolygon.h>
#include <qgsmultipolygon.h>
#include <QgsVectorLayer.h>
#include <qgsvectordataprovider.h>
#include <qgsline3dsymbol.h>
#include <QgsPolygon3DSymbol.h>
#include <QgsPoint3DSymbol.h>
#include <QgsVectorLayer3DRenderer.h>
#include <Qgs3DMapCanvas.h>
#include <qgs3dmapscene.h>
#include <qgs3dmapsettings.h>
#include <qgscameracontroller.h>
#include <qgs3dtypes.h>
#include <qgsphongmaterialsettings.h>
#include <qgsvectorfilewriter.h>
#include <qgsmapcanvas.h>
#include <qgsrectangle.h>

#include <Qt3DCore/QEntity>
#include <Qt3DCore/QTransform>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DRender/QAttribute>
#include <Qt3DRender/QBlendEquation>
#include <Qt3DRender/QBlendEquationArguments>
#include <Qt3DRender/QBuffer>
#include <Qt3DRender/QDepthTest>
#include <Qt3DRender/QNoDepthMask>
#include <Qt3DRender/QCullFace>
#include <Qt3DRender/QLineWidth>
#include <Qt3DRender/QEffect>
#include <Qt3DRender/QGeometry>
#include <Qt3DRender/QGeometryRenderer>
#include <Qt3DRender/QRenderPass>
#include <Qt3DRender/QTechnique>

namespace
{
struct MeshEdgeRecord
{
  int count = 0;
  QVector3D p0;
  QVector3D p1;
  QVector3D firstNormal;
  bool crease = false;
};

struct RealtimePreviewPart
{
  QPointer<Qt3DCore::QEntity> entity;
  QPointer<Qt3DRender::QGeometryRenderer> renderer;
  QPointer<Qt3DRender::QBuffer> buffer;
  QPointer<Qt3DRender::QAttribute> positionAttribute;
  QPointer<Qt3DRender::QAttribute> normalAttribute;
  QPointer<Qt3DExtras::QPhongMaterial> material;

  QPointer<Qt3DRender::QDepthTest> depthTest;
  QPointer<Qt3DRender::QNoDepthMask> noDepthMask;

  bool blendingSetup = false;
  bool depthStateSetup = false;
  bool cullingSetup = false;
};

struct RealtimePreviewState
{
  QPointer<Qt3DCore::QEntity> root;
  RealtimePreviewPart body;
  RealtimePreviewPart roof;
  RealtimePreviewPart wireframe;   // 线框模式：只显示边线
  QPointer<Qgs3DMapSettings> mapSettings;  // for origin tracking
};

QHash<Qgs3DMapScene *, RealtimePreviewState> sRealtimePreviewMeshes;
bool sWireframeMode = false;
const QString REALTIME_ANCHOR_LAYER_NAME = QStringLiteral( "ParamModeler_3D_Anchor" );

QString meshPointKey( const QVector3D &p )
{
  constexpr double scale = 1000000.0;
  return QStringLiteral( "%1,%2,%3" )
    .arg( qRound64( p.x() * scale ) )
    .arg( qRound64( p.y() * scale ) )
    .arg( qRound64( p.z() * scale ) );
}

QString meshEdgeKey( const QVector3D &a, const QVector3D &b )
{
  const QString ka = meshPointKey( a );
  const QString kb = meshPointKey( b );
  return ka < kb ? ka + QStringLiteral( "|" ) + kb : kb + QStringLiteral( "|" ) + ka;
}

void removeTempGpkgPaths( const QString &paths )
{
  for ( const QString &path : paths.split( '\n' ) )
  {
    const QString trimmed = path.trimmed();
    if ( !trimmed.isEmpty() )
      QFile::remove( trimmed );
  }
}

QgsPolygon *makeTrianglePolygon( const QVector3D &v0, const QVector3D &v1, const QVector3D &v2 )
{
  QgsPolygon *poly = new QgsPolygon();
  QgsLineString *ring = new QgsLineString();
  ring->setPoints( QgsPointSequence()
                   << QgsPoint( v0.x(), v0.y(), v0.z() )
                   << QgsPoint( v1.x(), v1.y(), v1.z() )
                   << QgsPoint( v2.x(), v2.y(), v2.z() )
                   << QgsPoint( v0.x(), v0.y(), v0.z() ) );
  poly->setExteriorRing( ring );
  return poly;
}

QgsVectorLayer *writeMeshLayer( QgsMultiPolygon *multiPoly,
                                const QString &layerName,
                                const QString &gpkgPath,
                                QString *errorMessage )
{
  QgsFeature feat;
  feat.setGeometry( QgsGeometry( multiPoly ) );
  QgsFeatureList features;
  features.append( feat );

  QgsVectorLayer *tmpLayer = new QgsVectorLayer( "MultiPolygonZ?crs=EPSG:3857", "tmp", "memory" );
  tmpLayer->dataProvider()->addFeatures( features );
  QgsVectorFileWriter::writeAsVectorFormat( tmpLayer, gpkgPath, "UTF-8",
                                            QgsCoordinateReferenceSystem( "EPSG:3857" ), "GPKG" );
  delete tmpLayer;

  QgsVectorLayer *layer = new QgsVectorLayer( gpkgPath, layerName, "ogr" );
  if ( !layer || !layer->isValid() )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "Failed to load model layer from GeoPackage: %1" ).arg( layerName );
    delete layer;
    QFile::remove( gpkgPath );
    return nullptr;
  }
  return layer;
}

QgsVectorLayer *makeMeshEdgeLayer( const MeshData &mesh,
                                   const QMatrix4x4 &mat,
                                   const QString &layerName )
{
  QMap<QString, MeshEdgeRecord> edgeMap;
  const int triCount = mesh.indices.size() / 3;
  for ( int i = 0; i < triCount; ++i )
  {
    const int idx0 = mesh.indices[i * 3];
    const int idx1 = mesh.indices[i * 3 + 1];
    const int idx2 = mesh.indices[i * 3 + 2];
    const QVector3D local0 = mesh.vertices[idx0];
    const QVector3D local1 = mesh.vertices[idx1];
    const QVector3D local2 = mesh.vertices[idx2];
    const QVector3D normal = QVector3D::crossProduct( local1 - local0, local2 - local0 ).normalized();

    const QVector3D points[3] = { local0, local1, local2 };
    const int edges[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };
    for ( const auto &edge : edges )
    {
      const QVector3D p0 = points[edge[0]];
      const QVector3D p1 = points[edge[1]];
      const QString key = meshEdgeKey( p0, p1 );
      MeshEdgeRecord record = edgeMap.value( key );
      if ( record.count == 0 )
      {
        record.p0 = p0;
        record.p1 = p1;
        record.firstNormal = normal;
      }
      else if ( std::abs( QVector3D::dotProduct( record.firstNormal, normal ) ) < 0.985f )
      {
        record.crease = true;
      }
      record.count++;
      edgeMap.insert( key, record );
    }
  }

  QgsVectorLayer *edgeLayer = new QgsVectorLayer( "LineStringZ?crs=EPSG:3857", layerName, "memory" );
  QgsFeatureList features;
  features.reserve( edgeMap.size() );
  for ( const MeshEdgeRecord &record : edgeMap )
  {
    if ( record.count > 1 && !record.crease )
      continue;

    const QVector3D p0 = mat.map( record.p0 );
    const QVector3D p1 = mat.map( record.p1 );
    QgsFeature feat;
    feat.setGeometry( QgsGeometry( new QgsLineString(
      QgsPoint( p0.x(), p0.y(), p0.z() ),
      QgsPoint( p1.x(), p1.y(), p1.z() )
    ) ) );
    features.append( feat );
  }

  if ( !features.isEmpty() )
    edgeLayer->dataProvider()->addFeatures( features );
  edgeLayer->updateExtents();
  return edgeLayer;
}

void applyPolygon3DMaterial( QgsVectorLayer *layer,
                             const QColor &diffuse,
                             const QColor &ambient,
                             const QColor &specular,
                             double opacity )
{
  if ( !layer )
    return;

  QgsPolygon3DSymbol *symbol3D = new QgsPolygon3DSymbol();
  symbol3D->setAltitudeClamping( Qgis::AltitudeClamping::Absolute );
  symbol3D->setAltitudeBinding( Qgis::AltitudeBinding::Vertex );
  symbol3D->setCullingMode( Qgs3DTypes::NoCulling );

  QgsPhongMaterialSettings material;
  material.setAmbient( ambient );
  material.setDiffuse( diffuse );
  material.setSpecular( specular );
  material.setShininess( 1.0 );
  symbol3D->setMaterialSettings( material.clone() );

  QgsVectorLayer3DRenderer *renderer3D = new QgsVectorLayer3DRenderer();
  renderer3D->setSymbol( symbol3D );
  layer->setRenderer3D( renderer3D );
  layer->setOpacity( opacity );
}

void applyLine3DMaterial( QgsVectorLayer *layer )
{
  if ( !layer )
    return;

  QgsLine3DSymbol *symbol3D = new QgsLine3DSymbol();
  symbol3D->setAltitudeClamping( Qgis::AltitudeClamping::Absolute );
  symbol3D->setAltitudeBinding( Qgis::AltitudeBinding::Vertex );
  symbol3D->setRenderAsSimpleLines( true );
  symbol3D->setWidth( 0.08f );

  QgsPhongMaterialSettings material;
  const QColor edgeColor( 28, 24, 22, 255 );
  material.setAmbient( edgeColor );
  material.setDiffuse( edgeColor );
  material.setSpecular( Qt::black );
  material.setShininess( 0.0 );
  symbol3D->setMaterialSettings( material.clone() );

  QgsVectorLayer3DRenderer *renderer3D = new QgsVectorLayer3DRenderer();
  renderer3D->setSymbol( symbol3D );
  layer->setRenderer3D( renderer3D );
  layer->setOpacity( 1.0 );
}

QgsRectangle padded3DViewExtent( const QgsRectangle &extent )
{
  QgsRectangle padded = extent;
  if ( padded.isNull() )
    return padded;

  const double pad = std::max( std::max( padded.width(), padded.height() ) * 0.25, 20.0 );
  padded.grow( pad );
  return padded;
}

QByteArray makeRealtimeVertexBytes( const QVector<float> &values )
{
  QByteArray bytes;
  bytes.resize( values.size() * static_cast<int>( sizeof( float ) ) );
  if ( !values.isEmpty() )
    std::memcpy( bytes.data(), values.constData(), static_cast<size_t>( bytes.size() ) );
  return bytes;
}

void appendRealtimeTriangle( QVector<float> &values,
                             const QVector3D &v0,
                             const QVector3D &v1,
                             const QVector3D &v2 )
{
  QVector3D normal = QVector3D::crossProduct( v1 - v0, v2 - v0 ).normalized();
  if ( normal.lengthSquared() <= 0.000001f )
    normal = QVector3D( 0.0f, 0.0f, 1.0f );

  auto appendVertex = [&]( const QVector3D &v )
  {
    values << v.x() << v.y() << v.z()
           << normal.x() << normal.y() << normal.z();
  };
  appendVertex( v0 );
  appendVertex( v1 );
  appendVertex( v2 );
}

// ────────────────────────────────────────────────────────────
//  线框边提取：三角形面片摊平为线段顶点（不去重，零哈希开销）
// ────────────────────────────────────────────────────────────
static QVector<float> extractWireframeEdges( const MeshData &mesh,
                                              const QMatrix4x4 &poseMat )
{
  QVector<float> lineVerts;
  const int triCount = mesh.indices.size() / 3;
  if ( triCount == 0 )
    return lineVerts;

  lineVerts.reserve( triCount * 6 * 3 );  // 3 条边 × 6 floats per edge

  for ( int i = 0; i < triCount; ++i )
  {
    QVector3D v0 = poseMat.map( mesh.vertices[mesh.indices[i * 3]] );
    QVector3D v1 = poseMat.map( mesh.vertices[mesh.indices[i * 3 + 1]] );
    QVector3D v2 = poseMat.map( mesh.vertices[mesh.indices[i * 3 + 2]] );

    auto pushEdge = [&]( const QVector3D &a, const QVector3D &b ) {
      lineVerts << a.x() << a.y() << a.z()
                << b.x() << b.y() << b.z();
    };
    pushEdge( v0, v1 );
    pushEdge( v1, v2 );
    pushEdge( v2, v0 );
  }

  return lineVerts;
}

void updateRealtimePart( RealtimePreviewPart &part,
                         Qt3DCore::QEntity *root,
                         const QVector<float> &values,
                         const QColor &color,
                         const QString &objectName )
{
  const int vertexCount = values.size() / 6;

  if ( !part.entity )
  {
    part.entity = new Qt3DCore::QEntity( root );
    part.entity->setObjectName( objectName );

    Qt3DRender::QGeometry *geometry = new Qt3DRender::QGeometry( part.entity );
    part.buffer = new Qt3DRender::QBuffer( geometry );

    part.positionAttribute = new Qt3DRender::QAttribute( geometry );
    part.positionAttribute->setName( Qt3DRender::QAttribute::defaultPositionAttributeName() );
    part.positionAttribute->setVertexBaseType( Qt3DRender::QAttribute::Float );
    part.positionAttribute->setVertexSize( 3 );
    part.positionAttribute->setAttributeType( Qt3DRender::QAttribute::VertexAttribute );
    part.positionAttribute->setBuffer( part.buffer );
    part.positionAttribute->setByteOffset( 0 );
    part.positionAttribute->setByteStride( 6 * sizeof( float ) );
    geometry->addAttribute( part.positionAttribute );

    part.normalAttribute = new Qt3DRender::QAttribute( geometry );
    part.normalAttribute->setName( Qt3DRender::QAttribute::defaultNormalAttributeName() );
    part.normalAttribute->setVertexBaseType( Qt3DRender::QAttribute::Float );
    part.normalAttribute->setVertexSize( 3 );
    part.normalAttribute->setAttributeType( Qt3DRender::QAttribute::VertexAttribute );
    part.normalAttribute->setBuffer( part.buffer );
    part.normalAttribute->setByteOffset( 3 * sizeof( float ) );
    part.normalAttribute->setByteStride( 6 * sizeof( float ) );
    geometry->addAttribute( part.normalAttribute );

    // Dummy giant bounding volume — prevents Qt3D frustum culling
    // from ever discarding the entity, regardless of camera position.
    {
      Qt3DRender::QBuffer *bboxBuf = new Qt3DRender::QBuffer( geometry );
      static const float giantBox[6] = { -1e7f, -1e7f, -1e7f, 1e7f, 1e7f, 1e7f };
      bboxBuf->setData( QByteArray::fromRawData(
        reinterpret_cast<const char *>( giantBox ), static_cast<int>( sizeof( giantBox ) ) ) );
      Qt3DRender::QAttribute *bboxAttr = new Qt3DRender::QAttribute( geometry );
      bboxAttr->setVertexBaseType( Qt3DRender::QAttribute::Float );
      bboxAttr->setVertexSize( 3 );
      bboxAttr->setAttributeType( Qt3DRender::QAttribute::VertexAttribute );
      bboxAttr->setBuffer( bboxBuf );
      bboxAttr->setByteOffset( 0 );
      bboxAttr->setByteStride( 3 * sizeof( float ) );
      bboxAttr->setCount( 2 );
      geometry->addAttribute( bboxAttr );
      geometry->setBoundingVolumePositionAttribute( bboxAttr );
    }

    part.renderer = new Qt3DRender::QGeometryRenderer( part.entity );
    part.renderer->setGeometry( geometry );
    part.renderer->setPrimitiveType( Qt3DRender::QGeometryRenderer::Triangles );
    part.entity->addComponent( part.renderer );

    part.material = new Qt3DExtras::QPhongMaterial( part.entity );
    part.entity->addComponent( part.material );
  }

  // 开启 alpha 混合（只设置一次），否则 setDiffuse/setAmbient 的 alpha 值不生效
  if ( !part.blendingSetup && part.material )
  {
    Qt3DRender::QEffect *effect = part.material->effect();
    if ( effect )
    {
      Qt3DRender::QBlendEquationArguments *blendArgs = new Qt3DRender::QBlendEquationArguments();
      blendArgs->setSourceRgb( Qt3DRender::QBlendEquationArguments::SourceAlpha );
      blendArgs->setDestinationRgb( Qt3DRender::QBlendEquationArguments::OneMinusSourceAlpha );

      Qt3DRender::QBlendEquation *blendEq = new Qt3DRender::QBlendEquation();
      blendEq->setBlendFunction( Qt3DRender::QBlendEquation::Add );

      // render state 是加到 QRenderPass 上的，不是 QEffect
      for ( Qt3DRender::QTechnique *tech : effect->techniques() )
      {
        for ( Qt3DRender::QRenderPass *pass : tech->renderPasses() )
        {
          pass->addRenderState( blendArgs );
          pass->addRenderState( blendEq );
        }
      }
    }
    part.blendingSetup = true;
  }

  // Disable back-face culling for this entity (one-shot).
  // Qt3D defaults to QCullFace::Back; NoCulling ensures every triangle
  // is visible from any camera angle, which is important for building
  // models with internal faces (indentations, open bottoms, etc.).
  if ( !part.cullingSetup && part.material )
  {
    Qt3DRender::QEffect *effect = part.material->effect();
    if ( effect )
    {
      Qt3DRender::QCullFace *noCull = new Qt3DRender::QCullFace();
      noCull->setMode( Qt3DRender::QCullFace::NoCulling );
      for ( Qt3DRender::QTechnique *tech : effect->techniques() )
      {
        for ( Qt3DRender::QRenderPass *pass : tech->renderPasses() )
          pass->addRenderState( noCull );
      }
    }
    part.cullingSetup = true;
  }

  // 深度状态初始化（一次性）：创建 QDepthTest + QNoDepthMask，挂 depthTest 到所有 render pass
  // （wireframe 模式下 body/roof 被隐藏，此代码不生效，保留以备未来可能的透明模式复用）
  if ( !part.depthStateSetup && part.material )
  {
    Qt3DRender::QEffect *effect = part.material->effect();
    if ( effect )
    {
      part.depthTest = new Qt3DRender::QDepthTest();
      part.noDepthMask = new Qt3DRender::QNoDepthMask();

      for ( Qt3DRender::QTechnique *tech : effect->techniques() )
      {
        for ( Qt3DRender::QRenderPass *pass : tech->renderPasses() )
        {
          pass->addRenderState( part.depthTest );
        }
      }

      part.depthStateSetup = true;
    }
  }

  // 每帧动态切换深度状态：wireframe ON → Always + 禁止写深度；OFF → Less + 正常写深度
  if ( part.depthStateSetup && part.depthTest && part.material )
  {
    Qt3DRender::QEffect *effect = part.material->effect();
    if ( effect )
    {
      part.depthTest->setDepthFunction(
        sWireframeMode
          ? Qt3DRender::QDepthTest::Always
          : Qt3DRender::QDepthTest::Less
      );

      for ( Qt3DRender::QTechnique *tech : effect->techniques() )
      {
        for ( Qt3DRender::QRenderPass *pass : tech->renderPasses() )
        {
          if ( !part.noDepthMask )
            continue;

          const QVector<Qt3DRender::QRenderState *> states = pass->renderStates();
          const bool hasNoDepthMask = states.contains( part.noDepthMask );

          if ( sWireframeMode )
          {
            if ( !hasNoDepthMask )
              pass->addRenderState( part.noDepthMask );
          }
          else
          {
            if ( hasNoDepthMask )
              pass->removeRenderState( part.noDepthMask );
          }
        }
      }
    }
  }

  // 每次调用都更新材质颜色
  if ( part.material )
  {
    part.material->setAmbient( color.darker( 135 ) );
    part.material->setDiffuse( color );
    part.material->setSpecular( QColor( 20, 18, 16, color.alpha() ) );
    part.material->setShininess( 1.0f );
  }

  part.buffer->setData( makeRealtimeVertexBytes( values ) );
  part.positionAttribute->setCount( vertexCount );
  part.normalAttribute->setCount( vertexCount );
  part.renderer->setVertexCount( vertexCount );
  part.entity->setEnabled( vertexCount > 0 );

  // Qt3D caches the bounding volume from the first buffer upload and never
  // recomputes it when QBuffer::setData() replaces vertex data later.
  // Toggling the geometry off / on forces the renderer to re-evaluate it,
  // triggering a fresh bounding-volume calculation so the entity does not
  // get incorrectly frustum-culled after parameter changes.
  if ( part.positionAttribute )
  {
    Qt3DRender::QGeometry *geom = qobject_cast<Qt3DRender::QGeometry *>( part.positionAttribute->parent() );
    if ( geom )
    {
      part.renderer->setGeometry( nullptr );
      part.renderer->setGeometry( geom );
    }
  }
}

void ensureRealtimeAnchorLayer( QgisInterface *iface, const QgsRectangle &extent,
                                 double minZ = 0.0, double maxZ = 0.0 )
{
  if ( extent.isNull() )
    return;

  ParamModelerScene3D::removeLayerByName( REALTIME_ANCHOR_LAYER_NAME );

  QgsVectorLayer *anchorLayer = new QgsVectorLayer( "PointZ?crs=EPSG:3857", REALTIME_ANCHOR_LAYER_NAME, "memory" );
  if ( !anchorLayer || !anchorLayer->isValid() )
  {
    delete anchorLayer;
    return;
  }

  // Create 4 corner points at minZ and maxZ so the 3D scene's
  // near/far plane computation covers the model's full vertical extent
  QgsFeatureList anchorFeatures;
  auto addAnchor = [&]( double x, double y, double z ) {
    QgsFeature f;
    f.setGeometry( QgsGeometry( new QgsPoint( x, y, z ) ) );
    anchorFeatures << f;
  };
  addAnchor( extent.xMinimum(), extent.yMinimum(), minZ );
  addAnchor( extent.xMaximum(), extent.yMinimum(), minZ );
  addAnchor( extent.xMaximum(), extent.yMaximum(), minZ );
  addAnchor( extent.xMinimum(), extent.yMaximum(), minZ );
  addAnchor( extent.xMinimum(), extent.yMinimum(), maxZ );
  addAnchor( extent.xMaximum(), extent.yMinimum(), maxZ );
  addAnchor( extent.xMaximum(), extent.yMaximum(), maxZ );
  addAnchor( extent.xMinimum(), extent.yMaximum(), maxZ );

  anchorLayer->dataProvider()->addFeatures( anchorFeatures );
  anchorLayer->updateExtents();
  anchorLayer->setOpacity( 0.0 );
  QgsProject::instance()->addMapLayer( anchorLayer, false );

  if ( iface && iface->mapCanvas() )
  {
    QList<QgsMapLayer *> layers = iface->mapCanvas()->layers();
    if ( !layers.contains( anchorLayer ) )
      layers.append( anchorLayer );
    iface->mapCanvas()->setLayers( layers );
    iface->mapCanvas()->setExtent( extent );
    iface->mapCanvas()->refresh();
  }
}

Qgs3DMapCanvas *ensureRealtimePreviewCanvas( QgisInterface *iface, const QgsRectangle &extent )
{
  if ( !iface )
    return nullptr;

  const QList<Qgs3DMapCanvas *> existingCanvases = iface->mapCanvases3D();
  for ( Qgs3DMapCanvas *canvas : existingCanvases )
  {
    if ( canvas && canvas->scene() )
      return canvas;
  }

  ensureRealtimeAnchorLayer( iface, extent );

  const bool isNewCanvas = iface->mapCanvases3D().isEmpty();
  if ( isNewCanvas )
    iface->createNewMapCanvas3D( QObject::tr( "ParamModeler 3D" ) );

  const QList<Qgs3DMapCanvas *> canvases = iface->mapCanvases3D();
  for ( Qgs3DMapCanvas *canvas : canvases )
  {
    if ( !canvas )
      continue;
    Qgs3DMapSettings *s = canvas->mapSettings();
    if ( !s )
      continue;
    // One-shot: configure scene appearance only when the canvas is first created
    if ( isNewCanvas )
    {
      s->setTerrainRenderingEnabled( false );
      s->setBackgroundColor( QColor( 45, 48, 50 ) );
    }
    if ( canvas->scene() )
      return canvas;
  }
  return nullptr;
}
}

ParamModelerModelLoadResult ParamModelerScene3D::loadModelMesh( QgisInterface *iface,
                                                                 const MeshData &mesh,
                                                                 const ParamModelerPose &pose,
                                                                 QgsVectorLayer *previousModelLayer,
                                                                 const QString &previousGpkgPath,
                                                                 bool zoomToLayer,
                                                                 QWidget *parent )
{
  ParamModelerModelLoadResult result;
  const QString layerName = "ParamModeler_Model";
  const QString roofLayerName = "ParamModeler_Model_Roof";
  const QString edgeLayerName = "ParamModeler_Model_Edges";

  if ( !iface )
  {
    result.errorMessage = "QGIS interface is unavailable.";
    return result;
  }
  if ( mesh.isEmpty() )
  {
    result.errorMessage = "Mesh is empty.";
    return result;
  }

  QMatrix4x4 mat;
  mat.setToIdentity();
  mat.translate( pose.tx, pose.ty, pose.tz );
  mat.rotate( pose.rx, 1, 0, 0 );
  mat.rotate( pose.ry, 0, 1, 0 );
  mat.rotate( pose.rz, 0, 0, 1 );
  mat.scale( pose.scale );

  QgsMultiPolygon *bodyMultiPoly = new QgsMultiPolygon();
  QgsMultiPolygon *roofMultiPoly = new QgsMultiPolygon();
  const int triCount = mesh.indices.size() / 3;
  int bodyTriCount = 0;
  int roofTriCount = 0;
  for ( int i = 0; i < triCount; i++ )
  {
    const QVector3D local0 = mesh.vertices[mesh.indices[i * 3]];
    const QVector3D local1 = mesh.vertices[mesh.indices[i * 3 + 1]];
    const QVector3D local2 = mesh.vertices[mesh.indices[i * 3 + 2]];
    QVector3D v0 = mat.map( local0 );
    QVector3D v1 = mat.map( local1 );
    QVector3D v2 = mat.map( local2 );

    const QVector3D normal = QVector3D::crossProduct( local1 - local0, local2 - local0 ).normalized();
    if ( normal.z() > 0.15 )
    {
      roofMultiPoly->addGeometry( makeTrianglePolygon( v0, v1, v2 ) );
      roofTriCount++;
    }
    else
    {
      bodyMultiPoly->addGeometry( makeTrianglePolygon( v0, v1, v2 ) );
      bodyTriCount++;
    }
  }

  QString bodyGpkgPath = QDir::tempPath() + "/parammodeler_body_"
                     + QString::number( QDateTime::currentMSecsSinceEpoch() ) + ".gpkg";
  QString roofGpkgPath = QDir::tempPath() + "/parammodeler_roof_"
                     + QString::number( QDateTime::currentMSecsSinceEpoch() ) + ".gpkg";

  QString errorMessage;
  QgsVectorLayer *layer = bodyTriCount > 0 ? writeMeshLayer( bodyMultiPoly, layerName, bodyGpkgPath, &errorMessage ) : nullptr;
  QgsVectorLayer *roofLayer = roofTriCount > 0 ? writeMeshLayer( roofMultiPoly, roofLayerName, roofGpkgPath, &errorMessage ) : nullptr;
  if ( !layer )
  {
    result.errorMessage = errorMessage.isEmpty() ? "Failed to load model layer from GeoPackage." : errorMessage;
    if ( roofLayer )
      delete roofLayer;
    QFile::remove( bodyGpkgPath );
    QFile::remove( roofGpkgPath );
    return result;
  }
  if ( roofTriCount <= 0 )
  {
    delete roofMultiPoly;
    roofGpkgPath.clear();
  }
  else if ( !roofLayer )
  {
    QFile::remove( roofGpkgPath );
    roofGpkgPath.clear();
  }

  QgsVectorLayer *edgeLayer = makeMeshEdgeLayer( mesh, mat, edgeLayerName );
  if ( edgeLayer && edgeLayer->featureCount() <= 0 )
  {
    delete edgeLayer;
    edgeLayer = nullptr;
  }

  applyPolygon3DMaterial( layer,
                          QColor( 198, 192, 178, 42 ),
                          QColor( 126, 120, 108, 42 ),
                          QColor( 20, 18, 15, 10 ),
                          0.16 );
  if ( roofLayer )
  {
    applyPolygon3DMaterial( roofLayer,
                            QColor( 154, 50, 46, 58 ),
                            QColor( 88, 28, 26, 58 ),
                            QColor( 18, 8, 6, 12 ),
                            0.22 );
  }
  applyLine3DMaterial( edgeLayer );

  if ( previousModelLayer )
    QgsProject::instance()->removeMapLayer( previousModelLayer->id() );
  removeLayerByName( roofLayerName );
  removeLayerByName( edgeLayerName );

  QgsProject::instance()->addMapLayer( layer );
  if ( roofLayer )
    QgsProject::instance()->addMapLayer( roofLayer );
  if ( edgeLayer )
    QgsProject::instance()->addMapLayer( edgeLayer );

  QgsRectangle viewExtent = layer->extent();
  if ( roofLayer )
    viewExtent.combineExtentWith( roofLayer->extent() );
  if ( edgeLayer )
    viewExtent.combineExtentWith( edgeLayer->extent() );
  viewExtent = padded3DViewExtent( viewExtent );

  if ( !previousGpkgPath.isEmpty() )
    removeTempGpkgPaths( previousGpkgPath );

  if ( iface->mapCanvases3D().isEmpty() )
    iface->createNewMapCanvas3D( QObject::tr( "ParamModeler 3D" ) );

  QList<Qgs3DMapCanvas *> canvases3D = iface->mapCanvases3D();
  for ( Qgs3DMapCanvas *canvas3D : canvases3D )
  {
    if ( !canvas3D )
      continue;
    Qgs3DMapSettings *settings = canvas3D->mapSettings();
    if ( !settings )
      continue;

    if ( !viewExtent.isNull() )
      settings->setExtent( viewExtent );

    QList<QgsMapLayer *> curLayers = settings->layers();
    curLayers.erase(
      std::remove_if( curLayers.begin(), curLayers.end(),
                      [&]( QgsMapLayer *l ) {
                        return l && ( ( l->name() == layerName && l->id() != layer->id() ) ||
                                      ( l->name() == roofLayerName && ( !roofLayer || l->id() != roofLayer->id() ) ) ||
                                      ( l->name() == edgeLayerName && ( !edgeLayer || l->id() != edgeLayer->id() ) ) );
                      } ),
      curLayers.end() );
    if ( !curLayers.contains( layer ) )
      curLayers.append( layer );
    if ( roofLayer && !curLayers.contains( roofLayer ) )
      curLayers.append( roofLayer );
    if ( edgeLayer && !curLayers.contains( edgeLayer ) )
      curLayers.append( edgeLayer );
    settings->setLayers( curLayers );
    if ( zoomToLayer && !viewExtent.isNull() )
      canvas3D->setViewFrom2DExtent( viewExtent );
  }

  layer->triggerRepaint();
  if ( roofLayer )
    roofLayer->triggerRepaint();
  if ( edgeLayer )
    edgeLayer->triggerRepaint();

  if ( zoomToLayer && iface->mapCanvas() )
  {
    iface->mapCanvas()->setExtent( viewExtent );
    iface->mapCanvas()->refresh();
  }

  Q_UNUSED( parent );
  result.layer = layer;
  result.gpkgPath = bodyGpkgPath;
  if ( !roofGpkgPath.isEmpty() )
    result.gpkgPath += "\n" + roofGpkgPath;
  result.triangleCount = triCount;
  return result;
}

bool ParamModelerScene3D::updateRealtimePreviewMesh( QgisInterface *iface,
                                                     const MeshData &mesh,
                                                     const ParamModelerPose &pose,
                                                     QString *errorMessage )
{
  if ( !iface )
  {
    if ( errorMessage )
      *errorMessage = "QGIS interface is unavailable.";
    return false;
  }
  if ( mesh.isEmpty() )
  {
    if ( errorMessage )
      *errorMessage = "Mesh is empty.";
    return false;
  }

  QMatrix4x4 mat;
  mat.setToIdentity();
  mat.translate( pose.tx, pose.ty, pose.tz );
  mat.rotate( pose.rx, 1, 0, 0 );
  mat.rotate( pose.ry, 0, 1, 0 );
  mat.rotate( pose.rz, 0, 0, 1 );
  mat.scale( pose.scale );

  QVector<float> bodyValues;
  QVector<float> roofValues;
  bodyValues.reserve( mesh.indices.size() * 6 );
  roofValues.reserve( mesh.indices.size() * 6 );

  bool hasExtent = false;
  double xmin = 0.0, ymin = 0.0, zmin = 0.0;
  double xmax = 0.0, ymax = 0.0, zmax = 0.0;
  auto addToExtent = [&]( const QVector3D &v )
  {
    if ( !hasExtent )
    {
      xmin = xmax = v.x();
      ymin = ymax = v.y();
      zmin = zmax = v.z();
      hasExtent = true;
      return;
    }
    xmin = std::min( xmin, static_cast<double>( v.x() ) );
    ymin = std::min( ymin, static_cast<double>( v.y() ) );
    zmin = std::min( zmin, static_cast<double>( v.z() ) );
    xmax = std::max( xmax, static_cast<double>( v.x() ) );
    ymax = std::max( ymax, static_cast<double>( v.y() ) );
    zmax = std::max( zmax, static_cast<double>( v.z() ) );
  };

  const int triCount = mesh.indices.size() / 3;
  for ( int i = 0; i < triCount; ++i )
  {
    const int idx0 = mesh.indices[i * 3];
    const int idx1 = mesh.indices[i * 3 + 1];
    const int idx2 = mesh.indices[i * 3 + 2];
    if ( idx0 < 0 || idx1 < 0 || idx2 < 0 ||
         idx0 >= mesh.vertices.size() || idx1 >= mesh.vertices.size() || idx2 >= mesh.vertices.size() )
      continue;

    const QVector3D local0 = mesh.vertices[idx0];
    const QVector3D local1 = mesh.vertices[idx1];
    const QVector3D local2 = mesh.vertices[idx2];
    const QVector3D v0 = mat.map( local0 );
    const QVector3D v1 = mat.map( local1 );
    const QVector3D v2 = mat.map( local2 );

    addToExtent( v0 );
    addToExtent( v1 );
    addToExtent( v2 );

    const QVector3D localNormal = QVector3D::crossProduct( local1 - local0, local2 - local0 ).normalized();
    QVector<float> &target = localNormal.z() > 0.15f ? roofValues : bodyValues;
    appendRealtimeTriangle( target, v0, v1, v2 );
  }

  if ( !hasExtent )
  {
    if ( errorMessage )
      *errorMessage = "Mesh has no valid triangles.";
    return false;
  }

  QgsRectangle previewExtent( xmin, ymin, xmax, ymax );
  previewExtent = padded3DViewExtent( previewExtent );

  Qgs3DMapCanvas *canvas = ensureRealtimePreviewCanvas( iface, previewExtent );
  // Refresh anchor layer with full 3D extent so QGIS's near/far plane
  // computation (updateCameraNearFarPlanes) covers the model's height
  ensureRealtimeAnchorLayer( iface, previewExtent, zmin, zmax );
  if ( !canvas || !canvas->scene() )
  {
    if ( errorMessage )
      *errorMessage = "QGIS 3D scene is unavailable.";
    return false;
  }

  Qgs3DMapScene *scene = canvas->scene();
  RealtimePreviewState state = sRealtimePreviewMeshes.value( scene );
  const bool newPreviewRoot = !state.root;
  if ( newPreviewRoot )
  {
    QObject::connect( scene, &QObject::destroyed, scene, [scene]() {
      sRealtimePreviewMeshes.remove( scene );
    }, Qt::UniqueConnection );

    state.root = new Qt3DCore::QEntity( scene );
    state.root->setObjectName( QStringLiteral( "ParamModeler_Qt3D_RealtimePreview" ) );
  }

  // Align Qt3D entity coordinates with QGIS layer coordinates.
  // QGIS 3D layers are rendered relative to the map origin, but raw Qt3D
  // entities are not.  Offset the root entity by -origin so the model and
  // point-cloud layers share the same coordinate frame.
  //
  // IMPORTANT: use the CURRENT origin (not a frozen snapshot).  QGIS shifts
  // its floating origin as the camera moves; if we keep a stale offset the
  // entity ends up in the wrong coordinate frame, causing the model to
  // disappear during zoom / rotation.
  {
    Qgs3DMapSettings *mapSettings = canvas->mapSettings();
    if ( mapSettings )
    {
      // Wire up origin tracking so the entity follows QGIS coordinate shifts
      if ( newPreviewRoot )
      {
        state.mapSettings = mapSettings;
        QObject::connect( mapSettings, &Qgs3DMapSettings::originChanged,
                          scene, [scene]()
        {
          RealtimePreviewState st = sRealtimePreviewMeshes.value( scene );
          if ( !st.root || !st.mapSettings )
            return;
          const QgsVector3D newOrigin = st.mapSettings->origin();
          for ( Qt3DCore::QComponent *comp : st.root->components() )
          {
            Qt3DCore::QTransform *t = qobject_cast<Qt3DCore::QTransform *>( comp );
            if ( t )
            {
              t->setTranslation( QVector3D(
                -static_cast<float>( newOrigin.x() ),
                -static_cast<float>( newOrigin.y() ),
                -static_cast<float>( newOrigin.z() ) ) );
              break;
            }
          }
        } );

        // QGIS's updateCameraNearFarPlanes() computes near/far from map-layer
        // scene entities.  Our custom Qt3D entity is not a map-layer entity,
        // so the model's vertical extent is invisible to the depth-range
        // calculation.  Force absurdly wide near/far after every camera update
        // so the model never gets clipped by the projection frustum.
        Qt3DRender::QCamera *camera = canvas->scene()->cameraController()->camera();
        if ( camera )
        {
          auto forceNearFar = [camera]()
          {
            camera->setNearPlane( 0.001f );
            camera->setFarPlane( 1e9f );
          };
          forceNearFar();
          QObject::connect( canvas->scene()->cameraController(),
                            &QgsCameraController::cameraChanged,
                            camera, forceNearFar, Qt::QueuedConnection );
        }
      }

      const QgsVector3D origin = mapSettings->origin();
      Qt3DCore::QTransform *rootTransform = nullptr;
      for ( Qt3DCore::QComponent *comp : state.root->components() )
      {
        rootTransform = qobject_cast<Qt3DCore::QTransform *>( comp );
        if ( rootTransform )
          break;
      }
      if ( !rootTransform )
      {
        rootTransform = new Qt3DCore::QTransform( state.root );
        state.root->addComponent( rootTransform );
      }
      rootTransform->setTranslation(
        QVector3D( -static_cast<float>( origin.x() ),
                   -static_cast<float>( origin.y() ),
                   -static_cast<float>( origin.z() ) ) );
    }
  }

  // ── 实体三角形：始终不透明 ──
  const int alpha = 255;
  updateRealtimePart( state.body,
                      state.root,
                      bodyValues,
                      QColor( 198, 192, 178, alpha ),
                      QStringLiteral( "ParamModeler_Qt3D_RealtimePreview_Body" ) );
  updateRealtimePart( state.roof,
                      state.root,
                      roofValues,
                      QColor( 154, 50, 46, alpha ),
                      QStringLiteral( "ParamModeler_Qt3D_RealtimePreview_Roof" ) );

  // ── 线框实体（一次性创建）──
  {
    QVector<float> wireVerts = extractWireframeEdges( mesh, mat );
    const int wireVertCount = wireVerts.size() / 3;

    if ( !state.wireframe.entity )
    {
      state.wireframe.entity = new Qt3DCore::QEntity( state.root );
      state.wireframe.entity->setObjectName(
        QStringLiteral( "ParamModeler_Qt3D_RealtimePreview_Wireframe" ) );

      Qt3DRender::QGeometry *wireGeom = new Qt3DRender::QGeometry( state.wireframe.entity );
      state.wireframe.buffer = new Qt3DRender::QBuffer( wireGeom );

      // 位置属性 (stride = 3 floats)
      state.wireframe.positionAttribute = new Qt3DRender::QAttribute( wireGeom );
      state.wireframe.positionAttribute->setName( Qt3DRender::QAttribute::defaultPositionAttributeName() );
      state.wireframe.positionAttribute->setVertexBaseType( Qt3DRender::QAttribute::Float );
      state.wireframe.positionAttribute->setVertexSize( 3 );
      state.wireframe.positionAttribute->setAttributeType( Qt3DRender::QAttribute::VertexAttribute );
      state.wireframe.positionAttribute->setBuffer( state.wireframe.buffer );
      state.wireframe.positionAttribute->setByteOffset( 0 );
      state.wireframe.positionAttribute->setByteStride( 3 * sizeof( float ) );
      wireGeom->addAttribute( state.wireframe.positionAttribute );

      // 法线属性 — QPhongMaterial 必须有法线，否则 shader 输出全黑
      // 全部填充 (0,1,0) 让线段获得正面 diffuse 光照
      Qt3DRender::QBuffer *wireNormalBuf = new Qt3DRender::QBuffer( wireGeom );
      state.wireframe.normalAttribute = new Qt3DRender::QAttribute( wireGeom );
      state.wireframe.normalAttribute->setName( Qt3DRender::QAttribute::defaultNormalAttributeName() );
      state.wireframe.normalAttribute->setVertexBaseType( Qt3DRender::QAttribute::Float );
      state.wireframe.normalAttribute->setVertexSize( 3 );
      state.wireframe.normalAttribute->setAttributeType( Qt3DRender::QAttribute::VertexAttribute );
      state.wireframe.normalAttribute->setBuffer( wireNormalBuf );
      state.wireframe.normalAttribute->setByteOffset( 0 );
      state.wireframe.normalAttribute->setByteStride( 3 * sizeof( float ) );
      wireGeom->addAttribute( state.wireframe.normalAttribute );

      // Dummy giant bounding volume — prevents Qt3D frustum culling
      {
        Qt3DRender::QBuffer *wBboxBuf = new Qt3DRender::QBuffer( wireGeom );
        static const float giantBox[6] = { -1e7f, -1e7f, -1e7f, 1e7f, 1e7f, 1e7f };
        wBboxBuf->setData( QByteArray::fromRawData(
          reinterpret_cast<const char *>( giantBox ), static_cast<int>( sizeof( giantBox ) ) ) );
        Qt3DRender::QAttribute *wBboxAttr = new Qt3DRender::QAttribute( wireGeom );
        wBboxAttr->setVertexBaseType( Qt3DRender::QAttribute::Float );
        wBboxAttr->setVertexSize( 3 );
        wBboxAttr->setAttributeType( Qt3DRender::QAttribute::VertexAttribute );
        wBboxAttr->setBuffer( wBboxBuf );
        wBboxAttr->setByteOffset( 0 );
        wBboxAttr->setByteStride( 3 * sizeof( float ) );
        wBboxAttr->setCount( 2 );
        wireGeom->addAttribute( wBboxAttr );
        wireGeom->setBoundingVolumePositionAttribute( wBboxAttr );
      }

      state.wireframe.renderer = new Qt3DRender::QGeometryRenderer( state.wireframe.entity );
      state.wireframe.renderer->setGeometry( wireGeom );
      state.wireframe.renderer->setPrimitiveType( Qt3DRender::QGeometryRenderer::Lines );
      state.wireframe.entity->addComponent( state.wireframe.renderer );

      state.wireframe.material = new Qt3DExtras::QPhongMaterial( state.wireframe.entity );
      state.wireframe.entity->addComponent( state.wireframe.material );
    }

    // ── 每帧更新线框顶点数据 ──
    state.wireframe.buffer->setData(
      QByteArray( reinterpret_cast<const char *>( wireVerts.constData() ),
                  wireVerts.size() * static_cast<int>( sizeof( float ) ) ) );
    state.wireframe.positionAttribute->setCount( wireVertCount );
    state.wireframe.renderer->setVertexCount( wireVertCount );

    // Force bounding-volume recalculation (same reason as body/roof)
    if ( state.wireframe.positionAttribute )
    {
      Qt3DRender::QGeometry *wGeom = qobject_cast<Qt3DRender::QGeometry *>( state.wireframe.positionAttribute->parent() );
      if ( wGeom )
      {
        state.wireframe.renderer->setGeometry( nullptr );
        state.wireframe.renderer->setGeometry( wGeom );
      }
    }

    // 每帧填充法线 buffer：统一指向上方 (0,1,0)，配合 Phong 光照
    {
      QByteArray normalBytes( wireVertCount * 3 * static_cast<int>( sizeof( float ) ), '\0' );
      float *norms = reinterpret_cast<float *>( normalBytes.data() );
      for ( int i = 0; i < wireVertCount; ++i )
      {
        norms[i * 3 + 0] = 0.0f;
        norms[i * 3 + 1] = 1.0f;   // 法线 = (0,1,0)，正面迎光
        norms[i * 3 + 2] = 0.0f;
      }
      // 正常 buffer 通过 normalAttribute 的 buffer() 获取
      Qt3DRender::QBuffer *nBuf = qobject_cast<Qt3DRender::QBuffer *>(
        state.wireframe.normalAttribute->buffer() );
      if ( nBuf )
        nBuf->setData( normalBytes );
      state.wireframe.normalAttribute->setCount( wireVertCount );
    }

    // 线框材质：亮黄色
    if ( state.wireframe.material )
    {
      QColor wireColor( 255, 220, 0 );   // 亮黄色
      state.wireframe.material->setAmbient( QColor( 180, 155, 0 ) );
      state.wireframe.material->setDiffuse( wireColor );
      state.wireframe.material->setSpecular( QColor( 30, 30, 30 ) );
      state.wireframe.material->setShininess( 0.0f );
    }
  }

  // ── 切换 visibility ──
  state.body.entity->setEnabled( !sWireframeMode && bodyValues.size() > 0 );
  state.roof.entity->setEnabled( !sWireframeMode && roofValues.size() > 0 );
  state.wireframe.entity->setEnabled( sWireframeMode && state.wireframe.entity );

  sRealtimePreviewMeshes.insert( scene, state );

  if ( newPreviewRoot )
    canvas->setViewFrom2DExtent( previewExtent );

  return true;
}

void ParamModelerScene3D::clearRealtimePreviewMesh( QgisInterface *iface )
{
  if ( !iface )
    return;

  const QList<Qgs3DMapCanvas *> canvases = iface->mapCanvases3D();
  for ( Qgs3DMapCanvas *canvas : canvases )
  {
    if ( !canvas || !canvas->scene() )
      continue;

    Qgs3DMapScene *scene = canvas->scene();
    RealtimePreviewState state = sRealtimePreviewMeshes.take( scene );
    if ( state.root )
      delete state.root;
  }

  removeLayerByName( REALTIME_ANCHOR_LAYER_NAME );
}

void ParamModelerScene3D::clearAll3DEntities( QgisInterface *iface )
{
  // 清除 Qt3D 实时预览实体 + anchor 图层
  clearRealtimePreviewMesh( iface );

  // 清除 legacy 模型图层
  removeLayerByName( QStringLiteral( "ParamModeler_Model" ) );
  removeLayerByName( QStringLiteral( "ParamModeler_Model_Roof" ) );
  removeLayerByName( QStringLiteral( "ParamModeler_Model_Edges" ) );
}

QgsMapLayer *ParamModelerScene3D::loadExternalPointCloud( QgisInterface *iface,
                                                          const QString &filePath,
                                                          const QString &layerName,
                                                          QWidget *parent,
                                                          QString *errorMessage )
{
  if ( !iface )
  {
    if ( errorMessage )
      *errorMessage = "QGIS interface is unavailable.";
    return nullptr;
  }

  const QString suffix = QFileInfo( filePath ).suffix().toLower();
  if ( suffix != "ply" && suffix != "las" && suffix != "laz" &&
       suffix != "txt" && suffix != "xyz" && suffix != "pts" )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "暂不支持该点云格式：%1" ).arg( suffix );
    return nullptr;
  }

  PointCloud pc = PointCloudLoader::load( filePath );
  if ( pc.points.isEmpty() )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "无法读取点云或点云为空：%1" ).arg( filePath );
    return nullptr;
  }

  QgsVectorLayer *vl = new QgsVectorLayer( "PointZ?crs=EPSG:3857", layerName, "memory" );

  QgsPoint3DSymbol *symbol3D = new QgsPoint3DSymbol();
  symbol3D->setAltitudeClamping( Qgis::AltitudeClamping::Absolute );
  symbol3D->setShape( Qgis::Point3DShape::Sphere );
  QVariantMap props;
  const QVector3D bboxSize = pc.bboxMax - pc.bboxMin;
  const double maxDim = std::max( { std::abs( static_cast<double>( bboxSize.x() ) ),
                                    std::abs( static_cast<double>( bboxSize.y() ) ),
                                    std::abs( static_cast<double>( bboxSize.z() ) ) } );
  props["radius"] = std::max( maxDim / 350.0, 0.003 );
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

  QgsFeatureList features;
  features.reserve( 1000 );
  for ( const QVector3D &p : pc.points )
  {
    QgsFeature feat;
    feat.setGeometry( QgsGeometry( new QgsPoint( p.x(), p.y(), p.z() ) ) );
    features.append( feat );

    if ( features.size() >= 1000 )
    {
      vl->dataProvider()->addFeatures( features );
      features.clear();
    }
  }

  if ( !features.isEmpty() )
    vl->dataProvider()->addFeatures( features );

  if ( !vl->isValid() )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "点云图层无效：%1" ).arg( filePath );
    delete vl;
    return nullptr;
  }

  removeLayersByNamePrefix( QStringLiteral( "External point cloud - " ) );
  removeLayerByName( layerName );
  QgsProject::instance()->addMapLayer( vl );

  QgsRectangle viewExtent = padded3DViewExtent( vl->extent() );

  if ( iface->mapCanvas() )
  {
    iface->mapCanvas()->setExtent( viewExtent );
    iface->mapCanvas()->refresh();
  }

  if ( iface->mapCanvases3D().isEmpty() )
  {
    Qgs3DMapCanvas *canvas3D = iface->createNewMapCanvas3D( "ParamModeler 3D" );
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

  QList<Qgs3DMapCanvas *> canvases3D = iface->mapCanvases3D();
  for ( Qgs3DMapCanvas *canvas3D : canvases3D )
  {
    if ( !canvas3D )
      continue;
    Qgs3DMapSettings *settings = canvas3D->mapSettings();
    if ( !settings )
      continue;

    if ( !viewExtent.isNull() )
      settings->setExtent( viewExtent );

    QList<QgsMapLayer *> curLayers = settings->layers();
    curLayers.erase(
      std::remove_if( curLayers.begin(), curLayers.end(),
                      [&]( QgsMapLayer *l ) {
                        return l && l->name() == layerName && l->id() != vl->id();
                      } ),
      curLayers.end() );
    if ( !curLayers.contains( vl ) )
      curLayers.append( vl );
    settings->setLayers( curLayers );
    if ( !viewExtent.isNull() )
      canvas3D->setViewFrom2DExtent( viewExtent );
  }

  vl->triggerRepaint();

  Q_UNUSED( parent );
  return vl;
}

namespace
{
void removeProjectLayersMatching( const std::function<bool( QgsMapLayer * )> &matches, const QString &excludeId )
{
  QStringList toRemove;
  const auto layers = QgsProject::instance()->mapLayers();
  for ( auto it = layers.cbegin(); it != layers.cend(); ++it )
  {
    QgsMapLayer *layer = it.value();
    if ( layer && matches( layer ) && it.key() != excludeId )
      toRemove << it.key();
  }

  for ( const QString &id : toRemove )
    QgsProject::instance()->removeMapLayer( id );
}
}

void ParamModelerScene3D::removeLayerByName( const QString &name, const QString &excludeId )
{
  removeProjectLayersMatching( [&]( QgsMapLayer *layer ) {
    return layer && layer->name() == name;
  }, excludeId );
}

void ParamModelerScene3D::removeLayersByNamePrefix( const QString &prefix, const QString &excludeId )
{
  removeProjectLayersMatching( [&]( QgsMapLayer *layer ) {
    return layer && layer->name().startsWith( prefix );
  }, excludeId );
}

void ParamModelerScene3D::setWireframeMode( bool on )
{
  sWireframeMode = on;
}

bool ParamModelerScene3D::isWireframeMode()
{
  return sWireframeMode;
}
