#ifndef PARAMMODELER_REGRESSION_AXES_H
#define PARAMMODELER_REGRESSION_AXES_H

#include <QMap>
#include <QString>

#include <algorithm>

namespace ParamModelerRegressionAxes
{

inline bool shouldSwapFromSpans( double xSpan, double ySpan )
{
  return xSpan > 1e-6 && ySpan > xSpan * 1.08;
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
