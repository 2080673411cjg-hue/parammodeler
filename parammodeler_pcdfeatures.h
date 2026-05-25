/***************************************************************************
  parammodeler_pcdfeatures.h
  Point Cloud Feature Extraction (preprocessing + 15 features)
  -------------------
         begin                : May 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_PCDFEATURES_H
#define PARAMMODELER_PCDFEATURES_H

#include "parammodeler_pcdtypes.h"

class FeatureExtractor
{
public:
    static PointCloud downsample( const PointCloud &pc, int targetPoints );
    static PointCloud curvatureFilter( const PointCloud &pc, double R1, double R2, int targetPoints );
    static FeatureVector extract( const PointCloud &pc );
};

#endif // PARAMMODELER_PCDFEATURES_H
