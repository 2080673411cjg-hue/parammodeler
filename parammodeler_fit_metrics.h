#ifndef PARAMMODELER_FIT_METRICS_H
#define PARAMMODELER_FIT_METRICS_H

#include "meshdata.h"
#include <QMatrix4x4>
#include <Eigen/Geometry>
#include <unsupported/Eigen/BVH>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
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
  int surfaceCount = 0;
  int outsideCount = 0;
  int insideCount = 0;
  bool solidAvailable = false;
  double percentage( int number ) const { return count ? 100.0 * number / count : 0.0; }
};

// MeshData duplicates vertices per face. Weld them and split T-junction edges
// for the closure check, without changing the rendering mesh or its triangles.
// Peeling open patches removes the tower's internal, open eave annulus while
// preserving its closed exterior. A genuinely open shell cannot give inside/outside.
inline bool closedShell( std::vector<Triangle> &triangles )
{
  using Edge = std::pair<int, int>;
  Eigen::AlignedBox3d bounds;
  for ( const auto &triangle : triangles )
    for ( const auto &v : { triangle.a, triangle.b, triangle.c } ) bounds.extend( v );
  const double epsilon = std::max( bounds.diagonal().norm() * 5e-7, 1e-9 );
  if ( ( bounds.sizes().array() <= epsilon ).any() ) return false;
  std::map<std::array<long long, 3>, int> vertexIds;
  std::vector<Eigen::Vector3d> vertices;
  auto vertexId = [&]( const Eigen::Vector3d &v ) {
    std::array<long long, 3> key;
    for ( int axis = 0; axis < 3; ++axis ) key[axis] = std::llround( ( v[axis] - bounds.min()[axis] ) / epsilon );
    const auto inserted = vertexIds.emplace( key, static_cast<int>( vertices.size() ) );
    if ( inserted.second ) vertices.push_back( v );
    return inserted.first->second;
  };
  auto edge = []( int a, int b ) { return Edge( std::min( a, b ), std::max( a, b ) ); };
  std::vector<std::array<int, 3>> faces;
  std::vector<Triangle> usable;
  std::map<Edge, int> counts;
  for ( const auto &triangle : triangles )
  {
    const std::array<int, 3> ids { vertexId( triangle.a ), vertexId( triangle.b ), vertexId( triangle.c ) };
    if ( ids[0] == ids[1] || ids[1] == ids[2] || ids[2] == ids[0] ||
         ( triangle.b - triangle.a ).cross( triangle.c - triangle.a ).squaredNorm() <= std::pow( epsilon, 4 ) ) continue;
    faces.push_back( ids );
    usable.push_back( triangle );
    for ( int i = 0; i < 3; ++i ) ++counts[edge( ids[i], ids[( i + 1 ) % 3] )];
  }
  std::vector<std::vector<Edge>> faceEdges( faces.size() );
  std::map<Edge, std::vector<int>> owners;
  for ( int face = 0; face < static_cast<int>( faces.size() ); ++face )
    for ( int i = 0; i < 3; ++i )
    {
      const int a = faces[face][i], b = faces[face][( i + 1 ) % 3];
      std::vector<std::pair<double, int>> cuts { { 0, a }, { 1, b } };
      if ( counts[edge( a, b )] != 2 )
      {
        const Eigen::Vector3d direction = vertices[b] - vertices[a];
        const double lengthSquared = direction.squaredNorm();
        for ( int v = 0; v < static_cast<int>( vertices.size() ); ++v )
        {
          if ( v == a || v == b ) continue;
          const double t = ( vertices[v] - vertices[a] ).dot( direction ) / lengthSquared;
          if ( t > 0 && t < 1 && ( vertices[v] - vertices[a] - t * direction ).norm() <= epsilon )
            cuts.emplace_back( t, v );
        }
      }
      std::sort( cuts.begin(), cuts.end() );
      for ( int part = 1; part < static_cast<int>( cuts.size() ); ++part )
      {
        const Edge segment = edge( cuts[part - 1].second, cuts[part].second );
        faceEdges[face].push_back( segment );
        owners[segment].push_back( face );
      }
    }
  counts.clear();
  std::deque<int> pending;
  for ( const auto &item : owners )
  {
    counts[item.first] = static_cast<int>( item.second.size() );
    if ( item.second.size() == 1 ) pending.push_back( item.second.front() );
  }
  std::vector<bool> active( faces.size(), true );
  while ( !pending.empty() )
  {
    const int face = pending.front(); pending.pop_front();
    if ( !active[face] ) continue;
    active[face] = false;
    for ( const Edge &segment : faceEdges[face] )
      if ( --counts[segment] == 1 )
        for ( const int other : owners[segment] )
          if ( active[other] ) pending.push_back( other );
  }
  for ( const auto &item : counts ) if ( item.second != 0 && item.second != 2 ) return false;
  std::vector<Triangle> shell;
  for ( int i = 0; i < static_cast<int>( active.size() ); ++i ) if ( active[i] ) shell.push_back( usable[i] );
  if ( shell.empty() ) return false;
  triangles.swap( shell );
  return true;
}

struct RayQuery
{
  Eigen::Vector3d origin, direction;
  std::vector<double> hits;
  bool intersectVolume( const Eigen::AlignedBox3d &box )
  {
    double nearT = 0, farT = std::numeric_limits<double>::infinity();
    for ( int axis = 0; axis < 3; ++axis )
    {
      const double a = ( box.min()[axis] - origin[axis] ) / direction[axis];
      const double b = ( box.max()[axis] - origin[axis] ) / direction[axis];
      nearT = std::max( nearT, std::min( a, b ) );
      farT = std::min( farT, std::max( a, b ) );
    }
    return farT >= nearT;
  }
  bool intersectObject( const Triangle &triangle )
  {
    const Eigen::Vector3d ab = triangle.b - triangle.a, ac = triangle.c - triangle.a;
    const Eigen::Vector3d cross = direction.cross( ac );
    const double determinant = ab.dot( cross );
    if ( std::abs( determinant ) <= 1e-12 * ab.norm() * ac.norm() ) return false;
    const Eigen::Vector3d offset = origin - triangle.a;
    const double u = offset.dot( cross ) / determinant;
    const Eigen::Vector3d q = offset.cross( ab );
    const double v = direction.dot( q ) / determinant;
    const double t = ac.dot( q ) / determinant;
    if ( u >= -1e-10 && v >= -1e-10 && u + v <= 1 + 1e-10 && t > 0 ) hits.push_back( t );
    return false; // Visit all intersections, rather than stop at the first hit.
  }
};

inline bool inside( const Eigen::KdBVH<double, 3, Triangle> &tree, const Eigen::Vector3d &point,
                    const Eigen::AlignedBox3d &bounds )
{
  if ( !bounds.contains( point ) ) return false;
  const std::array<Eigen::Vector3d, 3> directions { Eigen::Vector3d( 1, .371, .529 ).normalized(),
    Eigen::Vector3d( -.419, 1, .613 ).normalized(), Eigen::Vector3d( .283, -.457, 1 ).normalized() };
  const double epsilon = std::max( bounds.diagonal().norm() * 1e-8, 1e-10 );
  int votes = 0;
  for ( int ray = 0; ray < 3; ++ray )
  {
    RayQuery query { point, directions[ray], {} };
    Eigen::BVIntersect( tree, query );
    std::sort( query.hits.begin(), query.hits.end() );
    int crossings = 0;
    double previous = -std::numeric_limits<double>::infinity();
    for ( const double t : query.hits )
      if ( t - previous > epsilon ) { ++crossings; previous = t; }
    votes += crossings % 2;
    if ( ray == 1 && ( votes == 0 || votes == 2 ) ) return votes == 2;
  }
  return votes >= 2;
}

inline Eigen::Vector3d vector( const QVector3D &point )
{
  return Eigen::Vector3d( point.x(), point.y(), point.z() );
}

inline Metrics evaluate( const MeshData &mesh, const QVector<QVector3D> &points,
                         const QMatrix4x4 &modelToWorld, double tolerance = -1.0 )
{
  Metrics result;
  if ( mesh.isEmpty() || points.isEmpty() )
    return result;

  std::vector<Triangle> triangles;
  triangles.reserve( mesh.indices.size() / 3 );
  for ( int i = 0; i + 2 < mesh.indices.size(); i += 3 )
  {
    const Triangle triangle { vector( mesh.vertices[mesh.indices[i]] ),
                              vector( mesh.vertices[mesh.indices[i + 1]] ),
                              vector( mesh.vertices[mesh.indices[i + 2]] ) };
    if ( triangle.a.allFinite() && triangle.b.allFinite() && triangle.c.allFinite() )
      triangles.push_back( triangle );
  }
  if ( triangles.empty() )
    return result;

  // Three-argument callers retain the original unsigned-distance evaluation.
  if ( tolerance >= 0 && std::isfinite( tolerance ) ) result.solidAvailable = closedShell( triangles );
  Eigen::AlignedBox3d bounds;
  for ( auto &triangle : triangles )
  {
    for ( Eigen::Vector3d *v : { &triangle.a, &triangle.b, &triangle.c } )
    {
      *v = vector( modelToWorld.map( QVector3D( v->x(), v->y(), v->z() ) ) );
      bounds.extend( *v );
    }
  }

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
    if ( tolerance >= 0 && squared <= tolerance * tolerance ) ++result.surfaceCount;
    else if ( result.solidAvailable )
    {
      if ( inside( tree, query.point, bounds ) ) ++result.insideCount;
      else ++result.outsideCount;
    }
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
