#include "parammodeler_pointnet.h"
#include "parammodeler_config.h"
#include "parammodeler_pcdloader.h"
#include "parammodeler_dock.h"
#include "ui_parammodeler_dock.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryFile>
#include <QTextStream>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
struct PointNetBackendConfig
{
  QString name;
  QString scriptPath;
  QString logDir;
};

struct PointNetRegressionConfig
{
  QString modelName;
  QString className;
  QString scriptPath;
  QString logDir;
};

struct PreparedPointCloudInput
{
  QString pythonInputPath;
  PointCloud pointCloud;
  std::unique_ptr<QTemporaryFile> tempFile;
  QString errorMessage;
};

const QString defaultPythonExe()
{
  return ParamModelerConfig::pythonExe();
}

PointNetBackendConfig backendConfig( PointNetBackend backend )
{
  if ( backend == PointNetBackend::PointNet )
  {
    return {
      QStringLiteral( "PointNet" ),
      ParamModelerConfig::classifyScript( backend ),
      ParamModelerConfig::classifyLogDir( backend )
    };
  }
  if ( backend == PointNetBackend::PointNeXt )
  {
    return {
      QStringLiteral( "PointNeXt" ),
      ParamModelerConfig::classifyScript( backend ),
      ParamModelerConfig::classifyLogDir( backend )
    };
  }

  return {
    QStringLiteral( "PointNet++" ),
    ParamModelerConfig::classifyScript( backend ),
    ParamModelerConfig::classifyLogDir( backend )
  };
}

QString normalizedPrimitiveType( const QString &primitiveType )
{
  if ( primitiveType == QStringLiteral( "CylinderHemisphere" ) )
    return QStringLiteral( "CylinderDome" );
  return primitiveType;
}

PointNetRegressionConfig regressionConfig( PointNetBackend backend, const QString &primitiveType )
{
  const QString prim = normalizedPrimitiveType( primitiveType );
  if ( backend == PointNetBackend::PointNet )
    return { QStringLiteral( "PointNet" ), prim, QString(), QString() };

  const bool usePointNeXt = backend == PointNetBackend::PointNeXt;
  const QString modelName = usePointNeXt ? QStringLiteral( "PointNeXt" ) : QStringLiteral( "PointNet++" );
  const QString script = ParamModelerConfig::regressionScript( backend );
  const QString base   = ParamModelerConfig::regressionLogBase( backend );

  // Stem names (class → directory-safe short name).  These are stable
  // across model versions; only the prefix/suffix change between runs.
  static const QMap<QString, QString> stemNames = {
    { QStringLiteral( "Cuboid" ),               QStringLiteral( "cuboid" ) },
    { QStringLiteral( "Cylinder" ),             QStringLiteral( "cylinder" ) },
    { QStringLiteral( "LHouse" ),               QStringLiteral( "lhouse" ) },
    { QStringLiteral( "ConeCylinder" ),         QStringLiteral( "conecylinder" ) },
    { QStringLiteral( "GabledRoof" ),           QStringLiteral( "gabledroof" ) },
    { QStringLiteral( "PyramidRoof" ),          QStringLiteral( "pyramidroof" ) },
    { QStringLiteral( "TruncatedPyramidRoof" ), QStringLiteral( "truncatedpyramid" ) },
    { QStringLiteral( "HalfCylinderRoof" ),     QStringLiteral( "halfcylinder" ) },
    { QStringLiteral( "CylinderDome" ),         QStringLiteral( "cylinderdome" ) },
    { QStringLiteral( "IndentedCuboid" ),       QStringLiteral( "indentedcuboid" ) },
    { QStringLiteral( "AsymmetricGableHouse" ), QStringLiteral( "asymgable" ) },
    { QStringLiteral( "FourStageRoundTower" ),  QStringLiteral( "fourstage" ) },
    { QStringLiteral( "TwoGableHouses" ),       QStringLiteral( "twogable" ) }
  };

  if ( !stemNames.contains( prim ) )
    return { modelName, prim, script, QString() };

  const QString prefix = usePointNeXt
    ? ParamModelerConfig::regressionModelPrefix()
    : QStringLiteral( "reg_" );
  const QString suffix = usePointNeXt
    ? ParamModelerConfig::regressionModelSuffix()
    : QStringLiteral( "_aux" );
  const QString dirName = prefix + stemNames.value( prim ) + suffix;

  return { modelName, prim, script, base + dirName };
}

bool vectorFromJsonArray( const QJsonValue &value, QVector3D &out )
{
  if ( !value.isArray() )
    return false;
  const QJsonArray arr = value.toArray();
  if ( arr.size() < 3 )
    return false;
  out = QVector3D( arr.at( 0 ).toDouble(), arr.at( 1 ).toDouble(), arr.at( 2 ).toDouble() );
  return true;
}

QString metadataRelativePathForPointCloud( const QString &filePath )
{
  QString normalized = QDir::fromNativeSeparators( QFileInfo( filePath ).absoluteFilePath() );
  const QString lower = normalized.toLower();
  QString rel;

  const QString previewMarker = QStringLiteral( "/ply_preview/datasets_aug/" );
  const int previewIdx = lower.indexOf( previewMarker );
  if ( previewIdx >= 0 )
    rel = normalized.mid( previewIdx + previewMarker.size() );

  const QString datasetMarker = QStringLiteral( "/datasets_aug/" );
  const int datasetIdx = lower.indexOf( datasetMarker );
  if ( rel.isEmpty() && datasetIdx >= 0 )
    rel = normalized.mid( datasetIdx + datasetMarker.size() );

  if ( rel.isEmpty() || rel.startsWith( QStringLiteral( "metadata/" ), Qt::CaseInsensitive ) )
    return QString();

  QFileInfo relInfo( rel );
  const QString dir = relInfo.path() == QStringLiteral( "." ) ? QString() : relInfo.path() + QStringLiteral( "/" );
  return dir + relInfo.completeBaseName() + QStringLiteral( ".txt" );
}

bool metadataAuxForInput( const QString &filePath, QVector3D &bboxSize, double &scale )
{
  const QString rel = metadataRelativePathForPointCloud( filePath );
  if ( rel.isEmpty() )
    return false;

  QFile metadataFile( ParamModelerConfig::metadataJsonPath() );
  if ( !metadataFile.open( QIODevice::ReadOnly ) )
    return false;

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson( metadataFile.readAll(), &parseError );
  if ( parseError.error != QJsonParseError::NoError || !doc.isArray() )
    return false;

  const QString relLower = rel.toLower();
  const QJsonArray records = doc.array();
  for ( const QJsonValue &value : records )
  {
    const QJsonObject obj = value.toObject();
    if ( obj.value( QStringLiteral( "file" ) ).toString().toLower() != relLower )
      continue;

    const QJsonObject info = obj.value( QStringLiteral( "pointCloudInfo" ) ).toObject();
    if ( !vectorFromJsonArray( info.value( QStringLiteral( "bboxSize" ) ), bboxSize ) )
      return false;
    scale = info.value( QStringLiteral( "scale" ) ).toDouble( 1.0 );
    return scale > 1e-9;
  }

  return false;
}

bool isTextPointCloudFile( const QString &path )
{
  const QString suffix = QFileInfo( path ).suffix().toLower();
  return suffix == QStringLiteral( "txt" ) || suffix == QStringLiteral( "xyz" ) || suffix == QStringLiteral( "pts" );
}

PreparedPointCloudInput preparePointCloudInput( const QString &inputPath )
{
  PreparedPointCloudInput prepared;
  if ( !QFileInfo::exists( inputPath ) )
  {
    prepared.errorMessage = QStringLiteral( "Input point cloud does not exist: %1" ).arg( inputPath );
    return prepared;
  }

  prepared.pointCloud = PointCloudLoader::load( inputPath );
  if ( prepared.pointCloud.points.isEmpty() )
  {
    prepared.errorMessage = QStringLiteral( "Failed to load point cloud or point count is 0: %1" ).arg( inputPath );
    return prepared;
  }

  if ( isTextPointCloudFile( inputPath ) )
  {
    prepared.pythonInputPath = inputPath;
    return prepared;
  }

  prepared.tempFile.reset( new QTemporaryFile( QDir::tempPath() + QStringLiteral( "/parammodeler_pointnet_XXXXXX.txt" ) ) );
  prepared.tempFile->setAutoRemove( true );
  if ( !prepared.tempFile->open() )
  {
    prepared.errorMessage = QStringLiteral( "Failed to create temporary TXT for PointNet: %1" )
      .arg( prepared.tempFile->errorString() );
    return prepared;
  }

  QTextStream out( prepared.tempFile.get() );
  for ( const QVector3D &p : prepared.pointCloud.points )
    out << p.x() << ' ' << p.y() << ' ' << p.z() << '\n';
  out.flush();
  prepared.tempFile->flush();
  prepared.pythonInputPath = prepared.tempFile->fileName();
  return prepared;
}

double pointCloudNormalizationScale( const QVector<QVector3D> &points )
{
  if ( points.isEmpty() )
    return 1.0;

  QVector3D center;
  for ( const QVector3D &p : points )
    center += p;
  center /= static_cast<float>( points.size() );

  double scale = 0.0;
  for ( const QVector3D &p : points )
    scale = std::max( scale, static_cast<double>( ( p - center ).length() ) );
  return scale > 1e-9 ? scale : 1.0;
}

bool runPythonProcess( const QString &scriptPath,
                       const QStringList &args,
                       const QString &name,
                       QString &stdoutText,
                       QString &errorMessage )
{
  QProcess process;
  process.setWorkingDirectory( QFileInfo( scriptPath ).absolutePath() );
  process.start( defaultPythonExe(), args );
  if ( !process.waitForStarted( 5000 ) )
  {
    errorMessage = QStringLiteral( "Failed to start %1 process: %2" ).arg( name, process.errorString() );
    return false;
  }

  const int timeoutMs = 120000;
  int elapsedMs = 0;
  while ( process.state() != QProcess::NotRunning && elapsedMs < timeoutMs )
  {
    process.waitForFinished( 100 );
    QCoreApplication::processEvents();
    elapsedMs += 100;
  }

  if ( process.state() != QProcess::NotRunning )
  {
    process.kill();
    process.waitForFinished( 3000 );
    errorMessage = QStringLiteral( "%1 predict timed out." ).arg( name );
    return false;
  }

  stdoutText = QString::fromUtf8( process.readAllStandardOutput() ).trimmed();
  const QString stderrText = QString::fromUtf8( process.readAllStandardError() ).trimmed();

  if ( process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 )
  {
    errorMessage = QStringLiteral( "%1 predict failed.\n%2" ).arg( name, stderrText );
    return false;
  }
  return true;
}
}

PointNetPredictResult PointNetRunner::predict ( const QString &inputTxt, int numPoints, int topK )
{
  return predict( inputTxt, PointNetBackend::PointNeXt, numPoints, topK );
}

PointNetPredictResult PointNetRunner::predict( const QString &inputTxt,
                                               PointNetBackend backend,
                                               int numPoints,
                                               int topK )
{
  PointNetPredictResult result;
  const PointNetBackendConfig config = backendConfig( backend );

  if ( !QFileInfo::exists( inputTxt ) )
  {
    result.errorMessage = QStringLiteral( "Input TXT does not exist: %1" ).arg( inputTxt );
    return result;
  }
  if ( !QFileInfo::exists( defaultPythonExe() ) )
  {
    result.errorMessage = QStringLiteral( "PointNet python.exe does not exist: %1" ).arg( defaultPythonExe() );
    return result;
  }
  if ( !QFileInfo::exists( config.scriptPath ) )
  {
    result.errorMessage = QStringLiteral( "%1 main.py does not exist: %2" ).arg( config.name, config.scriptPath );
    return result;
  }
  if ( !QFileInfo::exists( config.logDir + QStringLiteral( "/best_model.pth" ) ) )
  {
    result.errorMessage = QStringLiteral( "%1 model does not exist: %2/best_model.pth" ).arg( config.name, config.logDir );
    return result;
  }

  PreparedPointCloudInput prepared = preparePointCloudInput( inputTxt );
  if ( !prepared.errorMessage.isEmpty() )
  {
    result.errorMessage = prepared.errorMessage;
    return result;
  }

  QStringList args;
  args << config.scriptPath
       << QStringLiteral( "--mode" ) << QStringLiteral( "predict" )
       << QStringLiteral( "--input" ) << prepared.pythonInputPath
       << QStringLiteral( "--log_dir" ) << config.logDir
       << QStringLiteral( "--num_points" ) << QString::number( numPoints )
       << QStringLiteral( "--topk" ) << QString::number( topK )
       << QStringLiteral( "--cpu" );

  QString stdoutText;
  if ( !runPythonProcess( config.scriptPath, args, config.name, stdoutText, result.errorMessage ) )
    return result;
  result.rawOutput = stdoutText;

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson( stdoutText.toUtf8(), &parseError );
  if ( parseError.error != QJsonParseError::NoError || !doc.isArray() )
  {
    result.errorMessage = QStringLiteral( "Failed to parse %1 JSON output: %2\n%3" )
      .arg( config.name )
      .arg( parseError.errorString(), stdoutText );
    return result;
  }

  const QJsonArray array = doc.array();
  for ( const QJsonValue &value : array )
  {
    const QJsonObject obj = value.toObject();
    PointNetPrediction pred;
    pred.className = obj.value( QStringLiteral( "class" ) ).toString();
    pred.probability = obj.value( QStringLiteral( "prob" ) ).toDouble();
    if ( !pred.className.isEmpty() )
      result.predictions.append( pred );
  }

  if ( result.predictions.isEmpty() )
    result.errorMessage = QStringLiteral( "%1 returned no predictions." ).arg( config.name );

  return result;
}

PointNetRegressionResult PointNetRunner::predictParams( const QString &inputTxt,
                                                        const QString &primitiveType,
                                                        int numPoints )
{
  return predictParams( inputTxt, PointNetBackend::PointNeXt, primitiveType, numPoints );
}

PointNetRegressionResult PointNetRunner::predictParams( const QString &inputTxt,
                                                        PointNetBackend backend,
                                                        const QString &primitiveType,
                                                        int numPoints )
{
  PointNetRegressionResult result;
  const PointNetRegressionConfig config = regressionConfig( backend, primitiveType );

  if ( config.logDir.isEmpty() )
  {
    result.errorMessage = QStringLiteral( "%1 regression model is not configured for primitive: %2" )
      .arg( config.modelName, primitiveType );
    return result;
  }
  if ( !QFileInfo::exists( defaultPythonExe() ) )
  {
    result.errorMessage = QStringLiteral( "PointNet python.exe does not exist: %1" ).arg( defaultPythonExe() );
    return result;
  }
  if ( !QFileInfo::exists( config.scriptPath ) )
  {
    result.errorMessage = QStringLiteral( "%1 regression script does not exist: %2" ).arg( config.modelName, config.scriptPath );
    return result;
  }
  if ( !QFileInfo::exists( config.logDir + QStringLiteral( "/best_model.pth" ) ) )
  {
    result.errorMessage = QStringLiteral( "%1 %2 regression model does not exist: %3/best_model.pth" )
      .arg( config.modelName, config.className, config.logDir );
    return result;
  }

  PreparedPointCloudInput prepared = preparePointCloudInput( inputTxt );
  if ( !prepared.errorMessage.isEmpty() )
  {
    result.errorMessage = prepared.errorMessage;
    return result;
  }

  QVector3D bboxSize = prepared.pointCloud.bboxMax - prepared.pointCloud.bboxMin;
  double scale = pointCloudNormalizationScale( prepared.pointCloud.points );
  metadataAuxForInput( inputTxt, bboxSize, scale );

  QStringList args;
  args << config.scriptPath
       << QStringLiteral( "--mode" ) << QStringLiteral( "predict" )
       << QStringLiteral( "--input" ) << prepared.pythonInputPath
       << QStringLiteral( "--data_root" ) << ParamModelerConfig::dataRootPath()
       << QStringLiteral( "--metadata" ) << ParamModelerConfig::metadataJsonPath()
       << QStringLiteral( "--log_dir" ) << config.logDir
       << QStringLiteral( "--num_points" ) << QString::number( numPoints )
       << QStringLiteral( "--bbox_x" ) << QString::number( bboxSize.x(), 'g', 12 )
       << QStringLiteral( "--bbox_y" ) << QString::number( bboxSize.y(), 'g', 12 )
       << QStringLiteral( "--bbox_z" ) << QString::number( bboxSize.z(), 'g', 12 )
       << QStringLiteral( "--scale" ) << QString::number( scale, 'g', 12 )
       << QStringLiteral( "--cpu" );

  QString stdoutText;
  if ( !runPythonProcess( config.scriptPath, args, config.modelName + QStringLiteral( " parameter regression" ), stdoutText, result.errorMessage ) )
    return result;
  result.rawOutput = stdoutText;

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson( stdoutText.toUtf8(), &parseError );
  if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
  {
    result.errorMessage = QStringLiteral( "Failed to parse %1 regression JSON output: %2\n%3" )
      .arg( config.modelName )
      .arg( parseError.errorString(), stdoutText );
    return result;
  }

  const QJsonObject obj = doc.object();
  result.className = obj.value( QStringLiteral( "class" ) ).toString( config.className );
  const QJsonObject paramsObj = obj.value( QStringLiteral( "params" ) ).toObject();
  for ( auto it = paramsObj.constBegin(); it != paramsObj.constEnd(); ++it )
    result.params.insert( it.key(), it.value().toDouble() );

  if ( result.params.isEmpty() )
    result.errorMessage = QStringLiteral( "%1 regression returned no parameters." ).arg( config.modelName );

  return result;
}

// ====================================================================
// 将 DL 回归结果写入 UI 控件
// ====================================================================
void PointNetRunner::applyToUI( ParamModelerDock *dock,
                                 const QMap<QString, double> &params )
{
    auto set = [&]( const QString &key, QDoubleSpinBox *spin, QSlider *slider ) {
        if ( params.contains( key ) )
        {
            double v = params[key];
            if ( spin ) spin->setValue( v );
            if ( slider ) slider->setValue( static_cast<int>( v * 100 ) );
        }
    };

    auto setTotalAndWallRatio = [&]( const QString &wallKey, const QString &roofKey,
                                     QDoubleSpinBox *totalSpin, QSlider *totalSlider,
                                     QDoubleSpinBox *ratioSpin, QSlider *ratioSlider ) {
        if ( !params.contains( wallKey ) && !params.contains( roofKey ) )
            return;
        const double currentTotal = totalSpin ? totalSpin->value() : 0.0;
        const double currentRatio = ratioSpin ? ratioSpin->value() : 0.7;
        const double wallH = params.value( wallKey, currentTotal * currentRatio );
        const double roofH = params.value( roofKey, currentTotal * ( 1.0 - currentRatio ) );
        const double totalH = std::max( 0.0, wallH + roofH );
        const double wallRatio = totalH > 1e-6 ? std::max( 0.2, std::min( 0.9, wallH / totalH ) ) : 0.7;
        if ( totalSpin ) totalSpin->setValue( totalH );
        if ( totalSlider ) totalSlider->setValue( static_cast<int>( totalH * 100 ) );
        if ( ratioSpin ) ratioSpin->setValue( wallRatio );
        if ( ratioSlider ) ratioSlider->setValue( static_cast<int>( wallRatio * 100 ) );
    };

    auto setTotalAndCylinderRatio = [&]( const QString &cylKey, const QString &upperKey,
                                         QDoubleSpinBox *totalSpin, QSlider *totalSlider,
                                         QDoubleSpinBox *ratioSpin, QSlider *ratioSlider ) {
        if ( !params.contains( cylKey ) && !params.contains( upperKey ) )
            return;
        const double currentTotal = totalSpin ? totalSpin->value() : 0.0;
        const double currentRatio = ratioSpin ? ratioSpin->value() : 0.7;
        const double cylH = params.value( cylKey, currentTotal * currentRatio );
        const double upperH = params.value( upperKey, currentTotal * ( 1.0 - currentRatio ) );
        const double totalH = std::max( 0.0, cylH + upperH );
        const double cylRatio = totalH > 1e-6 ? std::max( 0.2, std::min( 0.9, cylH / totalH ) ) : 0.7;
        if ( totalSpin ) totalSpin->setValue( totalH );
        if ( totalSlider ) totalSlider->setValue( static_cast<int>( totalH * 100 ) );
        if ( ratioSpin ) ratioSpin->setValue( cylRatio );
        if ( ratioSlider ) ratioSlider->setValue( static_cast<int>( cylRatio * 100 ) );
    };

    set( "length",     dock->ui->spinBoxCLength,     dock->ui->sliderCLength );
    set( "width",     dock->ui->spinBoxCWidth,     dock->ui->sliderCWidth );
    set( "height",    dock->ui->spinBoxCHeight,    dock->ui->sliderCHeight );
    set( "radius",    dock->ui->spinBoxCylRadius,  dock->ui->sliderCylRadius );
    set( "cylHeight", dock->ui->spinBoxCylHeight,  dock->ui->sliderCylHeight );
    set( "lMainL",    dock->ui->spinBoxLMainLength,  dock->ui->sliderLMainLength );
    set( "lMainW",    dock->ui->spinBoxLMainWidth,  dock->ui->sliderLMainWidth );
    set( "lWingL",    dock->ui->spinBoxLWingLength,  dock->ui->sliderLWingLength );
    set( "lWingW",    dock->ui->spinBoxLWingWidth,  dock->ui->sliderLWingWidth );
    set( "lHeight",   dock->ui->spinBoxLHeight,     dock->ui->sliderLHeight );
    set( "ccRadius",      dock->ui->spinBoxConeCylRadius,    dock->ui->sliderConeCylRadius );
    setTotalAndCylinderRatio( "ccCylHeight", "ccConeHeight", dock->ui->spinBoxConeCylCylHeight, dock->ui->sliderConeCylCylHeight, dock->ui->spinBoxConeCylConeHeight, dock->ui->sliderConeCylConeHeight );
    set( "grLength",       dock->ui->spinBoxGRLength,      dock->ui->sliderGRLength );
    set( "grWidth",       dock->ui->spinBoxGRWidth,      dock->ui->sliderGRWidth );
    setTotalAndWallRatio( "grWallHeight", "grRoofHeight", dock->ui->spinBoxGRHeightWall, dock->ui->sliderGRHeightWall, dock->ui->spinBoxGRHeightRoof, dock->ui->sliderGRHeightRoof );
    set( "prLength",       dock->ui->spinBoxPRLength,      dock->ui->sliderPRLength );
    set( "prWidth",       dock->ui->spinBoxPRWidth,      dock->ui->sliderPRWidth );
    setTotalAndWallRatio( "prWallHeight", "prRoofHeight", dock->ui->spinBoxPRHeightWall, dock->ui->sliderPRHeightWall, dock->ui->spinBoxPRHeightRoof, dock->ui->sliderPRHeightRoof );
    set( "tpBottomLength",  dock->ui->spinBoxTPRBottomLength,  dock->ui->sliderTPRBottomLength );
    set( "tpBottomWidth",  dock->ui->spinBoxTPRBottomWidth,  dock->ui->sliderTPRBottomWidth );
    set( "tpTopLength",     dock->ui->spinBoxTPRTopLength,     dock->ui->sliderTPRTopLength );
    set( "tpTopWidth",     dock->ui->spinBoxTPRTopWidth,     dock->ui->sliderTPRTopWidth );
    setTotalAndWallRatio( "tpWallHeight", "tpRoofHeight", dock->ui->spinBoxTPRHeightWall, dock->ui->sliderTPRHeightWall, dock->ui->spinBoxTPRHeightRoof, dock->ui->sliderTPRHeightRoof );
    set( "hcrLength",        dock->ui->spinBoxHCRLength,      dock->ui->sliderHCRLength );
    set( "hcrWidth",        dock->ui->spinBoxHCRWidth,      dock->ui->sliderHCRWidth );
    set( "hcrWallHeight",   dock->ui->spinBoxHCRHeightWall, dock->ui->sliderHCRHeightWall );
    set( "chRadius",     dock->ui->spinBoxCylHemiRadius,     dock->ui->sliderCylHemiRadius );
    setTotalAndCylinderRatio( "chCylHeight", "chDomeHeight", dock->ui->spinBoxCylHemiHeight, dock->ui->sliderCylHemiHeight, dock->ui->spinBoxCylHemiDomeHeight, dock->ui->sliderCylHemiDomeHeight );
    set( "chBulge",      dock->ui->spinBoxCylHemiBulge,      dock->ui->sliderCylHemiBulge );
    set( "icOuterL",    dock->ui->spinBoxICLength,       dock->ui->sliderICLength );
    set( "icOuterW",    dock->ui->spinBoxICWidth,       dock->ui->sliderICWidth );
    set( "icOuterH",    dock->ui->spinBoxICHeight,      dock->ui->sliderICHeight );
    set( "icInnerL",    dock->ui->spinBoxICInnerLength,  dock->ui->sliderICInnerLength );
    set( "icInnerW",    dock->ui->spinBoxICInnerWidth,  dock->ui->sliderICInnerWidth );
    set( "icInnerH",    dock->ui->spinBoxICInnerHeight, dock->ui->sliderICInnerHeight );
    if ( params.contains( "icOffsetX" ) )
    {
        const double movable = std::max( 0.0, dock->ui->spinBoxICLength->value() - dock->ui->spinBoxICInnerLength->value() );
        const double ratio = movable > 1e-6 ? std::max( 0.0, std::min( 1.0, params["icOffsetX"] / movable ) ) : 0.0;
        dock->ui->spinBoxICOffsetX->setValue( ratio );
        dock->ui->sliderICOffsetX->setValue( static_cast<int>( ratio * 100 ) );
    }
    if ( params.contains( "icOffsetY" ) )
    {
        const double movable = std::max( 0.0, dock->ui->spinBoxICWidth->value() - dock->ui->spinBoxICInnerWidth->value() );
        const double ratio = movable > 1e-6 ? std::max( 0.0, std::min( 1.0, params["icOffsetY"] / movable ) ) : 0.0;
        dock->ui->spinBoxICOffsetY->setValue( ratio );
        dock->ui->sliderICOffsetY->setValue( static_cast<int>( ratio * 100 ) );
    }
    set( "aghLength",       dock->ui->spinBoxAGHLength,       dock->ui->sliderAGHLength );
    set( "aghWidth",       dock->ui->spinBoxAGHWidth,       dock->ui->sliderAGHWidth );
    setTotalAndWallRatio( "aghWallHeight", "aghRoofHeight", dock->ui->spinBoxAGHHeightWall, dock->ui->sliderAGHHeightWall, dock->ui->spinBoxAGHRoofHeight, dock->ui->sliderAGHRoofHeight );
    set( "aghRidgeLen",    dock->ui->spinBoxAGHRidgeLength, dock->ui->sliderAGHRidgeLength );
    set( "aghRidgeRatio",  dock->ui->spinBoxAGHRidgeOffset, dock->ui->sliderAGHRidgeOffset );
    set( "ftBaseR",       dock->ui->spinBoxFTBaseRadius,     dock->ui->sliderFTBaseRadius );
    set( "ftBaseH",       dock->ui->spinBoxFTBaseHeight,     dock->ui->sliderFTBaseHeight );
    set( "ftMidH",        dock->ui->spinBoxFTMiddleHeight,   dock->ui->sliderFTMiddleHeight );
    set( "ftMidTopR",     dock->ui->spinBoxFTMiddleTopRadius,dock->ui->sliderFTMiddleTopRadius );
    set( "ftMidBulge",    dock->ui->spinBoxFTMiddleBulge,    dock->ui->sliderFTMiddleBulge );
    set( "ftConeH",       dock->ui->spinBoxFTConeHeight,     dock->ui->sliderFTConeHeight );
    set( "tgLength1",      dock->ui->spinBoxTGLength1,     dock->ui->sliderTGLength1 );
    set( "tgLength2",      dock->ui->spinBoxTGLength2,     dock->ui->sliderTGLength2 );
    set( "tgWidth",       dock->ui->spinBoxTGWidth,      dock->ui->sliderTGWidth );
    setTotalAndWallRatio( "tgWallHeight", "tgRoofHeight", dock->ui->spinBoxTGHeightWall, dock->ui->sliderTGHeightWall, dock->ui->spinBoxTGRoofHeight, dock->ui->sliderTGRoofHeight );
    set( "tgAngle",       dock->ui->spinBoxTGAngle,      dock->ui->sliderTGAngle );
    set( "tgRidgeRatio",  dock->ui->spinBoxTGRidgeRatio, dock->ui->sliderTGRidgeRatio );

    // 位姿参数
    set( "tx", dock->ui->spinBoxTX, dock->ui->sliderTX );
    set( "ty", dock->ui->spinBoxTY, dock->ui->sliderTY );
    set( "tz", dock->ui->spinBoxTZ, dock->ui->sliderTZ );

    // DL 回归输出的水平朝向
    if ( params.contains( QStringLiteral( "poseRotateZ" ) ) )
      dock->ui->spinBoxRKappa->setValue( params[QStringLiteral( "poseRotateZ" )] );
}
