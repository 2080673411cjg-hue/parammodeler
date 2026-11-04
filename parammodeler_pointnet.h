#ifndef PARAMMODELER_POINTNET_H
#define PARAMMODELER_POINTNET_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>
#include <functional>

inline QString towerRegressionSchemaError( const QString &primitiveType, const QMap<QString, double> &params )
{
  if ( primitiveType != QStringLiteral( "CurvedRoofRoundHouse" ) &&
       primitiveType != QStringLiteral( "FourStageRoundTower" ) ) return QString();
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
  QString modelName;
  QString checkpointPath;
  QString rawOutput;
  QString errorMessage;
  bool canceled = false;
};

struct PointNetRegressionResult
{
  QString modelName;
  QString checkpointPath;
  QString className;
  QJsonObject canonicalAlignment;
  QMap<QString, double> params;
  QString rawOutput;
  QString warningMessage;
  QString errorMessage;
  bool canceled = false;
};

inline QString inferenceModelDescription( const QString &modelName, const QString &checkpointPath )
{
  if ( checkpointPath.isEmpty() )
    return QCoreApplication::translate( "PointNetRunner", "%1 (version unconfirmed)" ).arg( modelName );
  QString normalized = checkpointPath;
  normalized.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
  const QString folder = QFileInfo( QFileInfo( normalized ).path() ).fileName();
  static const QRegularExpression version( QStringLiteral( "_(v[0-9]+(?:\\.[0-9]+)*)(?:_normals)?$" ) );
  const auto match = version.match( folder );
  return modelName + QLatin1Char( ' ' ) + ( match.hasMatch() ? match.captured( 1 ) : folder );
}

inline QString inferenceDirectionDescription( const QJsonObject &alignment )
{
  const auto applied = alignment.value( QStringLiteral( "applied" ) );
  if ( !applied.isBool() )
    return QCoreApplication::translate( "PointNetRunner", "Unconfirmed (direction status not returned)" );
  if ( applied.toBool() )
    return QCoreApplication::translate( "PointNetRunner", "Normalized for parameter prediction" );
  if ( alignment.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "SKIPPED_CLASS" ) )
    return QCoreApplication::translate( "PointNetRunner", "Not applied for this primitive" );
  return QCoreApplication::translate( "PointNetRunner", "Direction normalization not applied" );
}

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
                                        int topK = 3,
                                        const std::function<bool()> &shouldCancel = {} );
  static PointNetPredictResult predict( const QString &inputTxt,
                                        PointNetBackend backend,
                                        int numPoints = 1024,
                                        int topK = 3,
                                        const std::function<bool()> &shouldCancel = {} );
  static PointNetRegressionResult predictParams( const QString &inputTxt,
                                                 const QString &primitiveType,
                                                 int numPoints = 2048,
                                                 const std::function<bool()> &shouldCancel = {} );
  static PointNetRegressionResult predictParams( const QString &inputTxt,
                                                 PointNetBackend backend,
                                                 const QString &primitiveType,
                                                 int numPoints = 2048,
                                                 const std::function<bool()> &shouldCancel = {} );

  static void applyToUI( ParamModelerDock *dock,
                         const QMap<QString, double> &params );
};

#endif // PARAMMODELER_POINTNET_H
