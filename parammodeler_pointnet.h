#ifndef PARAMMODELER_POINTNET_H
#define PARAMMODELER_POINTNET_H

#include <QString>
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

class PointNetRunner
{
public:
  static PointNetPredictResult predict( const QString &inputTxt,
                                        int numPoints = 1024,
                                        int topK = 3 );
};

#endif // PARAMMODELER_POINTNET_H
