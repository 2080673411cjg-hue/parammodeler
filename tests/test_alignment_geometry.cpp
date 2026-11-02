#include "../parammodeler_alignment_geometry.h"

#include <cassert>
#include <cmath>

using namespace ParamModelerAlignmentGeometry;

int main()
{
  const QVector<QVector3D> box = {
    QVector3D( 0, 0, 0 ), QVector3D( 10, 0, 0 ),
    QVector3D( 10, 5, 0 ), QVector3D( 0, 5, 0 ),
    QVector3D( 0, 0, 4 ), QVector3D( 10, 0, 4 ),
    QVector3D( 10, 5, 4 ), QVector3D( 0, 5, 4 )
  };
  for ( const float angle : { 0.0f, 45.0f, 90.0f, -45.0f } )
  {
    QMatrix4x4 rotation;
    rotation.rotate( angle, 0, 0, 1 );
    QVector3D offset;
    assert( upperOffsetFromRotatedMesh( box, rotation, QVector3D( 0, 0, 4 ), offset ) );
    QVector3D cloudMin = rotation.map( box.first() ) + QVector3D( 100, 200, 30 );
    for ( const QVector3D &vertex : box )
    {
      const QVector3D p = rotation.map( vertex ) + QVector3D( 100, 200, 30 );
      cloudMin.setX( std::min( cloudMin.x(), p.x() ) );
      cloudMin.setY( std::min( cloudMin.y(), p.y() ) );
      cloudMin.setZ( std::min( cloudMin.z(), p.z() ) );
    }
    const QVector3D target = cloudMin + offset;
    assert( std::abs( target.x() - 100 ) < 0.001 );
    assert( std::abs( target.y() - 200 ) < 0.001 );
    assert( std::abs( target.z() - 34 ) < 0.001 );
  }

  assert( !bottomFootprintAgreesWithFull(
    QVector3D( 2.17f, -2.75f, 0 ), QVector3D( 40.7f, 9.7f, 4 ),
    QVector3D( 0.34f, -0.21f, 0 ), QVector3D( 40.7f, 9.76f, 4 ) ) );
  assert( bottomFootprintAgreesWithFull(
    QVector3D( 0.3f, -0.1f, 0 ), QVector3D( 40.5f, 9.8f, 4 ),
    QVector3D( 0.34f, -0.21f, 0 ), QVector3D( 40.7f, 9.76f, 4 ) ) );

  QVector<QVector3D> flatRoof;
  for ( int i = 0; i < 1000; ++i )
    flatRoof.append( QVector3D( i % 40, i % 10, 3.3f + 0.10f * std::sin( i * 0.37 ) ) );
  for ( int i = 0; i < 600; ++i )
    flatRoof.append( QVector3D( i % 40, 0, 3.3f * ( i % 100 ) / 100.0f ) );
  for ( int i = 0; i < 40; ++i )
    flatRoof.append( QVector3D( i % 40, i % 10, 4.2f ) );
  double top = 0.0;
  assert( supportedFlatRoofTop( flatRoof, top ) );
  assert( std::abs( top - 3.3 ) < 0.05 );

  QVector<QVector3D> houses;
  for ( int x = 0; x <= 14; ++x )
    for ( int z = 0; z <= 4; ++z )
      houses.append( QVector3D( 100 + x, -50, 20 + z ) );
  for ( int x = 0; x <= 14; ++x )
    for ( int y = 1; y <= 8; ++y )
      houses.append( QVector3D( 100 + x, -50 + y,
                                24 + 3.0f * ( 1.0f - std::abs( y - 4 ) / 4.0f ) ) );
  for ( int i = 0; i < 120; ++i )
    houses.append( QVector3D( 115 + i % 10, -42 + i % 8, 27 ) );

  double eave = 0.0;
  assert( firstGableEave( houses, QVector3D( 100, -50, 20 ),
                          QVector3D( 124, -35, 27 ), 20, 7, eave ) );
  assert( std::abs( eave - 24 ) < 0.05 );

  QVector<QVector3D> roof;
  constexpr double pi = 3.14159265358979323846;
  for ( int x = 1; x < 12; ++x )
    for ( int i = 0; i <= 16; ++i )
    {
      const double t = pi * i / 32.0; // One half of the arc is missing.
      roof.append( QVector3D( 100 + x, -46 + 4 * std::cos( t ),
                              24 + 4 * std::sin( t ) ) );
    }
  for ( int x = 0; x <= 12; ++x )
    for ( int z = 0; z <= 4; ++z )
      roof.append( QVector3D( 100 + x, -50, 20 + z ) );
  roof.append( QVector3D( 106, -46, 34 ) );
  roof.append( QVector3D( 107, -46, 17 ) );

  assert( halfCylinderEave( roof, QVector3D( 100, -50, 20 ),
                            QVector3D( 112, -42, 28 ), 20, 8, eave ) );
  assert( std::abs( eave - 24 ) < 0.05 );

  QVector<QVector3D> tallRoof;
  for ( int x = 1; x < 12; ++x )
    for ( int i = 3; i <= 29; ++i )
    {
      const double t = pi * i / 32.0;
      tallRoof.append( QVector3D( 100 + x, -46 + 4 * std::cos( t ),
                                  120 + 4 * std::sin( t ) ) );
    }
  assert( halfCylinderEave( tallRoof, QVector3D( 100, -50, 20 ),
                            QVector3D( 112, -42, 124 ), 20, 104, eave ) );
  assert( std::abs( eave - 120 ) < 0.05 );

  QVector<QVector3D> noRoof;
  for ( int i = 0; i < 60; ++i )
    noRoof.append( QVector3D( 100 + i % 12, -50, 20 + i % 5 ) );
  assert( !halfCylinderEave( noRoof, QVector3D( 100, -50, 20 ),
                             QVector3D( 112, -42, 28 ), 20, 8, eave ) );

  QVector<QVector3D> recess;
  for ( int x = 0; x <= 20; ++x )
    for ( int y = 0; y <= 20; ++y )
      if ( x < 3 || x > 17 || y < 3 || y > 17 )
        recess.append({x*.5f, y*.5f, 6+.02f*std::sin(float(x+y))});
  for ( int i = 0; i < 2000; ++i ) recess.append({3+(i%40)*.1f, 3+(i/40)*.08f, 5.5f});
  for ( int i = 0; i < 100; ++i ) recess.append({0, float(i%10), (i%30)*.2f});
  recess.append({0, 0, 8});
  assert( indentedRimTop(recess, {0,0,0}, {10,10,8}, top) );
  assert( std::abs(top-6) < .05 );

  QVector<QVector3D> gable;
  for ( int x = 0; x <= 40; ++x )
    for ( int y = 0; y <= 40; ++y )
      gable.append({x*.5f, y*.25f, 4+3*(1-std::abs(y*.25f-5)/5)});
  for ( int i = 0; i < 20; ++i ) gable.append({float(i), 0, 9});
  assert( roofPlaneEave(gable, {0,0,0}, {20,10,9}, true, 0, 9, eave) );
  assert( std::abs(eave-4) < .05 );
  QVector<QVector3D> plateau;
  for ( int x = 0; x <= 40; ++x )
    for ( int y = 0; y <= 20; ++y )
      plateau.append({x*.5f, y*.5f, 4+std::min(3.f, 1.5f*std::min({x*.5f,20-x*.5f,y*.5f,10-y*.5f}))});
  assert( roofPlaneEave(plateau, {0,0,0}, {20,10,7}, false, 0, 7, eave) );
  assert( std::abs(eave-4) < .05 );
  assert( !roofPlaneEave({}, {0,0,0}, {20,10,7}, false, 0, 7, eave) );

  QVector<QPointF> arc;
  for ( int i = 0; i < 160; ++i )
  {
    const double t = (10+i)*pi/180;
    const double r = 4+.02*std::sin(i*1.7);
    arc.append({100+r*std::cos(t), -50+r*std::sin(t)});
  }
  arc.append({104, -43}); arc.append({102, -44});
  Circle circle;
  assert( fitCircle(arc, circle) );
  assert( std::hypot(circle.center.x()-100, circle.center.y()+50) < .05 );
  assert( std::abs(circle.radius-4) < .05 );
  QVector<QPointF> line;
  for ( int i = 0; i < 80; ++i ) line.append({double(i), 3});
  assert( !fitCircle(line, circle) );
  QVector<QPointF> shortArc;
  for ( int i = 0; i < 80; ++i ) shortArc.append({4*std::cos(i*pi/1440), 4*std::sin(i*pi/1440)});
  assert( !fitCircle(shortArc, circle) );

  QVector<QVector3D> cylinder;
  for ( int i = 0; i < 160; ++i )
    for ( int z = 0; z < 8; ++z )
    {
      const double t = (10+i)*pi/180;
      cylinder.append({float(100+4*std::cos(t)), float(-50+4*std::sin(t)), float(20+z)});
    }
  QPointF center;
  assert( roundBaseCenter(cylinder, {96,-54,20}, {104,-46,27}, center) );
  assert( std::hypot(center.x()-100,center.y()+50) < .01 );

  QVector3D sectionMin(100,-50,20), sectionMax(112,-42,34);
  assert( halfCylinderSection(roof, sectionMin, sectionMax, eave) );
  assert( std::abs(sectionMin.y()+50) < .05 && std::abs(sectionMax.y()+42) < .05 && std::abs(eave-24) < .05 );
  assert( !halfCylinderSection(noRoof, sectionMin, sectionMax, eave) );

  QVector<QVector3D> tower;
  for ( int layer = 0; layer < 48; ++layer )
    for ( int i = 0; i < 80; ++i )
    {
      const double t = (15+i*2)*pi/180;
      const double r = (layer < 16 ? 4.0 : layer < 24 ? 4.5 : 4.5-(layer-24)*.09) + .01*std::sin(i*1.3+layer);
      tower.append({float(100+r*std::cos(t)), float(-50+r*std::sin(t)), float(20+layer*.15)});
    }
  assert( towerCenter(tower, {95.5f,-54.5f,20}, {104.5f,-45.5f,27.1f}, center) );
  assert( std::hypot(center.x()-100,center.y()+50) < .06 );
  assert( !towerCenter(noRoof, {100,-50,20}, {112,-42,28}, center) );
  QVector<QVector3D> unrelatedRings;
  for ( int layer = 0; layer < 16; ++layer )
    for ( int i = 0; i < 80; ++i )
    {
      const double t = 2*pi*i/80;
      unrelatedRings.append({float(layer*.3+4*std::cos(t)), float(4*std::sin(t)), float(layer)});
    }
  assert( !towerCenter(unrelatedRings, {-4,-4,0}, {8.5f,4,15}, center) );

  QVector<QVector3D> walls;
  for ( int a = 0; a <= 50; ++a )
    for ( int z = 0; z <= 20; ++z )
    {
      const float noise = .02f*std::sin(float(a*3+z));
      walls.append({100+noise, -50+a*.2f, 20+z*.25f});
      walls.append({100+a*.4f, -50+noise, 20+z*.25f});
    }
  // A remote second house must not replace the first house's exterior corner.
  for ( int a = 0; a <= 50; ++a )
    for ( int z = 0; z <= 20; ++z ) walls.append({120+a*.3f,-50+a*.15f,20+z*.25f});
  QPointF corner(100.2, -49.8);
  assert( exteriorWallCorner(walls, {100.2f,-49.8f,20}, {135,-30,28}, 25, corner) );
  assert( std::hypot(corner.x()-100,corner.y()+50) < .03 );
  QVector<QVector3D> oneWall;
  for ( int a = 0; a <= 50; ++a )
    for ( int z = 0; z <= 20; ++z ) oneWall.append({100.f, -50+a*.2f, 20+z*.25f});
  corner = QPointF(100.2, -49.7);
  assert( exteriorWallCorner(oneWall, {100.2f,-49.8f,20}, {120,-40,25}, 25, corner) );
  assert( std::abs(corner.x()-100) < .01 && std::abs(corner.y()+49.7) < .001 );
  assert( !exteriorWallCorner(gable, {0,0,0}, {20,10,9}, 4, corner) );

  QVector<QVector3D> firstRoof = walls;
  for ( int x = 0; x <= 60; ++x )
    for ( int y = 0; y <= 40; ++y )
      firstRoof.append({100+x*.25f, -50+y*.25f,
                        25+3*(1-std::abs(y*.25f-4)/((y*.25f <= 4) ? 4.f : 6.f))+.01f*std::sin(float(x+y*3))});
  // A dense, taller second roof cannot set the first house's eave.
  for ( int i = 0; i < 3000; ++i ) firstRoof.append({120+(i%50)*.3f, -43+(i/50)*.2f, 32});
  QVector3D roofCorner;
  assert( firstGableRoofCorner(firstRoof, {100.2f,-49.8f,20}, {135,-30,32}, {100.2,-49.8}, 24.94, roofCorner) );
  assert( std::abs(roofCorner.x()-100) < .04 && std::abs(roofCorner.y()+50) < .04 );
  assert( std::abs(roofCorner.z()-25) < .04 );
  QVector<QVector3D> noisyFirstRoof = firstRoof;
  for ( int i = 0; i < noisyFirstRoof.size(); ++i )
    noisyFirstRoof[i] += QVector3D(.08f*std::sin(i*1.37f), .08f*std::sin(i*2.17f), .08f*std::sin(i*.73f));
  assert( firstGableRoofCorner(noisyFirstRoof, {100.2f,-49.8f,20}, {135,-30,32}, {100.2,-49.8}, 24.94, roofCorner) );
  assert( std::abs(roofCorner.z()-25) < .10 );
  QVector<QVector3D> withoutEndWall;
  for ( const auto &p : firstRoof ) if ( p.x() > 100.1 ) withoutEndWall.append(p);
  assert( firstGableRoofCorner(withoutEndWall, {100.2f,-49.8f,20}, {135,-30,32}, {100.3,-49.8}, 24.94, roofCorner) );
  assert( std::abs(roofCorner.x()-100.3) < .001 && std::abs(roofCorner.z()-25) < .04 );
  QVector<QVector3D> withoutFrontWall, flatFirstRoof = firstRoof;
  for ( const auto &p : firstRoof ) if ( p.y() > -49.9 ) withoutFrontWall.append(p);
  for ( auto &p : flatFirstRoof ) if ( p.z() > 25 ) p.setZ(25);
  assert( !firstGableRoofCorner(withoutFrontWall, {100.2f,-49.8f,20}, {135,-30,32}, {100.2,-49.8}, 24.6, roofCorner) );
  assert( !firstGableRoofCorner(flatFirstRoof, {100.2f,-49.8f,20}, {135,-30,32}, {100.2,-49.8}, 24.6, roofCorner) );
  assert( !firstGableRoofCorner(walls, {100.2f,-49.8f,20}, {135,-30,28}, {100.2,-49.8}, 24.6, roofCorner) );
  assert( !firstGableRoofCorner(gable, {0,0,0}, {20,10,9}, {0,0}, 4, roofCorner) );
  const QVector3D previousCorner = roofCorner;
  assert( !firstGableRoofCorner(firstRoof, {100.2f,-49.8f,20}, {135,-30,32}, {100.2,-49.8}, 24.0, roofCorner) );
  assert( roofCorner == previousCorner );

  QVector<QVector3D> outerWalls;
  for ( int i = 0; i <= 50; ++i )
    for ( int z = 0; z <= 24; ++z )
    {
      const float noise = .02f*std::sin(float(i*3+z));
      outerWalls.append({100+noise, -50+i*.2f, 20+z*.25f});
      outerWalls.append({100+i*.4f, -50+noise, 20+z*.25f});
    }
  QVector<QVector3D> topRim;
  for ( int x = 0; x <= 80; ++x )
    for ( int y = 0; y <= 40; ++y )
      if ( x < 12 || x > 56 || y < 12 || y > 28 ) topRim.append({100+x*.25f, -50+y*.25f, 26});
  QVector<QVector3D> recessedCloud = outerWalls;
  recessedCloud += topRim;
  for ( int i = 0; i < 3000; ++i ) recessedCloud.append({103+(i%60)*.18f, -47+(i/60)*.08f, 24});
  QPointF outerCorner(100.2,-49.8);
  assert( indentedOuterWallCorner(recessedCloud, {100.2f,-49.8f,20}, {120,-40,26}, 26, outerCorner) );
  assert( std::hypot(outerCorner.x()-100,outerCorner.y()+50) < .03 );

  QVector<QVector3D> missingOuterWall = topRim;
  for ( const auto &p : outerWalls ) if ( p.x() > 100.1 ) missingOuterWall.append(p);
  // A nearby, densely sampled recess wall is not an exterior X boundary.
  for ( int y = 0; y <= 50; ++y )
    for ( int z = 0; z <= 24; ++z ) missingOuterWall.append({101.5f, -48+y*.12f, 20+z*.25f});
  outerCorner = QPointF(100.2,-49.8);
  assert( indentedOuterWallCorner(missingOuterWall, {100.2f,-49.8f,20}, {120,-40,26}, 26, outerCorner) );
  assert( std::abs(outerCorner.x()-100.2) < .001 && std::abs(outerCorner.y()+50) < .03 );
  outerCorner = QPointF(100.2,-49.8);
  assert( !indentedOuterWallCorner(topRim, {100.2f,-49.8f,20}, {120,-40,26}, 26, outerCorner) );
  assert( outerCorner == QPointF(100.2,-49.8) );
  assert( flatOuterRimCorner(topRim, {100.2f,-49.8f,20}, {120,-40,26}, 26, outerCorner) );
  assert( std::abs(outerCorner.x()-100) < .001 && std::abs(outerCorner.y()+50) < .001 );
  QVector<QVector3D> noisyRim = topRim;
  for ( int i = 0; i < noisyRim.size(); ++i )
    noisyRim[i] += QVector3D(.08f*std::sin(i*1.3f), .08f*std::sin(i*2.7f), .03f*std::sin(i*.7f));
  noisyRim.append({95,-48,26}); noisyRim.append({102,-55,26});
  outerCorner = QPointF(100.2,-49.8);
  assert( flatOuterRimCorner(noisyRim, {100.2f,-49.8f,20}, {120,-40,26}, 26, outerCorner) );
  assert( std::abs(outerCorner.x()-100) < .12 && std::abs(outerCorner.y()+50) < .12 );
  outerCorner = QPointF(100.2,-49.8);
  assert( !flatOuterRimCorner({{100,-50,26},{101,-50,26}}, {100.2f,-49.8f,20}, {120,-40,26}, 26, outerCorner) );
  assert( outerCorner == QPointF(100.2,-49.8) );

  QVector<QVector3D> lRoof;
  for ( int x = 0; x <= 80; ++x )
    for ( int y = 0; y <= 40; ++y )
      if ( x <= 32 || y <= 16 ) lRoof.append({100+x*.25f, -50+y*.25f, 26});
  // Dense notch edges must not replace the two full-length exterior boundaries.
  for ( int i = 0; i < 3000; ++i )
    lRoof.append({108+(i%60)*.2f, -46, 26});
  corner = QPointF(100.2,-49.8);
  assert( flatOuterRimCorner(lRoof, {100.2f,-49.8f,20}, {120,-40,26}, 26, corner) );
  assert( std::hypot(corner.x()-100,corner.y()+50) < .001 );
  QVector<QVector3D> lOneWall = lRoof;
  lOneWall += oneWall;
  std::array<ParamModelerCornerFit::Plane, 2> lWalls;
  corner = QPointF(100.2,-49.8);
  assert( exteriorWallCorner(lOneWall, {100.2f,-49.8f,20}, {120,-40,26}, 26, corner, &lWalls) );
  const double wallX = corner.x();
  assert( lWalls[0].radius > 0 && lWalls[1].radius <= 0 );
  assert( flatOuterRimCorner(lOneWall, {100.2f,-49.8f,20}, {120,-40,26}, 26, corner,
                            {lWalls[0].radius <= 0, lWalls[1].radius <= 0}) );
  assert( corner.x() == wallX && std::abs(corner.y()+50) < .001 );
  assert( !flatOuterRimCorner(lRoof, {100.2f,-49.8f,20}, {120,-40,26}, 26, corner, {false,false}) );
  // A surviving bottom reference can already extend beyond the sampled roof edge.
  corner = QPointF(99.9,-50.1);
  assert( !flatOuterRimCorner(lRoof, {100.2f,-49.8f,20}, {120,-40,26}, 26, corner) );
  assert( corner == QPointF(99.9,-50.1) );
  QVector<QVector3D> lFrontWall = lRoof;
  for ( const auto &p : outerWalls ) if ( p.x() > 100.1 ) lFrontWall.append(p);
  lWalls = {};
  corner = QPointF(100.2,-49.8);
  assert( exteriorWallCorner(lFrontWall, {100.2f,-49.8f,20}, {120,-40,26}, 26, corner, &lWalls) );
  const double wallY = corner.y();
  assert( lWalls[0].radius <= 0 && lWalls[1].radius > 0 );
  assert( flatOuterRimCorner(lFrontWall, {100.2f,-49.8f,20}, {120,-40,26}, 26, corner,
                            {lWalls[0].radius <= 0, lWalls[1].radius <= 0}) );
  assert( corner.y() == wallY && std::abs(corner.x()-100) < .001 );
  QVector<QVector3D> partialRoof;
  for ( const auto &p : lRoof ) if ( p.x() > 105 && p.y() > -47 ) partialRoof.append(p);
  corner = QPointF(100.2,-49.8);
  assert( !flatOuterRimCorner(partialRoof, {100.2f,-49.8f,20}, {120,-40,26}, 26, corner) );
  assert( corner == QPointF(100.2,-49.8) );
  return 0;
}
