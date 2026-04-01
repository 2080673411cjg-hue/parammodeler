#ifndef EXPORTOBJ_H
#define EXPORTOBJ_H

#include <QString>

class ParamModelerDock;

class ExportOBJ
{
public:
    static bool exportOBJ( const QString    &fileName,
                           const QString    &primitiveType,
                           ParamModelerDock *dock );
};

#endif // EXPORTOBJ_H