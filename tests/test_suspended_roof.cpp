#include "../parammodeler_suspended_roof.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static int g_checks = 0;

static void check( bool ok, const char *message )
{
  ++g_checks;
  if ( !ok )
  {
    std::cerr << "FAIL: " << message << '\n';
    std::exit( 1 );
  }
}

int main()
{
  // 1) Normalized synthetic cloud must not trigger Z-gap filtering.
  {
    QVector<QVector3D> pts;
    for ( int i = 0; i < 30; ++i )
      pts.append( QVector3D( 0.0f, 0.0f, -0.5f ) );
    for ( int i = 0; i < 300; ++i )
      pts.append( QVector3D( 0.1f, 0.1f, 0.3f + 0.001f * i ) );
    const auto a = ParamModelerSuspendedRoof::analyze( pts, true );
    check( a.profile == ParamModelerSuspendedRoof::CloudVerticalProfile::Normal,
           "normalized cloud stays Normal" );
    check( !a.shouldStripBottom(), "normalized cloud is not stripped" );
  }

  // 2) Thin flat roof slab with sparse ground points preserves ground reference.
  {
    QVector<QVector3D> pts;
    for ( int i = 0; i < 30; ++i )
      pts.append( QVector3D( 0.0f, 0.0f, 0.01f * i ) );
    for ( int i = 0; i < 500; ++i )
    {
      const float x = ( i % 25 ) * 0.4f;
      const float y = ( i / 25 ) * 0.4f;
      const float z = 5.2f + ( i % 5 ) * 0.1f;
      pts.append( QVector3D( x, y, z ) );
    }
    const auto a = ParamModelerSuspendedRoof::analyze( pts, false );
    check( a.profile == ParamModelerSuspendedRoof::CloudVerticalProfile::FlatKeepGround,
           "flat roof with sparse ground points keeps ground" );
    check( !a.shouldStripBottom(), "flat roof should not strip bottom" );
  }

  // 3) Tall upper body with sparse low noise strips only bottom noise.
  {
    QVector<QVector3D> pts;
    for ( int i = 0; i < 40; ++i )
      pts.append( QVector3D( 0.0f, 0.0f, 0.01f * i ) );
    for ( int i = 0; i < 600; ++i )
      pts.append( QVector3D( 1.0f, 1.0f, 5.0f + 7.5f * ( static_cast<float>( i ) / 599.0f ) ) );
    const auto a = ParamModelerSuspendedRoof::analyze( pts, false );
    check( a.profile == ParamModelerSuspendedRoof::CloudVerticalProfile::DetachedBottomStrip,
           "tall upper body with sparse bottom detects DetachedBottomStrip" );
    check( a.shouldStripBottom() && !a.isSuspendedRoofOnly(),
           "DetachedBottomStrip strips bottom without roof-only height reconstruction" );
    const auto filtered = ParamModelerSuspendedRoof::filterUpperCluster( pts, a );
    check( filtered.size() == 600, "all 40 low noise points removed" );
  }

  // 4) Suspended pyramidal roof filters ground points and reconstructs full height + wallRatio.
  {
    QVector<QVector3D> pts;
    for ( int i = 0; i < 200; ++i )
      pts.append( QVector3D( 0.0f, 0.0f, 0.002f * i ) ); // 0.0 .. 0.4 m
    for ( int i = 0; i < 2500; ++i )
      pts.append( QVector3D( 2.0f, 2.0f, 7.0f + 2.7f * ( static_cast<float>( i ) / 2499.0f ) ) );
    const auto a = ParamModelerSuspendedRoof::analyze( pts, false );
    check( a.profile == ParamModelerSuspendedRoof::CloudVerticalProfile::SuspendedRoofOnly,
           "suspended pyramid detects SuspendedRoofOnly" );
    check( a.shouldStripBottom() && a.isSuspendedRoofOnly(),
           "SuspendedRoofOnly enables bottom filter and reconstruction" );
    const auto filtered = ParamModelerSuspendedRoof::filterUpperCluster( pts, a );
    check( filtered.size() == 2500, "filtered suspended roof keeps 2500 roof points" );

    QMap<QString, double> nn;
    nn.insert( QStringLiteral( "totalHeight" ), 3.0 );
    nn.insert( QStringLiteral( "geom_total_height" ), 3.0 );
    nn.insert( QStringLiteral( "wallRatio" ), 0.65 );
    ParamModelerSuspendedRoof::reconstructSuspendedRoofHeights( QStringLiteral( "PyramidalRoofHouse" ), a, nn );
    check( nn.value( QStringLiteral( "totalHeight" ) ) > 9.0 && nn.value( QStringLiteral( "totalHeight" ) ) < 10.2,
           "ground-hint reconstruction restores ~9.7m full building height" );
    const double reconstructedRoof = nn.value( QStringLiteral( "totalHeight" ) ) * ( 1.0 - nn.value( QStringLiteral( "wallRatio" ) ) );
    check( std::abs( reconstructedRoof - a.roofHeight ) < 0.05,
           "reconstructed roof height equals observed roofHeight" );

    // Fallback when ground hint is absent (<20 low points): uses clamped wallRatio.
    ParamModelerSuspendedRoof::Analysis noGround = a;
    noGround.belowCount = 5;
    QMap<QString, double> nnFallback;
    nnFallback.insert( QStringLiteral( "wallRatio" ), 0.70 );
    ParamModelerSuspendedRoof::reconstructSuspendedRoofHeights( QStringLiteral( "PyramidalRoofHouse" ), noGround, nnFallback );
    check( std::abs( nnFallback.value( QStringLiteral( "wallRatio" ) ) - 0.70 ) < 1e-6,
           "wallRatio fallback preserved when no ground points" );
    check( nnFallback.value( QStringLiteral( "totalHeight" ) ) > 8.0,
           "totalHeight inferred from roofH / (1 - wallRatio) when no ground hint" );
  }

  std::cout << g_checks << " suspended roof checks passed\n";
  return 0;
}
