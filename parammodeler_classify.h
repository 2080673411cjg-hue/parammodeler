/***************************************************************************
  parammodeler_classify.h
  Point Cloud Primitive Classification
  -------------------
         begin                : May 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_CLASSIFY_H
#define PARAMMODELER_CLASSIFY_H

#include <QString>
#include <QVector>
#include <QVector3D>
#include <QMap>

class ParamModelerDock;

class PrimitiveClassifier
{
public:
    struct Result
    {
        QString primitiveType;
        double  confidence;
    };

    static Result classify( const QString &filePath );

private:
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

    static PointCloud loadPointCloud( const QString &filePath );
    static PointCloud downsample( const PointCloud &pc, int targetPoints );
    static PointCloud curvatureFilter( const PointCloud &pc, double R1, double R2, int targetPoints );

    static FeatureVector extractFeatures( const PointCloud &pc );

    // Sub-feature extractors
    static double computeCircularity( const QVector<QVector3D> &xyProj,
                                      const QVector3D &bboxMin, const QVector3D &bboxMax );
    static double computeConvexity( const QVector<QVector3D> &xyProj,
                                    const QVector3D &bboxMin, const QVector3D &bboxMax );
    static void   computePCA( const QVector<QVector3D> &pts,
                              QVector3D &eigenvalues, QVector3D *eigenvectors );
    static double computeSymmetry( const PointCloud &pc, int axis );
    static double computeTopSlope( const PointCloud &pc, double topFrac = 0.10 );
    static double computeTopLinearity( const PointCloud &pc, double topFrac = 0.05 );
    static double computeCrossSectionConsistency( const PointCloud &pc, int slices = 10 );
    static int    countHeightStages( const PointCloud &pc );
    static int    countVerticalSegments( const PointCloud &pc );

    static Result classifyByScore( const FeatureVector &fv );

    // Expected feature profiles for each primitive
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

    static QVector<TypeProfile> buildProfiles();
    static double scoreProfile( const FeatureVector &fv, const TypeProfile &tp );
};

#endif // PARAMMODELER_CLASSIFY_H
