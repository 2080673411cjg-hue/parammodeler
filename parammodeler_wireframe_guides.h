#ifndef PARAMMODELER_WIREFRAME_GUIDES_H
#define PARAMMODELER_WIREFRAME_GUIDES_H

#include <QVector3D>

#include <algorithm>
#include <cmath>

namespace ParamModelerWireframeGuides
{
inline bool sparseCircularAngle( double angle )
{
  constexpr double twoPi = 6.28318530717958647692;
  constexpr double step = twoPi / 8.0;
  if ( angle < 0.0 )
    angle += twoPi;
  const double nearest = std::round( angle / step ) * step;
  return std::abs( angle - nearest ) < 0.001 || std::abs( angle - nearest + twoPi ) < 0.001;
}

inline bool circularCapGuide( const QVector3D &a, const QVector3D &b, double minZ )
{
  if ( std::abs( a.z() - b.z() ) > 0.001 || std::abs( a.z() - minZ ) < 0.001 )
    return false;

  const double r0 = std::hypot( a.x(), a.y() );
  const double r1 = std::hypot( b.x(), b.y() );
  const QVector3D rim = r0 > r1 ? a : b;
  const double rimRadius = std::max( r0, r1 );
  const double centerRadius = std::min( r0, r1 );
  return rimRadius >= 0.001
         && centerRadius <= std::max( 0.002, rimRadius * 0.03 )
         && sparseCircularAngle( std::atan2( rim.y(), rim.x() ) );
}

inline bool circularMeridianGuide( const QVector3D &a, const QVector3D &b, double minZ )
{
  if ( std::abs( a.z() - b.z() ) < 0.001 )
    return false;

  const double r0 = std::hypot( a.x(), a.y() );
  const double r1 = std::hypot( b.x(), b.y() );
  if ( r0 < 0.001 || r1 < 0.001 )
  {
    const QVector3D center = r0 < r1 ? a : b;
    const QVector3D rim = r0 < r1 ? b : a;
    return std::hypot( rim.x(), rim.y() ) >= 0.001
           && center.z() > minZ + 0.001
           && sparseCircularAngle( std::atan2( rim.y(), rim.x() ) );
  }

  const double angle0 = std::atan2( a.y(), a.x() );
  const double angle1 = std::atan2( b.y(), b.x() );
  const double delta = std::atan2( std::sin( angle1 - angle0 ), std::cos( angle1 - angle0 ) );
  return std::abs( delta ) < 0.001 && sparseCircularAngle( angle0 );
}

inline bool circularRingGuide( const QVector3D &a, const QVector3D &b,
                               double radius, double z )
{
  const double tolerance = std::max( 0.002, radius * 0.001 );
  return std::abs( a.z() - z ) < 0.001 && std::abs( b.z() - z ) < 0.001
         && std::abs( std::hypot( a.x(), a.y() ) - radius ) < tolerance
         && std::abs( std::hypot( b.x(), b.y() ) - radius ) < tolerance;
}

inline bool domeInteriorRing( const QVector3D &a, const QVector3D &b, double baseZ )
{
  return std::abs( a.z() - b.z() ) < 0.001
         && std::min( a.z(), b.z() ) > baseZ + 0.001;
}

inline bool joinedGableRoofGuide( const QVector3D &a, const QVector3D &b,
                                  double minZ, double maxZ )
{
  const bool aAtRidge = std::abs( a.z() - maxZ ) < 0.001;
  const bool bAtRidge = std::abs( b.z() - maxZ ) < 0.001;
  if ( aAtRidge && bAtRidge )
    return true;
  return aAtRidge != bAtRidge && std::abs( a.x() - b.x() ) < 0.001
         && std::min( a.z(), b.z() ) > minZ + 0.001;
}

inline bool halfCylinderRidgeGuide( const QVector3D &a, const QVector3D &b,
                                    double centerY, double maxZ )
{
  return std::abs( a.x() - b.x() ) >= 0.001
         && std::abs( a.y() - centerY ) < 0.001
         && std::abs( b.y() - centerY ) < 0.001
         && std::abs( a.z() - maxZ ) < 0.001
         && std::abs( b.z() - maxZ ) < 0.001;
}

inline bool halfCylinderEaveGuide( const QVector3D &a, const QVector3D &b,
                                   double minY, double maxY, double maxZ )
{
  const double eaveZ = maxZ - ( maxY - minY ) * 0.5;
  const bool atSameSide =
    ( std::abs( a.y() - minY ) < 0.001 && std::abs( b.y() - minY ) < 0.001 ) ||
    ( std::abs( a.y() - maxY ) < 0.001 && std::abs( b.y() - maxY ) < 0.001 );
  return std::abs( a.x() - b.x() ) >= 0.001 && atSameSide
         && std::abs( a.z() - eaveZ ) < 0.001
         && std::abs( b.z() - eaveZ ) < 0.001;
}

inline bool halfCylinderArcGuide( const QVector3D &a, const QVector3D &b,
                                  double minY, double maxY, double maxZ )
{
  const double radius = ( maxY - minY ) * 0.5;
  const double centerY = ( minY + maxY ) * 0.5;
  const double baseZ = maxZ - radius;
  const double tolerance = std::max( 0.002, radius * 0.001 );
  const double r0 = std::hypot( a.y() - centerY, a.z() - baseZ );
  const double r1 = std::hypot( b.y() - centerY, b.z() - baseZ );
  if ( std::abs( a.x() - b.x() ) >= 0.001 )
    return std::abs( a.y() - b.y() ) < 0.001 && std::abs( a.z() - b.z() ) < 0.001
           && std::abs( r0 - radius ) < tolerance && a.z() >= baseZ - 0.001
           && sparseCircularAngle( std::atan2( a.z() - baseZ, a.y() - centerY ) );

  const QVector3D rim = r0 > r1 ? a : b;
  return std::min( r0, r1 ) < tolerance && std::abs( std::max( r0, r1 ) - radius ) < tolerance
         && rim.z() >= baseZ - 0.001
         && sparseCircularAngle( std::atan2( rim.z() - baseZ, rim.y() - centerY ) );
}

inline bool towerEaveConnector( const QVector3D &a, const QVector3D &b,
                                double baseRadius, double bodyTopZ, double outerRadius )
{
  if ( std::abs( a.z() - bodyTopZ ) >= 0.001 || std::abs( b.z() - bodyTopZ ) >= 0.001 )
    return false;
  const double r0 = std::hypot( a.x(), a.y() );
  const double r1 = std::hypot( b.x(), b.y() );
  const double tolerance = std::max( 0.002, baseRadius * 0.001 );
  const double angle0 = std::atan2( a.y(), a.x() );
  const double angle1 = std::atan2( b.y(), b.x() );
  const double delta = std::atan2( std::sin( angle1 - angle0 ), std::cos( angle1 - angle0 ) );
  return std::abs( std::min( r0, r1 ) - baseRadius ) < tolerance
         && std::abs( std::max( r0, r1 ) - outerRadius ) < tolerance
         && std::abs( delta ) < 0.001 && sparseCircularAngle( angle0 );
}

inline bool towerInnerEaveRing( const QVector3D &a, const QVector3D &b,
                                double baseRadius, double bodyTopZ, double eaveTopZ )
{
  if ( std::abs( a.z() - b.z() ) >= 0.001 )
    return false;
  if ( std::abs( a.z() - bodyTopZ ) >= 0.001 && std::abs( a.z() - eaveTopZ ) >= 0.001 )
    return false;

  const double radiusTolerance = std::max( 0.002, baseRadius * 0.001 );
  return std::abs( std::hypot( a.x(), a.y() ) - baseRadius ) < radiusTolerance
         && std::abs( std::hypot( b.x(), b.y() ) - baseRadius ) < radiusTolerance;
}
}

#endif // PARAMMODELER_WIREFRAME_GUIDES_H
