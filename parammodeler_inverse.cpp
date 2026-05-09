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
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <random>

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

    if ( primitiveType == "Cuboid" )
        return invertCuboid( pts );
    if ( primitiveType == "Cylinder" )
        return invertCylinder( pts );
    if ( primitiveType == "LHouse" )
        return invertLHouse( pts );
    if ( primitiveType == "ConeCylinder" )
        return invertConeCylinder( pts );
    if ( primitiveType == "GabledRoof" )
        return invertGabledRoof( pts );
    if ( primitiveType == "PyramidRoof" )
        return invertPyramidRoof( pts );
    if ( primitiveType == "TruncatedPyramidRoof" )
        return invertTruncatedPyramidRoof( pts );
    if ( primitiveType == "HalfCylinderRoof" )
        return invertHalfCylinderRoof( pts );
    if ( primitiveType == "CylinderHemisphere" )
        return invertCylinderHemisphere( pts );
    if ( primitiveType == "穹顶圆柱" )
        return invertCylinderHemisphere( pts );
    if ( primitiveType == "IndentedCuboid" || primitiveType == "凹陷长方体" )
        return invertIndentedCuboid( pts );
    if ( primitiveType == "AsymmetricGableHouse" || primitiveType == "非对称人字形屋顶房屋" )
        return invertAsymmetricGableHouse( pts );
    if ( primitiveType == "FourStageRoundTower" || primitiveType == "四段式圆塔形" )
        return invertFourStageRoundTower( pts );
    if ( primitiveType == "TwoGableHouses" || primitiveType == "双人字屋顶房屋" )
        return invertTwoGableHouses( pts );

    return QMap<QString, double>();
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

    set( "width",     dock->ui->spinBoxCWidth,     dock->ui->sliderCWidth );
    set( "depth",     dock->ui->spinBoxCDepth,     dock->ui->sliderCDepth );
    set( "height",    dock->ui->spinBoxCHeight,    dock->ui->sliderCHeight );
    set( "radius",    dock->ui->spinBoxCylRadius,  dock->ui->sliderCylRadius );
    set( "cylHeight", dock->ui->spinBoxCylHeight,  dock->ui->sliderCylHeight );
    set( "lMainW",    dock->ui->spinBoxLMainWidth,  dock->ui->sliderLMainWidth );
    set( "lMainD",    dock->ui->spinBoxLMainDepth,  dock->ui->sliderLMainDepth );
    set( "lWingW",    dock->ui->spinBoxLWingWidth,  dock->ui->sliderLWingWidth );
    set( "lWingD",    dock->ui->spinBoxLWingDepth,  dock->ui->sliderLWingDepth );
    set( "lHeight",   dock->ui->spinBoxLHeight,     dock->ui->sliderLHeight );
    set( "ccRadius",      dock->ui->spinBoxConeCylRadius,    dock->ui->sliderConeCylRadius );
    set( "ccCylHeight",   dock->ui->spinBoxConeCylCylHeight, dock->ui->sliderConeCylCylHeight );
    set( "ccConeHeight",  dock->ui->spinBoxConeCylConeHeight,dock->ui->sliderConeCylConeHeight );
    set( "grWidth",       dock->ui->spinBoxGRWidth,      dock->ui->sliderGRWidth );
    set( "grDepth",       dock->ui->spinBoxGRDepth,      dock->ui->sliderGRDepth );
    set( "grWallHeight",  dock->ui->spinBoxGRHeightWall, dock->ui->sliderGRHeightWall );
    set( "grRoofHeight",  dock->ui->spinBoxGRHeightRoof, dock->ui->sliderGRHeightRoof );
    set( "prWidth",       dock->ui->spinBoxPRWidth,      dock->ui->sliderPRWidth );
    set( "prDepth",       dock->ui->spinBoxPRDepth,      dock->ui->sliderPRDepth );
    set( "prWallHeight",  dock->ui->spinBoxPRHeightWall, dock->ui->sliderPRHeightWall );
    set( "prRoofHeight",  dock->ui->spinBoxPRHeightRoof, dock->ui->sliderPRHeightRoof );
    set( "tpBottomWidth",  dock->ui->spinBoxTPRBottomWidth,  dock->ui->sliderTPRBottomWidth );
    set( "tpBottomDepth",  dock->ui->spinBoxTPRBottomDepth,  dock->ui->sliderTPRBottomDepth );
    set( "tpTopWidth",     dock->ui->spinBoxTPRTopWidth,     dock->ui->sliderTPRTopWidth );
    set( "tpTopDepth",     dock->ui->spinBoxTPRTopDepth,     dock->ui->sliderTPRTopDepth );
    set( "tpWallHeight",   dock->ui->spinBoxTPRHeightWall,   dock->ui->sliderTPRHeightWall );
    set( "tpRoofHeight",   dock->ui->spinBoxTPRHeightRoof,   dock->ui->sliderTPRHeightRoof );
    set( "hcrWidth",        dock->ui->spinBoxHCRWidth,      dock->ui->sliderHCRWidth );
    set( "hcrDepth",        dock->ui->spinBoxHCRDepth,      dock->ui->sliderHCRDepth );
    set( "hcrWallHeight",   dock->ui->spinBoxHCRHeightWall, dock->ui->sliderHCRHeightWall );
    set( "hcrRadius",       dock->ui->spinBoxHCRRadius,     dock->ui->sliderHCRRadius );
    set( "chRadius",     dock->ui->spinBoxCylHemiRadius,     dock->ui->sliderCylHemiRadius );
    set( "chCylHeight",  dock->ui->spinBoxCylHemiHeight,     dock->ui->sliderCylHemiHeight );
    set( "chDomeHeight", dock->ui->spinBoxCylHemiDomeHeight, dock->ui->sliderCylHemiDomeHeight );
    set( "chBulge",      dock->ui->spinBoxCylHemiBulge,      dock->ui->sliderCylHemiBulge );
    set( "icOuterW",    dock->ui->spinBoxICWidth,       dock->ui->sliderICWidth );
    set( "icOuterD",    dock->ui->spinBoxICDepth,       dock->ui->sliderICDepth );
    set( "icOuterH",    dock->ui->spinBoxICHeight,      dock->ui->sliderICHeight );
    set( "icInnerW",    dock->ui->spinBoxICInnerWidth,  dock->ui->sliderICInnerWidth );
    set( "icInnerD",    dock->ui->spinBoxICInnerDepth,  dock->ui->sliderICInnerDepth );
    set( "icInnerH",    dock->ui->spinBoxICInnerHeight, dock->ui->sliderICInnerHeight );
    set( "icOffsetX",   dock->ui->spinBoxICOffsetX,     dock->ui->sliderICOffsetX );
    set( "icOffsetY",   dock->ui->spinBoxICOffsetY,     dock->ui->sliderICOffsetY );
    set( "aghWidth",       dock->ui->spinBoxAGHWidth,       dock->ui->sliderAGHWidth );
    set( "aghDepth",       dock->ui->spinBoxAGHDepth,       dock->ui->sliderAGHDepth );
    set( "aghWallHeight",  dock->ui->spinBoxAGHHeightWall,  dock->ui->sliderAGHHeightWall );
    set( "aghRoofHeight",  dock->ui->spinBoxAGHRoofHeight,  dock->ui->sliderAGHRoofHeight );
    set( "aghRidgeLen",    dock->ui->spinBoxAGHRidgeLength, dock->ui->sliderAGHRidgeLength );
    set( "aghRidgeOffset", dock->ui->spinBoxAGHRidgeOffset, dock->ui->sliderAGHRidgeOffset );
    set( "ftBaseR",       dock->ui->spinBoxFTBaseRadius,     dock->ui->sliderFTBaseRadius );
    set( "ftBaseH",       dock->ui->spinBoxFTBaseHeight,     dock->ui->sliderFTBaseHeight );
    set( "ftMidH",        dock->ui->spinBoxFTMiddleHeight,   dock->ui->sliderFTMiddleHeight );
    set( "ftMidTopR",     dock->ui->spinBoxFTMiddleTopRadius,dock->ui->sliderFTMiddleTopRadius );
    set( "ftMidBulge",    dock->ui->spinBoxFTMiddleBulge,    dock->ui->sliderFTMiddleBulge );
    set( "ftConeH",       dock->ui->spinBoxFTConeHeight,     dock->ui->sliderFTConeHeight );
    set( "tgWidth1",      dock->ui->spinBoxTGWidth1,     dock->ui->sliderTGWidth1 );
    set( "tgWidth2",      dock->ui->spinBoxTGWidth2,     dock->ui->sliderTGWidth2 );
    set( "tgDepth",       dock->ui->spinBoxTGDepth,      dock->ui->sliderTGDepth );
    set( "tgWallHeight",  dock->ui->spinBoxTGHeightWall, dock->ui->sliderTGHeightWall );
    set( "tgRoofHeight",  dock->ui->spinBoxTGRoofHeight, dock->ui->sliderTGRoofHeight );
    set( "tgAngle",       dock->ui->spinBoxTGAngle,      dock->ui->sliderTGAngle );

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

    p["width"]  = maxX - minX;
    p["depth"]  = maxY - minY;
    p["height"] = zs.last() - zs.first();
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zs.first();
    return p;
}

// ====================================================================
// Cylinder: RANSAC 拟合圆 + 高度范围
// ====================================================================
QMap<QString, double> ParamInverter::invertCylinder( const QVector<QVector3D> &pts )
{
    QMap<QString, double> p;
    if ( pts.size() < 10 ) return p;

    // 用底部的点拟合圆（避免顶部噪声）
    QVector<double> zs;
    zs.reserve( pts.size() );
    for ( const QVector3D &v : pts ) zs.append( v.z() );
    std::sort( zs.begin(), zs.end() );
    double zMin = zs.first(), zMax = zs.last();
    double zCut = zMin + ( zMax - zMin ) * 0.3;

    QVector<QVector3D> basePts;
    for ( const QVector3D &v : pts )
        if ( v.z() <= zCut )
            basePts.append( v );

    double cx, cy, radius;
    fitCircleRANSAC( basePts, cx, cy, radius );

    p["radius"] = radius;
    p["cylHeight"] = zMax - zMin;
    p["tx"] = cx - radius;
    p["ty"] = cy - radius;
    p["tz"] = zMin;
    return p;
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

    p["lMainW"]   = sX - minX;
    p["lMainD"]   = maxY - minY;
    p["lWingW"]   = maxX - sX;
    p["lWingD"]   = sY - minY;
    p["lHeight"]  = zMax - zMin;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return p;
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
    return p;
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

    p["grWidth"]      = maxX - minX;
    p["grDepth"]      = maxY - minY;
    p["grWallHeight"] = wallZ - zMin;
    p["grRoofHeight"] = zMax - wallZ;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return p;
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

    p["prWidth"]      = maxX - minX;
    p["prDepth"]      = maxY - minY;
    p["prWallHeight"] = wallZ - zMin;
    p["prRoofHeight"] = zMax - wallZ;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return p;
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

    p["tpBottomWidth"]  = maxX - minX;
    p["tpBottomDepth"]  = maxY - minY;
    p["tpTopWidth"]     = topW;
    p["tpTopDepth"]     = topD;
    p["tpWallHeight"]   = wallZ - zMin;
    p["tpRoofHeight"]   = zMax - wallZ;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return p;
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

    p["hcrWidth"]      = maxX - minX;
    p["hcrDepth"]      = maxY - minY;
    p["hcrWallHeight"] = wallZ - zMin;
    p["hcrRadius"]     = cr;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return p;
}

// ====================================================================
// CylinderHemisphere: 圆柱+穹顶
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
    return p;
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

    p["icOuterW"]   = W;
    p["icOuterD"]   = D;
    p["icOuterH"]   = H;
    p["icInnerW"]   = iW;
    p["icInnerD"]   = iD;
    p["icInnerH"]   = H * 0.6;
    p["icOffsetX"]  = ox;
    p["icOffsetY"]  = oy;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return p;
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
    double centerX = ( minX + maxX ) * 0.5;
    double ridgeCX = 0;
    for ( const QVector3D &v : topPts ) ridgeCX += v.x();
    if ( !topPts.isEmpty() ) ridgeCX /= topPts.size();
    double ridgeOffset = ridgeCX - centerX;

    // 脊线长度
    double ridgeLen = ( maxY - minY ) * 0.8;

    p["aghWidth"]       = maxX - minX;
    p["aghDepth"]       = maxY - minY;
    p["aghWallHeight"]  = wallZ - zMin;
    p["aghRoofHeight"]  = zMax - wallZ;
    p["aghRidgeLen"]    = ridgeLen;
    p["aghRidgeOffset"] = ridgeOffset;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return p;
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
    return p;
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

    p["tgWidth1"]     = w1;
    p["tgWidth2"]     = w2;
    p["tgDepth"]      = D;
    p["tgWallHeight"] = wallZ - zMin;
    p["tgRoofHeight"] = zMax - wallZ;
    p["tgAngle"]      = angle;
    p["tx"] = minX;
    p["ty"] = minY;
    p["tz"] = zMin;
    return p;
}
