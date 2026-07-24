#ifndef PARAMMODELER_SCENE3D_H
#define PARAMMODELER_SCENE3D_H

#include "meshdata.h"

#include <QString>

class QgisInterface;
class QWidget;
class QgsMapLayer;
class QgsVectorLayer;

struct ParamModelerPose
{
  double tx = 0.0;
  double ty = 0.0;
  double tz = 0.0;
  double rx = 0.0;
  double ry = 0.0;
  double rz = 0.0;
  double scale = 1.0;
};

struct ParamModelerModelLoadResult
{
  QgsVectorLayer *layer = nullptr;
  QString gpkgPath;
  int triangleCount = 0;
  QString errorMessage;
};

class ParamModelerScene3D
{
public:
  static ParamModelerModelLoadResult loadModelMesh( QgisInterface *iface,
                                                    const MeshData &mesh,
                                                    const ParamModelerPose &pose,
                                                    QgsVectorLayer *previousModelLayer,
                                                    const QString &previousGpkgPath,
                                                    bool zoomToLayer,
                                                    QWidget *parent );

  static bool updateRealtimePreviewMesh( QgisInterface *iface,
                                         const MeshData &mesh,
                                         const ParamModelerPose &pose,
                                         QString *errorMessage = nullptr );

  static void clearRealtimePreviewMesh( QgisInterface *iface );

  static QgsMapLayer *loadExternalPointCloud( QgisInterface *iface,
                                              const QString &filePath,
                                              const QString &layerName,
                                              QWidget *parent,
                                              QString *errorMessage = nullptr );

  static void setWireframeMode( bool on );
  static bool isWireframeMode();

  static void removeLayerByName( const QString &name, const QString &excludeId = QString() );
  static void removeLayersByNamePrefix( const QString &prefix, const QString &excludeId = QString() );
};

#endif // PARAMMODELER_SCENE3D_H
