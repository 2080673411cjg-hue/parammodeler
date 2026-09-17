#ifndef PARAMMODELER_PICK3D_OVERLAY_H
#define PARAMMODELER_PICK3D_OVERLAY_H

#include <Qt3DCore/QEntity>
#include <Qt3DRender/QAttribute>
#include <Qt3DRender/QBuffer>
#include <Qt3DRender/QEffect>
#include <Qt3DRender/QFilterKey>
#include <Qt3DRender/QGeometry>
#include <Qt3DRender/QGeometryRenderer>
#include <Qt3DRender/QGraphicsApiFilter>
#include <Qt3DRender/QMaterial>
#include <Qt3DRender/QNoDepthMask>
#include <Qt3DRender/QRenderPass>
#include <Qt3DRender/QShaderProgram>
#include <Qt3DRender/QTechnique>
#include <QColor>
#include <QPointF>
#include <QPointer>
#include <QSize>
#include <QVector>
#include <cmath>

// Screen-space triangles on QGIS's always-on-top rubber-band render pass.
// No world-space line width, geometry shader, lighting or terrain clamping.
class ParamModelerPickOverlay
{
public:
  static QByteArray vertexShader()
  {
    return QByteArrayLiteral( "#version 150\nin vec3 vertexPosition;\nin vec3 vertexColor;\nout vec3 color;\n"
                              "void main() { gl_Position=vec4(vertexPosition,1.0); color=vertexColor; }\n" );
  }
  static QByteArray fragmentShader()
  {
    return QByteArrayLiteral( "#version 150\nin vec3 color;\nout vec4 fragColor;\n"
                              "void main() { fragColor=vec4(color,1.0); }\n" );
  }

  explicit ParamModelerPickOverlay( Qt3DCore::QEntity *parent )
  {
    mEntity = new Qt3DCore::QEntity( parent );
    mEntity->setObjectName( QStringLiteral( "ParamModeler_PickMarkers" ) );
    auto *geometry = new Qt3DRender::QGeometry( mEntity );
    mBuffer = new Qt3DRender::QBuffer( geometry );
    auto attribute = [&]( const QString &name, int offset ) {
      auto *a = new Qt3DRender::QAttribute( geometry );
      a->setName( name );
      a->setAttributeType( Qt3DRender::QAttribute::VertexAttribute );
      a->setVertexBaseType( Qt3DRender::QAttribute::Float );
      a->setVertexSize( 3 );
      a->setByteStride( 6 * sizeof( float ) );
      a->setByteOffset( offset * sizeof( float ) );
      a->setBuffer( mBuffer );
      geometry->addAttribute( a );
      return a;
    };
    mPosition = attribute( Qt3DRender::QAttribute::defaultPositionAttributeName(), 0 );
    mColor = attribute( Qt3DRender::QAttribute::defaultColorAttributeName(), 3 );
    mRenderer = new Qt3DRender::QGeometryRenderer( mEntity );
    mRenderer->setGeometry( geometry );
    mRenderer->setPrimitiveType( Qt3DRender::QGeometryRenderer::Triangles );
    mEntity->addComponent( mRenderer );
    auto *material = new Qt3DRender::QMaterial( mEntity );
    auto *effect = new Qt3DRender::QEffect( material );
    auto *technique = new Qt3DRender::QTechnique( effect );
    auto *filter = new Qt3DRender::QFilterKey( technique );
    filter->setName( QStringLiteral( "renderingStyle" ) );
    filter->setValue( QStringLiteral( "forward" ) );
    technique->addFilterKey( filter );
    technique->graphicsApiFilter()->setApi( Qt3DRender::QGraphicsApiFilter::OpenGL );
    technique->graphicsApiFilter()->setProfile( Qt3DRender::QGraphicsApiFilter::CoreProfile );
    technique->graphicsApiFilter()->setMajorVersion( 3 );
    technique->graphicsApiFilter()->setMinorVersion( 2 );
    auto *pass = new Qt3DRender::QRenderPass( technique );
    auto *shader = new Qt3DRender::QShaderProgram( pass );
    shader->setVertexShaderCode( vertexShader() );
    shader->setFragmentShaderCode( fragmentShader() );
    pass->setShaderProgram( shader );
    pass->addRenderState( new Qt3DRender::QNoDepthMask( pass ) );
    technique->addRenderPass( pass );
    effect->addTechnique( technique );
    material->setEffect( effect );
    mEntity->addComponent( material );
  }

  ~ParamModelerPickOverlay() { delete mEntity.data(); }

  static QVector<float> vertices( const QSize &viewport, const QPointF *source, const QPointF *target )
  {
    QVector<float> data;
    if ( viewport.isEmpty() ) return data;
    auto vertex = [&]( const QPointF &p, const QColor &color ) {
      data << float( 2 * p.x() / viewport.width() - 1 ) << float( 1 - 2 * p.y() / viewport.height() )
           << 0.0f << float( color.redF() ) << float( color.greenF() ) << float( color.blueF() );
    };
    auto line = [&]( const QPointF &a, const QPointF &b, double width, const QColor &color ) {
      const QPointF d = b - a;
      const double length = std::hypot( d.x(), d.y() );
      if ( length == 0 ) return;
      const QPointF n( -d.y() * width / ( 2 * length ), d.x() * width / ( 2 * length ) );
      vertex( a + n, color ); vertex( a - n, color ); vertex( b - n, color );
      vertex( a + n, color ); vertex( b - n, color ); vertex( b + n, color );
    };
    if ( target )
    {
      const QPointF corners[] = { *target + QPointF( -9, -9 ), *target + QPointF( 9, -9 ),
                                 *target + QPointF( 9, 9 ), *target + QPointF( -9, 9 ) };
      for ( int i = 0; i < 4; ++i ) line( corners[i], corners[( i + 1 ) % 4], 6, Qt::black );
      for ( int i = 0; i < 4; ++i ) line( corners[i], corners[( i + 1 ) % 4], 3, QColor( 255, 210, 0 ) );
    }
    if ( source )
    {
      for ( int sign : { -1, 1 } )
        line( *source + QPointF( -12, sign * -12 ), *source + QPointF( 12, sign * 12 ), 8, Qt::white );
      for ( int sign : { -1, 1 } )
        line( *source + QPointF( -12, sign * -12 ), *source + QPointF( 12, sign * 12 ), 4, QColor( 240, 35, 35 ) );
    }
    return data;
  }

  void update( const QSize &viewport, const QPointF *source, const QPointF *target )
  {
    if ( !mEntity ) return;
    const QVector<float> data = vertices( viewport, source, target );
    mBuffer->setData( QByteArray( reinterpret_cast<const char *>( data.constData() ), data.size() * sizeof( float ) ) );
    const int count = data.size() / 6;
    mPosition->setCount( count );
    mColor->setCount( count );
    mRenderer->setVertexCount( count );
    mEntity->setEnabled( count > 0 );
  }

private:
  QPointer<Qt3DCore::QEntity> mEntity;
  Qt3DRender::QBuffer *mBuffer = nullptr;
  Qt3DRender::QAttribute *mPosition = nullptr;
  Qt3DRender::QAttribute *mColor = nullptr;
  Qt3DRender::QGeometryRenderer *mRenderer = nullptr;
};

#endif
