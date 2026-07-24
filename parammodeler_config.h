/***************************************************************************
  parammodeler_config.h
  User-configurable paths for PointNet deep learning backend
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_CONFIG_H
#define PARAMMODELER_CONFIG_H

#include <QString>

class QWidget;

#include "parammodeler_pointnet.h"

namespace ParamModelerConfig
{
  // ---- base paths (persisted in QgsSettings, fallback to hardcoded defaults) ----

  QString pythonExe();
  QString pointnetBaseDir();   // e.g.  E:/pointnet
  QString datasetsBaseDir();   // e.g.  E:/pointnet/datasets_aug

  // ---- model version selectors (persisted in QgsSettings) ----

  QString classifyModelName();          // e.g.  pointnext_cls_v2
  QString regressionModelPrefix();      // e.g.  pointnext_reg_
  QString regressionModelSuffix();      // e.g.  _rot

  // ---- derived paths (built from the base paths + model selectors) ----

  QString classifyScript( PointNetBackend backend );
  QString classifyLogDir( PointNetBackend backend );
  QString regressionScript( PointNetBackend backend );
  QString regressionLogBase( PointNetBackend backend );
  QString metadataJsonPath();
  QString dataRootPath();

  // ---- settings dialog ----
  void showSettingsDialog( QWidget *parent );
}

#endif // PARAMMODELER_CONFIG_H
