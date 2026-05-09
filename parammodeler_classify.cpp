/***************************************************************************
  parammodeler_classify.cpp
  Point Cloud Primitive Classification
  -------------------
         begin                : May 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "parammodeler_classify.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <set>

#include <qgspointcloudlayer.h>
#include <qgspointcloudindex.h>
#include <qgspointcloudblock.h>

#include <qgspointcloudrequest.h>
#include <qgspointcloudattribute.h>
#include <qgsvector3d.h>

// ====================================================================
// 公共入口
// ====================================================================
PrimitiveClassifier::Result PrimitiveClassifier::classify( const QString &filePath )
{
    Result result;
    result.primitiveType = "Cuboid";
    result.confidence = 0.0;

    if ( filePath.isEmpty() ) return result;

    PointCloud pc = loadPointCloud( filePath );
    if ( pc.points.isEmpty() ) return result;

    PointCloud sp = downsample( pc, 2000 );
    FeatureVector fv = extractFeatures( sp );
    result = classifyByScore( fv );

    return result;
}

// ====================================================================
// PLY 头部解析辅助
// ====================================================================
static int parsePlyHeader( QFile &file, int &vertexCount,
                            QMap<QString,int> &propOffset,
                            QMap<QString,int> &propSize,
                            QMap<QString,bool> &propIsDouble,
                            int &recordSize, bool &isAscii, bool &isBigEndian )
{
    vertexCount = 0;
    recordSize = 0;
    isAscii = false;
    isBigEndian = false;
    propOffset.clear();
    propSize.clear();
    propIsDouble.clear();

    file.seek( 0 );
    int curOff = 0;

    while ( !file.atEnd() )
    {
        QByteArray lineBA;
        char c;
        while ( file.getChar( &c ) )
        {
            if ( c == '\n' ) break;
            if ( c != '\r' ) lineBA.append( c );
        }
        QString line = QString::fromLatin1( lineBA ).trimmed();

        if ( line.startsWith( "format" ) )
        {
            if ( line.contains( "ascii" ) )                isAscii = true;
            else if ( line.contains( "binary_little_endian" ) ) isAscii = false;
            else if ( line.contains( "binary_big_endian" ) )    { isAscii = false; isBigEndian = true; }
        }
        else if ( line.startsWith( "element vertex" ) )
        {
            vertexCount = line.split( ' ', Qt::SkipEmptyParts ).last().toInt();
        }
        else if ( line.startsWith( "property" ) )
        {
            QStringList tok = line.split( ' ', Qt::SkipEmptyParts );
            if ( tok.size() >= 3 )
            {
                QString tname = tok[1];
                QString pname = tok[2];
                int sz = 4;
                bool isDbl = false;
                if      ( tname == "double" || tname == "float64" ) { sz = 8; isDbl = true; }
                else if ( tname == "float"  || tname == "float32" ) { sz = 4; isDbl = false; }
                else if ( tname == "int"    || tname == "int32" ||
                          tname == "uint"   || tname == "uint32"  ) { sz = 4; isDbl = false; }
                else if ( tname == "short"  || tname == "int16"   ||
                          tname == "ushort" || tname == "uint16"  ) { sz = 2; isDbl = false; }
                else if ( tname == "char"   || tname == "uchar"   ||
                          tname == "int8"   || tname == "uint8"   ) { sz = 1; isDbl = false; }
                propOffset[pname] = curOff;
                propSize[pname] = sz;
                propIsDouble[pname] = isDbl;
                curOff += sz;
            }
        }
        else if ( line == "end_header" )
        {
            break;
        }
    }
    recordSize = curOff;
    return 0;
}

// ====================================================================
// 加载点云
// ====================================================================
PrimitiveClassifier::PointCloud PrimitiveClassifier::loadPointCloud( const QString &filePath )
{
    PointCloud pc;
    QFileInfo fi( filePath );
    QString suffix = fi.suffix().toLower();

    if ( suffix == "ply" )
    {
        QFile file( filePath );
        if ( !file.open( QIODevice::ReadOnly ) ) return pc;

        int vertexCount = 0, recordSize = 0;
        bool isAscii = false, isBigEndian = false;
        QMap<QString,int> propOffset, propSize;
        QMap<QString,bool> propIsDouble;

        parsePlyHeader( file, vertexCount, propOffset, propSize,
                         propIsDouble, recordSize, isAscii, isBigEndian );

        if ( vertexCount <= 0 || !propOffset.contains( "x" ) ||
              !propOffset.contains( "y" ) || !propOffset.contains( "z" ) )
        {
            file.close();
            return pc;
        }

        int xOff = propOffset["x"], xSz = propSize["x"]; bool xDbl = propIsDouble["x"];
        int yOff = propOffset["y"], ySz = propSize["y"]; bool yDbl = propIsDouble["y"];
        int zOff = propOffset["z"], zSz = propSize["z"]; bool zDbl = propIsDouble["z"];

        auto readDouble = [&]( const QByteArray &rec, int off, int sz, bool isDbl, bool bigEndian ) -> double
        {
            if ( isDbl )
            {
                double v;
                memcpy( &v, rec.constData() + off, 8 );
                if ( bigEndian ) { char *b = reinterpret_cast<char*>(&v); std::reverse( b, b + 8 ); }
                return v;
            }
            else
            {
                float v;
                memcpy( &v, rec.constData() + off, qMin( sz, 4 ) );
                if ( bigEndian ) { char *b = reinterpret_cast<char*>(&v); std::reverse( b, b + 4 ); }
                return static_cast<double>( v );
            }
        };

        pc.points.reserve( vertexCount );
        pc.originalCount = vertexCount;

        if ( isAscii )
        {
            // Determine column order by offset
            QList<QPair<int,QString>> orderList;
            for ( auto it = propOffset.begin(); it != propOffset.end(); ++it )
                orderList.append( { it.value(), it.key() } );
            std::sort( orderList.begin(), orderList.end() );
            QStringList propOrder;
            for ( auto &p : orderList ) propOrder << p.second;
            int xCol = propOrder.indexOf( "x" );
            int yCol = propOrder.indexOf( "y" );
            int zCol = propOrder.indexOf( "z" );
            int need = std::max( { xCol, yCol, zCol } ) + 1;

            int count = 0;
            while ( !file.atEnd() && count < vertexCount )
            {
                QByteArray lineBA = file.readLine().trimmed();
                if ( lineBA.isEmpty() ) continue;
                QList<QByteArray> parts = lineBA.split( ' ' );
                parts.removeAll( QByteArray() );
                if ( parts.size() < need ) continue;

                double x = parts[xCol].toDouble();
                double y = parts[yCol].toDouble();
                double z = parts[zCol].toDouble();
                pc.points.append( QVector3D( x, y, z ) );
                count++;
            }
        }
        else
        {
            for ( int i = 0; i < vertexCount; i++ )
            {
                QByteArray rec = file.read( recordSize );
                if ( rec.size() < recordSize ) break;
                double x = readDouble( rec, xOff, xSz, xDbl, isBigEndian );
                double y = readDouble( rec, yOff, ySz, yDbl, isBigEndian );
                double z = readDouble( rec, zOff, zSz, zDbl, isBigEndian );
                pc.points.append( QVector3D( x, y, z ) );
            }
        }
        file.close();
    }
    else if ( suffix == "las" || suffix == "laz" )
    {
        QgsPointCloudLayer *tmpLayer = new QgsPointCloudLayer( filePath, "tmp_classify", "pdal" );
        if ( !tmpLayer || !tmpLayer->isValid() )
        {
            delete tmpLayer;
            return pc;
        }

        QgsPointCloudIndex index = tmpLayer->dataProvider()->index();
        if ( !index.isValid() )
        {
            delete tmpLayer;
            return pc;
        }

        QgsPointCloudAttributeCollection attrs;
        attrs.push_back( QgsPointCloudAttribute( "X", QgsPointCloudAttribute::Int32 ) );
        attrs.push_back( QgsPointCloudAttribute( "Y", QgsPointCloudAttribute::Int32 ) );
        attrs.push_back( QgsPointCloudAttribute( "Z", QgsPointCloudAttribute::Int32 ) );
        QgsPointCloudRequest request;
        request.setAttributes( attrs );

        QgsVector3D scale = index.scale();
        QgsVector3D offset = index.offset();

        QList<QgsPointCloudNodeId> queue;
        queue.append( index.root() );

        while ( !queue.isEmpty() )
        {
            QgsPointCloudNodeId nodeId = queue.takeFirst();
            QgsPointCloudNode node = index.getNode( nodeId );
            for ( const QgsPointCloudNodeId &child : node.children() )
                queue.append( child );

            std::unique_ptr<QgsPointCloudBlock> block = index.nodeData( nodeId, request );
            if ( !block ) continue;

            const char *data = block->data();
            int ptCnt = block->pointCount();
            int recSz = block->pointRecordSize();

            for ( int i = 0; i < ptCnt; i++ )
            {
                const char *ptr = data + i * recSz;
                qint32 ix = *reinterpret_cast<const qint32*>( ptr );
                qint32 iy = *reinterpret_cast<const qint32*>( ptr + 4 );
                qint32 iz = *reinterpret_cast<const qint32*>( ptr + 8 );
                double x = ix * scale.x() + offset.x();
                double y = iy * scale.y() + offset.y();
                double z = iz * scale.z() + offset.z();
                pc.points.append( QVector3D( x, y, z ) );
            }
        }
        pc.originalCount = pc.points.size();
        delete tmpLayer;
    }

    // Compute bounding box
    if ( !pc.points.isEmpty() )
    {
        pc.bboxMin = pc.points[0];
        pc.bboxMax = pc.points[0];
        for ( const QVector3D &p : pc.points )
        {
            if ( p.x() < pc.bboxMin.x() ) pc.bboxMin.setX( p.x() );
            if ( p.y() < pc.bboxMin.y() ) pc.bboxMin.setY( p.y() );
            if ( p.z() < pc.bboxMin.z() ) pc.bboxMin.setZ( p.z() );
            if ( p.x() > pc.bboxMax.x() ) pc.bboxMax.setX( p.x() );
            if ( p.y() > pc.bboxMax.y() ) pc.bboxMax.setY( p.y() );
            if ( p.z() > pc.bboxMax.z() ) pc.bboxMax.setZ( p.z() );
        }
    }
    return pc;
}

// ====================================================================
// 体素降采样
// ====================================================================
PrimitiveClassifier::PointCloud PrimitiveClassifier::downsample( const PointCloud &pc, int targetPoints )
{
    PointCloud result;
    result.bboxMin = pc.bboxMin;
    result.bboxMax = pc.bboxMax;
    result.originalCount = pc.originalCount;

    int n = pc.points.size();
    if ( n <= targetPoints )
    {
        result.points = pc.points;
        return result;
    }

    float step = static_cast<float>( n ) / targetPoints;
    result.points.reserve( targetPoints );
    for ( int i = 0; i < targetPoints; i++ )
    {
        int idx = static_cast<int>( i * step );
        if ( idx >= n ) idx = n - 1;
        result.points.append( pc.points[idx] );
    }
    return result;
}

// ====================================================================
// 特征提取入口
// ====================================================================
PrimitiveClassifier::FeatureVector
PrimitiveClassifier::extractFeatures( const PointCloud &pc )
{
    FeatureVector fv;
    if ( pc.points.size() < 10 ) return fv;

    const QVector<QVector3D> &pts = pc.points;
    QVector3D size = pc.bboxMax - pc.bboxMin;

    // XY 投影点（忽略 Z 坐标用于二维分析）
    QVector<QVector3D> xyProj;
    xyProj.reserve( pts.size() );
    for ( const QVector3D &p : pts )
        xyProj.append( QVector3D( p.x(), p.y(), 0 ) );

    // 1. 足迹圆度
    fv.footprintCircularity = computeCircularity( xyProj, pc.bboxMin, pc.bboxMax );

    // 2. 足迹长宽比
    double w = qMax( size.x(), size.y() );
    double d = qMin( size.x(), size.y() );
    fv.footprintAspectRatio = ( w > 0.001 ) ? ( d / w ) : 1.0;

    // 3. 足迹凸包度
    fv.footprintConvexity = computeConvexity( xyProj, pc.bboxMin, pc.bboxMax );

    // 4-5. PCA
    QVector3D eigenvalues;
    QVector3D eigenvectors[3];
    computePCA( pts, eigenvalues, eigenvectors );
    if ( eigenvalues.x() > 0.0001 )
    {
        fv.pcaRatio12 = eigenvalues.y() / eigenvalues.x();
        fv.pcaRatio23 = eigenvalues.z() / eigenvalues.y();
    }

    // 6-7. 高度分位数
    double hMin = pc.bboxMin.z(), hMax = pc.bboxMax.z();
    double hRange = hMax - hMin;
    if ( hRange > 0.0001 )
    {
        QVector<double> zVals;
        zVals.reserve( pts.size() );
        for ( const QVector3D &p : pts ) zVals.append( p.z() );
        std::sort( zVals.begin(), zVals.end() );
        int nz = zVals.size();
        fv.heightRatio50 = ( zVals[nz / 2] - hMin ) / hRange;
        fv.heightRatio80 = ( zVals[nz * 4 / 5] - hMin ) / hRange;
    }

    // 8. 顶面斜率
    fv.topSlope = computeTopSlope( pc );

    // 9-10. 对称性
    fv.symmetryX = computeSymmetry( pc, 0 );
    fv.symmetryY = computeSymmetry( pc, 1 );

    // 11. 截面一致性
    fv.crossSectionConsistency = computeCrossSectionConsistency( pc );

    // 12. 高度分段数
    fv.numStages = countHeightStages( pc );

    // 13. 屋顶角度指示
    if ( hRange > 0.001 )
    {
        QVector<double> allZ;
        allZ.reserve( pts.size() );
        for ( const QVector3D &p : pts ) allZ.append( p.z() );
        std::sort( allZ.begin(), allZ.end() );
        double z80 = allZ[allZ.size() * 4 / 5];
        double z95 = allZ[allZ.size() * 19 / 20];
        int cnt80 = 0, cnt95 = 0;
        for ( const QVector3D &p : pts )
        {
            if ( p.z() >= z80 ) cnt80++;
            if ( p.z() >= z95 ) cnt95++;
        }
        fv.roofAngle = ( cnt80 > 0 ) ? static_cast<double>( cnt95 ) / cnt80 : 0.0;
    }

    // 14. 顶面线性度
    fv.topLinearity = computeTopLinearity( pc );

    // 15. 垂直分段数
    fv.numVerticalSegments = countVerticalSegments( pc );

    return fv;
}

// ====================================================================
// 圆度：用 2D 网格近似（基于径向距离的变异系数）
// ====================================================================
double PrimitiveClassifier::computeCircularity( const QVector<QVector3D> &xyProj,
                                                  const QVector3D &bboxMin,
                                                  const QVector3D &bboxMax )
{
    double cx = ( bboxMin.x() + bboxMax.x() ) * 0.5;
    double cy = ( bboxMin.y() + bboxMax.y() ) * 0.5;

    QVector<double> radii;
    radii.reserve( xyProj.size() );

    for ( const QVector3D &p : xyProj )
    {
        double dx = p.x() - cx;
        double dy = p.y() - cy;
        radii.append( std::sqrt( dx * dx + dy * dy ) );
    }

    std::sort( radii.begin(), radii.end() );
    int n = radii.size();
    if ( n < 10 ) return 0.0;

    // 只用中间 80% 的点来计算，过滤离群点
    int lo = n / 10, hi = n * 9 / 10;
    double sum = 0, sumSq = 0;
    int cnt = 0;
    for ( int i = lo; i < hi; i++ )
    {
        sum += radii[i];
        sumSq += radii[i] * radii[i];
        cnt++;
    }
    if ( cnt == 0 || sum < 0.0001 ) return 0.0;

    double mean = sum / cnt;
    double variance = sumSq / cnt - mean * mean;
    if ( variance < 0 ) variance = 0;
    double std = std::sqrt( variance );
    double cv = std / mean;

    // 变异系数越小 → 越圆。映射到 [0, 1]，CV 约 0.05 得高分
    double circularity = std::exp( -cv * 20.0 );
    return qBound( 0.0, circularity, 1.0 );
}

// ====================================================================
// 凸包度：2D 凸包面积 / 包围盒面积
// ====================================================================
static double cross2D( QVector3D o, QVector3D a, QVector3D b )
{
    return ( a.x() - o.x() ) * ( b.y() - o.y() ) - ( a.y() - o.y() ) * ( b.x() - o.x() );
}

static QVector<QVector3D> convexHull2D( const QVector<QVector3D> &pts )
{
    if ( pts.size() < 3 ) return pts;
    QVector<QPair<double,int>> sorted;
    int lowest = 0;
    for ( int i = 1; i < pts.size(); i++ )
        if ( pts[i].y() < pts[lowest].y() ||
              ( qAbs( pts[i].y() - pts[lowest].y() ) < 0.0001 && pts[i].x() < pts[lowest].x() ) )
            lowest = i;
    QVector3D origin = pts[lowest];
    for ( int i = 0; i < pts.size(); i++ )
    {
        if ( i == lowest ) continue;
        double dx = pts[i].x() - origin.x();
        double dy = pts[i].y() - origin.y();
        double angle = std::atan2( dy, dx );
        sorted.append( { angle, i } );
    }
    std::sort( sorted.begin(), sorted.end(), []( const QPair<double,int> &a, const QPair<double,int> &b ) {
        return a.first < b.first;
    } );

    QVector<QVector3D> hull;
    hull.append( origin );
    for ( auto &p : sorted )
    {
        QVector3D pt = pts[p.second];
        while ( hull.size() >= 2 && cross2D( hull[hull.size()-2], hull.last(), pt ) <= 0 )
            hull.removeLast();
        hull.append( pt );
    }
    return hull;
}

static double polygonArea2D( const QVector<QVector3D> &poly )
{
    double area = 0;
    int n = poly.size();
    for ( int i = 0; i < n; i++ )
    {
        int j = ( i + 1 ) % n;
        area += poly[i].x() * poly[j].y();
        area -= poly[j].x() * poly[i].y();
    }
    return qAbs( area ) * 0.5;
}

double PrimitiveClassifier::computeConvexity( const QVector<QVector3D> &xyProj,
                                                const QVector3D &bboxMin,
                                                const QVector3D &bboxMax )
{
    double bboxArea = ( bboxMax.x() - bboxMin.x() ) * ( bboxMax.y() - bboxMin.y() );
    if ( bboxArea < 0.0001 ) return 1.0;

    QVector<QVector3D> hull = convexHull2D( xyProj );
    double hullArea = polygonArea2D( hull );
    if ( hullArea < 0.0001 ) return 1.0;

    double convexity = bboxArea / hullArea;
    return qBound( 0.0, convexity, 1.0 );
}

// ====================================================================
// PCA：协方差矩阵特征值分解
// ====================================================================
void PrimitiveClassifier::computePCA( const QVector<QVector3D> &pts,
                                        QVector3D &eigenvalues,
                                        QVector3D *eigenvectors )
{
    int n = pts.size();
    if ( n < 3 ) { eigenvalues = QVector3D( 1, 1, 1 ); return; }

    double mx = 0, my = 0, mz = 0;
    for ( const QVector3D &p : pts ) { mx += p.x(); my += p.y(); mz += p.z(); }
    mx /= n; my /= n; mz /= n;

    double cxx = 0, cxy = 0, cxz = 0, cyy = 0, cyz = 0, czz = 0;
    for ( const QVector3D &p : pts )
    {
        double dx = p.x() - mx, dy = p.y() - my, dz = p.z() - mz;
        cxx += dx * dx; cxy += dx * dy; cxz += dx * dz;
        cyy += dy * dy; cyz += dy * dz; czz += dz * dz;
    }
    cxx /= n; cxy /= n; cxz /= n; cyy /= n; cyz /= n; czz /= n;

    // Jacobi iteration for 3x3 symmetric matrix
    double a[3][3] = {
        { cxx, cxy, cxz },
        { cxy, cyy, cyz },
        { cxz, cyz, czz }
    };
    double v[3][3] = { {1,0,0}, {0,1,0}, {0,0,1} };
    double e[3] = { a[0][0], a[1][1], a[2][2] };

    for ( int iter = 0; iter < 50; iter++ )
    {
        int p = 0, q = 1;
        double maxOff = qAbs( a[0][1] );
        if ( qAbs( a[0][2] ) > maxOff ) { maxOff = qAbs( a[0][2] ); p = 0; q = 2; }
        if ( qAbs( a[1][2] ) > maxOff ) { maxOff = qAbs( a[1][2] ); p = 1; q = 2; }
        if ( maxOff < 1e-10 ) break;

        double theta = 0.5 * std::atan2( 2 * a[p][q], a[q][q] - a[p][p] );
        double c = std::cos( theta ), s = std::sin( theta );

        double app = c * c * a[p][p] + 2 * s * c * a[p][q] + s * s * a[q][q];
        double aqq = s * s * a[p][p] - 2 * s * c * a[p][q] + c * c * a[q][q];

        a[p][p] = app;
        a[q][q] = aqq;
        a[p][q] = a[q][p] = 0;

        for ( int r = 0; r < 3; r++ )
        {
            if ( r == p || r == q ) continue;
            double arp = c * a[r][p] + s * a[r][q];
            double arq = -s * a[r][p] + c * a[r][q];
            a[r][p] = a[p][r] = arp;
            a[r][q] = a[q][r] = arq;
        }
        for ( int r = 0; r < 3; r++ )
        {
            double vrp = c * v[r][p] + s * v[r][q];
            double vrq = -s * v[r][p] + c * v[r][q];
            v[r][p] = vrp; v[r][q] = vrq;
        }
    }

    e[0] = a[0][0]; e[1] = a[1][1]; e[2] = a[2][2];

    // Sort descending
    int idx[3] = { 0, 1, 2 };
    if ( e[idx[0]] < e[idx[1]] ) std::swap( idx[0], idx[1] );
    if ( e[idx[0]] < e[idx[2]] ) std::swap( idx[0], idx[2] );
    if ( e[idx[1]] < e[idx[2]] ) std::swap( idx[1], idx[2] );

    eigenvalues = QVector3D( e[idx[0]], e[idx[1]], e[idx[2]] );
    eigenvectors[0] = QVector3D( v[0][idx[0]], v[1][idx[0]], v[2][idx[0]] );
    eigenvectors[1] = QVector3D( v[0][idx[1]], v[1][idx[1]], v[2][idx[1]] );
    eigenvectors[2] = QVector3D( v[0][idx[2]], v[1][idx[2]], v[2][idx[2]] );
}

// ====================================================================
// 镜像对称性评分（axis: 0=X左右, 1=Y前后）
// ====================================================================
double PrimitiveClassifier::computeSymmetry( const PointCloud &pc, int axis )
{
    double cx = ( pc.bboxMin.x() + pc.bboxMax.x() ) * 0.5;
    double cy = ( pc.bboxMin.y() + pc.bboxMax.y() ) * 0.5;

    // 体素化到 20x20 格网
    double bx = pc.bboxMax.x() - pc.bboxMin.x();
    double by = pc.bboxMax.y() - pc.bboxMin.y();
    double bz = pc.bboxMax.z() - pc.bboxMin.z();
    if ( bx < 0.001 || by < 0.001 ) return 0.0;

    const int G = 20;
    int grid[G][G] = {};
    for ( const QVector3D &p : pc.points )
    {
        int ix = qBound( 0, static_cast<int>( ( p.x() - pc.bboxMin.x() ) / bx * G ), G - 1 );
        int iy = qBound( 0, static_cast<int>( ( p.y() - pc.bboxMin.y() ) / by * G ), G - 1 );
        grid[ix][iy] = 1;
    }

    int match = 0, total = 0;
    for ( int i = 0; i < G; i++ )
    {
        for ( int j = 0; j < G; j++ )
        {
            int mi = ( axis == 0 ) ? ( G - 1 - i ) : i;
            int mj = ( axis == 1 ) ? ( G - 1 - j ) : j;
            if ( mi == i && mj == j ) continue;
            total++;
            if ( grid[i][j] == grid[mi][mj] ) match++;
        }
    }
    if ( total == 0 ) return 0.0;
    return static_cast<double>( match ) / total;
}

// ====================================================================
// 顶面斜率：最高 10% 点的 Z 标准差 / 整体高度
// ====================================================================
double PrimitiveClassifier::computeTopSlope( const PointCloud &pc, double topFrac )
{
    QVector<double> zVals;
    zVals.reserve( pc.points.size() );
    for ( const QVector3D &p : pc.points ) zVals.append( p.z() );
    std::sort( zVals.begin(), zVals.end() );

    int n = zVals.size();
    int start = static_cast<int>( n * ( 1.0 - topFrac ) );
    if ( start >= n ) start = n - 1;

    double sum = 0, sumSq = 0;
    int cnt = 0;
    for ( int i = start; i < n; i++ ) { sum += zVals[i]; sumSq += zVals[i] * zVals[i]; cnt++; }
    if ( cnt < 2 ) return 0.0;

    double mean = sum / cnt;
    double var = sumSq / cnt - mean * mean;
    if ( var < 0 ) var = 0;
    double std = std::sqrt( var );

    double hRange = pc.bboxMax.z() - pc.bboxMin.z();
    if ( hRange < 0.0001 ) return 0.0;
    return std / hRange;
}

// ====================================================================
// 顶面线性度：最高 5% 点投影到 XY 后的拟合残差
// ====================================================================
double PrimitiveClassifier::computeTopLinearity( const PointCloud &pc, double topFrac )
{
    QVector<double> zVals;
    zVals.reserve( pc.points.size() );
    for ( const QVector3D &p : pc.points ) zVals.append( p.z() );
    std::sort( zVals.begin(), zVals.end() );

    int n = zVals.size();
    int start = static_cast<int>( n * ( 1.0 - topFrac ) );
    if ( start >= n ) start = n - 1;

    QVector<QVector3D> topPts;
    double zThresh = zVals[start];
    for ( const QVector3D &p : pc.points )
        if ( p.z() >= zThresh ) topPts.append( QVector3D( p.x(), p.y(), 0 ) );

    if ( topPts.size() < 5 ) return 0.0;

    // 用 XY 坐标做 PCA，看第一主成分解释了多少方差
    double mx = 0, my = 0;
    for ( const QVector3D &p : topPts ) { mx += p.x(); my += p.y(); }
    mx /= topPts.size(); my /= topPts.size();

    double cxx = 0, cxy = 0, cyy = 0;
    for ( const QVector3D &p : topPts )
    {
        double dx = p.x() - mx, dy = p.y() - my;
        cxx += dx * dx; cxy += dx * dy; cyy += dy * dy;
    }
    double trace = cxx + cyy;
    if ( trace < 0.0001 ) return 0.0;

    double disc = std::sqrt( ( cxx - cyy ) * ( cxx - cyy ) + 4 * cxy * cxy );
    double lambda1 = 0.5 * ( trace + disc );
    return lambda1 / trace;  // 越接近 1 → 越接近一条线（脊线屋顶）
}

// ====================================================================
// 截面一致性：10 层高度的截面积变异系数
// ====================================================================
double PrimitiveClassifier::computeCrossSectionConsistency( const PointCloud &pc, int slices )
{
    double zMin = pc.bboxMin.z(), zMax = pc.bboxMax.z();
    double zRange = zMax - zMin;
    if ( zRange < 0.001 ) return 1.0;

    QVector<double> areas;
    for ( int s = 0; s < slices; s++ )
    {
        double zLo = zMin + zRange * s / slices;
        double zHi = zMin + zRange * ( s + 1 ) / slices;
        double bandThick = ( zHi - zLo ) * 0.5;
        double zMid = ( zLo + zHi ) * 0.5;

        // 收集这一层的 XY 投影点
        QVector<QVector3D> slicePts;
        for ( const QVector3D &p : pc.points )
            if ( p.z() >= zMid - bandThick && p.z() <= zMid + bandThick )
                slicePts.append( QVector3D( p.x(), p.y(), 0 ) );

        if ( slicePts.size() < 5 ) continue;

        QVector3D bmin = slicePts[0], bmax = slicePts[0];
        for ( const QVector3D &p : slicePts )
        {
            if ( p.x() < bmin.x() ) bmin.setX( p.x() );
            if ( p.y() < bmin.y() ) bmin.setY( p.y() );
            if ( p.x() > bmax.x() ) bmax.setX( p.x() );
            if ( p.y() > bmax.y() ) bmax.setY( p.y() );
        }
        double area = ( bmax.x() - bmin.x() ) * ( bmax.y() - bmin.y() );
        if ( area > 0 ) areas.append( area );
    }

    if ( areas.size() < 2 ) return 1.0;

    double sum = 0, sumSq = 0;
    for ( double a : areas ) { sum += a; sumSq += a * a; }
    double mean = sum / areas.size();
    double var = sumSq / areas.size() - mean * mean;
    if ( var < 0 ) var = 0;
    double cv = std::sqrt( var ) / ( mean + 0.0001 );
    return std::exp( -cv * 5.0 );  // 高一致性 → 接近 1
}

// ====================================================================
// 高度分段数：用高度直方图找峰值
// ====================================================================
int PrimitiveClassifier::countHeightStages( const PointCloud &pc )
{
    double zMin = pc.bboxMin.z(), zMax = pc.bboxMax.z();
    double zRange = zMax - zMin;
    if ( zRange < 0.001 ) return 1;

    const int BINS = 30;
    int hist[BINS] = {};
    for ( const QVector3D &p : pc.points )
    {
        int b = static_cast<int>( ( p.z() - zMin ) / zRange * BINS );
        if ( b >= BINS ) b = BINS - 1;
        if ( b < 0 ) b = 0;
        hist[b]++;
    }

    // Count peaks (bins higher than both neighbors and above average)
    double avg = 0;
    for ( int i = 0; i < BINS; i++ ) avg += hist[i];
    avg /= BINS;

    int peaks = 0;
    for ( int i = 1; i < BINS - 1; i++ )
        if ( hist[i] > hist[i-1] && hist[i] > hist[i+1] && hist[i] > avg * 1.2 )
            peaks++;

    return qMax( 1, peaks );
}

// ====================================================================
// 垂直分段数：基于截面半径变化
// ====================================================================
int PrimitiveClassifier::countVerticalSegments( const PointCloud &pc )
{
    double zMin = pc.bboxMin.z(), zMax = pc.bboxMax.z();
    double zRange = zMax - zMin;
    if ( zRange < 0.001 ) return 1;

    const int SLICES = 20;
    QVector<double> avgRadii;
    double cx = ( pc.bboxMin.x() + pc.bboxMax.x() ) * 0.5;
    double cy = ( pc.bboxMin.y() + pc.bboxMax.y() ) * 0.5;

    for ( int s = 0; s < SLICES; s++ )
    {
        double zLo = zMin + zRange * s / SLICES;
        double zHi = zMin + zRange * ( s + 1 ) / SLICES;
        double zMid = ( zLo + zHi ) * 0.5;
        double band = ( zHi - zLo ) * 0.5;

        double sumR = 0;
        int cnt = 0;
        for ( const QVector3D &p : pc.points )
        {
            if ( p.z() >= zMid - band && p.z() <= zMid + band )
            {
                double dx = p.x() - cx, dy = p.y() - cy;
                sumR += std::sqrt( dx * dx + dy * dy );
                cnt++;
            }
        }
        if ( cnt > 3 )
            avgRadii.append( sumR / cnt );
    }

    if ( avgRadii.size() < 3 ) return 1;

    // 检测显著变化（相邻层半径变化 > 15%）
    int segments = 1;
    for ( int i = 1; i < avgRadii.size(); i++ )
    {
        double prev = avgRadii[i-1];
        double cur = avgRadii[i];
        if ( prev > 0.001 && qAbs( cur - prev ) / prev > 0.15 )
            segments++;
    }
    return qMax( 1, qMin( segments, 5 ) );
}

// ====================================================================
// 各型期望特征配置
// ====================================================================
QVector<PrimitiveClassifier::TypeProfile> PrimitiveClassifier::buildProfiles()
{
    QVector<TypeProfile> profiles;

    // --- Cuboid ---
    {
        TypeProfile p;
        p.name = "Cuboid";
        p.expCircularity = 0.05;       p.weightCircularity = 2.0;
        p.expAspectRatio = 0.6;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.95;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.7;         p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.3;         p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.5;           p.weightHeight50 = 0.5;
        p.expHeight80 = 0.8;           p.weightHeight80 = 0.5;
        p.expTopSlope = 0.01;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.9;          p.weightSymmetryX = 2.0;
        p.expSymmetryY = 0.9;          p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.95;      p.weightCrossSection = 2.0;
        p.expStages = 1;               p.weightStages = 1.0;
        p.expRoofAngle = 0.65;         p.weightRoofAngle = 0.5;
        p.expLinearity = 0.3;          p.weightLinearity = 1.5;
        p.expVertSegments = 1;         p.weightVertSegments = 1.0;
        profiles.append( p );
    }

    // --- Cylinder ---
    {
        TypeProfile p;
        p.name = "Cylinder";
        p.expCircularity = 0.85;       p.weightCircularity = 3.0;
        p.expAspectRatio = 0.9;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.80;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.9;         p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.5;         p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.5;           p.weightHeight50 = 0.5;
        p.expHeight80 = 0.8;           p.weightHeight80 = 0.5;
        p.expTopSlope = 0.01;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.85;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.85;         p.weightSymmetryY = 1.5;
        p.expCrossSection = 0.9;       p.weightCrossSection = 2.0;
        p.expStages = 1;               p.weightStages = 1.0;
        p.expRoofAngle = 0.7;          p.weightRoofAngle = 0.5;
        p.expLinearity = 0.25;         p.weightLinearity = 1.5;
        p.expVertSegments = 1;         p.weightVertSegments = 1.0;
        profiles.append( p );
    }

    // --- LHouse ---
    {
        TypeProfile p;
        p.name = "LHouse";
        p.expCircularity = 0.1;        p.weightCircularity = 1.5;
        p.expAspectRatio = 0.5;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.55;         p.weightConvexity = 3.0;
        p.expPcaRatio12 = 0.65;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.3;         p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.5;           p.weightHeight50 = 0.5;
        p.expHeight80 = 0.8;           p.weightHeight80 = 0.5;
        p.expTopSlope = 0.01;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.5;          p.weightSymmetryX = 2.0;
        p.expSymmetryY = 0.5;          p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.85;      p.weightCrossSection = 1.5;
        p.expStages = 1;               p.weightStages = 1.0;
        p.expRoofAngle = 0.7;          p.weightRoofAngle = 0.5;
        p.expLinearity = 0.3;          p.weightLinearity = 1.0;
        p.expVertSegments = 1;         p.weightVertSegments = 1.0;
        profiles.append( p );
    }

    // --- ConeCylinder ---
    {
        TypeProfile p;
        p.name = "ConeCylinder";
        p.expCircularity = 0.80;       p.weightCircularity = 2.5;
        p.expAspectRatio = 0.9;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.80;         p.weightConvexity = 1.0;
        p.expPcaRatio12 = 0.9;         p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.5;         p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.6;           p.weightHeight50 = 1.0;
        p.expHeight80 = 0.7;           p.weightHeight80 = 1.5;
        p.expTopSlope = 0.06;          p.weightTopSlope = 2.5;
        p.expSymmetryX = 0.85;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.85;         p.weightSymmetryY = 1.5;
        p.expCrossSection = 0.6;       p.weightCrossSection = 2.0;
        p.expStages = 2;               p.weightStages = 2.0;
        p.expRoofAngle = 0.15;         p.weightRoofAngle = 2.0;
        p.expLinearity = 0.25;         p.weightLinearity = 1.0;
        p.expVertSegments = 2;         p.weightVertSegments = 2.0;
        profiles.append( p );
    }

    // --- GabledRoof ---
    {
        TypeProfile p;
        p.name = "GabledRoof";
        p.expCircularity = 0.1;        p.weightCircularity = 2.0;
        p.expAspectRatio = 0.5;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.92;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.6;         p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.4;         p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.55;          p.weightHeight50 = 1.0;
        p.expHeight80 = 0.75;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.15;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.7;          p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.9;          p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.55;      p.weightCrossSection = 2.0;
        p.expStages = 2;               p.weightStages = 1.5;
        p.expRoofAngle = 0.4;          p.weightRoofAngle = 2.0;
        p.expLinearity = 0.85;         p.weightLinearity = 3.0;
        p.expVertSegments = 2;         p.weightVertSegments = 1.5;
        profiles.append( p );
    }

    // --- PyramidRoof ---
    {
        TypeProfile p;
        p.name = "PyramidRoof";
        p.expCircularity = 0.1;        p.weightCircularity = 2.0;
        p.expAspectRatio = 0.6;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.90;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.65;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.4;         p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.55;          p.weightHeight50 = 1.0;
        p.expHeight80 = 0.72;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.10;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.85;         p.weightSymmetryX = 2.0;
        p.expSymmetryY = 0.85;         p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.55;      p.weightCrossSection = 2.0;
        p.expStages = 2;               p.weightStages = 1.5;
        p.expRoofAngle = 0.2;          p.weightRoofAngle = 2.5;
        p.expLinearity = 0.3;          p.weightLinearity = 2.5;
        p.expVertSegments = 2;         p.weightVertSegments = 1.5;
        profiles.append( p );
    }

    // --- TruncatedPyramidRoof ---
    {
        TypeProfile p;
        p.name = "TruncatedPyramidRoof";
        p.expCircularity = 0.1;        p.weightCircularity = 2.0;
        p.expAspectRatio = 0.6;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.90;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.65;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.4;         p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.55;          p.weightHeight50 = 1.0;
        p.expHeight80 = 0.75;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.02;          p.weightTopSlope = 2.5;
        p.expSymmetryX = 0.85;         p.weightSymmetryX = 2.0;
        p.expSymmetryY = 0.85;         p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.55;      p.weightCrossSection = 2.0;
        p.expStages = 2;               p.weightStages = 1.5;
        p.expRoofAngle = 0.35;         p.weightRoofAngle = 1.5;
        p.expLinearity = 0.3;          p.weightLinearity = 1.5;
        p.expVertSegments = 2;         p.weightVertSegments = 1.5;
        profiles.append( p );
    }

    // --- HalfCylinderRoof ---
    {
        TypeProfile p;
        p.name = "HalfCylinderRoof";
        p.expCircularity = 0.1;        p.weightCircularity = 2.0;
        p.expAspectRatio = 0.5;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.92;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.55;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.45;        p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.6;           p.weightHeight50 = 1.0;
        p.expHeight80 = 0.78;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.08;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.8;          p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.9;          p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.6;       p.weightCrossSection = 2.0;
        p.expStages = 2;               p.weightStages = 1.5;
        p.expRoofAngle = 0.3;          p.weightRoofAngle = 1.5;
        p.expLinearity = 0.75;         p.weightLinearity = 2.0;
        p.expVertSegments = 2;         p.weightVertSegments = 1.5;
        profiles.append( p );
    }

    // --- CylinderHemisphere ---
    {
        TypeProfile p;
        p.name = "CylinderHemisphere";
        p.expCircularity = 0.85;       p.weightCircularity = 3.0;
        p.expAspectRatio = 0.9;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.80;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.9;         p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.6;         p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.5;           p.weightHeight50 = 0.5;
        p.expHeight80 = 0.82;          p.weightHeight80 = 1.0;
        p.expTopSlope = 0.04;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.85;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.85;         p.weightSymmetryY = 1.5;
        p.expCrossSection = 0.7;       p.weightCrossSection = 2.0;
        p.expStages = 2;               p.weightStages = 2.0;
        p.expRoofAngle = 0.5;          p.weightRoofAngle = 1.0;
        p.expLinearity = 0.2;          p.weightLinearity = 2.0;
        p.expVertSegments = 2;         p.weightVertSegments = 2.0;
        profiles.append( p );
    }

    // --- IndentedCuboid ---
    {
        TypeProfile p;
        p.name = "IndentedCuboid";
        p.expCircularity = 0.1;        p.weightCircularity = 1.5;
        p.expAspectRatio = 0.5;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.70;         p.weightConvexity = 3.0;
        p.expPcaRatio12 = 0.65;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.25;        p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.5;           p.weightHeight50 = 0.5;
        p.expHeight80 = 0.8;           p.weightHeight80 = 0.5;
        p.expTopSlope = 0.01;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.7;          p.weightSymmetryX = 2.0;
        p.expSymmetryY = 0.7;          p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.75;      p.weightCrossSection = 2.0;
        p.expStages = 1;               p.weightStages = 1.0;
        p.expRoofAngle = 0.65;         p.weightRoofAngle = 0.5;
        p.expLinearity = 0.3;          p.weightLinearity = 1.0;
        p.expVertSegments = 1;         p.weightVertSegments = 1.0;
        profiles.append( p );
    }

    // --- AsymmetricGableHouse ---
    {
        TypeProfile p;
        p.name = "AsymmetricGableHouse";
        p.expCircularity = 0.1;        p.weightCircularity = 2.0;
        p.expAspectRatio = 0.5;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.90;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.55;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.4;         p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.55;          p.weightHeight50 = 1.0;
        p.expHeight80 = 0.75;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.12;          p.weightTopSlope = 2.0;
        p.expSymmetryX = 0.55;         p.weightSymmetryX = 3.0;
        p.expSymmetryY = 0.9;          p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.55;      p.weightCrossSection = 2.0;
        p.expStages = 2;               p.weightStages = 1.5;
        p.expRoofAngle = 0.4;          p.weightRoofAngle = 1.5;
        p.expLinearity = 0.85;         p.weightLinearity = 3.0;
        p.expVertSegments = 2;         p.weightVertSegments = 1.5;
        profiles.append( p );
    }

    // --- FourStageRoundTower ---
    {
        TypeProfile p;
        p.name = "FourStageRoundTower";
        p.expCircularity = 0.80;       p.weightCircularity = 3.0;
        p.expAspectRatio = 0.9;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.78;         p.weightConvexity = 1.5;
        p.expPcaRatio12 = 0.88;        p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.3;         p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.4;           p.weightHeight50 = 1.0;
        p.expHeight80 = 0.7;           p.weightHeight80 = 1.0;
        p.expTopSlope = 0.05;          p.weightTopSlope = 1.5;
        p.expSymmetryX = 0.82;         p.weightSymmetryX = 1.5;
        p.expSymmetryY = 0.82;         p.weightSymmetryY = 1.5;
        p.expCrossSection = 0.45;      p.weightCrossSection = 2.5;
        p.expStages = 3;               p.weightStages = 2.0;
        p.expRoofAngle = 0.2;          p.weightRoofAngle = 1.5;
        p.expLinearity = 0.25;         p.weightLinearity = 1.5;
        p.expVertSegments = 3;         p.weightVertSegments = 3.0;
        profiles.append( p );
    }

    // --- TwoGableHouses ---
    {
        TypeProfile p;
        p.name = "TwoGableHouses";
        p.expCircularity = 0.1;        p.weightCircularity = 2.0;
        p.expAspectRatio = 0.4;        p.weightAspectRatio = 0.5;
        p.expConvexity = 0.72;         p.weightConvexity = 3.0;
        p.expPcaRatio12 = 0.5;         p.weightPcaRatio12 = 1.0;
        p.expPcaRatio23 = 0.35;        p.weightPcaRatio23 = 1.0;
        p.expHeight50 = 0.55;          p.weightHeight50 = 1.0;
        p.expHeight80 = 0.72;          p.weightHeight80 = 1.5;
        p.expTopSlope = 0.14;          p.weightTopSlope = 1.5;
        p.expSymmetryX = 0.55;         p.weightSymmetryX = 2.5;
        p.expSymmetryY = 0.75;         p.weightSymmetryY = 2.0;
        p.expCrossSection = 0.55;      p.weightCrossSection = 2.0;
        p.expStages = 2;               p.weightStages = 1.5;
        p.expRoofAngle = 0.35;         p.weightRoofAngle = 1.5;
        p.expLinearity = 0.65;         p.weightLinearity = 2.0;
        p.expVertSegments = 2;         p.weightVertSegments = 1.5;
        profiles.append( p );
    }

    return profiles;
}

// ====================================================================
// 评分函数：特征向量 vs 类型配置，返回 0-1 的匹配度
// ====================================================================
double PrimitiveClassifier::scoreProfile( const FeatureVector &fv, const TypeProfile &tp )
{
    auto gaussScore = []( double val, double expected, double sigma ) -> double
    {
        double diff = val - expected;
        return std::exp( -0.5 * ( diff * diff ) / ( sigma * sigma ) );
    };

    double totalWeight = 0, weightedScore = 0;

    auto addScore = [&]( double weight, double score )
    {
        if ( weight <= 0 ) return;
        totalWeight += weight;
        weightedScore += weight * score;
    };

    addScore( tp.weightCircularity,     gaussScore( fv.footprintCircularity, tp.expCircularity, 0.25 ) );
    addScore( tp.weightAspectRatio,     gaussScore( fv.footprintAspectRatio, tp.expAspectRatio, 0.30 ) );
    addScore( tp.weightConvexity,       gaussScore( fv.footprintConvexity,   tp.expConvexity,   0.20 ) );
    addScore( tp.weightPcaRatio12,      gaussScore( fv.pcaRatio12,           tp.expPcaRatio12,   0.25 ) );
    addScore( tp.weightPcaRatio23,      gaussScore( fv.pcaRatio23,           tp.expPcaRatio23,   0.25 ) );
    addScore( tp.weightHeight50,        gaussScore( fv.heightRatio50,        tp.expHeight50,     0.15 ) );
    addScore( tp.weightHeight80,        gaussScore( fv.heightRatio80,        tp.expHeight80,     0.15 ) );
    addScore( tp.weightTopSlope,        gaussScore( fv.topSlope,             tp.expTopSlope,     0.08 ) );
    addScore( tp.weightSymmetryX,       gaussScore( fv.symmetryX,            tp.expSymmetryX,    0.20 ) );
    addScore( tp.weightSymmetryY,       gaussScore( fv.symmetryY,            tp.expSymmetryY,    0.20 ) );
    addScore( tp.weightCrossSection,    gaussScore( fv.crossSectionConsistency, tp.expCrossSection, 0.20 ) );
    addScore( tp.weightStages,          gaussScore( static_cast<double>( fv.numStages ), static_cast<double>( tp.expStages ), 1.0 ) );
    addScore( tp.weightRoofAngle,       gaussScore( fv.roofAngle,            tp.expRoofAngle,    0.20 ) );
    addScore( tp.weightLinearity,       gaussScore( fv.topLinearity,         tp.expLinearity,    0.25 ) );
    addScore( tp.weightVertSegments,    gaussScore( static_cast<double>( fv.numVerticalSegments ), static_cast<double>( tp.expVertSegments ), 1.0 ) );

    if ( totalWeight < 0.0001 ) return 0.0;
    return weightedScore / totalWeight;
}

// ====================================================================
// 分类决策
// ====================================================================
PrimitiveClassifier::Result
PrimitiveClassifier::classifyByScore( const FeatureVector &fv )
{
    QVector<TypeProfile> profiles = buildProfiles();

    Result best;
    best.primitiveType = "Cuboid";
    best.confidence = 0.0;

    double bestScore = -1;
    double secondBestScore = -1;

    for ( const TypeProfile &tp : profiles )
    {
        double s = scoreProfile( fv, tp );
        if ( s > bestScore )
        {
            secondBestScore = bestScore;
            bestScore = s;
            best.primitiveType = tp.name;
        }
        else if ( s > secondBestScore )
        {
            secondBestScore = s;
        }
    }

    // 置信度 = 最佳分数，并考虑与第二名的差距
    double margin = bestScore - secondBestScore;
    best.confidence = bestScore * ( 0.7 + 0.3 * qBound( 0.0, margin * 2.0, 1.0 ) );
    best.confidence = qBound( 0.0, best.confidence, 1.0 );

    return best;
}
