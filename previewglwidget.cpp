/***************************************************************************
  previewglwidget.cpp
  OpenGL Preview Widget
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
#include "previewglwidget.h"
#include <QDebug>
#include <QtMath>
#include <QMatrix4x4>
#include <QVector3D>

// ============================================================
PreviewGLWidget::PreviewGLWidget( QWidget *parent )
    : QOpenGLWidget( parent )
{
    setMinimumSize( 300, 280 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
}

PreviewGLWidget::~PreviewGLWidget()
{
    makeCurrent();
    m_vbo.destroy();// 销毁存储面的显存缓冲
    m_vboWire.destroy();// 销毁存储线框的显存缓冲
    doneCurrent();
}

// ============================================================
void PreviewGLWidget::setMesh( const MeshData &mesh )
{
    m_mesh = mesh;// 保存从外部传来的网格数据
    computeBoundingInfo();// 标记数据已变，需要在下一次重绘时重新传给显卡
    m_gpuDirty = true;// 核心：计算模型的中心和大小，用于自动缩放视角
    update();
}

void PreviewGLWidget::resetView()
{
    m_rotX = 0.0f;
    m_rotZ = 0.0f;
    m_zoom = 1.0f;
    update();
}

// ============================================================
void PreviewGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    qDebug() << ">>> initializeGL, OpenGL version:"
             << (const char*)glGetString( GL_VERSION );

    glEnable( GL_DEPTH_TEST );
    glClearColor( 0.18f, 0.18f, 0.18f, 1.0f );

    // ---- Shader：实心面（带法线光照） ----
    const char *vertSrc = R"(
        attribute vec3 aPos;
        attribute vec3 aNormal;
        uniform mat4 uMVP;
        uniform mat3 uNormalMat;
        varying vec3 vNormal;
        void main() {
            vNormal = normalize(uNormalMat * aNormal);
            gl_Position = uMVP * vec4(aPos, 1.0);
        }
    )";
    const char *fragSrc = R"(
        varying vec3 vNormal;
        void main() {
            vec3 lightDir = normalize(vec3(1.0, 1.0, 2.0));
            float diff = max(dot(vNormal, lightDir), 0.0);
            vec3 color = vec3(0.45, 0.65, 0.85);
            vec3 ambient = 0.3 * color;
            gl_FragColor = vec4(ambient + diff * color, 1.0);
        }
    )";

    m_program = new QOpenGLShaderProgram( this );
    m_program->addShaderFromSourceCode( QOpenGLShader::Vertex,   vertSrc );
    m_program->addShaderFromSourceCode( QOpenGLShader::Fragment, fragSrc );
    m_program->link();

    // ---- Shader：线框 ----
    const char *vertWire = R"(
        attribute vec3 aPos;
        uniform mat4 uMVP;
        void main() {
            gl_Position = uMVP * vec4(aPos, 1.0);
        }
    )";
    const char *fragWire = R"(
        void main() {
            gl_FragColor = vec4(0.1, 0.1, 0.1, 1.0);
        }
    )";
    m_programWire = new QOpenGLShaderProgram( this );
    m_programWire->addShaderFromSourceCode( QOpenGLShader::Vertex,   vertWire );
    m_programWire->addShaderFromSourceCode( QOpenGLShader::Fragment, fragWire );
    m_programWire->link();

    m_vbo.create();
    m_vboWire.create();
}

// ============================================================
void PreviewGLWidget::resizeGL( int w, int h )
{
    glViewport( 0, 0, w, h );
    m_aspect = ( h > 0 ) ? float(w) / float(h) : 1.0f;
}

// ============================================================
// 把 MeshData 打包成 GPU buffer（位置 + 法线交替）
// ============================================================
void PreviewGLWidget::uploadMesh()
{
    // ---- 实心面：每顶点 6 floats（pos xyz + normal xyz） ----
    QVector<float> data;
    data.reserve( m_mesh.indices.size() * 6 );

    for ( int i = 0; i + 2 < m_mesh.indices.size(); i += 3 )
    {
        QVector3D v0 = m_mesh.vertices[ m_mesh.indices[i]   ];
        QVector3D v1 = m_mesh.vertices[ m_mesh.indices[i+1] ];
        QVector3D v2 = m_mesh.vertices[ m_mesh.indices[i+2] ];
        QVector3D n  = QVector3D::crossProduct( v1-v0, v2-v0 ).normalized();

        for ( const QVector3D &v : { v0, v1, v2 } )
        {
            data << v.x() << v.y() << v.z();
            data << n.x() << n.y() << n.z();
        }
    }
    m_faceCount = m_mesh.indices.size();

    m_vbo.bind();
    m_vbo.allocate( data.constData(), data.size() * sizeof(float) );
    m_vbo.release();

    // ---- 线框：每条边 2 个顶点，每三角形 3 条边 ----
    QVector<float> wireData;
    wireData.reserve( m_mesh.indices.size() * 2 * 3 );

    for ( int i = 0; i + 2 < m_mesh.indices.size(); i += 3 )
    {
        QVector3D v0 = m_mesh.vertices[ m_mesh.indices[i]   ];
        QVector3D v1 = m_mesh.vertices[ m_mesh.indices[i+1] ];
        QVector3D v2 = m_mesh.vertices[ m_mesh.indices[i+2] ];

        auto push = [&]( const QVector3D &a, const QVector3D &b ) {
            wireData << a.x() << a.y() << a.z();
            wireData << b.x() << b.y() << b.z();
        };
        push( v0, v1 );
        push( v1, v2 );
        push( v2, v0 );
    }
    m_wireCount = wireData.size() / 3;

    m_vboWire.bind();
    m_vboWire.allocate( wireData.constData(), wireData.size() * sizeof(float) );
    m_vboWire.release();

    m_gpuDirty = false;
}

// ============================================================
void PreviewGLWidget::paintGL()
{
    if ( m_gpuDirty && !m_mesh.isEmpty() )
        uploadMesh();

    glClearColor( 0.18f, 0.18f, 0.18f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    if ( m_mesh.isEmpty() ) return;

    // ---- 构建 MVP 矩阵 ----
    QMatrix4x4 proj;
    proj.perspective( 45.0f, m_aspect, 0.01f, 1000.0f );

    float dist = m_radius * 2.8f / m_zoom;
    QMatrix4x4 view;
    view.lookAt( QVector3D(0,0,dist), QVector3D(0,0,0), QVector3D(0,1,0) );

    QMatrix4x4 model;
    model.rotate( m_rotX, 1, 0, 0 );
    model.rotate( m_rotZ, 0, 0, 1 );
    model.translate( -m_center );

    QMatrix4x4 mvp = proj * view * model;
    QMatrix3x3 normalMat = model.normalMatrix();

    // ---- 画实心面 ----
    glEnable( GL_DEPTH_TEST );
    glEnable( GL_POLYGON_OFFSET_FILL );
    glPolygonOffset( 1.0f, 1.0f );

    m_program->bind();
    m_program->setUniformValue( "uMVP",       mvp );
    m_program->setUniformValue( "uNormalMat", normalMat );

    m_vbo.bind();
    int posLoc    = m_program->attributeLocation( "aPos" );
    int normalLoc = m_program->attributeLocation( "aNormal" );
    m_program->enableAttributeArray( posLoc );
    m_program->enableAttributeArray( normalLoc );
    m_program->setAttributeBuffer( posLoc,    GL_FLOAT, 0,               3, 6*sizeof(float) );
    m_program->setAttributeBuffer( normalLoc, GL_FLOAT, 3*sizeof(float), 3, 6*sizeof(float) );

    glDrawArrays( GL_TRIANGLES, 0, m_faceCount );

    m_program->disableAttributeArray( posLoc );
    m_program->disableAttributeArray( normalLoc );
    m_vbo.release();
    m_program->release();
    glDisable( GL_POLYGON_OFFSET_FILL );

    // ---- 画线框 ----
    m_programWire->bind();
    m_programWire->setUniformValue( "uMVP", mvp );

    m_vboWire.bind();
    int posWire = m_programWire->attributeLocation( "aPos" );
    m_programWire->enableAttributeArray( posWire );
    m_programWire->setAttributeBuffer( posWire, GL_FLOAT, 0, 3, 3*sizeof(float) );

    glDrawArrays( GL_LINES, 0, m_wireCount );

    m_programWire->disableAttributeArray( posWire );
    m_vboWire.release();
    m_programWire->release();
}

// ============================================================
void PreviewGLWidget::computeBoundingInfo()
{
    if ( m_mesh.vertices.isEmpty() )
    {
        m_center = QVector3D(0,0,0);
        m_radius = 1.0f;
        return;
    }
    QVector3D mn = m_mesh.vertices[0];
    QVector3D mx = m_mesh.vertices[0];
    for ( const auto &v : m_mesh.vertices )
    {
        mn.setX( qMin(mn.x(), v.x()) ); mx.setX( qMax(mx.x(), v.x()) );
        mn.setY( qMin(mn.y(), v.y()) ); mx.setY( qMax(mx.y(), v.y()) );
        mn.setZ( qMin(mn.z(), v.z()) ); mx.setZ( qMax(mx.z(), v.z()) );
    }
    m_center = ( mn + mx ) * 0.5f;
    m_radius = ( mx - mn ).length() * 0.5f;
    if ( m_radius < 0.001f ) m_radius = 1.0f;
}

// ============================================================
void PreviewGLWidget::mousePressEvent( QMouseEvent *event )
{
    m_lastPos = event->pos();
}

void PreviewGLWidget::mouseMoveEvent( QMouseEvent *event )
{
    int dx = event->pos().x() - m_lastPos.x();
    int dy = event->pos().y() - m_lastPos.y();
    if ( event->buttons() & Qt::LeftButton )
    {
        m_rotZ += dx * 0.5f;
        m_rotX += dy * 0.5f;
        update();
    }
    m_lastPos = event->pos();
}

void PreviewGLWidget::wheelEvent( QWheelEvent *event )
{
    float delta = event->angleDelta().y() / 120.0f;
    m_zoom *= ( 1.0f + delta * 0.1f );
    m_zoom = qBound( 0.1f, m_zoom, 20.0f );
    update();
				event->accept();  // ← 加这行，阻止事件继续传给 QScrollArea
}