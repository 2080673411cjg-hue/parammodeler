/***************************************************************************
  parammodeler_inverse.cpp
  Point Cloud Parameter Inversion
  -------------------
         begin                : May 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "parammodeler_inverse.h"
#include "parammodeler_dock.h"
#include "ui_parammodeler_dock.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>
#include <QDir>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <random>
#include <windows.h>
#define DEBUG_LOG(msg) OutputDebugStringW(msg)

#include <qgspointcloudlayer.h>
#include <qgspointcloudindex.h>
#include <qgspointcloudblock.h>

#include <qgspointcloudrequest.h>
#include <qgspointcloudattribute.h>
#include <qgsvector3d.h>

// ====================================================================
// 公共入口
// ====================================================================
QMap<QString, double> ParamInverter::invert( const QString &primitiveType,
                                               const QString &filePath )
{
    QVector<QVector3D> pts = loadPoints( filePath );

    QMap<QString, double> result;
    if ( primitiveType == "Cuboid" )
        result = invertCuboid( pts );
    else if ( primitiveType == "Cylinder" )
        result = invertCylinder( pts );
    else if ( primitiveType == "LHouse" )
        result = invertLHouse( pts );
    else if ( primitiveType == "ConeCylinder" )
        result = invertConeCylinder( pts );
    else if ( primitiveType == "GabledRoof" )
        result = invertGabledRoof( pts );
    else if ( primitiveType == "PyramidRoof" )
        result = invertPyramidRoof( pts );
    else if ( primitiveType == "TruncatedPyramidRoof" )
        result = invertTruncatedPyramidRoof( pts );
    else if ( primitiveType == "HalfCylinderRoof" )
        result = invertHalfCylinderRoof( pts );
    else if ( primitiveType == "CylinderDome" ||
              primitiveType == "CylinderHemisphere" ||
              primitiveType == "穹顶圆柱" ||
              primitiveType == "圆柱穹顶" )
        result = invertCylinderHemisphere( pts );
    else if ( primitiveType == "IndentedCuboid" || primitiveType == "凹陷长方体" )
        result = invertIndentedCuboid( pts );
    else if ( primitiveType == "AsymmetricGableHouse" || primitiveType == "非对称人字形屋顶房屋" )
        result = invertAsymmetricGableHouse( pts );
    else if ( primitiveType == "FourStageRoundTower" || primitiveType == "四段式圆塔形" )
        result = invertFourStageRoundTower( pts );
    else if ( primitiveType == "TwoGableHouses" || primitiveType == "双人字屋顶房屋" )
        result = invertTwoGableHouses( pts );

    // ===== 调试输出 =====
    if ( !result.isEmpty() )
    {
        QString dbg = QString( "\n=== ParamInverter Result [%1] ===\n" ).arg( primitiveType );
        dbg += QString( "点云点数: %1\n" ).arg( pts.size() );

        // 按类别分组输出参数
        QStringList shapeKeys, poseKeys;
        for ( auto it = result.constBegin(); it != result.constEnd(); ++it )
        {
            if ( it.key() == "tx" || it.key() == "ty" || it.key() == "tz" )
                poseKeys << QString( "  %1 = %2" ).arg( it.key() ).arg( it.value(), 0, 'f', 4 );
            else
                shapeKeys << QString( "  %1 = %2" ).arg( it.key() ).arg( it.value(), 0, 'f', 4 );
        }
        if ( !shapeKeys.isEmpty() )
            dbg += "形状参数:\n" + shapeKeys.join( "\n" ) + "\n";
        if ( !poseKeys.isEmpty() )
            dbg += "位姿参数:\n" + poseKeys.join( "\n" ) + "\n";
        dbg += "========================================\n";

        DEBUG_LOG( dbg.toStdWString().c_str() );

        QFile logFile( QDir::tempPath() + "/parammodeler_inverse.log" );
        if ( logFile.open( QIODevice::Append | QIODevice::Text ) )
        {
            QTextStream ts( &logFile );
            ts << dbg;
            logFile.close();
        }
    }

    return result;
}

// ====================================================================
// 将反演结果写入 UI
// ====================================================================
void ParamInverter::applyToUI( ParamModelerDock *dock,
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

    auto setLE = [&]( const QString &key, QLineEdit *edit ) {
        if ( params.contains( key ) )
            edit->setText( QString::number( params[key], 'f', 2 ) );
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
    setLE( "tx", dock->ui->lineEditTX );
    setLE( "ty", dock->ui->lineEditTY );
    setLE( "tz", dock->ui->lineEditTZ );
}

// ====================================================================
// 加载点云（与 classify 共享逻辑）
// ====================================================================
QVector<QVector3D> ParamInverter::loadPoints( const QString &filePath )
{
    QVector<QVector3D> pts;
    QFileInfo fi( filePath );
    QString suffix = fi.suffix().toLower();

    if ( suffix == "ply" )
    {
        QFile file( filePath );
        if ( !file.open( QIODevice::ReadOnly ) ) return pts;

        // Parse header
        int vertexCount = 0, recordSize = 0;
        bool isAscii = false, isBigEndian = false;
        QMap<QString,int> propOffset, propSize;
        QMap<QString,bool> propIsDouble;
        int curOff = 0;

        while ( !file.atEnd() )
        {
            QByteArray lineBA;
            char c;
            while ( file.getChar( &c ) ) { if ( c == '\n' ) break; if ( c != '\r' ) lineBA.append( c ); }
            QString line = QString::fromLatin1( lineBA ).trimmed();

            if ( line.startsWith( "format" ) )
            {
                if ( line.contains( "ascii" ) )                isAscii = true;
                else if ( line.contains( "binary_little_endian" ) ) ;
                else if ( line.contains( "binary_big_endian" ) ) isBigEndian = true;
            }
            else if ( line.startsWith( "element vertex" ) )
                vertexCount = line.split( ' ', Qt::SkipEmptyParts ).last().toInt();
            else if ( line.startsWith( "property" ) )
            {
                QStringList tok = line.split( ' ', Qt::SkipEmptyParts );
                if ( tok.size() >= 3 )
                {
                    QString tname = tok[1], pname = tok[2];
                    int sz = 4; bool isDbl = false;
                    if      ( tname == "double" || tname == "float64" ) { sz = 8; isDbl = true; }
                    else if ( tname == "float"  || tname == "float32" ) { sz = 4; }
                    else if ( tname == "int"    || tname == "int32"   ||
                              tname == "uint"   || tname == "uint32"  ) { sz = 4; }
                    else if ( tname == "short" || tname == "int16"   ||
                              tname == "ushort"|| tname == "uint16"  ) { sz = 2; }
                    else if ( tname == "char"  || tname == "uchar"   ||
                              tname == "int8"  || tname == "uint8"   ) { sz = 1; }
                    propOffset[pname] = curOff;
                    propSize[pname] = sz;
                    propIsDouble[pname] = isDbl;
                    curOff += sz;
                }
            }
            else if ( line == "end_header" ) break;
        }
        recordSize = curOff;

        if ( vertexCount <= 0 || !propOffset.contains( "x" ) ||
              !propOffset.contains( "y" ) || !propOffset.contains( "z" ) )
        { file.close(); return pts; }

        int xOff = propOffset["x"], xSz = propSize["x"]; bool xDbl = propIsDouble["x"];
        int yOff = propOffset["y"], ySz = propSize["y"]; bool yDbl = propIsDouble["y"];
        int zOff = propOffset["z"], zSz = propSize["z"]; bool zDbl = propIsDouble["z"];

        auto readD = [&]( const QByteArray &rec, int off, int sz, bool isDbl, bool bigE ) {
            if ( isDbl ) { double v; memcpy( &v, rec.constData() + off, 8 );
                if ( bigE ) { char *b = reinterpret_cast<char*>(&v); std::reverse( b, b+8 ); } return v; }
            else { float v; memcpy( &v, rec.constData() + off, qMin(sz,4) );
                if ( bigE ) { char *b = reinterpret_cast<char*>(&v); std::reverse( b, b+4 ); } return static_cast<double>(v); }
        };

        pts.reserve( vertexCount );

        if ( isAscii )
        {
            QList<QPair<int,QString>> ol;
            for ( auto it = propOffset.begin(); it != propOffset.end(); ++it )
                ol.append( { it.value(), it.key() } );
            std::sort( ol.begin(), ol.end() );
            QStringList po; for ( auto &p : ol ) po << p.second;
            int xC = po.indexOf( "x" ), yC = po.indexOf( "y" ), zC = po.indexOf( "z" );
            int need = std::max( { xC, yC, zC } ) + 1;

            int cnt = 0;
            while ( !file.atEnd() && cnt < vertexCount )
            {
                QByteArray lb = file.readLine().trimmed();
                if ( lb.isEmpty() ) continue;
                QList<QByteArray> parts = lb.split( ' ' );
                parts.removeAll( QByteArray() );
                if ( parts.size() < need ) continue;
                pts.append( QVector3D( parts[xC].toDouble(), parts[yC].toDouble(), parts[zC].toDouble() ) );
                cnt++;
            }
        }
        else
        {
            for ( int i = 0; i < vertexCount; i++ )
            {
                QByteArray rec = file.read( recordSize );
                if ( rec.size() < recordSize ) break;
                pts.append( QVector3D( readD( rec, xOff, xSz, xDbl, isBigEndian ),
                                        readD( rec, yOff, ySz, yDbl, isBigEndian ),
                                        readD( rec, zOff, zSz, zDbl, isBigEndian ) ) );
            }
        }
        file.close();
    }
    else if ( suffix == "las" || suffix == "laz" )
    {
        QgsPointCloudLayer *tmpLayer = new QgsPointCloudLayer( filePath, "tmp_invert", "pdal" );
        if ( !tmpLayer || !tmpLayer->isValid() ) { delete tmpLayer; return pts; }
        QgsPointCloudIndex index = tmpLayer->dataProvider()->index();
        if ( !index.isValid() ) { delete tmpLayer; return pts; }

        QgsPointCloudAttributeCollection attrs;
        attrs.push_back( QgsPointCloudAttribute( "X", QgsPointCloudAttribute::Int32 ) );
        attrs.push_back( QgsPointCloudAttribute( "Y", QgsPointCloudAttribute::Int32 ) );
        attrs.push_back( QgsPointCloudAttribute( "Z", QgsPointCloudAttribute::Int32 ) );
        QgsPointCloudRequest request;
        request.setAttributes( attrs );

        QgsVector3D scale = index.scale(), offset = index.offset();
        QList<QgsPointCloudNodeId> queue;
        queue.append( index.root() );

        while ( !queue.isEmpty() )
        {
            QgsPointCloudNodeId nid = queue.takeFirst();
            QgsPointCloudNode node = index.getNode( nid );
            for ( const QgsPointCloudNodeId &c : node.children() ) queue.append( c );
            std::unique_ptr<QgsPointCloudBlock> block = index.nodeData( nid, request );
            if ( !block ) continue;
            const char *data = block->data();
            int pn = block->pointCount(), rs = block->pointRecordSize();
            for ( int i = 0; i < pn; i++ )
            {
                const char *ptr = data + i * rs;
                qint32 ix = *reinterpret_cast<const qint32*>( ptr );
                qint32 iy = *reinterpret_cast<const qint32*>( ptr + 4 );
                qint32 iz = *reinterpret_cast<const qint32*>( ptr + 8 );
                pts.append( QVector3D( ix * scale.x() + offset.x(),
                                        iy * scale.y() + offset.y(),
                                        iz * scale.z() + offset.z() ) );
            }
        }
        delete tmpLayer;
    }
    return pts;
}

// ====================================================================
// 工具函数
// ====================================================================
void ParamInverter::fitCircleRANSAC( const QVector<QVector3D> &pts,
                                       double &cx, double &cy, double &radius,
                                       int iterations, double inlierThresh )
{
    cx = cy = radius = 0;
    int n = pts.size();
    if ( n < 3 ) return;

    std::mt19937 rng( 42 );
    std::uniform_int_distribution<int> dist( 0, n - 1 );

    int bestInliers = 0;

    for ( int iter = 0; iter < iterations; iter++ )
    {
        int i1 = dist( rng ), i2, i3;
        do { i2 = dist( rng ); } while ( i2 == i1 );
        do { i3 = dist( rng ); } while ( i3 == i1 || i3 == i2 );

        double x1 = pts[i1].x(), y1 = pts[i1].y();
        double x2 = pts[i2].x(), y2 = pts[i2].y();
        double x3 = pts[i3].x(), y3 = pts[i3].y();

        double ma = x2 - x1, mb = y2 - y1;
        double mc = x3 - x1, md = y3 - y1;
        double denom = 2 * ( ma * md - mb * mc );
        if ( qAbs( denom ) < 1e-10 ) continue;

        double ma2 = ma * ma + mb * mb;
        double mc2 = mc * mc + md * md;
        double cxi = x1 + ( md * ma2 - mb * mc2 ) / denom;
        double cyi = y1 + ( ma * mc2 - mc * ma2 ) / denom;
        double ri = std::sqrt( ( x1 - cxi ) * ( x1 - cxi ) + ( y1 - cyi ) * ( y1 - cyi ) );

        if ( ri < 0.01 ) continue;

        int inliers = 0;
        for ( const QVector3D &p : pts )
        {
            double d = std::sqrt( ( p.x() - cxi ) * ( p.x() - cxi ) +
                                   ( p.y() - cyi ) * ( p.y() - cyi ) );
            if ( qAbs( d - ri ) < inlierThresh ) inliers++;
        }

        if ( inliers > bestInliers )
        {
            bestInliers = inliers;
            cx = cxi; cy = cyi; radius = ri;
        }
    }
}

void ParamInverter::fitLineRANSAC( const QVector<QVector3D> &pts,
                                     QVector3D &origin, QVector3D &direction,
                                     int iterations, double inlierThresh )
{
    origin = QVector3D( 0, 0, 0 );
    direction = QVector3D( 1, 0, 0 );
    int n = pts.size();
    if ( n < 2 ) return;

    std::mt19937 rng( 42 );
    std::uniform_int_distribution<int> dist( 0, n - 1 );

    int bestInliers = 0;
    for ( int iter = 0; iter < iterations; iter++ )
    {
        int i1 = dist( rng ), i2;
        do { i2 = dist( rng ); } while ( i2 == i1 );

        QVector3D dir = ( pts[i2] - pts[i1] ).normalized();
        if ( dir.length() < 0.001 ) continue;

        int inliers = 0;
        for ( const QVector3D &p : pts )
        {
            QVector3D v = p - pts[i1];
            double projLen = QVector3D::dotProduct( v, dir );
            QVector3D proj = pts[i1] + dir * projLen;
            if ( ( p - proj ).length() < inlierThresh ) inliers++;
        }

        if ( inliers > bestInliers )
        {
            bestInliers = inliers;
            origin = pts[i1];
            direction = dir;
        }
    }
}

int ParamInverter::findHeightSplit( const QVector<double> &zValues, double wallRatio )
{
    int n = zValues.size();
    if ( n < 10 ) return n / 2;

    QVector<double> zs = zValues;
    std::sort( zs.begin(), zs.end() );

    // 基于高度直方图找最大值/最小值之间的分割点
    double zMin = zs.first(), zMax = zs.last();
    double zRange = zMax - zMin;
    if ( zRange < 0.01 ) return n / 2;

    const int BINS = 30;
    int hist[BINS] = {};
    for ( double z : zs )
    {
        int b = static_cast<int>( ( z - zMin ) / zRange * BINS );
        if ( b >= BINS ) b = BINS - 1;
        if ( b < 0 ) b = 0;
        hist[b]++;
    }

    // 找直方图的谷底（在总高度的 30%~70% 范围内）
    int bestBin = static_cast<int>( BINS * 0.4 );
    int minVal = hist[bestBin];
    for ( int i = static_cast<int>( BINS * 0.2 ); i < static_cast<int>( BINS * 0.8 ); i++ )
    {
        if ( hist[i] < minVal ) { minVal = hist[i]; bestBin = i; }
    }

    double splitZ = zMin + zRange * ( bestBin + 0.5 ) / BINS;

    // 返回对应索引
    int idx = 0;
    for ( int i = 0; i < n; i++ )
    {
        if ( zs[i] >= splitZ ) { idx = i; break; }
    }
    return idx;
}

void ParamInverter::computeFootprintBounds( const QVector<QVector3D> &pts,
                                               double &minX, double &maxX,
                                               double &minY, double &maxY )
{
    if ( pts.isEmpty() ) { minX = maxX = minY = maxY = 0; return; }
    minX = maxX = pts[0].x();
    minY = maxY = pts[0].y();
    for ( const QVector3D &p : pts )
    {
        if ( p.x() < minX ) minX = p.x();
        if ( p.x() > maxX ) maxX = p.x();
        if ( p.y() < minY ) minY = p.y();
        if ( p.y() > maxY ) maxY = p.y();
    }
}

// ====================================================================
// SA 目标函数：计算点云到参数化模型表面的 RMSE
// ====================================================================
double ParamInverter::computeFitError( const QVector<QVector3D> &pts,
                                        const QString &type,
                                        const QMap<QString, double> &params )
{
    double sumSq = 0;
    int cnt = 0;

    if ( type == "Cuboid" )
    {
        double x0 = params.value( "tx", 0 ), y0 = params.value( "ty", 0 ), z0 = params.value( "tz", 0 );
        double L = params.value( "length", 1 ), W = params.value( "width", 1 ), H = params.value( "height", 1 );
        for ( const QVector3D &v : pts )
        {
            double dx = qMax( 0.0, qMax( x0 - v.x(), v.x() - ( x0 + L ) ) );
            double dy = qMax( 0.0, qMax( y0 - v.y(), v.y() - ( y0 + W ) ) );
            double dz = qMax( 0.0, qMax( z0 - v.z(), v.z() - ( z0 + H ) ) );
            sumSq += dx*dx + dy*dy + dz*dz;
            cnt++;
        }
    }
    else if ( type == "Cylinder" )
    {
        double cx = params.value( "tx", 0 ) + params.value( "radius", 1 );
        double cy = params.value( "ty", 0 ) + params.value( "radius", 1 );
        double z0 = params.value( "tz", 0 );
        double H = params.value( "cylHeight", 1 ), R = params.value( "radius", 1 );
        for ( const QVector3D &v : pts )
        {
            double r = std::sqrt( ( v.x() - cx ) * ( v.x() - cx ) + ( v.y() - cy ) * ( v.y() - cy ) );
            bool inHeight = ( v.z() >= z0 && v.z() <= z0 + H );
            bool inRadius = ( r <= R );

            double dist;
            if ( inHeight && inRadius )
            {
                // 点在圆柱内部：到最近表面的距离
                double dSide = R - r;
                double dTop  = z0 + H - v.z();
                double dBot  = v.z() - z0;
                dist = qMin( dSide, qMin( dTop, dBot ) );
            }
            else if ( inHeight && !inRadius )
            {
                dist = r - R;
            }
            else if ( !inHeight && inRadius )
            {
                double dTop = qMax( 0.0, v.z() - ( z0 + H ) );
                double dBot = qMax( 0.0, z0 - v.z() );
                dist = dTop + dBot;
            }
            else
            {
                double dSide = r - R;
                double dTop  = qMax( 0.0, v.z() - ( z0 + H ) );
                double dBot  = qMax( 0.0, z0 - v.z() );
                dist = std::sqrt( dSide * dSide + ( dTop + dBot ) * ( dTop + dBot ) );
            }
            sumSq += dist * dist;
            cnt++;
        }
    }
    else if ( type == "LHouse" )
    {
        double x0 = params.value( "tx", 0 ), y0 = params.value( "ty", 0 ), z0 = params.value( "tz", 0 );
        double mL = params.value( "lMainL", 1 ), mW = params.value( "lMainW", 1 );
        double wL = params.value( "lWingL", 1 ), wW = params.value( "lWingW", 1 );
        double H = params.value( "lHeight", 1 );
        // L 形 = 主体矩形 (x0, y0)-(x0+mL, y0+mW) 并上 翼部矩形 (x0+mL, y0)-(x0+mL+wL, y0+wW)
        for ( const QVector3D &v : pts )
        {
            // 点到 L 形包围的距离
            double dz = qMax( 0.0, qMax( z0 - v.z(), v.z() - ( z0 + H ) ) );
            // 主体区域距离
            double dx1 = qMax( 0.0, qMax( x0 - v.x(), v.x() - ( x0 + mL ) ) );
            double dy1 = qMax( 0.0, qMax( y0 - v.y(), v.y() - ( y0 + mW ) ) );
            double d1 = std::sqrt( dx1*dx1 + dy1*dy1 + dz*dz );
            // 翼部区域距离
            double dx2 = qMax( 0.0, qMax( x0 + mL - v.x(), v.x() - ( x0 + mL + wL ) ) );
            double dy2 = qMax( 0.0, qMax( y0 - v.y(), v.y() - ( y0 + wW ) ) );
            double d2 = std::sqrt( dx2*dx2 + dy2*dy2 + dz*dz );
            double dist = qMin( d1, d2 );
            sumSq += dist * dist;
            cnt++;
        }
    }
    else if ( type == "ConeCylinder" )
    {
        double cx = params.value( "tx", 0 ) + params.value( "ccRadius", 1 );
        double cy = params.value( "ty", 0 ) + params.value( "ccRadius", 1 );
        double z0 = params.value( "tz", 0 );
        double R = params.value( "ccRadius", 1 );
        double cylH = params.value( "ccCylHeight", 1 );
        double coneH = params.value( "ccConeHeight", 1 );
        double splitZ = z0 + cylH;
        double topZ = splitZ + coneH;
        for ( const QVector3D &v : pts )
        {
            double r = std::sqrt( ( v.x() - cx ) * ( v.x() - cx ) + ( v.y() - cy ) * ( v.y() - cy ) );
            double dist;
            if ( v.z() <= splitZ )
            {
                // 圆柱部分
                double dLat = qAbs( r - R );
                double dBot = qMax( 0.0, z0 - v.z() );
                dist = std::sqrt( dLat * dLat + dBot * dBot );
            }
            else
            {
                // 圆锥部分：理想半径线性收缩到 0
                double t = ( v.z() - splitZ ) / coneH;
                double idealR = R * ( 1.0 - t );
                dist = qAbs( r - idealR );
            }
            sumSq += dist * dist;
            cnt++;
        }
    }
    else if ( type == "GabledRoof" || type == "PyramidRoof" || type == "AsymmetricGableHouse" )
    {
        // 通用棱柱+屋顶模型：底部为矩形棱柱，上部为斜面
        QString pfx;
        if ( type == "GabledRoof" ) pfx = "gr";
        else if ( type == "PyramidRoof" ) pfx = "pr";
        else pfx = "agh";

        double x0 = params.value( "tx", 0 ), y0 = params.value( "ty", 0 ), z0 = params.value( "tz", 0 );
        double L = params.value( pfx + "Length", params.value( pfx + "L", 1 ) );
        double W = params.value( pfx + "Width", params.value( pfx + "W", 1 ) );
        double wallH = params.value( pfx + "WallHeight", 1 );
        double roofH = params.value( pfx + "RoofHeight", 1 );
        double wallZ = z0 + wallH;
        double topZ = wallZ + roofH;

        for ( const QVector3D &v : pts )
        {
            double dist;
            if ( v.z() <= wallZ )
            {
                // 墙体部分：到矩形棱柱表面距离
                double dx = qMax( 0.0, qMax( x0 - v.x(), v.x() - ( x0 + L ) ) );
                double dy = qMax( 0.0, qMax( y0 - v.y(), v.y() - ( y0 + W ) ) );
                double dz = qMax( 0.0, z0 - v.z() );
                dist = std::sqrt( dx*dx + dy*dy + dz*dz );
            }
            else
            {
                // 屋顶部分：点到屋顶面的近似距离
                double cx = x0 + L * 0.5;
                double relX = ( v.x() - cx ) / ( L * 0.5 + 0.001 );
                double idealZ = wallZ + roofH * ( 1.0 - qAbs( relX ) );
                dist = qMax( 0.0, v.z() - idealZ );
            }
            sumSq += dist * dist;
            cnt++;
        }
    }
    else if ( type == "TruncatedPyramidRoof" )
    {
        double x0 = params.value( "tx", 0 ), y0 = params.value( "ty", 0 ), z0 = params.value( "tz", 0 );
        double bL = params.value( "tpBottomLength", 1 ), bW = params.value( "tpBottomWidth", 1 );
        double tL = params.value( "tpTopLength", 0.5 ), tW = params.value( "tpTopWidth", 0.5 );
        double wallH = params.value( "tpWallHeight", 1 );
        double roofH = params.value( "tpRoofHeight", 1 );
        double wallZ = z0 + wallH;

        for ( const QVector3D &v : pts )
        {
            double dist;
            if ( v.z() <= wallZ )
            {
                double dx = qMax( 0.0, qMax( x0 - v.x(), v.x() - ( x0 + bL ) ) );
                double dy = qMax( 0.0, qMax( y0 - v.y(), v.y() - ( y0 + bW ) ) );
                double dz = qMax( 0.0, z0 - v.z() );
                dist = std::sqrt( dx*dx + dy*dy + dz*dz );
            }
            else
            {
                double t = ( v.z() - wallZ ) / roofH;
                double iL = bL + ( tL - bL ) * t;
                double iW = bW + ( tW - bW ) * t;
                double cx = x0 + bL * 0.5, cy = y0 + bW * 0.5;
                double dx = qMax( 0.0, qAbs( v.x() - cx ) - iL * 0.5 );
                double dy = qMax( 0.0, qAbs( v.y() - cy ) - iW * 0.5 );
                dist = std::sqrt( dx*dx + dy*dy );
            }
            sumSq += dist * dist;
            cnt++;
        }
    }
    else if ( type == "HalfCylinderRoof" )
    {
        double x0 = params.value( "tx", 0 ), y0 = params.value( "ty", 0 ), z0 = params.value( "tz", 0 );
        double L = params.value( "hcrLength", 1 ), W = params.value( "hcrWidth", 1 );
        double wallH = params.value( "hcrWallHeight", 1 );
        double R = params.value( "hcrRadius", 1 );
        double wallZ = z0 + wallH;

        for ( const QVector3D &v : pts )
        {
            double dist;
            if ( v.z() <= wallZ )
            {
                double dx = qMax( 0.0, qMax( x0 - v.x(), v.x() - ( x0 + L ) ) );
                double dy = qMax( 0.0, qMax( y0 - v.y(), v.y() - ( y0 + W ) ) );
                double dz = qMax( 0.0, z0 - v.z() );
                dist = std::sqrt( dx*dx + dy*dy + dz*dz );
            }
            else
            {
                double cy = y0 + W * 0.5;
                double dy = v.y() - cy;
                double idealZ = wallZ + std::sqrt( qMax( 0.0, R*R - dy*dy ) );
                dist = qAbs( v.z() - idealZ );
            }
            sumSq += dist * dist;
            cnt++;
        }
    }
    else if ( type == "CylinderDome" || type == "CylinderHemisphere" )
    {
        double cx = params.value( "tx", 0 ) + params.value( "chRadius", 1 );
        double cy = params.value( "ty", 0 ) + params.value( "chRadius", 1 );
        double z0 = params.value( "tz", 0 );
        double R = params.value( "chRadius", 1 );
        double cylH = params.value( "chCylHeight", 1 );
        double domeH = params.value( "chDomeHeight", 1 );
        double splitZ = z0 + cylH;

        for ( const QVector3D &v : pts )
        {
            double r = std::sqrt( ( v.x() - cx ) * ( v.x() - cx ) + ( v.y() - cy ) * ( v.y() - cy ) );
            double dist;
            if ( v.z() <= splitZ )
            {
                double dLat = qAbs( r - R );
                double dBot = qMax( 0.0, z0 - v.z() );
                dist = std::sqrt( dLat*dLat + dBot*dBot );
            }
            else
            {
                // 穹顶近似为半球
                double t = ( v.z() - splitZ ) / domeH;
                double idealR = R * std::sqrt( qMax( 0.0, 1.0 - t * t ) );
                dist = qAbs( r - idealR );
            }
            sumSq += dist * dist;
            cnt++;
        }
    }
    else if ( type == "IndentedCuboid" )
    {
        double x0 = params.value( "tx", 0 ), y0 = params.value( "ty", 0 ), z0 = params.value( "tz", 0 );
        double oL = params.value( "icOuterL", 1 ), oW = params.value( "icOuterW", 1 ), oH = params.value( "icOuterH", 1 );
        double iL = params.value( "icInnerL", 0.5 ), iW = params.value( "icInnerW", 0.5 ), iH = params.value( "icInnerH", 0.5 );
        double ox = params.value( "icOffsetX", 0.3 ), oy = params.value( "icOffsetY", 0.3 );

        for ( const QVector3D &v : pts )
        {
            double dz = qMax( 0.0, qMax( z0 - v.z(), v.z() - ( z0 + oH ) ) );
            // 外框距离
            double dx1 = qMax( 0.0, qMax( x0 - v.x(), v.x() - ( x0 + oL ) ) );
            double dy1 = qMax( 0.0, qMax( y0 - v.y(), v.y() - ( y0 + oW ) ) );
            double d1 = std::sqrt( dx1*dx1 + dy1*dy1 + dz*dz );
            // 内凹距离（负距离表示在凹陷内部）
            double ix0 = x0 + ox, iy0 = y0 + oy;
            bool inIndent = ( v.x() >= ix0 && v.x() <= ix0 + iL && v.y() >= iy0 && v.y() <= iy0 + iW && v.z() >= z0 && v.z() <= z0 + iH );
            double dist;
            if ( inIndent )
            {
                // 在凹陷内：距离为到凹陷边界的最小距离
                double dix = qMin( v.x() - ix0, ix0 + iL - v.x() );
                double diy = qMin( v.y() - iy0, iy0 + iW - v.y() );
                dist = -qMin( dix, diy ); // 负值表示在内部
            }
            else
            {
                dist = d1;
            }
            sumSq += dist * dist;
            cnt++;
        }
    }
    else if ( type == "FourStageRoundTower" )
    {
        double cx = params.value( "tx", 0 ) + params.value( "ftBaseR", 1 );
        double cy = params.value( "ty", 0 ) + params.value( "ftBaseR", 1 );
        double z0 = params.value( "tz", 0 );
        double baseR = params.value( "ftBaseR", 1 );
        double baseH = params.value( "ftBaseH", 1 );
        double midH = params.value( "ftMidH", 1 );
        double midTopR = params.value( "ftMidTopR", 0.7 );
        double coneH = params.value( "ftConeH", 1 );
        double z1 = z0 + baseH;
        double z2 = z1 + midH;
        double z3 = z2 + coneH;

        for ( const QVector3D &v : pts )
        {
            double r = std::sqrt( ( v.x() - cx ) * ( v.x() - cx ) + ( v.y() - cy ) * ( v.y() - cy ) );
            double idealR;
            if ( v.z() <= z1 ) idealR = baseR;
            else if ( v.z() <= z2 ) { double t = ( v.z() - z1 ) / midH; idealR = baseR + ( midTopR - baseR ) * t; }
            else if ( v.z() <= z3 ) { double t = ( v.z() - z2 ) / coneH; idealR = midTopR * ( 1.0 - t ); }
            else idealR = 0;
            double dist = qAbs( r - idealR );
            sumSq += dist * dist;
            cnt++;
        }
    }
    else if ( type == "TwoGableHouses" )
    {
        double x0 = params.value( "tx", 0 ), y0 = params.value( "ty", 0 ), z0 = params.value( "tz", 0 );
        double L1 = params.value( "tgLength1", 1 ), L2 = params.value( "tgLength2", 1 );
        double W = params.value( "tgWidth", 1 );
        double wallH = params.value( "tgWallHeight", 1 );
        double roofH = params.value( "tgRoofHeight", 1 );
        double wallZ = z0 + wallH;

        for ( const QVector3D &v : pts )
        {
            double dist;
            if ( v.z() <= wallZ )
            {
                double dx = qMax( 0.0, qMax( x0 - v.x(), v.x() - ( x0 + L1 + L2 ) ) );
                double dy = qMax( 0.0, qMax( y0 - v.y(), v.y() - ( y0 + W ) ) );
                double dz = qMax( 0.0, z0 - v.z() );
                dist = std::sqrt( dx*dx + dy*dy + dz*dz );
            }
            else
            {
                double cx = x0 + ( L1 + L2 ) * 0.5;
                double relX = ( v.x() - cx ) / ( ( L1 + L2 ) * 0.5 + 0.001 );
                double idealZ = wallZ + roofH * ( 1.0 - qAbs( relX ) );
                dist = qMax( 0.0, v.z() - idealZ );
            }
            sumSq += dist * dist;
            cnt++;
        }
    }

    return ( cnt > 0 ) ? std::sqrt( sumSq / cnt ) : 1e9;
}

// ====================================================================
// SA 扰动策略：随机扰动一个形状参数，幅度随温度缩放
// ====================================================================
void ParamInverter::perturbParams( QMap<QString, double> &params,
                                    const QString &type,
                                    double T, std::mt19937 &rng )
{
    // 收集当前类型可扰动的形状参数键（排除位姿参数）
    QStringList keys;
    if ( type == "Cuboid" )
        keys << "length" << "width" << "height";
    else if ( type == "Cylinder" )
        keys << "radius" << "cylHeight";
    else if ( type == "LHouse" )
        keys << "lMainL" << "lMainW" << "lWingL" << "lWingW" << "lHeight";
    else if ( type == "ConeCylinder" )
        keys << "ccRadius" << "ccCylHeight" << "ccConeHeight";
    else if ( type == "GabledRoof" )
        keys << "grLength" << "grWidth" << "grWallHeight" << "grRoofHeight";
    else if ( type == "PyramidRoof" )
        keys << "prLength" << "prWidth" << "prWallHeight" << "prRoofHeight";
    else if ( type == "TruncatedPyramidRoof" )
        keys << "tpBottomLength" << "tpBottomWidth" << "tpTopLength" << "tpTopWidth" << "tpWallHeight" << "tpRoofHeight";
    else if ( type == "HalfCylinderRoof" )
        keys << "hcrLength" << "hcrWidth" << "hcrWallHeight" << "hcrRadius";
    else if ( type == "CylinderDome" || type == "CylinderHemisphere" )
        keys << "chRadius" << "chCylHeight" << "chDomeHeight" << "chBulge";
    else if ( type == "IndentedCuboid" )
        keys << "icOuterL" << "icOuterW" << "icOuterH" << "icInnerL" << "icInnerW" << "icInnerH" << "icOffsetX" << "icOffsetY";
    else if ( type == "AsymmetricGableHouse" )
        keys << "aghLength" << "aghWidth" << "aghWallHeight" << "aghRoofHeight" << "aghRidgeLen" << "aghRidgeRatio";
    else if ( type == "FourStageRoundTower" )
        keys << "ftBaseR" << "ftBaseH" << "ftMidH" << "ftMidTopR" << "ftMidBulge" << "ftConeH";
    else if ( type == "TwoGableHouses" )
        keys << "tgLength1" << "tgLength2" << "tgWidth" << "tgWallHeight" << "tgRoofHeight" << "tgAngle";

    if ( keys.isEmpty() ) return;

    // 随机选一个参数扰动
    std::uniform_int_distribution<int> pick( 0, keys.size() - 1 );
    QString key = keys[pick( rng )];

    double val = params.value( key, 1.0 );
    // 扰动幅度与温度和参数量级成正比
    double sigma = qAbs( val ) * 0.1 * T + 0.01;
    std::normal_distribution<double> noise( 0.0, sigma );

    double newVal = val + noise( rng );

    // 强制正值约束
    if ( newVal < 0.01 ) newVal = 0.01;
    params[key] = newVal;
}

// ====================================================================
// SA 精化主循环：在初解基础上搜索更优参数组合
// ====================================================================
QMap<QString, double> ParamInverter::refineWithSA( const QVector<QVector3D> &pts,
                                                    const QString &primitiveType,
                                                    const QMap<QString, double> &initParams,
                                                    int maxIter,
                                                    double initTemp,
                                                    double cooling )
{
    if ( pts.size() < 10 || initParams.isEmpty() ) return initParams;

    QMap<QString, double> current = initParams;
    double currentCost = computeFitError( pts, primitiveType, current );

    QMap<QString, double> best = current;
    double bestCost = currentCost;
    double initCost = currentCost;

    std::mt19937 rng( 42 );
    std::uniform_real_distribution<double> uni( 0.0, 1.0 );

    double T = initTemp;
    for ( int iter = 0; iter < maxIter; iter++ )
    {
        QMap<QString, double> candidate = current;
        perturbParams( candidate, primitiveType, T, rng );

        double candidateCost = computeFitError( pts, primitiveType, candidate );
        double delta = candidateCost - currentCost;

        // Metropolis 准则
        if ( delta < 0 || uni( rng ) < std::exp( -delta / T ) )
        {
            current = candidate;
            currentCost = candidateCost;
            if ( currentCost < bestCost )
            {
                best = current;
                bestCost = currentCost;
            }
        }
        T *= cooling;
    }

    // ===== 调试输出：SA 精化效果 =====
    double finalCost = computeFitError( pts, primitiveType, best );
    QString dbg = QString( "\n=== SA [%1] 初始RMSE=%2 → 最终RMSE=%3 ===\n" )
                    .arg( primitiveType )
                    .arg( initCost, 0, 'f', 4 )
                    .arg( finalCost, 0, 'f', 4 );
    // 输出精化前后关键参数的变化
    for ( auto it = initParams.constBegin(); it != initParams.constEnd(); ++it )
    {
        if ( it.key() == "tx" || it.key() == "ty" || it.key() == "tz" ) continue;
        double before = it.value();
        double after  = best.value( it.key(), before );
        dbg += QString( "  %1: %2 → %3\n" ).arg( it.key() ).arg( before, 0, 'f', 4 ).arg( after, 0, 'f', 4 );
    }
    DEBUG_LOG( dbg.toStdWString().c_str() );

    // SA 未明显改善时返回初解
    if ( finalCost >= initCost * 0.99 )
    {
        DEBUG_LOG( L"SA 未收敛，返回初解\n" );
        return initParams;
    }

    return best;
}

// ====================================================================
// Cuboid: 包围盒直接获取
// ====================================================================
QMap<QString, double> ParamInverter::invertCuboid( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 4 ) return p;

    double minX, maxX, minY, maxY;
    computeFootprintBounds( pts, minX, maxX, minY, maxY );

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );

    p["length"]  = maxX - minX;
    p["width"]  = maxY - minY;
    p["height"] = zs.last() - zs.first();
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zs.first();
    return refineWithSA( pts, "Cuboid", p );
}

// ====================================================================
// Cylinder: RANSAC 拟合圆 + 高度范围
// ====================================================================
QMap<QString, double> ParamInverter::invertCylinder( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );

    // 用 1%~99% 分位数替代 zMin/zMax，排除顶底面噪声点
    double zMin = zs[ zs.size() * 1 / 100 ];
    double zMax = zs[ zs.size() * 99 / 100 ];
    double H = zMax - zMin;

    // 用中间 40%~80% 高度的点拟合圆（纯侧面，排除顶底面干扰）
    double zLo = zMin + H * 0.40;
    double zHi = zMin + H * 0.80;
    QVector<QVector3D> sidePts;
    for ( const QVector3D &v : pts )
        if ( v.z() >= zLo && v.z() <= zHi )
            sidePts.append( v );
    if ( sidePts.size() < 10 ) sidePts = pts;

    double cx, cy, radius;
    fitCircleRANSAC( sidePts, cx, cy, radius );

    p["radius"]    = radius;
    p["cylHeight"] = H;
    p["tx"] = cx - radius;
    p["ty"] = cy - radius;
    p["tz"] = zMin;
    return refineWithSA( pts, "Cylinder", p );
}

// ====================================================================
// LHouse: 基于足迹密度分布检测 L 形
// ====================================================================
QMap<QString, double> ParamInverter::invertLHouse( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    double minX, maxX, minY, maxY;
    computeFootprintBounds( pts, minX, maxX, minY, maxY );
    double W = maxX - minX, D = maxY - minY;
    if ( W < 0.01 || D < 0.01 ) return p;

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();

    // 用底部 30% 的点分析足迹密度，找 L 形两个矩形的边界
    double zCut = zMin + ( zMax - zMin ) * 0.3;
    const int G = 40;
    int grid[G][G] = {};
    for ( const QVector3D &v : pts )
    {
        if ( v.z() > zCut ) continue;
        int ix = qBound( 0, static_cast<int>( ( v.x() - minX ) / W * G ), G - 1 );
        int iy = qBound( 0, static_cast<int>( ( v.y() - minY ) / D * G ), G - 1 );
        grid[ix][iy]++;
    }

    // 通过行列密度找 L 形分割点
    // 行密度（X 方向每列点数）
    QVector<int> colCount( G, 0 );
    for ( int i = 0; i < G; i++ )
        for ( int j = 0; j < G; j++ )
            colCount[i] += ( grid[i][j] > 0 ? 1 : 0 );
    // 找列密度下降最陡的位置
    int splitX = G / 2;
    int maxDrop = 0;
    for ( int i = 1; i < G - 1; i++ )
    {
        int drop = colCount[i-1] - colCount[i];
        if ( drop > maxDrop ) { maxDrop = drop; splitX = i; }
    }

    // 行密度（Y 方向每行点数）
    QVector<int> rowCount( G, 0 );
    for ( int j = 0; j < G; j++ )
        for ( int i = 0; i < G; i++ )
            rowCount[j] += ( grid[i][j] > 0 ? 1 : 0 );
    int splitY = G / 2;
    maxDrop = 0;
    for ( int j = 1; j < G - 1; j++ )
    {
        int drop = rowCount[j-1] - rowCount[j];
        if ( drop > maxDrop ) { maxDrop = drop; splitY = j; }
    }

    double sX = minX + W * splitX / G;
    double sY = minY + D * splitY / G;

    p["lMainL"]   = sX - minX;
    p["lMainW"]   = maxY - minY;
    p["lWingL"]   = maxX - sX;
    p["lWingW"]   = sY - minY;
    p["lHeight"]  = zMax - zMin;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return refineWithSA( pts, "LHouse", p );
}

// ====================================================================
// ConeCylinder: 底部圆柱 + 顶部圆锥
// ====================================================================
QMap<QString, double> ParamInverter::invertConeCylinder( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();

    // 找圆柱→圆锥分界点：截面半径开始显著减小处
    double cx = 0, cy = 0;
    for ( const QVector3D &v : pts ) { cx += v.x(); cy += v.y(); }
    cx /= pts.size(); cy /= pts.size();

    const int SLICES = 20;
    QVector<QPair<double,double>> sliceRadii; // (z, avgRadius)
    for ( int s = 0; s < SLICES; s++ )
    {
        double zLo = zMin + ( zMax - zMin ) * s / SLICES;
        double zHi = zMin + ( zMax - zMin ) * ( s + 1 ) / SLICES;
        double sumR = 0; int cnt = 0;
        for ( const QVector3D &v : pts )
        {
            if ( v.z() >= zLo && v.z() < zHi )
            { sumR += std::sqrt( ( v.x() - cx ) * ( v.x() - cx ) + ( v.y() - cy ) * ( v.y() - cy ) ); cnt++; }
        }
        if ( cnt > 2 )
            sliceRadii.append( { ( zLo + zHi ) * 0.5, sumR / cnt } );
    }

    // 找半径开始线性减小的转折点
    int splitIdx = sliceRadii.size() * 2 / 3;
    for ( int i = 1; i < sliceRadii.size() - 1; i++ )
    {
        if ( sliceRadii[i].second < sliceRadii[i-1].second * 0.85 )
        { splitIdx = i; break; }
    }
    double splitZ = ( splitIdx < sliceRadii.size() ) ? sliceRadii[splitIdx].first : zMin + ( zMax - zMin ) * 0.6;

    // 底部点拟合圆
    QVector<QVector3D> basePts;
    for ( const QVector3D &v : pts )
        if ( v.z() < splitZ ) basePts.append( v );
    if ( basePts.size() < 5 ) basePts = pts;

    double cr, crx, cry;
    fitCircleRANSAC( basePts, crx, cry, cr );

    // 顶部圆锥的高度
    QVector<QVector3D> topPts;
    for ( const QVector3D &v : pts )
        if ( v.z() >= splitZ ) topPts.append( v );
    double topZ = zMin;
    for ( const QVector3D &v : topPts )
        if ( v.z() > topZ ) topZ = v.z();

    p["ccRadius"]     = cr;
    p["ccCylHeight"]  = splitZ - zMin;
    p["ccConeHeight"] = zMax - splitZ;
    p["tx"] = crx - cr;
    p["ty"] = cry - cr;
    p["tz"] = zMin;
    return refineWithSA( pts, "ConeCylinder", p );
}

// ====================================================================
// GabledRoof: 高度直方图分割墙体与屋顶+脊线检测
// ====================================================================
QMap<QString, double> ParamInverter::invertGabledRoof( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    double minX, maxX, minY, maxY;
    computeFootprintBounds( pts, minX, maxX, minY, maxY );

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();

    int splitIdx = findHeightSplit( zs, 0.4 );
    double wallZ = zs[qBound( 0, splitIdx, zs.size() - 1 )];

    // 取顶部 ~10% 的点做脊线检测
    double topZ = zs[zs.size() * 9 / 10];
    QVector<QVector3D> topPts;
    for ( const QVector3D &v : pts )
        if ( v.z() >= topZ ) topPts.append( v );

    QVector3D ridgeOrg, ridgeDir;
    fitLineRANSAC( topPts, ridgeOrg, ridgeDir );

    p["grLength"]      = maxX - minX;
    p["grWidth"]      = maxY - minY;
    p["grWallHeight"] = wallZ - zMin;
    p["grRoofHeight"] = zMax - wallZ;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return refineWithSA( pts, "GabledRoof", p );
}

// ====================================================================
// PyramidRoof: 墙体+金字塔顶
// ====================================================================
QMap<QString, double> ParamInverter::invertPyramidRoof( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    double minX, maxX, minY, maxY;
    computeFootprintBounds( pts, minX, maxX, minY, maxY );

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();

    int splitIdx = findHeightSplit( zs, 0.4 );
    double wallZ = zs[qBound( 0, splitIdx, zs.size() - 1 )];

    p["prLength"]      = maxX - minX;
    p["prWidth"]      = maxY - minY;
    p["prWallHeight"] = wallZ - zMin;
    p["prRoofHeight"] = zMax - wallZ;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return refineWithSA( pts, "PyramidRoof", p );
}

// ====================================================================
// TruncatedPyramidRoof: 墙体+截断金字塔顶（顶平）
// ====================================================================
QMap<QString, double> ParamInverter::invertTruncatedPyramidRoof( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    double minX, maxX, minY, maxY;
    computeFootprintBounds( pts, minX, maxX, minY, maxY );

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();

    int splitIdx = findHeightSplit( zs, 0.4 );
    double wallZ = zs[qBound( 0, splitIdx, zs.size() - 1 )];

    // 取最高 5% 的点算顶部尺寸
    double topCut = zs[zs.size() * 19 / 20];
    double tMinX = maxX, tMaxX = minX, tMinY = maxY, tMaxY = minY;
    for ( const QVector3D &v : pts )
    {
        if ( v.z() >= topCut )
        {
            if ( v.x() < tMinX ) tMinX = v.x();
            if ( v.x() > tMaxX ) tMaxX = v.x();
            if ( v.y() < tMinY ) tMinY = v.y();
            if ( v.y() > tMaxY ) tMaxY = v.y();
        }
    }
    double topW = qMax( 0.5, tMaxX - tMinX );
    double topD = qMax( 0.5, tMaxY - tMinY );

    p["tpBottomLength"]  = maxX - minX;
    p["tpBottomWidth"]  = maxY - minY;
    p["tpTopLength"]     = topW;
    p["tpTopWidth"]     = topD;
    p["tpWallHeight"]   = wallZ - zMin;
    p["tpRoofHeight"]   = zMax - wallZ;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return refineWithSA( pts, "TruncatedPyramidRoof", p );
}

// ====================================================================
// HalfCylinderRoof: 墙体+半圆柱顶
// ====================================================================
QMap<QString, double> ParamInverter::invertHalfCylinderRoof( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    double minX, maxX, minY, maxY;
    computeFootprintBounds( pts, minX, maxX, minY, maxY );

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();

    int splitIdx = findHeightSplit( zs, 0.4 );
    double wallZ = zs[qBound( 0, splitIdx, zs.size() - 1 )];

    // 顶部点的曲率作为半径估计
    double topZ = zs[zs.size() * 8 / 10];
    QVector<QVector3D> topPts;
    for ( const QVector3D &v : pts )
        if ( v.z() >= topZ ) topPts.append( QVector3D( v.x(), v.y(), 0 ) );

    double crx, cry, cr;
    fitCircleRANSAC( topPts, crx, cry, cr );

    p["hcrLength"]      = maxX - minX;
    p["hcrWidth"]      = maxY - minY;
    p["hcrWallHeight"] = wallZ - zMin;
    p["hcrRadius"]     = cr;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return refineWithSA( pts, "HalfCylinderRoof", p );
}

// ====================================================================
// CylinderDome: 圆柱+贝塞尔穹顶
// ====================================================================
QMap<QString, double> ParamInverter::invertCylinderHemisphere( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();

    // 底部圆柱部分拟合圆
    double zCut = zMin + ( zMax - zMin ) * 0.4;
    QVector<QVector3D> basePts;
    for ( const QVector3D &v : pts )
        if ( v.z() <= zCut ) basePts.append( v );
    if ( basePts.size() < 5 ) basePts = pts;

    double cx, cy, radius;
    fitCircleRANSAC( basePts, cx, cy, radius );

    // 找穹顶起始高度：截面半径开始减小处
    const int SLICES = 20;
    double domeSplit = zMin + ( zMax - zMin ) * 0.7;
    for ( int s = 0; s < SLICES; s++ )
    {
        double zLo = zMin + ( zMax - zMin ) * s / SLICES;
        double zHi = zMin + ( zMax - zMin ) * ( s + 1 ) / SLICES;
        double sumR = 0; int cnt = 0;
        for ( const QVector3D &v : pts )
        {
            if ( v.z() >= zLo && v.z() < zHi )
            {
                double dr = std::sqrt( ( v.x() - cx ) * ( v.x() - cx ) + ( v.y() - cy ) * ( v.y() - cy ) );
                sumR += dr; cnt++;
            }
        }
        if ( cnt > 2 && sumR / cnt < radius * 0.92 )
        { domeSplit = ( zLo + zHi ) * 0.5; break; }
    }

    p["chRadius"]     = radius;
    p["chCylHeight"]  = domeSplit - zMin;
    p["chDomeHeight"] = zMax - domeSplit;
    p["chBulge"]      = 0.5; // 默认鼓胀
    p["tx"] = cx - radius;
    p["ty"] = cy - radius;
    p["tz"] = zMin;
    return refineWithSA( pts, "CylinderDome", p );
}

// ====================================================================
// IndentedCuboid: 外包围盒 + 内凹陷
// ====================================================================
QMap<QString, double> ParamInverter::invertIndentedCuboid( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    double minX, maxX, minY, maxY;
    computeFootprintBounds( pts, minX, maxX, minY, maxY );

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();
    double H = zMax - zMin, W = maxX - minX, D = maxY - minY;

    // 用底部点分析足迹密度，找缺失的凹陷区域
    double zCut = zMin + H * 0.3;
    const int G = 40;
    int grid[G][G] = {};
    for ( const QVector3D &v : pts )
    {
        if ( v.z() > zCut ) continue;
        int ix = qBound( 0, static_cast<int>( ( v.x() - minX ) / W * G ), G - 1 );
        int iy = qBound( 0, static_cast<int>( ( v.y() - minY ) / D * G ), G - 1 );
        grid[ix][iy] = 1;
    }

    // 找低密度连通区域作为凹陷
    int xStart = G, xEnd = 0, yStart = G, yEnd = 0;
    for ( int i = G / 4; i < G * 3 / 4; i++ )
    {
        for ( int j = G / 4; j < G * 3 / 4; j++ )
        {
            if ( grid[i][j] == 0 )
            {
                if ( i < xStart ) xStart = i;
                if ( i > xEnd )   xEnd = i;
                if ( j < yStart ) yStart = j;
                if ( j > yEnd )   yEnd = j;
            }
        }
    }

    double iW = ( xEnd > xStart ) ? ( xEnd - xStart + 1 ) * W / G : W * 0.2;
    double iD = ( yEnd > yStart ) ? ( yEnd - yStart + 1 ) * D / G : D * 0.2;
    double ox = ( xStart < G ) ? ( xStart * W / G ) : W * 0.3;
    double oy = ( yStart < G ) ? ( yStart * D / G ) : D * 0.3;

    p["icOuterL"]   = W;
    p["icOuterW"]   = D;
    p["icOuterH"]   = H;
    p["icInnerL"]   = iW;
    p["icInnerW"]   = iD;
    p["icInnerH"]   = H * 0.6;
    p["icOffsetX"]  = ox;
    p["icOffsetY"]  = oy;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return refineWithSA( pts, "IndentedCuboid", p );
}

// ====================================================================
// AsymmetricGableHouse: 非对称人字屋顶
// ====================================================================
QMap<QString, double> ParamInverter::invertAsymmetricGableHouse( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    double minX, maxX, minY, maxY;
    computeFootprintBounds( pts, minX, maxX, minY, maxY );

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();

    int splitIdx = findHeightSplit( zs, 0.4 );
    double wallZ = zs[qBound( 0, splitIdx, zs.size() - 1 )];

    // 顶部点做脊线检测
    double topZ = zs[zs.size() * 9 / 10];
    QVector<QVector3D> topPts;
    for ( const QVector3D &v : pts )
        if ( v.z() >= topZ ) topPts.append( v );

    QVector3D ridgeOrg, ridgeDir;
    fitLineRANSAC( topPts, ridgeOrg, ridgeDir );

    // 脊线偏离中心的偏移量
    double ridgeY = 0;
    for ( const QVector3D &v : topPts ) ridgeY += v.y();
    if ( !topPts.isEmpty() ) ridgeY /= topPts.size();
    double ridgeRatio = ( ridgeY - minY ) / qMax( 1e-6, maxY - minY );
    ridgeRatio = qBound( 0.2, ridgeRatio, 0.8 );

    // 脊线长度
    double ridgeLen = ( maxY - minY ) * 0.8;

    p["aghLength"]       = maxX - minX;
    p["aghWidth"]       = maxY - minY;
    p["aghWallHeight"]  = wallZ - zMin;
    p["aghRoofHeight"]  = zMax - wallZ;
    p["aghRidgeLen"]    = ridgeLen;
    p["aghRidgeRatio"]  = ridgeRatio;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return refineWithSA( pts, "AsymmetricGableHouse", p );
}

// ====================================================================
// FourStageRoundTower: 四段式圆塔
// ====================================================================
QMap<QString, double> ParamInverter::invertFourStageRoundTower( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();
    double H = zMax - zMin;

    // 圆柱拟合（底部）
    double cx, cy, baseR;
    {
        double zCut = zMin + H * 0.25;
        QVector<QVector3D> basePts;
        for ( const QVector3D &v : pts ) if ( v.z() <= zCut ) basePts.append( v );
        if ( basePts.size() < 5 ) basePts = pts;
        fitCircleRANSAC( basePts, cx, cy, baseR );
    }

    // 沿高度分析半径变化找分段
    const int SLICES = 30;
    QVector<double> sliceRadii;
    for ( int s = 0; s < SLICES; s++ )
    {
        double zLo = zMin + H * s / SLICES;
        double zHi = zMin + H * ( s + 1 ) / SLICES;
        double sumR = 0; int cnt = 0;
        for ( const QVector3D &v : pts )
        {
            if ( v.z() >= zLo && v.z() < zHi )
            { sumR += std::sqrt( ( v.x() - cx ) * ( v.x() - cx ) + ( v.y() - cy ) * ( v.y() - cy ) ); cnt++; }
        }
        sliceRadii.append( cnt > 2 ? sumR / cnt : baseR );
    }

    // 找分段点（相邻层半径变化 > 10%）
    QVector<int> segBreaks;
    for ( int i = 1; i < SLICES; i++ )
    {
        if ( sliceRadii[i-1] > 0.01 && qAbs( sliceRadii[i] - sliceRadii[i-1] ) / sliceRadii[i-1] > 0.12 )
            segBreaks.append( i );
    }

    double baseH  = ( segBreaks.size() >= 1 ) ? H * segBreaks[0] / SLICES : H * 0.3;
    double midH   = ( segBreaks.size() >= 2 ) ? H * ( segBreaks[1] - segBreaks[0] ) / SLICES : H * 0.25;
    double topMidR = ( segBreaks.size() >= 2 ) ? sliceRadii[segBreaks[1]] : baseR * 0.7;
    double midBulge = 0.6;
    double coneH  = H - baseH - midH;
    if ( coneH < 0 ) coneH = H * 0.2;

    p["ftBaseR"]   = baseR;
    p["ftBaseH"]   = baseH;
    p["ftMidH"]    = midH;
    p["ftMidTopR"] = topMidR;
    p["ftMidBulge"]= midBulge;
    p["ftConeH"]   = coneH;
    p["tx"] = cx - baseR;
    p["ty"] = cy - baseR;
    p["tz"] = zMin;
    return refineWithSA( pts, "FourStageRoundTower", p );
}

// ====================================================================
// TwoGableHouses: 两个带人字顶的房屋以角度拼接
// ====================================================================
QMap<QString, double> ParamInverter::invertTwoGableHouses( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    double minX, maxX, minY, maxY;
    computeFootprintBounds( pts, minX, maxX, minY, maxY );

    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();

    int splitIdx = findHeightSplit( zs, 0.4 );
    double wallZ = zs[qBound( 0, splitIdx, zs.size() - 1 )];

    // 顶部点检测两个脊线方向
    double topZ = zs[zs.size() * 9 / 10];
    QVector<QVector3D> topPts;
    for ( const QVector3D &v : pts )
        if ( v.z() >= topZ ) topPts.append( QVector3D( v.x(), v.y(), v.z() ) );

    // 用 PCA 找主方向来估计两个房屋的角度
    double mx = 0, my = 0;
    for ( const QVector3D &v : topPts ) { mx += v.x(); my += v.y(); }
    if ( !topPts.isEmpty() ) { mx /= topPts.size(); my /= topPts.size(); }

    double cxx = 0, cxy = 0, cyy = 0;
    for ( const QVector3D &v : topPts )
    { double dx = v.x() - mx, dy = v.y() - my; cxx += dx * dx; cxy += dx * dy; cyy += dy * dy; }

    double angle = 90; // 默认 90 度
    if ( qAbs( cxx - cyy ) > 0.001 || qAbs( cxy ) > 0.001 )
    {
        double theta = 0.5 * std::atan2( 2 * cxy, cxx - cyy );
        // 如果点云呈单一直线分布，两个房屋大概在 60-120 度之间
        double ratio = qAbs( cxx - cyy ) / ( cxx + cyy + 0.001 );
        if ( ratio < 0.4 )
            angle = 90; // 近似 L 形，角度约 90°
        else
            angle = 60 + 60 * ratio; // 根据分布估计
    }

    double W = maxX - minX, D = maxY - minY;
    double w1 = W * 0.55;
    double w2 = W * 0.45;

    p["tgLength1"]     = w1;
    p["tgLength2"]     = w2;
    p["tgWidth"]      = D;
    p["tgWallHeight"] = wallZ - zMin;
    p["tgRoofHeight"] = zMax - wallZ;
    p["tgAngle"]      = angle;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return refineWithSA( pts, "TwoGableHouses", p );
}
