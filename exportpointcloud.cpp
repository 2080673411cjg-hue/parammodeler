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
