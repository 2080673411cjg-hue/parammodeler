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

  static QVector<float> vertices( const QSize &viewport, const QPointF *source, const QPointF *target,
                                  const QVector<QPointF> &faces = {},
                                  const QVector<QPointF> &weakFaces = {} )
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
    for ( const QPointF &face : faces )
    {
      line( face + QPointF( -6, 0 ), face + QPointF( 6, 0 ), 6, Qt::black );
      line( face + QPointF( 0, -6 ), face + QPointF( 0, 6 ), 6, Qt::black );
      line( face + QPointF( -6, 0 ), face + QPointF( 6, 0 ), 3, QColor( 0, 230, 230 ) );
      line( face + QPointF( 0, -6 ), face + QPointF( 0, 6 ), 3, QColor( 0, 230, 230 ) );
    }
    for ( const QPointF &face : weakFaces )
    {
      line( face + QPointF( -6, 0 ), face + QPointF( 6, 0 ), 6, Qt::black );
      line( face + QPointF( 0, -6 ), face + QPointF( 0, 6 ), 6, Qt::black );
      line( face + QPointF( -6, 0 ), face + QPointF( 6, 0 ), 3, QColor( 255, 150, 0 ) );
      line( face + QPointF( 0, -6 ), face + QPointF( 0, 6 ), 3, QColor( 255, 150, 0 ) );
    }
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

  void update( const QSize &viewport, const QPointF *source, const QPointF *target,
               const QVector<QPointF> &faces = {},
               const QVector<QPointF> &weakFaces = {} )
  {
    if ( !mEntity ) return;
    const QVector<float> data = vertices( viewport, source, target, faces, weakFaces );
    setVertexData( data );
  }

  static QVector<float> verticesHandles3D(
    const QSize &viewport,
    const QVector<QPointF> &handleCenters,
    int activeHandle,
    const QVector<QPair<QPointF, QPointF>> &movingSegments = {},
    const QVector<QPair<QPointF, QPointF>> &fixedSegments = {} )
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
      if ( length <= 1e-4 ) return;
      const QPointF n( -d.y() * width / ( 2 * length ), d.x() * width / ( 2 * length ) );
      vertex( a + n, color ); vertex( a - n, color ); vertex( b - n, color );
      vertex( a + n, color ); vertex( b - n, color ); vertex( b + n, color );
    };
    auto circleRing = [&]( const QPointF &center, double radius, double width, const QColor &color, int sides = 12 ) {
      const double twoPi = 2.0 * std::acos( -1.0 );
      for ( int i = 0; i < sides; ++i )
      {
        const double a0 = twoPi * i / sides;
        const double a1 = twoPi * ( i + 1 ) / sides;
        const QPointF p0( center.x() + radius * std::cos( a0 ), center.y() + radius * std::sin( a0 ) );
        const QPointF p1( center.x() + radius * std::cos( a1 ), center.y() + radius * std::sin( a1 ) );
        line( p0, p1, width, color );
      }
    };
    auto dashedLine = [&]( const QPointF &a, const QPointF &b, double width, const QColor &color ) {
      const QPointF d = b - a;
      const double total = std::hypot( d.x(), d.y() );
      if ( total < 1.0 )
      {
        circleRing( a, 7.5, width, color );
        return;
      }
      const QPointF dir = d / total;
      constexpr double dash = 8.0, gap = 5.0;
      for ( double pos = 0.0; pos < total; pos += dash + gap )
      {
        const double end = std::min( total, pos + dash );
        line( a + dir * pos, a + dir * end, width, color );
      }
    };

    const QColor purple( 115, 61, 179 );
    const QColor orange( 208, 105, 13 );
    const QColor activeOrange( 235, 110, 0 );

    for ( const auto &seg : fixedSegments )
    {
      dashedLine( seg.first, seg.second, 5.0, Qt::white );
      dashedLine( seg.first, seg.second, 2.8, purple );
    }
    for ( const auto &seg : movingSegments )
    {
      if ( std::hypot( seg.second.x() - seg.first.x(), seg.second.y() - seg.first.y() ) < 1.0 )
      {
        circleRing( seg.first, 7.5, 5.0, Qt::white );
        circleRing( seg.first, 7.5, 2.8, orange );
      }
      else
      {
        line( seg.first, seg.second, 5.0, Qt::white );
        line( seg.first, seg.second, 2.8, orange );
      }
    }
    for ( int i = 0; i < handleCenters.size(); ++i )
    {
      const bool active = ( i == activeHandle );
      const double radius = active ? 6.8 : 5.2;
      circleRing( handleCenters[i], radius, active ? 5.2 : 4.2, Qt::white );
      circleRing( handleCenters[i], radius, active ? 2.8 : 2.2, active ? activeOrange : orange );
    }
    return data;
  }

  void updateHandles3D( const QSize &viewport,
                        const QVector<QPointF> &handleCenters,
                        int activeHandle,
                        const QVector<QPair<QPointF, QPointF>> &movingSegments = {},
                        const QVector<QPair<QPointF, QPointF>> &fixedSegments = {} )
  {
    if ( !mEntity ) return;
    setVertexData( verticesHandles3D( viewport, handleCenters, activeHandle, movingSegments, fixedSegments ) );
  }

private:
  void setVertexData( const QVector<float> &data )
  {
    mBuffer->setData( QByteArray( reinterpret_cast<const char *>( data.constData() ), data.size() * sizeof( float ) ) );
    const int count = data.size() / 6;
    mPosition->setCount( count );
    mColor->setCount( count );
    mRenderer->setVertexCount( count );
    mEntity->setEnabled( count > 0 );
  }
  QPointer<Qt3DCore::QEntity> mEntity;
  Qt3DRender::QBuffer *mBuffer = nullptr;
  Qt3DRender::QAttribute *mPosition = nullptr;
  Qt3DRender::QAttribute *mColor = nullptr;
  Qt3DRender::QGeometryRenderer *mRenderer = nullptr;
};

#endif
