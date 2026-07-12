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

struct PointCloud
{
    QVector<QVector3D> points;
    QVector3D bboxMin;
    QVector3D bboxMax;
    int      originalCount = 0;
};

#endif // PARAMMODELER_PCDTYPES_H
