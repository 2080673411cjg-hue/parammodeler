/***************************************************************************
  parammodeler_datasetgen.cpp
  DL dataset batch generation helpers
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "parammodeler_datasetgen.h"

#include "exportpointcloud.h"
#include "parammodeler_dlutils.h"
#include "parammodeler_dock.h"
#include "ui_parammodeler_dock.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTextStream>

void generateFullDataset( ParamModelerDock *dock )
{
  bool ok = false;
  const int samplesPerClass = QInputDialog::getInt(
    dock, QObject::tr( "Batch dataset generation" ), QObject::tr( "Samples per primitive:" ), 50, 1, 10000, 1, &ok
  );
  if ( !ok )
    return;

  const QString selectedPath = QFileDialog::getExistingDirectory( dock, QObject::tr( "Select PointNet dataset folder" ) );
  if ( selectedPath.isEmpty() )
    return;
  const QString rootPath = datasetRootFromSelectedFolder( selectedPath );

  const QMessageBox::StandardButton confirm = QMessageBox::question(
    dock,
    QObject::tr( "Overwrite dataset?" ),
    QObject::tr( "This will regenerate:\n%1\n\nExisting train, val, test, and metadata folders will be overwritten. Continue?" ).arg( rootPath ),
    QMessageBox::Yes | QMessageBox::No,
    QMessageBox::No
  );
  if ( confirm != QMessageBox::Yes )
    return;

  const int pointCount = 2048;
  const QString originalPrimitive = dock->ui->comboPrimitive->currentText();
  const bool previousUpdating = dock->m_isUpdating;
  dock->m_isUpdating = true;
  const QMap<QString, QVector<double>> savedPoses = dock->m_poseMap;
  if ( dock->m_previewTimer )
    dock->m_previewTimer->stop();

  QStringList primitiveTypes;
  for ( int i = 0; i < dock->ui->comboPrimitive->count(); ++i )
  {
    const QString prim = dock->ui->comboPrimitive->itemText( i );
    if ( !prim.isEmpty() && !primitiveTypes.contains( prim ) && prim != "CylinderHemisphere" )
      primitiveTypes << prim;
  }

  QDir rootDir( rootPath );
  if ( !rootDir.exists() )
    QDir().mkpath( rootPath );
  for ( const QString &entry : QStringList { "train", "val", "test", "metadata" } )
  {
    QDir oldDir( rootDir.filePath( entry ) );
    if ( oldDir.exists() )
      oldDir.removeRecursively();
  }
  rootDir.mkpath( "train" );
  rootDir.mkpath( "val" );
  rootDir.mkpath( "test" );
  rootDir.mkpath( "metadata" );

  QFile classFile( rootDir.filePath( "metadata/class_names.txt" ) );
  if ( classFile.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    QTextStream out( &classFile );
    for ( const QString &prim : primitiveTypes )
      out << prim << "\n";
  }

  QJsonArray metadata;
  QProgressDialog progress( QObject::tr( "Generating dataset..." ), QObject::tr( "Cancel" ), 0, static_cast<int>( primitiveTypes.size() ) * samplesPerClass, dock );
  progress.setWindowModality( Qt::WindowModal );

  int generated = 0;
  int failed = 0;
  for ( const QString &prim : primitiveTypes )
  {
    const QString classDir = safeClassDirName( prim );
    rootDir.mkpath( "train/" + classDir );
    rootDir.mkpath( "val/" + classDir );
    rootDir.mkpath( "test/" + classDir );

    dock->ui->comboPrimitive->setCurrentText( prim );
    for ( int i = 0; i < samplesPerClass; ++i )
    {
      if ( progress.wasCanceled() )
        break;

      dock->randomizeCurrentPrimitiveParams( false, true );
      const QString split = ( i < samplesPerClass * 8 / 10 )   ? "train"
                            : ( i < samplesPerClass * 9 / 10 ) ? "val"
                                                               : "test";
      const QString fileName = QString( "sample_%1.txt" ).arg( i + 1, 5, 10, QChar( '0' ) );
      const QString relativePath = split + "/" + classDir + "/" + fileName;
      const QString fullPath = rootDir.filePath( relativePath );

      DLPointCloudInfo pcInfo;
      if ( ExportPointCloud::exportOccludedTXT( fullPath, prim, dock, pointCount, &pcInfo ) )
      {
        QJsonObject item;
        item["file"] = relativePath;
        item["type"] = prim;
        item["split"] = split;
        item["pointCount"] = pointCount;
        item["params"] = currentPrimitiveParamsObject( prim, dock );
        item["pointCloudInfo"] = pointCloudInfoToJson( pcInfo );
        metadata.append( item );
        generated++;
      }
      else
      {
        failed++;
      }
      progress.setValue( generated + failed );
    }
    if ( progress.wasCanceled() )
      break;
  }

  QString metadataError;
  const QString metadataPath = rootDir.filePath( "metadata/sample_params.json" );
  const bool metadataOk = writeJsonDocumentChecked( metadataPath, QJsonDocument( metadata ), &metadataError );

  dock->m_poseMap = savedPoses;
  dock->ui->comboPrimitive->setCurrentText( originalPrimitive );
  dock->m_isUpdating = previousUpdating;
  if ( dock->m_previewTimer )
    dock->m_previewTimer->stop();
  dock->onUpdatePreview();

  if ( !metadataOk )
  {
    QMessageBox::critical(
      dock,
      QObject::tr( "Metadata export failed" ),
      QObject::tr( "Dataset point clouds were generated, but sample_params.json was not written correctly.\n\n%1" )
        .arg( metadataError )
    );
    return;
  }

  QMessageBox::information(
    dock,
    QObject::tr( "Done" ),
    QObject::tr( "Dataset generated.\nSuccess: %1\nFailed: %2\nMetadata records: %3\nOutput folder: %4" )
      .arg( generated )
      .arg( failed )
      .arg( metadata.size() )
      .arg( rootPath )
  );
}

void generateSinglePrimitiveDataset( ParamModelerDock *dock )
{
  const QString prim = dock->ui->comboPrimitive->currentText();
  if ( prim.isEmpty() )
    return;

  bool ok = false;
  const int samplesPerClass = QInputDialog::getInt(
    dock,
    QObject::tr( "Generate Current Primitive Dataset" ),
    QObject::tr( "Samples for %1:" ).arg( prim ),
    260,
    1,
    10000,
    1,
    &ok
  );
  if ( !ok )
    return;

  const QString selectedPath = QFileDialog::getExistingDirectory( dock, QObject::tr( "Select PointNet Folder" ) );
  if ( selectedPath.isEmpty() )
    return;
  const QString rootPath = datasetRootFromSelectedFolder( selectedPath );

  const QString classDir = safeClassDirName( prim );
  const int answer = QMessageBox::question(
    dock,
    QObject::tr( "Replace Current Primitive" ),
    QObject::tr( "This will replace existing point-cloud files for %1 under train/val/test in:\n%2\n\nContinue?" )
      .arg( prim, rootPath ),
    QMessageBox::Yes | QMessageBox::No,
    QMessageBox::No
  );
  if ( answer != QMessageBox::Yes )
    return;

  const int pointCount = 2048;
  const QString originalPrimitive = dock->ui->comboPrimitive->currentText();
  const bool previousUpdating = dock->m_isUpdating;
  dock->m_isUpdating = true;
  const QMap<QString, QVector<double>> savedPoses = dock->m_poseMap;
  if ( dock->m_previewTimer )
    dock->m_previewTimer->stop();

  QDir rootDir( rootPath );
  if ( !rootDir.exists() )
    QDir().mkpath( rootPath );
  rootDir.mkpath( "train" );
  rootDir.mkpath( "val" );
  rootDir.mkpath( "test" );
  rootDir.mkpath( "metadata" );

  for ( const QString &split : QStringList { "train", "val", "test" } )
  {
    QDir classPath( rootDir.filePath( split + "/" + classDir ) );
    if ( classPath.exists() )
      classPath.removeRecursively();
    rootDir.mkpath( split + "/" + classDir );
  }

  dock->ui->comboPrimitive->setCurrentText( prim );

  QJsonArray newRecords;
  QProgressDialog progress( QObject::tr( "Generating %1 dataset..." ).arg( prim ), QObject::tr( "Cancel" ), 0, samplesPerClass, dock );
  progress.setWindowModality( Qt::WindowModal );

  int generated = 0;
  int failed = 0;
  for ( int i = 0; i < samplesPerClass; ++i )
  {
    if ( progress.wasCanceled() )
      break;

    dock->randomizeCurrentPrimitiveParams( false, true );
    const QString split = ( i < samplesPerClass * 8 / 10 )   ? "train"
                          : ( i < samplesPerClass * 9 / 10 ) ? "val"
                                                             : "test";
    const QString fileName = QString( "sample_%1.txt" ).arg( i + 1, 5, 10, QChar( '0' ) );
    const QString relativePath = split + "/" + classDir + "/" + fileName;
    const QString fullPath = rootDir.filePath( relativePath );

    DLPointCloudInfo pcInfo;
    if ( ExportPointCloud::exportOccludedTXT( fullPath, prim, dock, pointCount, &pcInfo ) )
    {
      QJsonObject item;
      item["file"] = relativePath;
      item["type"] = prim;
      item["split"] = split;
      item["pointCount"] = pointCount;
      item["params"] = currentPrimitiveParamsObject( prim, dock );
      item["pointCloudInfo"] = pointCloudInfoToJson( pcInfo );
      newRecords.append( item );
      generated++;
    }
    else
    {
      failed++;
    }
    progress.setValue( generated + failed );
  }

  QString metadataError;
  const QString classMetadataPath = rootDir.filePath( "metadata/sample_params_" + classDir + ".json" );
  const bool classMetadataOk = writeJsonDocumentChecked( classMetadataPath, QJsonDocument( newRecords ), &metadataError );

  bool mergedMetadata = false;
  QString mergeMessage;
  const QString metadataPath = rootDir.filePath( "metadata/sample_params.json" );
  QFile existingMetadataFile( metadataPath );
  if ( existingMetadataFile.exists() && existingMetadataFile.open( QIODevice::ReadOnly ) )
  {
    QJsonParseError parseError;
    const QJsonDocument existingDoc = QJsonDocument::fromJson( existingMetadataFile.readAll(), &parseError );
    if ( parseError.error == QJsonParseError::NoError && existingDoc.isArray() )
    {
      QJsonArray merged;
      const QJsonArray oldRecords = existingDoc.array();
      for ( const QJsonValue &value : oldRecords )
      {
        const QJsonObject obj = value.toObject();
        if ( obj.value( QStringLiteral( "type" ) ).toString() != prim )
          merged.append( obj );
      }
      for ( const QJsonValue &value : newRecords )
        merged.append( value );

      QString mergeError;
      mergedMetadata = writeJsonDocumentChecked( metadataPath, QJsonDocument( merged ), &mergeError );
      if ( !mergedMetadata )
        mergeMessage = QObject::tr( "Main sample_params.json merge failed: %1" ).arg( mergeError );
    }
    else
    {
      mergeMessage = QObject::tr( "Main sample_params.json is not valid JSON, so it was not overwritten. Use the class metadata file instead." );
    }
  }
  else
  {
    QString mergeError;
    mergedMetadata = writeJsonDocumentChecked( metadataPath, QJsonDocument( newRecords ), &mergeError );
    if ( !mergedMetadata )
      mergeMessage = QObject::tr( "Main sample_params.json write failed: %1" ).arg( mergeError );
  }

  dock->m_poseMap = savedPoses;
  dock->ui->comboPrimitive->setCurrentText( originalPrimitive );
  dock->m_isUpdating = previousUpdating;
  if ( dock->m_previewTimer )
    dock->m_previewTimer->stop();
  dock->onUpdatePreview();

  if ( !classMetadataOk )
  {
    QMessageBox::critical(
      dock,
      QObject::tr( "Metadata export failed" ),
      QObject::tr( "Current primitive point clouds were generated, but class metadata was not written correctly.\n\n%1" )
        .arg( metadataError )
    );
    return;
  }

  QString message = QObject::tr( "Current primitive dataset generated.\nPrimitive: %1\nSuccess: %2\nFailed: %3\nMetadata records: %4\nClass metadata: %5" )
                      .arg( prim )
                      .arg( generated )
                      .arg( failed )
                      .arg( newRecords.size() )
                      .arg( classMetadataPath );
  if ( mergedMetadata )
    message += QObject::tr( "\nMain metadata updated: %1" ).arg( metadataPath );
  else if ( !mergeMessage.isEmpty() )
    message += QObject::tr( "\n\n%1" ).arg( mergeMessage );

  QMessageBox::information( dock, QObject::tr( "Done" ), message );
}
