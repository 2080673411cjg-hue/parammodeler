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

// ============================================================================
// helpers
// ============================================================================

/// Scan a class/split directory for the highest sample_NNNNN.txt index.
static int maxSampleIndexInDir( const QString &dirPath )
{
  QDir dir( dirPath );
  if ( !dir.exists() )
    return 0;
  int maxIdx = 0;
  const QStringList files = dir.entryList( { QStringLiteral( "sample_*.txt" ) }, QDir::Files, QDir::Name );
  for ( const QString &f : files )
  {
    // "sample_00042.txt" → 42
    QString numStr = f;
    numStr.remove( QStringLiteral( "sample_" ) ).remove( QStringLiteral( ".txt" ) );
    bool ok = false;
    int idx = numStr.toInt( &ok );
    if ( ok && idx > maxIdx )
      maxIdx = idx;
  }
  return maxIdx;
}

/// Load existing sample_params.json as a JSON array (empty if missing / corrupt).
static QJsonArray loadExistingMetadata( const QString &rootPath )
{
  QFile f( rootPath + QStringLiteral( "/metadata/sample_params.json" ) );
  if ( !f.open( QIODevice::ReadOnly ) )
    return {};
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson( f.readAll(), &err );
  if ( err.error != QJsonParseError::NoError || !doc.isArray() )
    return {};
  return doc.array();
}

/// Count existing samples per class per split from a metadata array.
static QMap<QString, QMap<QString, int>> countSamplesFromMetadata( const QJsonArray &meta )
{
  QMap<QString, QMap<QString, int>> counts;
  for ( const QJsonValue &v : meta )
  {
    QJsonObject o = v.toObject();
    QString prim  = o.value( QStringLiteral( "type" ) ).toString();
    QString split = o.value( QStringLiteral( "split" ) ).toString();
    if ( prim.isEmpty() || split.isEmpty() )
      continue;
    counts[prim][split]++;
  }
  return counts;
}

/// Build a human-readable summary of existing samples.
static QString existingCountsSummary( const QMap<QString, QMap<QString, int>> &counts,
                                      const QStringList &primitives )
{
  QStringList lines;
  for ( const QString &prim : primitives )
  {
    auto it = counts.constFind( prim );
    if ( it == counts.constEnd() )
    {
      lines << QStringLiteral( "  %1: (none)" ).arg( prim );
      continue;
    }
    int tr = it->value( QStringLiteral( "train" ), 0 );
    int va = it->value( QStringLiteral( "val" ), 0 );
    int te = it->value( QStringLiteral( "test" ), 0 );
    lines << QStringLiteral( "  %1: train=%2  val=%3  test=%4  (total=%5)" )
               .arg( prim )
               .arg( tr )
               .arg( va )
               .arg( te )
               .arg( tr + va + te );
  }
  return lines.join( QStringLiteral( "\n" ) );
}

/// Generate samples for a single split, returning new metadata records.
QJsonArray generateSplitSamples( const QString &prim,
                                        const QString &classDir,
                                        const QString &split,
                                        int startIdx,
                                        int count,
                                        const QString &rootPath,
                                        int pointCount,
                                        ParamModelerDock *dock,
                                        QProgressDialog &progress,
                                        int &generated,
                                        int &failed )
{
  QJsonArray records;
  for ( int i = 0; i < count; ++i )
  {
    if ( progress.wasCanceled() )
      break;

    dock->randomizeCurrentPrimitiveParams( false, true );
    const int fileIdx = startIdx + i + 1;
    const QString fileName = QStringLiteral( "sample_%1.txt" ).arg( fileIdx, 5, 10, QChar( '0' ) );
    const QString relativePath = split + QStringLiteral( "/" ) + classDir + QStringLiteral( "/" ) + fileName;
    const QString fullPath = rootPath + QStringLiteral( "/" ) + relativePath;

    DLPointCloudInfo pcInfo;
    if ( ExportPointCloud::exportOccludedTXT( fullPath, prim, dock, pointCount, &pcInfo ) )
    {
      QJsonObject item;
      item[QStringLiteral( "file" )]        = relativePath;
      item[QStringLiteral( "type" )]        = prim;
      item[QStringLiteral( "split" )]       = split;
      item[QStringLiteral( "pointCount" )]  = pointCount;
      item[QStringLiteral( "params" )]      = currentPrimitiveParamsObject( prim, dock );
      item[QStringLiteral( "pointCloudInfo" )] = pointCloudInfoToJson( pcInfo );
      records.append( item );
      generated++;
    }
    else
    {
      failed++;
    }
    progress.setValue( generated + failed );
  }
  return records;
}


// ============================================================================
// batch (all classes)
// ============================================================================

void generateFullDataset( ParamModelerDock *dock )
{
  // ---- 1. ask target count -------------------------------------------------
  bool ok = false;
  const int targetPerClass = QInputDialog::getInt(
    dock, QObject::tr( "Batch dataset generation" ),
    QObject::tr( "Target samples per primitive:" ), 500, 1, 10000, 1, &ok
  );
  if ( !ok )
    return;

  // ---- 2. select output folder ---------------------------------------------
  const QString selectedPath = QFileDialog::getExistingDirectory(
    dock, QObject::tr( "Select PointNet dataset folder" ) );
  if ( selectedPath.isEmpty() )
    return;
  const QString rootPath = datasetRootFromSelectedFolder( selectedPath );

  // ---- 3. collect primitives -----------------------------------------------
  QStringList primitiveTypes;
  for ( int i = 0; i < dock->ui->comboPrimitive->count(); ++i )
  {
    const QString prim = dock->ui->comboPrimitive->itemText( i );
    if ( !prim.isEmpty() && !primitiveTypes.contains( prim ) && prim != QStringLiteral( "CylinderHemisphere" ) )
      primitiveTypes << prim;
  }

  // ---- 4. detect existing data & choose mode --------------------------------
  const QJsonArray existingMeta = loadExistingMetadata( rootPath );
  const QMap<QString, QMap<QString, int>> existingCounts = countSamplesFromMetadata( existingMeta );

  bool appendMode = false;
  if ( !existingMeta.isEmpty() )
  {
    const QString summary = existingCountsSummary( existingCounts, primitiveTypes );
    QMessageBox modeDlg( dock );
    modeDlg.setWindowTitle( QObject::tr( "Existing dataset found" ) );
    modeDlg.setText( QObject::tr(
      "An existing dataset was found in:\n%1\n\n"
      "Current samples per class:\n%2\n\n"
      "What would you like to do?" ).arg( rootPath, summary ) );
    QPushButton *btnAppend    = modeDlg.addButton( QObject::tr( "Append — only generate missing up to %1" ).arg( targetPerClass ), QMessageBox::ActionRole );
    QPushButton *btnOverwrite = modeDlg.addButton( QObject::tr( "Overwrite — delete ALL and regenerate" ), QMessageBox::DestructiveRole );
    QPushButton *btnCancel    = modeDlg.addButton( QMessageBox::Cancel );
    modeDlg.exec();

    if ( modeDlg.clickedButton() == btnCancel )
      return;
    appendMode = ( modeDlg.clickedButton() == btnAppend );
  }

  // ---- 5. prepare directories ----------------------------------------------
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

  if ( !appendMode )
  {
    for ( const QString &entry : QStringList { QStringLiteral( "train" ), QStringLiteral( "val" ), QStringLiteral( "test" ), QStringLiteral( "metadata" ) } )
    {
      QDir oldDir( rootDir.filePath( entry ) );
      if ( oldDir.exists() )
        oldDir.removeRecursively();
    }
  }
  rootDir.mkpath( QStringLiteral( "train" ) );
  rootDir.mkpath( QStringLiteral( "val" ) );
  rootDir.mkpath( QStringLiteral( "test" ) );
  rootDir.mkpath( QStringLiteral( "metadata" ) );

  QFile classFile( rootDir.filePath( QStringLiteral( "metadata/class_names.txt" ) ) );
  if ( classFile.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    QTextStream out( &classFile );
    for ( const QString &prim : primitiveTypes )
      out << prim << "\n";
  }

  // ---- 6. generate ---------------------------------------------------------
  const int totalWork = targetPerClass * primitiveTypes.size();
  QProgressDialog progress( QObject::tr( "Generating dataset..." ),
                            QObject::tr( "Cancel" ), 0, totalWork, dock );
  progress.setWindowModality( Qt::WindowModal );

  int generated = 0;
  int failed = 0;

  // metadataRecords holds the final JSON to write
  QJsonArray metadataRecords;

  if ( appendMode )
  {
    // Start from existing metadata (all old records preserved)
    metadataRecords = existingMeta;

    const int targetTrain = targetPerClass * 8 / 10;
    const int targetVal   = targetPerClass * 9 / 10 - targetTrain;
    const int targetTest  = targetPerClass - targetTrain - targetVal;

    for ( const QString &prim : primitiveTypes )
    {
      if ( progress.wasCanceled() )
        break;

      const QString classDir = safeClassDirName( prim );
      rootDir.mkpath( QStringLiteral( "train/" ) + classDir );
      rootDir.mkpath( QStringLiteral( "val/" ) + classDir );
      rootDir.mkpath( QStringLiteral( "test/" ) + classDir );

      const int existTrain = existingCounts.value( prim ).value( QStringLiteral( "train" ), 0 );
      const int existVal   = existingCounts.value( prim ).value( QStringLiteral( "val" ), 0 );
      const int existTest  = existingCounts.value( prim ).value( QStringLiteral( "test" ), 0 );

      const int needTrain = qMax( 0, targetTrain - existTrain );
      const int needVal   = qMax( 0, targetVal - existVal );
      const int needTest  = qMax( 0, targetTest - existTest );
      if ( needTrain + needVal + needTest == 0 )
        continue;

      dock->ui->comboPrimitive->setCurrentText( prim );

      const int startTrain = maxSampleIndexInDir( rootDir.filePath( QStringLiteral( "train/" ) + classDir ) );
      const int startVal   = maxSampleIndexInDir( rootDir.filePath( QStringLiteral( "val/" ) + classDir ) );
      const int startTest  = maxSampleIndexInDir( rootDir.filePath( QStringLiteral( "test/" ) + classDir ) );

      // Collect new records from each split and merge into metadataRecords
      auto collect = [&]( const QJsonArray &records ) {
        for ( const QJsonValue &v : records )
          metadataRecords.append( v.toObject() );
      };

      if ( needTrain > 0 )
        collect( generateSplitSamples( prim, classDir, QStringLiteral( "train" ), startTrain, needTrain,
                                       rootPath, pointCount, dock, progress, generated, failed ) );
      if ( needVal > 0 )
        collect( generateSplitSamples( prim, classDir, QStringLiteral( "val" ), startVal, needVal,
                                       rootPath, pointCount, dock, progress, generated, failed ) );
      if ( needTest > 0 )
        collect( generateSplitSamples( prim, classDir, QStringLiteral( "test" ), startTest, needTest,
                                       rootPath, pointCount, dock, progress, generated, failed ) );
    }
  }
  else
  {
    // ---- overwrite: fresh metadata array -----------------------------------
    for ( const QString &prim : primitiveTypes )
    {
      const QString classDir = safeClassDirName( prim );
      rootDir.mkpath( QStringLiteral( "train/" ) + classDir );
      rootDir.mkpath( QStringLiteral( "val/" ) + classDir );
      rootDir.mkpath( QStringLiteral( "test/" ) + classDir );

      dock->ui->comboPrimitive->setCurrentText( prim );
      for ( int i = 0; i < targetPerClass; ++i )
      {
        if ( progress.wasCanceled() )
          break;

        dock->randomizeCurrentPrimitiveParams( false, true );
        const QString split = ( i < targetPerClass * 8 / 10 )   ? QStringLiteral( "train" )
                              : ( i < targetPerClass * 9 / 10 ) ? QStringLiteral( "val" )
                                                               : QStringLiteral( "test" );
        const QString fileName = QStringLiteral( "sample_%1.txt" ).arg( i + 1, 5, 10, QChar( '0' ) );
        const QString relativePath = split + QStringLiteral( "/" ) + classDir + QStringLiteral( "/" ) + fileName;
        const QString fullPath = rootDir.filePath( relativePath );

        DLPointCloudInfo pcInfo;
        if ( ExportPointCloud::exportOccludedTXT( fullPath, prim, dock, pointCount, &pcInfo ) )
        {
          QJsonObject item;
          item[QStringLiteral( "file" )]        = relativePath;
          item[QStringLiteral( "type" )]        = prim;
          item[QStringLiteral( "split" )]       = split;
          item[QStringLiteral( "pointCount" )]  = pointCount;
          item[QStringLiteral( "params" )]      = currentPrimitiveParamsObject( prim, dock );
          item[QStringLiteral( "pointCloudInfo" )] = pointCloudInfoToJson( pcInfo );
          metadataRecords.append( item );
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
  }

  // ---- 7. save metadata ----------------------------------------------------
  QString metadataError;
  const QString metadataPath = rootDir.filePath( QStringLiteral( "metadata/sample_params.json" ) );
  const bool metadataOk = writeJsonDocumentChecked( metadataPath, QJsonDocument( metadataRecords ), &metadataError );

  // ---- 8. restore state ----------------------------------------------------
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
        .arg( metadataError ) );
    return;
  }

  QMessageBox::information(
    dock,
    QObject::tr( "Done" ),
    QObject::tr( "Dataset generated.\nSuccess: %1\nFailed: %2\nMetadata records: %3\nOutput folder: %4" )
      .arg( generated )
      .arg( failed )
      .arg( metadataRecords.size() )
      .arg( rootPath ) );
}

// ============================================================================
// single-primitive (replace or append one class)
// ============================================================================

void generateSinglePrimitiveDataset( ParamModelerDock *dock )
{
  const QString prim = dock->ui->comboPrimitive->currentText();
  if ( prim.isEmpty() )
    return;

  // ---- 0. check existing ---------------------------------------------------
  const QString selectedPath = QFileDialog::getExistingDirectory(
    dock, QObject::tr( "Select PointNet Folder" ) );
  if ( selectedPath.isEmpty() )
    return;
  const QString rootPath = datasetRootFromSelectedFolder( selectedPath );

  const QJsonArray existingMeta = loadExistingMetadata( rootPath );
  const QMap<QString, QMap<QString, int>> existingCounts = countSamplesFromMetadata( existingMeta );
  const int existTrain = existingCounts.value( prim ).value( QStringLiteral( "train" ), 0 );
  const int existVal   = existingCounts.value( prim ).value( QStringLiteral( "val" ), 0 );
  const int existTest  = existingCounts.value( prim ).value( QStringLiteral( "test" ), 0 );
  const int existTotal = existTrain + existVal + existTest;

  // ---- 1. ask mode ---------------------------------------------------------
  QMessageBox modeDlg( dock );
  modeDlg.setWindowTitle( QObject::tr( "Generate %1 Dataset" ).arg( prim ) );
  modeDlg.setText( QObject::tr(
    "Primitive: %1\n"
    "Existing: train=%2  val=%3  test=%4  (total=%5)\n\n"
    "What would you like to do?" )
    .arg( prim )
    .arg( existTrain ).arg( existVal ).arg( existTest ).arg( existTotal ) );
  QPushButton *btnReplace = modeDlg.addButton( QObject::tr( "Replace — delete old and regenerate" ), QMessageBox::ActionRole );
  QPushButton *btnAppend  = modeDlg.addButton( QObject::tr( "Append — keep old, add more samples" ), QMessageBox::ActionRole );
  QPushButton *btnCancel  = modeDlg.addButton( QMessageBox::Cancel );
  modeDlg.exec();

  if ( modeDlg.clickedButton() == btnCancel )
    return;
  const bool appendMode = ( modeDlg.clickedButton() == btnAppend );

  // ---- 2. ask count --------------------------------------------------------
  bool ok = false;
  const int samplesPerClass = QInputDialog::getInt(
    dock,
    QObject::tr( "Generate %1 Dataset" ).arg( prim ),
    appendMode
      ? QObject::tr( "Additional samples for %1 (existing: %2):" ).arg( prim ).arg( existTotal )
      : QObject::tr( "Samples for %1:" ).arg( prim ),
    appendMode ? 100 : 500,
    1, 10000, 1, &ok
  );
  if ( !ok )
    return;

  // ---- 3. prepare ----------------------------------------------------------
  const int pointCount = 2048;
  const QString classDir = safeClassDirName( prim );
  const QString originalPrimitive = dock->ui->comboPrimitive->currentText();
  const bool previousUpdating = dock->m_isUpdating;
  dock->m_isUpdating = true;
  const QMap<QString, QVector<double>> savedPoses = dock->m_poseMap;
  if ( dock->m_previewTimer )
    dock->m_previewTimer->stop();

  QDir rootDir( rootPath );
  if ( !rootDir.exists() )
    QDir().mkpath( rootPath );
  rootDir.mkpath( QStringLiteral( "train" ) );
  rootDir.mkpath( QStringLiteral( "val" ) );
  rootDir.mkpath( QStringLiteral( "test" ) );
  rootDir.mkpath( QStringLiteral( "metadata" ) );

  if ( !appendMode )
  {
    // Replace: delete existing class directories
    for ( const QString &split : QStringList { QStringLiteral( "train" ), QStringLiteral( "val" ), QStringLiteral( "test" ) } )
    {
      QDir classPath( rootDir.filePath( split + QStringLiteral( "/" ) + classDir ) );
      if ( classPath.exists() )
        classPath.removeRecursively();
      rootDir.mkpath( split + QStringLiteral( "/" ) + classDir );
    }
  }
  else
  {
    // Append: ensure class directories exist
    for ( const QString &split : QStringList { QStringLiteral( "train" ), QStringLiteral( "val" ), QStringLiteral( "test" ) } )
      rootDir.mkpath( split + QStringLiteral( "/" ) + classDir );
  }

  dock->ui->comboPrimitive->setCurrentText( prim );

  // ---- 4. generate ---------------------------------------------------------
  const int startTrain = appendMode ? maxSampleIndexInDir( rootDir.filePath( QStringLiteral( "train/" ) + classDir ) ) : 0;
  const int startVal   = appendMode ? maxSampleIndexInDir( rootDir.filePath( QStringLiteral( "val/" ) + classDir ) ) : 0;
  const int startTest  = appendMode ? maxSampleIndexInDir( rootDir.filePath( QStringLiteral( "test/" ) + classDir ) ) : 0;

  QProgressDialog progress( QObject::tr( "Generating %1 dataset..." ).arg( prim ),
                            QObject::tr( "Cancel" ), 0, samplesPerClass, dock );
  progress.setWindowModality( Qt::WindowModal );

  int generated = 0;
  int failed = 0;

  // Collect new records for metadata merging.
  // Helper: append each element individually — QJsonArray::append(QJsonArray)
  // would nest the whole array as a single QJsonValue instead of flattening.
  auto appendRecords = []( QJsonArray &dest, const QJsonArray &src ) {
    for ( const QJsonValue &v : src )
      dest.append( v );
  };

  QJsonArray newRecords = generateSplitSamples(
    prim, classDir, QStringLiteral( "train" ), startTrain,
    samplesPerClass * 8 / 10, rootPath, pointCount, dock, progress, generated, failed );
  if ( !progress.wasCanceled() )
  {
    const int valCount = samplesPerClass * 9 / 10 - samplesPerClass * 8 / 10;
    appendRecords( newRecords, generateSplitSamples(
      prim, classDir, QStringLiteral( "val" ), startVal,
      valCount, rootPath, pointCount, dock, progress, generated, failed ) );
  }
  if ( !progress.wasCanceled() )
  {
    const int testCount = samplesPerClass - ( samplesPerClass * 8 / 10 ) - ( samplesPerClass * 9 / 10 - samplesPerClass * 8 / 10 );
    appendRecords( newRecords, generateSplitSamples(
      prim, classDir, QStringLiteral( "test" ), startTest,
      testCount, rootPath, pointCount, dock, progress, generated, failed ) );
  }

  // ---- 5. merge metadata ---------------------------------------------------
  QString metadataError;
  const QString classMetadataPath = rootDir.filePath( QStringLiteral( "metadata/sample_params_" ) + classDir + QStringLiteral( ".json" ) );
  const bool classMetadataOk = writeJsonDocumentChecked( classMetadataPath, QJsonDocument( newRecords ), &metadataError );

  bool mergedMetadata = false;
  QString mergeMessage;
  const QString metadataPath = rootDir.filePath( QStringLiteral( "metadata/sample_params.json" ) );

  QJsonArray finalMeta;
  if ( !existingMeta.isEmpty() )
  {
    // Keep records for OTHER classes; replace/appends are handled by which
    // newRecords we generated (replace = only new; append = old kept in dirs
    // but metadata already filtered below).
    for ( const QJsonValue &value : existingMeta )
    {
      const QJsonObject obj = value.toObject();
      if ( obj.value( QStringLiteral( "type" ) ).toString() != prim )
        finalMeta.append( obj );
    }
  }

  if ( appendMode )
  {
    // In append mode, existing class records are still valid on disk — but we
    // filtered them out above (since they had type == prim).  We need to
    // re-add them.  The cleanest way: reload existing, keep ALL records for
    // prim, then also add new ones.
    //
    // Actually, the correct approach: for ALL records in existingMeta where
    // type == prim, keep them (they correspond to files that still exist).
    // We'll rebuild finalMeta from scratch.
    finalMeta = QJsonArray();
    for ( const QJsonValue &value : existingMeta )
    {
      // In append mode, keep all old records (including the current prim)
      finalMeta.append( value.toObject() );
    }
  }

  // Add new records
  for ( const QJsonValue &value : newRecords )
    finalMeta.append( value.toObject() );

  QString mergeError;
  mergedMetadata = writeJsonDocumentChecked( metadataPath, QJsonDocument( finalMeta ), &mergeError );
  if ( !mergedMetadata )
    mergeMessage = QObject::tr( "sample_params.json write failed: %1" ).arg( mergeError );

  // ---- 6. restore state ----------------------------------------------------
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
      QObject::tr( "Point clouds were generated, but class metadata was not written correctly.\n\n%1" )
        .arg( metadataError ) );
    return;
  }

  QString message = QObject::tr(
    "Dataset generated.\n"
    "Primitive: %1\n"
    "Success: %2  Failed: %3\n"
    "New records: %4\n"
    "Class metadata: %5" )
    .arg( prim )
    .arg( generated )
    .arg( failed )
    .arg( newRecords.size() )
    .arg( classMetadataPath );
  if ( mergedMetadata )
    message += QObject::tr( "\nMain metadata updated: %1 (total records: %2)" )
                 .arg( metadataPath ).arg( finalMeta.size() );
  else if ( !mergeMessage.isEmpty() )
    message += QStringLiteral( "\n\n" ) + mergeMessage;

  QMessageBox::information( dock, QObject::tr( "Done" ), message );
}
