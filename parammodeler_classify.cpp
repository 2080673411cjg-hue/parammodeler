/***************************************************************************
  parammodeler_classify.cpp
  Point Cloud Primitive Classification (orchestrator)
  -------------------
         begin                : May 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "parammodeler_classify.h"
#include "parammodeler_pcdloader.h"
#include "parammodeler_pcdfeatures.h"

#include <QFile>
#include <QTextStream>
#include <QDir>
#include <algorithm>
#include <cmath>
#include <windows.h>
#define DEBUG_LOG(msg) OutputDebugStringW(msg)

// ====================================================================
// 公共入口
// ====================================================================
PcdResult PrimitiveClassifier::classify( const QString &filePath )
{
  PcdResult result;
  result.primitiveType = "Unknown";
  result.confidence = 0.0;

  if ( filePath.isEmpty() )
    return result;

  PointCloud pc = PointCloudLoader::load( filePath );
  if ( pc.points.isEmpty() )
    return result;

  PointCloud sp = FeatureExtractor::downsample( pc, 5000 );
  sp = FeatureExtractor::curvatureFilter( sp, 0.05, 0.20, 2000 );
  FeatureVector fv = FeatureExtractor::extract( sp );

  // ===== 调试输出 START =====
  QString dbg = QString(
                  "\n=== PrimitiveClassifier Features ===\n"
                  "文件路径:        %1\n"
                  "原始点数:        %2\n"
                  "采样后点数:      %3\n"
                  "Circularity:     %4\n"
                  "AspectRatio:     %5\n"
                  "Convexity:       %6\n"
                  "PcaRatio12:      %7\n"
                  "PcaRatio23:      %8\n"
                  "HeightRatio50:   %9\n"
                  "HeightRatio80:   %10\n"
                  "TopSlope:        %11\n"
                  "SymmetryX:       %12\n"
                  "SymmetryY:       %13\n"
                  "CrossSection:    %14\n"
                  "NumStages:       %15\n"
                  "RoofAngle:       %16\n"
                  "TopLinearity:    %17\n"
                  "VertSegments:    %18\n"
                  "====================================\n"
  )
                  .arg( filePath )
                  .arg( pc.originalCount )
                  .arg( sp.points.size() )
                  .arg( fv.footprintCircularity, 0, 'f', 4 )
                  .arg( fv.footprintAspectRatio, 0, 'f', 4 )
                  .arg( fv.footprintConvexity, 0, 'f', 4 )
                  .arg( fv.pcaRatio12, 0, 'f', 4 )
                  .arg( fv.pcaRatio23, 0, 'f', 4 )
                  .arg( fv.heightRatio50, 0, 'f', 4 )
                  .arg( fv.heightRatio80, 0, 'f', 4 )
                  .arg( fv.topSlope, 0, 'f', 4 )
                  .arg( fv.symmetryX, 0, 'f', 4 )
                  .arg( fv.symmetryY, 0, 'f', 4 )
                  .arg( fv.crossSectionConsistency, 0, 'f', 4 )
                  .arg( fv.numStages )
                  .arg( fv.roofAngle, 0, 'f', 4 )
                  .arg( fv.topLinearity, 0, 'f', 4 )
                  .arg( fv.numVerticalSegments );

  DEBUG_LOG( dbg.toStdWString().c_str() );

  QFile logFile( QDir::tempPath() + "/parammodeler_classify.log" );
  if ( logFile.open( QIODevice::Append | QIODevice::Text ) )
  {
    QTextStream ts( &logFile );
    ts << dbg;
    logFile.close();
  }
  // ===== 调试输出 END =====

  result = classifyByScore( fv );

  QString res = QString( ">>> 识别结果: %1  置信度: %2%\n" )
                  .arg( result.primitiveType )
                  .arg( result.confidence * 100, 0, 'f', 1 );
  DEBUG_LOG( res.toStdWString().c_str() );

  if ( logFile.open( QIODevice::Append | QIODevice::Text ) )
  {
    QTextStream ts( &logFile );
    ts << res;
    logFile.close();
  }

  return result;
}

// ====================================================================
// 各型期望特征配置
// ====================================================================
QVector<TypeProfile> PrimitiveClassifier::buildProfiles()
{
    QVector<TypeProfile> profiles;

    // --- Cuboid ---
    {
        TypeProfile p;
        p.name = "Cuboid";
        p.expCircularity = 0.48;       p.weightCircularity = 1.5;
        p.expAspectRatio = 0.70;       p.weightAspectRatio = 0.3;
        p.expConvexity = 0.92;         p.weightConvexity = 2.0;
        p.expPcaRatio12 = 0.56;        p.weightPcaRatio12 = 0.5;
        p.expPcaRatio23 = 0.55;        p.weightPcaRatio23 = 0.5;
        p.expHeight50 = 0.50;          p.weightHeight50 = 0.3;
        p.expHeight80 = 1.00;          p.weightHeight80 = 1.0;
        p.expTopSlope = 0.000;         p.weightTopSlope = 3.0;
        p.expSymmetryX = 0.92;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.94;         p.weightSymmetryY = 1.5;
        p.expCrossSection = 0.955;     p.weightCrossSection = 3.0;
        p.expStages = 1;               p.weightStages = 1.0;
        p.expRoofAngle = 1.00;         p.weightRoofAngle = 2.0;
        p.expLinearity = 0.62;         p.weightLinearity = 0.5;
        p.expVertSegments = 2;         p.weightVertSegments = 0.5;
        profiles.append( p );
    }

    // --- Cylinder ---
    {
        TypeProfile p;
        p.name = "Cylinder";
        p.expCircularity = 0.94;       p.weightCircularity = 4.0;
        p.expAspectRatio = 1.00;       p.weightAspectRatio = 0.5;
        p.expConvexity = 0.50;         p.weightConvexity = 0.3;
        p.expPcaRatio12 = 0.99;        p.weightPcaRatio12 = 2.0;
        p.expPcaRatio23 = 0.25;        p.weightPcaRatio23 = 2.0;
        p.expHeight50 = 0.00;          p.weightHeight50 = 0.3;
        p.expHeight80 = 0.50;          p.weightHeight80 = 1.0;
        p.expTopSlope = 0.077;         p.weightTopSlope = 1.0;
        p.expSymmetryX = 0.99;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.99;         p.weightSymmetryY = 1.5;
        p.expCrossSection = 0.99;      p.weightCrossSection = 3.0;
        p.expStages = 1;               p.weightStages = 1.0;
        p.expRoofAngle = 0.25;         p.weightRoofAngle = 2.0;
        p.expLinearity = 0.60;         p.weightLinearity = 0.5;
        p.expVertSegments = 2;         p.weightVertSegments = 1.0;
        profiles.append( p );
    }

    // --- LHouse ---
    {
        TypeProfile p;
        p.name = "LHouse";
        p.expCircularity = 0.48;       p.weightCircularity = 1.0;
        p.expAspectRatio = 0.60;       p.weightAspectRatio = 0.3;
        p.expConvexity = 0.82;         p.weightConvexity = 2.5;
        p.expPcaRatio12 = 0.45;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.20;        p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.50;          p.weightHeight50 = 0.3;
        p.expHeight80 = 1.00;          p.weightHeight80 = 0.5;
        p.expTopSlope = 0.000;         p.weightTopSlope = 2.5;
        p.expSymmetryX = 0.75;         p.weightSymmetryX = 3.0;
        p.expSymmetryY = 0.75;         p.weightSymmetryY = 3.0;
        p.expCrossSection = 0.93;      p.weightCrossSection = 2.0;
        p.expStages = 1;               p.weightStages = 1.0;
        p.expRoofAngle = 1.00;         p.weightRoofAngle = 1.5;
        p.expLinearity = 0.65;         p.weightLinearity = 0.5;
        p.expVertSegments = 2;         p.weightVertSegments = 0.5;
        profiles.append( p );
    }

    // --- ConeCylinder ---
    {
        TypeProfile p;
        p.name = "ConeCylinder";
        p.expCircularity = 0.86;       p.weightCircularity = 3.0;
        p.expAspectRatio = 1.00;       p.weightAspectRatio = 0.3;
        p.expConvexity = 0.50;         p.weightConvexity = 0.3;
        p.expPcaRatio12 = 0.96;        p.weightPcaRatio12 = 1.5;
        p.expPcaRatio23 = 0.62;        p.weightPcaRatio23 = 1.5;
        p.expHeight50 = 0.32;          p.weightHeight50 = 1.5;
        p.expHeight80 = 0.66;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.059;         p.weightTopSlope = 1.5;
        p.expSymmetryX = 0.99;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.99;         p.weightSymmetryY = 1.5;
        p.expCrossSection = 0.09;      p.weightCrossSection = 3.5;
        p.expStages = 2;               p.weightStages = 2.0;
        p.expRoofAngle = 0.25;         p.weightRoofAngle = 2.5;
        p.expLinearity = 0.52;         p.weightLinearity = 0.5;
        p.expVertSegments = 5;         p.weightVertSegments = 2.5;
        profiles.append( p );
    }

    // --- GabledRoof ---
    {
        TypeProfile p;
        p.name = "GabledRoof";
        p.expCircularity = 0.42;       p.weightCircularity = 1.5;
        p.expAspectRatio = 0.95;       p.weightAspectRatio = 0.5;
        p.expConvexity = 0.90;         p.weightConvexity = 3.0;
        p.expPcaRatio12 = 0.999;       p.weightPcaRatio12 = 2.0;
        p.expPcaRatio23 = 0.57;        p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.71;          p.weightHeight50 = 1.0;
        p.expHeight80 = 0.88;          p.weightHeight80 = 1.0;
        p.expTopSlope = 0.021;         p.weightTopSlope = 1.5;
        p.expSymmetryX = 0.98;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.99;         p.weightSymmetryY = 1.5;
        p.expCrossSection = 0.23;      p.weightCrossSection = 2.0;
        p.expStages = 3;               p.weightStages = 1.5;
        p.expRoofAngle = 0.25;         p.weightRoofAngle = 2.5;
        p.expLinearity = 0.990;        p.weightLinearity = 4.0;
        p.expVertSegments = 2;         p.weightVertSegments = 1.5;
        profiles.append( p );
    }

    // --- PyramidRoof ---
    {
        TypeProfile p;
        p.name = "PyramidRoof";
        p.expCircularity = 0.47;       p.weightCircularity = 1.5;
        p.expAspectRatio = 0.83;       p.weightAspectRatio = 0.5;
        p.expConvexity = 0.50;         p.weightConvexity = 0.3;
        p.expPcaRatio12 = 0.77;        p.weightPcaRatio12 = 2.0;
        p.expPcaRatio23 = 0.90;        p.weightPcaRatio23 = 2.0;
        p.expHeight50 = 0.69;          p.weightHeight50 = 1.5;
        p.expHeight80 = 0.81;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.034;         p.weightTopSlope = 1.5;
        p.expSymmetryX = 0.99;         p.weightSymmetryX = 2.0;
        p.expSymmetryY = 0.99;         p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.15;      p.weightCrossSection = 2.5;
        p.expStages = 1;               p.weightStages = 1.5;
        p.expRoofAngle = 0.25;         p.weightRoofAngle = 2.5;
        p.expLinearity = 0.524;        p.weightLinearity = 3.5;
        p.expVertSegments = 5;         p.weightVertSegments = 2.0;
        profiles.append( p );
    }

    // --- TruncatedPyramidRoof ---
    {
        TypeProfile p;
        p.name = "TruncatedPyramidRoof";
        p.expCircularity = 0.32;       p.weightCircularity = 1.5;
        p.expAspectRatio = 0.91;       p.weightAspectRatio = 0.5;
        p.expConvexity = 0.17;         p.weightConvexity = 2.0;
        p.expPcaRatio12 = 0.30;        p.weightPcaRatio12 = 3.5;
        p.expPcaRatio23 = 0.94;        p.weightPcaRatio23 = 3.0;
        p.expHeight50 = 1.00;          p.weightHeight50 = 1.5;
        p.expHeight80 = 1.00;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.000;         p.weightTopSlope = 2.5;
        p.expSymmetryX = 0.96;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.95;         p.weightSymmetryY = 1.5;
        p.expCrossSection = 0.19;      p.weightCrossSection = 2.0;
        p.expStages = 1;               p.weightStages = 1.0;
        p.expRoofAngle = 1.00;         p.weightRoofAngle = 1.5;
        p.expLinearity = 0.56;         p.weightLinearity = 1.0;
        p.expVertSegments = 2;         p.weightVertSegments = 1.5;
        profiles.append( p );
    }

    // --- HalfCylinderRoof ---
    {
        TypeProfile p;
        p.name = "HalfCylinderRoof";
        p.expCircularity = 0.48;       p.weightCircularity = 2.0;
        p.expAspectRatio = 0.72;       p.weightAspectRatio = 0.5;
        p.expConvexity = 0.92;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.55;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.77;        p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.52;          p.weightHeight50 = 1.0;
        p.expHeight80 = 0.95;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.00;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.99;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.99;         p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.55;      p.weightCrossSection = 2.0;
        p.expStages = 1;               p.weightStages = 1.5;
        p.expRoofAngle = 0.25;         p.weightRoofAngle = 1.5;
        p.expLinearity = 1.00;         p.weightLinearity = 2.0;
        p.expVertSegments = 2;         p.weightVertSegments = 1.5;
        profiles.append( p );
    }

    // --- CylinderDome ---
    {
        TypeProfile p;
        p.name = "CylinderDome";
        p.expCircularity = 0.77;       p.weightCircularity = 3.0;
        p.expAspectRatio = 0.79;       p.weightAspectRatio = 0.5;
        p.expConvexity = 0.66;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.80;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.58;        p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.68;          p.weightHeight50 = 0.5;
        p.expHeight80 = 0.85;          p.weightHeight80 = 1.0;
        p.expTopSlope = 0.04;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.98;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.99;         p.weightSymmetryY = 1.5;
        p.expCrossSection = 0.55;      p.weightCrossSection = 2.0;
        p.expStages = 1;               p.weightStages = 2.0;
        p.expRoofAngle = 0.25;         p.weightRoofAngle = 1.0;
        p.expLinearity = 0.58;         p.weightLinearity = 2.0;
        p.expVertSegments = 4;         p.weightVertSegments = 2.0;
        profiles.append( p );
    }

    // --- IndentedCuboid ---
    {
        TypeProfile p;
        p.name = "IndentedCuboid";
        p.expCircularity = 0.42;       p.weightCircularity = 1.5;
        p.expAspectRatio = 0.93;       p.weightAspectRatio = 0.5;
        p.expConvexity = 0.25;         p.weightConvexity = 3.0;
        p.expPcaRatio12 = 0.79;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.77;        p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.85;          p.weightHeight50 = 0.5;
        p.expHeight80 = 1.00;          p.weightHeight80 = 0.5;
        p.expTopSlope = 0.01;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.97;         p.weightSymmetryX = 2.0;
        p.expSymmetryY = 0.97;         p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.51;      p.weightCrossSection = 2.0;
        p.expStages = 1;               p.weightStages = 1.0;
        p.expRoofAngle = 0.50;         p.weightRoofAngle = 0.5;
        p.expLinearity = 0.75;         p.weightLinearity = 1.0;
        p.expVertSegments = 3;         p.weightVertSegments = 1.0;
        profiles.append( p );
    }

    // --- AsymmetricGableHouse ---
    {
        TypeProfile p;
        p.name = "AsymmetricGableHouse";
        p.expCircularity = 0.44;       p.weightCircularity = 2.0;
        p.expAspectRatio = 0.88;       p.weightAspectRatio = 0.5;
        p.expConvexity = 0.72;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.65;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.91;        p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.44;          p.weightHeight50 = 1.0;
        p.expHeight80 = 1.00;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.00;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.94;         p.weightSymmetryX = 3.0;
        p.expSymmetryY = 0.94;         p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.88;      p.weightCrossSection = 2.0;
        p.expStages = 1;               p.weightStages = 1.5;
        p.expRoofAngle = 1.00;         p.weightRoofAngle = 1.5;
        p.expLinearity = 0.52;         p.weightLinearity = 3.0;
        p.expVertSegments = 4;         p.weightVertSegments = 1.5;
        profiles.append( p );
    }

    // --- FourStageRoundTower ---
    {
        TypeProfile p;
        p.name = "FourStageRoundTower";
        p.expCircularity = 0.56;       p.weightCircularity = 3.0;
        p.expAspectRatio = 0.62;       p.weightAspectRatio = 0.5;
        p.expConvexity = 0.87;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.58;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.11;        p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 1.00;          p.weightHeight50 = 1.0;
        p.expHeight80 = 1.00;          p.weightHeight80 = 1.0;
        p.expTopSlope = 0.00;          p.weightTopSlope = 1.5;
        p.expSymmetryX = 0.78;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.77;         p.weightSymmetryY = 1.5;
        p.expCrossSection = 0.88;      p.weightCrossSection = 2.5;
        p.expStages = 4;               p.weightStages = 4.0;
        p.expRoofAngle = 1.00;         p.weightRoofAngle = 1.5;
        p.expLinearity = 0.75;         p.weightLinearity = 1.5;
        p.expVertSegments = 3;         p.weightVertSegments = 3.0;
        profiles.append( p );
    }

    // --- TwoGableHouses ---
    {
        TypeProfile p;
        p.name = "TwoGableHouses";
        p.expCircularity = 0.45;       p.weightCircularity = 2.0;
        p.expAspectRatio = 0.98;       p.weightAspectRatio = 0.5;
        p.expConvexity = 0.99;         p.weightConvexity = 3.0;
        p.expPcaRatio12 = 0.49;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.90;        p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.52;          p.weightHeight50 = 1.0;
        p.expHeight80 = 1.00;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.00;          p.weightTopSlope = 1.5;
        p.expSymmetryX = 0.99;         p.weightSymmetryX = 2.5;
        p.expSymmetryY = 0.99;         p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.92;      p.weightCrossSection = 2.0;
        p.expStages = 1;               p.weightStages = 1.5;
        p.expRoofAngle = 1.00;         p.weightRoofAngle = 1.5;
        p.expLinearity = 0.55;         p.weightLinearity = 2.0;
        p.expVertSegments = 3;         p.weightVertSegments = 1.5;
        profiles.append( p );
    }

    return profiles;
}

// ====================================================================
// 评分函数：特征向量 vs 类型配置，返回 0-1 的匹配度
// ====================================================================
double PrimitiveClassifier::scoreProfile( const FeatureVector &fv, const TypeProfile &tp )
{
    auto gaussScore = []( double val, double expected, double sigma ) -> double
    {
        double diff = val - expected;
        return std::exp( -0.5 * ( diff * diff ) / ( sigma * sigma ) );
    };

    double totalWeight = 0, weightedScore = 0;

    auto addScore = [&]( double weight, double score, double actualVal, double expectedVal, double sigma )
    {
        if ( weight <= 0 ) return;
        // 动态降权：偏差超过 3*sigma 时权重减半
        if ( qAbs( actualVal - expectedVal ) > 3.0 * sigma )
            weight *= 0.5;
        totalWeight += weight;
        weightedScore += weight * score;
    };

    addScore( tp.weightCircularity,     gaussScore( fv.footprintCircularity, tp.expCircularity, 0.25 ),  fv.footprintCircularity, tp.expCircularity, 0.25 );
    addScore( tp.weightAspectRatio,     gaussScore( fv.footprintAspectRatio, tp.expAspectRatio, 0.30 ),  fv.footprintAspectRatio, tp.expAspectRatio, 0.30 );
    addScore( tp.weightConvexity,       gaussScore( fv.footprintConvexity,   tp.expConvexity,   0.15 ),  fv.footprintConvexity,   tp.expConvexity,   0.15 );
    addScore( tp.weightPcaRatio12,      gaussScore( fv.pcaRatio12,           tp.expPcaRatio12,   0.25 ),  fv.pcaRatio12,           tp.expPcaRatio12,   0.25 );
    addScore( tp.weightPcaRatio23,      gaussScore( fv.pcaRatio23,           tp.expPcaRatio23,   0.25 ),  fv.pcaRatio23,           tp.expPcaRatio23,   0.25 );
    addScore( tp.weightHeight50,        gaussScore( fv.heightRatio50,        tp.expHeight50,     0.15 ),  fv.heightRatio50,        tp.expHeight50,     0.15 );
    addScore( tp.weightHeight80,        gaussScore( fv.heightRatio80,        tp.expHeight80,     0.15 ),  fv.heightRatio80,        tp.expHeight80,     0.15 );
    addScore( tp.weightTopSlope,        gaussScore( fv.topSlope,             tp.expTopSlope,     0.12 ),  fv.topSlope,             tp.expTopSlope,     0.12 );
    addScore( tp.weightSymmetryX,       gaussScore( fv.symmetryX,            tp.expSymmetryX,    0.12 ),  fv.symmetryX,            tp.expSymmetryX,    0.12 );
    addScore( tp.weightSymmetryY,       gaussScore( fv.symmetryY,            tp.expSymmetryY,    0.12 ),  fv.symmetryY,            tp.expSymmetryY,    0.12 );
    addScore( tp.weightCrossSection,    gaussScore( fv.crossSectionConsistency, tp.expCrossSection, 0.20 ), fv.crossSectionConsistency, tp.expCrossSection, 0.20 );
    addScore( tp.weightStages,          gaussScore( static_cast<double>( fv.numStages ), static_cast<double>( tp.expStages ), 1.0 ),  static_cast<double>(fv.numStages), static_cast<double>(tp.expStages), 1.0 );
    addScore( tp.weightRoofAngle,       gaussScore( fv.roofAngle,            tp.expRoofAngle,    0.20 ),  fv.roofAngle,            tp.expRoofAngle,    0.20 );
    addScore( tp.weightLinearity,       gaussScore( fv.topLinearity,         tp.expLinearity,    0.25 ),  fv.topLinearity,         tp.expLinearity,    0.25 );
    addScore( tp.weightVertSegments,    gaussScore( static_cast<double>( fv.numVerticalSegments ), static_cast<double>( tp.expVertSegments ), 1.0 ),  static_cast<double>(fv.numVerticalSegments), static_cast<double>(tp.expVertSegments), 1.0 );

    if ( totalWeight < 0.0001 ) return 0.0;
    return weightedScore / totalWeight;
}

// ====================================================================
// 分类决策
// ====================================================================
PcdResult PrimitiveClassifier::classifyByScore( const FeatureVector &fv )
{
    QVector<TypeProfile> profiles = buildProfiles();

    PcdResult best;
    best.primitiveType = "Cuboid";
    best.confidence = 0.0;

    double bestScore = -1;
    double secondBestScore = -1;

    // ===== 诊断：打印每个 profile 的得分 =====
    QString scoreLog = "\n--- 各 Profile 得分 ---\n";
    QVector<QPair<QString, double>> allScores;

    for ( const TypeProfile &tp : profiles )
    {
        double s = scoreProfile( fv, tp );
        allScores.append( { tp.name, s } );
        scoreLog += QString( "  %1: %2\n" ).arg( tp.name, -22 ).arg( s, 0, 'f', 4 );
    }

    std::sort( allScores.begin(), allScores.end(),
               []( const QPair<QString,double> &a, const QPair<QString,double> &b ) { return a.second > b.second; } );
    scoreLog += "--- 排序 ---\n";
    for ( int i = 0; i < qMin( 5, allScores.size() ); i++ )
        scoreLog += QString( "  #%1  %2: %3\n" ).arg( i + 1 ).arg( allScores[i].first, -22 ).arg( allScores[i].second, 0, 'f', 4 );

    // 详细特征值
    scoreLog += QString( "\n--- 实际特征值 ---\n"
        "  Circularity:     %1\n"
        "  AspectRatio:     %2\n"
        "  Convexity:       %3\n"
        "  PcaRatio12:      %4\n"
        "  PcaRatio23:      %5\n"
        "  HeightRatio50:   %6\n"
        "  HeightRatio80:   %7\n"
        "  TopSlope:        %8\n"
        "  SymmetryX:       %9\n"
        "  SymmetryY:       %10\n"
        "  CrossSection:    %11\n"
        "  NumStages:       %12\n"
        "  RoofAngle:       %13\n"
        "  TopLinearity:    %14\n"
        "  VertSegments:    %15\n" )
        .arg( fv.footprintCircularity, 0, 'f', 4 )
        .arg( fv.footprintAspectRatio, 0, 'f', 4 )
        .arg( fv.footprintConvexity, 0, 'f', 4 )
        .arg( fv.pcaRatio12, 0, 'f', 4 )
        .arg( fv.pcaRatio23, 0, 'f', 4 )
        .arg( fv.heightRatio50, 0, 'f', 4 )
        .arg( fv.heightRatio80, 0, 'f', 4 )
        .arg( fv.topSlope, 0, 'f', 4 )
        .arg( fv.symmetryX, 0, 'f', 4 )
        .arg( fv.symmetryY, 0, 'f', 4 )
        .arg( fv.crossSectionConsistency, 0, 'f', 4 )
        .arg( fv.numStages )
        .arg( fv.roofAngle, 0, 'f', 4 )
        .arg( fv.topLinearity, 0, 'f', 4 )
        .arg( fv.numVerticalSegments );

    DEBUG_LOG( scoreLog.toStdWString().c_str() );
    QFile scoreFile( QDir::tempPath() + "/parammodeler_classify.log" );
    if ( scoreFile.open( QIODevice::Append | QIODevice::Text ) )
    {
        QTextStream ts( &scoreFile );
        ts << scoreLog;
        scoreFile.close();
    }

    for ( const TypeProfile &tp : profiles )
    {
        double s = scoreProfile( fv, tp );
        if ( s > bestScore )
        {
            secondBestScore = bestScore;
            bestScore = s;
            best.primitiveType = tp.name;
        }
        else if ( s > secondBestScore )
        {
            secondBestScore = s;
        }
    }

    double margin = bestScore - secondBestScore;
    best.confidence = bestScore * ( 0.7 + 0.3 * qBound( 0.0, margin * 2.0, 1.0 ) );
    best.confidence = qBound( 0.0, best.confidence, 1.0 );

    const double REJECT_THRESHOLD = 0.45;
    if ( bestScore < REJECT_THRESHOLD )
    {
        best.primitiveType = "Unknown";
        best.confidence = 0.0;
        return best;
    }

    const double AMBIGUITY_THRESHOLD = 0.05;
    if ( margin < AMBIGUITY_THRESHOLD )
    {
        best.confidence *= 0.5;
        best.confidence = qBound( 0.0, best.confidence, 1.0 );
    }

    return best;
}
