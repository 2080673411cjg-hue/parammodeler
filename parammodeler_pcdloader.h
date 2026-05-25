/***************************************************************************
  parammodeler_pcdloader.h
  Point Cloud File Loader (PLY / LAS / LAZ)
  -------------------
         begin                : May 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_PCDLOADER_H
#define PARAMMODELER_PCDLOADER_H

#include "parammodeler_pcdtypes.h"
#include <QString>

class PointCloudLoader
{
public:
    static PointCloud load( const QString &filePath );
};

#endif // PARAMMODELER_PCDLOADER_H
