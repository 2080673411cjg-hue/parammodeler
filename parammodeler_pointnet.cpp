#include "parammodeler_pointnet.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace
{
const QString defaultPythonExe()
{
  return QStringLiteral( "E:/mambaforge/envs/pointnet_train/python.exe" );
}

const QString defaultScriptPath()
{
  return QStringLiteral( "E:/pointnet/pointnet_simple/main.py" );
}

const QString defaultLogDir()
{
  return QStringLiteral( "E:/pointnet/pointnet_simple/logs/pointnet_aug_250_gpu" );
}
}

PointNetPredictResult PointNetRunner::predict( const QString &inputTxt, int numPoints, int topK )
{
  PointNetPredictResult result;

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
  if ( !QFileInfo::exists( defaultScriptPath() ) )
  {
    result.errorMessage = QStringLiteral( "PointNet main.py does not exist: %1" ).arg( defaultScriptPath() );
    return result;
  }
  if ( !QFileInfo::exists( defaultLogDir() + QStringLiteral( "/best_model.pth" ) ) )
  {
    result.errorMessage = QStringLiteral( "PointNet model does not exist: %1/best_model.pth" ).arg( defaultLogDir() );
    return result;
  }

  QStringList args;
  args << defaultScriptPath()
       << QStringLiteral( "--mode" ) << QStringLiteral( "predict" )
       << QStringLiteral( "--input" ) << inputTxt
       << QStringLiteral( "--log_dir" ) << defaultLogDir()
       << QStringLiteral( "--num_points" ) << QString::number( numPoints )
       << QStringLiteral( "--topk" ) << QString::number( topK )
       << QStringLiteral( "--cpu" );

  QProcess process;
  process.setWorkingDirectory( QFileInfo( defaultScriptPath() ).absolutePath() );
  process.start( defaultPythonExe(), args );
  if ( !process.waitForStarted( 5000 ) )
  {
    result.errorMessage = QStringLiteral( "Failed to start PointNet process: %1" ).arg( process.errorString() );
    return result;
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
    result.errorMessage = QStringLiteral( "PointNet predict timed out." );
    return result;
  }

  const QString stdoutText = QString::fromUtf8( process.readAllStandardOutput() ).trimmed();
  const QString stderrText = QString::fromUtf8( process.readAllStandardError() ).trimmed();
  result.rawOutput = stdoutText;

  if ( process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 )
  {
    result.errorMessage = QStringLiteral( "PointNet predict failed.\n%1" ).arg( stderrText );
    return result;
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson( stdoutText.toUtf8(), &parseError );
  if ( parseError.error != QJsonParseError::NoError || !doc.isArray() )
  {
    result.errorMessage = QStringLiteral( "Failed to parse PointNet JSON output: %1\n%2" )
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
    result.errorMessage = QStringLiteral( "PointNet returned no predictions." );

  return result;
}
