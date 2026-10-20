#ifndef PARAMMODELER_POINTNET_H
#define PARAMMODELER_POINTNET_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QCoreApplication>
#include <cmath>

inline QString towerRegressionSchemaError( const QString &primitiveType, const QMap<QString, double> &params )
{
  if ( primitiveType != QStringLiteral( "FourStageRoundTower" ) ) return QString();
  if ( !params.contains( QStringLiteral( "middleRise" ) ) || params.contains( QStringLiteral( "middleBulge" ) ) )
    return QCoreApplication::translate( "PointNetRunner", "This tower checkpoint uses the old bulge definition. Retrain with middleRise (metres) before using it." );
  if ( !std::isfinite( params.value( QStringLiteral( "middleRise" ) ) ) )
    return QCoreApplication::translate( "PointNetRunner", "The tower model returned an invalid middleRise value." );
  return QString();
}

struct PointNetPrediction
{
  QString className;
  double probability = 0.0;
};

struct PointNetPredictResult
{
  QVector<PointNetPrediction> predictions;
  QString rawOutput;
  QString errorMessage;
};

struct PointNetRegressionResult
{
  QString modelName;
  QString checkpointPath;
  QString className;
  QMap<QString, double> params;
  QString rawOutput;
  QString warningMessage;
  QString errorMessage;
};

enum class PointNetBackend
{
  PointNet,
  PointNet2,
  PointNeXt,
  PCT,
  PTv3
};

class ParamModelerDock;

class PointNetRunner
{
public:
  static PointNetPredictResult predict( const QString &inputTxt,
                                        int numPoints = 1024,
                                        int topK = 3 );
  static PointNetPredictResult predict( const QString &inputTxt,
                                        PointNetBackend backend,
                                        int numPoints = 1024,
                                        int topK = 3 );
  static PointNetRegressionResult predictParams( const QString &inputTxt,
                                                 const QString &primitiveType,
                                                 int numPoints = 2048 );
  static PointNetRegressionResult predictParams( const QString &inputTxt,
                                                 PointNetBackend backend,
                                                 const QString &primitiveType,
                                                 int numPoints = 2048 );

  static void applyToUI( ParamModelerDock *dock,
                         const QMap<QString, double> &params );
};

#endif // PARAMMODELER_POINTNET_H
