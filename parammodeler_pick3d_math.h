#ifndef PARAMMODELER_PICK3D_MATH_H
#define PARAMMODELER_PICK3D_MATH_H

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector>
#include <cmath>
#include <limits>

namespace ParamModelerPicking
{
// Select an actual upper vertex on the local anchor's vertical line, not bbox max Z.
inline bool upperAnchor( const QVector<QVector3D> &vertices, QVector3D &anchor )
{
  bool found = false;
  for ( const QVector3D &v : vertices )
  {
    if ( !std::isfinite( v.x() ) || !std::isfinite( v.y() ) || !std::isfinite( v.z() ) ||
         std::abs( v.x() ) > 1e-5f || std::abs( v.y() ) > 1e-5f ) continue;
    if ( !found || v.z() > anchor.z() ) { anchor = v; found = true; }
  }
  return found;
}

inline bool project( const QVector3D &world, const QMatrix4x4 &viewMatrix,
                     const QMatrix4x4 &projection, const QSize &viewport,
                     QPointF &screen, double &depth )
{
  if ( viewport.isEmpty() ) return false;
  const QVector4D view = viewMatrix * QVector4D( world, 1 );
  const QVector4D clip = projection * view;
  if ( !std::isfinite( clip.w() ) || clip.w() <= 0 ||
       std::abs( clip.x() ) > clip.w() || std::abs( clip.y() ) > clip.w() ||
       std::abs( clip.z() ) > clip.w() )
    return false;
  screen = QPointF( ( clip.x() / clip.w() + 1 ) * viewport.width() / 2.0,
                    ( 1 - clip.y() / clip.w() ) * viewport.height() / 2.0 );
  depth = -view.z();
  return std::isfinite( screen.x() ) && std::isfinite( screen.y() ) && depth > 0;
}

struct NearestPoint
{
  int index = -1;
  int pixel = 13;
  double depth = std::numeric_limits<double>::max();

  void consider( int candidate, double distance, double candidateDepth )
  {
    if ( !std::isfinite( distance ) || !std::isfinite( candidateDepth ) ||
         distance < 0 || distance > 12 || candidateDepth <= 0 ) return;
    // Within one screen pixel, prefer the front sample.
    const int candidatePixel = static_cast<int>( std::floor( distance ) );
    if ( candidatePixel < pixel || ( candidatePixel == pixel && candidateDepth < depth ) )
    {
      index = candidate;
      pixel = candidatePixel;
      depth = candidateDepth;
    }
  }
};
}

#endif
