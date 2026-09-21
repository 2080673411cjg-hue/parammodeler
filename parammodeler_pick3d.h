#ifndef PARAMMODELER_PICK3D_H
#define PARAMMODELER_PICK3D_H

#include "qgs3dmaptool.h"
#include "qgspoint.h"
#include "parammodeler_cornerfit.h"
#include <QPointer>
#include <QVector>
#include <QVector3D>
#include <functional>
#include <memory>

class QgsVectorLayer;
class ParamModelerPickOverlay;
class QDialog;
class QLabel;
class QPushButton;
class QWidget;

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
  void enableCornerFit( double radius, const QVector3D &up, QWidget *panelParent );

  std::function<void( const QgsPoint & )> picked;
  std::function<void()> stopped;

protected:
  bool eventFilter( QObject *watched, QEvent *event ) override;

private:
  int nearestPoint( const QPoint &position ) const;
  void updateMarkers( int candidate );
  void finish();
  void applyTarget( const QgsPoint &target );
  void selectFace( int candidate, const QPoint &globalPosition );
  void updateFitPanel( const QString &error = QString() );
  bool sceneValid() const;
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
  bool mCornerFit = false;
  bool mNavigate = false;
  bool mHasCorner = false;
  double mFitRadius = 1;
  ParamModelerCornerFit::Point mUp = ParamModelerCornerFit::Point::UnitZ();
  std::vector<ParamModelerCornerFit::Point> mFitPoints;
  std::vector<ParamModelerCornerFit::Plane> mPlanes;
  std::vector<bool> mPlaneWeak;
  bool mHasCandidatePlane = false;
  ParamModelerCornerFit::Plane mCandidatePlane;
  bool mCandidatePlaneWeak = true;
  QgsPoint mCorner;
  QPointer<QDialog> mFitPanel;
  QPointer<QWidget> mPanelParent;
  QLabel *mFitStatus = nullptr;
  QPushButton *mApplyFit = nullptr;
  QPushButton *mBackFit = nullptr;
};

#endif
