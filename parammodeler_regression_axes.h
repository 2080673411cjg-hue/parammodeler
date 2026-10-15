#ifndef PARAMMODELER_REGRESSION_AXES_H
#define PARAMMODELER_REGRESSION_AXES_H

#include <QMap>
#include <QString>

#include <algorithm>
#include <array>
#include <cmath>

namespace ParamModelerRegressionAxes
{

inline bool shouldSwapFromSpans( double xSpan, double ySpan )
{
  return xSpan > 1e-6 && ySpan > xSpan * 1.08;
}

inline QMap<QString, double> paramsInMetadataFrame( const QString &primitiveType,
                                                    const QMap<QString, double> &params,
                                                    double metadataRz )
{
  if ( !std::isfinite( metadataRz ) || !params.contains( QStringLiteral( "poseRotateZ" ) ) )
    return params;

  QMap<QString, double> aligned = params;
  const double delta = params.value( QStringLiteral( "poseRotateZ" ) ) - metadataRz;
  const int turns = ( static_cast<int>( std::lround( delta / 90.0 ) ) % 4 + 4 ) % 4;
  const auto swapPair = [&]( const QString &first, const QString &second ) {
    if ( aligned.contains( first ) && aligned.contains( second ) )
      std::swap( aligned[first], aligned[second] );
  };
  if ( ( primitiveType == QStringLiteral( "GabledRoof" ) ||
         primitiveType == QStringLiteral( "AsymmetricGableHouse" ) ) && turns == 2 &&
       aligned.contains( QStringLiteral( "ridgeRatio" ) ) )
    aligned[QStringLiteral( "ridgeRatio" )] = 1.0 - aligned.value( QStringLiteral( "ridgeRatio" ) );

  if ( turns % 2 )
  {
    if ( primitiveType == QStringLiteral( "Cuboid" ) || primitiveType == QStringLiteral( "PyramidRoof" ) )
      swapPair( QStringLiteral( "length" ), QStringLiteral( "width" ) );
    else if ( primitiveType == QStringLiteral( "TruncatedPyramidRoof" ) )
    {
      swapPair( QStringLiteral( "bottomLength" ), QStringLiteral( "bottomWidth" ) );
      swapPair( QStringLiteral( "topLengthRatio" ), QStringLiteral( "topWidthRatio" ) );
      swapPair( QStringLiteral( "topLength" ), QStringLiteral( "topWidth" ) );
    }
    else if ( primitiveType == QStringLiteral( "IndentedCuboid" ) )
      swapPair( QStringLiteral( "outerLength" ), QStringLiteral( "outerWidth" ) );
  }
  if ( primitiveType == QStringLiteral( "IndentedCuboid" ) )
  {
    const std::array<QString, 4> sides = { QStringLiteral( "leftMarginRatio" ), QStringLiteral( "rightMarginRatio" ),
                                          QStringLiteral( "frontMarginRatio" ), QStringLiteral( "backMarginRatio" ) };
    const int permutation[4][4] = { { 0, 1, 2, 3 }, { 3, 2, 0, 1 }, { 1, 0, 3, 2 }, { 2, 3, 1, 0 } };
    for ( int i = 0; i < 4; ++i )
      if ( params.contains( sides[permutation[turns][i]] ) )
        aligned[sides[i]] = params.value( sides[permutation[turns][i]] );
  }
  aligned[QStringLiteral( "poseRotateZ" )] = metadataRz;
  return aligned;
}

inline QMap<QString, double> remapCanonicalParams( const QString &primitiveType,
                                                   const QMap<QString, double> &params,
                                                   bool swapAxes )
{
  if ( !swapAxes || !params.contains( QStringLiteral( "geom_footprint_long" ) ) ||
       !params.contains( QStringLiteral( "geom_footprint_short" ) ) )
    return params;

  QMap<QString, double> aligned = params;
  const auto swapPair = [&]( const QString &first, const QString &second ) {
    if ( aligned.contains( first ) && aligned.contains( second ) )
      std::swap( aligned[first], aligned[second] );
  };

  if ( primitiveType == QStringLiteral( "Cuboid" ) ||
       primitiveType == QStringLiteral( "PyramidRoof" ) )
    swapPair( QStringLiteral( "length" ), QStringLiteral( "width" ) );
  else if ( primitiveType == QStringLiteral( "TruncatedPyramidRoof" ) )
  {
    swapPair( QStringLiteral( "bottomLength" ), QStringLiteral( "bottomWidth" ) );
    swapPair( QStringLiteral( "topLengthRatio" ), QStringLiteral( "topWidthRatio" ) );
    swapPair( QStringLiteral( "topLength" ), QStringLiteral( "topWidth" ) );
  }
  else if ( primitiveType == QStringLiteral( "IndentedCuboid" ) )
  {
    swapPair( QStringLiteral( "outerLength" ), QStringLiteral( "outerWidth" ) );
    swapPair( QStringLiteral( "leftMarginRatio" ), QStringLiteral( "frontMarginRatio" ) );
    swapPair( QStringLiteral( "rightMarginRatio" ), QStringLiteral( "backMarginRatio" ) );
  }
  return aligned;
}

} // namespace ParamModelerRegressionAxes

#endif // PARAMMODELER_REGRESSION_AXES_H
