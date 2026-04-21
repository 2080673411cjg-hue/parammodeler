/***************************************************************************
  parammodeler_dock.h
  ParamModeler Dock Widget
  -------------------
         begin                : Nov. 2025
         copyright            : (C) 2025 by Chai
         email                : 2080673411@qq.com

 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

/***************************************************************************
  parammodeler_dock.h
  ParamModeler Dock Widget
 ***************************************************************************/

#ifndef PARAMMODELER_DOCK_H
#define PARAMMODELER_DOCK_H

#include <QDockWidget>
#include <QTimer>

class QgisInterface;
class QgsVectorLayer; 
class PreviewGLWidget;
class QMenu;                    // ← 新增这一行（推荐显式包含）

namespace Ui {
class ParamModelerDock;
}

class ParamModelerDock : public QDockWidget
{
  Q_OBJECT
  friend class ExportJSON;
  friend class ExportOBJ; 

public:
  explicit ParamModelerDock( QgisInterface *iface, QWidget *parent = nullptr );
  ~ParamModelerDock();

  // ===== 位姿参数访问接口 =====
  double poseTranslateX() const;
  double poseTranslateY() const;
  double poseTranslateZ() const;
  double poseRotateX() const; // Omega
		double poseRotateY() const; // Phi
		double poseRotateZ() const; // Kappa

  // ===== 各基元参数访问接口 =====
  double cuboidWidth() const;
  double cuboidDepth() const;
  double cuboidHeight() const;
  double cylinderRadius() const;
  double cylinderHeight() const;
  double LMainWidth() const;
  double LMainDepth() const;
  double LWingWidth() const;
  double LWingDepth() const;
  double LHeight() const;
  double coneCylRadius() const;
  double coneCylCylHeight() const;
  double coneCylConeHeight() const;
  double gabledRoofWidth() const;
  double gabledRoofDepth() const;
  double gabledRoofWallHeight() const;
  double gabledRoofRoofHeight() const;
  double pyramidWidth() const;
  double pyramidDepth() const;
  double pyramidWallHeight() const;
  double pyramidRoofHeight() const;
  double tpBottomWidth() const;
  double tpBottomDepth() const;
  double tpTopWidth() const;
  double tpTopDepth() const;
  double tpWallHeight() const;
  double tpRoofHeight() const;
  double hcrWidth() const;
  double hcrDepth() const;
  double hcrWallHeight() const;
  double hcrRadius() const;
  double icOuterWidth() const;
  double icOuterDepth() const;
  double icOuterHeight() const;
  double icInnerWidth() const;
  double icInnerDepth() const;
  double icInnerHeight() const;
  double icOffsetX() const;
  double icOffsetY() const;
  double aghWidth() const;
  double aghDepth() const;
  double aghWallHeight() const;
  double aghRoofHeight() const;
  double aghRidgeLength() const;
  double aghRidgeOffset() const;
  double cylHemiRadius() const;
  double cylHemiHeight() const;
  double cylHemiDomeHeight() const;
  double cylHemiBulge() const;
  double ftBaseRadius() const;
  double ftBaseHeight() const;
  double ftMiddleHeight() const;
  double ftMiddleTopRadius() const;
  double ftMiddleBulge() const;
  double ftConeHeight() const;
  double tgWidth1() const;
  double tgWidth2() const;
  double tgDepth() const;
  double tgWallHeight() const;
  double tgRoofHeight() const;
  double tgAngle() const;

private slots:
  void onPrimitiveChanged(const QString &prim); // 基元切换槽函数
  void onExportOBJClicked();
  void onExportJSONClicked();
  void onExportPLYClicked();
		void onExportMeshClicked();
		void onLoadToQGIS3D(bool zoomToLayer = true); // 将模型加载/同步到QGIS 3D视图，增加默认参数，true 表示缩放相机          
		void onLoadExternalPointCloud();
		
		
  void onLoadInputData();
  void onClassifyPrimitive();
  void onInverseParams();
  void onExportRebuiltOBJ();
  void onExportRebuiltJSON();
		
  void onUpdatePreview();//主刷新入口


private:
  Ui::ParamModelerDock *ui;
  QgisInterface *mIface;

  // ===== Tab2：输入数据 =====
  QString m_inputDataPath;

  // ===== 导出菜单 =====
  QMenu *m_exportMenu = nullptr;        

  // ===== 预览 =====
  PreviewGLWidget *m_previewWidget = nullptr;
  QTimer          *m_previewTimer  = nullptr;
		
		void removeLayerByName( const QString &name, const QString &excludeId = QString() );  //在加载新模型前，根据名称添加并删除旧图层
  QgsVectorLayer *m_modelLayer = nullptr;//新增一个成员变量，缓存图层指针
		bool            m_isUpdating = false; 
};

#endif // PARAMMODELER_DOCK_H