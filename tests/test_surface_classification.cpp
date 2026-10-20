#include "../parammodeler_surface_classification.h"

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>

using ParamModelerSurfaces::Surface;
using ParamModelerSurfaces::classify;

static void grid( QVector<QVector3D> &points, const QVector3D &origin,
                  const QVector3D &u, const QVector3D &v, int count = 21 )
{
  for ( int i = 0; i < count; ++i )
    for ( int j = 0; j < count; ++j )
      points.append( origin + u * ( float( i ) / ( count - 1 ) ) + v * ( float( j ) / ( count - 1 ) ) );
}

static void regressionTests()
{
  assert( classify( {} ).isEmpty() );
  assert( classify( { QVector3D( 0, 0, 0 ) } )[0] == Surface::Other );
  QVector<QVector3D> line;
  for ( int i = 0; i < 30; ++i ) line.append( QVector3D( i, 0, 2 ) );
  for ( Surface label : classify( line ) ) assert( label == Surface::Other );

  QVector<QVector3D> box;
  grid( box, QVector3D( 0, 0, 4 ), QVector3D( 10, 0, 0 ), QVector3D( 0, 8, 0 ) );
  const int roofCenter = 220;
  const int sideCenter = box.size() + 220;
  grid( box, QVector3D( 0, 0, 0 ), QVector3D( 10, 0, 0 ), QVector3D( 0, 0, 4 ) );
  grid( box, QVector3D( 0, 8, 0 ), QVector3D( 10, 0, 0 ), QVector3D( 0, 0, 4 ) );
  grid( box, QVector3D( 0, 0, 0 ), QVector3D( 0, 8, 0 ), QVector3D( 0, 0, 4 ) );
  grid( box, QVector3D( 10, 0, 0 ), QVector3D( 0, 8, 0 ), QVector3D( 0, 0, 4 ) );
  const int bottomCenter = box.size() + 220;
  grid( box, QVector3D( 0, 0, 0 ), QVector3D( 10, 0, 0 ), QVector3D( 0, 8, 0 ) );
  const auto labels = classify( box );
  assert( labels[roofCenter] == Surface::Top );
  assert( labels[sideCenter] == Surface::Side );
  assert( labels[bottomCenter] == Surface::Other );

  QVector<QVector3D> repeated = box;
  repeated += box;
  repeated += box;
  assert( box.size() < 6000 && repeated.size() > 6000 );
  const auto duplicateLabels = classify( repeated );
  for ( int i = 0; i < repeated.size(); ++i ) assert( duplicateLabels[i] == labels[i % box.size()] );
  QVector<QVector3D> transformed;
  for ( const QVector3D &point : box ) transformed.append( point * 5.0f + QVector3D( 200, -100, 50 ) );
  const auto transformedLabels = classify( transformed );
  int changed = 0;
  for ( int i = 0; i < labels.size(); ++i ) if ( labels[i] != transformedLabels[i] ) ++changed;
  std::cout << "Scale/translation boundary changes: " << changed << '/' << labels.size() << std::endl;
  assert( changed < labels.size() / 20 );
  assert( transformedLabels[roofCenter] == labels[roofCenter] );
  assert( transformedLabels[sideCenter] == labels[sideCenter] );
  assert( transformedLabels[bottomCenter] == labels[bottomCenter] );

  QVector<QVector3D> recess;
  grid( recess, QVector3D( 0, 0, 4 ), QVector3D( 10, 0, 0 ), QVector3D( 0, 1, 0 ) );
  grid( recess, QVector3D( 0, 7, 4 ), QVector3D( 10, 0, 0 ), QVector3D( 0, 1, 0 ) );
  grid( recess, QVector3D( 0, 0, 0 ), QVector3D( 10, 0, 0 ), QVector3D( 0, 0, 4 ) );
  const int floorCenter = recess.size() + 220;
  grid( recess, QVector3D( 3, 2, 0.02f ), QVector3D( 4, 0, 0 ), QVector3D( 0, 4, 0 ) );
  const int innerWallCenter = recess.size() + 220;
  grid( recess, QVector3D( 3, 2, 0.02f ), QVector3D( 4, 0, 0 ), QVector3D( 0, 0, 3.98f ) );
  const auto recessLabels = classify( recess );
  assert( recessLabels[floorCenter] == Surface::Top );
  assert( recessLabels[innerWallCenter] == Surface::Side );

  QVector<QVector3D> slab;
  grid( slab, QVector3D( 0, 0, 2 ), QVector3D( 4, 0, 0 ), QVector3D( 0, 4, 0 ) );
  grid( slab, QVector3D( 0, 0, 2.4f ), QVector3D( 4, 0, 0 ), QVector3D( 0, 4, 0 ) );
  grid( slab, QVector3D( 0, 0, 0 ), QVector3D( 4, 0, 0 ), QVector3D( 0, 0, 2.4f ) );
  const auto slabLabels = classify( slab );
  assert( slabLabels[220] == Surface::Other );
  assert( slabLabels[441 + 220] == Surface::Top );
  for ( int i = 441; i < 882; ++i ) slab[i].setZ( 2.12f );
  const auto thinSlabLabels = classify( slab );
  assert( thinSlabLabels[220] == Surface::Other );
  assert( thinSlabLabels[441 + 220] == Surface::Top );

  QVector<QVector3D> arc;
  for ( int x = 0; x <= 30; ++x )
    for ( int angle = 0; angle <= 80; ++angle )
    {
      const double t = 3.14159265358979323846 * angle / 80;
      arc.append( QVector3D( x * 0.3f, 3 + 3 * std::cos( t ), 2 + 3 * std::sin( t ) ) );
    }
  grid( arc, QVector3D( 0, 0, 0 ), QVector3D( 9, 0, 0 ), QVector3D( 0, 0, 2 ), 31 );
  grid( arc, QVector3D( 0, 6, 0 ), QVector3D( 9, 0, 0 ), QVector3D( 0, 0, 2 ), 31 );
  const auto arcLabels = classify( arc );
  assert( arcLabels[15 * 81 + 40] == Surface::Top );
  assert( arcLabels[15 * 81 + 4] == Surface::Top );
  assert( arcLabels[15 * 81 + 76] == Surface::Top );
  assert( arcLabels[31 * 81 + 15 * 31 + 15] == Surface::Side );

  QVector<QVector3D> dome;
  for ( int latitude = 0; latitude <= 30; ++latitude )
    for ( int sector = 0; sector < 64; ++sector )
    {
      const double t = latitude * 3.14159265358979323846 / 60;
      const double a = sector * 3.14159265358979323846 / 32;
      dome.append( QVector3D( 3 * std::sin( t ) * std::cos( a ), 3 * std::sin( t ) * std::sin( a ), 2 + 3 * std::cos( t ) ) );
    }
  for ( int z = 0; z <= 20; ++z )
    for ( int sector = 0; sector < 64; ++sector )
    {
      const double a = sector * 3.14159265358979323846 / 32;
      dome.append( QVector3D( 3 * std::cos( a ), 3 * std::sin( a ), z * 0.1f ) );
    }
  const auto domeLabels = classify( dome );
  assert( domeLabels[28 * 64 + 16] == Surface::Top );
  assert( domeLabels[31 * 64 + 10 * 64 + 16] == Surface::Side );

  QVector<QVector3D> denseBox;
  grid( denseBox, QVector3D( 0, 0, 4 ), QVector3D( 10, 0, 0 ), QVector3D( 0, 8, 0 ), 41 );
  grid( denseBox, QVector3D( 0, 0, 0 ), QVector3D( 10, 0, 0 ), QVector3D( 0, 0, 4 ), 41 );
  grid( denseBox, QVector3D( 0, 8, 0 ), QVector3D( 10, 0, 0 ), QVector3D( 0, 0, 4 ), 41 );
  grid( denseBox, QVector3D( 0, 0, 0 ), QVector3D( 0, 8, 0 ), QVector3D( 0, 0, 4 ), 41 );
  grid( denseBox, QVector3D( 10, 0, 0 ), QVector3D( 0, 8, 0 ), QVector3D( 0, 0, 4 ), 41 );
  grid( denseBox, QVector3D( 0, 0, 0 ), QVector3D( 10, 0, 0 ), QVector3D( 0, 8, 0 ), 41 );
  const auto denseLabels = classify( denseBox );
  assert( denseLabels[840] == Surface::Top );
  assert( denseLabels[1681 + 840] == Surface::Side );
  assert( denseLabels[5 * 1681 + 840] == Surface::Other );

  QVector<QVector3D> slope;
  grid( slope, QVector3D( 0, 0, 3 ), QVector3D( 6, 0, 0 ), QVector3D( 0, 3, 4 ) );
  assert( classify( slope )[220] == Surface::Top );
  QVector<QVector3D> noisyRoof;
  grid( noisyRoof, QVector3D( 0, 0, 3 ), QVector3D( 6, 0, 0 ), QVector3D( 0, 6, 0 ) );
  std::mt19937 random( 42 );
  std::normal_distribution<float> noise( 0.0f, 0.015f );
  for ( QVector3D &point : noisyRoof ) point += QVector3D( noise( random ), noise( random ), noise( random ) );
  const auto noisyLabels = classify( noisyRoof );
  const int blue = static_cast<int>( std::count( noisyLabels.cbegin(), noisyLabels.cend(), Surface::Top ) );
  assert( blue > noisyRoof.size() * 0.8 );

  QVector<QVector3D> invalid = box;
  invalid.append( QVector3D( std::numeric_limits<float>::quiet_NaN(), 0, 0 ) );
  assert( classify( invalid ).last() == Surface::Other );
  std::cout << "PASS: surface classification regression tests\n";
}

int main( int argc, char **argv )
{
  regressionTests();
  for ( int argument = 1; argument < argc; ++argument )
  {
    std::ifstream file( argv[argument] );
    assert( file.good() );
    QVector<QVector3D> points;
    std::string line;
    while ( std::getline( file, line ) )
    {
      std::istringstream record( line );
      float x, y, z;
      if ( record >> x >> y >> z ) points.append( QVector3D( x, y, z ) );
    }
    const auto start = std::chrono::steady_clock::now();
    const auto labels = classify( points );
    std::array<int, 3> counts {};
    for ( Surface label : labels ) ++counts[static_cast<int>( label )];
    assert( labels.size() == points.size() );
    const double seconds = std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
    std::cout << argv[argument] << " top=" << counts[0] << " side=" << counts[1]
              << " other=" << counts[2] << " seconds=" << seconds << '\n';
  }
}
