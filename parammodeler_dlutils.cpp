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
#include "parammodeler_pick3d_math.h"

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
#include <functional>
#include <limits>
#include <utility>
#include <windows.h>

#define DEBUG_LOG( msg ) OutputDebugStringW( msg )

// ====================================================================
// DL parameter mapping
// ====================================================================

QMap<QString, double> pointNetParamsToUiParams( const QString &primitiveType,
                                                 const QMap<QString, double> &nn )
{
  QMap<QString, double> uiParams;
  const auto has = [&]( const QString &key ) { return nn.contains( key ); };
  const auto val = [&]( const QString &key, double fallback = 0.0 ) { return nn.value( key, fallback ); };
  const auto clamp01 = []( double x ) { return std::max( 0.0, std::min( 1.0, x ) ); };
  const auto clampMarginPair = []( double a, double b ) {
    a = std::max( 0.0, std::min( 1.0, a ) );
    b = std::max( 0.0, std::min( 1.0, b ) );
    const double sum = a + b;
    if ( sum > 0.95 )
    {
      const double factor = 0.95 / sum;
      a *= factor;
      b *= factor;
    }
    return std::pair<double, double>( a, b );
  };
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
    const bool hasFootprintParams =
      has( QStringLiteral( "outerLength" ) ) ||
      has( QStringLiteral( "outerWidth" ) ) ||
      has( QStringLiteral( "cutoutLengthRatio" ) ) ||
      has( QStringLiteral( "cutoutWidthRatio" ) );
    if ( hasFootprintParams )
    {
      put( QStringLiteral( "outerLength" ), QStringLiteral( "lTotalL" ) );
      put( QStringLiteral( "outerWidth" ), QStringLiteral( "lTotalW" ) );
      if ( has( QStringLiteral( "cutoutLengthRatio" ) ) )
        uiParams.insert( QStringLiteral( "lWingR" ), clamp01( val( QStringLiteral( "cutoutLengthRatio" ) ) ) );
      if ( has( QStringLiteral( "cutoutWidthRatio" ) ) )
        uiParams.insert( QStringLiteral( "lWingWR" ), clamp01( 1.0 - val( QStringLiteral( "cutoutWidthRatio" ) ) ) );
    }
    else
    {
      put( QStringLiteral( "totalLength" ), QStringLiteral( "lTotalL" ) );
      put( QStringLiteral( "wingRatio" ), QStringLiteral( "lWingR" ) );
      put( QStringLiteral( "totalWidth" ), QStringLiteral( "lTotalW" ) );
      put( QStringLiteral( "wingWidthRatio" ), QStringLiteral( "lWingWR" ) );
    }
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
    if ( has( QStringLiteral( "topLengthRatio" ) ) && has( QStringLiteral( "bottomLength" ) ) )
      uiParams.insert( QStringLiteral( "tpTopLength" ),
                       val( QStringLiteral( "bottomLength" ) ) * clamp01( val( QStringLiteral( "topLengthRatio" ) ) ) );
    else
      put( QStringLiteral( "topLength" ), QStringLiteral( "tpTopLength" ) );
    if ( has( QStringLiteral( "topWidthRatio" ) ) && has( QStringLiteral( "bottomWidth" ) ) )
      uiParams.insert( QStringLiteral( "tpTopWidth" ),
                       val( QStringLiteral( "bottomWidth" ) ) * clamp01( val( QStringLiteral( "topWidthRatio" ) ) ) );
    else
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
    const double outerLength = val( QStringLiteral( "outerLength" ), 0.0 );
    const double outerWidth = val( QStringLiteral( "outerWidth" ), 0.0 );
    put( QStringLiteral( "innerHeight" ), QStringLiteral( "icInnerH" ) );
    const bool hasMarginX = has( QStringLiteral( "leftMarginRatio" ) ) ||
                            has( QStringLiteral( "rightMarginRatio" ) );
    const bool hasMarginY = has( QStringLiteral( "frontMarginRatio" ) ) ||
                            has( QStringLiteral( "backMarginRatio" ) );
    if ( hasMarginX && outerLength > 1e-6 )
    {
      auto margins = clampMarginPair( val( QStringLiteral( "leftMarginRatio" ) ),
                                      val( QStringLiteral( "rightMarginRatio" ) ) );
      uiParams.insert( QStringLiteral( "icOffsetX" ), outerLength * margins.first );
      uiParams.insert( QStringLiteral( "icInnerL" ), outerLength * std::max( 0.05, 1.0 - margins.first - margins.second ) );
    }
    else
    {
      if ( has( QStringLiteral( "innerLengthRatio" ) ) && outerLength > 1e-6 )
        uiParams.insert( QStringLiteral( "icInnerL" ),
                         outerLength * clamp01( val( QStringLiteral( "innerLengthRatio" ) ) ) );
      else
        put( QStringLiteral( "innerLength" ), QStringLiteral( "icInnerL" ) );
      if ( has( QStringLiteral( "innerMinXRatio" ) ) && outerLength > 1e-6 )
        uiParams.insert( QStringLiteral( "icOffsetX" ), outerLength * clamp01( val( QStringLiteral( "innerMinXRatio" ) ) ) );
      else
        put( QStringLiteral( "offsetX" ), QStringLiteral( "icOffsetX" ) );
    }
    if ( hasMarginY && outerWidth > 1e-6 )
    {
      auto margins = clampMarginPair( val( QStringLiteral( "frontMarginRatio" ) ),
                                      val( QStringLiteral( "backMarginRatio" ) ) );
      uiParams.insert( QStringLiteral( "icOffsetY" ), outerWidth * margins.first );
      uiParams.insert( QStringLiteral( "icInnerW" ), outerWidth * std::max( 0.05, 1.0 - margins.first - margins.second ) );
    }
    else
    {
      if ( has( QStringLiteral( "innerWidthRatio" ) ) && outerWidth > 1e-6 )
        uiParams.insert( QStringLiteral( "icInnerW" ),
                         outerWidth * clamp01( val( QStringLiteral( "innerWidthRatio" ) ) ) );
      else
        put( QStringLiteral( "innerWidth" ), QStringLiteral( "icInnerW" ) );
      if ( has( QStringLiteral( "innerMinYRatio" ) ) && outerWidth > 1e-6 )
        uiParams.insert( QStringLiteral( "icOffsetY" ), outerWidth * clamp01( val( QStringLiteral( "innerMinYRatio" ) ) ) );
      else
        put( QStringLiteral( "offsetY" ), QStringLiteral( "icOffsetY" ) );
    }
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
  // 几何对齐模块直接输出 poseRotateZ，透传
  put( QStringLiteral( "poseRotateZ" ), QStringLiteral( "poseRotateZ" ) );

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
    params["outerLength"]       = dock->LTotalLength();
    params["outerWidth"]        = dock->LTotalWidth();
    params["cutoutLengthRatio"] = dock->LWingRatio();
    params["cutoutWidthRatio"]  = 1.0 - dock->LWingWidthRatio();
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
    const double bottomLength = dock->tpBottomLength();
    const double bottomWidth = dock->tpBottomWidth();
    params["bottomLength"] = bottomLength;
    params["bottomWidth"] = bottomWidth;
    params["topLengthRatio"] = bottomLength > 1e-6 ? dock->tpTopLength() / bottomLength : 0.6;
    params["topWidthRatio"] = bottomWidth > 1e-6 ? dock->tpTopWidth() / bottomWidth : 0.6;
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
    const double outerLength = dock->icOuterLength();
    const double outerWidth = dock->icOuterWidth();
    const double innerLength = dock->icInnerLength();
    const double innerWidth = dock->icInnerWidth();
    const double offsetX = dock->icOffsetX();
    const double offsetY = dock->icOffsetY();
    params["outerLength"] = outerLength;
    params["outerWidth"] = outerWidth;
    params["outerHeight"] = dock->icOuterHeight();
    params["innerHeight"] = dock->icInnerHeight();
    params["leftMarginRatio"] = outerLength > 1e-6 ? offsetX / outerLength : 0.2;
    params["rightMarginRatio"] = outerLength > 1e-6 ? ( outerLength - offsetX - innerLength ) / outerLength : 0.4;
    params["frontMarginRatio"] = outerWidth > 1e-6 ? offsetY / outerWidth : 0.2;
    params["backMarginRatio"] = outerWidth > 1e-6 ? ( outerWidth - offsetY - innerWidth ) / outerWidth : 0.4;
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

struct MetadataLookup
{
  QString relativePath;
  QString metadataPath;
};

MetadataLookup metadataLookupForPointCloud( const QString &filePath )
{
  QString normalized = QDir::fromNativeSeparators( QFileInfo( filePath ).absoluteFilePath() );
  QFileInfo fileInfo( normalized );

  // Prefer the metadata stored next to the dataset root. This supports versioned
  // dataset folders such as datasets_aug_v7 without requiring settings changes.
  QDir dir = fileInfo.absoluteDir();
  while ( true )
  {
    const QString candidate = dir.filePath( QStringLiteral( "metadata/sample_params.json" ) );
    if ( QFileInfo::exists( candidate ) )
    {
      QString rel = QDir::fromNativeSeparators( dir.relativeFilePath( normalized ) );
      if ( !rel.isEmpty() && !rel.startsWith( QStringLiteral( "../" ) ) &&
           !rel.startsWith( QStringLiteral( "metadata/" ), Qt::CaseInsensitive ) )
      {
        QFileInfo relInfo( rel );
        const QString relDir = relInfo.path() == QStringLiteral( "." ) ? QString() : relInfo.path() + QStringLiteral( "/" );
        return { relDir + relInfo.completeBaseName() + QStringLiteral( ".txt" ),
                 QDir::fromNativeSeparators( QFileInfo( candidate ).absoluteFilePath() ) };
      }
    }

    if ( !dir.cdUp() )
      break;
  }

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

  // 原始导出目录 datasets/（未增强）同样需要反归一化，否则加载后点云停留在归一化尺度、显示为极小的一团
  const QString rawMarker = QStringLiteral( "/datasets/" );
  const int rawIdx = lower.indexOf( rawMarker );
  if ( rel.isEmpty() && rawIdx >= 0 )
    rel = normalized.mid( rawIdx + rawMarker.size() );

  if ( rel.isEmpty() || rel.startsWith( QStringLiteral( "metadata/" ), Qt::CaseInsensitive ) )
    return {};

  QFileInfo relInfo( rel );
  const QString relDir = relInfo.path() == QStringLiteral( "." ) ? QString() : relInfo.path() + QStringLiteral( "/" );
  return { relDir + relInfo.completeBaseName() + QStringLiteral( ".txt" ),
           QDir::fromNativeSeparators( QFileInfo( ParamModelerConfig::metadataJsonPath() ).absoluteFilePath() ) };
}

bool metadataPointCloudInfoForInput( const QString &filePath,
                                     QVector3D *bboxMin,
                                     QVector3D *bboxSize,
                                     QVector3D *center,
                                     double *scale,
                                     double *rz )
{
  // 只有 JSON metadata 记了 rz；PLY / 兜底路径取不到 → 保持 NaN。
  // 调用方以 NaN 作为"该记录没有 rz"的判据，所以必须先置位。
  if ( rz )
    *rz = qQNaN();

  const MetadataLookup lookup = metadataLookupForPointCloud( filePath );
  if ( !lookup.relativePath.isEmpty() && !lookup.metadataPath.isEmpty() )
  {
    QFile metadataFile( lookup.metadataPath );
    if ( metadataFile.open( QIODevice::ReadOnly ) )
    {
      QJsonParseError parseError;
      const QJsonDocument doc = QJsonDocument::fromJson( metadataFile.readAll(), &parseError );
      if ( parseError.error == QJsonParseError::NoError && doc.isArray() )
      {
        const QString relLower = lookup.relativePath.toLower();
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
          if ( bboxSize )
            ok = vectorFromJsonArray( info.value( QStringLiteral( "bboxSize" ) ), *bboxSize ) && ok;
          if ( center )
            ok = vectorFromJsonArray( info.value( QStringLiteral( "center" ) ), *center ) && ok;
          if ( scale )
          {
            *scale = info.value( QStringLiteral( "scale" ) ).toDouble( 1.0 );
            ok = *scale > 1e-9 && ok;
          }
          if ( rz )
          {
            // params.rz：导出时 applyPose 用的那个角（度）。缺失 → 保持 NaN，不算记录无效
            const QJsonValue rzValue = obj.value( QStringLiteral( "params" ) )
                                          .toObject().value( QStringLiteral( "rz" ) );
            if ( rzValue.isDouble() )
              *rz = rzValue.toDouble();
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
    if ( bboxMin || bboxSize )
    {
      const PointCloud pc = PointCloudLoader::load( filePath );
      if ( pc.points.isEmpty() )
        return false;
      if ( bboxMin )
        *bboxMin = pc.bboxMin * static_cast<float>( plyScale ) + plyCenter;
      if ( bboxSize )
        *bboxSize = ( pc.bboxMax - pc.bboxMin ) * static_cast<float>( plyScale );
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
      if ( bboxSize )
        *bboxSize = pc.bboxMax - pc.bboxMin;
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
  const QString folderName = selectedInfo.fileName();
  if ( folderName.compare( QStringLiteral( "datasets" ), Qt::CaseInsensitive ) == 0 ||
       folderName.startsWith( QStringLiteral( "datasets_" ), Qt::CaseInsensitive ) )
    return selectedPath;

  const QDir selectedDir( selectedPath );
  const bool looksLikeDatasetRoot =
    selectedDir.exists( QStringLiteral( "metadata" ) ) ||
    selectedDir.exists( QStringLiteral( "train" ) ) ||
    selectedDir.exists( QStringLiteral( "val" ) ) ||
    selectedDir.exists( QStringLiteral( "test" ) );
  if ( looksLikeDatasetRoot )
    return selectedPath;

  return selectedDir.filePath( QStringLiteral( "datasets" ) );
}

// ============================================================
// metadata 记录明显是"从归一化坐标存下来的坏记录"：center ≈ 0 且 scale ≈ 1。
// 显示路径和对齐路径必须用同一判据，否则会出现"点云画在 A、模型对到 B"。
// ============================================================
bool pointCloudMetadataLooksBuggy( const QVector3D &center, double scale )
{
  return scale < 2.0 && center.lengthSquared() < 0.01;
}

bool usesSemanticUpperAlignment( const QString &primitiveType )
{
  return primitiveType == QStringLiteral( "Cuboid" ) ||
         primitiveType == QStringLiteral( "PyramidRoof" ) ||
         primitiveType == QStringLiteral( "TruncatedPyramidRoof" ) ||
         primitiveType == QStringLiteral( "GabledRoof" ) ||
         primitiveType == QStringLiteral( "AsymmetricGableHouse" ) ||
         primitiveType == QStringLiteral( "IndentedCuboid" ) ||
         primitiveType == QStringLiteral( "LHouse" );
}

QMatrix4x4 modelRotationMatrix( const ParamModelerDock *dock )
{
  QMatrix4x4 posMat;
  posMat.setToIdentity();
  posMat.rotate( static_cast<float>( dock->poseRotateX() ), 1, 0, 0 );
  posMat.rotate( static_cast<float>( dock->poseRotateY() ), 0, 1, 0 );
  posMat.rotate( static_cast<float>( dock->poseRotateZ() ), 0, 0, 1 );
  return posMat;
}

// ============================================================
// 稳健包围盒：XY 取分位数，Z 底部取硬 min。
//
// 为什么必须这样：点云里的离群点（真实扫描的误匹配点，或 datasets_aug 里
// add_outliers 撒的点）数量很少但离得远，硬 min/max 会被它们完全主导。
// 实测（datasets_aug，每朵云约 1% 离群点、撒在归一化空间 [-1.15,1.15]^3）
// 角锚定点被拉偏 3–7 m —— 模型对齐就落不到建筑的左下角。
//
// 为什么 XY 分位数不会削掉建筑本体：建筑在水平轴上的极值是被一整条边/面（长方体）
// 或平方根聚集的点列（圆柱）逼近的，1% 分位与真实极值几乎重合。
// 但 Z 不能用 1% 分位：底面通常不采样，墙脚点也可能很稀，分位数会把模型整体垫高。
// 所以 Z 的 min 保留硬最小值；显示路径已把反归一化后的负 Z clamp 到 0。
// 点数 <100 时退回硬 min/max（样本太少，分位数没有意义）。
// ============================================================
void hardBBoxFromPoints( const QVector<QVector3D> &pts,
                                QVector3D &outMin, QVector3D &outMax )
{
  outMin = QVector3D();
  outMax = QVector3D();
  if ( pts.isEmpty() )
    return;

  outMin = pts.first();
  outMax = pts.first();
  for ( const QVector3D &p : pts )
  {
    if ( p.x() < outMin.x() ) outMin.setX( p.x() );
    if ( p.y() < outMin.y() ) outMin.setY( p.y() );
    if ( p.z() < outMin.z() ) outMin.setZ( p.z() );
    if ( p.x() > outMax.x() ) outMax.setX( p.x() );
    if ( p.y() > outMax.y() ) outMax.setY( p.y() );
    if ( p.z() > outMax.z() ) outMax.setZ( p.z() );
  }
}

void robustBBoxFromPoints( const QVector<QVector3D> &pts,
                                  QVector3D &outMin, QVector3D &outMax )
{
  outMin = QVector3D();
  outMax = QVector3D();
  const int n = pts.size();
  if ( n == 0 )
    return;

  int lo = 0;
  int hi = n - 1;
  if ( n >= 100 )
  {
    lo = static_cast<int>( std::floor( 0.01 * n ) );
    hi = n - 1 - lo;
  }

  QVector<float> col;
  col.resize( n );
  float mn[3] = { pts[0].x(), pts[0].y(), pts[0].z() };
  float mx[3] = { pts[0].x(), pts[0].y(), pts[0].z() };
  for ( int a = 0; a < 3; ++a )
  {
    for ( int i = 0; i < n; ++i )
      col[i] = pts[i][a];
    std::sort( col.begin(), col.end() );
    if ( a < 2 )
    {
      mn[a] = col[lo];
      mx[a] = col[hi];
    }
    else
    {
      mn[a] = col.first();
      mx[a] = col.last();
    }
  }
  outMin = QVector3D( mn[0], mn[1], mn[2] );
  outMax = QVector3D( mx[0], mx[1], mx[2] );
}

static bool loadRestoredPointsForGeometryCorrection( const QString &filePath,
                                                     QVector<QVector3D> &points )
{
  points.clear();
  if ( filePath.isEmpty() )
    return false;

  QVector3D metadataCenter;
  double metadataScale = 1.0;
  const bool hasMetadata = metadataPointCloudInfoForInput( filePath, nullptr, nullptr,
                                                           &metadataCenter, &metadataScale );
  const PointCloud pc = PointCloudLoader::load( filePath );
  if ( pc.points.isEmpty() )
    return false;

  points.reserve( pc.points.size() );
  const bool restoreFromDlNormalization = hasMetadata && pointCloudLooksNormalizedForDisplay( pc );
  for ( const QVector3D &p : pc.points )
  {
    QVector3D q = restoreFromDlNormalization
      ? p * static_cast<float>( metadataScale ) + metadataCenter
      : p;
    if ( q.z() < 0.0f && restoreFromDlNormalization )
      q.setZ( 0.0f );
    points.append( q );
  }
  return !points.isEmpty();
}

static QVector<QVector3D> unrotatePointsAroundZ( const QVector<QVector3D> &points, double rzDeg )
{
  QVector<QVector3D> out;
  out.reserve( points.size() );
  constexpr double pi = 3.14159265358979323846;
  const double a = -rzDeg * pi / 180.0;
  const double c = std::cos( a );
  const double s = std::sin( a );
  for ( const QVector3D &p : points )
  {
    const double x = static_cast<double>( p.x() );
    const double y = static_cast<double>( p.y() );
    out.append( QVector3D( static_cast<float>( x * c - y * s ),
                           static_cast<float>( x * s + y * c ),
                           p.z() ) );
  }
  return out;
}

static bool footprintBBoxForGeometryCorrection( const QVector<QVector3D> &points,
                                                QVector3D &outMin, QVector3D &outMax,
                                                QString &source )
{
  if ( points.isEmpty() )
    return false;

  QVector3D hardMin, hardMax;
  hardBBoxFromPoints( points, hardMin, hardMax );
  QVector3D fullMin, fullMax;
  robustBBoxFromPoints( points, fullMin, fullMax );

  const double height = static_cast<double>( hardMax.z() - hardMin.z() );
  const double sliceTop = static_cast<double>( hardMin.z() ) + std::max( 0.5, height * 0.12 );
  QVector<QVector3D> bottom;
  bottom.reserve( points.size() );
  for ( const QVector3D &p : points )
  {
    if ( static_cast<double>( p.z() ) <= sliceTop )
      bottom.append( p );
  }

  const int minSlicePoints = std::max( 50, static_cast<int>( std::ceil( points.size() * 0.05 ) ) );
  if ( bottom.size() >= minSlicePoints )
  {
    QVector3D sliceMin, sliceMax;
    robustBBoxFromPoints( bottom, sliceMin, sliceMax );
    const QVector3D fullRange = fullMax - fullMin;
    const QVector3D sliceRange = sliceMax - sliceMin;
    const bool xReasonable = fullRange.x() <= 1e-6f || sliceRange.x() >= fullRange.x() * 0.25f;
    const bool yReasonable = fullRange.y() <= 1e-6f || sliceRange.y() >= fullRange.y() * 0.25f;
    if ( xReasonable && yReasonable )
    {
      outMin = QVector3D( sliceMin.x(), sliceMin.y(), hardMin.z() );
      outMax = QVector3D( sliceMax.x(), sliceMax.y(), hardMax.z() );
      source = QStringLiteral( "bottom" );
      return true;
    }
  }

  outMin = QVector3D( fullMin.x(), fullMin.y(), hardMin.z() );
  outMax = QVector3D( fullMax.x(), fullMax.y(), hardMax.z() );
  source = QStringLiteral( "full" );
  return true;
}

static bool estimateGabledWallRatioFromProfile( const QVector<QVector3D> &points,
                                                double baseWidth,
                                                double zMin,
                                                double height,
                                                double &outRatio )
{
  if ( points.size() < 100 || baseWidth <= 1e-6 || height <= 1e-6 )
    return false;

  constexpr int bins = 24;
  QVector<QVector<float>> yByBin;
  yByBin.resize( bins );
  for ( const QVector3D &p : points )
  {
    const double t = ( static_cast<double>( p.z() ) - zMin ) / height;
    const int idx = std::max( 0, std::min( bins - 1, static_cast<int>( std::floor( t * bins ) ) ) );
    yByBin[idx].append( p.y() );
  }

  for ( int i = 2; i < bins - 2; ++i )
  {
    QVector<float> col = yByBin[i];
    if ( col.size() < 10 )
      continue;
    std::sort( col.begin(), col.end() );
    const int lo = static_cast<int>( std::floor( col.size() * 0.05 ) );
    const int hi = std::max( lo, static_cast<int>( std::ceil( col.size() * 0.95 ) ) - 1 );
    const double yRange = static_cast<double>( col[hi] - col[lo] );
    if ( yRange < baseWidth * 0.88 )
    {
      outRatio = std::max( 0.2, std::min( 0.9, ( i + 0.5 ) / bins ) );
      return true;
    }
  }
  return false;
}

static bool robustSliceBBox( const QVector<QVector3D> &points,
                             const std::function<bool( const QVector3D & )> &accept,
                             QVector3D &outMin, QVector3D &outMax, int minPoints = 24 )
{
  QVector<QVector3D> slice;
  slice.reserve( points.size() );
  for ( const QVector3D &p : points )
    if ( accept( p ) )
      slice.append( p );
  if ( slice.size() < minPoints )
    return false;
  robustBBoxFromPoints( slice, outMin, outMax );
  return true;
}

static bool estimateShrinkWallRatio( const QVector<QVector3D> &points,
                                     double baseLength,
                                     double baseWidth,
                                     double zMin,
                                     double height,
                                     double &outRatio )
{
  if ( points.size() < 100 || baseLength <= 1e-6 || baseWidth <= 1e-6 || height <= 1e-6 )
    return false;

  constexpr int bins = 24;
  QVector<QVector<QVector3D>> byBin;
  byBin.resize( bins );
  for ( const QVector3D &p : points )
  {
    const double t = ( static_cast<double>( p.z() ) - zMin ) / height;
    const int idx = std::max( 0, std::min( bins - 1, static_cast<int>( std::floor( t * bins ) ) ) );
    byBin[idx].append( p );
  }

  for ( int i = 2; i < bins - 2; ++i )
  {
    if ( byBin[i].size() < 10 )
      continue;
    QVector3D mn, mx;
    robustBBoxFromPoints( byBin[i], mn, mx );
    const double xRange = static_cast<double>( mx.x() - mn.x() );
    const double yRange = static_cast<double>( mx.y() - mn.y() );
    if ( xRange < baseLength * 0.90 || yRange < baseWidth * 0.90 )
    {
      outRatio = std::max( 0.15, std::min( 0.95, ( i + 0.5 ) / bins ) );
      return true;
    }
  }
  return false;
}

static bool estimateRoofEaveFromOuterWalls( const QVector<QVector3D> &points,
                                            const QVector3D &fpMin, const QVector3D &fpMax,
                                            bool gabled, double zMin, double height,
                                            double &eaveZ )
{
  const double length = static_cast<double>( fpMax.x() - fpMin.x() );
  const double width = static_cast<double>( fpMax.y() - fpMin.y() );
  if ( length <= 1e-6 || width <= 1e-6 || height <= 1e-6 )
    return false;

  QVector<float> wallZ;
  wallZ.reserve( points.size() / 3 );
  for ( const QVector3D &p : points )
  {
    const bool nearYWall = std::abs( p.y() - fpMin.y() ) <= width * 0.04 ||
                           std::abs( p.y() - fpMax.y() ) <= width * 0.04;
    const bool nearXWall = std::abs( p.x() - fpMin.x() ) <= length * 0.04 ||
                           std::abs( p.x() - fpMax.x() ) <= length * 0.04;
    if ( nearYWall || ( !gabled && nearXWall ) )
      wallZ.append( p.z() );
  }
  if ( wallZ.size() < std::max( 32, static_cast<int>( points.size() * 0.03 ) ) )
    return false;

  std::sort( wallZ.begin(), wallZ.end() );
  const double low = wallZ[static_cast<int>( wallZ.size() * 0.10 )];
  const double high = wallZ[static_cast<int>( wallZ.size() * 0.98 )];
  if ( low > zMin + height * 0.35 ||
       high < zMin + height * 0.15 || high > zMin + height * 0.98 )
    return false;
  eaveZ = high;
  return true;
}

// Locate the point-cloud counterpart of the manual alignment UpperOrigin.
// XY is the robust lower-left footprint corner in the unrotated model frame.
// Z is the global top for flat-topped solids, or the wall/roof transition for roofs.
// Roof eaves are measured from outer wall samples, so the exact corner may be absent.
bool semanticUpperPointCloudTarget( const QString &filePath,
                                           const QString &primitiveType,
                                           double rzDeg,
                                           double fallbackUpperOffset,
                                           bool useDisplayCoordinates,
                                           bool displayWasRestored,
                                           const QVector3D &displayRestoreCenter,
                                           double displayRestoreScale,
                                           QVector3D &out,
                                           QString &detail )
{
  if ( !usesSemanticUpperAlignment( primitiveType ) || !std::isfinite( rzDeg ) )
    return false;

  QVector<QVector3D> points;
  if ( useDisplayCoordinates )
  {
    const PointCloud pc = PointCloudLoader::load( filePath );
    points.reserve( pc.points.size() );
    for ( const QVector3D &p : pc.points )
    {
      QVector3D q = displayWasRestored
        ? p * static_cast<float>( displayRestoreScale ) + displayRestoreCenter : p;
      if ( displayWasRestored && q.z() < 0.0f )
        q.setZ( 0.0f );
      points.append( q );
    }
  }
  else if ( !loadRestoredPointsForGeometryCorrection( filePath, points ) )
    return false;
  if ( points.size() < 32 )
    return false;

  const QVector<QVector3D> canonicalPoints = std::abs( rzDeg ) > 1e-9
    ? unrotatePointsAroundZ( points, rzDeg )
    : points;

  QVector3D hardMin, hardMax;
  hardBBoxFromPoints( canonicalPoints, hardMin, hardMax );
  const double height = static_cast<double>( hardMax.z() - hardMin.z() );
  if ( height <= 1e-6 )
    return false;

  QVector3D fpMin, fpMax;
  QString footprintSource;
  if ( !footprintBBoxForGeometryCorrection( canonicalPoints, fpMin, fpMax, footprintSource ) )
    return false;
  const double length = static_cast<double>( fpMax.x() - fpMin.x() );
  const double width = static_cast<double>( fpMax.y() - fpMin.y() );
  if ( length <= 1e-6 || width <= 1e-6 )
    return false;

  double targetZ = static_cast<double>( hardMax.z() );
  detail = QStringLiteral( "top/%1" ).arg( footprintSource );

  const bool gabled = primitiveType == QStringLiteral( "GabledRoof" ) ||
                      primitiveType == QStringLiteral( "AsymmetricGableHouse" );
  const bool shrinkingRoof = primitiveType == QStringLiteral( "PyramidRoof" ) ||
                             primitiveType == QStringLiteral( "TruncatedPyramidRoof" );
  if ( gabled || shrinkingRoof )
  {
    if ( estimateRoofEaveFromOuterWalls( canonicalPoints, fpMin, fpMax, gabled,
                                        static_cast<double>( hardMin.z() ), height, targetZ ) )
    {
      detail = QStringLiteral( "wall-eave/%1" ).arg( footprintSource );
    }
    else
    {
      // Missing walls near the chosen corner do not make alignment fail: preserve
      // base-height alignment using the regressed model's own eave offset.
      if ( !std::isfinite( fallbackUpperOffset ) || fallbackUpperOffset < 0.0 ||
           fallbackUpperOffset > height * 1.5 )
        return false;
      targetZ = static_cast<double>( hardMin.z() ) + fallbackUpperOffset;
      detail = QStringLiteral( "predicted-eave/%1" ).arg( footprintSource );
    }
  }

  constexpr double pi = 3.14159265358979323846;
  const double a = rzDeg * pi / 180.0;
  const double c = std::cos( a );
  const double s = std::sin( a );
  const double x = static_cast<double>( fpMin.x() );
  const double y = static_cast<double>( fpMin.y() );
  out = QVector3D( static_cast<float>( x * c - y * s ),
                   static_cast<float>( x * s + y * c ),
                   static_cast<float>( targetZ ) );
  return std::isfinite( out.x() ) && std::isfinite( out.y() ) && std::isfinite( out.z() );
}

static bool estimateLHouseCutoutRatios( const QVector<QVector3D> &points,
                                        const QVector3D &fpMin,
                                        const QVector3D &fpMax,
                                        double zMin,
                                        double height,
                                        double &cutoutLengthRatio,
                                        double &wingWidthRatio )
{
  const double length = static_cast<double>( fpMax.x() - fpMin.x() );
  const double width = static_cast<double>( fpMax.y() - fpMin.y() );
  if ( points.size() < 100 || length <= 1e-6 || width <= 1e-6 || height <= 1e-6 )
    return false;

  const double sliceTop = zMin + std::max( 0.5, height * 0.30 );
  double maxXInUpperY = -std::numeric_limits<double>::infinity();
  double maxYInRightX = -std::numeric_limits<double>::infinity();
  int upperCount = 0, rightCount = 0;
  for ( const QVector3D &p : points )
  {
    if ( static_cast<double>( p.z() ) > sliceTop )
      continue;
    const double x = ( static_cast<double>( p.x() ) - static_cast<double>( fpMin.x() ) ) / length;
    const double y = ( static_cast<double>( p.y() ) - static_cast<double>( fpMin.y() ) ) / width;
    if ( y > 0.60 )
    {
      maxXInUpperY = std::max( maxXInUpperY, x );
      ++upperCount;
    }
    if ( x > 0.60 )
    {
      maxYInRightX = std::max( maxYInRightX, y );
      ++rightCount;
    }
  }
  if ( upperCount < 12 || rightCount < 12 || !std::isfinite( maxXInUpperY ) || !std::isfinite( maxYInRightX ) )
    return false;

  cutoutLengthRatio = std::max( 0.2, std::min( 0.9, 1.0 - maxXInUpperY ) );
  wingWidthRatio = std::max( 0.2, std::min( 0.9, maxYInRightX ) );
  return true;
}

static bool estimateIndentedInnerBox( const QVector<QVector3D> &points,
                                      const QVector3D &fpMin,
                                      const QVector3D &fpMax,
                                      double zMin,
                                      double height,
                                      QVector3D &innerMin,
                                      QVector3D &innerMax,
                                      double &innerHeight )
{
  const double length = static_cast<double>( fpMax.x() - fpMin.x() );
  const double width = static_cast<double>( fpMax.y() - fpMin.y() );
  if ( points.size() < 100 || length <= 1e-6 || width <= 1e-6 || height <= 1e-6 )
    return false;

  const double marginX = length * 0.08;
  const double marginY = width * 0.08;
  QVector3D recessMin, recessMax;
  if ( !robustSliceBBox( points, [&]( const QVector3D &p ) {
         const double x = static_cast<double>( p.x() );
         const double y = static_cast<double>( p.y() );
         const double z = static_cast<double>( p.z() );
         return x > static_cast<double>( fpMin.x() ) + marginX &&
                x < static_cast<double>( fpMax.x() ) - marginX &&
                y > static_cast<double>( fpMin.y() ) + marginY &&
                y < static_cast<double>( fpMax.y() ) - marginY &&
                z > zMin + height * 0.20;
       }, recessMin, recessMax, std::max( 20, static_cast<int>( points.size() * 0.02 ) ) ) )
    return false;

  const double innerLength = static_cast<double>( recessMax.x() - recessMin.x() );
  const double innerWidth = static_cast<double>( recessMax.y() - recessMin.y() );
  if ( innerLength < length * 0.08 || innerLength > length * 0.92 ||
       innerWidth < width * 0.08 || innerWidth > width * 0.92 )
    return false;

  double recessFloor = std::numeric_limits<double>::infinity();
  int floorCount = 0;
  for ( const QVector3D &p : points )
  {
    const double x = static_cast<double>( p.x() );
    const double y = static_cast<double>( p.y() );
    if ( x >= static_cast<double>( recessMin.x() ) && x <= static_cast<double>( recessMax.x() ) &&
         y >= static_cast<double>( recessMin.y() ) && y <= static_cast<double>( recessMax.y() ) )
    {
      recessFloor = std::min( recessFloor, static_cast<double>( p.z() ) );
      ++floorCount;
    }
  }
  if ( floorCount < 12 || !std::isfinite( recessFloor ) )
    return false;

  innerMin = recessMin;
  innerMax = recessMax;
  innerHeight = std::max( 0.0, zMin + height - recessFloor );
  return innerHeight > 1e-6;
}

static double regularizedGeometryValue( double pctValue, double geometryValue, double strength )
{
  if ( !std::isfinite( geometryValue ) )
    return pctValue;
  if ( !std::isfinite( pctValue ) )
    return geometryValue;
  const double alpha = std::max( 0.0, std::min( 1.0, strength ) );
  return pctValue + alpha * ( geometryValue - pctValue );
}

static double applyRegularizedGeometryValue( QMap<QString, double> &uiParams,
                                             const QString &key,
                                             double geometryValue,
                                             double strength )
{
  const double pctValue = uiParams.value( key, geometryValue );
  const double correctedValue = regularizedGeometryValue( pctValue, geometryValue, strength );
  uiParams.insert( key, correctedValue );
  return correctedValue;
}

void ParamModelerDock::applyDataDrivenParamCorrections( const QString &primitiveType,
                                                        QMap<QString, double> &uiParams ) const
{
  if ( !m_geometryCorrectionEnabled )
    return;

  const QString prim = primitiveType == QStringLiteral( "CylinderHemisphere" )
                         ? QStringLiteral( "CylinderDome" )
                         : primitiveType;
  if ( prim != QStringLiteral( "Cuboid" ) &&
       prim != QStringLiteral( "Cylinder" ) &&
       prim != QStringLiteral( "GabledRoof" ) &&
       prim != QStringLiteral( "HalfCylinderRoof" ) &&
       prim != QStringLiteral( "TruncatedPyramidRoof" ) &&
       prim != QStringLiteral( "LHouse" ) &&
       prim != QStringLiteral( "IndentedCuboid" ) )
    return;

  QVector<QVector3D> points;
  if ( !loadRestoredPointsForGeometryCorrection( m_inputDataPath, points ) )
  {
    DEBUG_LOG( QString( "[ParamCorrection] %1 skipped: no readable points\n" ).arg( prim ).toStdWString().c_str() );
    return;
  }

  double correctionRz = m_hasMetadataRz ? m_metadataRz : qQNaN();
  if ( !std::isfinite( correctionRz ) )
  {
    double metadataRz = qQNaN();
    metadataPointCloudInfoForInput( m_inputDataPath, nullptr, nullptr, nullptr, nullptr, &metadataRz );
    if ( std::isfinite( metadataRz ) )
      correctionRz = metadataRz;
  }
  const bool hasCorrectionRz = std::isfinite( correctionRz );

  const QVector<QVector3D> canonicalPoints = hasCorrectionRz
    ? unrotatePointsAroundZ( points, correctionRz )
    : points;

  QVector3D hardMin, hardMax;
  hardBBoxFromPoints( canonicalPoints, hardMin, hardMax );
  const double height = static_cast<double>( hardMax.z() - hardMin.z() );
  if ( height <= 1e-6 )
    return;

  QVector3D fpMin, fpMax;
  QString fpSource;
  if ( !footprintBBoxForGeometryCorrection( canonicalPoints, fpMin, fpMax, fpSource ) )
    return;
  const double fpLength = static_cast<double>( fpMax.x() - fpMin.x() );
  const double fpWidth = static_cast<double>( fpMax.y() - fpMin.y() );
  const double strength = std::max( 0.0, std::min( 1.0, m_geometryCorrectionStrength ) );

  if ( prim == QStringLiteral( "Cuboid" ) )
  {
    const double length = fpLength > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "length" ), fpLength, strength ) : uiParams.value( QStringLiteral( "length" ), fpLength );
    const double width = fpWidth > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "width" ), fpWidth, strength ) : uiParams.value( QStringLiteral( "width" ), fpWidth );
    const double correctedHeight = applyRegularizedGeometryValue( uiParams, QStringLiteral( "height" ), height, strength );
    DEBUG_LOG( QString( "[ParamCorrection] Cuboid source=%1 strength=%2 geo=(%3,%4,%5) corrected=(%6,%7,%8) rz=%9\n" )
                 .arg( fpSource )
                 .arg( strength, 0, 'f', 2 )
                 .arg( fpLength, 0, 'f', 3 )
                 .arg( fpWidth, 0, 'f', 3 )
                 .arg( height, 0, 'f', 3 )
                 .arg( length, 0, 'f', 3 )
                 .arg( width, 0, 'f', 3 )
                 .arg( correctedHeight, 0, 'f', 3 )
                 .arg( hasCorrectionRz ? QString::number( correctionRz, 'f', 2 ) : QStringLiteral( "<none>" ) )
                 .toStdWString().c_str() );
    return;
  }

  if ( prim == QStringLiteral( "Cylinder" ) )
  {
    QVector3D robustMin, robustMax;
    robustBBoxFromPoints( canonicalPoints, robustMin, robustMax );
    const double cx = ( static_cast<double>( robustMin.x() ) + static_cast<double>( robustMax.x() ) ) * 0.5;
    const double cy = ( static_cast<double>( robustMin.y() ) + static_cast<double>( robustMax.y() ) ) * 0.5;
    QVector<double> radii;
    radii.reserve( canonicalPoints.size() );
    for ( const QVector3D &p : canonicalPoints )
      radii.append( std::hypot( static_cast<double>( p.x() ) - cx,
                                static_cast<double>( p.y() ) - cy ) );
    std::sort( radii.begin(), radii.end() );
    const int idx = std::max( 0, std::min( radii.size() - 1, static_cast<int>( std::floor( radii.size() * 0.90 ) ) ) );
    const double radius = radii[idx];
    const double correctedRadius = radius > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "radius" ), radius, strength ) : uiParams.value( QStringLiteral( "radius" ), radius );
    const double correctedHeight = applyRegularizedGeometryValue( uiParams, QStringLiteral( "cylHeight" ), height, strength );
    DEBUG_LOG( QString( "[ParamCorrection] Cylinder strength=%1 geoRadius=%2 geoHeight=%3 correctedRadius=%4 correctedHeight=%5 n=%6\n" )
                 .arg( strength, 0, 'f', 2 )
                 .arg( radius, 0, 'f', 3 )
                 .arg( height, 0, 'f', 3 )
                 .arg( correctedRadius, 0, 'f', 3 )
                 .arg( correctedHeight, 0, 'f', 3 )
                 .arg( canonicalPoints.size() )
                 .toStdWString().c_str() );
    return;
  }

  if ( prim == QStringLiteral( "GabledRoof" ) )
  {
    const double length = fpLength > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "grLength" ), fpLength, strength ) : uiParams.value( QStringLiteral( "grLength" ), fpLength );
    const double width = fpWidth > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "grWidth" ), fpWidth, strength ) : uiParams.value( QStringLiteral( "grWidth" ), fpWidth );

    double wallRatio = 0.0;
    if ( estimateGabledWallRatioFromProfile( canonicalPoints, fpWidth, hardMin.z(), height, wallRatio ) )
    {
      const double wallHeight = applyRegularizedGeometryValue( uiParams, QStringLiteral( "grWallHeight" ), height * wallRatio, strength );
      const double roofHeight = applyRegularizedGeometryValue( uiParams, QStringLiteral( "grRoofHeight" ), height * ( 1.0 - wallRatio ), strength );
      DEBUG_LOG( QString( "[ParamCorrection] GabledRoof source=%1 strength=%2 geo=(%3,%4,%5) corrected=(%6,%7,%8,%9) wallRatio=%10 rz=%11\n" )
                   .arg( fpSource )
                   .arg( strength, 0, 'f', 2 )
                   .arg( fpLength, 0, 'f', 3 )
                   .arg( fpWidth, 0, 'f', 3 )
                   .arg( height, 0, 'f', 3 )
                   .arg( length, 0, 'f', 3 )
                   .arg( width, 0, 'f', 3 )
                   .arg( wallHeight, 0, 'f', 3 )
                   .arg( roofHeight, 0, 'f', 3 )
                   .arg( wallRatio, 0, 'f', 3 )
                   .arg( hasCorrectionRz ? QString::number( correctionRz, 'f', 2 ) : QStringLiteral( "<none>" ) )
                   .toStdWString().c_str() );
    }
    else
    {
      DEBUG_LOG( QString( "[ParamCorrection] GabledRoof source=%1 strength=%2 geo=(%3,%4,%5) corrected=(%6,%7) wallRatio=<kept PCT>\n" )
                   .arg( fpSource )
                   .arg( strength, 0, 'f', 2 )
                   .arg( fpLength, 0, 'f', 3 )
                   .arg( fpWidth, 0, 'f', 3 )
                   .arg( height, 0, 'f', 3 )
                   .arg( length, 0, 'f', 3 )
                   .arg( width, 0, 'f', 3 )
                   .toStdWString().c_str() );
    }
  }

  if ( prim == QStringLiteral( "HalfCylinderRoof" ) )
  {
    const double length = fpLength > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "hcrLength" ), fpLength, strength ) : uiParams.value( QStringLiteral( "hcrLength" ), fpLength );
    const double width = fpWidth > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "hcrWidth" ), fpWidth, strength ) : uiParams.value( QStringLiteral( "hcrWidth" ), fpWidth );
    const double roofRadius = std::max( 0.0, fpWidth * 0.5 );
    const double wallHeightGeo = std::max( 0.0, height - roofRadius );
    const double wallHeight = applyRegularizedGeometryValue( uiParams, QStringLiteral( "hcrWallHeight" ), wallHeightGeo, strength );
    DEBUG_LOG( QString( "[ParamCorrection] HalfCylinderRoof source=%1 strength=%2 geo=(length=%3,width=%4,totalHeight=%5,wallHeight=%6) corrected=(%7,%8,%9) rz=%10\n" )
                 .arg( fpSource )
                 .arg( strength, 0, 'f', 2 )
                 .arg( fpLength, 0, 'f', 3 )
                 .arg( fpWidth, 0, 'f', 3 )
                 .arg( height, 0, 'f', 3 )
                 .arg( wallHeightGeo, 0, 'f', 3 )
                 .arg( length, 0, 'f', 3 )
                 .arg( width, 0, 'f', 3 )
                 .arg( wallHeight, 0, 'f', 3 )
                 .arg( hasCorrectionRz ? QString::number( correctionRz, 'f', 2 ) : QStringLiteral( "<none>" ) )
                 .toStdWString().c_str() );
    return;
  }

  if ( prim == QStringLiteral( "TruncatedPyramidRoof" ) )
  {
    const double bottomLength = fpLength > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "tpBottomLength" ), fpLength, strength ) : uiParams.value( QStringLiteral( "tpBottomLength" ), fpLength );
    const double bottomWidth = fpWidth > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "tpBottomWidth" ), fpWidth, strength ) : uiParams.value( QStringLiteral( "tpBottomWidth" ), fpWidth );

    double topLengthGeo = qQNaN();
    double topWidthGeo = qQNaN();
    QVector3D topMin, topMax;
    const double topSliceBottom = static_cast<double>( hardMin.z() ) + std::max( 0.0, height - std::max( 0.3, height * 0.15 ) );
    if ( robustSliceBBox( canonicalPoints, [&]( const QVector3D &p ) {
           return static_cast<double>( p.z() ) >= topSliceBottom;
         }, topMin, topMax, std::max( 16, static_cast<int>( canonicalPoints.size() * 0.015 ) ) ) )
    {
      const double measuredTopLength = static_cast<double>( topMax.x() - topMin.x() );
      const double measuredTopWidth = static_cast<double>( topMax.y() - topMin.y() );
      if ( measuredTopLength > fpLength * 0.05 && measuredTopLength < fpLength * 1.05 )
        topLengthGeo = measuredTopLength;
      if ( measuredTopWidth > fpWidth * 0.05 && measuredTopWidth < fpWidth * 1.05 )
        topWidthGeo = measuredTopWidth;
    }
    const double topLength = std::isfinite( topLengthGeo )
                               ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "tpTopLength" ), topLengthGeo, strength )
                               : uiParams.value( QStringLiteral( "tpTopLength" ), qMax( 0.0, fpLength * 0.55 ) );
    const double topWidth = std::isfinite( topWidthGeo )
                              ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "tpTopWidth" ), topWidthGeo, strength )
                              : uiParams.value( QStringLiteral( "tpTopWidth" ), qMax( 0.0, fpWidth * 0.55 ) );

    double wallRatio = qQNaN();
    if ( !estimateShrinkWallRatio( canonicalPoints, fpLength, fpWidth, hardMin.z(), height, wallRatio ) )
    {
      const double pctWall = uiParams.value( QStringLiteral( "tpWallHeight" ), qQNaN() );
      const double pctRoof = uiParams.value( QStringLiteral( "tpRoofHeight" ), qQNaN() );
      if ( std::isfinite( pctWall ) && std::isfinite( pctRoof ) && pctWall + pctRoof > 1e-6 )
        wallRatio = std::max( 0.05, std::min( 0.95, pctWall / ( pctWall + pctRoof ) ) );
    }

    double wallHeight = uiParams.value( QStringLiteral( "tpWallHeight" ), qQNaN() );
    double roofHeight = uiParams.value( QStringLiteral( "tpRoofHeight" ), qQNaN() );
    if ( std::isfinite( wallRatio ) )
    {
      wallHeight = applyRegularizedGeometryValue( uiParams, QStringLiteral( "tpWallHeight" ), height * wallRatio, strength );
      roofHeight = applyRegularizedGeometryValue( uiParams, QStringLiteral( "tpRoofHeight" ), height * ( 1.0 - wallRatio ), strength );
    }
    DEBUG_LOG( QString( "[ParamCorrection] TruncatedPyramidRoof source=%1 strength=%2 geoBase=(%3,%4,%5) geoTop=(%6,%7) corrected=(%8,%9,%10,%11,%12,%13) wallRatio=%14 rz=%15\n" )
                 .arg( fpSource )
                 .arg( strength, 0, 'f', 2 )
                 .arg( fpLength, 0, 'f', 3 )
                 .arg( fpWidth, 0, 'f', 3 )
                 .arg( height, 0, 'f', 3 )
                 .arg( std::isfinite( topLengthGeo ) ? QString::number( topLengthGeo, 'f', 3 ) : QStringLiteral( "<kept PCT>" ) )
                 .arg( std::isfinite( topWidthGeo ) ? QString::number( topWidthGeo, 'f', 3 ) : QStringLiteral( "<kept PCT>" ) )
                 .arg( bottomLength, 0, 'f', 3 )
                 .arg( bottomWidth, 0, 'f', 3 )
                 .arg( topLength, 0, 'f', 3 )
                 .arg( topWidth, 0, 'f', 3 )
                 .arg( wallHeight, 0, 'f', 3 )
                 .arg( roofHeight, 0, 'f', 3 )
                 .arg( std::isfinite( wallRatio ) ? QString::number( wallRatio, 'f', 3 ) : QStringLiteral( "<kept PCT>" ) )
                 .arg( hasCorrectionRz ? QString::number( correctionRz, 'f', 2 ) : QStringLiteral( "<none>" ) )
                 .toStdWString().c_str() );
    return;
  }

  if ( prim == QStringLiteral( "LHouse" ) )
  {
    const double totalLength = fpLength > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "lTotalL" ), fpLength, strength ) : uiParams.value( QStringLiteral( "lTotalL" ), fpLength );
    const double totalWidth = fpWidth > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "lTotalW" ), fpWidth, strength ) : uiParams.value( QStringLiteral( "lTotalW" ), fpWidth );
    const double totalHeight = applyRegularizedGeometryValue( uiParams, QStringLiteral( "lHeight" ), height, strength );

    double cutoutLengthRatio = qQNaN();
    double wingWidthRatio = qQNaN();
    const bool hasCutout = estimateLHouseCutoutRatios( canonicalPoints, fpMin, fpMax, hardMin.z(), height,
                                                       cutoutLengthRatio, wingWidthRatio );
    double correctedWingRatio = uiParams.value( QStringLiteral( "lWingR" ), qQNaN() );
    double correctedWingWidthRatio = uiParams.value( QStringLiteral( "lWingWR" ), qQNaN() );
    if ( hasCutout )
    {
      correctedWingRatio = applyRegularizedGeometryValue( uiParams, QStringLiteral( "lWingR" ), cutoutLengthRatio, strength );
      correctedWingWidthRatio = applyRegularizedGeometryValue( uiParams, QStringLiteral( "lWingWR" ), wingWidthRatio, strength );
    }
    DEBUG_LOG( QString( "[ParamCorrection] LHouse source=%1 strength=%2 geoOuter=(%3,%4,%5) geoCutout=(%6,%7) corrected=(%8,%9,%10,%11,%12) rz=%13\n" )
                 .arg( fpSource )
                 .arg( strength, 0, 'f', 2 )
                 .arg( fpLength, 0, 'f', 3 )
                 .arg( fpWidth, 0, 'f', 3 )
                 .arg( height, 0, 'f', 3 )
                 .arg( hasCutout ? QString::number( cutoutLengthRatio, 'f', 3 ) : QStringLiteral( "<kept PCT>" ) )
                 .arg( hasCutout ? QString::number( wingWidthRatio, 'f', 3 ) : QStringLiteral( "<kept PCT>" ) )
                 .arg( totalLength, 0, 'f', 3 )
                 .arg( totalWidth, 0, 'f', 3 )
                 .arg( totalHeight, 0, 'f', 3 )
                 .arg( correctedWingRatio, 0, 'f', 3 )
                 .arg( correctedWingWidthRatio, 0, 'f', 3 )
                 .arg( hasCorrectionRz ? QString::number( correctionRz, 'f', 2 ) : QStringLiteral( "<none>" ) )
                 .toStdWString().c_str() );
    return;
  }

  if ( prim == QStringLiteral( "IndentedCuboid" ) )
  {
    const double outerLength = fpLength > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "icOuterL" ), fpLength, strength ) : uiParams.value( QStringLiteral( "icOuterL" ), fpLength );
    const double outerWidth = fpWidth > 1e-6 ? applyRegularizedGeometryValue( uiParams, QStringLiteral( "icOuterW" ), fpWidth, strength ) : uiParams.value( QStringLiteral( "icOuterW" ), fpWidth );
    const double outerHeight = applyRegularizedGeometryValue( uiParams, QStringLiteral( "icOuterH" ), height, strength );

    QVector3D innerMin, innerMax;
    double innerHeightGeo = qQNaN();
    const bool hasInner = estimateIndentedInnerBox( canonicalPoints, fpMin, fpMax, hardMin.z(), height,
                                                    innerMin, innerMax, innerHeightGeo );
    double innerLength = uiParams.value( QStringLiteral( "icInnerL" ), qQNaN() );
    double innerWidth = uiParams.value( QStringLiteral( "icInnerW" ), qQNaN() );
    double innerHeight = uiParams.value( QStringLiteral( "icInnerH" ), qQNaN() );
    double offsetX = uiParams.value( QStringLiteral( "icOffsetX" ), qQNaN() );
    double offsetY = uiParams.value( QStringLiteral( "icOffsetY" ), qQNaN() );
    if ( hasInner )
    {
      innerLength = applyRegularizedGeometryValue( uiParams, QStringLiteral( "icInnerL" ), static_cast<double>( innerMax.x() - innerMin.x() ), strength );
      innerWidth = applyRegularizedGeometryValue( uiParams, QStringLiteral( "icInnerW" ), static_cast<double>( innerMax.y() - innerMin.y() ), strength );
      innerHeight = applyRegularizedGeometryValue( uiParams, QStringLiteral( "icInnerH" ), innerHeightGeo, strength );
      offsetX = applyRegularizedGeometryValue( uiParams, QStringLiteral( "icOffsetX" ), static_cast<double>( innerMin.x() - fpMin.x() ), strength );
      offsetY = applyRegularizedGeometryValue( uiParams, QStringLiteral( "icOffsetY" ), static_cast<double>( innerMin.y() - fpMin.y() ), strength );
    }
    DEBUG_LOG( QString( "[ParamCorrection] IndentedCuboid source=%1 strength=%2 geoOuter=(%3,%4,%5) geoInner=%6 corrected=(%7,%8,%9,%10,%11,%12,%13,%14) rz=%15\n" )
                 .arg( fpSource )
                 .arg( strength, 0, 'f', 2 )
                 .arg( fpLength, 0, 'f', 3 )
                 .arg( fpWidth, 0, 'f', 3 )
                 .arg( height, 0, 'f', 3 )
                 .arg( hasInner ? QString( "(%1,%2,%3,%4,%5)" )
                                      .arg( innerMax.x() - innerMin.x(), 0, 'f', 3 )
                                      .arg( innerMax.y() - innerMin.y(), 0, 'f', 3 )
                                      .arg( innerHeightGeo, 0, 'f', 3 )
                                      .arg( innerMin.x() - fpMin.x(), 0, 'f', 3 )
                                      .arg( innerMin.y() - fpMin.y(), 0, 'f', 3 )
                                : QStringLiteral( "<kept PCT>" ) )
                 .arg( outerLength, 0, 'f', 3 )
                 .arg( outerWidth, 0, 'f', 3 )
                 .arg( outerHeight, 0, 'f', 3 )
                 .arg( innerLength, 0, 'f', 3 )
                 .arg( innerWidth, 0, 'f', 3 )
                 .arg( innerHeight, 0, 'f', 3 )
                 .arg( offsetX, 0, 'f', 3 )
                 .arg( offsetY, 0, 'f', 3 )
                 .arg( hasCorrectionRz ? QString::number( correctionRz, 'f', 2 ) : QStringLiteral( "<none>" ) )
                 .toStdWString().c_str() );
    return;
  }
}
