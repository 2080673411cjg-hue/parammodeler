#ifndef PARAMMODELER_POINTNET_H
#define PARAMMODELER_POINTNET_H

#include <QString>
#include <QMap>
#include <QVector>

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
  QString className;
  QMap<QString, double> params;
  QString rawOutput;
  QString errorMessage;
};

enum class PointNetBackend
{
  PointNet,
  PointNet2,
  PointNeXt
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
