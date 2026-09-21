#include "../parammodeler_cornerfit.h"
#include <cstdlib>
#include <iostream>

using namespace ParamModelerCornerFit;
static int checks = 0;
static void check( bool condition, const char *name )
{
  ++checks;
  if ( !condition ) { std::cerr << "FAIL: " << name << '\n'; std::exit( 1 ); }
}

static void missingCorner( double scale, const Point &offset, bool rotated )
{
  const Eigen::Matrix3d rotation = rotated
    ? ( Eigen::AngleAxisd( 0.7, Point::UnitZ() ) * Eigen::AngleAxisd( 0.2, Point::UnitX() ) ).toRotationMatrix()
    : Eigen::Matrix3d::Identity();
  const auto transform = [&]( const Point &p ) -> Point { return offset + scale * ( rotation * p ); };
  std::vector<Point> cloud;
  std::mt19937 rng( 123 );
  std::normal_distribution<double> noise( 0, 0.001 );
  for ( int face = 0; face < 3; ++face )
    for ( int i = 0; i <= 20; ++i )
      for ( int j = 0; j <= 20; ++j )
      {
        const double u = i * 0.08, v = j * 0.08;
        Point p = face == 0 ? Point( 0, u, -v ) : face == 1 ? Point( u, 0, -v ) : Point( u, v, 0 );
        // No samples at the desired corner, nor in the surrounding 0.3-unit hole.
        if ( p.norm() < 0.3 ) continue;
        p += Point( noise( rng ), noise( rng ), noise( rng ) );
        cloud.push_back( transform( p ) );
      }
  std::uniform_real_distribution<double> scatter( -1, 2 );
  for ( int i = 0; i < 120; ++i ) cloud.push_back( transform( Point( scatter( rng ), scatter( rng ), scatter( rng ) ) ) );
  std::array<Plane, 3> planes;
  const Point seeds[] = { Point( 0, 0.65, -0.65 ), Point( 0.65, 0, -0.65 ), Point( 0.65, 0.65, 0 ) };
  for ( int i = 0; i < 3; ++i )
    check( fitPlane( cloud, transform( seeds[i] ), scale * 0.5, planes[i] ), "noisy face with missing corner and outliers" );
  const Point up = rotation * Point::UnitZ();
  check( isWall( planes[0], up ) && isWall( planes[1], up ) && isTop( planes[2], up ), "wall/top classification under rotation" );
  check( !isTop( planes[0], up ) && !isWall( planes[2], up ), "wrong face direction rejected" );
  Point corner;
  check( intersect( planes, corner ) && ( corner - offset ).norm() < 0.01 * scale, "virtual intersection recovers unsampled corner" );
  check( !intersect( { planes[0], planes[0], planes[2] }, corner ), "repeated/parallel face rejected" );
  auto distant = planes;
  distant[2].center += up * 20 * scale;
  check( !intersect( distant, corner ), "distant extrapolation rejected" );
}

int main()
{
  missingCorner( 1, Point::Zero(), false );
  missingCorner( 0.01, Point::Zero(), true );
  missingCorner( 100, Point( 500000, 4000000, 120 ), true );
  Plane plane;
  check( !fitPlane( {}, Point::Zero(), 1, plane ), "empty cloud rejected" );
  std::vector<Point> line;
  for ( int i = 0; i < 60; ++i ) line.emplace_back( i * 0.01, 0, 0 );
  check( !fitPlane( line, Point::Zero(), 1, plane ), "collinear patch rejected" );
  check( !fitPlane( line, Point::Zero(), 0, plane ), "zero radius rejected" );
  std::vector<Point> volume;
  for ( int i = -5; i <= 5; ++i )
    for ( int j = -5; j <= 5; ++j )
      for ( int k = -5; k <= 5; ++k ) volume.emplace_back( i * 0.1, j * 0.1, k * 0.1 );
  check( !fitPlane( volume, Point::Zero(), 1, plane ), "nonplanar volume rejected" );
  std::cout << checks << " corner fitting checks passed\n";
}
