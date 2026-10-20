#include "../parammodeler_fit_metrics.h"
#include <QElapsedTimer>
#include <cassert>
#include <iostream>
#include <limits>

static void near( double actual, double expected, double tolerance = 1e-6 )
{
  assert( std::abs( actual - expected ) < tolerance );
}

int main()
{
  using namespace ParamModelerFit;
  const Triangle triangle { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
  near( triangleSquaredDistance( { .2, .2, 0 }, triangle ), 0 );
  near( triangleSquaredDistance( { .2, .2, 2 }, triangle ), 4 );
  near( triangleSquaredDistance( { 1, 1, 0 }, triangle ), .5 );
  near( triangleSquaredDistance( { -1, -1, 0 }, triangle ), 2 );
  near( triangleSquaredDistance( { 1, 1, 2 }, triangle ), 4.5 );
  near( triangleSquaredDistance( { .5, 1, 0 }, { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 } } ), 1 );
  near( triangleSquaredDistance( { 0, 0, 2 }, { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } } ), 4 );

  MeshData mesh;
  mesh.addTriangle( { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } );
  QMatrix4x4 identity;
  const QVector<QVector3D> points { { .2f, .2f, 0 }, { .2f, .2f, 1 }, { .2f, .2f, 3 } };
  auto metrics = evaluate( mesh, points, identity );
  assert( metrics.count == 3 );
  near( metrics.rmse, std::sqrt( 10.0 / 3 ) );
  near( metrics.p95, 3 );
  assert( evaluate( MeshData(), points, identity ).count == 0 );
  assert( evaluate( mesh, {}, identity ).count == 0 );

  QMatrix4x4 pose;
  pose.translate( 4, -3, 2 );
  pose.rotate( 15, 1, 0, 0 );
  pose.rotate( 20, 0, 1, 0 );
  pose.rotate( 63, 0, 0, 1 );
  QVector<QVector3D> moved;
  for ( const auto &point : points )
    moved.append( pose.map( point ) );
  metrics = evaluate( mesh, moved, pose );
  near( metrics.rmse, std::sqrt( 10.0 / 3 ), 1e-5 );
  near( metrics.p95, 3, 1e-5 );
  QMatrix4x4 shifted;
  shifted.translate( 0, 0, 1 );
  near( evaluate( mesh, { { .2f, .2f, 0 } }, shifted ).rmse, 1 );

  moved.append( { std::numeric_limits<float>::quiet_NaN(), 0, 0 } );
  assert( evaluate( mesh, moved, pose ).count == 3 );
  QVector<QVector3D> outliers( 100, { .2f, .2f, 0 } );
  outliers[0].setZ( 10 );
  metrics = evaluate( mesh, outliers, identity );
  near( metrics.rmse, 1 );
  near( metrics.p95, 0 );

  MeshData dense;
  for ( int i = 0; i < 64; ++i )
    for ( int j = 0; j < 32; ++j )
      dense.addQuad( { float( i ), float( j ), 0 }, { float( i + 1 ), float( j ), 0 },
                     { float( i + 1 ), float( j + 1 ), 0 }, { float( i ), float( j + 1 ), 0 } );
  QVector<QVector3D> sample;
  for ( int i = 0; i < 2048; ++i )
    sample.append( { float( i % 64 ) + .2f, float( i / 64 ) + .2f, .1f } );
  QElapsedTimer timer;
  timer.start();
  for ( int i = 0; i < 10; ++i )
    near( evaluate( dense, sample, identity ).rmse, .1 );
  std::cout << "Fit metrics PASS; 4096 triangles / 2048 points: " << timer.elapsed() / 10.0 << " ms/update\n";
}
