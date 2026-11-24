#ifndef PARAMMODELER_FACE_FIT_PROFILES_H
#define PARAMMODELER_FACE_FIT_PROFILES_H

#include "parammodeler_fit_metrics.h"
#include "parammodeler_pick3d_math.h"
#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <QSizeF>
#include <QString>
#include <array>
#include <optional>
#include <set>

namespace ParamModelerFaceFit
{
enum class ProfileType { None, Cylinder, Cone, Barrel, Dome, Tower, AngledGable };
enum TowerParameter { BodyRadius, BodyHeight, EaveRadius, EaveThickness, CurvedHeight, TopRadius, CurvedRise, ConeHeight };

// A fixed feature occupies [offset, offset + size] before the exterior edge moves.
inline double minimumSpanAroundFeature( double span, double offset, double size, bool movesMinimum )
{
  return movesMinimum ? span - offset : offset + size;
}

inline QPair<double, double> spanRangeAtFixedFraction( double span, double fraction, double minimumFraction,
                                                       double maximumFraction, bool movesMinimum )
{
  const double distance = span * ( movesMinimum ? 1 - fraction : fraction );
  return movesMinimum ? qMakePair( distance / ( 1 - minimumFraction ), distance / ( 1 - maximumFraction ) )
                      : qMakePair( distance / maximumFraction, distance / minimumFraction );
}

inline ProfileType profileType( const QString &primitive )
{
  if ( primitive == QLatin1String( "RoundFlatRoofHouse" ) ) return ProfileType::Cylinder;
  if ( primitive == QLatin1String( "ConicalRoofHouse" ) ) return ProfileType::Cone;
  if ( primitive == QLatin1String( "BarrelRoofHouse" ) ) return ProfileType::Barrel;
  if ( primitive == QLatin1String( "DomeRoofHouse" ) ) return ProfileType::Dome;
  if ( primitive == QLatin1String( "CurvedRoofRoundHouse" ) ) return ProfileType::Tower;
  if ( primitive == QLatin1String( "AngledGableHouse" ) ) return ProfileType::AngledGable;
  return ProfileType::None;
}

struct ProfileHandle
{
  int parameter;
  QPointF position;
  QPointF tangent; // Projected movement for a one-unit increase in the parameter.
  const char *label;
  bool movesMinimum = false;
  QLineF movingEdge;
  QLineF fixedEdge;

  double changeForDrag( const QPointF &drag ) const
  {
    return QPointF::dotProduct( drag, tangent ) / QPointF::dotProduct( tangent, tangent );
  }
};

struct ProfileProjection
{
  QPainterPath outline;
  QPainterPath guides;
  QVector<ProfileHandle> handles;
  std::optional<double> measuredRadius;
};

inline ProfileProjection projectTowerRadius( const QVector<double> &values, TowerParameter layer )
{
  ProfileProjection result;
  result.outline.addEllipse( QPointF(), values[EaveRadius], values[EaveRadius] );
  for ( TowerParameter radius : { BodyRadius, EaveRadius, TopRadius } )
    if ( radius != layer ) result.guides.addEllipse( QPointF(), values[radius], values[radius] );

  const char *label = layer == BodyRadius ? QT_TRANSLATE_NOOP( "ParamModelerDock", "Cylinder radius" ) :
                      layer == EaveRadius ? QT_TRANSLATE_NOOP( "ParamModelerDock", "Eave outer radius" ) :
                                            QT_TRANSLATE_NOOP( "ParamModelerDock", "Cone base radius" );
  result.measuredRadius = values[layer];
  result.handles.append( { layer, QPointF( values[layer], 0 ), QPointF( 1, 0 ), label, false, {}, {} } );
  return result;
}

inline QVector<int> sampleTowerRadius( const QVector<double> &values, const QVector<QVector3D> &cloud,
                                        const QMatrix4x4 &worldToModel, TowerParameter layer, double band )
{
  const double body = values[BodyHeight];
  const double eaveTop = body + values[EaveThickness];
  const double curvedHeight = values[CurvedHeight];
  const double coneBase = eaveTop + curvedHeight;
  double lower = 0.15 * body, upper = 0.85 * body;
  if ( layer == EaveRadius )
  {
    lower = body - std::min( band, 0.1 * body );
    upper = eaveTop + std::min( band, 0.25 * curvedHeight );
  }
  else if ( layer == TopRadius )
  {
    lower = coneBase - std::min( band, 0.25 * curvedHeight );
    upper = coneBase + std::min( band, 0.25 * values[ConeHeight] );
  }
  QVector<int> sampled;
  for ( int i = 0; i < cloud.size(); ++i )
  {
    const QVector3D local = worldToModel.map( cloud[i] );
    // Keep radial discrepancies visible while excluding points from other height bands.
    if ( local.z() >= lower && local.z() <= upper && std::isfinite( local.x() ) && std::isfinite( local.y() ) )
      sampled.append( i );
  }
  return sampled;
}

inline QPainterPath projectedSilhouette( const MeshData &mesh, int horizontal, int vertical )
{
  QPainterPath projection;
  projection.setFillRule( Qt::WindingFill );
  const auto project = [horizontal, vertical]( const QVector3D &p ) { return QPointF( p[horizontal], -p[vertical] ); };
  for ( int i = 0; i + 2 < mesh.indices.size(); i += 3 )
  {
    const QPointF a = project( mesh.vertices[mesh.indices[i]] );
    QPointF b = project( mesh.vertices[mesh.indices[i + 1]] );
    QPointF c = project( mesh.vertices[mesh.indices[i + 2]] );
    const double cross = ( b.x() - a.x() ) * ( c.y() - a.y() ) - ( b.y() - a.y() ) * ( c.x() - a.x() );
    if ( std::abs( cross ) <= 1e-9 ) continue;
    if ( cross < 0 ) std::swap( b, c );
    projection.moveTo( a );
    projection.lineTo( b );
    projection.lineTo( c );
    projection.closeSubpath();
  }
  return projection.simplified();
}

inline QPainterPath radialSilhouette( const MeshData &mesh, int face, double radius )
{
  QPainterPath outline;
  if ( face == 0 )
  {
    outline.addEllipse( QPointF(), radius, radius );
    return outline;
  }
  // The mesh rings have a vertex on each model axis. Read the meridian directly
  // so the profile keeps a tower's overhang and the original roof tessellation.
  const int horizontal = face - 1;
  std::set<std::pair<float, float>> meridian;
  float maximumHeight = 0;
  for ( const auto &vertex : mesh.vertices )
  {
    maximumHeight = std::max( maximumHeight, vertex.z() );
    if ( vertex[horizontal] > 0 && std::abs( vertex[1 - horizontal] ) <= vertex[horizontal] * 1e-6 )
      meridian.emplace( vertex.z(), vertex[horizontal] );
  }
  if ( meridian.empty() ) return outline;
  if ( meridian.rbegin()->first < maximumHeight ) meridian.emplace( maximumHeight, 0 );
  outline.moveTo( meridian.begin()->second, -meridian.begin()->first );
  for ( const auto &point : meridian )
    outline.lineTo( point.second, -point.first );
  for ( auto it = meridian.rbegin(); it != meridian.rend(); ++it )
    outline.lineTo( -it->second, -it->first );
  outline.closeSubpath();
  return outline;
}

inline ProfileProjection projectProfile( ProfileType type, const QVector<double> &v, const MeshData &mesh, int face )
{
  ProfileProjection result;
  if ( mesh.isEmpty() || v.isEmpty() ) return result;
  const bool top = face == 0;
  const QPointF right( 1, 0 ), up( 0, -1 );
  const char *anchoredEdge = QT_TRANSLATE_NOOP( "ParamModelerDock", "Move this edge with the opposite edge fixed" );
  const auto handle = [&result]( int parameter, const QPointF &position, const QPointF &tangent, const char *label,
                                 bool movesMinimum = false, const QLineF &moving = {}, const QLineF &fixed = {} ) {
    result.handles.append( { parameter, position, tangent, label, movesMinimum, moving, fixed } );
  };
  const auto guide = [&result]( const QPointF &from, const QPointF &to ) {
    result.guides.moveTo( from );
    result.guides.lineTo( to );
  };
  if ( type == ProfileType::Barrel || type == ProfileType::AngledGable )
    result.outline = projectedSilhouette( mesh, face == 2 ? 1 : 0, top ? 1 : 2 );
  else
    result.outline = radialSilhouette( mesh, face, type == ProfileType::Tower ? v[EaveRadius] : v[0] );
  if ( result.outline.isEmpty() ) return result;

  switch ( type )
  {
    case ProfileType::Cylinder:
    case ProfileType::Cone:
    case ProfileType::Dome:
    {
      const double radius = v[0], height = v[1];
      const double cylinder = type == ProfileType::Cylinder ? height : height * v[2];
      if ( top )
        handle( 0, QPointF( radius, 0 ), right, "Radius:" );
      else
      {
        const QLineF left( -radius, 0, -radius, -cylinder ), side( radius, 0, radius, -cylinder );
        handle( 0, side.center(), 2 * right, anchoredEdge, false, side, left );
        handle( 0, left.center(), -2 * right, anchoredEdge, true, left, side );
      }
      if ( top ) break;
      const QLineF eave( -radius, -cylinder, radius, -cylinder );
      const bool flat = type == ProfileType::Cylinder;
      handle( 1, QPointF( 0, -height ), up, flat ? "Total height:" :
              QT_TRANSLATE_NOOP( "ParamModelerDock", "Roof height with eaves fixed" ), false,
              flat ? eave : QLineF(), flat ? QLineF( -radius, 0, radius, 0 ) : eave );
      if ( type == ProfileType::Cylinder ) break;
      guide( QPointF( -radius, -cylinder ), QPointF( radius, -cylinder ) );
      handle( 2, QPointF( 0, -cylinder ), up * height, "Cylinder height at fixed total height", false, eave,
              QLineF( 0, -height, 0, -height ) );
      if ( type == ProfileType::Dome )
      {
        const double domeHeight = height - cylinder;
        const double midRadius = radius * ( 0.875 - 0.375 * v[3] );
        const double midHeight = cylinder + domeHeight * ( 0.4625 + 0.1875 * v[3] );
        handle( 3, QPointF( midRadius, -midHeight ), QPointF( -0.375 * radius, -0.1875 * domeHeight ), "Bulge:" );
      }
      break;
    }
    case ProfileType::Barrel:
    {
      const double length = v[0], width = v[1], wall = v[2];
      if ( face != 2 )
      {
        const double h = top ? width : wall;
        const QLineF left( 0, 0, 0, -h ), side( length, 0, length, -h );
        handle( 0, side.center(), right, anchoredEdge, false, side, left );
        handle( 0, left.center(), -right, anchoredEdge, true, left, side );
      }
      if ( top )
      {
        const QLineF high( 0, -width, length, -width ), low( 0, 0, length, 0 );
        handle( 1, high.center(), up, anchoredEdge, false, high, low );
        handle( 1, low.center(), -up, anchoredEdge, true, low, high );
      }
      else
      {
        const double span = face == 2 ? width : length;
        guide( QPointF( 0, -wall ), QPointF( span, -wall ) );
        handle( 2, QPointF( span / 2, -wall ), up, "Wall height:", false,
                QLineF( 0, -wall, span, -wall ), QLineF( 0, 0, span, 0 ) );
        if ( face == 2 )
        {
          const QLineF left( 0, 0, 0, -wall ), side( width, 0, width, -wall );
          handle( 1, side.center(), right, anchoredEdge, false, side, left );
          handle( 1, left.center(), -right, anchoredEdge, true, left, side );
        }
      }
      break;
    }
    case ProfileType::Tower:
    {
      const double body = v[BodyHeight], eaveTop = body + v[EaveThickness];
      const double roofTop = eaveTop + v[CurvedHeight];
      const double radius = v[BodyRadius], eave = v[EaveRadius], cap = v[TopRadius];
      if ( top ) return projectTowerRadius( v, BodyRadius );
      guide( QPointF( -eave, -body ), QPointF( eave, -body ) );
      guide( QPointF( -eave, -eaveTop ), QPointF( eave, -eaveTop ) );
      guide( QPointF( -cap, -roofTop ), QPointF( cap, -roofTop ) );
      handle( BodyRadius, QPointF( radius, -body / 2 ), right, "Base radius:" );
      handle( BodyHeight, QPointF( 0, -body ), up, QT_TRANSLATE_NOOP( "ParamModelerDock", "Move lower eave edge with upper edge fixed" ),
              false, QLineF( -eave, -body, eave, -body ), QLineF( -eave, -eaveTop, eave, -eaveTop ) );
      handle( EaveRadius, QPointF( -eave, -( body + eaveTop ) / 2 ), -right, "Eave outer radius:" );
      handle( EaveThickness, QPointF( eave / 2, -eaveTop ), up, "Eave thickness:", false,
              QLineF( -eave, -eaveTop, eave, -eaveTop ), QLineF( -eave, -body, eave, -body ) );
      const double tip = -( roofTop + v[ConeHeight] );
      handle( CurvedHeight, QPointF( 0, -roofTop ), up, QT_TRANSLATE_NOOP( "ParamModelerDock", "Move cone base with tower apex fixed" ),
              false, QLineF( -cap, -roofTop, cap, -roofTop ), QLineF( 0, tip, 0, tip ) );
      handle( TopRadius, QPointF( cap, -roofTop ), right, "Middle top radius:" );
      handle( CurvedRise, QPointF( ( eave + cap ) / 2, -( eaveTop + v[CurvedHeight] / 2 + v[CurvedRise] ) ),
              up, "Middle rise (m):" );
      handle( ConeHeight, QPointF( 0, tip ), up, "Cone height:", false, {}, QLineF( -cap, -roofTop, cap, -roofTop ) );
      break;
    }
    case ProfileType::AngledGable:
    {
      const double length1 = v[0], length2 = v[1], width = v[2];
      const double height = v[3], wall = height * v[4];
      const double radians = std::acos( -1.0 ) / 180;
      const double turn = ( 180 - v[5] ) * radians;
      const QPointF direction( std::cos( turn ), -std::sin( turn ) );
      const QPointF joint( length1, 0 ), end = joint + direction * length2;
      const QPointF ridgeOffset = up * ( width * v[6] );
      if ( top )
      {
        guide( joint, joint + up * width );
        guide( ridgeOffset, joint + ridgeOffset );
        guide( joint + ridgeOffset, end + ridgeOffset );
        const QLineF first( QPointF(), up * width ), seam( joint, joint + up * width ), last( end, end + up * width );
        handle( 0, seam.center(), right, "House 1 length:", false, seam, first );
        handle( 0, first.center(), -right, QT_TRANSLATE_NOOP( "ParamModelerDock", "Move first end with second house fixed" ), true, first, seam );
        handle( 1, last.center(), direction, "House 2 length:", false, last, seam );
        handle( 2, QPointF( length1 / 2, -width ), up, "Width:" );
        handle( 5, end + up * width, QPointF( std::sin( turn ), std::cos( turn ) ) * ( length2 * radians ), "Angle:" );
        handle( 6, ridgeOffset, up * width, "Ridge ratio:" );
      }
      else
      {
        const double endPosition = face == 2 ? width - end.y() : end.x();
        const double ridge = face == 2 ? width * v[6] : length1 / 2;
        guide( QPointF( 0, -wall ), QPointF( endPosition, -wall ) );
        const QLineF eave( 0, -wall, endPosition, -wall );
        handle( 3, QPointF( ridge, -height ), up, QT_TRANSLATE_NOOP( "ParamModelerDock", "Roof height with eaves fixed" ), false, {}, eave );
        handle( 4, QPointF( ridge, -wall ), up * height, "Wall height at fixed total height", false, eave,
                QLineF( ridge, -height, ridge, -height ) );
      }
      break;
    }
    case ProfileType::None:
      break;
  }
  return result;
}

struct ProfileSample
{
  int side = -1;
  QVector<int> indices;
};

inline ProfileSample sampleProfile( const MeshData &mesh, const QVector<QVector3D> &cloud,
                                    const QMatrix4x4 &worldToModel, int face, int lockedSide, double band )
{
  using namespace ParamModelerFit;
  const int normal = face == 0 ? 2 : 2 - face;
  std::vector<Triangle> triangles;
  triangles.reserve( mesh.indices.size() / 3 );
  Eigen::AlignedBox3d bounds;
  for ( const auto &vertex : mesh.vertices )
    bounds.extend( vector( vertex ) );
  for ( int i = 0; i + 2 < mesh.indices.size(); i += 3 )
    triangles.push_back( { vector( mesh.vertices[mesh.indices[i]] ), vector( mesh.vertices[mesh.indices[i + 1]] ),
                           vector( mesh.vertices[mesh.indices[i + 2]] ) } );
  if ( triangles.empty() ) return {};
  const Eigen::KdBVH<double, 3, Triangle> tree( triangles.begin(), triangles.end() );
  const double low = bounds.min()[normal], high = bounds.max()[normal];
  const double origin = low - std::max( 1.0, high - low );
  RayQuery ray{ Eigen::Vector3d::Zero(), Eigen::Vector3d::Unit( normal ), {} };
  std::array<QVector<int>, 2> slices, fallback;
  for ( int i = 0; i < cloud.size(); ++i )
  {
    const Eigen::Vector3d point = vector( worldToModel.map( cloud[i] ) );
    if ( !point.allFinite() ) continue;
    ray.origin = point;
    ray.origin[normal] = origin;
    ray.hits.clear();
    Eigen::BVIntersect( tree, ray );
    double near = low, far = high;
    if ( !ray.hits.empty() )
    {
      const auto depths = std::minmax_element( ray.hits.begin(), ray.hits.end() );
      near = origin + *depths.first;
      far = origin + *depths.second;
    }
    const double nearError = std::abs( point[normal] - near );
    const double farError = std::abs( point[normal] - far );
    if ( face == 0 )
    {
      // A top-view slice follows the upper envelope, including sloped roofs.
      if ( ray.hits.empty() || farError <= nearError || far == near ) fallback[1].append( i );
      if ( farError <= band && ( !ray.hits.empty() || point[normal] > ( low + high ) / 2 ) ) slices[1].append( i );
      continue;
    }
    const int side = nearError <= farError ? 0 : 1;
    fallback[side].append( i );
    if ( nearError <= band && ( side == 0 || !ray.hits.empty() ) ) slices[0].append( i );
    if ( farError <= band && ( side == 1 || !ray.hits.empty() ) ) slices[1].append( i );
  }
  const auto support = [&slices, &fallback]( int side ) { return std::pair( slices[side].size(), fallback[side].size() ); };
  const int side = face == 0 ? 1 : lockedSide >= 0 ? lockedSide : support( 1 ) > support( 0 ) ? 1 : 0;
  return { face == 0 ? -1 : side, slices[side].isEmpty() ? std::move( fallback[side] ) : std::move( slices[side] ) };
}

struct Segment3D
{
  QVector3D start;
  QVector3D end;
  Segment3D() = default;
  Segment3D( const QVector3D &a, const QVector3D &b ) : start( a ), end( b ) {}
};

enum class Handle3DKind
{
  ExteriorAxis,
  WallRatio,
  RecessDepth,
  GableRidge,
  NotchX,
  NotchY,
  PlatformX,
  PlatformY,
  ShortRidgeEnd,
  ProfileParameter
};

struct Handle3D
{
  Handle3DKind kind = Handle3DKind::ExteriorAxis;
  int parameter = 0;
  bool movesMinimum = false;
  QVector3D position;
  QVector3D tangent; // Local model-space displacement for a +1 unit increase in the parameter.
  const char *label = "";
  QVector<Segment3D> movingEdges;
  QVector<Segment3D> fixedEdges;

  bool projectToScreen( const QMatrix4x4 &modelToWorld, const QMatrix4x4 &viewMatrix,
                        const QMatrix4x4 &projectionMatrix, const QSize &viewport,
                        QPointF &screenPosition, QPointF &screenTangent, double &depth ) const
  {
    if ( !ParamModelerPicking::project( modelToWorld.map( position ), viewMatrix, projectionMatrix,
                                        viewport, screenPosition, depth ) )
      return false;
    constexpr double step = 0.05;
    QPointF movedScreen;
    double movedDepth = 0;
    if ( !ParamModelerPicking::project( modelToWorld.map( position + float( step ) * tangent ),
                                        viewMatrix, projectionMatrix, viewport, movedScreen, movedDepth ) )
      return false;
    screenTangent = ( movedScreen - screenPosition ) / step;
    return std::isfinite( screenTangent.x() ) && std::isfinite( screenTangent.y() );
  }

  static double changeForScreenDrag( const QPointF &screenDrag, const QPointF &screenTangent )
  {
    const double denom = QPointF::dotProduct( screenTangent, screenTangent );
    return denom > 1e-6 ? QPointF::dotProduct( screenDrag, screenTangent ) / denom : 0.0;
  }
};

inline QVector<Segment3D> ringSegments3D( double radius, double z, int segments = 12 )
{
  QVector<Segment3D> ring;
  if ( radius <= 0 || segments < 3 )
  {
    ring.append( { QVector3D( 0, 0, float( z ) ), QVector3D( 0, 0, float( z ) ) } );
    return ring;
  }
  const double twoPi = 2.0 * std::acos( -1.0 );
  for ( int i = 0; i < segments; ++i )
  {
    const double a0 = twoPi * i / segments;
    const double a1 = twoPi * ( i + 1 ) / segments;
    ring.append( { QVector3D( float( radius * std::cos( a0 ) ), float( radius * std::sin( a0 ) ), float( z ) ),
                   QVector3D( float( radius * std::cos( a1 ) ), float( radius * std::sin( a1 ) ), float( z ) ) } );
  }
  return ring;
}

inline QVector<Segment3D> boxLoop3D( double x0, double y0, double x1, double y1, double z )
{
  const QVector3D p00{ float( x0 ), float( y0 ), float( z ) };
  const QVector3D p10{ float( x1 ), float( y0 ), float( z ) };
  const QVector3D p11{ float( x1 ), float( y1 ), float( z ) };
  const QVector3D p01{ float( x0 ), float( y1 ), float( z ) };
  return { { p00, p10 }, { p10, p11 }, { p11, p01 }, { p01, p00 } };
}

inline QVector<Handle3D> buildHandles3D(
  const QString &primitive,
  const std::array<double, 3> &dimensions,
  double wallFraction,
  double ridgeFraction,
  const QSizeF &roofTop,
  const QPointF &roofTopPosition,
  const QPointF &notch,
  double recessDepth,
  const QVector<double> &profileParameters,
  double cameraAzimuthRadians = 0.0 )
{
  QVector<Handle3D> handles;
  const char *anchoredEdge = QT_TRANSLATE_NOOP( "ParamModelerDock", "Move this edge with the opposite edge fixed" );
  const char *roofFixedEaves = QT_TRANSLATE_NOOP( "ParamModelerDock", "Roof height with eaves fixed" );
  const auto type = profileType( primitive );

  if ( type != ProfileType::None )
  {
    if ( profileParameters.isEmpty() ) return handles;
    const auto &v = profileParameters;
    const QVector3D radial( float( std::cos( cameraAzimuthRadians ) ), float( std::sin( cameraAzimuthRadians ) ), 0.0f );
    const QVector3D up( 0.0f, 0.0f, 1.0f );
    const auto addProfile = [&]( int param, bool movesMin, const QVector3D &pos, const QVector3D &tan,
                                 const char *label, QVector<Segment3D> moving, QVector<Segment3D> fixed ) {
      handles.append( { Handle3DKind::ProfileParameter, param, movesMin, pos, tan, label, std::move( moving ), std::move( fixed ) } );
    };

    switch ( type )
    {
      case ProfileType::Cylinder:
      case ProfileType::Cone:
      case ProfileType::Dome:
      {
        const double radius = v[0], height = v[1];
        const double cylinder = type == ProfileType::Cylinder ? height : height * v[2];
        const auto topEaveRing = ringSegments3D( radius, cylinder );
        const auto baseRing = ringSegments3D( radius, 0.0 );
        addProfile( 0, false, radial * float( radius ) + QVector3D( 0, 0, float( cylinder * 0.5 ) ),
                    radial, "Radius:", topEaveRing, { { QVector3D( 0, 0, 0 ), QVector3D( 0, 0, float( height ) ) } } );
        const bool flat = type == ProfileType::Cylinder;
        const QVector3D apex( 0, 0, float( height ) );
        addProfile( 1, false, apex, up, flat ? "Total height:" : roofFixedEaves,
                    flat ? topEaveRing : QVector<Segment3D>{ { apex, apex } },
                    flat ? baseRing : topEaveRing );
        if ( !flat )
        {
          addProfile( 2, false, -radial * float( radius ) + QVector3D( 0, 0, float( cylinder ) ),
                      up * float( height ), "Cylinder height at fixed total height",
                      topEaveRing, { { apex, apex } } );
        }
        if ( type == ProfileType::Dome && v.size() >= 4 )
        {
          const double domeHeight = height - cylinder;
          const double midRadius = radius * ( 0.875 - 0.375 * v[3] );
          const double midHeight = cylinder + domeHeight * ( 0.4625 + 0.1875 * v[3] );
          addProfile( 3, false, radial * float( midRadius ) + QVector3D( 0, 0, float( midHeight ) ),
                      radial * float( -0.375 * radius ) + up * float( 0.1875 * domeHeight ),
                      "Bulge:", ringSegments3D( midRadius, midHeight ), topEaveRing );
        }
        break;
      }
      case ProfileType::Barrel:
      {
        const double length = v[0], width = v[1], wall = v[2];
        const Segment3D xMaxEdge{ QVector3D( float( length ), 0, float( wall ) ), QVector3D( float( length ), float( width ), float( wall ) ) };
        const Segment3D xMinEdge{ QVector3D( 0, 0, float( wall ) ), QVector3D( 0, float( width ), float( wall ) ) };
        const Segment3D yMaxEdge{ QVector3D( 0, float( width ), float( wall ) ), QVector3D( float( length ), float( width ), float( wall ) ) };
        const Segment3D yMinEdge{ QVector3D( 0, 0, float( wall ) ), QVector3D( float( length ), 0, float( wall ) ) };
        addProfile( 0, false, QVector3D( float( length ), float( width * 0.5 ), float( wall * 0.5 ) ),
                    QVector3D( 1, 0, 0 ), anchoredEdge, { xMaxEdge }, { xMinEdge } );
        addProfile( 0, true, QVector3D( 0, float( width * 0.5 ), float( wall * 0.5 ) ),
                    QVector3D( -1, 0, 0 ), anchoredEdge, { xMinEdge }, { xMaxEdge } );
        addProfile( 1, false, QVector3D( float( length * 0.5 ), float( width ), float( wall * 0.5 ) ),
                    QVector3D( 0, 1, 0 ), anchoredEdge, { yMaxEdge }, { yMinEdge } );
        addProfile( 1, true, QVector3D( float( length * 0.5 ), 0, float( wall * 0.5 ) ),
                    QVector3D( 0, -1, 0 ), anchoredEdge, { yMinEdge }, { yMaxEdge } );
        addProfile( 2, false, QVector3D( float( length * 0.5 ), 0, float( wall ) ),
                    up, "Wall height:", { yMinEdge, yMaxEdge },
                    { { QVector3D( 0, 0, 0 ), QVector3D( float( length ), 0, 0 ) },
                      { QVector3D( 0, float( width ), 0 ), QVector3D( float( length ), float( width ), 0 ) } } );
        break;
      }
      case ProfileType::Tower:
      {
        const double body = v[BodyHeight], eaveTop = body + v[EaveThickness];
        const double roofTopZ = eaveTop + v[CurvedHeight];
        const double tipZ = roofTopZ + v[ConeHeight];
        const double radius = v[BodyRadius], eave = v[EaveRadius], cap = v[TopRadius];
        const auto eaveBottomRing = ringSegments3D( eave, body );
        const auto eaveTopRing = ringSegments3D( eave, eaveTop );
        const auto capRing = ringSegments3D( cap, roofTopZ );
        const QVector3D apex( 0, 0, float( tipZ ) );
        addProfile( BodyRadius, false, radial * float( radius ) + QVector3D( 0, 0, float( body * 0.5 ) ),
                    radial, "Base radius:", ringSegments3D( radius, body * 0.5 ), eaveTopRing );
        addProfile( BodyHeight, false, radial * float( eave ) + QVector3D( 0, 0, float( body ) ),
                    up, QT_TRANSLATE_NOOP( "ParamModelerDock", "Move lower eave edge with upper edge fixed" ),
                    eaveBottomRing, eaveTopRing );
        addProfile( EaveRadius, false, -radial * float( eave ) + QVector3D( 0, 0, float( ( body + eaveTop ) * 0.5 ) ),
                    -radial, "Eave outer radius:", eaveTopRing, ringSegments3D( radius, body ) );
        addProfile( EaveThickness, false, radial * float( eave * 0.5 ) + QVector3D( 0, 0, float( eaveTop ) ),
                    up, "Eave thickness:", eaveTopRing, eaveBottomRing );
        addProfile( CurvedHeight, false, QVector3D( 0, 0, float( roofTopZ ) ),
                    up, QT_TRANSLATE_NOOP( "ParamModelerDock", "Move cone base with tower apex fixed" ),
                    capRing, { { apex, apex } } );
        addProfile( TopRadius, false, radial * float( cap ) + QVector3D( 0, 0, float( roofTopZ ) ),
                    radial, "Middle top radius:", capRing, eaveTopRing );
        const double midRadius = ( eave + cap ) * 0.5;
        const double midZ = eaveTop + v[CurvedHeight] * 0.5 + v[CurvedRise];
        addProfile( CurvedRise, false, radial * float( midRadius ) + QVector3D( 0, 0, float( midZ ) ),
                    up, "Middle rise (m):", ringSegments3D( midRadius, midZ ), eaveTopRing );
        addProfile( ConeHeight, false, apex, up, "Cone height:", { { apex, apex } }, capRing );
        break;
      }
      case ProfileType::AngledGable:
      {
        const double length1 = v[0], length2 = v[1], width = v[2];
        const double height = v[3], wall = height * v[4];
        const double radians = std::acos( -1.0 ) / 180.0;
        const double turn = ( 180.0 - v[5] ) * radians;
        const QVector3D dir2( float( std::cos( turn ) ), float( std::sin( turn ) ), 0.0f );
        const QVector3D joint( float( length1 ), 0, 0 );
        const QVector3D end = joint + dir2 * float( length2 );
        const double ridgeY = width * v[6];
        const Segment3D firstEnd{ QVector3D( 0, 0, float( wall ) ), QVector3D( 0, float( width ), float( wall ) ) };
        const Segment3D seam{ QVector3D( float( length1 ), 0, float( wall ) ), QVector3D( float( length1 ), float( width ), float( wall ) ) };
        const Segment3D secondEnd{ end + QVector3D( 0, 0, float( wall ) ), end + QVector3D( 0, float( width ), float( wall ) ) };
        const Segment3D ridge1{ QVector3D( 0, float( ridgeY ), float( height ) ), QVector3D( float( length1 ), float( ridgeY ), float( height ) ) };
        const Segment3D eaveFront{ QVector3D( 0, 0, float( wall ) ), QVector3D( float( length1 ), 0, float( wall ) ) };
        const Segment3D eaveBack{ QVector3D( 0, float( width ), float( wall ) ), QVector3D( float( length1 ), float( width ), float( wall ) ) };
        addProfile( 0, false, QVector3D( float( length1 ), float( width * 0.5 ), float( wall ) ),
                    QVector3D( 1, 0, 0 ), "House 1 length:", { seam }, { firstEnd } );
        addProfile( 0, true, QVector3D( 0, float( width * 0.5 ), float( wall * 0.5 ) ),
                    QVector3D( -1, 0, 0 ), QT_TRANSLATE_NOOP( "ParamModelerDock", "Move first end with second house fixed" ),
                    { firstEnd }, { seam } );
        addProfile( 1, false, end + QVector3D( 0, float( width * 0.5 ), float( wall * 0.5 ) ),
                    dir2, "House 2 length:", { secondEnd }, { seam } );
        addProfile( 2, false, QVector3D( float( length1 * 0.5 ), float( width ), float( wall * 0.5 ) ),
                    QVector3D( 0, 1, 0 ), "Width:", { eaveBack }, { eaveFront } );
        addProfile( 3, false, QVector3D( float( length1 * 0.5 ), float( ridgeY ), float( height ) ),
                    up, roofFixedEaves, { ridge1 }, { eaveFront, eaveBack } );
        addProfile( 4, false, QVector3D( float( length1 * 0.5 ), 0, float( wall ) ),
                    up * float( height ), "Wall height at fixed total height", { eaveFront, eaveBack }, { ridge1 } );
        addProfile( 5, false, end + QVector3D( 0, float( width ), float( wall ) ),
                    QVector3D( float( std::sin( turn ) * length2 * radians ), float( -std::cos( turn ) * length2 * radians ), 0.0f ),
                    "Angle:", { secondEnd }, { seam } );
        addProfile( 6, false, QVector3D( float( length1 * 0.25 ), float( ridgeY ), float( height ) ),
                    QVector3D( 0, float( width ), 0 ), "Ridge ratio:", { ridge1 }, { eaveFront, eaveBack } );
        break;
      }
      case ProfileType::None:
        break;
    }
    return handles;
  }

  const double length = dimensions[0], width = dimensions[1], height = dimensions[2];
  if ( length <= 0 || width <= 0 || height <= 0 ) return handles;
  const bool lShaped = primitive == QLatin1String( "LShapedHouse" );
  const bool gable = primitive == QLatin1String( "GableRoofHouse" );
  const bool pyramid = primitive == QLatin1String( "PyramidalRoofHouse" );
  const bool flatHip = primitive == QLatin1String( "FlatTopHipRoofHouse" );
  const bool recessed = primitive == QLatin1String( "RecessedRoofHouse" );
  const bool shortRidge = primitive == QLatin1String( "ShortRidgeHipRoofHouse" );
  const bool sloped = gable || pyramid || flatHip || shortRidge;
  const double wallZ = sloped ? height * wallFraction : height;
  const double notchX = lShaped ? length * ( 1.0 - notch.x() ) : length;
  const double notchY = lShaped ? width * notch.y() : width;

  const Segment3D xMaxEdge{ QVector3D( float( length ), 0, float( wallZ ) ),
                            QVector3D( float( length ), float( lShaped ? notchY : width ), float( wallZ ) ) };
  const Segment3D xMinEdge{ QVector3D( 0, 0, float( wallZ ) ), QVector3D( 0, float( width ), float( wallZ ) ) };
  const Segment3D yMaxEdge{ QVector3D( 0, float( width ), float( wallZ ) ),
                            QVector3D( float( lShaped ? notchX : length ), float( width ), float( wallZ ) ) };
  const Segment3D yMinEdge{ QVector3D( 0, 0, float( wallZ ) ), QVector3D( float( length ), 0, float( wallZ ) ) };

  // 4 outer wall handles (keep opposite wall fixed).
  handles.append( { Handle3DKind::ExteriorAxis, 0, false,
                    QVector3D( float( length ), float( ( lShaped ? notchY : width ) * 0.5 ), float( wallZ * 0.5 ) ),
                    QVector3D( 1, 0, 0 ), anchoredEdge, { xMaxEdge }, { xMinEdge } } );
  handles.append( { Handle3DKind::ExteriorAxis, 0, true,
                    QVector3D( 0, float( width * 0.5 ), float( wallZ * 0.5 ) ),
                    QVector3D( -1, 0, 0 ), anchoredEdge, { xMinEdge }, { xMaxEdge } } );
  handles.append( { Handle3DKind::ExteriorAxis, 1, false,
                    QVector3D( float( ( lShaped ? notchX : length ) * 0.5 ), float( width ), float( wallZ * 0.5 ) ),
                    QVector3D( 0, 1, 0 ), anchoredEdge, { yMaxEdge }, { yMinEdge } } );
  handles.append( { Handle3DKind::ExteriorAxis, 1, true,
                    QVector3D( float( length * 0.5 ), 0, float( wallZ * 0.5 ) ),
                    QVector3D( 0, -1, 0 ), anchoredEdge, { yMinEdge }, { yMaxEdge } } );

  const auto eaveLoop = boxLoop3D( 0, 0, length, width, wallZ );
  const auto groundLoop = boxLoop3D( 0, 0, length, width, 0.0 );

  // Top / roof height handle & bottom ground handle.
  QVector3D topHandlePos( float( length * 0.5 ), float( width * 0.5 ), float( height ) );
  QVector<Segment3D> topMoving = eaveLoop;
  if ( gable )
  {
    const double ridgeY = width * ridgeFraction;
    topHandlePos = QVector3D( float( length * 0.5 ), float( ridgeY ), float( height ) );
    topMoving = { { QVector3D( 0, float( ridgeY ), float( height ) ), QVector3D( float( length ), float( ridgeY ), float( height ) ) } };
  }
  else if ( pyramid )
  {
    topMoving = { { topHandlePos, topHandlePos } };
  }
  else if ( flatHip )
  {
    const double x0 = ( length - roofTop.width() ) * roofTopPosition.x();
    const double y0 = ( width - roofTop.height() ) * roofTopPosition.y();
    topHandlePos = QVector3D( float( x0 + roofTop.width() * 0.5 ), float( y0 + roofTop.height() * 0.5 ), float( height ) );
    topMoving = boxLoop3D( x0, y0, x0 + roofTop.width(), y0 + roofTop.height(), height );
  }
  else if ( shortRidge )
  {
    const double x0 = ( length - roofTop.width() ) * roofTopPosition.x();
    const double ridgeY = width * ridgeFraction;
    topHandlePos = QVector3D( float( x0 + roofTop.width() * 0.5 ), float( ridgeY ), float( height ) );
    topMoving = { { QVector3D( float( x0 ), float( ridgeY ), float( height ) ),
                    QVector3D( float( x0 + roofTop.width() ), float( ridgeY ), float( height ) ) } };
  }
  else if ( recessed )
  {
    topHandlePos = QVector3D( float( length * 0.5 ), 0, float( height ) );
  }
  handles.append( { Handle3DKind::ExteriorAxis, 2, false, topHandlePos, QVector3D( 0, 0, 1 ),
                    sloped ? roofFixedEaves : anchoredEdge, topMoving, sloped ? eaveLoop : groundLoop } );
  handles.append( { Handle3DKind::ExteriorAxis, 2, true, QVector3D( float( length * 0.5 ), 0, 0 ),
                    QVector3D( 0, 0, -1 ), anchoredEdge, groundLoop, sloped ? eaveLoop : topMoving } );

  if ( sloped )
  {
    handles.append( { Handle3DKind::WallRatio, 2, false, QVector3D( float( length * 0.5 ), 0, float( wallZ ) ),
                      QVector3D( 0, 0, float( height ) ), "Wall height at fixed total height", eaveLoop, topMoving } );
  }

  if ( gable )
  {
    const double ridgeY = width * ridgeFraction;
    handles.append( { Handle3DKind::GableRidge, 0, false, QVector3D( float( length * 0.25 ), float( ridgeY ), float( height ) ),
                      QVector3D( 0, float( width ), 0 ), "Ridge ratio:", topMoving, { yMinEdge, yMaxEdge } } );
  }
  else if ( lShaped )
  {
    const Segment3D innerX{ QVector3D( float( notchX ), float( notchY ), float( height ) ),
                            QVector3D( float( notchX ), float( width ), float( height ) ) };
    const Segment3D innerY{ QVector3D( float( notchX ), float( notchY ), float( height ) ),
                            QVector3D( float( length ), float( notchY ), float( height ) ) };
    handles.append( { Handle3DKind::NotchX, 0, false,
                      QVector3D( float( notchX ), float( ( notchY + width ) * 0.5 ), float( height ) ),
                      QVector3D( float( -length ), 0, 0 ), anchoredEdge, { innerX }, { xMinEdge, xMaxEdge } } );
    handles.append( { Handle3DKind::NotchY, 1, false,
                      QVector3D( float( ( notchX + length ) * 0.5 ), float( notchY ), float( height ) ),
                      QVector3D( 0, float( width ), 0 ), anchoredEdge, { innerY }, { yMinEdge, yMaxEdge } } );
  }
  else if ( flatHip || recessed )
  {
    const double x0 = ( length - roofTop.width() ) * roofTopPosition.x();
    const double y0 = ( width - roofTop.height() ) * roofTopPosition.y();
    const double x1 = x0 + roofTop.width(), y1 = y0 + roofTop.height();
    const Segment3D pXMin{ QVector3D( float( x0 ), float( y0 ), float( height ) ), QVector3D( float( x0 ), float( y1 ), float( height ) ) };
    const Segment3D pXMax{ QVector3D( float( x1 ), float( y0 ), float( height ) ), QVector3D( float( x1 ), float( y1 ), float( height ) ) };
    const Segment3D pYMin{ QVector3D( float( x0 ), float( y0 ), float( height ) ), QVector3D( float( x1 ), float( y0 ), float( height ) ) };
    const Segment3D pYMax{ QVector3D( float( x0 ), float( y1 ), float( height ) ), QVector3D( float( x1 ), float( y1 ), float( height ) ) };
    handles.append( { Handle3DKind::PlatformX, 0, false, QVector3D( float( x1 ), float( ( y0 + y1 ) * 0.5 ), float( height ) ),
                      QVector3D( 1, 0, 0 ), anchoredEdge, { pXMax }, { pXMin } } );
    handles.append( { Handle3DKind::PlatformX, 0, true, QVector3D( float( x0 ), float( ( y0 + y1 ) * 0.5 ), float( height ) ),
                      QVector3D( -1, 0, 0 ), anchoredEdge, { pXMin }, { pXMax } } );
    handles.append( { Handle3DKind::PlatformY, 1, false, QVector3D( float( ( x0 + x1 ) * 0.5 ), float( y1 ), float( height ) ),
                      QVector3D( 0, 1, 0 ), anchoredEdge, { pYMax }, { pYMin } } );
    handles.append( { Handle3DKind::PlatformY, 1, true, QVector3D( float( ( x0 + x1 ) * 0.5 ), float( y0 ), float( height ) ),
                      QVector3D( 0, -1, 0 ), anchoredEdge, { pYMin }, { pYMax } } );
    if ( recessed )
    {
      const double floorZ = height - recessDepth;
      handles.append( { Handle3DKind::RecessDepth, 2, false,
                        QVector3D( float( ( x0 + x1 ) * 0.5 ), float( ( y0 + y1 ) * 0.5 ), float( floorZ ) ),
                        QVector3D( 0, 0, -1 ), "Recess depth:",
                        boxLoop3D( x0, y0, x1, y1, floorZ ), boxLoop3D( x0, y0, x1, y1, height ) } );
    }
  }
  else if ( shortRidge )
  {
    const double x0 = ( length - roofTop.width() ) * roofTopPosition.x();
    const double x1 = x0 + roofTop.width();
    const double ridgeY = width * ridgeFraction;
    const QVector3D r0{ float( x0 ), float( ridgeY ), float( height ) };
    const QVector3D r1{ float( x1 ), float( ridgeY ), float( height ) };
    handles.append( { Handle3DKind::ShortRidgeEnd, 0, false, r1, QVector3D( 1, 0, 0 ),
                      anchoredEdge, { { r0, r1 } }, { { r0, r0 } } } );
    handles.append( { Handle3DKind::ShortRidgeEnd, 0, true, r0, QVector3D( -1, 0, 0 ),
                      anchoredEdge, { { r0, r1 } }, { { r1, r1 } } } );
    handles.append( { Handle3DKind::GableRidge, 1, false, QVector3D( float( x0 + roofTop.width() * 0.25 ), float( ridgeY ), float( height ) ),
                      QVector3D( 0, float( width ), 0 ), "Ridge ratio:", { { r0, r1 } }, { yMinEdge, yMaxEdge } } );
  }
  return handles;
}

inline int nearestHandle3D( const QVector<Handle3D> &handles, const QPointF &cursor,
                            const QMatrix4x4 &modelToWorld, const QMatrix4x4 &viewMatrix,
                            const QMatrix4x4 &projectionMatrix, const QSize &viewport,
                            double maxPixelDistance = 10.0 )
{
  int bestIndex = -1;
  double bestDistance = maxPixelDistance;
  double bestDepth = std::numeric_limits<double>::max();
  for ( int i = 0; i < handles.size(); ++i )
  {
    QPointF screenPos, screenTangent;
    double depth = 0;
    if ( !handles[i].projectToScreen( modelToWorld, viewMatrix, projectionMatrix,
                                      viewport, screenPos, screenTangent, depth ) )
      continue;
    if ( QPointF::dotProduct( screenTangent, screenTangent ) <= 1e-4 )
      continue;
    const double distance = std::hypot( cursor.x() - screenPos.x(), cursor.y() - screenPos.y() );
    if ( distance < bestDistance - 0.5 || ( std::abs( distance - bestDistance ) <= 0.5 && depth < bestDepth ) )
    {
      bestIndex = i;
      bestDistance = distance;
      bestDepth = depth;
    }
  }
  return bestIndex;
}
}

#endif
