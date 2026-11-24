#include "../parammodeler_pick3d_overlay.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QImage>
#include <iostream>
#include <cstdlib>

static void check( bool condition, const char *name )
{
  if ( !condition ) { std::cerr << "FAIL: " << name << '\n'; std::exit( 1 ); }
}

int main( int argc, char **argv )
{
  QGuiApplication app( argc, argv );
  QSurfaceFormat format;
  format.setVersion( 3, 2 );
  format.setProfile( QSurfaceFormat::CoreProfile );
  QOpenGLContext context;
  context.setFormat( format );
  check( context.create(), "OpenGL context" );
  QOffscreenSurface surface;
  surface.setFormat( context.format() );
  surface.create();
  check( context.makeCurrent( &surface ), "offscreen surface" );
  QOpenGLShaderProgram shader;
  check( shader.addShaderFromSourceCode( QOpenGLShader::Vertex, ParamModelerPickOverlay::vertexShader() ), "vertex shader" );
  check( shader.addShaderFromSourceCode( QOpenGLShader::Fragment, ParamModelerPickOverlay::fragmentShader() ), "fragment shader" );
  check( shader.link(), "shader link" );
  auto *gl = context.functions();
  for ( bool fit : { false, true } )
  for ( const QSize size : { QSize( 800, 600 ), QSize( 400, 300 ), QSize( 1600, 1200 ) } )
  {
    const QPointF source( size.width() / 3, size.height() / 2 );
    const QPointF target( size.width() * 2 / 3, size.height() / 2 );
    const QVector<QPointF> faces = fit ? QVector<QPointF>{ QPointF( size.width() / 3, size.height() / 4 ),
      QPointF( size.width() * 2 / 3, size.height() / 4 ) } : QVector<QPointF>();
    const QVector<QPointF> weakFaces = fit ? QVector<QPointF>{ QPointF( size.width() / 2, size.height() * 3 / 4 ) } : QVector<QPointF>();
    const auto vertices = ParamModelerPickOverlay::vertices( size, &source, &target, faces, weakFaces );
    QOpenGLFramebufferObject framebuffer( size, QOpenGLFramebufferObject::CombinedDepthStencil );
    check( framebuffer.bind(), "framebuffer" );
    gl->glViewport( 0, 0, size.width(), size.height() );
    gl->glClearColor( 0.12f, 0.2f, 0.3f, 1 );
    gl->glDepthMask( GL_TRUE );
    gl->glClearDepthf( 0 );
    gl->glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    gl->glEnable( GL_DEPTH_TEST );
    gl->glDepthFunc( GL_ALWAYS );
    gl->glDepthMask( GL_FALSE );
    QOpenGLVertexArrayObject vao;
    check( vao.create(), "vertex array" );
    vao.bind();
    QOpenGLBuffer buffer;
    check( buffer.create() && buffer.bind(), "vertex buffer" );
    buffer.allocate( vertices.constData(), vertices.size() * sizeof( float ) );
    check( shader.bind(), "shader bind" );
    shader.enableAttributeArray( "vertexPosition" );
    shader.enableAttributeArray( "vertexColor" );
    shader.setAttributeBuffer( "vertexPosition", GL_FLOAT, 0, 3, 6 * sizeof( float ) );
    shader.setAttributeBuffer( "vertexColor", GL_FLOAT, 3 * sizeof( float ), 3, 6 * sizeof( float ) );
    gl->glDrawArrays( GL_TRIANGLES, 0, vertices.size() / 6 );
    gl->glFinish();
    check( gl->glGetError() == GL_NO_ERROR, "render" );
    const QImage image = framebuffer.toImage();
    int red = 0, white = 0, yellow = 0, cyan = 0, orange = 0;
    for ( int y = 0; y < image.height(); ++y )
      for ( int x = 0; x < image.width(); ++x )
      {
        const QColor color = image.pixelColor( x, y );
        if ( color.red() > 200 && color.green() < 80 && color.blue() < 80 )
        {
          ++red;
          check( std::abs( x - source.x() ) <= 16 && std::abs( y - source.y() ) <= 16, "source pixel placement" );
        }
        if ( color.red() > 230 && color.green() > 230 && color.blue() > 230 ) ++white;
        if ( color.red() > 230 && color.green() > 180 && color.green() < 230 && color.blue() < 30 ) ++yellow;
        if ( color.red() < 40 && color.green() > 200 && color.blue() > 200 ) ++cyan;
        if ( color.red() > 230 && color.green() > 110 && color.green() < 190 && color.blue() < 40 ) ++orange;
      }
    check( red > 100 && white > 100 && yellow > 100, "visible red X, white outline and yellow target" );
    check( fit ? cyan > 100 : cyan == 0, "selected face markers only in fitting mode" );
    check( fit ? orange > 40 : orange == 0, "weak face markers only in fitting mode" );
    if ( argc > 1 ) check( image.save( QString::fromLocal8Bit( argv[1] ) + QStringLiteral( "/markers_%1_%2.png" ).arg( size.width() ).arg( fit ) ), "screenshot" );
    std::cout << "PASS: overlay " << size.width() << "x" << size.height()
              << " red=" << red << " white=" << white << " yellow=" << yellow
              << " cyan=" << cyan << " orange=" << orange << '\n';
  }
  {
    const QSize size( 800, 600 );
    const QVector<QPointF> handles{ QPointF( 200, 300 ), QPointF( 400, 300 ), QPointF( 600, 300 ) };
    const QVector<QPair<QPointF, QPointF>> movingEdges{ { QPointF( 200, 200 ), QPointF( 200, 400 ) } };
    const QVector<QPair<QPointF, QPointF>> fixedEdges{ { QPointF( 600, 200 ), QPointF( 600, 400 ) } };
    const auto vertices = ParamModelerPickOverlay::verticesHandles3D( size, handles, 0, movingEdges, fixedEdges );
    QOpenGLFramebufferObject framebuffer( size, QOpenGLFramebufferObject::CombinedDepthStencil );
    check( framebuffer.bind(), "handles3d framebuffer" );
    gl->glViewport( 0, 0, size.width(), size.height() );
    gl->glClearColor( 0.12f, 0.2f, 0.3f, 1 );
    gl->glDepthMask( GL_TRUE );
    gl->glClearDepthf( 0 );
    gl->glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    gl->glEnable( GL_DEPTH_TEST );
    gl->glDepthFunc( GL_ALWAYS );
    gl->glDepthMask( GL_FALSE );
    QOpenGLVertexArrayObject vao;
    check( vao.create(), "handles3d vertex array" );
    vao.bind();
    QOpenGLBuffer buffer;
    check( buffer.create() && buffer.bind(), "handles3d vertex buffer" );
    buffer.allocate( vertices.constData(), vertices.size() * sizeof( float ) );
    check( shader.bind(), "handles3d shader bind" );
    shader.enableAttributeArray( "vertexPosition" );
    shader.enableAttributeArray( "vertexColor" );
    shader.setAttributeBuffer( "vertexPosition", GL_FLOAT, 0, 3, 6 * sizeof( float ) );
    shader.setAttributeBuffer( "vertexColor", GL_FLOAT, 3 * sizeof( float ), 3, 6 * sizeof( float ) );
    gl->glDrawArrays( GL_TRIANGLES, 0, vertices.size() / 6 );
    gl->glFinish();
    check( gl->glGetError() == GL_NO_ERROR, "handles3d render" );
    const QImage image = framebuffer.toImage();
    int orange = 0, purple = 0, white = 0;
    for ( int y = 0; y < image.height(); ++y )
      for ( int x = 0; x < image.width(); ++x )
      {
        const QColor color = image.pixelColor( x, y );
        if ( color.red() > 195 && color.green() > 90 && color.green() < 150 && color.blue() < 30 ) ++orange;
        if ( color.red() > 100 && color.red() < 130 && color.green() > 45 && color.green() < 75 && color.blue() > 160 ) ++purple;
        if ( color.red() > 230 && color.green() > 230 && color.blue() > 230 ) ++white;
      }
    check( orange > 100 && purple > 50 && white > 100, "visible 3D handles, orange moving edges and purple fixed edges" );
    std::cout << "PASS: handles3d orange=" << orange << " purple=" << purple << " white=" << white << '\n';
  }
}
