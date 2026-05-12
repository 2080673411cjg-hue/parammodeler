/***************************************************************************
  exportjson.cpp
  JSON 格式模型参数导出实现
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
 * *
 ***************************************************************************/
#include "exportjson.h"
#include "parammodeler_dock.h"
#include "ui_parammodeler_dock.h"

#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QMessageBox>

bool ExportJSON::writeJSON(ParamModelerDock *dock)
{
    QString fileName = QFileDialog::getSaveFileName(
        dock, QObject::tr("保存 JSON 文件"), "", QObject::tr("JSON Files (*.json)")
    );

    if (fileName.isEmpty())
        return false;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(dock, QObject::tr("错误"), QObject::tr("无法写入文件！"));
        return false;
    }
    // 基元类型

    QString primitiveType = dock->ui->comboPrimitive->currentText();

    // 构建 JSON
    QJsonObject root;
    root["type"] = primitiveType;
    root["transform"] = buildTransform(dock);
    root["params"] = buildParams(dock, primitiveType);

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    QMessageBox::information(
        dock,
        QObject::tr("导出成功"),
        QObject::tr("JSON 文件已保存到：\n%1").arg(fileName));

    return true;
}

// Transform
QJsonObject ExportJSON::buildTransform(ParamModelerDock *dock)
{
    QJsonObject t;

    auto toDouble = [](const QString &s) {
        bool ok;
        double v = s.toDouble(&ok);
        return ok ? v : 0.0;
    };

    t["tx"] = dock->poseTranslateX();
    t["ty"] = dock->poseTranslateY();
    t["tz"] = dock->poseTranslateZ();
    t["rx"] = dock->poseRotateX(); // 内部自动指向 Omega spinBox
    t["ry"] = dock->poseRotateY(); // 内部自动指向 Phi spinBox
    t["rz"] = dock->poseRotateZ(); // 内部自动指向 Kappa spinBox

    return t;
}

// Params  （所有基元的参数都在这里）
QJsonObject ExportJSON::buildParams(ParamModelerDock *dock, const QString &type)
{
    QJsonObject p;

    if (type == "Cuboid")
    {
        p["length"]  = dock->cuboidLength();
        p["width"]  = dock->cuboidWidth();
        p["height"] = dock->cuboidHeight();
    }
    else if (type == "Cylinder")
    {
      p["radius"] = dock->cylinderRadius();
      p["height"] = dock->cylinderHeight();
    }
    else if (type == "LHouse")
    {
      p["A_length"] = dock->LMainLength();
      p["A_width"] = dock->LMainWidth();
      p["B_length"] = dock->LWingLength();
      p["B_width"] = dock->LWingWidth();
      p["height"] = dock->LHeight();
    }
    else if (type == "ConeCylinder")
	{
		p["radius"] = dock->coneCylRadius();
		p["cyl_height"] = dock->coneCylCylHeight();
		p["cone_height"] = dock->coneCylConeHeight();
	}
    else if (type == "GabledRoof")
    {
        p["length"] = dock->gabledRoofLength();
        p["width"] = dock->gabledRoofWidth();
        p["wallHeight"] = dock->gabledRoofWallHeight();
        p["roofHeight"] = dock->gabledRoofRoofHeight();
    }
    else if (type == "PyramidRoof")
    {
        p["length"] = dock->pyramidLength();
        p["width"] = dock->pyramidWidth();
        p["wallHeight"] = dock->pyramidWallHeight();
        p["roofHeight"] = dock->pyramidRoofHeight();
    }
    else if (type == "TruncatedPyramidRoof")
    {
      p["length"] = dock->tpBottomLength();
      p["width"] = dock->tpBottomWidth();
      p["wallHeight"] = dock->tpWallHeight();
      p["roofHeight"] = dock->tpRoofHeight();
      p["topLength"] = dock->tpTopLength();
      p["topWidth"] = dock->tpTopWidth();
    }
    else if (type == "HalfCylinderRoof")
    {
        p["length"]      = dock->hcrLength();
        p["width"]      = dock->hcrWidth();
        p["wallHeight"] = dock->hcrWallHeight();
        p["radius"]     = dock->hcrRadius();
    }
    else if ( type == "穹顶圆柱" )
    {
      p["radius"]     = dock->cylHemiRadius();
      p["cyl_height"] = dock->cylHemiHeight();
      p["domeHeight"] = dock->cylHemiDomeHeight();
      p["bulgeFactor"]= dock->cylHemiBulge();
    }
    else if ( type == "凹陷长方体" )
    {
      p["outerLength"]  = dock->icOuterLength();
      p["outerWidth"]  = dock->icOuterWidth();
      p["outerHeight"] = dock->icOuterHeight();
      p["innerLength"]  = dock->icInnerLength();
      p["innerWidth"]  = dock->icInnerWidth();
      p["innerHeight"] = dock->icInnerHeight();
      p["offsetX"]     = dock->icOffsetX();
      p["offsetY"]     = dock->icOffsetY();
    }
    else if ( type == "非对称人字形屋顶房屋" )
    {
      p["length"]       = dock->aghLength();
      p["width"]       = dock->aghWidth();
      p["wallHeight"]  = dock->aghWallHeight();
      p["roofHeight"]  = dock->aghRoofHeight();
      p["ridgeLength"] = dock->aghRidgeLength();
      p["ridgeOffset"] = dock->aghRidgeOffset();
    }
    else if ( type == "四段式圆塔形" )
    {
      p["baseRadius"]     = dock->ftBaseRadius();
      p["baseHeight"]     = dock->ftBaseHeight();
      p["middleHeight"]   = dock->ftMiddleHeight();
      p["middleTopRadius"]= dock->ftMiddleTopRadius();
      p["middleBulge"]    = dock->ftMiddleBulge();
      p["coneHeight"]     = dock->ftConeHeight();
    }


    else if ( type == "双人字屋顶房屋" )
    {
      p["length1"]     = dock->tgLength1();
      p["length2"]     = dock->tgLength2();
      p["width"]      = dock->tgWidth();
      p["wallHeight"] = dock->tgWallHeight();
      p["roofHeight"] = dock->tgRoofHeight();
      p["angle"]      = dock->tgAngle();
    }

    return p;
}
