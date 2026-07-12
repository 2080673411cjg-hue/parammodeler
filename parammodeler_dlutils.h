/***************************************************************************
  parammodeler_dlutils.h
  DL parameter mapping, metadata, and JSON utilities
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_DLUTILS_H
#define PARAMMODELER_DLUTILS_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector3D>

class ParamModelerDock;
struct PointCloud;
struct DLPointCloudInfo;

// ---- DL parameter mapping ----

QMap<QString, double> pointNetParamsToUiParams( const QString &primitiveType,
                                                 const QMap<QString, double> &nn );

QJsonObject currentPrimitiveParamsObject( const QString &prim,
                                          const ParamModelerDock *dock );

// ---- JSON helpers ----

QJsonArray vectorToJsonArray( const QVector3D &v );

QJsonObject pointCloudInfoToJson( const DLPointCloudInfo &info );

bool vectorFromJsonArray( const QJsonValue &value, QVector3D &out );

bool writeJsonDocumentChecked( const QString &path,
                                const QJsonDocument &doc,
                                QString *errorMessage );

// ---- point cloud metadata ----

double maxAbsComponent( const QVector3D &v );

bool pointCloudLooksNormalizedForDisplay( const PointCloud &pc );

bool denormInfoFromPlyComment( const QString &filePath,
                                QVector3D &center,
                                double &scale );

QString metadataRelativePathForPointCloud( const QString &filePath );

bool metadataPointCloudInfoForInput( const QString &filePath,
                                     QVector3D *bboxMin,
                                     QVector3D *center,
                                     double *scale );

// ---- dataset path helpers ----

QString safeClassDirName( const QString &primitiveType );

QString datasetRootFromSelectedFolder( const QString &selectedPath );

#endif // PARAMMODELER_DLUTILS_H
