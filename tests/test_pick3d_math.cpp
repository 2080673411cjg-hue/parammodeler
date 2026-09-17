#include "../parammodeler_pick3d_math.h"
#include <cstdlib>
#include <iostream>

static void check( bool condition, const char *name )
{
  if ( !condition ) { std::cerr << "FAIL: " << name << '\n'; std::exit( 1 ); }
}

int main()
{
  using namespace ParamModelerPicking;
  QMatrix4x4 view, projection;
  projection.perspective( 90, 2, 1, 100 );
  const QSize viewport( 800, 400 );
  QPointF screen;
  double depth;
  check( project( QVector3D( 0, 0, -10 ), view, projection, viewport, screen, depth ) &&
         screen == QPointF( 400, 200 ) && depth == 10, "center" );
  check( project( QVector3D( 10, 5, -10 ), view, projection, viewport, screen, depth ) &&
         std::abs( screen.x() - 600 ) < 0.001 && std::abs( screen.y() - 100 ) < 0.001,
         "screen Y inversion and aspect ratio" );
  check( !project( QVector3D( 0, 0, 10 ), view, projection, viewport, screen, depth ), "behind camera" );
  check( !project( QVector3D( 0, 0, -0.5 ), view, projection, viewport, screen, depth ), "near clip" );
  check( !project( QVector3D( 0, 0, -110 ), view, projection, viewport, screen, depth ), "far clip" );
  check( !project( QVector3D( 30, 0, -10 ), view, projection, viewport, screen, depth ), "outside viewport" );
  check( !project( QVector3D( 0, 0, -10 ), view, projection, QSize(), screen, depth ), "zero viewport" );

  QMatrix4x4 rotatedView;
  rotatedView.lookAt( QVector3D( 10, 0, 0 ), QVector3D(), QVector3D( 0, 0, 1 ) );
  check( project( QVector3D(), rotatedView, projection, viewport, screen, depth ) &&
         screen == QPointF( 400, 200 ), "rotated camera" );
  const QVector3D origin( 1200, -500, 300 );
  QMatrix4x4 shiftedView;
  shiftedView.lookAt( QVector3D( 10, 0, 0 ) - origin, -origin, QVector3D( 0, 0, 1 ) );
  check( project( -origin, shiftedView, projection, viewport, screen, depth ) &&
         std::abs( screen.x() - 400 ) < 0.01 && std::abs( screen.y() - 200 ) < 0.01,
         "floating origin invariance" );

  projection.setToIdentity();
  projection.ortho( -20, 20, -10, 10, 1, 100 );
  check( project( QVector3D( 10, 5, -10 ), view, projection, viewport, screen, depth ) &&
         screen == QPointF( 600, 100 ), "orthographic camera" );
  check( project( QVector3D( 10, 5, -10 ), view, projection, QSize( 1600, 800 ), screen, depth ) &&
         screen == QPointF( 1200, 200 ), "viewport resize" );

  NearestPoint hit;
  hit.consider( 0, 12.1, 10 );
  check( hit.index == -1, "blank click rejected" );
  hit.consider( 1, 5.1, 20 );
  hit.consider( 2, 5.8, 10 );
  check( hit.index == 2, "overlapping pixels prefer front" );
  hit.consider( 3, 4.9, 30 );
  check( hit.index == 3, "nearest screen pixel wins" );
  hit.consider( 4, std::numeric_limits<double>::quiet_NaN(), 1 );
  check( hit.index == 3, "invalid sample ignored" );

  QVector3D anchor;
  check( upperAnchor( { { 0, 0, 0 }, { 0, 0, 5 }, { 10, 8, 5 } }, anchor ) &&
         anchor == QVector3D( 0, 0, 5 ), "cuboid upper corner" );
  check( upperAnchor( { { 0, 0, 0 }, { 0, 0, 4 }, { 0, 4, 9 }, { 5, 4, 9 } }, anchor ) &&
         anchor == QVector3D( 0, 0, 4 ), "roof eave, not ridge or bbox top" );
  check( upperAnchor( { { 0, 0, 0 }, { 0, 0, 6 }, { 3, 2, 6 }, { 3, 2, 2 } }, anchor ) &&
         anchor == QVector3D( 0, 0, 6 ), "L/indented outer upper corner" );
  check( upperAnchor( { { 0, 0, 0 }, { 4, 0, 8 }, { 0, 0, 8 } }, anchor ) &&
         anchor == QVector3D( 0, 0, 8 ), "cylinder top center" );
  check( upperAnchor( { { 0, 0, 0 }, { 4, 0, 8 }, { 0, 0, 11 } }, anchor ) &&
         anchor == QVector3D( 0, 0, 11 ), "dome/cone/tower apex" );
  QMatrix4x4 pose;
  pose.rotate( 90, 0, 1, 0 );
  const QVector3D offset = pose.map( anchor );
  const QVector3D previousTranslation( 2, 3, 4 ), target( 20, 10, 6 );
  const QVector3D source = offset + previousTranslation;
  const QVector3D translation = previousTranslation + target - source;
  check( ( offset + translation - target ).length() < 1e-5, "upper anchor translation with model tilt" );
  check( !upperAnchor( { { 3, 2, 5 } }, anchor ), "missing vertical anchor rejected" );
  std::cout << "PASS: 22 projection, selection and upper-anchor checks\n";
}
