/***************************************************************************
  parammodeler_export.h
  OBJ / JSON model export
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_EXPORT_H
#define PARAMMODELER_EXPORT_H

#include <QString>
#include <QJsonObject>

class ParamModelerDock;

class ExportOBJ
{
public:
    static bool exportOBJ( const QString    &fileName,
                           const QString    &primitiveType,
                           ParamModelerDock *dock );
};

class ExportJSON
{
public:
    static bool writeJSON( ParamModelerDock *dock );

private:
    static QJsonObject buildTransform( ParamModelerDock *dock );
    static QJsonObject buildParams( ParamModelerDock *dock, const QString &type );
};

#endif // PARAMMODELER_EXPORT_H
