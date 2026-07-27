/***************************************************************************
  parammodeler_dlutils.cpp
  DL parameter mapping, metadata, and JSON utilities
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "parammodeler_dlutils.h"

#include "exportpointcloud.h"
#include "parammodeler_config.h"
#include "parammodeler_dock.h"
#include "parammodeler_pcdloader.h"
#include "parammodeler_pcdtypes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>
#include <algorithm>
#include <cmath>

// ====================================================================
// DL parameter mapping
// ====================================================================

QMap<QString, double> pointNetParamsToUiParams( const QString &primitiveType,
                                                 const QMap<QString, double> &nn )
{
  QMap<QString, double> uiParams;
  const auto has = [&]( const QString &key ) { return nn.contains( key ); };
  const auto val = [&]( const QString &key, double fallback = 0.0 ) { return nn.value( key, fallback ); };
  const auto put = [&]( const QString &from, const QString &to ) {
    if ( has( from ) )
      uiParams.insert( to, val( from ) );
  };
  const auto putWallRatio = [&]( const QString &wallKey, const QString &roofKey ) {
    if ( has( QStringLiteral( "totalHeight" ) ) && has( QStringLiteral( "wallRatio" ) ) )
    {
      const double totalHeight = val( QStringLiteral( "totalHeight" ) );
      const double wallRatio = std::max( 0.0, std::min( 1.0, val( QStringLiteral( "wallRatio" ) ) ) );
      uiParams.insert( wallKey, totalHeight * wallRatio );
      uiParams.insert( roofKey, totalHeight * ( 1.0 - wallRatio ) );
    }
  };
  const auto putCylinderRatio = [&]( const QString &cylKey, const QString &upperKey ) {
    if ( has( QStringLiteral( "totalHeight" ) ) && has( QStringLiteral( "cylinderRatio" ) ) )
    {
      const double totalHeight = val( QStringLiteral( "totalHeight" ) );
      const double cylinderRatio = std::max( 0.0, std::min( 1.0, val( QStringLiteral( "cylinderRatio" ) ) ) );
      uiParams.insert( cylKey, totalHeight * cylinderRatio );
      uiParams.insert( upperKey, totalHeight * ( 1.0 - cylinderRatio ) );
    }
  };

  const QString prim = primitiveType == QStringLiteral( "CylinderHemisphere" )
                         ? QStringLiteral( "CylinderDome" )
                         : primitiveType;

  if ( prim == QStringLiteral( "Cuboid" ) )
  {
    put( QStringLiteral( "length" ), QStringLiteral( "length" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "width" ) );
    put( QStringLiteral( "height" ), QStringLiteral( "height" ) );
  }
  else if ( prim == QStringLiteral( "Cylinder" ) )
  {
    put( QStringLiteral( "radius" ), QStringLiteral( "radius" ) );
    put( QStringLiteral( "height" ), QStringLiteral( "cylHeight" ) );
  }
  else if ( prim == QStringLiteral( "LHouse" ) )
  {
    put( QStringLiteral( "totalLength" ), QStringLiteral( "lTotalL" ) );
    put( QStringLiteral( "wingRatio" ), QStringLiteral( "lWingR" ) );
    put( QStringLiteral( "totalWidth" ), QStringLiteral( "lTotalW" ) );
    put( QStringLiteral( "wingWidthRatio" ), QStringLiteral( "lWingWR" ) );
    put( QStringLiteral( "height" ), QStringLiteral( "lHeight" ) );
  }
  else if ( prim == QStringLiteral( "ConeCylinder" ) )
  {
    put( QStringLiteral( "radius" ), QStringLiteral( "ccRadius" ) );
    putCylinderRatio( QStringLiteral( "ccCylHeight" ), QStringLiteral( "ccConeHeight" ) );
  }
  else if ( prim == QStringLiteral( "GabledRoof" ) )
  {
    put( QStringLiteral( "length" ), QStringLiteral( "grLength" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "grWidth" ) );
    putWallRatio( QStringLiteral( "grWallHeight" ), QStringLiteral( "grRoofHeight" ) );
  }
  else if ( prim == QStringLiteral( "PyramidRoof" ) )
  {
    put( QStringLiteral( "length" ), QStringLiteral( "prLength" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "prWidth" ) );
    putWallRatio( QStringLiteral( "prWallHeight" ), QStringLiteral( "prRoofHeight" ) );
  }
  else if ( prim == QStringLiteral( "TruncatedPyramidRoof" ) )
  {
    put( QStringLiteral( "bottomLength" ), QStringLiteral( "tpBottomLength" ) );
    put( QStringLiteral( "bottomWidth" ), QStringLiteral( "tpBottomWidth" ) );
    put( QStringLiteral( "topLength" ), QStringLiteral( "tpTopLength" ) );
    put( QStringLiteral( "topWidth" ), QStringLiteral( "tpTopWidth" ) );
    putWallRatio( QStringLiteral( "tpWallHeight" ), QStringLiteral( "tpRoofHeight" ) );
  }
  else if ( prim == QStringLiteral( "HalfCylinderRoof" ) )
  {
    put( QStringLiteral( "length" ), QStringLiteral( "hcrLength" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "hcrWidth" ) );
    put( QStringLiteral( "wallHeight" ), QStringLiteral( "hcrWallHeight" ) );
  }
  else if ( prim == QStringLiteral( "CylinderDome" ) )
  {
    put( QStringLiteral( "radius" ), QStringLiteral( "chRadius" ) );
    putCylinderRatio( QStringLiteral( "chCylHeight" ), QStringLiteral( "chDomeHeight" ) );
    put( QStringLiteral( "bulge" ), QStringLiteral( "chBulge" ) );
  }
  else if ( prim == QStringLiteral( "IndentedCuboid" ) )
  {
    put( QStringLiteral( "outerLength" ), QStringLiteral( "icOuterL" ) );
    put( QStringLiteral( "outerWidth" ), QStringLiteral( "icOuterW" ) );
    put( QStringLiteral( "outerHeight" ), QStringLiteral( "icOuterH" ) );
    put( QStringLiteral( "innerLength" ), QStringLiteral( "icInnerL" ) );
    put( QStringLiteral( "innerWidth" ), QStringLiteral( "icInnerW" ) );
    put( QStringLiteral( "innerHeight" ), QStringLiteral( "icInnerH" ) );
    put( QStringLiteral( "offsetX" ), QStringLiteral( "icOffsetX" ) );
    put( QStringLiteral( "offsetY" ), QStringLiteral( "icOffsetY" ) );
  }
  else if ( prim == QStringLiteral( "AsymmetricGableHouse" ) )
  {
    put( QStringLiteral( "length" ), QStringLiteral( "aghLength" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "aghWidth" ) );
    putWallRatio( QStringLiteral( "aghWallHeight" ), QStringLiteral( "aghRoofHeight" ) );
    put( QStringLiteral( "ridgeLength" ), QStringLiteral( "aghRidgeLen" ) );
    put( QStringLiteral( "ridgeRatio" ), QStringLiteral( "aghRidgeRatio" ) );
  }
  else if ( prim == QStringLiteral( "FourStageRoundTower" ) )
  {
    put( QStringLiteral( "baseRadius" ), QStringLiteral( "ftBaseR" ) );
    put( QStringLiteral( "baseHeight" ), QStringLiteral( "ftBaseH" ) );
    put( QStringLiteral( "middleHeight" ), QStringLiteral( "ftMidH" ) );
    put( QStringLiteral( "middleTopRadius" ), QStringLiteral( "ftMidTopR" ) );
    put( QStringLiteral( "middleBulge" ), QStringLiteral( "ftMidBulge" ) );
    put( QStringLiteral( "coneHeight" ), QStringLiteral( "ftConeH" ) );
  }
  else if ( prim == QStringLiteral( "TwoGableHouses" ) )
  {
    put( QStringLiteral( "length1" ), QStringLiteral( "tgLength1" ) );
    put( QStringLiteral( "length2" ), QStringLiteral( "tgLength2" ) );
    put( QStringLiteral( "width" ), QStringLiteral( "tgWidth" ) );
    putWallRatio( QStringLiteral( "tgWallHeight" ), QStringLiteral( "tgRoofHeight" ) );
    put( QStringLiteral( "angle" ), QStringLiteral( "tgAngle" ) );
    put( QStringLiteral( "ridgeRatio" ), QStringLiteral( "tgRidgeRatio" ) );
  }

  // 水平朝向（所有基元通用）
  put( QStringLiteral( "rz" ), QStringLiteral( "poseRotateZ" ) );

  return uiParams;
}

QJsonObject currentPrimitiveParamsObject( const QString &prim, const ParamModelerDock *dock )
{
  QJsonObject params;
  if ( prim == "Cuboid" )
  {
    params["length"] = dock->cuboidLength();
    params["width"] = dock->cuboidWidth();
    params["height"] = dock->cuboidHeight();
  }
  else if ( prim == "Cylinder" )
  {
    params["radius"] = dock->cylinderRadius();
    params["height"] = dock->cylinderHeight();
  }
  else if ( prim == "LHouse" )
  {
    params["totalLength"]    = dock->LTotalLength();
    params["wingRatio"]      = dock->LWingRatio();
    params["totalWidth"]     = dock->LTotalWidth();
    params["wingWidthRatio"] = dock->LWingWidthRatio();
    params["height"] = dock->LHeight();
  }
  else if ( prim == "ConeCylinder" )
  {
    params["radius"] = dock->coneCylRadius();
    const double cylH = dock->coneCylCylHeight();
    const double coneH = dock->coneCylConeHeight();
    const double totalH = cylH + coneH;
    params["totalHeight"] = totalH;
    params["cylinderRatio"] = totalH > 1e-6 ? cylH / totalH : 0.7;
  }
  else if ( prim == "GabledRoof" )
  {
    params["length"] = dock->gabledRoofLength();
    params["width"] = dock->gabledRoofWidth();
    const double wallH = dock->gabledRoofWallHeight();
    const double roofH = dock->gabledRoofRoofHeight();
    const double totalH = wallH + roofH;
    params["totalHeight"] = totalH;
    params["wallRatio"] = totalH > 1e-6 ? wallH / totalH : 0.7;
  }
  else if ( prim == "PyramidRoof" )
  {
    params["length"] = dock->pyramidLength();
    params["width"] = dock->pyramidWidth();
    const double wallH = dock->pyramidWallHeight();
    const double roofH = dock->pyramidRoofHeight();
    const double totalH = wallH + roofH;
    params["totalHeight"] = totalH;
    params["wallRatio"] = totalH > 1e-6 ? wallH / totalH : 0.7;
  }
  else if ( prim == "TruncatedPyramidRoof" )
  {
    params["bottomLength"] = dock->tpBottomLength();
    params["bottomWidth"] = dock->tpBottomWidth();
    params["topLength"] = dock->tpTopLength();
    params["topWidth"] = dock->tpTopWidth();
    const double wallH = dock->tpWallHeight();
    const double roofH = dock->tpRoofHeight();
    const double totalH = wallH + roofH;
    params["totalHeight"] = totalH;
    params["wallRatio"] = totalH > 1e-6 ? wallH / totalH : 0.7;
  }
  else if ( prim == "HalfCylinderRoof" )
  {
    params["length"] = dock->hcrLength();
    params["width"] = dock->hcrWidth();
    params["wallHeight"] = dock->hcrWallHeight();
    params["radius"] = dock->hcrRadius();
  }
  else if ( prim == "CylinderDome" || prim == "CylinderHemisphere" )
  {
    params["radius"] = dock->cylHemiRadius();
    const double cylH = dock->cylHemiHeight();
    const double domeH = dock->cylHemiDomeHeight();
    const double totalH = cylH + domeH;
    params["totalHeight"] = totalH;
    params["cylinderRatio"] = totalH > 1e-6 ? cylH / totalH : 0.7;
    params["bulge"] = dock->cylHemiBulge();
  }
  else if ( prim == "IndentedCuboid" )
  {
    params["outerLength"] = dock->icOuterLength();
    params["outerWidth"] = dock->icOuterWidth();
    params["outerHeight"] = dock->icOuterHeight();
    params["innerLength"] = dock->icInnerLength();
    params["innerWidth"] = dock->icInnerWidth();
    params["innerHeight"] = dock->icInnerHeight();
    params["offsetX"] = dock->icOffsetX();
    params["offsetY"] = dock->icOffsetY();
  }
  else if ( prim == "AsymmetricGableHouse" )
  {
    params["length"] = dock->aghLength();
    params["width"] = dock->aghWidth();
    const double wallH = dock->aghWallHeight();
    const double roofH = dock->aghRoofHeight();
    const double totalH = wallH + roofH;
    params["totalHeight"] = totalH;
    params["wallRatio"] = totalH > 1e-6 ? wallH / totalH : 0.7;
    params["ridgeLength"] = dock->aghRidgeLength();
    params["ridgeRatio"] = dock->aghRidgeRatio();
  }
  else if ( prim == "FourStageRoundTower" )
  {
    params["baseRadius"] = dock->ftBaseRadius();
    params["baseHeight"] = dock->ftBaseHeight();
    params["middleHeight"] = dock->ftMiddleHeight();
    params["middleTopRadius"] = dock->ftMiddleTopRadius();
    params["middleBulge"] = dock->ftMiddleBulge();
    params["coneHeight"] = dock->ftConeHeight();
  }
  else if ( prim == "TwoGableHouses" )
  {
    params["length1"] = dock->tgLength1();
    params["length2"] = dock->tgLength2();
    params["width"] = dock->tgWidth();
    const double wallH = dock->tgWallHeight();
    const double roofH = dock->tgRoofHeight();
    const double totalH = wallH + roofH;
    params["totalHeight"] = totalH;
    params["wallRatio"] = totalH > 1e-6 ? wallH / totalH : 0.7;
    params["angle"] = dock->tgAngle();
    params["ridgeRatio"] = dock->tgRidgeRatio();
  }
  params["rz"] = dock->poseRotateZ();
  return params;
}

// ====================================================================
// JSON helpers
// ====================================================================

QJsonArray vectorToJsonArray( const QVector3D &v )
{
  QJsonArray arr;
  arr.append( v.x() );
  arr.append( v.y() );
  arr.append( v.z() );
  return arr;
}

QJsonObject pointCloudInfoToJson( const DLPointCloudInfo &info )
{
  QJsonObject obj;
  obj["bboxMin"] = vectorToJsonArray( info.bboxMin );
  obj["bboxMax"] = vectorToJsonArray( info.bboxMax );
  obj["bboxSize"] = vectorToJsonArray( info.bboxSize );
  obj["center"] = vectorToJsonArray( info.center );
  obj["scale"] = info.scale;
  obj["normalization"] = QStringLiteral( "centered_by_mean_and_scaled_by_max_radius" );
  obj["skipBottom"] = true;
  return obj;
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

bool writeJsonDocumentChecked( const QString &path, const QJsonDocument &doc, QString *errorMessage )
{
  const QByteArray bytes = doc.toJson( QJsonDocument::Indented );

  QSaveFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "Cannot open %1 for writing: %2" ).arg( path, file.errorString() );
    return false;
  }

  const qint64 written = file.write( bytes );
  if ( written != bytes.size() )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "Incomplete JSON write: %1/%2 bytes." ).arg( written ).arg( bytes.size() );
    file.cancelWriting();
    return false;
  }

  if ( !file.commit() )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "Cannot commit %1: %2" ).arg( path, file.errorString() );
    return false;
  }

  QFile verifyFile( path );
  if ( !verifyFile.open( QIODevice::ReadOnly ) )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "Cannot reopen %1 for verification: %2" ).arg( path, verifyFile.errorString() );
    return false;
  }

  QJsonParseError parseError;
  QJsonDocument::fromJson( verifyFile.readAll(), &parseError );
  if ( parseError.error != QJsonParseError::NoError )
  {
    if ( errorMessage )
      *errorMessage = QObject::tr( "Written JSON verification failed at offset %1: %2" )
                        .arg( parseError.offset )
                        .arg( parseError.errorString() );
    return false;
  }

  return true;
}

// ====================================================================
// point cloud metadata
// ====================================================================

double maxAbsComponent( const QVector3D &v )
{
  return std::max( { std::abs( static_cast<double>( v.x() ) ),
                     std::abs( static_cast<double>( v.y() ) ),
                     std::abs( static_cast<double>( v.z() ) ) } );
}

bool pointCloudLooksNormalizedForDisplay( const PointCloud &pc )
{
  if ( pc.points.isEmpty() )
    return false;

  const QVector3D bboxSize = pc.bboxMax - pc.bboxMin;
  const double maxDim = maxAbsComponent( bboxSize );
  if ( maxDim <= 3.5 )
    return true;

  QVector3D center( 0, 0, 0 );
  for ( const QVector3D &p : pc.points )
    center += p;
  center /= static_cast<float>( pc.points.size() );

  double maxRadius = 0.0;
  for ( const QVector3D &p : pc.points )
    maxRadius = std::max( maxRadius, static_cast<double>( ( p - center ).length() ) );

  return maxRadius <= 1.5;
}

bool denormInfoFromPlyComment( const QString &filePath, QVector3D &center, double &scale )
{
  if ( QFileInfo( filePath ).suffix().compare( QStringLiteral( "ply" ), Qt::CaseInsensitive ) != 0 )
    return false;

  QFile file( filePath );
  if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
    return false;

  bool hasCenter = false;
  bool hasScale = false;
  while ( !file.atEnd() )
  {
    const QString line = QString::fromLatin1( file.readLine() ).trimmed();
    if ( line == QStringLiteral( "end_header" ) )
      break;

    if ( line.startsWith( QStringLiteral( "comment denorm_center " ) ) ||
         line.startsWith( QStringLiteral( "comment center " ) ) )
    {
      const QStringList parts = line.split( ' ', Qt::SkipEmptyParts );
      if ( parts.size() >= 5 )
      {
        bool okX = false;
        bool okY = false;
        bool okZ = false;
        const double x = parts.at( 2 ).toDouble( &okX );
        const double y = parts.at( 3 ).toDouble( &okY );
        const double z = parts.at( 4 ).toDouble( &okZ );
        if ( okX && okY && okZ )
        {
          center = QVector3D( x, y, z );
          hasCenter = true;
        }
      }
    }
    else if ( line.startsWith( QStringLiteral( "comment denorm_scale " ) ) ||
              line.startsWith( QStringLiteral( "comment scale " ) ) )
    {
      const QStringList parts = line.split( ' ', Qt::SkipEmptyParts );
      if ( parts.size() >= 3 )
      {
        bool ok = false;
        const double s = parts.at( 2 ).toDouble( &ok );
        if ( ok && s > 1e-9 )
        {
          scale = s;
          hasScale = true;
        }
      }
    }
  }

  return hasCenter && hasScale;
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

bool metadataPointCloudInfoForInput( const QString &filePath,
                                     QVector3D *bboxMin,
                                     QVector3D *center,
                                     double *scale )
{
  const QString rel = metadataRelativePathForPointCloud( filePath );
  if ( !rel.isEmpty() )
  {
    QFile metadataFile( ParamModelerConfig::metadataJsonPath() );
    if ( metadataFile.open( QIODevice::ReadOnly ) )
    {
      QJsonParseError parseError;
      const QJsonDocument doc = QJsonDocument::fromJson( metadataFile.readAll(), &parseError );
      if ( parseError.error == QJsonParseError::NoError && doc.isArray() )
      {
        const QString relLower = rel.toLower();
        const QJsonArray records = doc.array();
        for ( const QJsonValue &value : records )
        {
          const QJsonObject obj = value.toObject();
          if ( obj.value( QStringLiteral( "file" ) ).toString().toLower() != relLower )
            continue;

          const QJsonObject info = obj.value( QStringLiteral( "pointCloudInfo" ) ).toObject();
          bool ok = true;
          if ( bboxMin )
            ok = vectorFromJsonArray( info.value( QStringLiteral( "bboxMin" ) ), *bboxMin ) && ok;
          if ( center )
            ok = vectorFromJsonArray( info.value( QStringLiteral( "center" ) ), *center ) && ok;
          if ( scale )
          {
            *scale = info.value( QStringLiteral( "scale" ) ).toDouble( 1.0 );
            ok = *scale > 1e-9 && ok;
          }
          if ( ok )
            return true;
        }
      }
    }
  }

  QVector3D plyCenter;
  double plyScale = 1.0;
  if ( denormInfoFromPlyComment( filePath, plyCenter, plyScale ) )
  {
    if ( bboxMin )
    {
      const PointCloud pc = PointCloudLoader::load( filePath );
      if ( pc.points.isEmpty() )
        return false;
      *bboxMin = pc.bboxMin * static_cast<float>( plyScale ) + plyCenter;
    }
    if ( center )
      *center = plyCenter;
    if ( scale )
      *scale = plyScale;
    return true;
  }

  // Fallback: compute directly from the point cloud itself
  {
    const PointCloud pc = PointCloudLoader::load( filePath );
    if ( !pc.points.isEmpty() )
    {
      const QVector3D pcCenter = ( pc.bboxMin + pc.bboxMax ) * 0.5f;
      double maxRadius = 0.0;
      for ( const QVector3D &p : pc.points )
        maxRadius = qMax( maxRadius, static_cast<double>( ( p - pcCenter ).length() ) );
      if ( maxRadius < 1e-8 )
        maxRadius = 1.0;

      if ( bboxMin )
        *bboxMin = pc.bboxMin;
      if ( center )
        *center = pcCenter;
      if ( scale )
        *scale = maxRadius;
      return true;
    }
  }

  return false;
}

// ====================================================================
// dataset path helpers
// ====================================================================

QString safeClassDirName( const QString &primitiveType )
{
  QString safe = primitiveType;
  safe.replace( QRegularExpression( "[^A-Za-z0-9_\\-]" ), "_" );
  return safe;
}

QString datasetRootFromSelectedFolder( const QString &selectedPath )
{
  const QFileInfo selectedInfo( selectedPath );
  if ( selectedInfo.fileName().compare( QStringLiteral( "datasets" ), Qt::CaseInsensitive ) == 0 )
    return selectedPath;

  return QDir( selectedPath ).filePath( QStringLiteral( "datasets" ) );
}
