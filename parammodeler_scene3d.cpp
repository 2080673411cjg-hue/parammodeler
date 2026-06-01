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
#include <QColor>
#include <QStringList>
#include <QVariantMap>
#include <QVector3D>
#include <algorithm>

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
#include <QgsPolygon3DSymbol.h>
#include <QgsPoint3DSymbol.h>
#include <QgsVectorLayer3DRenderer.h>
#include <Qgs3DMapCanvas.h>
#include <qgs3dmapsettings.h>
#include <qgs3dtypes.h>
#include <qgsphongmaterialsettings.h>
#include <qgsvectorfilewriter.h>
#include <qgsmapcanvas.h>

namespace
{
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

void applyPolygon3DMaterial( QgsVectorLayer *layer,
                             const QColor &diffuse,
                             const QColor &ambient,
                             const QColor &specular )
{
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

  applyPolygon3DMaterial( layer,
                          QColor( 168, 158, 138, 225 ),
                          QColor( 92, 84, 70, 225 ),
                          QColor( 18, 15, 12, 45 ) );
  if ( roofLayer )
  {
    applyPolygon3DMaterial( roofLayer,
                            QColor( 132, 50, 42, 235 ),
                            QColor( 72, 28, 24, 235 ),
                            QColor( 18, 8, 6, 45 ) );
  }

  if ( previousModelLayer )
    QgsProject::instance()->removeMapLayer( previousModelLayer->id() );
  removeLayerByName( roofLayerName );

  QgsProject::instance()->addMapLayer( layer );
  if ( roofLayer )
    QgsProject::instance()->addMapLayer( roofLayer );

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

    QList<QgsMapLayer *> curLayers = settings->layers();
    curLayers.erase(
      std::remove_if( curLayers.begin(), curLayers.end(),
                      [&]( QgsMapLayer *l ) {
                        return ( l->name() == layerName && l->id() != layer->id() ) ||
                               ( l->name() == roofLayerName && ( !roofLayer || l->id() != roofLayer->id() ) );
                      } ),
      curLayers.end() );
    if ( !curLayers.contains( layer ) )
      curLayers.append( layer );
    if ( roofLayer && !curLayers.contains( roofLayer ) )
      curLayers.append( roofLayer );
    settings->setLayers( curLayers );
  }

  layer->triggerRepaint();
  if ( roofLayer )
    roofLayer->triggerRepaint();

  if ( zoomToLayer && iface->mapCanvas() )
  {
    iface->mapCanvas()->setExtent( layer->extent() );
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
  if ( suffix != "ply" && suffix != "las" && suffix != "laz" )
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

  removeLayerByName( layerName );
  QgsProject::instance()->addMapLayer( vl );

  if ( iface->mapCanvas() )
  {
    iface->mapCanvas()->setExtent( vl->extent() );
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

  Q_UNUSED( parent );
  return vl;
}

void ParamModelerScene3D::removeLayerByName( const QString &name, const QString &excludeId )
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
