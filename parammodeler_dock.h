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
#include <qgspointxy.h>

class QCheckBox;
class QProgressDialog;
class QPushButton;
class QJsonArray;

class QgisInterface;
class QgsMapLayer;
class QgsMapTool;
class QgsMapToolEmitPoint;
class QgsRubberBand;
class QgsVectorLayer;
class QMenu;                    // ← 新增这一行（推荐显式包含）

namespace Ui {
class ParamModelerDock;
}

class ParamModelerDock : public QDockWidget
{
  Q_OBJECT
  friend class ExportJSON;
  friend class ExportOBJ;
  friend class PointNetRunner;

  friend void generateFullDataset( ParamModelerDock *dock );
  friend void generateSinglePrimitiveDataset( ParamModelerDock *dock );
  friend QJsonArray generateSplitSamples( const QString &prim, const QString &classDir,
                                          const QString &split, int startIdx, int count,
                                          const QString &rootPath, int pointCount,
                                          ParamModelerDock *dock, QProgressDialog &progress,
                                          int &generated, int &failed );
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
  double LTotalLength() const;
  double LTotalWidth() const;
  double LWingRatio() const;
  double LWingWidthRatio() const;
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
  double triPrismPyramidLeg() const;
  double triPrismPyramidBase() const;
  double triPrismPyramidHeight() const;
  double triPrismPyramidRatio() const;

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
  void startManualTranslateByClick();
  void handleManualTranslateClick( const QgsPointXY &point, Qt::MouseButton button );
		
  void onUpdatePreview();//主刷新入口


private:
  void initUiControls();
  void initConnections();
  void initPreview();
  void initPointNet();

  void schedulePreviewUpdate();
  void randomizeCurrentPrimitiveParams( bool refreshPreview, bool randomizePose = false );
  bool loadPointCloudToQGIS3D( const QString &filePath, bool showMessage );

  // 缓存输入文件的 metadata（center 质心用于反归一化，bbox 极值/底面用于对齐）
  void cacheInputMetadata( const QString &filePath );

  // 取模型对齐的目标点。角锚定类取点云 bbox 左下角；中心锚定类取 XY 中心 + Z 底面。
  // 优先用"实际显示的点云"的 bbox，没有时退回 metadata 记录的 bbox。
  // 返回 false = 无可用目标，调用方应跳过对齐。
  bool pointCloudAlignTarget( QVector3D &out ) const;

  // 把 metadata 里记录的导出朝向 rz 回填到 pose（spinBoxRKappa）。
  // 点云坐标导出时已经被 applyPose 转过 rz，模型补上同一个角才能对上朝向。
  // 必须在 alignModelToPointCloud 之前调用（对齐参考点按 pose 旋转后的 mesh 算）。
  // 返回 false = 该输入没有可用的 rz，pose 保持不变。
  bool applyMetadataRz();

  // 实验性参数级数据驱动校正：PCT 回归后、写 UI 前，对少数强几何参数用点云统计量覆盖。
  // 当前只启用 Cuboid / Cylinder / GabledRoof，失败即跳过，保留原 PCT 输出。
  void applyDataDrivenParamCorrections( const QString &primitiveType,
                                        QMap<QString, double> &uiParams ) const;

  Ui::ParamModelerDock *ui;
  QgisInterface *mIface;
		QString m_currentPrimitive;                  // 记录当前基元名
  QMap<QString, QVector<double>> m_poseMap;    // 各基元的位姿存档

  // ===== Tab2：输入数据 =====
  QString m_inputDataPath;

  // ===== 元数据缓存（用于模型反归一化 / 对齐） =====
  // 注意：center 是采样点**质心**，只能用于反归一化 p*scale+center；
  // 模型对齐必须用 bbox 的极值（质心被 60/40 屋顶/墙采样比例顶高，见 alignModelToPointCloud）。
  QVector3D m_metadataCenter;                  // 质心（反归一化用）
  QVector3D m_metadataBBoxCenter;              // bbox XY 中心（圆类对齐用；Z 对齐时取 m_metadataBBoxMin）
  QVector3D m_metadataBBoxMin;                 // bbox 左下角（角锚定类对齐用）
  double    m_metadataScale = 1.0;
  double    m_metadataRz = 0.0;                // 导出朝向（度，applyPose 用的那个角）
  bool      m_hasMetadata = false;
  bool      m_hasMetadataBBox = false;
  bool      m_hasMetadataRz = false;           // 该输入是否带 rz（PLY / 无 params 的记录为 false）

  // ===== 场景中实际显示的点云 bbox（对齐首选，仅当它就是当前输入文件时用） =====
  QVector3D m_displayCloudBBoxCenter;          // bbox XY 中心（圆类用；Z 对齐时取 m_displayCloudBBoxMin）
  QVector3D m_displayCloudBBoxMin;             // bbox 左下角（角锚定类用）
  bool      m_hasDisplayCloudBBox = false;
  QString   m_displayCloudSourcePath;          // 显示在场景里的那个点云文件

  // ===== 预览 =====
  QTimer          *m_previewTimer  = nullptr;
  bool             m_previewUpdatePending = false;
  bool             m_previewUpdateInProgress = false;
		
  QgsVectorLayer *m_modelLayer = nullptr;//新增一个成员变量，缓存图层指针
  QgsMapLayer    *m_pointCloudLayer = nullptr;    // 缓存外部点云图层，用于清除
		bool            m_isUpdating = false; 
			QString         m_lastGpkgPath;             // 上一次临时 GPKG 文件路径，用于清理
  bool m_realtimeModelLoaded = false;
  QCheckBox *mWireframeModeCheckBox = nullptr;

  // ===== DL预测值锚点（微调复位用） =====
  QMap<QString, double> m_dlAnchorParams;
  bool m_hasDlAnchor = false;
  QPushButton *m_resetAnchorBtn = nullptr;
  QCheckBox *m_geometryCorrectionCheckBox = nullptr;
  void resetToDlAnchor();

  // ===== 手动目标点平移对齐（2D canvas 拾取） =====
  QPushButton *m_manualTranslateBtn = nullptr;
  QgsMapToolEmitPoint *m_manualTranslateTool = nullptr;
  QgsMapTool *m_previousMapTool = nullptr;
  QgsRubberBand *m_manualTranslateSourceMarker = nullptr;
  QgsRubberBand *m_manualTranslateXAxisMarker = nullptr;
  QgsRubberBand *m_manualTranslateYAxisMarker = nullptr;
  bool m_manualTranslateHasSource = false;
  QgsPointXY m_manualTranslateSource;
};

#endif // PARAMMODELER_DOCK_H
