#ifndef EXPORTPOINTCLOUD_H
#define EXPORTPOINTCLOUD_H

#include <QString>
#include <QVector3D>

class ParamModelerDock;

struct DLPointCloudInfo
{
    QVector3D bboxMin;
    QVector3D bboxMax;
    QVector3D bboxSize;
    QVector3D center;
    double scale = 1.0;
};

class ExportPointCloud
{
public:
    // 从当前基元参数生成点云并保存为 .ply 文件
    // sampleCount: 采样点数
    static bool exportPLY( const QString    &fileName,
                           const QString    &primitiveType,
                           ParamModelerDock *dock,
                           int               sampleCount = 50000 );

    // Export normalized fixed-size xyz points for external deep learning models.
    static bool exportDLInputTXT( const QString    &fileName,
                                  const QString    &primitiveType,
                                  ParamModelerDock *dock,
                                  int               pointCount = 2048,
                                  DLPointCloudInfo *info = nullptr );
};

#endif // EXPORTPOINTCLOUD_H
