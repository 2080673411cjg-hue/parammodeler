#ifndef PARAMMODELER_PICK3D_H
#define PARAMMODELER_PICK3D_H

#include "qgs3dmaptool.h"
#include "qgspoint.h"
#include <QPointer>
#include <QVector>
#include <functional>
#include <memory>

class QgsVectorLayer;
class ParamModelerPickOverlay;

class ParamModelerPick3D : public Qgs3DMapTool
{
public:
  ParamModelerPick3D( Qgs3DMapCanvas *canvas, QgsVectorLayer *layer,
                     std::function<QgsPoint()> source );
  ~ParamModelerPick3D() override;
  void activate() override;
  void deactivate() override;
  void mousePressEvent( QMouseEvent *event ) override;
  void mouseMoveEvent( QMouseEvent *event ) override;
  void mouseReleaseEvent( QMouseEvent *event ) override;
  void refreshMarkers();

  std::function<void( const QgsPoint & )> picked;
  std::function<void()> stopped;

protected:
  bool eventFilter( QObject *watched, QEvent *event ) override;

private:
  int nearestPoint( const QPoint &position ) const;
  void updateMarkers( int candidate );
  void finish();
  QPointer<QgsVectorLayer> mLayer;
  QVector<QgsPoint> mPoints;
  std::function<QgsPoint()> mSource;
  std::unique_ptr<ParamModelerPickOverlay> mOverlay;
  int mCandidate = -1;
  QPoint mPressPosition;
  bool mPressed = false;
  bool mActive = false;
  bool mFinished = false;
  bool mCameraWasEnabled = true;
};

#endif
