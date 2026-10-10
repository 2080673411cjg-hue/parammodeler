#include "../parammodeler_alignment_geometry.h"

#include <cassert>
#include <cmath>

using namespace ParamModelerAlignmentGeometry;

int main()
{
  const QVector<QVector3D> box = {
    QVector3D( 0, 0, 0 ), QVector3D( 10, 0, 0 ),
    QVector3D( 10, 5, 0 ), QVector3D( 0, 5, 0 ),
    QVector3D( 0, 0, 4 ), QVector3D( 10, 0, 4 ),
    QVector3D( 10, 5, 4 ), QVector3D( 0, 5, 4 )
  };
  for ( const float angle : { 0.0f, 45.0f, 90.0f, -45.0f } )
  {
    QMatrix4x4 rotation;
    rotation.rotate( angle, 0, 0, 1 );
    QVector3D offset;
    assert( upperOffsetFromRotatedMesh( box, rotation, QVector3D( 0, 0, 4 ), offset ) );
    QVector3D cloudMin = rotation.map( box.first() ) + QVector3D( 100, 200, 30 );
    for ( const QVector3D &vertex : box )
    {
      const QVector3D p = rotation.map( vertex ) + QVector3D( 100, 200, 30 );
      cloudMin.setX( std::min( cloudMin.x(), p.x() ) );
      cloudMin.setY( std::min( cloudMin.y(), p.y() ) );
      cloudMin.setZ( std::min( cloudMin.z(), p.z() ) );
    }
    const QVector3D target = cloudMin + offset;
    assert( std::abs( target.x() - 100 ) < 0.001 );
    assert( std::abs( target.y() - 200 ) < 0.001 );
    assert( std::abs( target.z() - 34 ) < 0.001 );
  }

  assert( !bottomFootprintAgreesWithFull(
    QVector3D( 2.17f, -2.75f, 0 ), QVector3D( 40.7f, 9.7f, 4 ),
    QVector3D( 0.34f, -0.21f, 0 ), QVector3D( 40.7f, 9.76f, 4 ) ) );
  assert( bottomFootprintAgreesWithFull(
    QVector3D( 0.3f, -0.1f, 0 ), QVector3D( 40.5f, 9.8f, 4 ),
    QVector3D( 0.34f, -0.21f, 0 ), QVector3D( 40.7f, 9.76f, 4 ) ) );

  QVector<QVector3D> flatRoof;
  for ( int i = 0; i < 1000; ++i )
    flatRoof.append( QVector3D( i % 40, i % 10, 3.3f + 0.10f * std::sin( i * 0.37 ) ) );
  for ( int i = 0; i < 600; ++i )
    flatRoof.append( QVector3D( i % 40, 0, 3.3f * ( i % 100 ) / 100.0f ) );
  for ( int i = 0; i < 40; ++i )
    flatRoof.append( QVector3D( i % 40, i % 10, 4.2f ) );
  double top = 0.0;
  assert( supportedFlatRoofTop( flatRoof, top ) );
  assert( std::abs( top - 3.3 ) < 0.05 );

  QVector<QVector3D> houses;
  for ( int x = 0; x <= 14; ++x )
    for ( int z = 0; z <= 4; ++z )
      houses.append( QVector3D( 100 + x, -50, 20 + z ) );
  for ( int x = 0; x <= 14; ++x )
    for ( int y = 1; y <= 8; ++y )
      houses.append( QVector3D( 100 + x, -50 + y,
                                24 + 3.0f * ( 1.0f - std::abs( y - 4 ) / 4.0f ) ) );
  for ( int i = 0; i < 120; ++i )
    houses.append( QVector3D( 115 + i % 10, -42 + i % 8, 27 ) );

  double eave = 0.0;
  assert( firstGableEave( houses, QVector3D( 100, -50, 20 ),
                          QVector3D( 124, -35, 27 ), 20, 7, eave ) );
  assert( std::abs( eave - 24 ) < 0.05 );

  QVector<QVector3D> roof;
  constexpr double pi = 3.14159265358979323846;
  for ( int x = 1; x < 12; ++x )
    for ( int i = 0; i <= 16; ++i )
    {
      const double t = pi * i / 32.0; // One half of the arc is missing.
      roof.append( QVector3D( 100 + x, -46 + 4 * std::cos( t ),
                              24 + 4 * std::sin( t ) ) );
    }
  for ( int x = 0; x <= 12; ++x )
    for ( int z = 0; z <= 4; ++z )
      roof.append( QVector3D( 100 + x, -50, 20 + z ) );
  roof.append( QVector3D( 106, -46, 34 ) );
  roof.append( QVector3D( 107, -46, 17 ) );

  assert( halfCylinderEave( roof, QVector3D( 100, -50, 20 ),
                            QVector3D( 112, -42, 28 ), 20, 8, eave ) );
  assert( std::abs( eave - 24 ) < 0.05 );

  QVector<QVector3D> tallRoof;
  for ( int x = 1; x < 12; ++x )
    for ( int i = 3; i <= 29; ++i )
    {
      const double t = pi * i / 32.0;
      tallRoof.append( QVector3D( 100 + x, -46 + 4 * std::cos( t ),
                                  120 + 4 * std::sin( t ) ) );
    }
  assert( halfCylinderEave( tallRoof, QVector3D( 100, -50, 20 ),
                            QVector3D( 112, -42, 124 ), 20, 104, eave ) );
  assert( std::abs( eave - 120 ) < 0.05 );

  QVector<QVector3D> noRoof;
  for ( int i = 0; i < 60; ++i )
    noRoof.append( QVector3D( 100 + i % 12, -50, 20 + i % 5 ) );
  assert( !halfCylinderEave( noRoof, QVector3D( 100, -50, 20 ),
                             QVector3D( 112, -42, 28 ), 20, 8, eave ) );
  return 0;
}
