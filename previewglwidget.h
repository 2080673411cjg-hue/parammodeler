/***************************************************************************
  previewglwidget.h
  OpenGL Preview Widget Header
  -------------------
         begin                : Mar. 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef PREVIEWGLWIDGET_H
#define PREVIEWGLWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVector3D>
#include "meshdata.h"

// ============================================================
// PreviewGLWidget
// 用现代 OpenGL（Shader）渲染 MeshData
// 支持鼠标旋转 + 滚轮缩放
// ============================================================
class PreviewGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit PreviewGLWidget( QWidget *parent = nullptr );
    ~PreviewGLWidget();

    void setMesh( const MeshData &mesh );
    void resetView();

protected:
    void initializeGL() override;
    void resizeGL( int w, int h ) override;
    void paintGL() override;

    void mousePressEvent( QMouseEvent *event ) override;
    void mouseMoveEvent( QMouseEvent *event ) override;
    void wheelEvent( QWheelEvent *event ) override;

private:
    void uploadMesh();
    void computeBoundingInfo();

    MeshData   m_mesh;
    bool       m_gpuDirty = false;

    // Shader：实心面
    QOpenGLShaderProgram *m_program     = nullptr;
    QOpenGLBuffer         m_vbo;

    // Shader：线框
    QOpenGLShaderProgram *m_programWire = nullptr;
    QOpenGLBuffer         m_vboWire;

    int   m_faceCount = 0;
    int   m_wireCount = 0;

    // 视角
    float  m_rotX   = 0.0f;  
    float  m_rotZ   = 0.0f;  
    float  m_zoom   = 1.0f;
    float  m_aspect = 1.0f;
    QPoint m_lastPos;

    // 包围盒
    QVector3D m_center;
    float     m_radius = 1.0f;
};

#endif // PREVIEWGLWIDGET_H