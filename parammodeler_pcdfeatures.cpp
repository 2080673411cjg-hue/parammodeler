/***************************************************************************
  parammodeler_pcdfeatures.cpp
  Point Cloud Feature Extraction (preprocessing + 15 features)
  -------------------
         begin                : May 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "parammodeler_pcdfeatures.h"

#include <QLineF>
#include <QPointF>
#include <QtMath>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <set>
#include <numeric>
#include <random>
#include <windows.h>
#define DEBUG_LOG(msg) OutputDebugStringW(msg)

// ====================================================================
// 体素降采样
// ====================================================================
PointCloud FeatureExtractor::downsample(
  const PointCloud &pc, int targetPoints
)
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

  QVector<int> indices( n );
  std::iota( indices.begin(), indices.end(), 0 );

  std::mt19937 rng( 42 );
  std::shuffle( indices.begin(), indices.end(), rng );

  result.points.reserve( targetPoints );
  for ( int i = 0; i < targetPoints; i++ )
    result.points.append( pc.points[indices[i]] );

  return result;
}

// ====================================================================
// 曲率关键点滤波：基于双半径法向量夹角保留几何语义最丰富的点
// ====================================================================
PointCloud FeatureExtractor::curvatureFilter(
  const PointCloud &pc, double R1, double R2, int targetPoints
)
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

    // 构建空间哈希网格加速邻域搜索
    double gridSize = R2;
    if ( gridSize < 0.001 ) gridSize = 0.1;

    auto hashKey = [&]( int ix, int iy, int iz ) -> qint64 {
        return ( static_cast<qint64>( ix + 50000 ) << 40 ) |
               ( static_cast<qint64>( iy + 50000 ) << 20 ) |
               static_cast<qint64>( iz + 50000 );
    };

    QMap<qint64, QVector<int>> grid;
    for ( int i = 0; i < n; i++ )
    {
        int ix = static_cast<int>( std::floor( pc.points[i].x() / gridSize ) );
        int iy = static_cast<int>( std::floor( pc.points[i].y() / gridSize ) );
        int iz = static_cast<int>( std::floor( pc.points[i].z() / gridSize ) );
        grid[hashKey( ix, iy, iz )].append( i );
    }

    auto findNeighbors = [&]( int idx, double radius ) -> QVector<int> {
        QVector<int> result;
        double px = pc.points[idx].x(), py = pc.points[idx].y(), pz = pc.points[idx].z();
        int cx = static_cast<int>( std::floor( px / gridSize ) );
        int cy = static_cast<int>( std::floor( py / gridSize ) );
        int cz = static_cast<int>( std::floor( pz / gridSize ) );
        int cellRadius = static_cast<int>( std::ceil( radius / gridSize ) );
        double r2 = radius * radius;

        for ( int dx = -cellRadius; dx <= cellRadius; dx++ )
            for ( int dy = -cellRadius; dy <= cellRadius; dy++ )
                for ( int dz = -cellRadius; dz <= cellRadius; dz++ )
                {
                    auto it = grid.find( hashKey( cx + dx, cy + dy, cz + dz ) );
                    if ( it == grid.end() ) continue;
                    for ( int j : it.value() )
                    {
                        double ddx = pc.points[j].x() - px;
                        double ddy = pc.points[j].y() - py;
                        double ddz = pc.points[j].z() - pz;
                        if ( ddx * ddx + ddy * ddy + ddz * ddz <= r2 )
                            result.append( j );
                    }
                }
        return result;
    };

    // 对每个点计算双半径曲率得分
    struct ScoredPoint { int idx; double score; };
    QVector<ScoredPoint> scores;
    scores.reserve( n );

    for ( int i = 0; i < n; i++ )
    {
        // --- 半径 R1 的法向量估计（通过协方差矩阵最小特征值方向） ---
        QVector<int> nb1 = findNeighbors( i, R1 );
        double curvature1 = 0;
        double nx1 = 0, ny1 = 0, nz1 = 1;

        if ( nb1.size() >= 4 )
        {
            double mx = 0, my = 0, mz = 0;
            for ( int j : nb1 ) { mx += pc.points[j].x(); my += pc.points[j].y(); mz += pc.points[j].z(); }
            int cnt = nb1.size();
            mx /= cnt; my /= cnt; mz /= cnt;

            double cxx = 0, cxy = 0, cxz = 0, cyy = 0, cyz = 0, czz = 0;
            for ( int j : nb1 )
            {
                double dx = pc.points[j].x() - mx, dy = pc.points[j].y() - my, dz = pc.points[j].z() - mz;
                cxx += dx*dx; cxy += dx*dy; cxz += dx*dz;
                cyy += dy*dy; cyz += dy*dz; czz += dz*dz;
            }

            double a = cxx, b = cxy, c = cxz;
            double e = cyy, f = cyz;
            double h = czz;

            double v1x = e * h - f * f;
            double v1y = c * f - b * h;
            double v1z = b * f - c * e;
            double len = std::sqrt( v1x*v1x + v1y*v1y + v1z*v1z );
            if ( len > 1e-10 )
            {
                nx1 = v1x / len; ny1 = v1y / len; nz1 = v1z / len;
            }

            double trace = a + e + h;
            curvature1 = ( trace > 1e-10 ) ? ( len / ( trace * trace ) ) : 0;
        }

        // --- 半径 R2 的法向量 ---
        QVector<int> nb2 = findNeighbors( i, R2 );
        double curvature2 = 0;
        double nx2 = 0, ny2 = 0, nz2 = 1;

        if ( nb2.size() >= 4 )
        {
            double mx = 0, my = 0, mz = 0;
            for ( int j : nb2 ) { mx += pc.points[j].x(); my += pc.points[j].y(); mz += pc.points[j].z(); }
            int cnt = nb2.size();
            mx /= cnt; my /= cnt; mz /= cnt;

            double cxx = 0, cxy = 0, cxz = 0, cyy = 0, cyz = 0, czz = 0;
            for ( int j : nb2 )
            {
                double dx = pc.points[j].x() - mx, dy = pc.points[j].y() - my, dz = pc.points[j].z() - mz;
                cxx += dx*dx; cxy += dx*dy; cxz += dx*dz;
                cyy += dy*dy; cyz += dy*dz; czz += dz*dz;
            }

            double a = cxx, b = cxy, c = cxz;
            double e = cyy, f = cyz;
            double h = czz;

            double v1x = e * h - f * f;
            double v1y = c * f - b * h;
            double v1z = b * f - c * e;
            double len = std::sqrt( v1x*v1x + v1y*v1y + v1z*v1z );
            if ( len > 1e-10 )
            {
                nx2 = v1x / len; ny2 = v1y / len; nz2 = v1z / len;
            }

            double trace = a + e + h;
            curvature2 = ( trace > 1e-10 ) ? ( len / ( trace * trace ) ) : 0;
        }

        // 双半径法向量夹角
        double dot = qBound( -1.0, nx1*nx2 + ny1*ny2 + nz1*nz2, 1.0 );
        double angle = std::acos( qAbs( dot ) );

        // 综合得分：曲率差异 + 法向量夹角
        double score = qAbs( curvature1 - curvature2 ) * 10.0 + angle;
        scores.append( { i, score } );
    }

    // 按得分降序排列，取前 targetPoints 个
    std::sort( scores.begin(), scores.end(),
               []( const ScoredPoint &a, const ScoredPoint &b ) { return a.score > b.score; } );

    result.points.reserve( targetPoints );
    for ( int i = 0; i < targetPoints && i < scores.size(); i++ )
        result.points.append( pc.points[scores[i].idx] );

    return result;
}

// ====================================================================
// 2D 几何辅助函数（file-scope static）
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

// ====================================================================
// 圆度：基于径向距离的变异系数
// ====================================================================
static double computeCircularity( const QVector<QVector3D> &xyProj,
                                  const QVector3D &bboxMin,
                                  const QVector3D &bboxMax )
{
    Q_UNUSED(bboxMin); Q_UNUSED(bboxMax);
    double cx = 0, cy = 0;
    for ( const QVector3D &p : xyProj ) { cx += p.x(); cy += p.y(); }
    cx /= xyProj.size();
    cy /= xyProj.size();

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

    int lo = n * 4 / 5, hi = n * 19 / 20;
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

    double circularity = std::exp( -cv * 20.0 );
    return qBound( 0.0, circularity, 1.0 );
}

// ====================================================================
// 凸度：凸包面积 / 包围盒面积比
// ====================================================================
static double computeConvexity( const QVector<QVector3D> &xyProj,
                                const QVector3D &bboxMin,
                                const QVector3D &bboxMax )
{
  double bx = bboxMax.x() - bboxMin.x();
  double by = bboxMax.y() - bboxMin.y();
  if ( bx < 0.001 || by < 0.001 )
    return 1.0;
  if ( xyProj.size() < 10 )
    return 0.5;

  QVector<QVector3D> hull = convexHull2D( xyProj );
  double hullArea = polygonArea2D( hull );

  double bboxArea = bx * by;
  if ( bboxArea < 1e-12 ) return 1.0;

  double result = hullArea / bboxArea;

  QString convLog = QString( "[Convexity] hullPts=%1 hullArea=%2 bboxArea=%3 result=%4\n" )
                 .arg( hull.size() )
                 .arg( hullArea, 0, 'f', 4 )
                 .arg( bboxArea, 0, 'f', 4 )
                 .arg( result, 0, 'f', 4 );
  DEBUG_LOG( convLog.toStdWString().c_str() );
  QFile convFile( QDir::tempPath() + "/parammodeler_classify.log" );
  if ( convFile.open( QIODevice::Append | QIODevice::Text ) )
  {
    QTextStream ts( &convFile );
    ts << convLog;
    convFile.close();
  }
  return qBound( 0.0, result, 1.0 );
}

// ====================================================================
// PCA：协方差矩阵特征值分解
// ====================================================================
static void computePCA( const QVector<QVector3D> &pts,
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
static double computeSymmetry( const PointCloud &pc, int axis )
{
    double cx = 0, cy = 0;
    for ( const QVector3D &p : pc.points ) { cx += p.x(); cy += p.y(); }
    cx /= pc.points.size();
    cy /= pc.points.size();

    int n = pc.points.size();
    int step = qMax( 1, n / 2000 );
    double threshold = qMax( pc.bboxMax.x() - pc.bboxMin.x(),
                             pc.bboxMax.y() - pc.bboxMin.y() ) * 0.03;

    int match = 0, total = 0;
    for ( int i = 0; i < n; i += step )
    {
        const QVector3D &p = pc.points[i];
        double mx = ( axis == 0 ) ? ( 2.0 * cx - p.x() ) : p.x();
        double my = ( axis == 1 ) ? ( 2.0 * cy - p.y() ) : p.y();

        bool found = false;
        for ( int j = 0; j < n; j += step )
        {
            double dx = pc.points[j].x() - mx;
            double dy = pc.points[j].y() - my;
            if ( dx * dx + dy * dy < threshold * threshold )
            {
                found = true;
                break;
            }
        }
        total++;
        if ( found ) match++;
    }
    if ( total == 0 ) return 0.0;
    return static_cast<double>( match ) / total;
}

// ====================================================================
// 顶面斜率：最高 10% 点的 Z 标准差 / 整体高度
// ====================================================================
static double computeTopSlope( const PointCloud &pc, double topFrac = 0.10 )
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
// 顶面线性度：最高 5% 点投影到 XY 后的 PCA
// ====================================================================
static double computeTopLinearity( const PointCloud &pc, double topFrac = 0.05 )
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
    return lambda1 / trace;
}

// ====================================================================
// 截面一致性：10 层高度的截面积变异系数
// ====================================================================
static double computeCrossSectionConsistency( const PointCloud &pc, int slices = 10 )
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

        QVector<QVector3D> slicePts;
        for ( const QVector3D &p : pc.points )
            if ( p.z() >= zMid - bandThick && p.z() <= zMid + bandThick )
                slicePts.append( QVector3D( p.x(), p.y(), 0 ) );

        if ( slicePts.size() < 5 ) continue;

        QVector<QVector3D> hull = convexHull2D( slicePts );
        double area = polygonArea2D( hull );
        if ( area > 0 ) areas.append( area );
    }

    if ( areas.size() < 2 ) return 1.0;

    double sum = 0, sumSq = 0;
    for ( double a : areas ) { sum += a; sumSq += a * a; }
    double mean = sum / areas.size();
    double var = sumSq / areas.size() - mean * mean;
    if ( var < 0 ) var = 0;
    double cv = std::sqrt( var ) / ( mean + 0.0001 );
    return std::exp( -cv * 5.0 );
}

// ====================================================================
// 高度分段数：基于截面宽度变化检测阶段边界
// ====================================================================
static int countHeightStages( const PointCloud &pc )
{
    double zMin = pc.bboxMin.z(), zMax = pc.bboxMax.z();
    double zRange = zMax - zMin;
    if ( zRange < 0.001 ) return 1;

    const int SLICES = 20;
    QVector<double> widths;

    for ( int s = 0; s < SLICES; s++ )
    {
        double zLo = zMin + zRange * s / SLICES;
        double zHi = zMin + zRange * ( s + 1 ) / SLICES;
        double zMid = ( zLo + zHi ) * 0.5;
        double band = ( zHi - zLo ) * 0.5;

        double xMin = 1e30, xMax = -1e30;
        double yMin = 1e30, yMax = -1e30;
        for ( const QVector3D &p : pc.points )
        {
            if ( p.z() >= zMid - band && p.z() <= zMid + band )
            {
                if ( p.x() < xMin ) xMin = p.x();
                if ( p.x() > xMax ) xMax = p.x();
                if ( p.y() < yMin ) yMin = p.y();
                if ( p.y() > yMax ) yMax = p.y();
            }
        }
        double wx = ( xMax > xMin ) ? ( xMax - xMin ) : 0;
        double wy = ( yMax > yMin ) ? ( yMax - yMin ) : 0;
        double w = qMax( wx, wy );
        widths.append( w );
    }

    QVector<double> diff;
    for ( int i = 1; i < widths.size(); i++ )
        diff.append( widths[i] - widths[i-1] );

    QVector<double> smoothed;
    for ( int i = 0; i < diff.size(); i++ )
    {
        double sum = diff[i];
        int cnt = 1;
        if ( i > 0 ) { sum += diff[i-1]; cnt++; }
        if ( i < diff.size() - 1 ) { sum += diff[i+1]; cnt++; }
        smoothed.append( sum / cnt );
    }

    double wMin = 1e30, wMax = -1e30;
    for ( double w : widths ) { if ( w < wMin ) wMin = w; if ( w > wMax ) wMax = w; }
    double wRange = wMax - wMin;
    double threshold = wRange * 0.30;
    if ( threshold < 0.01 ) threshold = 0.01;

    struct Segment { int start, end; };
    QVector<Segment> segments;
    int segStart = 0;
    for ( int i = 0; i < smoothed.size(); i++ )
    {
        if ( qAbs( smoothed[i] ) > threshold )
        {
            segments.append( { segStart, i } );
            segStart = i + 1;
        }
    }
    segments.append( { segStart, SLICES - 1 } );

    double minLen = SLICES * 0.15;
    QVector<Segment> merged;
    for ( const Segment &s : segments )
    {
        int len = s.end - s.start + 1;
        if ( len < minLen && !merged.isEmpty() )
        {
            merged.last().end = s.end;
        }
        else
        {
            merged.append( s );
        }
    }

    return qMax( 1, merged.size() );
}

// ====================================================================
// 垂直分段数：基于截面半径变化率检测
// ====================================================================
static int countVerticalSegments( const PointCloud &pc )
{
    double zMin = pc.bboxMin.z(), zMax = pc.bboxMax.z();
    double zRange = zMax - zMin;
    if ( zRange < 0.001 ) return 1;

    const int SLICES = 20;
    QVector<double> avgRadii;

    double cx = 0, cy = 0;
    for ( const QVector3D &p : pc.points ) { cx += p.x(); cy += p.y(); }
    cx /= pc.points.size();
    cy /= pc.points.size();

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

    QVector<double> rateOfChange;
    for ( int i = 1; i < avgRadii.size(); i++ )
    {
        double prev = avgRadii[i-1];
        double cur = avgRadii[i];
        double roc = ( prev > 0.001 ) ? qAbs( cur - prev ) / prev : 0;
        rateOfChange.append( roc );
    }

    QVector<double> smoothed;
    for ( int i = 0; i < rateOfChange.size(); i++ )
    {
        double sum = rateOfChange[i];
        int cnt = 1;
        if ( i > 0 ) { sum += rateOfChange[i-1]; cnt++; }
        if ( i < rateOfChange.size() - 1 ) { sum += rateOfChange[i+1]; cnt++; }
        smoothed.append( sum / cnt );
    }

    QVector<double> sortedROC = smoothed;
    std::sort( sortedROC.begin(), sortedROC.end() );
    double medianROC = sortedROC[sortedROC.size() / 2];
    double threshold = qMax( medianROC * 3.0, 0.10 );

    struct Segment { int start, end; };
    QVector<Segment> segments;
    int segStart = 0;
    for ( int i = 0; i < smoothed.size(); i++ )
    {
        if ( smoothed[i] > threshold )
        {
            segments.append( { segStart, i } );
            segStart = i + 1;
        }
    }
    segments.append( { segStart, SLICES - 1 } );

    double minLen = SLICES * 0.15;
    QVector<Segment> merged;
    for ( const Segment &s : segments )
    {
        int len = s.end - s.start + 1;
        if ( len < minLen && !merged.isEmpty() )
        {
            merged.last().end = s.end;
        }
        else
        {
            merged.append( s );
        }
    }

    // 后处理：合并半径变化小于均值15%的段
    if ( merged.size() > 1 )
    {
        double overallMean = 0;
        int totalCnt = 0;
        for ( double r : avgRadii ) { overallMean += r; totalCnt++; }
        if ( totalCnt > 0 ) overallMean /= totalCnt;

        QVector<Segment> finalMerged;
        finalMerged.append( merged[0] );
        for ( int i = 1; i < merged.size(); i++ )
        {
            int boundaryIdx = merged[i].start;
            if ( boundaryIdx > 0 && boundaryIdx < avgRadii.size() )
            {
                double rBefore = avgRadii[boundaryIdx - 1];
                double rAfter = avgRadii[boundaryIdx];
                double relChange = ( overallMean > 0.001 )
                    ? qAbs( rAfter - rBefore ) / overallMean : 0;
                if ( relChange < 0.15 )
                {
                    finalMerged.last().end = merged[i].end;
                    continue;
                }
            }
            finalMerged.append( merged[i] );
        }
        merged = finalMerged;
    }

    return qMax( 1, qMin( merged.size(), 5 ) );
}

// ====================================================================
// 特征提取入口
// ====================================================================
FeatureVector FeatureExtractor::extract( const PointCloud &pc )
{
    FeatureVector fv;
    if ( pc.points.size() < 10 ) return fv;

    const QVector<QVector3D> &pts = pc.points;
    QVector3D size = pc.bboxMax - pc.bboxMin;

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

    // 3. 足迹凸度
    double zRange = pc.bboxMax.z() - pc.bboxMin.z();
    double zTopThreshold = pc.bboxMax.z() - zRange * 0.05;
    QVector<QVector3D> topXyProj;
    for ( const QVector3D &p : pts )
    {
        if ( p.z() >= zTopThreshold )
            topXyProj.append( QVector3D( p.x(), p.y(), 0 ) );
    }
    fv.footprintConvexity = computeConvexity( topXyProj, pc.bboxMin, pc.bboxMax );

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
