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
  constexpr double step = twoPi / 4.0;
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

inline bool halfCylinderRidgeGuide( const QVector3D &a, const QVector3D &b,
                                    double centerY, double maxZ )
{
  return std::abs( a.x() - b.x() ) >= 0.001
         && std::abs( a.y() - centerY ) < 0.001
         && std::abs( b.y() - centerY ) < 0.001
         && std::abs( a.z() - maxZ ) < 0.001
         && std::abs( b.z() - maxZ ) < 0.001;
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
