#ifndef PARAMMODELER_SURFACE_CLASSIFICATION_H
#define PARAMMODELER_SURFACE_CLASSIFICATION_H

#include <QVector>
#include <QVector3D>
#include <Eigen/Eigenvalues>
#include <unsupported/Eigen/BVH>
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <utility>
#include <vector>

namespace ParamModelerSurfaces
{
// Display-only surface cues in a Z-up frame, independent of model predictions.
enum class Surface : unsigned char { Top, Side, Other };

namespace Detail
{
struct Point
{
  Eigen::Vector3d position;
  int index = -1;
};

inline Eigen::AlignedBox3d bounding_box( const Point &point )
{
  return Eigen::AlignedBox3d( point.position, point.position );
}

using Tree = Eigen::KdBVH<double, 3, Point>;

struct Nearest
{
  const Point &point;
  int count;
  std::priority_queue<std::pair<double, int>> found;

  bool intersectVolume( const Eigen::AlignedBox3d &box ) const
  {
    return static_cast<int>( found.size() ) < count
      || box.squaredExteriorDistance( point.position ) <= found.top().first;
  }

  bool intersectObject( const Point &other )
  {
    if ( other.index == point.index ) return false;
    const std::pair<double, int> candidate { ( other.position - point.position ).squaredNorm(), other.index };
    if ( static_cast<int>( found.size() ) < count ) found.push( candidate );
    else if ( candidate < found.top() )
    {
      found.pop();
      found.push( candidate );
    }
    return false;
  }
};

struct Plane
{
  Eigen::Vector3d normal = Eigen::Vector3d::Zero();
  double noise = 0.0;
  bool stable = false;
};

inline Plane fitPlane( const std::vector<Point> &points, int index,
                       const std::array<int, 18> &neighbors, int count )
{
  Plane plane;
  if ( count < 5 ) return plane;
  const Eigen::Vector3d origin = points[index].position;
  Eigen::Vector3d mean = Eigen::Vector3d::Zero();
  for ( int i = 0; i < count; ++i ) mean += points[neighbors[i]].position - origin;
  mean /= count + 1;
  Eigen::Matrix3d covariance = mean * mean.transpose();
  for ( int i = 0; i < count; ++i )
  {
    const Eigen::Vector3d delta = points[neighbors[i]].position - origin - mean;
    covariance += delta * delta.transpose();
  }
  Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver( covariance );
  if ( solver.info() != Eigen::Success ) return plane;
  const Eigen::Vector3d values = solver.eigenvalues().cwiseMax( 0.0 );
  if ( values[2] <= 0.0 || values[1] <= values[2] * 0.04 ) return plane;
  plane.normal = solver.eigenvectors().col( 0 );
  if ( plane.normal.z() < 0.0 ) plane.normal = -plane.normal;
  plane.noise = std::sqrt( values[0] / ( count + 1 ) );
  plane.stable = values[0] <= values.sum() * 0.08;
  return plane;
}

struct Patch
{
  Plane plane;
  std::array<int, 18> neighbors {};
  int count = 0;
  double radius = 0.0;
  double medianNz = 0.0;
  bool blocked = false;
};

inline double upperLayerGap( const std::vector<Point> &points, int index, const Patch &patch )
{
  if ( patch.plane.normal.z() < 0.8 ) return 0.0;
  std::array<double, 19> offsets {};
  const int count = patch.count + 1;
  for ( int i = 0; i < patch.count; ++i )
    offsets[i + 1] = ( points[patch.neighbors[i]].position - points[index].position ).dot( patch.plane.normal );
  std::sort( offsets.begin(), offsets.begin() + count );
  double total = 0.0, totalSquared = 0.0;
  for ( int i = 0; i < count; ++i )
  {
    total += offsets[i];
    totalSquared += offsets[i] * offsets[i];
  }
  double left = 0.0, leftSquared = 0.0;
  for ( int i = 1; i < count; ++i )
  {
    left += offsets[i - 1];
    leftSquared += offsets[i - 1] * offsets[i - 1];
    if ( i < 4 || count - i < 4 ) continue;
    const double gap = offsets[i] - offsets[i - 1];
    const double lower = left / i, upper = ( total - left ) / ( count - i );
    const double variance = std::max( { 0.0, leftSquared / i - lower * lower,
      ( totalSquared - leftSquared ) / ( count - i ) - upper * upper } );
    // A real gap between two supported layers, not the scatter of one noisy face.
    if ( gap > std::max( patch.radius * 0.015, 4.0 * std::sqrt( variance ) )
         && std::abs( lower ) < std::abs( upper ) && std::abs( lower ) < gap * 0.5 ) return gap;
  }
  return 0.0;
}

struct Overhead
{
  const Point &point;
  const Patch &patch;
  const std::vector<Patch> &patches;
  double radius;
  double layerGap;
  int quadrants = 0;
  int support = 0;

  bool intersectVolume( const Eigen::AlignedBox3d &box ) const
  {
    return box.max().z() > point.position.z()
      && box.min().x() <= point.position.x() + radius && box.max().x() >= point.position.x() - radius
      && box.min().y() <= point.position.y() + radius && box.max().y() >= point.position.y() - radius;
  }

  bool intersectObject( const Point &other )
  {
    const Patch &candidate = patches[other.index];
    if ( !candidate.plane.stable || std::abs( patch.plane.normal.dot( candidate.plane.normal ) ) < 0.94 ) return false;
    const Eigen::Vector3d delta = other.position - point.position;
    if ( delta.head<2>().squaredNorm() > radius * radius || delta.z() <= 0.0 ) return false;
    double tolerance = std::max( patch.radius * 0.03,
                                 2.5 * std::max( patch.plane.noise, candidate.plane.noise ) );
    if ( layerGap > 0.0 ) tolerance = std::min( tolerance, layerGap * 0.5 );
    if ( delta.dot( patch.plane.normal ) <= tolerance ) return false;
    quadrants |= 1 << ( ( delta.x() >= 0.0 ? 1 : 0 ) + ( delta.y() >= 0.0 ? 2 : 0 ) );
    ++support;
    int sectors = 0;
    for ( int i = 0; i < 4; ++i ) if ( quadrants & ( 1 << i ) ) ++sectors;
    return support >= 4 && sectors >= 3;
  }
};
}

inline QVector<Surface> classify( const QVector<QVector3D> &input )
{
  using namespace Detail;
  QVector<Surface> result( input.size(), Surface::Other );
  std::vector<int> order;
  order.reserve( input.size() );
  for ( int i = 0; i < input.size(); ++i )
    if ( std::isfinite( input[i].x() ) && std::isfinite( input[i].y() ) && std::isfinite( input[i].z() ) ) order.push_back( i );
  std::sort( order.begin(), order.end(), [&]( int a, int b ) {
    for ( int axis = 0; axis < 3; ++axis )
      if ( input[a][axis] != input[b][axis] ) return input[a][axis] < input[b][axis];
    return a < b;
  } );

  // Duplicate samples must not change the neighborhood or its normal.
  std::vector<Point> points;
  std::vector<int> originalToUnique( input.size(), -1 );
  for ( int original : order )
  {
    const QVector3D &p = input[original];
    const Eigen::Vector3d position( p.x(), p.y(), p.z() );
    if ( points.empty() || position != points.back().position )
      points.push_back( { position, static_cast<int>( points.size() ) } );
    originalToUnique[original] = points.back().index;
  }
  const int size = static_cast<int>( points.size() );
  if ( size < 6 ) return result;
  const Tree tree( points.begin(), points.end() );
  std::vector<Patch> patches( size );
  Eigen::AlignedBox3d bounds;
  std::vector<double> heights;
  heights.reserve( size );
  for ( int i = 0; i < size; ++i )
  {
    bounds.extend( points[i].position );
    heights.push_back( points[i].position.z() );
    Patch &patch = patches[i];
    Nearest query { points[i], std::min( 18, size - 1 ), {} };
    Eigen::BVIntersect( tree, query );
    patch.count = static_cast<int>( query.found.size() );
    patch.radius = std::sqrt( query.found.top().first );
    for ( int j = patch.count - 1; j >= 0; --j )
    {
      patch.neighbors[j] = query.found.top().second;
      query.found.pop();
    }
    patch.plane = fitPlane( points, i, patch.neighbors, patch.count );
    const Plane small = fitPlane( points, i, patch.neighbors, std::min( 8, patch.count ) );
    if ( !patch.plane.stable && small.stable ) patch.plane = small;
    else if ( small.stable && std::abs( small.normal.dot( patch.plane.normal ) ) < 0.88 ) patch.plane.stable = false;
  }
  std::sort( heights.begin(), heights.end() );
  const double lowZ = heights[size / 100];
  const double height = heights.back() - heights.front();

  // Only a lowest horizontal patch spanning the footprint is a ground face.
  // A small, deep recess floor remains an upward-facing surface.
  std::vector<bool> visited( size, false );
  std::vector<int> component;
  for ( int i = 0; i < size; ++i )
  {
    if ( visited[i] || !patches[i].plane.stable || patches[i].plane.normal.z() < 0.9 ) continue;
    component.clear();
    component.push_back( i );
    visited[i] = true;
    Eigen::AlignedBox3d extent;
    double sumZ = 0.0, tolerance = 0.0;
    for ( size_t next = 0; next < component.size(); ++next )
    {
      const int index = component[next];
      const Patch &patch = patches[index];
      extent.extend( points[index].position );
      sumZ += points[index].position.z();
      tolerance = std::max( tolerance, std::max( patch.radius * 0.03, patch.plane.noise * 2.5 ) );
      for ( int j = 0; j < patch.count; ++j )
      {
        const int neighbor = patch.neighbors[j];
        const Patch &other = patches[neighbor];
        if ( visited[neighbor] || !other.plane.stable || other.plane.normal.z() < 0.9 ) continue;
        const Eigen::Vector3d delta = points[neighbor].position - points[index].position;
        const double distance = std::max( patch.radius * 0.03, 2.5 * std::max( patch.plane.noise, other.plane.noise ) );
        if ( delta.norm() > patch.radius || std::abs( delta.dot( patch.plane.normal ) ) > distance ) continue;
        visited[neighbor] = true;
        component.push_back( neighbor );
      }
    }
    if ( height > tolerance * 10.0 && component.size() >= 8
         && sumZ / component.size() <= lowZ + tolerance
         && extent.sizes().x() >= bounds.sizes().x() * 0.7
         && extent.sizes().y() >= bounds.sizes().y() * 0.7 )
      for ( int index : component ) patches[index].blocked = true;
  }

  std::vector<Surface> labels( size, Surface::Other );
  std::vector<int> frontier;
  frontier.reserve( size );
  for ( int i = 0; i < size; ++i )
  {
    Patch &patch = patches[i];
    if ( !patch.plane.stable ) continue;
    if ( patch.plane.normal.z() >= 0.2 && !patch.blocked )
    {
      Overhead query { points[i], patch, patches, patch.radius * 0.85, upperLayerGap( points, i, patch ) };
      Eigen::BVIntersect( tree, query );
      int sectors = 0;
      for ( int j = 0; j < 4; ++j ) if ( query.quadrants & ( 1 << j ) ) ++sectors;
      patch.blocked = query.support >= 4 && sectors >= 3;
    }
    std::array<double, 19> nz {};
    int count = 0;
    nz[count++] = patch.plane.normal.z();
    for ( int j = 0; j < patch.count; ++j )
    {
      const Plane &other = patches[patch.neighbors[j]].plane;
      if ( other.stable && std::abs( other.normal.dot( patch.plane.normal ) ) >= 0.94 ) nz[count++] = other.normal.z();
    }
    std::sort( nz.begin(), nz.begin() + count );
    patch.medianNz = nz[count / 2];
    if ( patch.blocked || count < 4 ) continue;
    if ( patch.plane.normal.z() >= 0.15 && patch.medianNz >= 0.2 )
    {
      labels[i] = Surface::Top;
      frontier.push_back( i );
    }
    else if ( patch.plane.normal.z() <= 0.15 && patch.medianNz <= 0.12 ) labels[i] = Surface::Side;
  }

  // Extend only over reliable, smoothly changing, non-vertical roof patches.
  for ( size_t next = 0; next < frontier.size(); ++next )
  {
    const int index = frontier[next];
    const Patch &patch = patches[index];
    for ( int j = 0; j < patch.count; ++j )
    {
      const int neighbor = patch.neighbors[j];
      const Patch &other = patches[neighbor];
      if ( labels[neighbor] == Surface::Top || other.blocked || !other.plane.stable
           || other.plane.normal.z() < 0.04 || other.medianNz < 0.06
           || std::abs( patch.plane.normal.dot( other.plane.normal ) ) < 0.96 ) continue;
      labels[neighbor] = Surface::Top;
      frontier.push_back( neighbor );
    }
  }
  for ( int i = 0; i < input.size(); ++i )
    if ( originalToUnique[i] >= 0 ) result[i] = labels[originalToUnique[i]];
  return result;
}
}

#endif
