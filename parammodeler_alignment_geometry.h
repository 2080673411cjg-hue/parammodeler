#ifndef PARAMMODELER_ALIGNMENT_GEOMETRY_H
#define PARAMMODELER_ALIGNMENT_GEOMETRY_H

#include <QMatrix4x4>
#include <QVector>
#include <QVector3D>

#include <algorithm>
#include <cmath>

namespace ParamModelerAlignmentGeometry
{
inline bool upperOffsetFromRotatedMesh( const QVector<QVector3D> &vertices,
                                        const QMatrix4x4 &rotation,
                                        const QVector3D &localUpper,
                                        QVector3D &offset )
{
  if ( vertices.isEmpty() )
    return false;

  QVector3D rotatedMin = rotation.map( vertices.first() );
  for ( const QVector3D &vertex : vertices )
  {
    const QVector3D p = rotation.map( vertex );
    rotatedMin.setX( std::min( rotatedMin.x(), p.x() ) );
    rotatedMin.setY( std::min( rotatedMin.y(), p.y() ) );
    rotatedMin.setZ( std::min( rotatedMin.z(), p.z() ) );
  }
  offset = rotation.map( localUpper ) - rotatedMin;
  return std::isfinite( offset.x() ) && std::isfinite( offset.y() ) &&
         std::isfinite( offset.z() );
}

inline bool bottomFootprintAgreesWithFull( const QVector3D &bottomMin, const QVector3D &bottomMax,
                                           const QVector3D &fullMin, const QVector3D &fullMax )
{
  const double length = fullMax.x() - fullMin.x();
  const double width = fullMax.y() - fullMin.y();
  if ( length <= 0.0 || width <= 0.0 )
    return false;

  const double xTolerance = std::max( 0.05, length * 0.03 );
  const double yTolerance = std::max( 0.05, width * 0.03 );
  return std::abs( bottomMin.x() - fullMin.x() ) <= xTolerance &&
         std::abs( bottomMax.x() - fullMax.x() ) <= xTolerance &&
         std::abs( bottomMin.y() - fullMin.y() ) <= yTolerance &&
         std::abs( bottomMax.y() - fullMax.y() ) <= yTolerance;
}

inline bool supportedFlatRoofTop( const QVector<QVector3D> &points, double &topZ )
{
  QVector<double> heights;
  heights.reserve( points.size() );
  for ( const QVector3D &p : points )
    if ( std::isfinite( p.z() ) )
      heights.append( p.z() );
  if ( heights.size() < 80 )
    return false;

  std::sort( heights.begin(), heights.end() );
  const double low = heights[static_cast<int>( heights.size() * 0.05 )];
  const double high = heights[static_cast<int>( heights.size() * 0.95 )];
  if ( high - low <= 0.01 )
    return false;

  constexpr int bins = 32;
  const double step = ( high - low ) / bins;
  int counts[bins] = {};
  for ( const double z : heights )
  {
    if ( z < low || z > high )
      continue;
    const int index = std::min( bins - 1, static_cast<int>( ( z - low ) / step ) );
    if ( index >= bins / 2 )
      ++counts[index];
  }

  int bestBin = bins / 2;
  for ( int i = bestBin + 1; i < bins; ++i )
    if ( counts[i] > counts[bestBin] )
      bestBin = i;

  const double center = low + ( bestBin + 0.5 ) * step;
  QVector<double> support;
  for ( const double z : heights )
    if ( std::abs( z - center ) <= step * 2.0 )
      support.append( z );
  if ( support.size() < std::max( 24, static_cast<int>( heights.size() * 0.08 ) ) )
    return false;

  topZ = support[support.size() / 2];
  return true;
}

inline bool firstGableEave( const QVector<QVector3D> &points,
                            const QVector3D &footprintMin, const QVector3D &footprintMax,
                            double baseZ, double totalHeight, double &eaveZ )
{
  const double width = footprintMax.y() - footprintMin.y();
  if ( width <= 0.0 || totalHeight <= 0.0 )
    return false;

  // In the canonical frame, only the first house has an exterior wall at min Y.
  const double wallBand = std::max( 0.01, width * 0.008 );
  QVector<float> wallZ;
  for ( const QVector3D &p : points )
    if ( std::abs( p.y() - footprintMin.y() ) <= wallBand )
      wallZ.append( p.z() );
  if ( wallZ.size() < std::max( 24, static_cast<int>( points.size() * 0.02 ) ) )
    return false;

  std::sort( wallZ.begin(), wallZ.end() );
  const double low = wallZ[static_cast<int>( wallZ.size() * 0.10 )];
  const double high = wallZ[static_cast<int>( wallZ.size() * 0.95 )];
  if ( low > baseZ + totalHeight * 0.35 || high < baseZ + totalHeight * 0.15 )
    return false;
  eaveZ = high;
  return std::isfinite( eaveZ );
}

inline bool halfCylinderEave( const QVector<QVector3D> &points,
                              const QVector3D &footprintMin, const QVector3D &footprintMax,
                              double baseZ, double totalHeight, double &eaveZ )
{
  const double length = footprintMax.x() - footprintMin.x();
  const double width = footprintMax.y() - footprintMin.y();
  if ( length <= 0.0 || width <= 0.0 || totalHeight <= 0.0 )
    return false;

  const double radius = width * 0.5;
  const double centerY = ( footprintMin.y() + footprintMax.y() ) * 0.5;
  QVector<double> estimates;
  for ( const QVector3D &p : points )
  {
    const double x = p.x();
    const double y = p.y();
    if ( x <= footprintMin.x() + length * 0.10 || x >= footprintMax.x() - length * 0.10 ||
         y <= footprintMin.y() + width * 0.12 || y >= footprintMax.y() - width * 0.12 ||
         p.z() <= baseZ + totalHeight * 0.35 )
      continue;

    const double offset = y - centerY;
    const double arcHeight = std::sqrt( std::max( 0.0, radius * radius - offset * offset ) );
    estimates.append( p.z() - arcHeight );
  }
  if ( estimates.size() < std::max( 24, static_cast<int>( points.size() * 0.03 ) ) )
    return false;

  std::sort( estimates.begin(), estimates.end() );
  const double median = estimates[estimates.size() / 2];
  const double q10 = estimates[static_cast<int>( estimates.size() * 0.10 )];
  const double q90 = estimates[static_cast<int>( estimates.size() * 0.90 )];
  if ( !std::isfinite( median ) || q90 - q10 > std::max( 0.10, radius * 0.08 ) ||
       median <= baseZ || median >= baseZ + totalHeight )
    return false;

  eaveZ = median;
  return true;
}
}

#endif // PARAMMODELER_ALIGNMENT_GEOMETRY_H
