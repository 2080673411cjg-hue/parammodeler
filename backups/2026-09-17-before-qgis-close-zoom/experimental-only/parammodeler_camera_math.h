#ifndef PARAMMODELER_CAMERA_MATH_H
#define PARAMMODELER_CAMERA_MATH_H

#include <algorithm>
#include <cmath>

namespace ParamModelerCamera
{
inline double minimumDistance( double diagonal )
{
  return std::isfinite( diagonal ) ? std::clamp( diagonal * 0.001, 0.005, 0.1 ) : 0.1;
}

inline double framingDistance( double diagonal, double verticalFovDegrees, double aspect )
{
  const double halfVertical = std::clamp( verticalFovDegrees, 1.0, 170.0 ) * 0.008726646259971648;
  const double halfHorizontal = std::atan( std::tan( halfVertical ) * std::max( aspect, 0.01 ) );
  const double halfAngle = std::min( halfVertical, halfHorizontal );
  return std::max( minimumDistance( diagonal ), diagonal * 0.55 / std::sin( halfAngle ) );
}
}

#endif
