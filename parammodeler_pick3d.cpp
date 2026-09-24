#include "parammodeler_pick3d.h"
#include <QCoreApplication>
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
#include <QDialog>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include "qgsmessagelog.h"
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

QString fitFailureMessage( const ParamModelerCornerFit::FitDiagnostics &diagnostics )
{
  using ParamModelerCornerFit::FitFailure;
  switch ( diagnostics.failure )
  {
    case FitFailure::InvalidInput:
      return QCoreApplication::translate( "ParamModelerPick3D", "Invalid selected point or neighborhood radius." );
    case FitFailure::TooFewNearbyPoints:
      return QCoreApplication::translate( "ParamModelerPick3D",
        "Nearby point-cloud points are too few (%1). Increase the radius or click a denser part of the face." )
        .arg( diagnostics.samples );
    case FitFailure::NoPlanarConsensus:
      return QCoreApplication::translate( "ParamModelerPick3D",
        "Nearby points do not form one clear face (%1/%2 inliers). Click farther inside the wall/top, or reduce the radius if it crossed an edge." )
        .arg( diagnostics.inliers ).arg( diagnostics.samples );
    case FitFailure::LineLikePatch:
      return QCoreApplication::translate( "ParamModelerPick3D",
        "The selected neighborhood looks like an edge/line, not a surface. Click inside the face, away from the corner or boundary." );
    case FitFailure::TooNoisy:
      return QCoreApplication::translate( "ParamModelerPick3D",
        "The fitted face is too noisy (RMS %1). Click a flatter patch or reduce the radius." )
        .arg( diagnostics.rms, 0, 'f', 4 );
    case FitFailure::SeedOffPlane:
      return QCoreApplication::translate( "ParamModelerPick3D",
        "The clicked point is not on the fitted face, likely near an edge/corner or sparse hole. Click inside the visible face." );
    case FitFailure::None:
      break;
  }
  return QCoreApplication::translate( "ParamModelerPick3D", "No reliable plane. Select inside the face or adjust the radius." );
}

QString fitQualityMessage( const ParamModelerCornerFit::Plane &plane, bool weak )
{
  if ( weak )
    return QCoreApplication::translate( "ParamModelerPick3D",
      "Weak candidate face accepted (orange cross): %1/%2 inliers, RMS %3. You can continue, or press Back and re-click a flatter interior point." )
      .arg( plane.inliers ).arg( plane.samples ).arg( plane.rms, 0, 'f', 4 );
  return QCoreApplication::translate( "ParamModelerPick3D",
    "Candidate face accepted (cyan cross): %1/%2 inliers, RMS %3." )
    .arg( plane.inliers ).arg( plane.samples ).arg( plane.rms, 0, 'f', 4 );
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
  delete mFitPanel.data();
  mCanvas->removeEventFilter( this );
}

void ParamModelerPick3D::enableCornerFit( double radius, const QVector3D &up, QWidget *panelParent )
{
  mPanelParent = panelParent;
  mCornerFit = true;
  mFitRadius = std::max( 0.001, radius );
  mUp = ParamModelerCornerFit::Point( up.x(), up.y(), up.z() ).normalized();
  mFitPoints.reserve( mPoints.size() );
  for ( const QgsPoint &p : mPoints ) mFitPoints.emplace_back( p.x(), p.y(), p.z() );
}

void ParamModelerPick3D::activate()
{
  mActive = true;
  mCameraWasEnabled = mCanvas->cameraController()->isEnabled();
  mCanvas->cameraController()->setEnabled( false );
  mCanvas->installEventFilter( this );
  mOverlay = std::make_unique<ParamModelerPickOverlay>( mCanvas->engine()->frameGraph()->rubberBandsRootEntity() );
  updateMarkers( -1 );
  if ( mCornerFit )
  {
    auto *panel = new QDialog( mPanelParent.data(), Qt::Tool );
    mFitPanel = panel;
    panel->setWindowTitle( QCoreApplication::translate( "ParamModelerPick3D", "Fit corner" ) );
    panel->setMinimumWidth( 320 );
    auto *layout = new QVBoxLayout( panel );
    auto *form = new QFormLayout;
    auto *radius = new QDoubleSpinBox( panel );
    radius->setDecimals( 3 );
    radius->setRange( 0.001, 1000000 );
    radius->setValue( mFitRadius );
    radius->setSingleStep( std::max( 0.001, mFitRadius * 0.1 ) );
    radius->setKeyboardTracking( false );
    radius->setToolTip( QCoreApplication::translate( "ParamModelerPick3D", "Radius in point-cloud coordinate units. Changing it clears selected faces." ) );
    form->addRow( QCoreApplication::translate( "ParamModelerPick3D", "Neighborhood radius:" ), radius );
    layout->addLayout( form );
    mFitStatus = new QLabel( panel );
    mFitStatus->setWordWrap( true );
    mFitStatus->setMinimumHeight( 90 );
    layout->addWidget( mFitStatus );
    auto *navigate = new QCheckBox( QCoreApplication::translate( "ParamModelerPick3D", "Navigate camera" ), panel );
    layout->addWidget( navigate );
    auto *buttons = new QHBoxLayout;
    mBackFit = new QPushButton( QCoreApplication::translate( "ParamModelerPick3D", "Back" ), panel );
    mApplyFit = new QPushButton( QCoreApplication::translate( "ParamModelerPick3D", "Apply alignment" ), panel );
    auto *cancel = new QPushButton( QCoreApplication::translate( "ParamModelerPick3D", "Cancel" ), panel );
    for ( auto *button : { mBackFit, mApplyFit, cancel } )
    {
      button->setAutoDefault( false );
      buttons->addWidget( button );
    }
    layout->addLayout( buttons );
    connect( radius, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [this]( double value ) {
      mFitRadius = value; mPlanes.clear(); mPlaneWeak.clear(); mHasCandidatePlane = false; mHasCorner = false; updateFitPanel(); refreshMarkers();
    } );
    connect( navigate, &QCheckBox::toggled, this, [this]( bool enabled ) {
      mNavigate = enabled; mPressed = false; updateMarkers( -1 );
      mCanvas->cameraController()->setEnabled( enabled ); updateFitPanel();
    } );
    connect( mBackFit, &QPushButton::clicked, this, [this] {
      if ( !mPlanes.empty() ) mPlanes.pop_back();
      if ( !mPlaneWeak.empty() ) mPlaneWeak.pop_back();
      mHasCandidatePlane = false;
      mHasCorner = false; updateFitPanel(); refreshMarkers();
    } );
    connect( mApplyFit, &QPushButton::clicked, this, [this] {
      if ( mHasCorner && sceneValid() ) applyTarget( mCorner );
    } );
    connect( cancel, &QPushButton::clicked, this, &ParamModelerPick3D::finish );
    connect( panel, &QDialog::finished, this, [this] { finish(); } );
    updateFitPanel();
    panel->show();
  }
}

void ParamModelerPick3D::deactivate()
{
  mActive = false;
  mCanvas->removeEventFilter( this );
  mOverlay.reset();
  if ( mFitPanel )
  {
    const QSignalBlocker blocker( mFitPanel.data() );
    mFitPanel->hide();
  }
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

bool ParamModelerPick3D::sceneValid() const
{
  return mLayer && mCanvas->mapSettings() && mCanvas->cameraController() &&
    mCanvas->mapSettings()->crs() == mLayer->crs() &&
    mCanvas->mapSettings()->sceneMode() == Qgis::SceneMode::Local;
}

int ParamModelerPick3D::nearestPoint( const QPoint &position ) const
{
  if ( !sceneValid() ) return -1;

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
  if ( !sceneValid() ) { finish(); return; }
  QPointF source, target;
  double depth;
  const bool hasSource = projectPoint( mSource(), mCanvas, source, depth );
  const bool hasTarget = mHasCorner ? projectPoint( mCorner, mCanvas, target, depth ) :
    ( mCandidate >= 0 && mCandidate < mPoints.size() && projectPoint( mPoints[mCandidate], mCanvas, target, depth ) );
  QVector<QPointF> faces;
  QVector<QPointF> weakFaces;
  for ( size_t i = 0; i < mPlanes.size(); ++i )
  {
    QPointF screen;
    const auto &plane = mPlanes[i];
    if ( projectPoint( QgsPoint( plane.center.x(), plane.center.y(), plane.center.z() ), mCanvas, screen, depth ) )
    {
      if ( i < mPlaneWeak.size() && mPlaneWeak[i] ) weakFaces.append( screen );
      else faces.append( screen );
    }
  }
  if ( mHasCandidatePlane )
  {
    QPointF screen;
    if ( projectPoint( QgsPoint( mCandidatePlane.center.x(), mCandidatePlane.center.y(), mCandidatePlane.center.z() ), mCanvas, screen, depth ) )
    {
      if ( mCandidatePlaneWeak ) weakFaces.append( screen );
      else faces.append( screen );
    }
  }
  mOverlay->update( mCanvas->size(), hasSource ? &source : nullptr, hasTarget ? &target : nullptr, faces, weakFaces );
}

void ParamModelerPick3D::mousePressEvent( QMouseEvent *event )
{
  mPressed = !mNavigate && event->button() == Qt::LeftButton;
  mPressPosition = event->pos();
}

void ParamModelerPick3D::mouseMoveEvent( QMouseEvent *event )
{
  if ( !mFinished && !mNavigate && !mHasCorner ) updateMarkers( nearestPoint( event->pos() ) );
}

void ParamModelerPick3D::mouseReleaseEvent( QMouseEvent *event )
{
  if ( mFinished ) return;
  if ( mNavigate ) return;
  if ( event->button() == Qt::RightButton ) { finish(); return; }
  if ( event->button() != Qt::LeftButton || !mPressed ) return;
  mPressed = false;
  if ( ( event->pos() - mPressPosition ).manhattanLength() > 4 ) return;
  if ( mHasCorner ) return;
  const int candidate = nearestPoint( event->pos() );
  updateMarkers( candidate );
  if ( candidate < 0 )
  {
    const QString message = QCoreApplication::translate( "ParamModelerPick3D",
      "No point-cloud point within 12 pixels. Zoom in and click directly on a visible point." );
    if ( mCornerFit ) updateFitPanel( message );
    QToolTip::showText( event->globalPos(), message );
    return;
  }
  if ( mCornerFit ) { selectFace( candidate, event->globalPos() ); return; }
  applyTarget( mPoints[candidate] );
}

void ParamModelerPick3D::applyTarget( const QgsPoint &target )
{
  if ( mFinished || !mActive || !sceneValid() ) return;
  mFinished = true;
  QTimer::singleShot( 0, this, [this, target] {
    const auto onPicked = picked;
    const auto onStopped = stopped;
    if ( mActive && sceneValid() && onPicked ) onPicked( target );
    else if ( onStopped ) onStopped();
  } );
}

void ParamModelerPick3D::updateFitPanel( const QString &error )
{
  if ( !mFitPanel ) return;
  QString status;
  if ( mHasCorner )
    status = QCoreApplication::translate( "ParamModelerPick3D", "Virtual corner ready (yellow square).\nX: %1  Y: %2  Z: %3" )
      .arg( mCorner.x(), 0, 'f', 3 ).arg( mCorner.y(), 0, 'f', 3 ).arg( mCorner.z(), 0, 'f', 3 );
  else if ( mPlanes.empty() ) status = QCoreApplication::translate( "ParamModelerPick3D", "1/3: Select the first wall near the corner." );
  else if ( mPlanes.size() == 1 ) status = QCoreApplication::translate( "ParamModelerPick3D", "2/3: Select the adjacent wall near the same corner." );
  else status = QCoreApplication::translate( "ParamModelerPick3D", "3/3: Select the flat top near the same corner." );
  if ( mNavigate ) status = QCoreApplication::translate( "ParamModelerPick3D", "Camera navigation active; face picking paused." ) + QLatin1Char( '\n' ) + status;
  if ( !error.isEmpty() ) status = error + QLatin1Char( '\n' ) + status;
  mFitStatus->setText( status );
  mApplyFit->setEnabled( mHasCorner && !mFinished );
  mBackFit->setEnabled( !mPlanes.empty() && !mFinished );
}

void ParamModelerPick3D::selectFace( int candidate, const QPoint &globalPosition )
{
  using namespace ParamModelerCornerFit;
  Plane plane;
  FitDiagnostics diagnostics;
  mHasCandidatePlane = false;
  if ( !fitPlane( mFitPoints, mFitPoints[candidate], mFitRadius, plane, &diagnostics ) )
  {
    const QString message = fitFailureMessage( diagnostics );
    updateFitPanel( message );
    QToolTip::showText( globalPosition, message );
    return;
  }
  const bool weak = diagnostics.weak;
  mCandidatePlane = plane;
  mCandidatePlaneWeak = true;
  if ( ( mPlanes.size() < 2 && !isWall( plane, mUp ) ) || ( mPlanes.size() == 2 && !isTop( plane, mUp ) ) )
  {
    mHasCandidatePlane = true;
    const QString expected = mPlanes.size() < 2
      ? QCoreApplication::translate( "ParamModelerPick3D", "a vertical wall" )
      : QCoreApplication::translate( "ParamModelerPick3D", "the flat top" );
    const QString message = QCoreApplication::translate( "ParamModelerPick3D",
      "Wrong face direction: this step expects %1. Re-click the correct face near the same corner." ).arg( expected );
    updateFitPanel( message );
    QToolTip::showText( globalPosition, message );
    return;
  }
  if ( mPlanes.size() == 1 && std::abs( plane.normal.dot( mPlanes[0].normal ) ) > 0.65 )
  {
    mHasCandidatePlane = true;
    const QString message = QCoreApplication::translate( "ParamModelerPick3D",
      "The second wall is still too parallel to the first one (orange cross shows the candidate). Select the adjacent wall farther around the corner." );
    updateFitPanel( message );
    QToolTip::showText( globalPosition, message );
    return;
  }
  if ( mPlanes.size() == 2 )
  {
    Point corner;
    if ( !intersect( { mPlanes[0], mPlanes[1], plane }, corner ) )
    {
      mHasCandidatePlane = true;
      const QString message = QCoreApplication::translate( "ParamModelerPick3D",
        "The three faces do not meet in a stable nearby corner (orange cross shows the last candidate). Go back or select faces closer to the same corner." );
      updateFitPanel( message );
      QToolTip::showText( globalPosition, message );
      return;
    }
    mCorner = QgsPoint( corner.x(), corner.y(), corner.z() );
    mHasCorner = true;
  }
  mHasCandidatePlane = false;
  mPlanes.push_back( plane );
  mPlaneWeak.push_back( weak );
  QgsMessageLog::logMessage( QStringLiteral( "[CornerFit3D] face=%1 inliers=%2/%3 radius=%4 rms=%5 weak=%6" )
    .arg( mPlanes.size() ).arg( plane.inliers ).arg( plane.samples ).arg( plane.radius ).arg( plane.rms )
    .arg( weak ? 1 : 0 ),
    QStringLiteral( "ParamModeler" ), Qgis::MessageLevel::Info );
  updateFitPanel( fitQualityMessage( plane, weak ) );
  refreshMarkers();
}
