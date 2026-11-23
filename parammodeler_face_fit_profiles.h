#ifndef PARAMMODELER_FACE_FIT_PROFILES_H
#define PARAMMODELER_FACE_FIT_PROFILES_H

#include "parammodeler_fit_metrics.h"
#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <QString>
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
}

#endif
