#include "parammodeler_pick3d.h"
#include "parammodeler_pick3d_math.h"
#include "parammodeler_pick3d_overlay.h"
#include "qgs3dmapcanvas.h"
#include "qgs3dmapsettings.h"
#include "qgscameracontroller.h"
#include "qgsfeature.h"
#include "qgsframegraph.h"
#include "qgsvectorlayer.h"
#include "qgswindow3dengine.h"
#include <Qt3DRender/QCamera>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QToolTip>
#include <cmath>

namespace
{
// Both mouse positions and the canvas size use logical pixels (including HiDPI).
bool projectPoint( const QgsPoint &point, Qgs3DMapCanvas *canvas, QPointF &screen, double &depth )
{
  const QgsVector3D world = canvas->mapSettings()->mapToWorldCoordinates(
    QgsVector3D( point.x(), point.y(), point.z() ) );
  const auto *camera = canvas->cameraController()->camera();
  return ParamModelerPicking::project( QVector3D( world.x(), world.y(), world.z() ),
    camera->viewMatrix(), camera->projectionMatrix(), canvas->size(), screen, depth );
}
}

ParamModelerPick3D::ParamModelerPick3D( Qgs3DMapCanvas *canvas, QgsVectorLayer *layer,
                                      std::function<QgsPoint()> source )
  : Qgs3DMapTool( canvas ), mLayer( layer ), mSource( std::move( source ) )
{
  QgsFeature feature;
  auto features = layer->getFeatures( QgsFeatureRequest().setNoAttributes() );
  while ( features.nextFeature( feature ) )
  {
    const QgsGeometry geometry = feature.geometry();
    const auto *point = qgsgeometry_cast<const QgsPoint *>( geometry.constGet() );
    if ( point && std::isfinite( point->x() ) && std::isfinite( point->y() ) && std::isfinite( point->z() ) )
      mPoints.append( *point );
  }
  connect( layer, &QObject::destroyed, this, [this] { finish(); } );
  auto *markerTimer = new QTimer( this );
  markerTimer->setInterval( 50 );
  connect( markerTimer, &QTimer::timeout, this, &ParamModelerPick3D::refreshMarkers );
  markerTimer->start();
}

ParamModelerPick3D::~ParamModelerPick3D()
{
  mCanvas->removeEventFilter( this );
}

void ParamModelerPick3D::activate()
{
  mActive = true;
  mCameraWasEnabled = mCanvas->cameraController()->isEnabled();
  mCanvas->cameraController()->setEnabled( false );
  mCanvas->installEventFilter( this );
  mOverlay = std::make_unique<ParamModelerPickOverlay>( mCanvas->engine()->frameGraph()->rubberBandsRootEntity() );
  updateMarkers( -1 );
}

void ParamModelerPick3D::deactivate()
{
  mActive = false;
  mCanvas->removeEventFilter( this );
  mOverlay.reset();
  QToolTip::hideText();
  if ( mCanvas->cameraController() )
    mCanvas->cameraController()->setEnabled( mCameraWasEnabled );
  finish();
}

void ParamModelerPick3D::finish()
{
  if ( mFinished )
    return;
  mFinished = true;
  // Never delete the active tool from inside a canvas event or setMapTool().
  QTimer::singleShot( 0, this, [this] {
    const auto callback = stopped;
    if ( callback ) callback();
  } );
}

bool ParamModelerPick3D::eventFilter( QObject *watched, QEvent *event )
{
  // QGIS's canvas handles key events before forwarding to Qgs3DMapTool.
  if ( watched == mCanvas && mActive )
  {
    if ( event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress )
    {
      const auto *key = static_cast<QKeyEvent *>( event );
      if ( key->key() == Qt::Key_Escape )
      {
        if ( event->type() == QEvent::KeyPress ) finish();
        event->accept();
        return true;
      }
    }
    if ( event->type() == QEvent::Leave ) updateMarkers( -1 );
    if ( event->type() == QEvent::Resize ) updateMarkers( -1 );
  }
  return Qgs3DMapTool::eventFilter( watched, event );
}

int ParamModelerPick3D::nearestPoint( const QPoint &position ) const
{
  if ( !mLayer || !mCanvas->mapSettings() || !mCanvas->cameraController() ||
       !mCanvas->mapSettings()->layers().contains( mLayer.data() ) ||
       mCanvas->mapSettings()->crs() != mLayer->crs() )
    return -1;

  ParamModelerPicking::NearestPoint best;
  for ( int i = 0; i < mPoints.size(); ++i )
  {
    QPointF screen;
    double depth;
    if ( !projectPoint( mPoints[i], mCanvas, screen, depth ) ) continue;
    const double distance = std::hypot( screen.x() - position.x(), screen.y() - position.y() );
    best.consider( i, distance, depth );
  }
  return best.index;
}

void ParamModelerPick3D::updateMarkers( int candidate )
{
  mCandidate = candidate;
  refreshMarkers();
}

void ParamModelerPick3D::refreshMarkers()
{
  if ( !mActive || !mOverlay || !mCanvas->mapSettings() || !mCanvas->cameraController() ) return;
  QPointF source, target;
  double depth;
  const bool hasSource = projectPoint( mSource(), mCanvas, source, depth );
  const bool hasTarget = mCandidate >= 0 && mCandidate < mPoints.size() &&
                        projectPoint( mPoints[mCandidate], mCanvas, target, depth );
  mOverlay->update( mCanvas->size(), hasSource ? &source : nullptr, hasTarget ? &target : nullptr );
}

void ParamModelerPick3D::mousePressEvent( QMouseEvent *event )
{
  mPressed = event->button() == Qt::LeftButton;
  mPressPosition = event->pos();
}

void ParamModelerPick3D::mouseMoveEvent( QMouseEvent *event )
{
  if ( !mFinished ) updateMarkers( nearestPoint( event->pos() ) );
}

void ParamModelerPick3D::mouseReleaseEvent( QMouseEvent *event )
{
  if ( mFinished ) return;
  if ( event->button() == Qt::RightButton ) { finish(); return; }
  if ( event->button() != Qt::LeftButton || !mPressed ) return;
  mPressed = false;
  if ( ( event->pos() - mPressPosition ).manhattanLength() > 4 ) return;
  const int candidate = nearestPoint( event->pos() );
  updateMarkers( candidate );
  if ( candidate < 0 )
  {
    QToolTip::showText( event->globalPos(), tr( "No point within 12 pixels." ) );
    return;
  }
  mFinished = true;
  const QgsPoint target = mPoints[candidate];
  QTimer::singleShot( 0, this, [this, target] {
    const auto onPicked = picked;
    const auto onStopped = stopped;
    if ( mActive && mLayer && onPicked ) onPicked( target );
    else if ( onStopped ) onStopped();
  } );
}
