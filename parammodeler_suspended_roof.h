/***************************************************************************
  parammodeler_suspended_roof.h
  Z-gap detection & height reconstruction for suspended airborne LiDAR roofs
  -------------------
         begin                : November 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_SUSPENDED_ROOF_H
#define PARAMMODELER_SUSPENDED_ROOF_H

#include <QMap>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <cmath>

namespace ParamModelerSuspendedRoof
{

enum class CloudVerticalProfile
{
  Normal,
  FlatKeepGround,
  DetachedBottomStrip,
  SuspendedRoofOnly
};

struct Analysis
{
  CloudVerticalProfile profile = CloudVerticalProfile::Normal;
  double originalMinZ = 0.0;
  double originalMaxZ = 0.0;
  double gapBottomZ = 0.0;
  double gapTopZ = 0.0;
  double eaveZ = 0.0;
  double roofTopZ = 0.0;
  double roofHeight = 0.0;
  double gapFraction = 0.0;
  double belowFraction = 0.0;
  double aboveFraction = 0.0;
  int belowCount = 0;
  int aboveCount = 0;

  bool shouldStripBottom() const
  {
    return profile == CloudVerticalProfile::DetachedBottomStrip ||
           profile == CloudVerticalProfile::SuspendedRoofOnly;
  }

  bool isSuspendedRoofOnly() const
  {
    return profile == CloudVerticalProfile::SuspendedRoofOnly;
  }
};

inline double quantileSorted( const QVector<double> &sorted, double q )
{
  if ( sorted.isEmpty() )
    return 0.0;
  if ( sorted.size() == 1 )
    return sorted.first();
  const double pos = std::clamp( q, 0.0, 1.0 ) * static_cast<double>( sorted.size() - 1 );
  const int lo = static_cast<int>( std::floor( pos ) );
  const int hi = std::min( sorted.size() - 1, lo + 1 );
  const double frac = pos - static_cast<double>( lo );
  return sorted[lo] + frac * ( sorted[hi] - sorted[lo] );
}

inline Analysis analyze( const QVector<QVector3D> &points, bool isNormalized )
{
  Analysis result;
  if ( isNormalized || points.size() < 200 )
    return result;

  double zMin = 0.0;
  double zMax = 0.0;
  bool hasFinite = false;
  for ( const QVector3D &p : points )
  {
    if ( !std::isfinite( p.x() ) || !std::isfinite( p.y() ) || !std::isfinite( p.z() ) )
      continue;
    const double z = static_cast<double>( p.z() );
    if ( !hasFinite )
    {
      zMin = zMax = z;
      hasFinite = true;
    }
    else
    {
      zMin = std::min( zMin, z );
      zMax = std::max( zMax, z );
    }
  }
  const double height = zMax - zMin;
  if ( !hasFinite || height < 2.0 )
    return result;

  result.originalMinZ = zMin;
  result.originalMaxZ = zMax;

  constexpr int kBins = 24;
  std::array<int, kBins> counts{};
  int validCount = 0;
  for ( const QVector3D &p : points )
  {
    if ( !std::isfinite( p.z() ) )
      continue;
    const double norm = ( static_cast<double>( p.z() ) - zMin ) / height;
    const int bin = std::clamp( static_cast<int>( std::floor( norm * kBins ) ), 0, kBins - 1 );
    ++counts[bin];
    ++validCount;
  }
  if ( validCount < 200 )
    return result;

  const int sparseThresh = std::max( 2, static_cast<int>( std::floor( validCount * 0.005 ) ) );
  int bestLen = 0;
  int bestStart = -1;
  int bestEnd = -1;
  int curStart = -1;
  for ( int i = 0; i < kBins; ++i )
  {
    if ( counts[i] <= sparseThresh )
    {
      if ( curStart < 0 )
        curStart = i;
      const int len = i - curStart + 1;
      if ( len > bestLen )
      {
        bestLen = len;
        bestStart = curStart;
        bestEnd = i;
      }
    }
    else
    {
      curStart = -1;
    }
  }

  if ( bestLen <= 0 || bestStart <= 0 || bestEnd >= kBins - 1 )
    return result;

  int belowSum = 0;
  for ( int i = 0; i < bestStart; ++i )
    belowSum += counts[i];

  const double gapFraction = static_cast<double>( bestLen ) / static_cast<double>( kBins );
  const double belowFraction = static_cast<double>( belowSum ) / static_cast<double>( validCount );
  const double gapBottomZ = zMin + height * ( static_cast<double>( bestStart ) / kBins );
  const double gapTopZ = zMin + height * ( static_cast<double>( bestEnd + 1 ) / kBins );

  QVector<double> aboveZ;
  aboveZ.reserve( validCount );
  for ( const QVector3D &p : points )
  {
    if ( std::isfinite( p.z() ) && static_cast<double>( p.z() ) >= gapTopZ )
      aboveZ.append( static_cast<double>( p.z() ) );
  }
  const double aboveFraction = static_cast<double>( aboveZ.size() ) / static_cast<double>( validCount );
  if ( gapFraction < 0.20 || belowFraction > 0.15 || aboveFraction < 0.80 || aboveZ.size() < 100 )
    return result;

  std::sort( aboveZ.begin(), aboveZ.end() );
  const double q01 = quantileSorted( aboveZ, 0.01 );
  const double q05 = quantileSorted( aboveZ, 0.05 );
  const double q95 = quantileSorted( aboveZ, 0.95 );
  const double q998 = quantileSorted( aboveZ, 0.998 );
  const double thick90 = std::max( 0.0, q95 - q05 );
  const double roofHeight = std::max( 0.0, q998 - q01 );

  result.gapBottomZ = gapBottomZ;
  result.gapTopZ = gapTopZ;
  result.eaveZ = q01;
  result.roofTopZ = q998;
  result.roofHeight = roofHeight;
  result.gapFraction = gapFraction;
  result.belowFraction = belowFraction;
  result.aboveFraction = aboveFraction;
  result.belowCount = belowSum;
  result.aboveCount = aboveZ.size();

  if ( thick90 < 1.0 )
    result.profile = CloudVerticalProfile::FlatKeepGround;
  else if ( gapFraction >= 0.30 && ( roofHeight / height ) < 0.40 )
    result.profile = CloudVerticalProfile::SuspendedRoofOnly;
  else
    result.profile = CloudVerticalProfile::DetachedBottomStrip;

  return result;
}

inline QVector<QVector3D> filterUpperCluster( const QVector<QVector3D> &points, const Analysis &analysis )
{
  if ( !analysis.shouldStripBottom() )
    return points;

  QVector<QVector3D> filtered;
  filtered.reserve( points.size() );
  const float cutoff = static_cast<float>( analysis.gapTopZ );
  for ( const QVector3D &p : points )
  {
    if ( std::isfinite( p.z() ) && p.z() >= cutoff )
      filtered.append( p );
  }
  return filtered.size() >= 100 ? filtered : points;
}

inline bool isPitchedRoofClass( const QString &primitiveType )
{
  return primitiveType == QStringLiteral( "PyramidalRoofHouse" ) ||
         primitiveType == QStringLiteral( "GableRoofHouse" ) ||
         primitiveType == QStringLiteral( "ShortRidgeHipRoofHouse" ) ||
         primitiveType == QStringLiteral( "FlatTopHipRoofHouse" ) ||
         primitiveType == QStringLiteral( "AngledGableHouse" ) ||
         primitiveType == QStringLiteral( "ConicalRoofHouse" ) ||
         primitiveType == QStringLiteral( "DomeRoofHouse" ) ||
         primitiveType == QStringLiteral( "BarrelRoofHouse" );
}

inline void reconstructSuspendedRoofHeights( const QString &primitiveType,
                                           const Analysis &analysis,
                                           QMap<QString, double> &nnParams )
{
  if ( !analysis.isSuspendedRoofOnly() || !isPitchedRoofClass( primitiveType ) ||
       analysis.roofHeight <= 0.3 )
    return;

  const double roofH = analysis.roofHeight;
  const double groundWall = analysis.eaveZ - analysis.originalMinZ;
  const bool hasGroundHint = analysis.belowCount >= 20 && groundWall > 0.0;

  if ( primitiveType == QStringLiteral( "BarrelRoofHouse" ) )
  {
    const double wallH = ( hasGroundHint && groundWall >= 1.5 && groundWall <= roofH * 5.0 )
      ? groundWall
      : std::max( 2.0, roofH * 1.5 );
    nnParams.insert( QStringLiteral( "wallHeight" ), wallH );
    return;
  }

  const bool usesCylRatio = primitiveType == QStringLiteral( "ConicalRoofHouse" ) ||
                            primitiveType == QStringLiteral( "DomeRoofHouse" );
  const QString ratioKey = usesCylRatio ? QStringLiteral( "cylinderRatio" ) : QStringLiteral( "wallRatio" );
  const double rawRatio = nnParams.value( ratioKey, 0.68 );
  const double ratio = std::isfinite( rawRatio ) ? std::clamp( rawRatio, 0.35, 0.82 ) : 0.68;

  const double wallH = ( hasGroundHint && groundWall >= roofH * 0.5 && groundWall <= roofH * 4.5 )
    ? groundWall
    : roofH * ratio / ( 1.0 - ratio );
  const double totalH = wallH + roofH;
  if ( totalH <= 1e-6 )
    return;

  nnParams.insert( QStringLiteral( "totalHeight" ), totalH );
  if ( nnParams.contains( QStringLiteral( "geom_total_height" ) ) )
    nnParams.insert( QStringLiteral( "geom_total_height" ), totalH );
  nnParams.insert( ratioKey, wallH / totalH );
}

} // namespace ParamModelerSuspendedRoof

#endif // PARAMMODELER_SUSPENDED_ROOF_H
