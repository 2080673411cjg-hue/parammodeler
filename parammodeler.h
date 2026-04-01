/***************************************************************************
  parammodeler.h
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

#ifndef PARAMMODELER_H
#define PARAMMODELER_H

#include "qgisplugin.h"
#include <QObject>

class QAction;
class QgisInterface;
class ParamModelerDock;

/**
 * \class ParamModeler
 * \brief 主插件类 —— 参数化建模器
 *
 * 插件生成参数化建筑模型，支持 JSON 输入/输出、OBJ 导出等扩展功能。
 */
class ParamModeler : public QObject, public QgisPlugin
{
    Q_OBJECT

  public:
    //! 构造函数
    ParamModeler( QgisInterface *iface );

    //! 析构函数
    ~ParamModeler() override;

    //! 初始化 GUI（菜单、工具栏按钮）
    void initGui() override;

    //! 卸载插件（移除菜单与按钮）
    void unload() override;

  private slots:
    //! 显示 / 隐藏主 Dock
    void showOrHide();

  private:
    QgisInterface *mIface;        //!< QGIS 接口
    QAction *mAction;             //!< 插件按钮
    ParamModelerDock *mDock;      //!< Dock 面板
};

#endif // PARAMMODELER_H
