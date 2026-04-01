/***************************************************************************
  parammodeler.cpp
  ParamModeler
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

#include "parammodeler.h"
#include "parammodeler_dock.h"
#include "qgisinterface.h"
#include <QIcon>
#include <QMenu>
#include <QDockWidget>
#include <QMessageBox>

// ========================================================================
// ==================== 静态常量定义（插件元数据）=======================
// ========================================================================
static const QString sName = QObject::tr( "Parametric Modeler" );
static const QString sDescription = QObject::tr( "Generate 3D building models from parameters" );
static const QString sCategory = QObject::tr( "3D" );
static const QString sPluginVersion = QObject::tr( "Version 0.1" );
static const QgisPlugin::PluginType sPluginType = QgisPlugin::UI;
static const QString sPluginIcon = QStringLiteral( ":/parammodeler/plugin_icon.png" );
// ========================================================================
// ========================= 插件类实现 =================================
// ========================================================================
ParamModeler::ParamModeler( QgisInterface *iface )
  : QgisPlugin( sName, sDescription, sCategory, sPluginVersion, sPluginType )
  , mIface( iface )
  , mAction( nullptr )
  , mDock( nullptr )
{
}
ParamModeler::~ParamModeler()
{
  // 不要 delete mIface 或 mDock，QGIS 会管理
  mAction = nullptr;
  mDock = nullptr;
}
// 注册插件菜单、工具栏
void ParamModeler::initGui()
{
  QIcon icon( sPluginIcon );
  mAction = new QAction( icon, tr( "Open ParamModeler" ), this );
  mAction->setObjectName( "paramModelerAction" );
  mAction->setCheckable( true );

  connect( mAction, &QAction::triggered, this, &ParamModeler::showOrHide );

  mIface->addToolBarIcon( mAction );
  mIface->addPluginToMenu( tr( "Parametric Modeler" ), mAction );
}
// 卸载插件
void ParamModeler::unload()
{
  if ( mAction )
  {
    mIface->removePluginMenu( tr( "Parametric Modeler" ), mAction );
    mIface->removeToolBarIcon( mAction );
    delete mAction;
    mAction = nullptr;
  }
  // mDock 不 delete（QGIS 管理）
}
// 点击插件按钮
void ParamModeler::showOrHide()
{
  if ( !mDock )
  {
    mDock = new ParamModelerDock( mIface );
    mIface->addDockWidget( Qt::RightDockWidgetArea, mDock );
    connect( mDock, &QDockWidget::visibilityChanged, mAction, &QAction::setChecked );
  }

  if ( mAction->isChecked() )
    mDock->show();
  else
    mDock->hide();
}
// ========================================================================
// ==================== C风格导出函数（QGIS 插件接口）==================
// ========================================================================

QGISEXTERN QgisPlugin *classFactory( QgisInterface *qgisInterfacePointer )
{
  return new ParamModeler( qgisInterfacePointer );
}
QGISEXTERN const QString *name()
{
  return &sName;
}
QGISEXTERN const QString *description()
{
  return &sDescription;
}
QGISEXTERN int type()
{
  return sPluginType;
}
QGISEXTERN const QString *category()
{
  return &sCategory;
}
QGISEXTERN const QString *version()
{
  return &sPluginVersion;
}
QGISEXTERN const QString *icon()
{
  return &sPluginIcon;
}
QGISEXTERN void unload( QgisPlugin *pluginPointer )
{
  delete pluginPointer;
}
