#include "../parammodeler_wireframe_guides.h"

#include <cassert>
#include <cmath>

using namespace ParamModelerWireframeGuides;

int main()
{
  constexpr float pi = 3.14159265358979323846f;
  const auto pointOnRing = []( float radius, float angle, float z ) {
    return QVector3D( radius * std::cos( angle ), radius * std::sin( angle ), z );
  };

  // Cylinder: four vertical guides and four top spokes, without bottom spokes.
  assert( circularMeridianGuide( pointOnRing( 4, 0, 0 ), pointOnRing( 4, 0, 5 ), 0 ) );
  assert( circularMeridianGuide( pointOnRing( 4, pi / 2, 0 ), pointOnRing( 4, pi / 2, 5 ), 0 ) );
  assert( !circularMeridianGuide( pointOnRing( 4, pi / 4, 0 ), pointOnRing( 4, pi / 4, 5 ), 0 ) );
  assert( !circularMeridianGuide( pointOnRing( 4, 0, 0 ), pointOnRing( 4, pi / 32, 5 ), 0 ) );
  assert( circularCapGuide( QVector3D( 0, 0, 5 ), QVector3D( 4, 0, 5 ), 0 ) );
  assert( !circularCapGuide( QVector3D( 0, 0, 0 ), QVector3D( 4, 0, 0 ), 0 ) );

  // Cone, dome and tower: the guide follows changing radius all the way to the apex.
  assert( circularMeridianGuide( pointOnRing( 4, 0, 5 ), QVector3D( 0, 0, 8 ), 0 ) );
  assert( circularMeridianGuide( pointOnRing( 4, pi / 2, 5 ), pointOnRing( 3, pi / 2, 6 ), 0 ) );
  assert( circularMeridianGuide( pointOnRing( 3, pi, 6 ), pointOnRing( 1, pi, 8 ), 0 ) );
  assert( circularMeridianGuide( pointOnRing( 1, 3 * pi / 2, 8 ), QVector3D( 0, 0, 9 ), 0 ) );
  assert( !circularMeridianGuide( QVector3D( 0, 0, 0 ), QVector3D( 4, 0, 1 ), 0 ) );

  // A half-cylinder roof runs along X, not the circular models' Z axis.
  assert( halfCylinderRidgeGuide( QVector3D( 0, 3, 8 ), QVector3D( 10, 3, 8 ), 3, 8 ) );
  assert( !halfCylinderRidgeGuide( QVector3D( 0, 0, 5 ), QVector3D( 10, 0, 5 ), 3, 8 ) );
  assert( !halfCylinderRidgeGuide( QVector3D( 0, 3, 8 ), QVector3D( 0, 4, 7 ), 3, 8 ) );

  // Tower: hide the two rings matching the base radius, not the outer eave outline.
  assert( towerInnerEaveRing( pointOnRing( 8, 0, 1.6f ), pointOnRing( 8, pi / 32, 1.6f ), 8, 1.6, 2 ) );
  assert( towerInnerEaveRing( pointOnRing( 8, 0, 2 ), pointOnRing( 8, pi / 32, 2 ), 8, 1.6, 2 ) );
  assert( !towerInnerEaveRing( pointOnRing( 8.96f, 0, 1.6f ), pointOnRing( 8.96f, pi / 32, 1.6f ), 8, 1.6, 2 ) );
  assert( !towerInnerEaveRing( pointOnRing( 8, 0, 0 ), pointOnRing( 8, pi / 32, 0 ), 8, 1.6, 2 ) );
  return 0;
}
