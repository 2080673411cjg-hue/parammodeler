#ifndef PARAMMODELER_FIT_OVERLAY_H
#define PARAMMODELER_FIT_OVERLAY_H

#include <Qt3DCore/QEntity>
#include <Qt3DRender/QAttribute>
#include <Qt3DRender/QBlendEquation>
#include <Qt3DRender/QBlendEquationArguments>
#include <Qt3DRender/QBuffer>
#include <Qt3DRender/QDepthTest>
#include <Qt3DRender/QEffect>
#include <Qt3DRender/QFilterKey>
#include <Qt3DRender/QGeometry>
#include <Qt3DRender/QGeometryRenderer>
#include <Qt3DRender/QGraphicsApiFilter>
#include <Qt3DRender/QMaterial>
#include <Qt3DRender/QNoDepthMask>
#include <Qt3DRender/QPaintedTextureImage>
#include <Qt3DRender/QParameter>
#include <Qt3DRender/QRenderPass>
#include <Qt3DRender/QShaderProgram>
#include <Qt3DRender/QTechnique>
#include <Qt3DRender/QTexture>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <algorithm>

class ParamModelerFitTexture : public Qt3DRender::QPaintedTextureImage
{
public:
  explicit ParamModelerFitTexture( Qt3DCore::QNode *parent ) : QPaintedTextureImage( parent ) {}
  void setImage( const QImage &image ) { mImage = image; setSize( image.size() ); update(); }
protected:
  void paint( QPainter *painter ) override { painter->drawImage( 0, 0, mImage ); }
private:
  QImage mImage;
};

// A screen-space quad in the existing overlay pass, not a native window.
class ParamModelerFitOverlay
{
public:
  static QByteArray vertexShader()
  {
    return QByteArrayLiteral( "#version 150\nin vec3 vertexPosition;\nin vec2 vertexTexCoord;\nout vec2 uv;\n"
                              "void main(){gl_Position=vec4(vertexPosition,1.0);uv=vertexTexCoord;}\n" );
  }
  static QByteArray fragmentShader()
  {
    return QByteArrayLiteral( "#version 150\nuniform sampler2D fitTexture;\nin vec2 uv;\nout vec4 fragColor;\n"
                              "void main(){fragColor=texture(fitTexture,uv);if(fragColor.a<0.01)discard;}\n" );
  }
  static QImage captionImage( const QString &text, const QSize &viewport, const QFont &font, double dpr = 1.0 )
  {
    if ( text.isEmpty() || viewport.width() <= 24 || dpr <= 0 ) return {};
    const QFontMetrics metrics( font );
    int width = std::max( 240, metrics.horizontalAdvance( QStringLiteral( "Fit RMSE: 9999.999" ) ) + 24 );
    for ( const QString &line : text.split( QLatin1Char( '\n' ) ) )
      width = std::max( width, metrics.horizontalAdvance( line ) + 24 );
    width = std::min( width, viewport.width() - 24 );
    const int height = metrics.boundingRect( QRect( 0, 0, width - 20, 10000 ), Qt::TextWordWrap, text ).height() + 16;
    if ( height + 24 > viewport.height() ) return {};
    QImage image( QSize( qRound( width * dpr ), qRound( height * dpr ) ), QImage::Format_RGBA8888 );
    image.fill( Qt::transparent );
    QPainter painter( &image );
    painter.scale( dpr, dpr );
    painter.setRenderHint( QPainter::Antialiasing );
    painter.setPen( QColor( 102, 102, 102 ) );
    painter.setBrush( QColor( 37, 37, 37 ) );
    painter.drawRoundedRect( QRectF( 0.5, 0.5, width - 1, height - 1 ), 4, 4 );
    painter.setFont( font );
    painter.setPen( QColor( 245, 245, 245 ) );
    painter.drawText( QRect( 10, 8, width - 20, height - 16 ), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text );
    return image;
  }
  static QVector<float> vertices( const QSize &viewport, const QSize &imageSize, double dpr = 1.0 )
  {
    if ( viewport.isEmpty() || imageSize.isEmpty() || dpr <= 0 ) return {};
    const float left = float( 24.0 / viewport.width() - 1.0 );
    const float right = float( 2.0 * ( 12.0 + imageSize.width() / dpr ) / viewport.width() - 1.0 );
    const float top = float( 1.0 - 24.0 / viewport.height() );
    const float bottom = float( 1.0 - 2.0 * ( 12.0 + imageSize.height() / dpr ) / viewport.height() );
    return { left, top, 0, 0, 0, left, bottom, 0, 0, 1, right, bottom, 0, 1, 1,
             left, top, 0, 0, 0, right, bottom, 0, 1, 1, right, top, 0, 1, 0 };
  }

  explicit ParamModelerFitOverlay( Qt3DCore::QEntity *parent )
  {
    mEntity = new Qt3DCore::QEntity( parent );
    mEntity->setObjectName( QStringLiteral( "ParamModeler_FitMetrics" ) );
    auto *geometry = new Qt3DRender::QGeometry( mEntity );
    mBuffer = new Qt3DRender::QBuffer( geometry );
    for ( int i = 0; i < 2; ++i )
    {
      auto *attribute = new Qt3DRender::QAttribute( geometry );
      attribute->setName( i ? Qt3DRender::QAttribute::defaultTextureCoordinateAttributeName() : Qt3DRender::QAttribute::defaultPositionAttributeName() );
      attribute->setAttributeType( Qt3DRender::QAttribute::VertexAttribute );
      attribute->setVertexBaseType( Qt3DRender::QAttribute::Float );
      attribute->setVertexSize( i ? 2 : 3 );
      attribute->setByteStride( 5 * sizeof( float ) );
      attribute->setByteOffset( i ? 3 * sizeof( float ) : 0 );
      attribute->setCount( 6 );
      attribute->setBuffer( mBuffer );
      geometry->addAttribute( attribute );
    }
    auto *renderer = new Qt3DRender::QGeometryRenderer( mEntity );
    renderer->setGeometry( geometry );
    renderer->setPrimitiveType( Qt3DRender::QGeometryRenderer::Triangles );
    renderer->setVertexCount( 6 );
    mEntity->addComponent( renderer );
    auto *material = new Qt3DRender::QMaterial( mEntity );
    auto *texture = new Qt3DRender::QTexture2D( material );
    texture->setGenerateMipMaps( false );
    texture->setMinificationFilter( Qt3DRender::QAbstractTexture::Linear );
    texture->setMagnificationFilter( Qt3DRender::QAbstractTexture::Linear );
    mTextureImage = new ParamModelerFitTexture( texture );
    texture->addTextureImage( mTextureImage );
    material->addParameter( new Qt3DRender::QParameter( QStringLiteral( "fitTexture" ), texture, material ) );
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
    auto *depth = new Qt3DRender::QDepthTest( pass );
    depth->setDepthFunction( Qt3DRender::QDepthTest::Always );
    pass->addRenderState( depth );
    auto *blend = new Qt3DRender::QBlendEquationArguments( pass );
    blend->setSourceRgba( Qt3DRender::QBlendEquationArguments::SourceAlpha );
    blend->setDestinationRgba( Qt3DRender::QBlendEquationArguments::OneMinusSourceAlpha );
    pass->addRenderState( blend );
    pass->addRenderState( new Qt3DRender::QBlendEquation( pass ) );
    technique->addRenderPass( pass );
    effect->addTechnique( technique );
    material->setEffect( effect );
    mEntity->addComponent( material );
    mEntity->setEnabled( false );
  }
  ~ParamModelerFitOverlay() { delete mEntity.data(); }
  void update( const QSize &viewport, const QString &text, double dpr )
  {
    if ( !mEntity ) return;
    const QImage image = captionImage( text, viewport, QGuiApplication::font(), dpr );
    if ( image.isNull() ) { mEntity->setEnabled( false ); return; }
    mTextureImage->setImage( image );
    const auto data = vertices( viewport, image.size(), dpr );
    mBuffer->setData( QByteArray( reinterpret_cast<const char *>( data.constData() ), data.size() * sizeof( float ) ) );
    mEntity->setEnabled( true );
  }
private:
  QPointer<Qt3DCore::QEntity> mEntity;
  Qt3DRender::QBuffer *mBuffer = nullptr;
  ParamModelerFitTexture *mTextureImage = nullptr;
};

#endif
