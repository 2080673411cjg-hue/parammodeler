#include "../parammodeler_fit_overlay.h"
#include "../parammodeler_pick3d_overlay.h"
#include <QFontDatabase>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <cassert>
#include <iostream>

int main( int argc, char **argv )
{
  QGuiApplication app( argc, argv );
  QFontDatabase::addApplicationFont( QStringLiteral( "C:/Windows/Fonts/msyh.ttc" ) );
  QGuiApplication::setFont( QFont( QStringLiteral( "Microsoft YaHei" ), 10 ) );
  const QString text = QString::fromUtf8( "拟合残差 RMSE：0.068\nP95：0.145\n单位：场景坐标单位" );
  assert( ParamModelerFitOverlay::captionImage( {}, { 640, 480 }, app.font() ).isNull() );
  assert( ParamModelerFitOverlay::captionImage( text, { 20, 20 }, app.font() ).isNull() );

  QSurfaceFormat format;
  format.setVersion( 3, 2 );
  format.setProfile( QSurfaceFormat::CoreProfile );
  QOpenGLContext context;
  context.setFormat( format );
  assert( context.create() );
  QOffscreenSurface surface;
  surface.setFormat( context.format() );
  surface.create();
  assert( context.makeCurrent( &surface ) );
  auto *gl = context.functions();
  QOpenGLShaderProgram sceneShader, metricShader;
  assert( sceneShader.addShaderFromSourceCode( QOpenGLShader::Vertex, ParamModelerPickOverlay::vertexShader() ) );
  assert( sceneShader.addShaderFromSourceCode( QOpenGLShader::Fragment, ParamModelerPickOverlay::fragmentShader() ) );
  assert( sceneShader.link() );
  assert( metricShader.addShaderFromSourceCode( QOpenGLShader::Vertex, ParamModelerFitOverlay::vertexShader() ) );
  assert( metricShader.addShaderFromSourceCode( QOpenGLShader::Fragment, ParamModelerFitOverlay::fragmentShader() ) );
  assert( metricShader.link() );

  const int windows = QGuiApplication::topLevelWindows().size();
  auto *root = new Qt3DCore::QEntity;
  ParamModelerFitOverlay overlay( root );
  overlay.update( { 800, 600 }, text, 1 );
  auto *entity = root->findChild<Qt3DCore::QEntity *>( QStringLiteral( "ParamModeler_FitMetrics" ) );
  assert( entity && entity->isEnabled() );
  overlay.update( { 800, 600 }, {}, 1 );
  assert( !entity->isEnabled() );
  assert( QGuiApplication::topLevelWindows().size() == windows );

  for ( const QSize viewport : { QSize( 800, 600 ), QSize( 320, 240 ), QSize( 1280, 720 ) } )
  for ( const int dpr : { 1, 2 } )
  {
    const QSize pixels = viewport * dpr;
    QOpenGLFramebufferObject framebuffer( pixels, QOpenGLFramebufferObject::CombinedDepthStencil );
    assert( framebuffer.bind() );
    gl->glViewport( 0, 0, pixels.width(), pixels.height() );
    gl->glEnable( GL_DEPTH_TEST );
    gl->glDepthFunc( GL_LESS );
    gl->glDepthMask( GL_TRUE );
    gl->glClearDepthf( 1 );
    gl->glClearColor( 0, 0, 0, 1 );
    gl->glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    QOpenGLVertexArrayObject vao;
    assert( vao.create() );
    vao.bind();
    QOpenGLBuffer buffer;
    assert( buffer.create() && buffer.bind() );
    QVector<float> model { -.65f, -.6f, .4f, .8f, .1f, .1f, .65f, -.6f, .4f, .8f, .1f, .1f,
                            0, .5f, .4f, .8f, .1f, .1f };
    buffer.allocate( model.constData(), model.size() * sizeof( float ) );
    assert( sceneShader.bind() );
    sceneShader.enableAttributeArray( "vertexPosition" );
    sceneShader.enableAttributeArray( "vertexColor" );
    sceneShader.setAttributeBuffer( "vertexPosition", GL_FLOAT, 0, 3, 6 * sizeof( float ) );
    sceneShader.setAttributeBuffer( "vertexColor", GL_FLOAT, 3 * sizeof( float ), 3, 6 * sizeof( float ) );
    gl->glDrawArrays( GL_TRIANGLES, 0, 3 );
    QVector<float> points;
    for ( int i = 0; i < 120; ++i )
      points << float( -.7 + ( i % 20 ) * .07 ) << float( -.75 + ( i / 20 ) * .06 ) << .2f << 0.f << 1.f << 1.f;
    buffer.allocate( points.constData(), points.size() * sizeof( float ) );
    gl->glDrawArrays( GL_POINTS, 0, points.size() / 6 );
    gl->glFinish();
    const QImage before = framebuffer.toImage();
    assert( framebuffer.bind() );

    const QImage caption = ParamModelerFitOverlay::captionImage( text, viewport, app.font(), dpr );
    assert( !caption.isNull() );
    const auto vertices = ParamModelerFitOverlay::vertices( viewport, caption.size(), dpr );
    assert( vertices.size() == 30 );
    buffer.allocate( vertices.constData(), vertices.size() * sizeof( float ) );
    assert( metricShader.bind() );
    metricShader.enableAttributeArray( "vertexPosition" );
    metricShader.enableAttributeArray( "vertexTexCoord" );
    metricShader.setAttributeBuffer( "vertexPosition", GL_FLOAT, 0, 3, 5 * sizeof( float ) );
    metricShader.setAttributeBuffer( "vertexTexCoord", GL_FLOAT, 3 * sizeof( float ), 2, 5 * sizeof( float ) );
    QOpenGLTexture texture( caption );
    texture.setMinificationFilter( QOpenGLTexture::Linear );
    texture.setMagnificationFilter( QOpenGLTexture::Linear );
    texture.setWrapMode( QOpenGLTexture::ClampToEdge );
    texture.bind( 0 );
    metricShader.setUniformValue( "fitTexture", 0 );
    gl->glDepthFunc( GL_ALWAYS );
    gl->glDepthMask( GL_FALSE );
    gl->glEnable( GL_BLEND );
    gl->glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    gl->glDrawArrays( GL_TRIANGLES, 0, 6 );
    gl->glFinish();
    assert( gl->glGetError() == GL_NO_ERROR );
    const QImage after = framebuffer.toImage();
    const QRect captionRect( QPoint( 12 * dpr, 12 * dpr ), caption.size() );
    int red = 0, cyan = 0, white = 0;
    for ( int y = 0; y < after.height(); ++y )
      for ( int x = 0; x < after.width(); ++x )
      {
        const QColor color = after.pixelColor( x, y );
        if ( !captionRect.contains( x, y ) ) assert( before.pixel( x, y ) == after.pixel( x, y ) );
        if ( color.red() > 180 && color.green() < 60 && color.blue() < 60 ) ++red;
        if ( color.red() < 40 && color.green() > 200 && color.blue() > 200 ) ++cyan;
        if ( color.red() > 180 && color.green() > 180 && color.blue() > 180 ) ++white;
      }
    assert( red > 1000 && cyan > 50 && white > 100 );
    overlay.update( viewport, text, dpr );
    assert( QGuiApplication::topLevelWindows().size() == windows );
    if ( viewport == QSize( 800, 600 ) && dpr == 1 )
      after.save( QStringLiteral( "tests/.fit_overlay_gpu.png" ) );
    std::cout << "PASS: GPU overlay " << viewport.width() << "x" << viewport.height() << " DPR=" << dpr
              << "; model/cloud preserved, no pixels changed outside caption, no native windows\n";
    gl->glDisable( GL_BLEND );
  }
  delete root;
}
