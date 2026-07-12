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
#include <QMap>
#include <QVector>
#include <QVector3D>

class QCheckBox;

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
  friend class ParamInverter;
  friend void generateFullDataset( ParamModelerDock *dock );
  friend void generateSinglePrimitiveDataset( ParamModelerDock *dock );
  friend void randomizePrimitiveParams( ParamModelerDock *dock, bool refreshPreview, bool randomizePose );

public:
  explicit ParamModelerDock( QgisInterface *iface, QWidget *parent = nullptr );
  ~ParamModelerDock();

  // ===== 位姿参数访问接口 =====
  double poseTranslateX() const;
  double poseTranslateY() const;
  double poseTranslateZ() const;
  void setPoseTranslate( double tx, double ty, double tz );
  double poseRotateX() const; // Omega
	double poseRotateY() const; // Phi
	double poseRotateZ() const; // Kappa

  // ===== 各基元参数访问接口 =====
  double cuboidLength() const;
  double cuboidWidth() const;
  double cuboidHeight() const;
  double cylinderRadius() const;
  double cylinderHeight() const;
  double LMainLength() const;
  double LMainWidth() const;
  double LWingLength() const;
  double LWingWidth() const;
  double LHeight() const;
  double coneCylRadius() const;
  double coneCylCylHeight() const;
  double coneCylConeHeight() const;
  double gabledRoofLength() const;
  double gabledRoofWidth() const;
  double gabledRoofWallHeight() const;
  double gabledRoofRoofHeight() const;
  double pyramidLength() const;
  double pyramidWidth() const;
  double pyramidWallHeight() const;
  double pyramidRoofHeight() const;
  double tpBottomLength() const;
  double tpBottomWidth() const;
  double tpTopLength() const;
  double tpTopWidth() const;
  double tpWallHeight() const;
  double tpRoofHeight() const;
  double hcrLength() const;
  double hcrWidth() const;
  double hcrWallHeight() const;
  double hcrRadius() const;
  double icOuterLength() const;
  double icOuterWidth() const;
  double icOuterHeight() const;
  double icInnerLength() const;
  double icInnerWidth() const;
  double icInnerHeight() const;
  double icOffsetX() const;
  double icOffsetY() const;
  double aghLength() const;
  double aghWidth() const;
  double aghWallHeight() const;
  double aghRoofHeight() const;
  double aghRidgeLength() const;
  double aghRidgeOffset() const;
  double aghRidgeRatio() const;
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
  double tgLength1() const;
  double tgLength2() const;
  double tgWidth() const;
  double tgWallHeight() const;
  double tgRoofHeight() const;
  double tgAngle() const;
  double tgRidgeRatio() const;

private slots:
  void onPrimitiveChanged(const QString &prim); // 基元切换槽函数
  void onExportOBJClicked();
  void onExportJSONClicked();
  void onExportPLYClicked();
  void onExportDLPointCloudClicked();
  void onExportLoadedDLPointCloudClicked();
  void onExportDLDatasetClicked();
  void onExportCurrentPrimitiveDLDatasetClicked();
	void onExportMeshClicked();
	void onLoadToQGIS3D(bool zoomToLayer = true); // 将模型加载/同步到QGIS 3D视图，增加默认参数，true 表示缩放相机          
	void onLoadExternalPointCloud();
		
		
  void onLoadInputData();
  void onPointNetClassify();
  void onInverseParams();
  void onOpenPointCloudEstimateDialog();
  void onRandomizeCurrentPrimitive();
		
  void onUpdatePreview();//主刷新入口


private:
  void schedulePreviewUpdate();
  void randomizeCurrentPrimitiveParams( bool refreshPreview, bool randomizePose = false );
  bool loadPointCloudToQGIS3D( const QString &filePath, bool showMessage );

  Ui::ParamModelerDock *ui;
  QgisInterface *mIface;
		QString m_currentPrimitive;                  // 记录当前基元名
  QMap<QString, QVector<double>> m_poseMap;    // 各基元的位姿存档

  // ===== Tab2：输入数据 =====
  QString m_inputDataPath;

  // ===== 元数据缓存（用于模型反归一化对齐） =====
  QVector3D m_metadataCenter;
  double    m_metadataScale = 1.0;
  bool      m_hasMetadata = false;

  // ===== 预览 =====
  PreviewGLWidget *m_previewWidget = nullptr;
  QTimer          *m_previewTimer  = nullptr;
  bool             m_previewUpdatePending = false;
  bool             m_previewUpdateInProgress = false;
		
  QgsVectorLayer *m_modelLayer = nullptr;//新增一个成员变量，缓存图层指针
		bool            m_isUpdating = false; 
			QString         m_lastGpkgPath;             // 上一次临时 GPKG 文件路径，用于清理
  bool m_realtimeModelLoaded = false;
  QCheckBox *mGhostModeCheckBox = nullptr;
};

#endif // PARAMMODELER_DOCK_H
