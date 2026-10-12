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
#include <QMatrix4x4>
#include <QString>
#include <QVector3D>
#include <QVector>

class ParamModelerDock;
struct PointCloud;
struct DLPointCloudInfo;

// ---- DL parameter mapping ----

QMap<QString, double> pointNetParamsToUiParams( const QString &primitiveType,
                                                 const QMap<QString, double> &nn,
                                                 bool swapCanonicalAxes = false );

bool shouldSwapCanonicalRegressionAxes( const QString &primitiveType,
                                        const QMap<QString, double> &nn,
                                        const QString &filePath,
                                        double rzDeg );

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
bool pointCloudMetadataLooksBuggy( const QVector3D &center, double scale );
bool usesSemanticUpperAlignment( const QString &primitiveType );
QMatrix4x4 modelRotationMatrix( const ParamModelerDock *dock );
void hardBBoxFromPoints( const QVector<QVector3D> &points, QVector3D &outMin, QVector3D &outMax );
void robustBBoxFromPoints( const QVector<QVector3D> &points, QVector3D &outMin, QVector3D &outMax );
bool estimateIndentedInnerBox( const QVector<QVector3D> &points,
                               const QVector3D &fpMin, const QVector3D &fpMax,
                               double zMin, double height,
                               QVector3D &innerMin, QVector3D &innerMax,
                               double &innerHeight );
bool semanticUpperPointCloudTarget( const QString &filePath, const QString &primitiveType,
                                    double rzDeg, double fallbackUpperOffset,
                                    bool useDisplayCoordinates, bool displayWasRestored,
                                    const QVector3D &displayRestoreCenter, double displayRestoreScale,
                                    QVector3D &out, QString &detail );

bool denormInfoFromPlyComment( const QString &filePath,
                                QVector3D &center,
                                double &scale );

QString metadataRelativePathForPointCloud( const QString &filePath );

// center 是采样点**质心**（反归一化 p*scale+center 用它还原原始坐标）；
// bboxMin/bboxSize 是原始坐标下的包围盒。两者语义不同，不可互换：
// 只有 bbox 中心（bboxMin + bboxSize/2）才能和 mesh bbox 中心比较 —— 模型对齐用它，
// 不能用质心（质心被 60/40 的屋顶/墙采样比例顶高，见 alignModelToPointCloud）。
//
// rz 是导出时烘进点云坐标的水平朝向（度，与 spinBoxRKappa 同向同约定），**不是**预测值：
// 采样导出走 applyPose(..., rz)（exportpointcloud.cpp），所以点云已经转过 rz，
// 模型要补的正是同一个角。只有 JSON metadata 记了它（PLY / 兜底路径取不到 → 返回 NaN）。
// 读不到 **不** 视作记录无效，不影响返回值。
bool metadataPointCloudInfoForInput( const QString &filePath,
                                     QVector3D *bboxMin,
                                     QVector3D *bboxSize,
                                     QVector3D *center,
                                     double *scale,
                                     double *rz = nullptr,
                                     QJsonObject *params = nullptr );

// ---- dataset path helpers ----

QString safeClassDirName( const QString &primitiveType );

QString datasetRootFromSelectedFolder( const QString &selectedPath );

#endif // PARAMMODELER_DLUTILS_H
