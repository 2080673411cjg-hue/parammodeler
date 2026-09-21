#ifndef PARAMMODELER_CORNERFIT_H
#define PARAMMODELER_CORNERFIT_H

#include <Eigen/Eigenvalues>
#include <Eigen/Geometry>
#include <Eigen/LU>
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

namespace ParamModelerCornerFit
{
using Point = Eigen::Vector3d;
struct Plane
{
  Point center = Point::Zero();
  Point normal = Point::Zero();
  double radius = 0;
  double rms = 0;
  int inliers = 0;
  int samples = 0;
};

enum class FitFailure
{
  None,
  InvalidInput,
  TooFewNearbyPoints,
  NoPlanarConsensus,
  LineLikePatch,
  TooNoisy,
  SeedOffPlane
};

struct FitDiagnostics
{
  FitFailure failure = FitFailure::None;
  int samples = 0;
  int inliers = 0;
  double support = 0;
  double rms = 0;
  bool weak = false;
};

// Seeded consensus isolates the selected face; Eigen PCA refines its plane.
inline bool fitPlane( const std::vector<Point> &cloud, const Point &seed, double radius, Plane &out,
                      FitDiagnostics *diagnostics = nullptr )
{
  if ( diagnostics ) *diagnostics = FitDiagnostics();
  const auto fail = [diagnostics]( FitFailure reason ) {
    if ( diagnostics ) diagnostics->failure = reason;
    return false;
  };
  if ( !seed.allFinite() || !std::isfinite( radius ) || radius <= 0 ) return fail( FitFailure::InvalidInput );
  std::vector<Point> local;
  for ( const Point &p : cloud )
    if ( p.allFinite() && ( p - seed ).norm() <= radius ) local.push_back( ( p - seed ) / radius );
  if ( diagnostics ) diagnostics->samples = static_cast<int>( local.size() );
  if ( local.size() < 8 ) return fail( FitFailure::TooFewNearbyPoints );
  if ( local.size() > 4096 )
  {
    std::vector<Point> sampled;
    for ( size_t i = 0; i < 4096; ++i ) sampled.push_back( local[i * local.size() / 4096] );
    local.swap( sampled );
    if ( diagnostics ) diagnostics->samples = static_cast<int>( local.size() );
  }
  const double tolerance = 0.04;
  const double strongTolerance = 0.02;
  std::mt19937 random( 731 );
  std::uniform_int_distribution<size_t> index( 0, local.size() - 1 );
  std::vector<size_t> best;
  for ( int trial = 0; trial < 256; ++trial )
  {
    const Point a = local[index( random )];
    const Point b = local[index( random )];
    const Point c = local[index( random )];
    Point n = ( b - a ).cross( c - a );
    if ( n.norm() < 1e-6 ) continue;
    n.normalize();
    if ( std::abs( n.dot( a ) ) > tolerance ) continue;
    std::vector<size_t> inliers;
    for ( size_t i = 0; i < local.size(); ++i )
      if ( std::abs( n.dot( local[i] - a ) ) <= tolerance ) inliers.push_back( i );
    if ( inliers.size() > best.size() ) best = std::move( inliers );
  }
  if ( diagnostics )
  {
    diagnostics->inliers = static_cast<int>( best.size() );
    diagnostics->support = local.empty() ? 0 : static_cast<double>( best.size() ) / static_cast<double>( local.size() );
  }
  if ( best.size() < 8 || best.size() < local.size() * 0.35 ) return fail( FitFailure::NoPlanarConsensus );
  Point center = Point::Zero(), normal = Point::Zero();
  double rms = 0;
  for ( int iteration = 0; iteration < 3; ++iteration )
  {
    center.setZero();
    for ( size_t i : best ) center += local[i];
    center /= static_cast<double>( best.size() );
    Eigen::Matrix3d covariance = Eigen::Matrix3d::Zero();
    for ( size_t i : best )
    {
      const Point d = local[i] - center;
      covariance += d * d.transpose();
    }
    covariance /= static_cast<double>( best.size() );
    const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver( covariance );
    if ( solver.info() != Eigen::Success || solver.eigenvalues()[1] < 0.0008 ) return fail( FitFailure::LineLikePatch );
    normal = solver.eigenvectors().col( 0 );
    rms = std::sqrt( std::max( 0.0, solver.eigenvalues()[0] ) );
    if ( iteration < 2 )
    {
      best.clear();
      for ( size_t i = 0; i < local.size(); ++i )
        if ( std::abs( normal.dot( local[i] - center ) ) <= tolerance ) best.push_back( i );
      if ( diagnostics )
      {
        diagnostics->inliers = static_cast<int>( best.size() );
        diagnostics->support = local.empty() ? 0 : static_cast<double>( best.size() ) / static_cast<double>( local.size() );
      }
      if ( best.size() < 8 || best.size() < local.size() * 0.35 ) return fail( FitFailure::NoPlanarConsensus );
    }
  }
  if ( diagnostics )
  {
    diagnostics->inliers = static_cast<int>( best.size() );
    diagnostics->support = local.empty() ? 0 : static_cast<double>( best.size() ) / static_cast<double>( local.size() );
    diagnostics->rms = rms * radius;
  }
  if ( rms > 0.035 ) return fail( FitFailure::TooNoisy );
  if ( std::abs( normal.dot( center ) ) > 0.06 ) return fail( FitFailure::SeedOffPlane );
  const bool weak = best.size() < 12 || best.size() < local.size() * 0.60 ||
                    rms > 0.012 || std::abs( normal.dot( center ) ) > strongTolerance;
  out.center = seed + center * radius;
  out.normal = normal;
  out.radius = radius;
  out.rms = rms * radius;
  out.inliers = static_cast<int>( best.size() );
  out.samples = static_cast<int>( local.size() );
  if ( diagnostics )
  {
    diagnostics->failure = FitFailure::None;
    diagnostics->weak = weak;
  }
  return true;
}

inline bool isWall( const Plane &plane, const Point &up )
{
  return std::abs( plane.normal.dot( up.normalized() ) ) < 0.55;
}
inline bool isTop( const Plane &plane, const Point &up )
{
  return std::abs( plane.normal.dot( up.normalized() ) ) > 0.80;
}
inline bool intersect( const std::array<Plane, 3> &planes, Point &corner )
{
  Eigen::Matrix3d a;
  Point b;
  const Point origin = planes[0].center;
  for ( int i = 0; i < 3; ++i )
  {
    if ( !planes[i].center.allFinite() || !planes[i].normal.allFinite() ||
         planes[i].radius <= 0 || std::abs( planes[i].normal.norm() - 1.0 ) > 1e-6 ) return false;
    a.row( i ) = planes[i].normal.transpose();
    b[i] = planes[i].normal.dot( planes[i].center - origin );
  }
  if ( std::abs( a.determinant() ) < 0.25 ) return false;
  const Point candidate = origin + a.fullPivLu().solve( b );
  if ( !candidate.allFinite() ) return false;
  for ( const Plane &plane : planes )
    if ( ( candidate - plane.center ).norm() > 4.0 * plane.radius ) return false;
  corner = candidate;
  return true;
}
}
#endif
