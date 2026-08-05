/***************************************************************************
  parammodeler_datasetgen.h
  DL dataset batch generation helpers
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_DATASETGEN_H
#define PARAMMODELER_DATASETGEN_H

#include <QJsonArray>
#include <QString>

class ParamModelerDock;
class QProgressDialog;

void generateFullDataset( ParamModelerDock *dock );

void generateSinglePrimitiveDataset( ParamModelerDock *dock );

QJsonArray generateSplitSamples( const QString &prim,
                                 const QString &classDir,
                                 const QString &split,
                                 int startIdx,
                                 int count,
                                 const QString &rootPath,
                                 int pointCount,
                                 ParamModelerDock *dock,
                                 QProgressDialog &progress,
                                 int &generated,
                                 int &failed );

#endif // PARAMMODELER_DATASETGEN_H
