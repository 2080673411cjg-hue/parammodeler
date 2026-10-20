#include "../buildmesh.h"
#include "../parammodeler_alignment_geometry.h"
#include "../parammodeler_dlutils.h"
#include "../parammodeler_fit_metrics.h"
#include "../parammodeler_pcdloader.h"
#include "../parammodeler_pick3d_math.h"
#include "../parammodeler_regression_axes.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <stdexcept>

using Values = QMap<QString, double>;

// LHouse's declared data overload is not defined in buildmesh.cpp. Reproduce
// its two rectangular roof patches and six exterior walls without a Dock.
static MeshData lHouse( const LHouseParams &p )
{
  const float x = p.totalLength * (1-p.wingRatio), y = p.totalWidth * p.wingWidthRatio;
  const float length = p.totalLength, width = p.totalWidth, height = p.height;
  if ( x <= 0 || x >= length || y <= 0 || y >= width || height <= 0 ) return {};
  MeshData mesh;
  const QVector<QVector3D> perimeter{{0,0,0}, {length,0,0}, {length,y,0}, {x,y,0}, {x,width,0}, {0,width,0}};
  const QVector3D up(0,0,height);
  for ( int i = 0; i < perimeter.size(); ++i )
  {
    const auto a = perimeter[i], b = perimeter[(i+1)%perimeter.size()];
    mesh.addQuad(a,b,b+up,a+up);
  }
  for ( float z : {0.f, height} )
  {
    mesh.addQuad({0,0,z}, {x,0,z}, {x,width,z}, {0,width,z});
    mesh.addQuad({x,0,z}, {length,0,z}, {length,y,z}, {x,y,z});
  }
  return mesh;
}

static Values values( const QJsonObject &object )
{
  Values result;
  for ( auto it = object.begin(); it != object.end(); ++it )
    if ( it.value().isDouble() ) result.insert( it.key(), it.value().toDouble() );
  return result;
}

static MeshData build( const QString &name, const Values &v )
{
  const auto p = [&]( const char *key ) {
    if ( !v.contains( key ) ) throw std::runtime_error( std::string( "Missing mesh parameter: " ) + key );
    return v.value( key );
  };
  if ( name == "Cuboid" ) return BuildMesh::buildCuboid( CuboidParams{p("length"), p("width"), p("height")} );
  if ( name == "Cylinder" ) return BuildMesh::buildCylinder( CylinderParams{p("radius"), p("cylHeight")} );
  if ( name == "LHouse" ) return lHouse( LHouseParams{p("lTotalL"), p("lWingR"), p("lTotalW"), p("lWingWR"), p("lHeight")} );
  if ( name == "ConeCylinder" ) return BuildMesh::buildConeCylinder( ConeCylinderParams{p("ccRadius"), p("ccCylHeight"), p("ccConeHeight")} );
  if ( name == "GabledRoof" ) return BuildMesh::buildGabledRoof( GabledRoofParams{p("grLength"), p("grWidth"), p("grWallHeight"), p("grRoofHeight"), p("grRidgeRatio")} );
  if ( name == "PyramidRoof" ) return BuildMesh::buildPyramidRoof( PyramidRoofParams{p("prLength"), p("prWidth"), p("prWallHeight"), p("prRoofHeight")} );
  if ( name == "TruncatedPyramidRoof" ) return BuildMesh::buildTruncatedPyramidRoof( TruncatedPyramidRoofParams{p("tpBottomLength"), p("tpBottomWidth"), p("tpTopLength"), p("tpTopWidth"), p("tpWallHeight"), p("tpRoofHeight")} );
  if ( name == "HalfCylinderRoof" ) return BuildMesh::buildHalfCylinderRoof( HalfCylinderRoofParams{p("hcrLength"), p("hcrWidth"), p("hcrWallHeight")} );
  if ( name == "CylinderDome" ) return BuildMesh::buildCylinderHemisphere( CylinderHemisphereParams{p("chRadius"), p("chCylHeight"), p("chDomeHeight"), p("chBulge")} );
  if ( name == "IndentedCuboid" )
  {
    return BuildMesh::buildIndentedCuboid( IndentedCuboidParams{p("icOuterL"), p("icOuterW"), p("icOuterH"),
      p("icInnerL"), p("icInnerW"), p("icInnerH"), p("icOffsetX"), p("icOffsetY")} );
  }
  if ( name == "AsymmetricGableHouse" ) return BuildMesh::buildAsymmetricGableHouse( AsymmetricGableHouseParams{p("aghLength"), p("aghWidth"), p("aghWallHeight"), p("aghRoofHeight"), p("aghRidgeLen"), p("aghRidgeRatio")} );
  if ( name == "FourStageRoundTower" ) return BuildMesh::buildFourStageRoundTower( FourStageRoundTowerParams{p("ftBaseR"), p("ftBaseH"), p("ftMidH"), p("ftMidTopR"), p("ftMidRise"), p("ftConeH"), p("ftEaveRadius"), p("ftEaveHeight")} );
  if ( name == "TwoGableHouses" ) return BuildMesh::buildTwoGableHouses( TwoGableHousesParams{p("tgLength1"), p("tgLength2"), p("tgWidth"), p("tgWallHeight"), p("tgRoofHeight"), p("tgAngle"), p("tgRidgeRatio")} );
  throw std::runtime_error( "Unsupported class" );
}

static QVector<QVector3D> restored( const QString &path, const QJsonObject &info, bool &normalized )
{
  const PointCloud cloud = PointCloudLoader::load( path );
  if ( cloud.points.size() < 100 ) throw std::runtime_error( "Missing or too small cloud" );
  normalized = pointCloudLooksNormalizedForDisplay( cloud );
  QVector3D center;
  if ( !vectorFromJsonArray( info.value("center"), center ) ) throw std::runtime_error( "Missing cloud center" );
  const double scale = info.value("scale").toDouble();
  if ( scale <= 0 ) throw std::runtime_error( "Invalid cloud scale" );
  QVector<QVector3D> points;
  for ( const QVector3D &p : cloud.points )
  {
    QVector3D q = normalized ? p * static_cast<float>( scale ) + center : p;
    if ( normalized && q.z() < 0 ) q.setZ( 0 );
    points.append( q );
  }
  return points;
}

static void displayBBox( const QString &name, const QVector<QVector3D> &points,
                          QVector3D &minimum, QVector3D &maximum )
{
  QVector3D hardMin, hardMax, fullMin, fullMax;
  hardBBoxFromPoints( points, hardMin, hardMax );
  robustBBoxFromPoints( points, fullMin, fullMax );
  QVector<QVector3D> bottom;
  const double top = hardMin.z() + std::max( .5, (hardMax.z() - hardMin.z()) * .12 );
  for ( const auto &p : points ) if ( p.z() <= top ) bottom.append( p );
  bool useBottom = name != "FourStageRoundTower" && bottom.size() >= std::max(50, int(std::ceil(points.size() * .05)));
  QVector3D bottomMin, bottomMax;
  if ( useBottom )
  {
    robustBBoxFromPoints( bottom, bottomMin, bottomMax );
    const QVector3D fullRange = fullMax - fullMin, bottomRange = bottomMax - bottomMin;
    useBottom = (fullRange.x() <= 1e-6 || bottomRange.x() >= fullRange.x() * .25) &&
                (fullRange.y() <= 1e-6 || bottomRange.y() >= fullRange.y() * .25);
  }
  minimum = useBottom ? QVector3D(bottomMin.x(), bottomMin.y(), hardMin.z()) : QVector3D(fullMin.x(), fullMin.y(), hardMin.z());
  maximum = useBottom ? QVector3D(bottomMax.x(), bottomMax.y(), fullMax.z()) : fullMax;
}

static QVector3D align( const QString &name, const QString &path, const MeshData &mesh,
                         const QVector<QVector3D> &points, const QJsonObject &info,
                         bool normalized, double yaw, QString &detail )
{
  QMatrix4x4 rotation;
  rotation.rotate( yaw, 0, 0, 1 );
  QVector<QVector3D> vertices;
  for ( const auto &v : mesh.vertices ) vertices.append( rotation.map(v) );
  QVector3D meshMin, meshMax, cloudMin, cloudMax;
  hardBBoxFromPoints( vertices, meshMin, meshMax );
  displayBBox( name, points, cloudMin, cloudMax );
  QVector3D upper, offset, center;
  const bool hasUpper = usesSemanticUpperAlignment(name) && ParamModelerPicking::upperAnchor(mesh.vertices, upper) &&
    ParamModelerAlignmentGeometry::upperOffsetFromRotatedMesh(mesh.vertices, rotation, upper, offset) && offset.z() > 1e-6;
  QVector3D target;
  vectorFromJsonArray( info.value("center"), center );
  if ( hasUpper && semanticUpperPointCloudTarget(path, name, yaw, offset.z(), true, normalized, center,
                                                info.value("scale").toDouble(), target, detail) )
    return target - rotation.map(upper);
  if ( hasUpper && offset.z() <= (cloudMax.z() - cloudMin.z()) * 1.5 )
  {
    detail = "display-upper-fallback";
    return cloudMin + offset - rotation.map(upper);
  }
  detail = "display-bbox";
  if ( BuildMesh::usesCornerAnchor(name) ) return cloudMin - meshMin;
  return QVector3D((cloudMin.x()+cloudMax.x()-meshMin.x()-meshMax.x())*.5f,
                   (cloudMin.y()+cloudMax.y()-meshMin.y()-meshMax.y())*.5f, cloudMin.z()-meshMin.z());
}

static QString alignInterior( Values &v, const QVector<QVector3D> &points, const QMatrix4x4 &pose )
{
  QVector<QVector3D> local;
  const QMatrix4x4 inverse = pose.inverted();
  for ( const auto &p : points ) local.append( inverse.map(p) );
  QVector3D outerMin, outerMax, hardMin, hardMax, innerMin, innerMax;
  robustBBoxFromPoints( local, outerMin, outerMax );
  hardBBoxFromPoints( local, hardMin, hardMax );
  double depth = 0;
  if ( !estimateIndentedInnerBox(local, outerMin, outerMax, hardMin.z(), hardMax.z()-hardMin.z(), innerMin, innerMax, depth) )
    return "unavailable";
  const double dx = std::max(0.0, v.value("icOuterL")-v.value("icInnerL"));
  const double dy = std::max(0.0, v.value("icOuterW")-v.value("icInnerW"));
  if ( dx < 1e-6 || dy < 1e-6 ) return "no-room";
  v["icOffsetX"] = qBound(0.0, double(innerMin.x()), dx);
  v["icOffsetY"] = qBound(0.0, double(innerMin.y()), dy);
  return "applied";
}

static QJsonObject score( const MeshData &mesh, const QVector<QVector3D> &points,
                           const QMatrix4x4 &pose, double diagonal )
{
  const auto metrics = ParamModelerFit::evaluate(mesh, points, pose);
  if ( !metrics.count ) throw std::runtime_error( "No valid surface distances" );
  std::vector<ParamModelerFit::Triangle> triangles;
  for ( int i = 0; i+2 < mesh.indices.size(); i += 3 )
    triangles.push_back({ParamModelerFit::vector(pose.map(mesh.vertices[mesh.indices[i]])),
                         ParamModelerFit::vector(pose.map(mesh.vertices[mesh.indices[i+1]])),
                         ParamModelerFit::vector(pose.map(mesh.vertices[mesh.indices[i+2]]))});
  const Eigen::KdBVH<double, 3, ParamModelerFit::Triangle> tree(triangles.begin(), triangles.end());
  int fixedInliers = 0, relativeInliers = 0;
  for ( const auto &p : points )
  {
    ParamModelerFit::PointQuery query{ParamModelerFit::vector(p)};
    const double squared = Eigen::BVMinimize(tree, query);
    fixedInliers += squared <= .2*.2;
    relativeInliers += squared <= std::pow(diagonal*.01, 2);
  }
  return {{"rmse", metrics.rmse}, {"p95", metrics.p95}, {"points", metrics.count},
          {"inlier_0_2", double(fixedInliers)/metrics.count}, {"inlier_1pct", double(relativeInliers)/metrics.count}};
}

int main( int argc, char **argv )
{
  QCoreApplication application(argc, argv);
  if ( argc != 3 && argc != 4 ) return 2;
  const bool validateRaw = argc == 4 && QString::fromLocal8Bit(argv[3]) == "--validate-raw";
  QFile input(QString::fromLocal8Bit(argv[1]));
  if ( !input.open(QIODevice::ReadOnly) ) return 2;
  const auto manifest = QJsonDocument::fromJson(input.readAll()).object();
  QJsonArray results;
  int processed = 0;
  for ( const auto &sampleValue : manifest.value("samples").toArray() )
  {
    const auto sample = sampleValue.toObject();
    const QString name = sample.value("class").toString(), path = sample.value(validateRaw ? "raw_cloud" : "cloud").toString();
    const auto record = sample.value("record").toObject();
    const auto info = validateRaw ? sample.value("raw_info").toObject() : record.value("pointCloudInfo").toObject();
    const auto params = record.value("params").toObject();
    const double yaw = params.value("rz").toDouble();
    try
    {
      bool normalized = false;
      const auto allPoints = restored(path, info, normalized);
      QVector<QVector3D> points;
      const int count = std::min(allPoints.size(), 2048);
      for ( int i = 0; i < count; ++i ) points.append(allPoints[qint64(i)*allPoints.size()/count]);
      const auto truth = pointNetParamsToUiParams(name, values(params));
      const MeshData truthMesh = build(name, truth);
      QVector3D meshMin, meshMax;
      hardBBoxFromPoints(truthMesh.vertices, meshMin, meshMax);
      const double diagonal = (meshMax-meshMin).length();
      if ( truthMesh.isEmpty() || diagonal <= 0 ) throw std::runtime_error("Invalid ground truth mesh");
      const auto prediction = ParamModelerRegressionAxes::paramsInMetadataFrame(name, values(sample.value("prediction").toObject()), yaw);
      const QStringList groups = validateRaw ? QStringList{"baseline"} : QStringList{"baseline", "alignment", "shape", "combined"};
      for ( const QString &group : groups )
      {
        QJsonObject result{{"class", name}, {"file", sample.value("file")}, {"group", group}, {"log", sample.value("log")}};
        try
        {
          const bool predicted = group == "shape" || group == "combined";
          const bool automatic = group == "alignment" || group == "combined";
          if ( predicted && !sample.value("prediction_valid").toBool() ) throw std::runtime_error("Stale prediction labels");
          Values v = predicted ? pointNetParamsToUiParams(name, prediction) : truth;
          MeshData mesh = build(name, v);
          if ( mesh.isEmpty() ) throw std::runtime_error("Invalid predicted mesh");
          QString detail = "truth-pose", interior = "not-requested";
          const QVector3D translation = automatic ? align(name, path, mesh, allPoints, info, normalized, yaw, detail) : QVector3D();
          QMatrix4x4 pose;
          pose.translate(translation);
          pose.rotate(yaw, 0, 0, 1);
          if ( automatic && name == "IndentedCuboid" )
          {
            interior = alignInterior(v, allPoints, pose);
            mesh = build(name, v);
          }
          const auto metrics = score(mesh, points, pose, diagonal);
          for ( auto it = metrics.begin(); it != metrics.end(); ++it ) result.insert(it.key(), it.value());
          result.insert("status", "ok");
          result.insert("xy_error", std::hypot(translation.x(), translation.y()));
          result.insert("z_error", std::abs(translation.z()));
          result.insert("tx", translation.x()); result.insert("ty", translation.y()); result.insert("tz", translation.z());
          result.insert("target_source", detail); result.insert("interior", interior);
          if ( name == "IndentedCuboid" )
          {
            const QVector3D localDelta(v.value("icOffsetX")-truth.value("icOffsetX"), v.value("icOffsetY")-truth.value("icOffsetY"), 0);
            const QVector3D worldDelta = pose.mapVector(localDelta) + translation;
            result.insert("inner_local_xy_error", double(localDelta.length()));
            result.insert("inner_xy_error", std::hypot(worldDelta.x(), worldDelta.y()));
            result.insert("inner_top_z_error", std::abs(v.value("icOuterH")-truth.value("icOuterH")+translation.z()));
          }
        }
        catch ( const std::exception &error ) { result.insert("status", QString::fromLocal8Bit(error.what())); }
        results.append(result);
      }
    }
    catch ( const std::exception &error )
    {
      results.append(QJsonObject{{"class", name}, {"file", sample.value("file")}, {"group", "load"}, {"status", QString::fromLocal8Bit(error.what())}});
    }
    if ( ++processed % 25 == 0 ) QTextStream(stdout) << name << ": " << processed << " samples\n" << flush;
  }
  QFile output(QString::fromLocal8Bit(argv[2]));
  if ( !output.open(QIODevice::WriteOnly) ) return 2;
  output.write(QJsonDocument(results).toJson());
  return 0;
}
