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
    "HalfCylinderRoof", "AsymmetricGableHouse", "TwoGableHouses",
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

  QVector<QVector3D> roofPts = sampleGroup( mesh, horzTris, horzCount );
  QVector<QVector3D> wallPts = sampleGroup( mesh, sideTris, sideCount );

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
  if ( isBoxLike( primitiveType ) )
    wallPts = applyBoxOcclusion( wallPts, rz, 0.3 );
  else if ( isCylinderLike( primitiveType ) )
    wallPts = applyCylinderOcclusion( wallPts );

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
