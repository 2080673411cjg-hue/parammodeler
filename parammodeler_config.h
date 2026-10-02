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
  QString ptv3PythonExe();
  QString ptv3ServerUrl();
  QString ptv3LanServerUrl();
  QString ptv3TailscaleServerUrl();
  QString ptv3ServerProfile();
  QString pythonExeForBackend( PointNetBackend backend );
  QString pointnetBaseDir();   // e.g.  E:/pointnet
  QString datasetsBaseDir();   // e.g.  E:/pointnet/datasets_aug

  // ---- model version selectors (persisted in QgsSettings) ----

  QString classifyModelName();          // e.g.  pct_cls_v2
  QString regressionModelPrefix();      // e.g.  pointnext_reg_  (PointNeXt legacy)
  QString regressionModelSuffix();      // e.g.  _v2              (PointNeXt legacy)
  QString pctClassifyLogDir();          // e.g.  E:/pointnet/pct_simple/logs/pct_cls_v4
  QString pctRegressionSuffix();        // e.g.  _v4_normals      (PCT default variant)
  QString ptv3ClassifyLogDir();         // e.g.  E:/pointnet/ptv3_simple/logs/ptv3_cls_v2
  QString ptv3RegressionSuffix();       // e.g.  _v2_normals

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
