#include "../buildmesh.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
  FourStageRoundTowerParams p;
  p.eaveRadius = 9;
  p.eaveHeight = .4;
  const MeshData reference = BuildMesh::buildFourStageRoundTower( p );
  for ( const double scale : { .5, 1.0, 2.0 } )
  {
    FourStageRoundTowerParams scaled = p;
    scaled.baseRadius *= scale;
    scaled.baseHeight *= scale;
    scaled.eaveRadius *= scale;
    scaled.eaveHeight *= scale;
    scaled.middleHeight *= scale;
    scaled.middleTopRadius *= scale;
    scaled.middleRise *= scale;
    scaled.coneHeight *= scale;
    const MeshData mesh = BuildMesh::buildFourStageRoundTower( scaled );
    assert( mesh.indices == reference.indices && mesh.vertices.size() == reference.vertices.size() );
    for ( int i = 0; i < mesh.vertices.size(); ++i )
      assert( ( mesh.vertices[i] - reference.vertices[i] * scale ).length() < 1e-5 );
  }
  for ( const double height : { .5, 2.0, 5.0 } )
  {
    p.middleHeight = height;
    for ( const double fraction : { 0.0, .03, .1, .2, .25 } )
    {
      p.middleRise = height * fraction;
      const MeshData mesh = BuildMesh::buildFourStageRoundTower( p );
      assert( !mesh.isEmpty() );
      bool midpoint = false, lower = false, upper = false;
      for ( const QVector3D &v : mesh.vertices )
      {
        const double radius = std::hypot( v.x(), v.y() );
        if ( v.z() < p.baseHeight - 1e-5 || v.z() > p.baseHeight + height + 1e-5 ) continue;
        if ( std::abs( v.z() - p.baseHeight ) < 1e-5 && std::abs( radius - p.eaveRadius ) > 1e-5 ) continue;
        const double t = ( p.eaveRadius - radius ) / ( p.eaveRadius - p.middleTopRadius );
        const double expected = p.baseHeight + height * t + 4 * p.middleRise * t * ( 1 - t );
        assert( std::abs( v.z() - expected ) < 1e-5 );
        assert( v.z() >= p.baseHeight - 1e-5 && v.z() <= p.baseHeight + height + 1e-5 );
        if ( std::abs( t - .5 ) < 1e-6 )
        {
          midpoint = true;
          assert( std::abs( v.z() - p.baseHeight - height * .5 - p.middleRise ) < 1e-5 );
        }
        lower = lower || std::abs( t ) < 1e-6;
        upper = upper || std::abs( t - 1 ) < 1e-6;
      }
      assert( midpoint && lower && upper );
    }
    for ( const double rise : { -.001, height * .25 + .001, std::numeric_limits<double>::quiet_NaN() } )
    {
      p.middleRise = rise;
      assert( BuildMesh::buildFourStageRoundTower( p ).isEmpty() );
    }
  }
  p.middleHeight = 2;
  p.middleRise = .2;
  for ( const double radius : { .01, 5.0, 8.0 } )
  {
    p.middleTopRadius = radius;
    const MeshData mesh = BuildMesh::buildFourStageRoundTower( p );
    assert( !mesh.isEmpty() );
    bool topRing = false;
    for ( const QVector3D &v : mesh.vertices )
      if ( std::abs( v.z() - p.baseHeight - p.middleHeight ) < 1e-5 )
      {
        assert( std::abs( std::hypot( v.x(), v.y() ) - radius ) < 1e-5 );
        topRing = true;
      }
    assert( topRing );
  }
  for ( const double radius : { 0.0, 9.0, 10.0, std::numeric_limits<double>::quiet_NaN() } )
  {
    p.middleTopRadius = radius;
    assert( BuildMesh::buildFourStageRoundTower( p ).isEmpty() );
  }
  std::cout << "PASS: actual tower mesh endpoints, rise and uncapped top radius\n";
}
