#ifndef PARAMMODELER_FIT_METRICS_H
#define PARAMMODELER_FIT_METRICS_H

#include "meshdata.h"
#include <QMatrix4x4>
#include <Eigen/Geometry>
#include <unsupported/Eigen/BVH>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ParamModelerFit
{
struct Triangle
{
  Eigen::Vector3d a, b, c;
};

inline Eigen::AlignedBox3d bounding_box( const Triangle &triangle )
{
  Eigen::AlignedBox3d box;
  box.extend( triangle.a );
  box.extend( triangle.b );
  box.extend( triangle.c );
  return box;
}

inline double segmentSquaredDistance( const Eigen::Vector3d &p, const Eigen::Vector3d &a,
                                      const Eigen::Vector3d &b )
{
  const Eigen::Vector3d edge = b - a;
  const double lengthSquared = edge.squaredNorm();
  const double t = lengthSquared > 0.0 ? std::clamp( ( p - a ).dot( edge ) / lengthSquared, 0.0, 1.0 ) : 0.0;
  return ( p - a - t * edge ).squaredNorm();
}

inline double triangleSquaredDistance( const Eigen::Vector3d &p, const Triangle &triangle )
{
  const Eigen::Vector3d ab = triangle.b - triangle.a;
  const Eigen::Vector3d ac = triangle.c - triangle.a;
  const Eigen::Vector3d ap = p - triangle.a;
  const Eigen::Vector3d normal = ab.cross( ac );
  const double areaSquared = normal.squaredNorm();
  if ( areaSquared > 0.0 )
  {
    const double v = ( ac.squaredNorm() * ap.dot( ab ) - ab.dot( ac ) * ap.dot( ac ) ) / areaSquared;
    const double w = ( ab.squaredNorm() * ap.dot( ac ) - ab.dot( ac ) * ap.dot( ab ) ) / areaSquared;
    if ( v >= 0.0 && w >= 0.0 && v + w <= 1.0 )
      return std::pow( ap.dot( normal ), 2 ) / areaSquared;
  }
  return std::min( { segmentSquaredDistance( p, triangle.a, triangle.b ),
                     segmentSquaredDistance( p, triangle.b, triangle.c ),
                     segmentSquaredDistance( p, triangle.c, triangle.a ) } );
}

struct PointQuery
{
  using Scalar = double;
  Eigen::Vector3d point;
  double minimumOnVolume( const Eigen::AlignedBox3d &box ) { return box.squaredExteriorDistance( point ); }
  double minimumOnObject( const Triangle &triangle ) { return triangleSquaredDistance( point, triangle ); }
};

struct Metrics
{
  int count = 0;
  double rmse = 0.0;
  double p95 = 0.0;
};

inline Eigen::Vector3d vector( const QVector3D &point )
{
  return Eigen::Vector3d( point.x(), point.y(), point.z() );
}

inline Metrics evaluate( const MeshData &mesh, const QVector<QVector3D> &points,
                         const QMatrix4x4 &modelToWorld )
{
  Metrics result;
  if ( mesh.isEmpty() || points.isEmpty() )
    return result;

  std::vector<Triangle> triangles;
  triangles.reserve( mesh.indices.size() / 3 );
  for ( int i = 0; i + 2 < mesh.indices.size(); i += 3 )
  {
    const Triangle triangle { vector( modelToWorld.map( mesh.vertices[mesh.indices[i]] ) ),
                              vector( modelToWorld.map( mesh.vertices[mesh.indices[i + 1]] ) ),
                              vector( modelToWorld.map( mesh.vertices[mesh.indices[i + 2]] ) ) };
    if ( triangle.a.allFinite() && triangle.b.allFinite() && triangle.c.allFinite() )
      triangles.push_back( triangle );
  }
  if ( triangles.empty() )
    return result;

  // Eigen's BVH avoids testing every cloud point against every mesh triangle.
  const Eigen::KdBVH<double, 3, Triangle> tree( triangles.begin(), triangles.end() );
  std::vector<double> distances;
  distances.reserve( points.size() );
  double sumSquared = 0.0;
  for ( const QVector3D &point : points )
  {
    PointQuery query { vector( point ) };
    if ( !query.point.allFinite() )
      continue;
    const double squared = Eigen::BVMinimize( tree, query );
    sumSquared += squared;
    distances.push_back( std::sqrt( squared ) );
  }
  result.count = static_cast<int>( distances.size() );
  if ( result.count )
  {
    result.rmse = std::sqrt( sumSquared / result.count );
    const int index = static_cast<int>( std::ceil( result.count * 0.95 ) ) - 1;
    std::nth_element( distances.begin(), distances.begin() + index, distances.end() );
    result.p95 = distances[index];
  }
  return result;
}
}

#endif
