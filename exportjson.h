/***************************************************************************
  exportjson.h
  JSON 格式模型参数导出头文件
  -------------------
          begin                : Jan. 2026
          copyright            : (C) 2026 by Chai
          email                : 2080673411@qq.com
***************************************************************************/

/***************************************************************************
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 ***************************************************************************/
#ifndef EXPORTJSON_H
#define EXPORTJSON_H

#include <QString>
#include <QJsonObject>

class ParamModelerDock;

class ExportJSON
{
public:
    static bool writeJSON(ParamModelerDock *dock);

private:
    static QJsonObject buildTransform(ParamModelerDock *dock);
    static QJsonObject buildParams(ParamModelerDock *dock, const QString &type);
};

#endif // EXPORTJSON_H
