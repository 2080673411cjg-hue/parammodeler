#ifndef PARAMMODELER_ALIGNMENT_GEOMETRY_H
#define PARAMMODELER_ALIGNMENT_GEOMETRY_H

#include "parammodeler_cornerfit.h"
#include <QMatrix4x4>
#include <QPointF>
#include <QVector>
#include <QVector3D>
#include <Eigen/QR>

#include <algorithm>
#include <cmath>

namespace ParamModelerAlignmentGeometry
{
inline bool upperOffsetFromRotatedMesh( const QVector<QVector3D> &vertices,
                                        const QMatrix4x4 &rotation,
                                        const QVector3D &localUpper,
                                        QVector3D &offset )
{
  if ( vertices.isEmpty() )
    return false;

  QVector3D rotatedMin = rotation.map( vertices.first() );
  for ( const QVector3D &vertex : vertices )
  {
    const QVector3D p = rotation.map( vertex );
    rotatedMin.setX( std::min( rotatedMin.x(), p.x() ) );
    rotatedMin.setY( std::min( rotatedMin.y(), p.y() ) );
    rotatedMin.setZ( std::min( rotatedMin.z(), p.z() ) );
  }
  offset = rotation.map( localUpper ) - rotatedMin;
  return std::isfinite( offset.x() ) && std::isfinite( offset.y() ) &&
         std::isfinite( offset.z() );
}

inline bool bottomFootprintAgreesWithFull( const QVector3D &bottomMin, const QVector3D &bottomMax,
                                           const QVector3D &fullMin, const QVector3D &fullMax )
{
  const double length = fullMax.x() - fullMin.x();
  const double width = fullMax.y() - fullMin.y();
  if ( length <= 0.0 || width <= 0.0 )
    return false;

  const double xTolerance = std::max( 0.05, length * 0.03 );
  const double yTolerance = std::max( 0.05, width * 0.03 );
  return std::abs( bottomMin.x() - fullMin.x() ) <= xTolerance &&
         std::abs( bottomMax.x() - fullMax.x() ) <= xTolerance &&
         std::abs( bottomMin.y() - fullMin.y() ) <= yTolerance &&
         std::abs( bottomMax.y() - fullMax.y() ) <= yTolerance;
}

inline bool supportedFlatRoofTop( const QVector<QVector3D> &points, double &topZ )
{
  QVector<double> heights;
  heights.reserve( points.size() );
  for ( const QVector3D &p : points )
    if ( std::isfinite( p.z() ) )
      heights.append( p.z() );
  if ( heights.size() < 80 )
    return false;

  std::sort( heights.begin(), heights.end() );
  const double low = heights[static_cast<int>( heights.size() * 0.05 )];
  const double high = heights[static_cast<int>( heights.size() * 0.95 )];
  if ( high - low <= 0.01 )
    return false;

  constexpr int bins = 32;
  const double step = ( high - low ) / bins;
  int counts[bins] = {};
  for ( const double z : heights )
  {
    if ( z < low || z > high )
      continue;
    const int index = std::min( bins - 1, static_cast<int>( ( z - low ) / step ) );
    if ( index >= bins / 2 )
      ++counts[index];
  }

  int bestBin = bins / 2;
  for ( int i = bestBin + 1; i < bins; ++i )
    if ( counts[i] > counts[bestBin] )
      bestBin = i;

  const double center = low + ( bestBin + 0.5 ) * step;
  QVector<double> support;
  for ( const double z : heights )
    if ( std::abs( z - center ) <= step * 2.0 )
      support.append( z );
  if ( support.size() < std::max( 24, static_cast<int>( heights.size() * 0.08 ) ) )
    return false;

  topZ = support[support.size() / 2];
  return true;
}

inline bool indentedRimTop( const QVector<QVector3D> &points,
                            const QVector3D &minimum, const QVector3D &maximum, double &topZ )
{
  const double xBand = ( maximum.x() - minimum.x() ) * 0.08;
  const double yBand = ( maximum.y() - minimum.y() ) * 0.08;
  if ( xBand <= 0.0 || yBand <= 0.0 ) return false;
  QVector<QVector3D> rim;
  for ( const auto &p : points )
    if ( p.x() <= minimum.x() + xBand || p.x() >= maximum.x() - xBand ||
         p.y() <= minimum.y() + yBand || p.y() >= maximum.y() - yBand ) rim.append( p );
  return supportedFlatRoofTop( rim, topZ );
}

inline bool roofPlaneEave( const QVector<QVector3D> &points,
                           const QVector3D &minimum, const QVector3D &maximum,
                           bool gabled, double baseZ, double height, double &eaveZ )
{
  const double spans[2] = { maximum.x() - minimum.x(), maximum.y() - minimum.y() };
  if ( spans[0] <= 0.0 || spans[1] <= 0.0 || height <= 0.0 ) return false;
  QVector<double> candidates;
  for ( int axis = gabled ? 1 : 0; axis < 2; ++axis )
    for ( int side = 0; side < 2; ++side )
    {
      const int along = 1 - axis;
      const double boundary = side ? maximum[axis] : minimum[axis];
      std::vector<ParamModelerCornerFit::Point> patch;
      for ( const auto &p : points )
      {
        const double inward = ( side ? boundary - p[axis] : p[axis] - boundary ) / spans[axis];
        const double lengthwise = ( p[along] - minimum[along] ) / spans[along];
        if ( inward >= 0.02 && inward <= 0.20 && lengthwise >= 0.35 && lengthwise <= 0.65 &&
             p.z() > baseZ + height * 0.15 ) patch.emplace_back( p.x(), p.y(), p.z() );
      }
      if ( patch.size() < 16 ) continue;
      std::sort( patch.begin(), patch.end(), []( const auto &a, const auto &b ) { return a.z() < b.z(); } );
      const double radius = std::hypot( spans[axis] * 0.22, spans[along] * 0.35 );
      ParamModelerCornerFit::Plane plane;
      for ( double fraction : { 0.5, 0.8 } )
      {
        if ( !ParamModelerCornerFit::fitPlane( patch, patch[static_cast<size_t>( patch.size() * fraction )], radius, plane ) ) continue;
        const double nz = std::abs( plane.normal.z() );
        // A top plateau or an exterior wall is not a sloping roof face.
        if ( nz < 0.15 || nz > 0.9995 || plane.rms > radius * 0.02 || plane.inliers < 16 ||
             plane.normal[axis] * ( side ? -1.0 : 1.0 ) * plane.normal.z() >= 0.0 ) continue;
        Eigen::Vector3d edge( (minimum.x()+maximum.x())*.5, (minimum.y()+maximum.y())*.5, 0 );
        edge[axis] = boundary;
        const double z = plane.center.z() - (plane.normal.x()*(edge.x()-plane.center.x()) +
                         plane.normal.y()*(edge.y()-plane.center.y())) / plane.normal.z();
        if ( std::isfinite(z) && z > baseZ + height * 0.10 && z < baseZ + height * 0.95 )
        {
          candidates.append( z );
          break;
        }
      }
    }
  if ( candidates.isEmpty() ) return false;
  std::sort( candidates.begin(), candidates.end() );
  eaveZ = candidates[candidates.size()/2];
  return true;
}

struct Circle
{
  QPointF center;
  double radius = 0.0;
  double rms = 0.0;
};

inline bool fitCircle( const QVector<QPointF> &points, Circle &circle )
{
  if ( points.size() < 24 ) return false;
  Eigen::Vector2d origin = Eigen::Vector2d::Zero();
  for ( const auto &p : points ) origin += Eigen::Vector2d(p.x(), p.y());
  origin /= points.size();
  Eigen::MatrixXd a(points.size(), 3);
  Eigen::VectorXd b(points.size());
  for ( int i = 0; i < points.size(); ++i )
  {
    const Eigen::Vector2d p = Eigen::Vector2d(points[i].x(), points[i].y()) - origin;
    a.row(i) << 2*p.x(), 2*p.y(), 1;
    b[i] = p.squaredNorm();
  }
  const auto initial = a.colPivHouseholderQr();
  if ( initial.rank() < 3 ) return false;
  Eigen::Vector3d solution = initial.solve(b);
  Eigen::Vector2d center = solution.head<2>();
  double radius = std::sqrt(std::max(0.0, solution[2] + center.squaredNorm()));
  if ( !solution.allFinite() || radius <= 0 ) return false;
  double sigma = 0;
  for ( int iteration = 0; iteration < 8; ++iteration )
  {
    QVector<double> errors;
    for ( const auto &p : points ) errors.append(std::abs((Eigen::Vector2d(p.x(),p.y())-origin-center).norm()-radius));
    std::sort(errors.begin(), errors.end());
    sigma = std::max(radius*0.002, errors[errors.size()/2]*1.4826);
    for ( int i = 0; i < points.size(); ++i )
    {
      const Eigen::Vector2d delta = Eigen::Vector2d(points[i].x(),points[i].y())-origin-center;
      const double length = delta.norm(), error = length-radius;
      const double u = error/(4.685*sigma);
      const double weight = length > 1e-9 && std::abs(u) < 1 ? 1-u*u : 0;
      a.row(i) << weight*(length > 1e-9 ? delta.x()/length : 0), weight*(length > 1e-9 ? delta.y()/length : 0), weight;
      b[i] = weight*error;
    }
    const auto solver = a.colPivHouseholderQr();
    if ( solver.rank() < 3 ) return false;
    const Eigen::Vector3d step = solver.solve(b);
    center += step.head<2>();
    radius += step[2];
    if ( !step.allFinite() || radius <= 0 ) return false;
  }
  Eigen::Matrix2d spread = Eigen::Matrix2d::Zero();
  Eigen::Vector2d mean = Eigen::Vector2d::Zero();
  int count = 0;
  double sum = 0;
  for ( const auto &p : points )
  {
    const Eigen::Vector2d delta = Eigen::Vector2d(p.x(),p.y())-origin-center;
    const double error = delta.norm()-radius;
    if ( std::abs(error) > 3*sigma ) continue;
    const Eigen::Vector2d direction = delta.normalized();
    mean += direction;
    spread += direction*direction.transpose();
    sum += error*error;
    ++count;
  }
  if ( count < 24 || count < points.size()*0.6 ) return false;
  spread -= mean*mean.transpose()/count;
  const Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> eig(spread);
  if ( eig.info() != Eigen::Success || eig.eigenvalues()[0] < eig.eigenvalues()[1]*0.005 ) return false;
  const double rms = std::sqrt(sum/count);
  if ( rms > radius*0.04 ) return false;
  circle = { QPointF(center.x()+origin.x(), center.y()+origin.y()), radius, rms };
  return true;
}

inline bool roundBaseCenter( const QVector<QVector3D> &points,
                             const QVector3D &minimum, const QVector3D &maximum, QPointF &center )
{
  QVector<double> heights;
  for ( const auto &p : points ) heights.append(p.z());
  if ( heights.size() < 100 ) return false;
  std::sort(heights.begin(), heights.end());
  const double top = heights[heights.size()/10];
  QVector<QPointF> wall;
  const double span = std::max(maximum.x()-minimum.x(), maximum.y()-minimum.y());
  if ( span <= 0 ) return false;
  for ( const auto &p : points )
    if ( p.z() <= top && p.x() >= minimum.x()-span*.05 && p.x() <= maximum.x()+span*.05 &&
         p.y() >= minimum.y()-span*.05 && p.y() <= maximum.y()+span*.05 ) wall.append(QPointF(p.x(),p.y()));
  Circle circle;
  if ( !fitCircle(wall, circle) || circle.radius < span*.35 || circle.radius > span ||
       std::abs(circle.center.x()-(minimum.x()+maximum.x())*.5) > span*.4 ||
       std::abs(circle.center.y()-(minimum.y()+maximum.y())*.5) > span*.4 ) return false;
  center = circle.center;
  return true;
}

inline bool towerEaveOutlineCenter( const QVector<QVector3D> &points, const QVector3D &minimum,
                                    const QVector3D &maximum, double span, QPointF &center )
{
  // The eave is the widest part. Projecting the outline keeps its support
  // together when vertical noise spreads a thin eave across several slices.
  const QPointF boxCenter( (minimum.x()+maximum.x())*.5, (minimum.y()+maximum.y())*.5 );
  constexpr double pi = 3.14159265358979323846;
  constexpr int sectors = 36;
  struct OutlinePoint { QPointF xy; double radius; };
  QVector<OutlinePoint> bins[sectors];
  const double padding = span*.05;
  for ( const auto &p : points )
  {
    if ( !std::isfinite(p.x()) || !std::isfinite(p.y()) ||
         p.x() < minimum.x()-padding || p.x() > maximum.x()+padding ||
         p.y() < minimum.y()-padding || p.y() > maximum.y()+padding ) continue;
    const double x = p.x()-boxCenter.x(), y = p.y()-boxCenter.y();
    const int sector = std::min( sectors-1, static_cast<int>( (std::atan2(y,x)+pi)*sectors/(2*pi) ) );
    bins[sector].append( {QPointF(p.x(),p.y()), std::hypot(x,y)} );
  }
  QVector<QPointF> outline;
  for ( auto &bin : bins )
  {
    if ( bin.size() < 5 ) continue;
    // Use one near-edge point per direction so dense arcs and isolated maxima
    // do not dominate the fit.
    const int edge = static_cast<int>( (bin.size()-1)*.98 );
    std::nth_element( bin.begin(), bin.begin()+edge, bin.end(),
                      []( const OutlinePoint &a, const OutlinePoint &b ) { return a.radius < b.radius; } );
    outline.append( bin[edge].xy );
  }
  Circle circle;
  if ( !fitCircle(outline, circle) || circle.radius < span*.4 || circle.radius > span*.65 ||
       circle.rms > circle.radius*.025 ||
       std::hypot(circle.center.x()-boxCenter.x(), circle.center.y()-boxCenter.y()) > span*.2 ) return false;
  QVector<double> angles;
  const double radialTolerance = std::max( 3*circle.rms, circle.radius*.01 );
  for ( const auto &p : outline )
  {
    const double x = p.x()-circle.center.x(), y = p.y()-circle.center.y();
    if ( std::abs(std::hypot(x,y)-circle.radius) <= radialTolerance ) angles.append( std::atan2(y,x) );
  }
  if ( angles.size() < 24 ) return false;
  std::sort( angles.begin(), angles.end() );
  // At least 270 degrees of angular coverage, measured by the largest gap.
  if ( angles.first()+2*pi-angles.last() > pi/2 ) return false;
  for ( int i = 1; i < angles.size(); ++i )
    if ( angles[i]-angles[i-1] > pi/2 ) return false;
  center = circle.center;
  return true;
}

inline bool towerCenter( const QVector<QVector3D> &points,
                         const QVector3D &minimum, const QVector3D &maximum, QPointF &center )
{
  if ( points.size() < 100 ) return false;
  QVector<double> heights;
  for ( const auto &p : points ) heights.append( p.z() );
  std::sort( heights.begin(), heights.end() );
  const double low = heights[heights.size()/50];
  const double high = heights[heights.size()*9/10];
  const double span = std::max( maximum.x()-minimum.x(), maximum.y()-minimum.y() );
  if ( high <= low || span <= 0 ) return false;
  QVector<Circle> rings;
  constexpr int bands = 16;
  for ( int band = 0; band < bands; ++band )
  {
    const double bottom = low + (high-low)*band/bands;
    const double top = low + (high-low)*(band+1)/bands;
    QVector<QPointF> slice;
    for ( const auto &p : points )
      if ( p.z() >= bottom && p.z() < top ) slice.append( {p.x(), p.y()} );
    Circle ring;
    if ( fitCircle( slice, ring ) && ring.radius >= span*.25 && ring.radius <= span*.75 &&
         ring.rms <= ring.radius*.025 &&
         std::hypot( ring.center.x()-(minimum.x()+maximum.x())*.5,
                     ring.center.y()-(minimum.y()+maximum.y())*.5 ) <= span*.2 ) rings.append( ring );
  }
  // Concentric sections must independently support the same horizontal center.
  int best = -1, support = 0;
  const double tolerance = span*.015;
  for ( int i = 0; i < rings.size(); ++i )
  {
    int count = 0;
    for ( const auto &ring : rings )
      if ( std::hypot( ring.center.x()-rings[i].center.x(), ring.center.y()-rings[i].center.y() ) <= tolerance ) ++count;
    if ( count > support ) { support = count; best = i; }
  }
  if ( support < 3 || support*2 <= rings.size() )
    return towerEaveOutlineCenter( points, minimum, maximum, span, center );
  QVector<double> xs, ys;
  for ( const auto &ring : rings )
    if ( std::hypot( ring.center.x()-rings[best].center.x(), ring.center.y()-rings[best].center.y() ) <= tolerance )
    { xs.append( ring.center.x() ); ys.append( ring.center.y() ); }
  std::sort( xs.begin(), xs.end() );
  std::sort( ys.begin(), ys.end() );
  center = QPointF( xs[xs.size()/2], ys[ys.size()/2] );
  return true;
}

inline bool exteriorWallCorner( const QVector<QVector3D> &points,
                                const QVector3D &minimum, const QVector3D &maximum,
                                double cornerZ, QPointF &corner,
                                std::array<ParamModelerCornerFit::Plane, 2> *walls = nullptr )
{
  const double spans[2] = { maximum.x()-minimum.x(), maximum.y()-minimum.y() };
  const double height = maximum.z()-minimum.z();
  if ( spans[0] <= 0 || spans[1] <= 0 || height <= 0 ) return false;
  Eigen::Matrix2d normals = Eigen::Matrix2d::Identity();
  Eigen::Vector2d offsets( corner.x(), corner.y() );
  bool found = false;
  for ( int axis = 0; axis < 2; ++axis )
  {
    const int along = 1-axis;
    std::vector<ParamModelerCornerFit::Point> patch;
    for ( const auto &p : points )
    {
      const double inward = (p[axis]-minimum[axis])/spans[axis];
      const double position = (p[along]-minimum[along])/spans[along];
      const double z = (p.z()-minimum.z())/height;
      if ( inward >= -.08 && inward <= .10 && position >= .05 && position <= .45 &&
           z >= .12 && z <= .65 ) patch.emplace_back( p.x(), p.y(), p.z() );
    }
    if ( patch.size() < 24 ) continue;
    std::sort( patch.begin(), patch.end(), [axis]( const auto &a, const auto &b ) { return a[axis] < b[axis]; } );
    const double radius = std::hypot( spans[along]*.45, height*.60 );
    for ( double fraction : { .25, .5, .75 } )
    {
      ParamModelerCornerFit::Plane plane;
      if ( !ParamModelerCornerFit::fitPlane( patch, patch[static_cast<size_t>(patch.size()*fraction)], radius, plane ) ||
           plane.inliers < 24 || std::abs(plane.normal[axis]) < .985 || plane.rms > radius*.02 ) continue;
      const double offset = plane.normal.dot(plane.center) - plane.normal.z()*cornerZ;
      const double boundary = (offset-plane.normal[along]*minimum[along])/plane.normal[axis];
      if ( std::abs(boundary-minimum[axis]) > spans[axis]*.08 ) continue;
      normals.row(axis) = plane.normal.head<2>();
      offsets[axis] = offset;
      if ( walls ) (*walls)[axis] = plane;
      found = true;
      break;
    }
  }
  // Keep the original reference on an axis whose exterior wall is missing.
  if ( !found ) return false;
  const Eigen::Vector2d intersection = normals.fullPivLu().solve(offsets);
  if ( !intersection.allFinite() ) return false;
  corner = QPointF( intersection.x(), intersection.y() );
  return true;
}

inline bool firstGableRoofCorner( const QVector<QVector3D> &points,
                                 const QVector3D &minimum, const QVector3D &maximum,
                                 const QPointF &fallbackCorner, double fallbackZ, QVector3D &corner )
{
  const double length = maximum.x()-minimum.x(), height = maximum.z()-minimum.z();
  if ( length <= 0 || height <= 0 ) return false;
  std::array<ParamModelerCornerFit::Plane, 2> walls;
  QPointF wallCorner = fallbackCorner;
  QVector<QVector3D> belowEave;
  for ( const auto &p : points )
    if ( p.z() < fallbackZ-height*.08 ) belowEave.append( p );
  if ( !exteriorWallCorner( belowEave, minimum, maximum, fallbackZ, wallCorner, &walls ) ||
       walls[1].radius <= 0 ) return false;
  // This upright primitive has vertical walls; do not extrapolate a noise-induced tilt.
  for ( auto &wall : walls )
    if ( wall.radius > 0 )
    {
      wall.normal.z() = 0;
      wall.normal.normalize();
    }
  wallCorner.setY( walls[1].center.y()-walls[1].normal.x()*(wallCorner.x()-walls[1].center.x())/walls[1].normal.y() );

  // The early X strip belongs to the first house; the full Y extent includes
  // the turned second house and must not set the first roof's sampling width.
  QVector<double> ys;
  QVector<QVector3D> firstSection;
  for ( const auto &p : points )
    if ( p.x() >= minimum.x()+length*.04 && p.x() <= minimum.x()+length*.25 &&
         p.z() > minimum.z()+height*.15 )
    { ys.append( p.y() ); firstSection.append( p ); }
  if ( ys.size() < 50 ) return false;
  std::sort( ys.begin(), ys.end() );
  const double width = ys[ys.size()*95/100]-wallCorner.y();
  if ( width <= 0 ) return false;
  std::sort( firstSection.begin(), firstSection.end(), []( const auto &a, const auto &b ) { return a.z() < b.z(); } );
  QVector<double> ridgeYs;
  for ( int i = firstSection.size()*85/100; i < firstSection.size(); ++i ) ridgeYs.append(firstSection[i].y());
  std::sort( ridgeYs.begin(), ridgeYs.end() );
  const double ridgeDistance = ridgeYs[ridgeYs.size()/2]-wallCorner.y();
  if ( ridgeDistance < width*.10 || ridgeDistance > width*.90 ) return false;
  std::vector<ParamModelerCornerFit::Point> patch;
  for ( const auto &p : firstSection )
    if ( p.y() >= wallCorner.y()+ridgeDistance*.20 && p.y() <= wallCorner.y()+ridgeDistance*.80 &&
         p.z() > fallbackZ+height*.05 ) patch.emplace_back( p.x(), p.y(), p.z() );
  if ( patch.size() < 20 ) return false;
  std::sort( patch.begin(), patch.end(), []( const auto &a, const auto &b ) { return a.z() < b.z(); } );
  const double radius = std::sqrt( std::pow(length*.12, 2)+std::pow(width*.35, 2)+std::pow(height*.5, 2) );
  if ( walls[0].radius <= 0 )
  {
    // Without the end wall, preserve the old X reference instead of inferring length.
    walls[0].normal = Eigen::Vector3d::UnitX();
    walls[0].center = Eigen::Vector3d( fallbackCorner.x(), fallbackCorner.y(), fallbackZ );
    walls[0].radius = (maximum-minimum).length();
  }
  for ( double fraction : { .55, .8 } )
  {
    ParamModelerCornerFit::Plane roof;
    if ( !ParamModelerCornerFit::fitPlane( patch, patch[static_cast<size_t>(patch.size()*fraction)], radius, roof ) ||
         roof.inliers < 20 || roof.inliers < roof.samples*.60 || std::abs(roof.normal.x()) > .08 ||
         std::abs(roof.normal.z()) < .15 || std::abs(roof.normal.z()) > .9995 ||
         roof.normal.y()*roof.normal.z() >= 0 || roof.rms > radius*.02 ) continue;
    // The first roof and its eave wall share a horizontal longitudinal axis.
    // Remove a noise-induced slope along that axis before extrapolating to the end.
    const Eigen::Vector2d wallNormal = walls[1].normal.head<2>().normalized();
    roof.normal.head<2>() = wallNormal*roof.normal.head<2>().dot(wallNormal);
    roof.normal.normalize();
    Eigen::Vector3d intersection;
    if ( !ParamModelerCornerFit::intersect( {walls[0], walls[1], roof}, intersection ) ||
         intersection.z() <= minimum.z()+height*.10 || intersection.z() >= maximum.z()-height*.05 ||
         std::abs(intersection.z()-fallbackZ) > height*.01 ||
         std::abs(intersection.x()-fallbackCorner.x()) > length*.10 ||
         std::abs(intersection.y()-fallbackCorner.y()) > (maximum.y()-minimum.y())*.10 ) continue;
    corner = QVector3D( intersection.x(), intersection.y(), intersection.z() );
    return true;
  }
  return false;
}

inline bool flatOuterRimCorner( const QVector<QVector3D> &points,
                                    const QVector3D &minimum, const QVector3D &maximum,
                                    double topZ, QPointF &corner,
                                    const std::array<bool, 2> &refineAxes = {true, true} )
{
  const double height = topZ-minimum.z();
  if ( height <= 0 ) return false;
  QVector<QVector3D> rim;
  for ( const auto &p : points )
    if ( std::abs(p.z()-topZ) <= std::max(.05, height*.025) ) rim.append(p);
  if ( rim.size() < 80 ) return false;
  bool found = false;
  constexpr int bins = 8;
  for ( int axis = 0; axis < 2; ++axis )
  {
    if ( !refineAxes[axis] ) continue;
    const int along = 1-axis;
    const double span = maximum[axis]-minimum[axis], alongSpan = maximum[along]-minimum[along];
    if ( span <= 0 || alongSpan <= 0 ) continue;
    QVector<double> slices[bins];
    for ( const auto &p : rim )
    {
      const double fraction = (p[along]-minimum[along])/alongSpan;
      if ( !std::isfinite(fraction) || !std::isfinite(p[axis]) || fraction < 0 || fraction >= 1 ) continue;
      slices[static_cast<int>(fraction*bins)].append(p[axis]);
    }
    QVector<double> edges;
    for ( const auto &slice : slices )
      if ( slice.size() >= 8 ) edges.append(*std::min_element(slice.begin(), slice.end()));
    if ( edges.size() < 6 ) continue;
    std::sort(edges.begin(), edges.end());
    const double candidate = edges[edges.size()/2];
    const double spread = edges[edges.size()*3/4]-edges[edges.size()/4];
    // Require a supported straight outer edge, not one extreme point or an inner rim.
    const double previous = axis == 0 ? corner.x() : corner.y();
    if ( spread > std::max(.05, span*.03) || candidate > std::min<double>(minimum[axis], previous) ||
         minimum[axis]-candidate > span*.05 ) continue;
    if ( axis == 0 ) corner.setX(candidate); else corner.setY(candidate);
    found = true;
  }
  return found;
}

inline bool indentedOuterWallCorner( const QVector<QVector3D> &points,
                                     const QVector3D &minimum, const QVector3D &maximum,
                                     double topZ, QPointF &corner )
{
  const double height = topZ-minimum.z();
  if ( height <= 0 ) return false;
  QVector<QVector3D> belowRim, rim;
  const double rimBand = std::max( .05, height*.025 );
  for ( const auto &p : points )
  {
    if ( p.z() < topZ-height*.08 ) belowRim.append(p);
    if ( std::abs(p.z()-topZ) <= rimBand ) rim.append(p);
  }
  if ( rim.size() < 40 ) return false;
  std::array<ParamModelerCornerFit::Plane, 2> walls;
  QPointF estimate = corner;
  if ( !exteriorWallCorner( belowRim, minimum, maximum, topZ, estimate, &walls ) ) return false;
  Eigen::Matrix2d normals = Eigen::Matrix2d::Identity();
  Eigen::Vector2d offsets(corner.x(), corner.y());
  bool found = false;
  for ( int axis = 0; axis < 2; ++axis )
  {
    auto wall = walls[axis];
    if ( wall.radius <= 0 ) continue;
    wall.normal.z() = 0;
    wall.normal.normalize();
    const double tolerance = std::max({.03, wall.rms*3, (maximum[axis]-minimum[axis])*.005});
    int outside = 0;
    for ( const auto &p : rim )
    {
      const Eigen::Vector3d delta(p.x()-wall.center.x(), p.y()-wall.center.y(), 0);
      const double inward = wall.normal.dot(delta)*(wall.normal[axis] > 0 ? 1 : -1);
      if ( inward < -tolerance ) ++outside;
    }
    // An inner recess wall has supported top-rim points on its outer side.
    if ( outside >= std::max(8, static_cast<int>(rim.size()*.03)) ) continue;
    normals.row(axis) = wall.normal.head<2>();
    offsets[axis] = wall.normal.dot(wall.center);
    found = true;
  }
  if ( !found ) return false;
  const Eigen::Vector2d intersection = normals.fullPivLu().solve(offsets);
  if ( !intersection.allFinite() ) return false;
  corner = QPointF(intersection.x(), intersection.y());
  return true;
}

inline bool halfCylinderSection( const QVector<QVector3D> &points,
                                 QVector3D &minimum, QVector3D &maximum, double &eaveZ )
{
  const double length = maximum.x()-minimum.x(), width = maximum.y()-minimum.y();
  if ( length <= 0 || width <= 0 ) return false;
  QVector<double> heights;
  for ( const auto &p : points )
    if ( p.x() > minimum.x()+length*.1 && p.x() < maximum.x()-length*.1 ) heights.append(p.z());
  if ( heights.size() < 50 ) return false;
  std::sort(heights.begin(), heights.end());
  const double low = heights[static_cast<int>(heights.size()*.35)];
  const double high = heights[static_cast<int>(heights.size()*.98)];
  QVector<QPointF> roof;
  for ( const auto &p : points )
    if ( p.x() > minimum.x()+length*.1 && p.x() < maximum.x()-length*.1 && p.z() >= low && p.z() <= high &&
         p.y() >= minimum.y()-width*.1 && p.y() <= maximum.y()+width*.1 ) roof.append(QPointF(p.y(),p.z()));
  Circle circle;
  if ( !fitCircle(roof, circle) ) return false;
  // The height cut seeds the arc; refit its radial inliers without truncating
  // their noisy apex or lower edge, which would otherwise flatten the section.
  QVector<QPointF> arc;
  const double tolerance = std::max(circle.radius*.08, circle.rms*4);
  for ( const auto &p : points )
    if ( p.x() > minimum.x()+length*.1 && p.x() < maximum.x()-length*.1 &&
         p.z() > circle.center.y()+circle.radius*.12 &&
         std::abs(std::hypot(p.y()-circle.center.x(),p.z()-circle.center.y())-circle.radius) <= tolerance )
      arc.append(QPointF(p.y(),p.z()));
  Circle refined;
  if ( fitCircle(arc, refined) ) circle = refined;
  if ( circle.radius < width*.35 || circle.radius > width ||
       circle.center.y() <= minimum.z() || circle.center.y() >= maximum.z() ) return false;
  minimum.setY(circle.center.x()-circle.radius);
  maximum.setY(circle.center.x()+circle.radius);
  eaveZ = circle.center.y();
  return true;
}

inline bool firstGableEave( const QVector<QVector3D> &points,
                            const QVector3D &footprintMin, const QVector3D &footprintMax,
                            double baseZ, double totalHeight, double &eaveZ )
{
  const double width = footprintMax.y() - footprintMin.y();
  if ( width <= 0.0 || totalHeight <= 0.0 )
    return false;

  // In the canonical frame, only the first house has an exterior wall at min Y.
  const double wallBand = std::max( 0.01, width * 0.008 );
  QVector<float> wallZ;
  for ( const QVector3D &p : points )
    if ( std::abs( p.y() - footprintMin.y() ) <= wallBand )
      wallZ.append( p.z() );
  if ( wallZ.size() < std::max( 24, static_cast<int>( points.size() * 0.02 ) ) )
    return false;

  std::sort( wallZ.begin(), wallZ.end() );
  const double low = wallZ[static_cast<int>( wallZ.size() * 0.10 )];
  const double high = wallZ[static_cast<int>( wallZ.size() * 0.95 )];
  if ( low > baseZ + totalHeight * 0.35 || high < baseZ + totalHeight * 0.15 )
    return false;
  eaveZ = high;
  return std::isfinite( eaveZ );
}

inline bool halfCylinderEave( const QVector<QVector3D> &points,
                              const QVector3D &footprintMin, const QVector3D &footprintMax,
                              double baseZ, double totalHeight, double &eaveZ )
{
  const double length = footprintMax.x() - footprintMin.x();
  const double width = footprintMax.y() - footprintMin.y();
  if ( length <= 0.0 || width <= 0.0 || totalHeight <= 0.0 )
    return false;

  const double radius = width * 0.5;
  const double centerY = ( footprintMin.y() + footprintMax.y() ) * 0.5;
  QVector<double> estimates;
  for ( const QVector3D &p : points )
  {
    const double x = p.x();
    const double y = p.y();
    if ( x <= footprintMin.x() + length * 0.10 || x >= footprintMax.x() - length * 0.10 ||
         y <= footprintMin.y() + width * 0.12 || y >= footprintMax.y() - width * 0.12 ||
         p.z() <= baseZ + totalHeight * 0.35 )
      continue;

    const double offset = y - centerY;
    const double arcHeight = std::sqrt( std::max( 0.0, radius * radius - offset * offset ) );
    estimates.append( p.z() - arcHeight );
  }
  if ( estimates.size() < std::max( 24, static_cast<int>( points.size() * 0.03 ) ) )
    return false;

  std::sort( estimates.begin(), estimates.end() );
  const double median = estimates[estimates.size() / 2];
  const double q10 = estimates[static_cast<int>( estimates.size() * 0.10 )];
  const double q90 = estimates[static_cast<int>( estimates.size() * 0.90 )];
  if ( !std::isfinite( median ) || q90 - q10 > std::max( 0.10, radius * 0.08 ) ||
       median <= baseZ || median >= baseZ + totalHeight )
    return false;

  eaveZ = median;
  return true;
}
}

#endif // PARAMMODELER_ALIGNMENT_GEOMETRY_H
