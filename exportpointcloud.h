#ifndef EXPORTPOINTCLOUD_H
#define EXPORTPOINTCLOUD_H

#include <QString>

class ParamModelerDock;

class ExportPointCloud
{
public:
    // 从当前基元参数生成点云并保存为 .ply 文件
    // sampleCount: 采样点数
    static bool exportPLY( const QString    &fileName,
                           const QString    &primitiveType,
                           ParamModelerDock *dock,
                           int               sampleCount = 50000 );
};

#endif // EXPORTPOINTCLOUD_H
