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

// ============================================================
// 对单个三角形用重心坐标随机采一个点
// ============================================================
static QVector3D sampleTriangle( const QVector3D &A, const QVector3D &B, const QVector3D &C )
{
  float r1 = float( qrand() ) / RAND_MAX;
  float r2 = float( qrand() ) / RAND_MAX;
  float sqr1 = qSqrt( r1 );
  return ( 1.0f - sqr1 ) * A
         + sqr1 * ( 1.0f - r2 ) * B
         + sqr1 * r2 * C;
}

// ============================================================
// 计算三角形面积
// ============================================================
static float triangleArea( const QVector3D &A, const QVector3D &B, const QVector3D &C )
{
  return QVector3D::crossProduct( B - A, C - A ).length() * 0.5f;
}

// ============================================================
// 位姿变换
// ============================================================
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

// ============================================================
// 在一组三角形内按面积加权采样 n 个点
// ============================================================
static QVector<QVector3D> sampleGroup( const MeshData &mesh, const QVector<int> &tris, int n )
{
  QVector<QVector3D> pts;
  if ( tris.isEmpty() || n <= 0 )
    return pts;

  // 构建累积面积表
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
    int lo = 0, hi = tris.size() - 1;
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

// ============================================================
// 主函数
// ============================================================
bool ExportPointCloud::exportPLY( const QString &fileName, const QString &primitiveType, ParamModelerDock *dock, int sampleCount )
{
  // 调试日志
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

  // 1. 生成网格
  MeshData mesh = BuildMesh::build( primitiveType, dock );
  if ( mesh.isEmpty() )
  {
    QMessageBox::warning( nullptr, "警告", QString( "基元 \"%1\" 暂不支持导出。" ).arg( primitiveType ) );
    return false;
  }

  int triCount = mesh.indices.size() / 3;
  if ( triCount == 0 )
  {
    QMessageBox::warning( nullptr, "警告", "网格三角形数为零，无法采样。" );
    return false;
  }

  // 2. 按法线方向把三角形分成顶面和侧面两组，底面直接排除
  //    法线 Z > 0.7 → 顶面；Z < -0.7 → 底面（跳过）；其余 → 侧面
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
      continue;  // 底面跳过，不采样
    else
      sideTris << i;
  }

  // 3. 分配采样点数：侧面强制保底 40%，保证高度方向有足够点
  int sideCount = 0, horzCount = 0;
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

  // 4. 分组按面积加权采样
  QVector<QVector3D> points;
  points << sampleGroup( mesh, horzTris, horzCount );
  points << sampleGroup( mesh, sideTris, sideCount );

  // 5. 位姿变换
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

  // 6. 写 PLY 文件（ASCII 格式）
  QFile file( fileName );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    QMessageBox::critical( nullptr, "错误", "无法写入 PLY 文件!" );
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
