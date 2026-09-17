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
  for ( const QSize size : { QSize( 800, 600 ), QSize( 400, 300 ), QSize( 1600, 1200 ) } )
  {
    const QPointF source( size.width() / 3, size.height() / 2 );
    const QPointF target( size.width() * 2 / 3, size.height() / 2 );
    const auto vertices = ParamModelerPickOverlay::vertices( size, &source, &target );
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
    int red = 0, white = 0, yellow = 0;
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
      }
    check( red > 100 && white > 100 && yellow > 100, "visible red X, white outline and yellow target" );
    if ( argc > 1 ) check( image.save( QString::fromLocal8Bit( argv[1] ) + QStringLiteral( "/markers_%1.png" ).arg( size.width() ) ), "screenshot" );
    std::cout << "PASS: overlay " << size.width() << "x" << size.height()
              << " red=" << red << " white=" << white << " yellow=" << yellow << '\n';
  }
}
