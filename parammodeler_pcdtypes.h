/***************************************************************************
  parammodeler_pcdtypes.h
  Shared data types for point cloud classification pipeline
  -------------------
         begin                : May 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_PCDTYPES_H
#define PARAMMODELER_PCDTYPES_H

#include <QString>
#include <QVector>
#include <QVector3D>

struct PcdResult
{
    QString primitiveType;
    double  confidence = 0.0;
};

struct PointCloud
{
    QVector<QVector3D> points;
    QVector3D bboxMin;
    QVector3D bboxMax;
    int      originalCount = 0;
};

struct FeatureVector
{
    double footprintCircularity = 0;
    double footprintAspectRatio = 0;
    double footprintConvexity = 0;
    double pcaRatio12 = 0;
    double pcaRatio23 = 0;
    double heightRatio50 = 0;
    double heightRatio80 = 0;
    double topSlope = 0;
    double symmetryX = 0;
    double symmetryY = 0;
    double crossSectionConsistency = 0;
    int    numStages = 1;
    double roofAngle = 0;
    double topLinearity = 0;
    int    numVerticalSegments = 1;
};

struct TypeProfile
{
    QString name;
    double weightCircularity = 1.0;
    double weightAspectRatio = 1.0;
    double weightConvexity = 1.0;
    double weightPcaRatio12 = 1.0;
    double weightPcaRatio23 = 1.0;
    double weightHeight50 = 1.0;
    double weightHeight80 = 1.0;
    double weightTopSlope = 1.0;
    double weightSymmetryX = 1.0;
    double weightSymmetryY = 1.0;
    double weightCrossSection = 1.0;
    double weightStages = 0.5;
    double weightRoofAngle = 1.0;
    double weightLinearity = 1.0;
    double weightVertSegments = 1.0;

    // Expected value ranges
    double expCircularity = 0,    expAspectRatio = 0;
    double expConvexity = 0,      expPcaRatio12 = 0;
    double expPcaRatio23 = 0,     expHeight50 = 0;
    double expHeight80 = 0,       expTopSlope = 0;
    double expSymmetryX = 0,      expSymmetryY = 0;
    double expCrossSection = 0;
    int    expStages = 1;
    double expRoofAngle = 0,      expLinearity = 0;
    int    expVertSegments = 1;
};

#endif // PARAMMODELER_PCDTYPES_H
