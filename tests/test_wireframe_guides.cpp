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

  // Cylinder: eight vertical guides and eight top spokes, without bottom spokes.
  assert( circularMeridianGuide( pointOnRing( 4, 0, 0 ), pointOnRing( 4, 0, 5 ), 0 ) );
  assert( circularMeridianGuide( pointOnRing( 4, pi / 2, 0 ), pointOnRing( 4, pi / 2, 5 ), 0 ) );
  assert( circularMeridianGuide( pointOnRing( 4, pi / 4, 0 ), pointOnRing( 4, pi / 4, 5 ), 0 ) );
  assert( !circularMeridianGuide( pointOnRing( 4, pi / 8, 0 ), pointOnRing( 4, pi / 8, 5 ), 0 ) );
  int meridians = 0;
  for ( int i = 0; i < 64; ++i )
    meridians += circularMeridianGuide( pointOnRing( 4, 2 * pi * i / 64, 0 ),
                                       pointOnRing( 4, 2 * pi * i / 64, 5 ), 0 );
  assert( meridians == 8 );
  assert( !circularMeridianGuide( pointOnRing( 4, 0, 0 ), pointOnRing( 4, pi / 32, 5 ), 0 ) );
  assert( circularCapGuide( QVector3D( 0, 0, 5 ), QVector3D( 4, 0, 5 ), 0 ) );
  assert( !circularCapGuide( QVector3D( 0, 0, 0 ), QVector3D( 4, 0, 0 ), 0 ) );
  assert( circularRingGuide( pointOnRing( 4, 0, 5 ), pointOnRing( 4, pi / 32, 5 ), 4, 5 ) );
  assert( !circularRingGuide( pointOnRing( 4, 0, 6 ), pointOnRing( 3, pi / 32, 6 ), 4, 5 ) );
  assert( !circularRingGuide( QVector3D( 0, 0, 5 ), QVector3D( 4, 0, 5 ), 4, 5 ) );
  assert( !domeInteriorRing( pointOnRing( 4, 0, 5 ), pointOnRing( 4, pi / 32, 5 ), 5 ) );
  assert( domeInteriorRing( pointOnRing( 3.9f, 0, 5.1f ), pointOnRing( 3.9f, pi / 32, 5.1f ), 5 ) );
  assert( !domeInteriorRing( pointOnRing( 4, 0, 5 ), pointOnRing( 3.9f, 0, 5.1f ), 5 ) );

  // Cone, dome and tower: the guide follows changing radius all the way to the apex.
  assert( circularMeridianGuide( pointOnRing( 4, 0, 5 ), QVector3D( 0, 0, 8 ), 0 ) );
  assert( circularMeridianGuide( pointOnRing( 4, pi / 2, 5 ), pointOnRing( 3, pi / 2, 6 ), 0 ) );
  assert( circularMeridianGuide( pointOnRing( 3, pi, 6 ), pointOnRing( 1, pi, 8 ), 0 ) );
  assert( circularMeridianGuide( pointOnRing( 1, 3 * pi / 2, 8 ), QVector3D( 0, 0, 9 ), 0 ) );
  assert( !circularMeridianGuide( QVector3D( 0, 0, 0 ), QVector3D( 4, 0, 1 ), 0 ) );

  const QVector3D joint( 14, 3, 7 );
  assert( joinedGableRoofGuide( QVector3D( 14, 0, 4 ), joint, 0, 7 ) );
  assert( joinedGableRoofGuide( joint, QVector3D( 14, 8, 4 ), 0, 7 ) );
  assert( joinedGableRoofGuide( QVector3D( 0, 3, 7 ), joint, 0, 7 ) );
  assert( joinedGableRoofGuide( joint, QVector3D( 24, 6, 7 ), 0, 7 ) );
  assert( !joinedGableRoofGuide( QVector3D( 0, 0, 4 ), joint, 0, 7 ) );
  assert( !joinedGableRoofGuide( QVector3D( 14, 0, 0 ), joint, 0, 7 ) );

  // A half-cylinder roof runs along X, not the circular models' Z axis.
  assert( halfCylinderRidgeGuide( QVector3D( 0, 3, 8 ), QVector3D( 10, 3, 8 ), 3, 8 ) );
  assert( !halfCylinderRidgeGuide( QVector3D( 0, 0, 5 ), QVector3D( 10, 0, 5 ), 3, 8 ) );
  assert( !halfCylinderRidgeGuide( QVector3D( 0, 3, 8 ), QVector3D( 0, 4, 7 ), 3, 8 ) );

  // Preserve both longitudinal wall-top edges, excluding base and diagonals.
  assert( halfCylinderEaveGuide( QVector3D( 0, 0, 5 ), QVector3D( 10, 0, 5 ), 0, 6, 8 ) );
  assert( halfCylinderEaveGuide( QVector3D( 10, 6, 5 ), QVector3D( 0, 6, 5 ), 0, 6, 8 ) );
  assert( !halfCylinderEaveGuide( QVector3D( 0, 0, 0 ), QVector3D( 10, 0, 0 ), 0, 6, 8 ) );
  assert( !halfCylinderEaveGuide( QVector3D( 0, 0, 5 ), QVector3D( 10, 6, 5 ), 0, 6, 8 ) );
  assert( !halfCylinderEaveGuide( QVector3D( 0, 3, 8 ), QVector3D( 10, 3, 8 ), 0, 6, 8 ) );

  // Tower: hide the two rings matching the base radius, not the outer eave outline.
  int roofLines = 0, endSpokes = 0, connectors = 0;
  for ( int i = 0; i <= 32; ++i )
  {
    const double angle = pi * i / 32;
    const QVector3D rim( 0, 3 + 3 * std::cos( angle ), 5 + 3 * std::sin( angle ) );
    roofLines += halfCylinderArcGuide( rim, rim + QVector3D( 10, 0, 0 ), 0, 6, 8 );
    endSpokes += halfCylinderArcGuide( QVector3D( 0, 3, 5 ), rim, 0, 6, 8 );
  }
  assert( roofLines == 5 && endSpokes == 5 );
  assert( !halfCylinderArcGuide( QVector3D( 0, 0, 5 ), QVector3D( 10, 3, 8 ), 0, 6, 8 ) );
  for ( int i = 0; i < 64; ++i )
  {
    const float angle = 2 * pi * i / 64;
    connectors += towerEaveConnector( pointOnRing( 8, angle, 1.6f ),
                                      pointOnRing( 8.96f, angle, 1.6f ), 8, 1.6, 8.96 );
  }
  assert( connectors == 8 );
  assert( !towerEaveConnector( pointOnRing( 8, 0, 1.6f ), pointOnRing( 8.96f, pi / 32, 1.6f ), 8, 1.6, 8.96 ) );
  assert( !towerEaveConnector( pointOnRing( 8, 0, 2 ), pointOnRing( 8.96f, 0, 2 ), 8, 1.6, 8.96 ) );
  assert( towerEaveConnector( pointOnRing( 8, 0, 1.6f ), pointOnRing( 9.6f, 0, 1.6f ), 8, 1.6, 9.6 ) );
  assert( towerInnerEaveRing( pointOnRing( 8, 0, 1.6f ), pointOnRing( 8, pi / 32, 1.6f ), 8, 1.6, 2 ) );
  assert( towerInnerEaveRing( pointOnRing( 8, 0, 2 ), pointOnRing( 8, pi / 32, 2 ), 8, 1.6, 2 ) );
  assert( !towerInnerEaveRing( pointOnRing( 8.96f, 0, 1.6f ), pointOnRing( 8.96f, pi / 32, 1.6f ), 8, 1.6, 2 ) );
  assert( !towerInnerEaveRing( pointOnRing( 8, 0, 0 ), pointOnRing( 8, pi / 32, 0 ), 8, 1.6, 2 ) );
  return 0;
}
