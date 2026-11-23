#include "../buildmesh.h"
#include "../parammodeler_face_fit_profiles.h"
#include "../parammodeler_fit_metrics.h"
#include <QElapsedTimer>
#include <cassert>
#include <iostream>

int main()
{
  const std::vector<std::pair<const char *, MeshData>> models {
    { "FlatRoofHouse", BuildMesh::buildCuboid( CuboidParams() ) },
    { "RoundFlatRoofHouse", BuildMesh::buildCylinder( CylinderParams() ) },
    { "LShapedHouse", BuildMesh::buildLHouse( LHouseParams() ) },
    { "ConicalRoofHouse", BuildMesh::buildConeCylinder( ConeCylinderParams() ) },
    { "GableRoofHouse", BuildMesh::buildGabledRoof( GabledRoofParams() ) },
    { "PyramidalRoofHouse", BuildMesh::buildPyramidRoof( PyramidRoofParams() ) },
    { "FlatTopHipRoofHouse", BuildMesh::buildTruncatedPyramidRoof( TruncatedPyramidRoofParams() ) },
    { "BarrelRoofHouse", BuildMesh::buildHalfCylinderRoof( HalfCylinderRoofParams() ) },
    { "DomeRoofHouse", BuildMesh::buildCylinderHemisphere( CylinderHemisphereParams() ) },
    { "RecessedRoofHouse", BuildMesh::buildIndentedCuboid( IndentedCuboidParams() ) },
    { "ShortRidgeHipRoofHouse", BuildMesh::buildAsymmetricGableHouse( AsymmetricGableHouseParams() ) },
    { "CurvedRoofRoundHouse", BuildMesh::buildFourStageRoundTower( FourStageRoundTowerParams() ) },
    { "AngledGableHouse", BuildMesh::buildTwoGableHouses( TwoGableHousesParams() ) }
  };
  QMatrix4x4 identity, pose;
  pose.translate( 100, -50, 30 );
  pose.rotate( 51, 1, 0, 0 );
  pose.rotate( 73, 0, 1, 0 );
  pose.rotate( 123, 0, 0, 1 );
  for ( const auto &item : models )
  {
    const MeshData &mesh = item.second;
    Eigen::AlignedBox3d box;
    for ( const auto &v : mesh.vertices ) box.extend( ParamModelerFit::vector( v ) );
    const QVector3D inner( box.center().x(), box.center().y(), box.min().z() + .1 * box.sizes().z() );
    const QVector3D surface = ( mesh.vertices[mesh.indices[0]] + mesh.vertices[mesh.indices[1]] + mesh.vertices[mesh.indices[2]] ) / 3;
    const QVector3D outer( box.max().x() + 3, box.max().y() + 3, box.max().z() + 3 );
    const QVector<QVector3D> probes { inner, surface, outer };
    for ( const auto &transform : { identity, pose } )
    {
      QVector<QVector3D> moved;
      for ( const auto &point : probes ) moved.append( transform.map( point ) );
      const auto result = ParamModelerFit::evaluate( mesh, moved, transform, .001 );
      std::cout << item.first << ": closed=" << result.solidAvailable << " fit=" << result.surfaceCount
                << " inside=" << result.insideCount << " outside=" << result.outsideCount << std::endl;
      assert( result.solidAvailable && result.surfaceCount == 1 && result.insideCount == 1 && result.outsideCount == 1 );
    }
  }
  auto check = [&]( const MeshData &mesh, const QVector3D &point, bool interior ) {
    const auto result = ParamModelerFit::evaluate( mesh, { point }, identity, .001 );
    assert( result.solidAvailable && result.surfaceCount == 0 );
    assert( result.insideCount == ( interior ? 1 : 0 ) );
    assert( result.outsideCount == ( interior ? 0 : 1 ) );
  };
  check( models[2].second, { 18, 9, 3 }, false ); // L notch, inside the outer bounding box.
  check( models[2].second, { 18, 3, 3 }, true );
  check( models[9].second, { 2, 2, 5 }, false ); // Recess air, also inside the outer bounding box.
  check( models[9].second, { 2, 2, 2 }, true );
  check( models[7].second, { 6, 4, 6 }, true );
  check( models[7].second, { 6, .1f, 7.9f }, false );
  check( models[11].second, { 8.5f, 0, 2.05f }, true ); // Inside the tower, below the curved roof.
  check( models[11].second, { 8.5f, 0, 2.0f }, true ); // The old internal eave ring is not a surface.
  TwoGableHousesParams bent;
  bent.angle = 150;
  check( BuildMesh::buildTwoGableHouses( bent ), { 18, 5, 2 }, true );
  check( BuildMesh::buildTwoGableHouses( bent ), { 2, 10, 2 }, false );

  using namespace ParamModelerFaceFit;
  const std::vector<std::tuple<int, QVector<double>, MeshData, std::array<int, 3>>> profiles {
    { 1, { 4, 8 }, models[1].second, { 1, 3, 3 } },
    { 3, { 4, 8, 5.0 / 8 }, models[3].second, { 1, 4, 4 } },
    { 7, { 12, 8, 4 }, models[7].second, { 4, 3, 3 } },
    { 8, { 4, 7, 5.0 / 7, .3 }, models[8].second, { 1, 5, 5 } },
    { 11, { 8, 1.9, 8.96, .1, 2, 1, .2, 1.5 }, models[11].second, { 1, 8, 8 } },
    { 12, { 14, 10, 8, 7, 4.0 / 7, 150, .5 }, BuildMesh::buildTwoGableHouses( bent ), { 6, 2, 2 } }
  };
  for ( const auto &[model, values, mesh, handleCounts] : profiles )
  {
    Eigen::AlignedBox3d bounds;
    for ( const auto &v : mesh.vertices ) bounds.extend( ParamModelerFit::vector( v ) );
    const auto type = profileType( QString::fromLatin1( models[model].first ) );
    assert( type != ProfileType::None );
    for ( int face = 0; face < 3; ++face )
    {
      const auto projection = projectProfile( type, values, mesh, face );
      assert( !projection.outline.isEmpty() && projection.handles.size() == handleCounts[face] );
      const QRectF outline = projection.outline.boundingRect();
      assert( std::abs( outline.width() - bounds.sizes()[face == 2 ? 1 : 0] ) < 1e-5 );
      assert( std::abs( outline.height() - bounds.sizes()[face == 0 ? 1 : 2] ) < 1e-5 );
      for ( const auto &handle : projection.handles )
      {
        assert( handle.parameter >= 0 && handle.parameter < values.size() );
        assert( std::abs( handle.changeForDrag( 0.25 * handle.tangent ) - 0.25 ) < 1e-8 );
        assert( outline.adjusted( -1e-5, -1e-5, 1e-5, 1e-5 ).contains( handle.position ) );
      }
    }
    std::cout << models[model].first << ": profile and handles verified in all three views" << std::endl;
  }
  const auto barrel = projectProfile( ProfileType::Barrel, { 12, 8, 4 }, models[7].second, 2 );

  const QVector<double> towerValues{ 8, 4, 10, .5, 3, 2, .3, 2 };
  for ( TowerParameter layer : { BodyRadius, EaveRadius, TopRadius } )
  {
    const auto projection = projectTowerRadius( towerValues, layer );
    assert( projection.measuredRadius == towerValues[layer] && projection.handles.size() == 1 );
    assert( projection.handles.front().parameter == layer && projection.handles.front().position == QPointF( towerValues[layer], 0 ) );
    assert( projection.outline.boundingRect() == QRectF( -10, -10, 20, 20 ) );
  }
  QVector<QVector3D> towerCloud{ { 8, 0, 2 }, { 11, 0, 2 }, { 4, 0, 0 }, { 10, 0, 4.25 },
                                { 9, 0, 4.9f }, { 2, 0, 7.5 }, { 2.5, 0, 7.9f }, { 1, 0, 8.6f } };
  for ( auto &point : towerCloud ) point = pose.map( point );
  const auto towerInverse = pose.inverted();
  assert( ( sampleTowerRadius( towerValues, towerCloud, towerInverse, BodyRadius, .2 ) == QVector<int>{ 0, 1 } ) );
  assert( ( sampleTowerRadius( towerValues, towerCloud, towerInverse, EaveRadius, .2 ) == QVector<int>{ 3 } ) );
  assert( ( sampleTowerRadius( towerValues, towerCloud, towerInverse, EaveRadius, .5 ) == QVector<int>{ 3, 4 } ) );
  assert( ( sampleTowerRadius( towerValues, towerCloud, towerInverse, TopRadius, .2 ) == QVector<int>{ 5 } ) );
  auto movedRadius = towerValues;
  movedRadius[BodyRadius] = 7;
  assert( ( sampleTowerRadius( movedRadius, towerCloud, towerInverse, BodyRadius, .2 ) == QVector<int>{ 0, 1 } ) );

  assert( barrel.outline.contains( { 4, -7 } ) && !barrel.outline.contains( { .1, -7.9 } ) );
  assert( barrel.handles[1].parameter == 1 && !barrel.handles[1].movesMinimum );
  assert( barrel.handles[2].parameter == 1 && barrel.handles[2].movesMinimum );
  assert( barrel.handles[2].changeForDrag( { -1, 0 } ) == 1 );
  assert( minimumSpanAroundFeature( 12, 2, 6, false ) == 8 );
  assert( minimumSpanAroundFeature( 12, 2, 6, true ) == 10 );
  for ( bool movesMinimum : { false, true } )
  {
    const auto [minimum, maximum] = spanRangeAtFixedFraction( 12, .25, .1, .8, movesMinimum );
    for ( double next : { minimum, maximum } )
    {
      const double shift = movesMinimum ? 12 - next : 0;
      const double fraction = ( 3 - shift ) / next;
      assert( fraction >= .1 - 1e-8 && fraction <= .8 + 1e-8 );
      assert( std::abs( shift + fraction * next - 3 ) < 1e-8 );
    }
  }
  const auto angled = projectProfile( ProfileType::AngledGable, { 14, 10, 8, 7, 4.0 / 7, 150, .5 },
                                      BuildMesh::buildTwoGableHouses( bent ), 0 );
  assert( angled.outline.contains( { 18, -5 } ) && !angled.outline.contains( { 2, -10 } ) );
  const auto &cylinder = models[1].second;
  const QVector<QVector3D> topCloud { { 0, 0, 8 }, { 2, 0, 8 }, { 0, 0, 0 }, { 0, 0, 3 } };
  assert( ( sampleProfile( cylinder, topCloud, identity, 0, -1, .1 ).indices == QVector<int>{ 0, 1 } ) );
  const QVector<QVector3D> sideCloud { { 0, 4, 3 }, { 0, 4, 4 }, { 0, -4, 4 }, { 0, 1, 4 } };
  const auto positiveSide = sampleProfile( cylinder, sideCloud, identity, 1, -1, .1 );
  assert( ( positiveSide.side == 1 && positiveSide.indices == QVector<int>{ 0, 1 } ) );
  const auto negativeSide = sampleProfile( cylinder, sideCloud, identity, 1, 0, .1 );
  assert( ( negativeSide.side == 0 && negativeSide.indices == QVector<int>{ 2 } ) );
  const auto fallback = sampleProfile( cylinder, { { 0, 3, 4 } }, identity, 1, -1, .1 );
  assert( ( fallback.side == 1 && fallback.indices == QVector<int>{ 0 } ) );

  const auto occluded = []( const MeshData &mesh, const Eigen::Vector3d &camera, const Eigen::Vector3d &point ) {
    using ParamModelerFit::Triangle;
    using ParamModelerFit::vector;
    std::vector<Triangle> triangles;
    for ( int i = 0; i < mesh.indices.size(); i += 3 )
      triangles.push_back( { vector( mesh.vertices[mesh.indices[i]] ), vector( mesh.vertices[mesh.indices[i + 1]] ),
                             vector( mesh.vertices[mesh.indices[i + 2]] ) } );
    const Eigen::KdBVH<double, 3, Triangle> tree( triangles.begin(), triangles.end() );
    ParamModelerFit::RayQuery ray{ camera, point - camera, {}, 1.0 - 1e-6, true };
    Eigen::BVIntersect( tree, ray );
    return ray.hit;
  };
  LHouseParams l;
  l.totalLength = 10;
  l.totalWidth = 8;
  l.wingRatio = .6;
  l.wingWidthRatio = .375;
  l.height = 5;
  const auto lMesh = BuildMesh::buildLHouse( l );
  assert( occluded( lMesh, { 15, -1, 9 }, { 4, 4, .75 } ) );
  assert( !occluded( lMesh, { 15, -1, 9 }, { 4, 4, 4.8 } ) );
  assert( !occluded( lMesh, { 15, 12, 10 }, { 7, 3, 1 } ) );
  assert( occluded( lMesh, { -8, -8, 12 }, { 7, 3, 1 } ) );

  IndentedCuboidParams pit;
  pit.outerLength = 10;
  pit.outerWidth = 8;
  pit.outerHeight = 6;
  pit.innerLength = 4;
  pit.innerWidth = 3;
  pit.innerHeight = 3;
  pit.offsetX = 2;
  pit.offsetY = 2;
  const auto pitMesh = BuildMesh::buildIndentedCuboid( pit );
  assert( !occluded( pitMesh, { 4, 3.5, 17 }, { 4, 3.5, 3 } ) );
  assert( occluded( pitMesh, { 30, 3.5, 14 }, { 4, 3.5, 3 } ) );

  TwoGableHousesParams turned;
  turned.length1 = 8;
  turned.length2 = 10;
  turned.width = 6;
  turned.wallHeight = 4;
  turned.roofHeight = 2;
  turned.angle = 135;
  const auto turnedMesh = BuildMesh::buildTwoGableHouses( turned );
  const double end = 10 / std::sqrt( 2.0 );
  const Eigen::Vector3d endWall( static_cast<float>( 8 + end ), static_cast<float>( 3 + end ), 2 );
  assert( occluded( turnedMesh, { -3, 3, 7 }, endWall ) );
  assert( !occluded( turnedMesh, { 22, 10, 8 }, endWall ) );
  std::cout << "Camera segments: L notch, recess floor, and turned gable wall PASS\n";
  // Changing radii/roof proportions exercises floating-point seam welding and T-junctions.
  for ( int i = 0; i < 8; ++i )
  {
    const double f = i / 7.0;
    FourStageRoundTowerParams towerParams;
    towerParams.baseRadius = 1.3 + i * 2.7;
    towerParams.baseHeight = .7 + i * .41;
    towerParams.eaveRadius = towerParams.baseRadius * 1.12;
    towerParams.eaveHeight = .15;
    towerParams.middleTopRadius = towerParams.baseRadius * .18;
    towerParams.middleHeight = .9 + i * .37;
    towerParams.middleRise = towerParams.middleHeight * .25 * f;
    CylinderHemisphereParams dome;
    dome.radius = 1.7 + i * 1.31;
    dome.bulge = f;
    HalfCylinderRoofParams half;
    half.width = 3.1 + i * 2.19;
    half.wallHeight = 1.3;
    AsymmetricGableHouseParams roof;
    roof.length = 10.1 + i * 1.79;
    roof.ridgeLength = roof.length * ( .1 + .8 * f );
    roof.ridgeRatio = .2 + .6 * f;
    bent.angle = 135 + 45 * f;
    bent.ridgeRatio = roof.ridgeRatio;
    LHouseParams lhouse;
    lhouse.totalLength = 11.7 + i * 2.17;
    lhouse.wingRatio = .2 + .5 * f;
    lhouse.wingWidthRatio = .25 + .5 * f;
    IndentedCuboidParams recess;
    recess.offsetX = .31 + i * .13;
    recess.offsetY = .47 + i * .11;
    for ( const MeshData &variation : { BuildMesh::buildFourStageRoundTower( towerParams ),
          BuildMesh::buildCylinderHemisphere( dome ), BuildMesh::buildHalfCylinderRoof( half ),
          BuildMesh::buildAsymmetricGableHouse( roof ), BuildMesh::buildTwoGableHouses( bent ),
          BuildMesh::buildLHouse( lhouse ), BuildMesh::buildIndentedCuboid( recess ) } )
    {
      const QVector3D base = ( variation.vertices[variation.indices[0]] + variation.vertices[variation.indices[1]]
                              + variation.vertices[variation.indices[2]] ) / 3;
      const auto result = ParamModelerFit::evaluate( variation, { base, base + QVector3D( 0, 0, .01f ), base - QVector3D( 0, 0, 1 ) }, identity, .001 );
      assert( result.solidAvailable && result.surfaceCount == 1 && result.insideCount == 1 && result.outsideCount == 1 );
    }
  }
  std::cout << "56 varied solid meshes PASS\n";
  // Exercise the dense curved mesh at the same point budget as the live overlay.
  const MeshData &tower = models[11].second;
  QVector<QVector3D> points;
  for ( int i = 0; i < 2048; ++i ) points.append( { float( ( i % 64 ) * .3 - 9 ), float( ( i / 64 ) * .6 - 9 ), 1.f } );
  QElapsedTimer timer; timer.start();
  for ( int i = 0; i < 5; ++i )
  {
    const auto result = ParamModelerFit::evaluate( tower, points, identity, .1 );
    assert( result.surfaceCount + result.outsideCount + result.insideCount == result.count );
  }
  std::cout << "13 solids PASS; tower / 2048 points: " << timer.elapsed() / 5.0 << " ms/update\n";
}
