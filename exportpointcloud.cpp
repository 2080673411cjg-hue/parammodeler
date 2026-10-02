/***************************************************************************
  exportpointcloud.cpp
  -------------------
         begin                : Mar. 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "exportpointcloud.h"
#include "buildmesh.h"
#include "parammodeler_dock.h"

#include <QDir>
#include <QFile>
#include <QMatrix4x4>
#include <QMessageBox>
#include <QSet>
#include <QTextStream>
#include <QVector3D>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstdlib>

static QVector3D sampleTriangle( const QVector3D &A, const QVector3D &B, const QVector3D &C )
{
  float r1 = float( qrand() ) / RAND_MAX;
  float r2 = float( qrand() ) / RAND_MAX;
  float sqr1 = qSqrt( r1 );
  return ( 1.0f - sqr1 ) * A
         + sqr1 * ( 1.0f - r2 ) * B
         + sqr1 * r2 * C;
}

static float triangleArea( const QVector3D &A, const QVector3D &B, const QVector3D &C )
{
  return QVector3D::crossProduct( B - A, C - A ).length() * 0.5f;
}

static QVector3D applyPose( const QVector3D &v, double tx, double ty, double tz, double rx, double ry, double rz )
{
  QMatrix4x4 mat;
  mat.setToIdentity();
  mat.translate( tx, ty, tz );
  mat.rotate( rx, 1, 0, 0 );
  mat.rotate( ry, 0, 1, 0 );
  mat.rotate( rz, 0, 0, 1 );
  return mat.map( v );
}

static QVector<QVector3D> sampleGroup( const MeshData &mesh, const QVector<int> &tris, int n )
{
  QVector<QVector3D> pts;
  if ( tris.isEmpty() || n <= 0 )
    return pts;

  QVector<float> cum( tris.size() );
  float total = 0.0f;
  for ( int k = 0; k < tris.size(); k++ )
  {
    int i = tris[k];
    QVector3D A = mesh.vertices[mesh.indices[i * 3]];
    QVector3D B = mesh.vertices[mesh.indices[i * 3 + 1]];
    QVector3D C = mesh.vertices[mesh.indices[i * 3 + 2]];
    total += triangleArea( A, B, C );
    cum[k] = total;
  }
  if ( total <= 0.0f )
    return pts;

  pts.reserve( n );
  for ( int s = 0; s < n; s++ )
  {
    float r = float( qrand() ) / RAND_MAX * total;
    int lo = 0;
    int hi = tris.size() - 1;
    while ( lo < hi )
    {
      int mid = ( lo + hi ) / 2;
      if ( cum[mid] < r )
        lo = mid + 1;
      else
        hi = mid;
    }

    int i = tris[lo];
    QVector3D A = mesh.vertices[mesh.indices[i * 3]];
    QVector3D B = mesh.vertices[mesh.indices[i * 3 + 1]];
    QVector3D C = mesh.vertices[mesh.indices[i * 3 + 2]];
    pts << sampleTriangle( A, B, C );
  }
  return pts;
}

static void resampleToFixedCount( QVector<QVector3D> &points, int targetCount )
{
  if ( targetCount <= 0 || points.isEmpty() )
    return;

  while ( points.size() > targetCount )
  {
    const int idx = qrand() % points.size();
    points.removeAt( idx );
  }

  const int sourceCount = points.size();
  while ( points.size() < targetCount && sourceCount > 0 )
  {
    const int idx = qrand() % sourceCount;
    points.append( points.at( idx ) );
  }
}

static QVector<QVector3D> sampleCurrentPrimitive( const QString &primitiveType,
                                                  ParamModelerDock *dock,
                                                  int sampleCount,
                                                  bool skipBottom,
                                                  QString *errorMessage )
{
  QVector<QVector3D> points;
  MeshData mesh = BuildMesh::build( primitiveType, dock );
  if ( mesh.isEmpty() )
  {
    if ( errorMessage )
      *errorMessage = QString( "Primitive \"%1\" cannot generate a valid mesh." ).arg( primitiveType );
    return points;
  }

  int triCount = mesh.indices.size() / 3;
  if ( triCount == 0 )
  {
    if ( errorMessage )
      *errorMessage = "Mesh has no triangles to sample.";
    return points;
  }

  QVector<int> horzTris;
  QVector<int> sideTris;
  for ( int i = 0; i < triCount; i++ )
  {
    QVector3D A = mesh.vertices[mesh.indices[i * 3]];
    QVector3D B = mesh.vertices[mesh.indices[i * 3 + 1]];
    QVector3D C = mesh.vertices[mesh.indices[i * 3 + 2]];
    QVector3D n = QVector3D::crossProduct( B - A, C - A ).normalized();
    if ( n.z() > 0.7f )
      horzTris << i;
    else if ( skipBottom && n.z() < -0.7f )
      continue;
    else
      sideTris << i;
  }

  int sideCount = 0;
  int horzCount = 0;
  if ( sideTris.isEmpty() )
  {
    horzCount = sampleCount;
  }
  else if ( horzTris.isEmpty() )
  {
    sideCount = sampleCount;
  }
  else
  {
    sideCount = qMax( int( sampleCount * 0.40 ), sampleCount / 4 );
    horzCount = sampleCount - sideCount;
  }

  points << sampleGroup( mesh, horzTris, horzCount );
  points << sampleGroup( mesh, sideTris, sideCount );

  double tx = dock->poseTranslateX();
  double ty = dock->poseTranslateY();
  double tz = dock->poseTranslateZ();
  double rx = dock->poseRotateX();
  double ry = dock->poseRotateY();
  double rz = dock->poseRotateZ();
  bool hasPose = ( tx != 0 || ty != 0 || tz != 0 || rx != 0 || ry != 0 || rz != 0 );
  if ( hasPose )
  {
    for ( QVector3D &p : points )
      p = applyPose( p, tx, ty, tz, rx, ry, rz );
  }

  if ( points.isEmpty() && errorMessage )
    *errorMessage = "Sampling produced no points.";

  return points;
}

static void normalizeForDL( QVector<QVector3D> &points )
{
  if ( points.isEmpty() )
    return;

  QVector3D center( 0, 0, 0 );
  for ( const QVector3D &p : points )
    center += p;
  center /= float( points.size() );

  float maxRadius = 0.0f;
  for ( const QVector3D &p : points )
    maxRadius = qMax( maxRadius, ( p - center ).length() );
  if ( maxRadius <= 1e-8f )
    maxRadius = 1.0f;

  for ( QVector3D &p : points )
    p = ( p - center ) / maxRadius;
}

static DLPointCloudInfo computeDLPointCloudInfo( const QVector<QVector3D> &points )
{
  DLPointCloudInfo info;
  if ( points.isEmpty() )
    return info;

  info.bboxMin = points.first();
  info.bboxMax = points.first();
  info.center = QVector3D( 0, 0, 0 );

  for ( const QVector3D &p : points )
  {
    info.bboxMin.setX( qMin( info.bboxMin.x(), p.x() ) );
    info.bboxMin.setY( qMin( info.bboxMin.y(), p.y() ) );
    info.bboxMin.setZ( qMin( info.bboxMin.z(), p.z() ) );
    info.bboxMax.setX( qMax( info.bboxMax.x(), p.x() ) );
    info.bboxMax.setY( qMax( info.bboxMax.y(), p.y() ) );
    info.bboxMax.setZ( qMax( info.bboxMax.z(), p.z() ) );
    info.center += p;
  }

  info.center /= float( points.size() );
  info.bboxSize = info.bboxMax - info.bboxMin;

  double maxRadius = 0.0;
  for ( const QVector3D &p : points )
    maxRadius = qMax( maxRadius, double( ( p - info.center ).length() ) );
  info.scale = maxRadius > 1e-8 ? maxRadius : 1.0;

  return info;
}

bool ExportPointCloud::exportPLY( const QString &fileName, const QString &primitiveType, ParamModelerDock *dock, int sampleCount )
{
  {
    QFile f( QDir::tempPath() + "/export_debug.log" );
    if ( f.open( QIODevice::Append | QIODevice::Text ) )
    {
      QTextStream ts( &f );
      ts << QString( "primitiveType=%1  sampleCount=%2  fileName=%3\n" )
              .arg( primitiveType )
              .arg( sampleCount )
              .arg( fileName );
    }
  }

  QString errorMessage;
  QVector<QVector3D> points = sampleCurrentPrimitive( primitiveType, dock, sampleCount, true, &errorMessage );
  if ( points.isEmpty() )
  {
    QMessageBox::warning( nullptr, "Warning", errorMessage );
    return false;
  }

  QFile file( fileName );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    QMessageBox::critical( nullptr, "Error", "Cannot write PLY file." );
    return false;
  }

  QTextStream out( &file );
  out << "ply\n";
  out << "format ascii 1.0\n";
  out << "comment generated by ParamModeler\n";
  out << "element vertex " << points.size() << "\n";
  out << "property float x\n";
  out << "property float y\n";
  out << "property float z\n";
  out << "end_header\n";

  for ( const QVector3D &p : points )
    out << p.x() << " " << p.y() << " " << p.z() << "\n";

  file.close();
  return true;
}

bool ExportPointCloud::exportDLInputTXT( const QString &fileName,
                                         const QString &primitiveType,
                                         ParamModelerDock *dock,
                                         int pointCount,
                                         DLPointCloudInfo *info )
{
  QString errorMessage;
  QVector<QVector3D> points = sampleCurrentPrimitive( primitiveType, dock, pointCount, true, &errorMessage );
  if ( points.isEmpty() )
  {
    QMessageBox::warning( nullptr, "Warning", errorMessage );
    return false;
  }

  if ( info )
    *info = computeDLPointCloudInfo( points );

  normalizeForDL( points );

  QFile file( fileName );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    QMessageBox::critical( nullptr, "Error", "Cannot write deep learning input TXT file." );
    return false;
  }

  QTextStream out( &file );
  out.setRealNumberNotation( QTextStream::FixedNotation );
  out.setRealNumberPrecision( 8 );
  for ( const QVector3D &p : points )
    out << p.x() << " " << p.y() << " " << p.z() << "\n";

  file.close();
  return true;
}

bool ExportPointCloud::exportLabeledTXT( const QString &fileName,
                                         const QString &primitiveType,
                                         ParamModelerDock *dock,
                                         int pointCount,
                                         DLPointCloudInfo *info )
{
  // ── Build mesh and classify triangles (same logic as sampleCurrentPrimitive) ──
  MeshData mesh = BuildMesh::build( primitiveType, dock );
  if ( mesh.isEmpty() )
  {
    QMessageBox::warning( nullptr, "Warning",
                          QString( "Primitive \"%1\" cannot generate a valid mesh." ).arg( primitiveType ) );
    return false;
  }

  int triCount = mesh.indices.size() / 3;
  if ( triCount == 0 )
  {
    QMessageBox::warning( nullptr, "Warning", "Mesh has no triangles to sample." );
    return false;
  }

  QVector<int> horzTris;  // roof  (normal.z > 0.7)
  QVector<int> sideTris;  // wall  (everything else, skip bottom faces)
  for ( int i = 0; i < triCount; i++ )
  {
    QVector3D A = mesh.vertices[mesh.indices[i * 3]];
    QVector3D B = mesh.vertices[mesh.indices[i * 3 + 1]];
    QVector3D C = mesh.vertices[mesh.indices[i * 3 + 2]];
    QVector3D n = QVector3D::crossProduct( B - A, C - A ).normalized();
    if ( n.z() > 0.7f )
      horzTris << i;
    else if ( n.z() < -0.7f )
      continue;  // skip bottom faces
    else
      sideTris << i;
  }

  // ── Distribute point budget between roof and wall ──
  int sideCount = 0;
  int horzCount = 0;
  if ( sideTris.isEmpty() )
    horzCount = pointCount;
  else if ( horzTris.isEmpty() )
    sideCount = pointCount;
  else
  {
    sideCount = qMax( int( pointCount * 0.40 ), pointCount / 4 );
    horzCount = pointCount - sideCount;
  }

  // ── Sample roof and wall SEPARATELY to preserve labels ──
  QVector<QVector3D> roofPts = sampleGroup( mesh, horzTris, horzCount );
  QVector<QVector3D> wallPts = sampleGroup( mesh, sideTris, sideCount );

  // ── Apply pose ──
  double tx = dock->poseTranslateX();
  double ty = dock->poseTranslateY();
  double tz = dock->poseTranslateZ();
  double rx = dock->poseRotateX();
  double ry = dock->poseRotateY();
  double rz = dock->poseRotateZ();
  bool hasPose = ( tx != 0 || ty != 0 || tz != 0 || rx != 0 || ry != 0 || rz != 0 );
  if ( hasPose )
  {
    for ( QVector3D &p : roofPts )
      p = applyPose( p, tx, ty, tz, rx, ry, rz );
    for ( QVector3D &p : wallPts )
      p = applyPose( p, tx, ty, tz, rx, ry, rz );
  }

  // ── Compute DL info from combined set (before normalization) ──
  QVector<QVector3D> allPts;
  allPts << roofPts << wallPts;
  if ( info )
    *info = computeDLPointCloudInfo( allPts );

  // ── Normalize using shared center & scale ──
  {
    QVector3D center( 0, 0, 0 );
    for ( const QVector3D &p : allPts )
      center += p;
    center /= float( allPts.size() );

    float maxRadius = 0.0f;
    for ( const QVector3D &p : allPts )
      maxRadius = qMax( maxRadius, ( p - center ).length() );
    if ( maxRadius <= 1e-8f )
      maxRadius = 1.0f;

    for ( QVector3D &p : roofPts )
      p = ( p - center ) / maxRadius;
    for ( QVector3D &p : wallPts )
      p = ( p - center ) / maxRadius;
  }

  if ( roofPts.isEmpty() && wallPts.isEmpty() )
  {
    QMessageBox::warning( nullptr, "Warning", "Sampling produced no points." );
    return false;
  }

  // NOTE: do NOT overwrite *info here — it already holds the original
  // (pre-normalization) center/scale from line 379.
  QVector<QVector3D> combined;
  combined << roofPts << wallPts;

  // ── Write:  x  y  z  label  (label: 0=wall, 1=roof) ──
  QFile file( fileName );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    QMessageBox::critical( nullptr, "Error", "Cannot write labeled TXT file." );
    return false;
  }

  QTextStream out( &file );
  out.setRealNumberNotation( QTextStream::FixedNotation );
  out.setRealNumberPrecision( 8 );
  for ( const QVector3D &p : roofPts )
    out << p.x() << " " << p.y() << " " << p.z() << " 1\n";  // 1 = roof
  for ( const QVector3D &p : wallPts )
    out << p.x() << " " << p.y() << " " << p.z() << " 0\n";  // 0 = wall

  file.close();
  return true;
}

// ====================================================================
// 侧面遮挡辅助函数
// ====================================================================

static bool isBoxLike( const QString &primType )
{
  static const QSet<QString> s = {
    "Cuboid", "GabledRoof", "PyramidRoof", "TruncatedPyramidRoof",
    "HalfCylinderRoof", "AsymmetricGableHouse",
    "LHouse", "IndentedCuboid"
  };
  return s.contains( primType );
}

static bool isCylinderLike( const QString &primType )
{
  static const QSet<QString> s = {
    "Cylinder", "CylinderDome", "ConeCylinder", "FourStageRoundTower"
  };
  return s.contains( primType );
}

// Face index (CCW): 0=x_min, 1=y_min, 2=x_max, 3=y_max
// Adjacent pairs: (0,1), (1,2), (2,3), (3,0)
static QVector<int> classifyBoxWallFaces( const QVector<QVector3D> &wallPts, double rzDeg )
{
  const double rzRad = qDegreesToRadians( rzDeg );
  const double cosR = std::cos( rzRad ), sinR = std::sin( rzRad );
  const int n = wallPts.size();
  QVector<int> labels( n );
  if ( n < 8 ) return labels;

  // Un-rotate to local building frame
  QVector<double> xLoc( n ), yLoc( n );
  for ( int i = 0; i < n; i++ )
  {
    xLoc[i] =  wallPts[i].x() * cosR + wallPts[i].y() * sinR;
    yLoc[i] = -wallPts[i].x() * sinR + wallPts[i].y() * cosR;
  }

  // Estimate 4 face-plane positions from outer-quartile means
  QVector<double> xSorted = xLoc, ySorted = yLoc;
  std::sort( xSorted.begin(), xSorted.end() );
  std::sort( ySorted.begin(), ySorted.end() );
  const int k = qMax( n / 4, 4 );
  double xMinP = 0, xMaxP = 0, yMinP = 0, yMaxP = 0;
  for ( int i = 0; i < k; i++ )
  {
    xMinP += xSorted[i];        xMaxP += xSorted[n - 1 - i];
    yMinP += ySorted[i];        yMaxP += ySorted[n - 1 - i];
  }
  xMinP /= k;  xMaxP /= k;  yMinP /= k;  yMaxP /= k;

  // Assign each point to nearest face plane
  for ( int i = 0; i < n; i++ )
  {
    double d[] = { std::abs( xLoc[i] - xMinP ),  // 0: x_min
                   std::abs( yLoc[i] - yMinP ),  // 1: y_min
                   std::abs( xLoc[i] - xMaxP ),  // 2: x_max
                   std::abs( yLoc[i] - yMaxP ) };// 3: y_max
    double dMin = std::min( { d[0], d[1], d[2], d[3] } );
    if ( dMin == d[0] )      labels[i] = 0;
    else if ( dMin == d[1] ) labels[i] = 1;
    else if ( dMin == d[2] ) labels[i] = 2;
    else                     labels[i] = 3;
  }
  return labels;
}

static QVector<QVector3D> applyBoxOcclusion( const QVector<QVector3D> &wallPts,
                                              double rzDeg, double singleFaceProb )
{
  if ( wallPts.size() < 16 )
    return wallPts;

  QVector<int> faceLabels = classifyBoxWallFaces( wallPts, rzDeg );

  // Select 1 or 2 adjacent faces (CCW: 0,1,2,3)
  QSet<int> keepFaces;
  if ( static_cast<double>( qrand() ) / RAND_MAX < singleFaceProb )
    keepFaces.insert( qrand() % 4 );
  else
  {
    int start = qrand() % 4;
    keepFaces.insert( start );
    keepFaces.insert( ( start + 1 ) % 4 );
  }

  QVector<QVector3D> kept;
  kept.reserve( wallPts.size() / 2 );
  for ( int i = 0; i < wallPts.size(); i++ )
    if ( keepFaces.contains( faceLabels[i] ) )
      kept << wallPts[i];
  return kept.isEmpty() ? wallPts : kept;
}

static QVector<QVector3D> applyCylinderOcclusion( const QVector<QVector3D> &wallPts )
{
  if ( wallPts.size() < 16 )
    return wallPts;

  // Random 150°–210° continuous arc
  const double keepDeg = 150.0 + static_cast<double>( qrand() ) / RAND_MAX * 60.0;
  const double keepRatio = keepDeg / 360.0;
  const double halfSpan = keepRatio * M_PI;

  QVector<double> angles( wallPts.size() );
  double cx = 0, cy = 0;
  for ( const QVector3D &p : wallPts ) { cx += p.x(); cy += p.y(); }
  cx /= wallPts.size();  cy /= wallPts.size();
  for ( int i = 0; i < wallPts.size(); i++ )
    angles[i] = std::atan2( wallPts[i].y() - cy, wallPts[i].x() - cx );

  const double start = static_cast<double>( qrand() ) / RAND_MAX * 2.0 * M_PI;
  const double mid = start + halfSpan;

  QVector<QVector3D> kept;
  kept.reserve( wallPts.size() / 2 );
  for ( int i = 0; i < wallPts.size(); i++ )
  {
    double diff = angles[i] - mid;
    diff = std::atan2( std::sin( diff ), std::cos( diff ) );  // wrap to [-pi,pi]
    if ( std::abs( diff ) <= halfSpan )
      kept << wallPts[i];
  }
  return kept.isEmpty() ? wallPts : kept;
}

static QVector<QVector3D> sampleIndentedRimPoints( ParamModelerDock *dock, int n )
{
  QVector<QVector3D> pts;
  if ( !dock || n <= 0 )
    return pts;

  const double H = dock->icOuterHeight();
  const double ox = dock->icOffsetX();
  const double oy = dock->icOffsetY();
  const double w = dock->icInnerLength();
  const double d = dock->icInnerWidth();
  if ( H <= 0.0 || w <= 0.0 || d <= 0.0 )
    return pts;

  const QVector<QVector3D> corners = {
    QVector3D( ox,     oy,     H ),
    QVector3D( ox + w, oy,     H ),
    QVector3D( ox + w, oy + d, H ),
    QVector3D( ox,     oy + d, H )
  };

  pts.reserve( n );
  for ( int i = 0; i < n; ++i )
  {
    const int edge = qrand() % 4;
    const float t = float( qrand() ) / RAND_MAX;
    const QVector3D a = corners[edge];
    const QVector3D b = corners[( edge + 1 ) % 4];
    pts << ( a * ( 1.0f - t ) + b * t );
  }
  return pts;
}

static void classifyIndentedCuboidTriangles( const MeshData &mesh,
                                             QVector<int> &outerTopTris,
                                             QVector<int> &recessFloorTris,
                                             QVector<int> &outerWallTris,
                                             QVector<int> &recessWallTris )
{
  if ( mesh.vertices.isEmpty() || mesh.indices.isEmpty() )
    return;

  QVector3D bboxMin = mesh.vertices.first();
  QVector3D bboxMax = mesh.vertices.first();
  for ( const QVector3D &v : mesh.vertices )
  {
    bboxMin.setX( qMin( bboxMin.x(), v.x() ) );
    bboxMin.setY( qMin( bboxMin.y(), v.y() ) );
    bboxMin.setZ( qMin( bboxMin.z(), v.z() ) );
    bboxMax.setX( qMax( bboxMax.x(), v.x() ) );
    bboxMax.setY( qMax( bboxMax.y(), v.y() ) );
    bboxMax.setZ( qMax( bboxMax.z(), v.z() ) );
  }

  const double maxDim = std::max( { static_cast<double>( bboxMax.x() - bboxMin.x() ),
                                    static_cast<double>( bboxMax.y() - bboxMin.y() ),
                                    static_cast<double>( bboxMax.z() - bboxMin.z() ),
                                    1.0 } );
  const double eps = maxDim * 1e-5;
  const int triCount = mesh.indices.size() / 3;
  for ( int i = 0; i < triCount; ++i )
  {
    const QVector3D A = mesh.vertices[mesh.indices[i * 3]];
    const QVector3D B = mesh.vertices[mesh.indices[i * 3 + 1]];
    const QVector3D C = mesh.vertices[mesh.indices[i * 3 + 2]];
    const QVector3D n = QVector3D::crossProduct( B - A, C - A ).normalized();
    const QVector3D c = ( A + B + C ) / 3.0f;

    if ( n.z() < -0.7f )
      continue;
    if ( n.z() > 0.7f )
    {
      if ( c.z() < bboxMax.z() - eps )
        recessFloorTris << i;
      else
        outerTopTris << i;
      continue;
    }

    const bool onOuterFace =
      std::abs( static_cast<double>( c.x() - bboxMin.x() ) ) <= eps ||
      std::abs( static_cast<double>( c.x() - bboxMax.x() ) ) <= eps ||
      std::abs( static_cast<double>( c.y() - bboxMin.y() ) ) <= eps ||
      std::abs( static_cast<double>( c.y() - bboxMax.y() ) ) <= eps;
    if ( onOuterFace )
      outerWallTris << i;
    else
      recessWallTris << i;
  }
}

static QVector<QVector3D> sampleLHouseNotchEdgePoints( ParamModelerDock *dock, int n )
{
  QVector<QVector3D> pts;
  if ( !dock || n <= 0 )
    return pts;

  const double totalLength = dock->LTotalLength();
  const double totalWidth = dock->LTotalWidth();
  const double wingRatio = dock->LWingRatio();
  const double wingWidthRatio = dock->LWingWidthRatio();
  const double height = dock->LHeight();
  const double aw = totalLength * ( 1.0 - wingRatio );
  const double bd = totalWidth * wingWidthRatio;
  if ( totalLength <= 0.0 || totalWidth <= 0.0 || aw <= 0.0 ||
       aw >= totalLength || bd <= 0.0 || bd >= totalWidth || height <= 0.0 )
    return pts;

  const QVector<QVector3D> starts = {
    QVector3D( aw, bd, height ),
    QVector3D( aw, bd, height )
  };
  const QVector<QVector3D> ends = {
    QVector3D( aw, totalWidth, height ),
    QVector3D( totalLength, bd, height )
  };
  const QVector<double> lengths = {
    totalWidth - bd,
    totalLength - aw
  };
  const double total = lengths[0] + lengths[1];

  pts.reserve( n );
  for ( int i = 0; i < n; ++i )
  {
    const double r = static_cast<double>( qrand() ) / RAND_MAX * total;
    const int edge = r < lengths[0] ? 0 : 1;
    const float t = float( qrand() ) / RAND_MAX;
    pts << ( starts[edge] * ( 1.0f - t ) + ends[edge] * t );
  }
  return pts;
}

static void classifyLHouseTriangles( const MeshData &mesh,
                                     QVector<int> &topTris,
                                     QVector<int> &outerWallTris,
                                     QVector<int> &notchWallTris )
{
  if ( mesh.vertices.isEmpty() || mesh.indices.isEmpty() )
    return;

  QVector3D bboxMin = mesh.vertices.first();
  QVector3D bboxMax = mesh.vertices.first();
  for ( const QVector3D &v : mesh.vertices )
  {
    bboxMin.setX( qMin( bboxMin.x(), v.x() ) );
    bboxMin.setY( qMin( bboxMin.y(), v.y() ) );
    bboxMin.setZ( qMin( bboxMin.z(), v.z() ) );
    bboxMax.setX( qMax( bboxMax.x(), v.x() ) );
    bboxMax.setY( qMax( bboxMax.y(), v.y() ) );
    bboxMax.setZ( qMax( bboxMax.z(), v.z() ) );
  }

  const double maxDim = std::max( { static_cast<double>( bboxMax.x() - bboxMin.x() ),
                                    static_cast<double>( bboxMax.y() - bboxMin.y() ),
                                    static_cast<double>( bboxMax.z() - bboxMin.z() ),
                                    1.0 } );
  const double eps = maxDim * 1e-5;
  const int triCount = mesh.indices.size() / 3;
  for ( int i = 0; i < triCount; ++i )
  {
    const QVector3D A = mesh.vertices[mesh.indices[i * 3]];
    const QVector3D B = mesh.vertices[mesh.indices[i * 3 + 1]];
    const QVector3D C = mesh.vertices[mesh.indices[i * 3 + 2]];
    const QVector3D n = QVector3D::crossProduct( B - A, C - A ).normalized();
    const QVector3D c = ( A + B + C ) / 3.0f;

    if ( n.z() < -0.7f )
      continue;
    if ( n.z() > 0.7f )
    {
      topTris << i;
      continue;
    }

    const bool onOuterFace =
      std::abs( static_cast<double>( c.x() - bboxMin.x() ) ) <= eps ||
      std::abs( static_cast<double>( c.x() - bboxMax.x() ) ) <= eps ||
      std::abs( static_cast<double>( c.y() - bboxMin.y() ) ) <= eps ||
      std::abs( static_cast<double>( c.y() - bboxMax.y() ) ) <= eps;
    if ( onOuterFace )
      outerWallTris << i;
    else
      notchWallTris << i;
  }
}

static QVector<QVector3D> sampleTruncatedPyramidRoofEdgePoints( ParamModelerDock *dock, int n )
{
  QVector<QVector3D> pts;
  if ( !dock || n <= 0 )
    return pts;

  const double bottomLength = dock->tpBottomLength();
  const double bottomWidth = dock->tpBottomWidth();
  const double topLength = dock->tpTopLength();
  const double topWidth = dock->tpTopWidth();
  const double wallHeight = dock->tpWallHeight();
  const double topZ = wallHeight + dock->tpRoofHeight();
  if ( bottomLength <= 0.0 || bottomWidth <= 0.0 || topLength <= 0.0 ||
       topWidth <= 0.0 || wallHeight <= 0.0 || topZ <= wallHeight )
    return pts;

  const double ox = ( bottomLength - topLength ) * 0.5;
  const double oy = ( bottomWidth - topWidth ) * 0.5;
  const QVector<QVector3D> lower = {
    QVector3D( 0.0f, 0.0f, static_cast<float>( wallHeight ) ),
    QVector3D( static_cast<float>( bottomLength ), 0.0f, static_cast<float>( wallHeight ) ),
    QVector3D( static_cast<float>( bottomLength ), static_cast<float>( bottomWidth ), static_cast<float>( wallHeight ) ),
    QVector3D( 0.0f, static_cast<float>( bottomWidth ), static_cast<float>( wallHeight ) )
  };
  const QVector<QVector3D> upper = {
    QVector3D( static_cast<float>( ox ), static_cast<float>( oy ), static_cast<float>( topZ ) ),
    QVector3D( static_cast<float>( ox + topLength ), static_cast<float>( oy ), static_cast<float>( topZ ) ),
    QVector3D( static_cast<float>( ox + topLength ), static_cast<float>( oy + topWidth ), static_cast<float>( topZ ) ),
    QVector3D( static_cast<float>( ox ), static_cast<float>( oy + topWidth ), static_cast<float>( topZ ) )
  };

  QVector<QVector3D> starts;
  QVector<QVector3D> ends;
  QVector<double> lengths;
  starts.reserve( 8 );
  ends.reserve( 8 );
  lengths.reserve( 8 );
  double total = 0.0;
  const auto addEdge = [&]( const QVector3D &a, const QVector3D &b ) {
    starts << a;
    ends << b;
    const double len = static_cast<double>( ( b - a ).length() );
    lengths << len;
    total += len;
  };
  for ( int i = 0; i < 4; ++i )
  {
    addEdge( lower[i], lower[( i + 1 ) % 4] );
    addEdge( upper[i], upper[( i + 1 ) % 4] );
  }
  if ( total <= 0.0 )
    return pts;

  pts.reserve( n );
  for ( int i = 0; i < n; ++i )
  {
    double r = static_cast<double>( qrand() ) / RAND_MAX * total;
    int edge = 0;
    while ( edge < lengths.size() - 1 && r > lengths[edge] )
    {
      r -= lengths[edge];
      ++edge;
    }
    const float t = float( qrand() ) / RAND_MAX;
    pts << ( starts[edge] * ( 1.0f - t ) + ends[edge] * t );
  }
  return pts;
}

static void classifyTruncatedPyramidRoofTriangles( const MeshData &mesh,
                                                   double wallHeight,
                                                   QVector<int> &topTris,
                                                   QVector<int> &roofSlopeTris,
                                                   QVector<int> &wallTris )
{
  if ( mesh.vertices.isEmpty() || mesh.indices.isEmpty() )
    return;

  const double eps = qMax( 1e-5, std::abs( wallHeight ) * 1e-5 );
  const int triCount = mesh.indices.size() / 3;
  for ( int i = 0; i < triCount; ++i )
  {
    const QVector3D A = mesh.vertices[mesh.indices[i * 3]];
    const QVector3D B = mesh.vertices[mesh.indices[i * 3 + 1]];
    const QVector3D C = mesh.vertices[mesh.indices[i * 3 + 2]];
    const QVector3D n = QVector3D::crossProduct( B - A, C - A ).normalized();
    const QVector3D c = ( A + B + C ) / 3.0f;

    if ( n.z() < -0.7f )
      continue;
    if ( n.z() > 0.7f )
    {
      topTris << i;
      continue;
    }

    if ( static_cast<double>( c.z() ) > wallHeight + eps )
      roofSlopeTris << i;
    else
      wallTris << i;
  }
}

static QVector<QVector3D> sampleAsymmetricGableHouseEdgePoints( ParamModelerDock *dock, int n )
{
  QVector<QVector3D> pts;
  if ( !dock || n <= 0 )
    return pts;

  const double length = dock->aghLength();
  const double width = dock->aghWidth();
  const double wallHeight = dock->aghWallHeight();
  const double roofZ = wallHeight + dock->aghRoofHeight();
  const double ridgeLength = dock->aghRidgeLength();
  double ridgeRatio = dock->aghRidgeRatio();
  ridgeRatio = std::max( 0.2, std::min( 0.8, ridgeRatio ) );
  if ( length <= 0.0 || width <= 0.0 || wallHeight <= 0.0 ||
       roofZ <= wallHeight || ridgeLength <= 0.0 )
    return pts;

  const double rs = length * 0.5 - ridgeLength * 0.5;
  const double re = length * 0.5 + ridgeLength * 0.5;
  const double ry = width * ridgeRatio;
  const QVector3D r0( static_cast<float>( rs ), static_cast<float>( ry ), static_cast<float>( roofZ ) );
  const QVector3D r1( static_cast<float>( re ), static_cast<float>( ry ), static_cast<float>( roofZ ) );
  const QVector3D e0( static_cast<float>( rs ), 0.0f, static_cast<float>( wallHeight ) );
  const QVector3D e1( static_cast<float>( rs ), static_cast<float>( width ), static_cast<float>( wallHeight ) );
  const QVector3D e2( static_cast<float>( re ), 0.0f, static_cast<float>( wallHeight ) );
  const QVector3D e3( static_cast<float>( re ), static_cast<float>( width ), static_cast<float>( wallHeight ) );
  const QVector3D v4( 0.0f, 0.0f, static_cast<float>( wallHeight ) );
  const QVector3D v5( static_cast<float>( length ), 0.0f, static_cast<float>( wallHeight ) );
  const QVector3D v6( static_cast<float>( length ), static_cast<float>( width ), static_cast<float>( wallHeight ) );
  const QVector3D v7( 0.0f, static_cast<float>( width ), static_cast<float>( wallHeight ) );

  QVector<QVector3D> starts;
  QVector<QVector3D> ends;
  QVector<double> lengths;
  double total = 0.0;
  const auto addEdge = [&]( const QVector3D &a, const QVector3D &b ) {
    starts << a;
    ends << b;
    const double len = static_cast<double>( ( b - a ).length() );
    lengths << len;
    total += len;
  };
  addEdge( r0, r1 );   // roof ridge: carries ridgeLength and ridgeRatio directly
  addEdge( v4, v5 );   // front eave
  addEdge( v7, v6 );   // back eave
  addEdge( v4, r0 );   // left gable roof edges
  addEdge( v7, r0 );
  addEdge( v5, r1 );   // right gable roof edges
  addEdge( v6, r1 );
  addEdge( e0, r0 );   // ridge-end cross sections
  addEdge( e1, r0 );
  addEdge( e2, r1 );
  addEdge( e3, r1 );
  if ( total <= 0.0 )
    return pts;

  pts.reserve( n );
  for ( int i = 0; i < n; ++i )
  {
    double r = static_cast<double>( qrand() ) / RAND_MAX * total;
    int edge = 0;
    while ( edge < lengths.size() - 1 && r > lengths[edge] )
    {
      r -= lengths[edge];
      ++edge;
    }
    const float t = float( qrand() ) / RAND_MAX;
    pts << ( starts[edge] * ( 1.0f - t ) + ends[edge] * t );
  }
  return pts;
}

static QVector<QVector3D> sampleTwoGableHousesEdgePoints( ParamModelerDock *dock, int n )
{
  QVector<QVector3D> pts;
  if ( !dock || n <= 0 )
    return pts;

  const double length1 = dock->tgLength1();
  const double length2 = dock->tgLength2();
  const double width = dock->tgWidth();
  const double wallHeight = dock->tgWallHeight();
  const double roofZ = wallHeight + dock->tgRoofHeight();
  double angle = dock->tgAngle();
  double ridgeRatio = dock->tgRidgeRatio();
  angle = std::max( 135.0, std::min( 180.0, angle ) );
  ridgeRatio = std::max( 0.2, std::min( 0.8, ridgeRatio ) );
  if ( length1 <= 0.0 || length2 <= 0.0 || width <= 0.0 ||
       wallHeight <= 0.0 || roofZ <= wallHeight )
    return pts;

  const double turnRad = ( 180.0 - angle ) * M_PI / 180.0;
  const QVector3D dir( static_cast<float>( std::cos( turnRad ) ),
                       static_cast<float>( std::sin( turnRad ) ), 0.0f );
  const QVector3D a( 0.0f, 0.0f, 0.0f );
  const QVector3D b( static_cast<float>( length1 ), 0.0f, 0.0f );
  const QVector3D c( static_cast<float>( length1 ), static_cast<float>( width ), 0.0f );
  const QVector3D d( 0.0f, static_cast<float>( width ), 0.0f );
  const QVector3D e = b + dir * static_cast<float>( length2 );
  const QVector3D g = c + dir * static_cast<float>( length2 );
  const auto withZ = []( const QVector3D &p, double z ) {
    return QVector3D( p.x(), p.y(), static_cast<float>( z ) );
  };

  const QVector3D aw = withZ( a, wallHeight );
  const QVector3D bw = withZ( b, wallHeight );
  const QVector3D cw = withZ( c, wallHeight );
  const QVector3D dw = withZ( d, wallHeight );
  const QVector3D ew = withZ( e, wallHeight );
  const QVector3D gw = withZ( g, wallHeight );
  const QVector3D r0( 0.0f, static_cast<float>( width * ridgeRatio ), static_cast<float>( roofZ ) );
  const QVector3D r1( static_cast<float>( length1 ), static_cast<float>( width * ridgeRatio ), static_cast<float>( roofZ ) );
  const QVector3D r2 = r1 + dir * static_cast<float>( length2 );

  QVector<QVector3D> starts;
  QVector<QVector3D> ends;
  QVector<double> lengths;
  double total = 0.0;
  const auto addEdge = [&]( const QVector3D &p0, const QVector3D &p1 ) {
    starts << p0;
    ends << p1;
    const double len = static_cast<double>( ( p1 - p0 ).length() );
    lengths << len;
    total += len;
  };
  addEdge( r0, r1 );   // house 1 ridge
  addEdge( r1, r2 );   // house 2 ridge
  addEdge( aw, bw );   // house 1 lower eaves
  addEdge( dw, cw );
  addEdge( bw, ew );   // house 2 lower eaves
  addEdge( cw, gw );
  addEdge( aw, r0 );   // visible gable edges
  addEdge( dw, r0 );
  addEdge( ew, r2 );
  addEdge( gw, r2 );
  addEdge( bw, r1 );   // roof junction around the bend
  addEdge( cw, r1 );
  if ( total <= 0.0 )
    return pts;

  pts.reserve( n );
  for ( int i = 0; i < n; ++i )
  {
    double r = static_cast<double>( qrand() ) / RAND_MAX * total;
    int edge = 0;
    while ( edge < lengths.size() - 1 && r > lengths[edge] )
    {
      r -= lengths[edge];
      ++edge;
    }
    const float t = float( qrand() ) / RAND_MAX;
    pts << ( starts[edge] * ( 1.0f - t ) + ends[edge] * t );
  }
  return pts;
}

static void classifyWallAndRoofByHeight( const MeshData &mesh,
                                         double wallHeight,
                                         QVector<int> &roofTris,
                                         QVector<int> &wallTris )
{
  if ( mesh.vertices.isEmpty() || mesh.indices.isEmpty() )
    return;

  const double eps = qMax( 1e-5, std::abs( wallHeight ) * 1e-5 );
  const int triCount = mesh.indices.size() / 3;
  for ( int i = 0; i < triCount; ++i )
  {
    const QVector3D A = mesh.vertices[mesh.indices[i * 3]];
    const QVector3D B = mesh.vertices[mesh.indices[i * 3 + 1]];
    const QVector3D C = mesh.vertices[mesh.indices[i * 3 + 2]];
    const QVector3D n = QVector3D::crossProduct( B - A, C - A ).normalized();
    const QVector3D c = ( A + B + C ) / 3.0f;

    if ( n.z() < -0.7f )
      continue;
    if ( static_cast<double>( c.z() ) > wallHeight + eps )
      roofTris << i;
    else
      wallTris << i;
  }
}

// ====================================================================
// 遮挡导出：在采样阶段直接模拟摄影测量缺失，输出纯 xyz
// ====================================================================

bool ExportPointCloud::exportOccludedTXT( const QString &fileName,
                                           const QString &primitiveType,
                                           ParamModelerDock *dock,
                                           int pointCount,
                                           DLPointCloudInfo *info )
{
  MeshData mesh = BuildMesh::build( primitiveType, dock );
  if ( mesh.isEmpty() )
  {
    QMessageBox::warning( nullptr, "Warning",
                          QString( "Primitive \"%1\" cannot generate a valid mesh." ).arg( primitiveType ) );
    return false;
  }

  int triCount = mesh.indices.size() / 3;
  if ( triCount == 0 )
  {
    QMessageBox::warning( nullptr, "Warning", "Mesh has no triangles to sample." );
    return false;
  }

  QVector<QVector3D> roofPts;
  QVector<QVector3D> wallPts;

  if ( primitiveType == QStringLiteral( "IndentedCuboid" ) )
  {
    QVector<int> outerTopTris, recessFloorTris, outerWallTris, recessWallTris;
    classifyIndentedCuboidTriangles( mesh, outerTopTris, recessFloorTris, outerWallTris, recessWallTris );

    const int rimCount = qMax( 24, int( pointCount * 0.08 ) );
    const int recessWallCount = int( pointCount * 0.24 );
    const int recessFloorCount = int( pointCount * 0.18 );
    const int outerWallCount = int( pointCount * 0.25 );
    const int outerTopCount = qMax( 0, pointCount - rimCount - recessWallCount - recessFloorCount - outerWallCount );

    roofPts << sampleGroup( mesh, outerTopTris, outerTopCount );
    roofPts << sampleGroup( mesh, recessFloorTris, recessFloorCount );
    roofPts << sampleGroup( mesh, recessWallTris, recessWallCount );
    roofPts << sampleIndentedRimPoints( dock, rimCount );
    wallPts << sampleGroup( mesh, outerWallTris, outerWallCount );
  }
  else if ( primitiveType == QStringLiteral( "LHouse" ) )
  {
    QVector<int> topTris, outerWallTris, notchWallTris;
    classifyLHouseTriangles( mesh, topTris, outerWallTris, notchWallTris );

    const int notchEdgeCount = qMax( 24, int( pointCount * 0.08 ) );
    const int notchWallCount = int( pointCount * 0.24 );
    const int outerWallCount = int( pointCount * 0.28 );
    const int topCount = qMax( 0, pointCount - notchEdgeCount - notchWallCount - outerWallCount );

    roofPts << sampleGroup( mesh, topTris, topCount );
    roofPts << sampleGroup( mesh, notchWallTris, notchWallCount );
    roofPts << sampleLHouseNotchEdgePoints( dock, notchEdgeCount );
    wallPts << sampleGroup( mesh, outerWallTris, outerWallCount );
  }
  else if ( primitiveType == QStringLiteral( "TruncatedPyramidRoof" ) )
  {
    QVector<int> topTris, roofSlopeTris, wallTris;
    classifyTruncatedPyramidRoofTriangles( mesh, dock->tpWallHeight(), topTris, roofSlopeTris, wallTris );

    const int edgeCount = qMax( 32, int( pointCount * 0.12 ) );
    const int roofSlopeCount = int( pointCount * 0.46 );
    const int wallCount = int( pointCount * 0.24 );
    const int topCount = qMax( 0, pointCount - edgeCount - roofSlopeCount - wallCount );

    roofPts << sampleGroup( mesh, topTris, topCount );
    roofPts << sampleGroup( mesh, roofSlopeTris, roofSlopeCount );
    roofPts << sampleTruncatedPyramidRoofEdgePoints( dock, edgeCount );
    wallPts << sampleGroup( mesh, wallTris, wallCount );
  }
  else if ( primitiveType == QStringLiteral( "AsymmetricGableHouse" ) )
  {
    QVector<int> roofTris, wallTris;
    classifyWallAndRoofByHeight( mesh, dock->aghWallHeight(), roofTris, wallTris );

    const int edgeCount = qMax( 32, int( pointCount * 0.14 ) );
    const int roofCount = int( pointCount * 0.46 );
    const int wallCount = qMax( 0, pointCount - edgeCount - roofCount );

    roofPts << sampleGroup( mesh, roofTris, roofCount );
    roofPts << sampleAsymmetricGableHouseEdgePoints( dock, edgeCount );
    wallPts << sampleGroup( mesh, wallTris, wallCount );
  }
  else if ( primitiveType == QStringLiteral( "TwoGableHouses" ) )
  {
    QVector<int> roofTris, wallTris;
    classifyWallAndRoofByHeight( mesh, dock->tgWallHeight(), roofTris, wallTris );

    const int edgeCount = qMax( 40, int( pointCount * 0.16 ) );
    const int roofCount = int( pointCount * 0.46 );
    const int wallCount = qMax( 0, pointCount - edgeCount - roofCount );

    roofPts << sampleGroup( mesh, roofTris, roofCount );
    roofPts << sampleTwoGableHousesEdgePoints( dock, edgeCount );
    wallPts << sampleGroup( mesh, wallTris, wallCount );
  }
  else
  {
    QVector<int> horzTris, sideTris;
    for ( int i = 0; i < triCount; i++ )
    {
      QVector3D A = mesh.vertices[mesh.indices[i * 3]];
      QVector3D B = mesh.vertices[mesh.indices[i * 3 + 1]];
      QVector3D C = mesh.vertices[mesh.indices[i * 3 + 2]];
      QVector3D n = QVector3D::crossProduct( B - A, C - A ).normalized();
      if ( n.z() > 0.7f )
        horzTris << i;
      else if ( n.z() < -0.7f )
        continue;
      else
        sideTris << i;
    }

    int sideCount = 0, horzCount = 0;
    if ( sideTris.isEmpty() )       horzCount = pointCount;
    else if ( horzTris.isEmpty() )  sideCount = pointCount;
    else
    {
      sideCount = qMax( int( pointCount * 0.40 ), pointCount / 4 );
      horzCount = pointCount - sideCount;
    }

    roofPts = sampleGroup( mesh, horzTris, horzCount );
    wallPts = sampleGroup( mesh, sideTris, sideCount );
  }

  // ── Apply pose ──
  double tx = dock->poseTranslateX(), ty = dock->poseTranslateY(), tz = dock->poseTranslateZ();
  double rx = dock->poseRotateX(), ry = dock->poseRotateY(), rz = dock->poseRotateZ();
  bool hasPose = ( tx != 0 || ty != 0 || tz != 0 || rx != 0 || ry != 0 || rz != 0 );
  if ( hasPose )
  {
    for ( QVector3D &p : roofPts ) p = applyPose( p, tx, ty, tz, rx, ry, rz );
    for ( QVector3D &p : wallPts ) p = applyPose( p, tx, ty, tz, rx, ry, rz );
  }

  // ── ★ 在导出时直接应用遮挡 ──
  if ( primitiveType == QStringLiteral( "IndentedCuboid" ) )
    wallPts = applyBoxOcclusion( wallPts, rz, 0.3 );
  else if ( isBoxLike( primitiveType ) )
    wallPts = applyBoxOcclusion( wallPts, rz, 0.3 );
  else if ( isCylinderLike( primitiveType ) )
    wallPts = applyCylinderOcclusion( wallPts );

  if ( roofPts.isEmpty() && wallPts.isEmpty() )
  {
    QMessageBox::warning( nullptr, "Warning", "Sampling produced no points." );
    return false;
  }

  QVector<QVector3D> allPts;
  allPts << roofPts << wallPts;
  if ( info )
    *info = computeDLPointCloudInfo( allPts );

  // ── Normalize ──
  {
    QVector3D center( 0, 0, 0 );
    for ( const QVector3D &p : allPts ) center += p;
    center /= float( allPts.size() );
    float maxRadius = 0.0f;
    for ( const QVector3D &p : allPts ) maxRadius = qMax( maxRadius, ( p - center ).length() );
    if ( maxRadius <= 1e-8f ) maxRadius = 1.0f;
    for ( QVector3D &p : roofPts ) p = ( p - center ) / maxRadius;
    for ( QVector3D &p : wallPts ) p = ( p - center ) / maxRadius;
  }

  QVector<QVector3D> combined;
  combined << roofPts << wallPts;
  // NOTE: do NOT overwrite *info here — it already holds the original
  // (pre-normalization) center/scale from line 631.  Overwriting with the
  // normalized values would break the denormalisation chain in
  // loadPointCloudToQGIS3D, making the point cloud appear tiny next to
  // the metre-scale model.

  // ── Safety: Z-based bottom removal (catch any bottom triangles that escaped n.z filter) ──
  {
    float zMin = combined[0].z(), zMax = combined[0].z();
    for ( const QVector3D &p : combined ) { zMin = qMin( zMin, p.z() ); zMax = qMax( zMax, p.z() ); }
    float zHeight = zMax - zMin;
    if ( zHeight > 1e-6f )
    {
      float zThr = zMin + zHeight * 0.015f;  // remove bottom 1.5% of height
      combined.erase( std::remove_if( combined.begin(), combined.end(),
                       [zThr]( const QVector3D &p ) { return p.z() < zThr; } ),
                      combined.end() );
    }
  }

  if ( combined.isEmpty() )
  {
    QMessageBox::warning( nullptr, "Warning", "Occlusion removed all points." );
    return false;
  }

  resampleToFixedCount( combined, pointCount );

  // ── Write plain xyz (occlusion is baked in, no labels needed) ──
  QFile file( fileName );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    QMessageBox::critical( nullptr, "Error", "Cannot write occluded TXT file." );
    return false;
  }

  QTextStream out( &file );
  out.setRealNumberNotation( QTextStream::FixedNotation );
  out.setRealNumberPrecision( 8 );
  for ( const QVector3D &p : combined )
    out << p.x() << " " << p.y() << " " << p.z() << "\n";

  file.close();
  return true;
}
