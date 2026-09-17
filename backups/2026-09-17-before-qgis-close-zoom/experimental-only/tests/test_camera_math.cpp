#include "../parammodeler_camera_math.h"
#include <cstdlib>
#include <iostream>
#include <limits>

static void check( bool ok, const char *name )
{
  if ( !ok ) { std::cerr << "FAIL: " << name << '\n'; std::exit( 1 ); }
}

int main()
{
  using namespace ParamModelerCamera;
  check( minimumDistance( 10 ) == 0.01, "building scale" );
  check( minimumDistance( 1 ) == 0.005, "small cylinder scale" );
  check( minimumDistance( 10000 ) == 0.1, "large scene cap" );
  check( minimumDistance( 0 ) > 0, "degenerate bounds" );
  check( minimumDistance( std::numeric_limits<double>::quiet_NaN() ) == 0.1, "invalid bounds" );
  const double landscape = framingDistance( 10, 45, 1.5 );
  const double portrait = framingDistance( 10, 45, 0.5 );
  check( portrait > landscape, "portrait fits horizontal extent" );
  check( std::abs( framingDistance( 20, 45, 1.5 ) - 2 * landscape ) < 1e-9, "scale proportional framing" );
  check( landscape * std::sin( 45 * 0.008726646259971648 ) > 5, "sphere fits with margin" );
  check( framingDistance( 0, 45, 1 ) > 0, "degenerate framing" );
  check( std::isfinite( framingDistance( 1, 0, 0 ) ), "zero viewport guards" );
  std::cout << "PASS: 10 camera scale/framing checks\n";
}
