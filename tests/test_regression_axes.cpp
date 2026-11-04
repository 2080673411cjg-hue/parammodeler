#include "../parammodeler_regression_axes.h"
#include "../parammodeler_pointnet.h"

#include <cassert>
#include <limits>

using namespace ParamModelerRegressionAxes;

int main()
{
  assert( inferenceModelDescription( QStringLiteral( "PTv3" ),
    QStringLiteral( "/home/xubo/pointnet/ptv3_simple/logs/ptv3_cls_v9/best_model.pth" ) ) == QStringLiteral( "PTv3 v9" ) );
  assert( inferenceModelDescription( QStringLiteral( "PTv3" ),
    QStringLiteral( "E:\\pointnet\\ptv3_simple\\logs\\ptv3_reg_lshaped_v10_normals\\best_model.pth" ) ) == QStringLiteral( "PTv3 v10" ) );
  assert( inferenceModelDescription( QStringLiteral( "PTv3" ),
    QStringLiteral( "/models/ptv3_reg_fourstage_v3.1_normals/best_model.pth" ) ) == QStringLiteral( "PTv3 v3.1" ) );
  assert( inferenceModelDescription( QStringLiteral( "PTv3" ),
    QStringLiteral( "/models/ptv3_reg_lshaped_axis_trial_normals/best_model.pth" ) ) == QStringLiteral( "PTv3 ptv3_reg_lshaped_axis_trial_normals" ) );
  assert( inferenceModelDescription( QStringLiteral( "PTv3" ), {} ) == QStringLiteral( "PTv3 (version unconfirmed)" ) );
  assert( inferenceDirectionDescription( {} ) == QStringLiteral( "Unconfirmed (direction status not returned)" ) );
  assert( inferenceDirectionDescription( { { QStringLiteral( "applied" ), true } } ) == QStringLiteral( "Normalized for parameter prediction" ) );
  assert( inferenceDirectionDescription( { { QStringLiteral( "applied" ), false },
    { QStringLiteral( "status" ), QStringLiteral( "SKIPPED_CLASS" ) } } ) == QStringLiteral( "Not applied for this primitive" ) );
  assert( inferenceDirectionDescription( { { QStringLiteral( "applied" ), false },
    { QStringLiteral( "reason" ), QStringLiteral( "uncertain" ) } } ) == QStringLiteral( "Direction normalization not applied" ) );
  assert( inferenceDirectionDescription( { { QStringLiteral( "applied" ), QStringLiteral( "true" ) } } ) == QStringLiteral( "Unconfirmed (direction status not returned)" ) );
  const QString tower = QStringLiteral( "CurvedRoofRoundHouse" );
  assert( !towerRegressionSchemaError( tower, { { QStringLiteral( "middleBulge" ), .3 } } ).isEmpty() );
  assert( !towerRegressionSchemaError( tower, {} ).isEmpty() );
  assert( towerRegressionSchemaError( tower, { { QStringLiteral( "middleRise" ), .2 } } ).isEmpty() );
  assert( towerRegressionSchemaError( tower, { { QStringLiteral( "middleRise" ), 0 } } ).isEmpty() );
  assert( !towerRegressionSchemaError( tower, { { QStringLiteral( "middleRise" ), std::numeric_limits<double>::quiet_NaN() } } ).isEmpty() );
  assert( towerRegressionSchemaError( QStringLiteral( "DomeRoofHouse" ), { { QStringLiteral( "bulge" ), .3 } } ).isEmpty() );
  assert( shouldSwapFromSpans( 10.0, 14.0 ) );
  assert( !shouldSwapFromSpans( 14.0, 10.0 ) );
  assert( !shouldSwapFromSpans( 10.0, 10.7 ) );

  const QMap<QString, double> base = {
    { QStringLiteral( "geom_footprint_long" ), 14.0 },
    { QStringLiteral( "geom_footprint_short" ), 10.0 },
  };
  QMap<QString, double> cuboid = base;
  cuboid.insert( QStringLiteral( "length" ), 14.0 );
  cuboid.insert( QStringLiteral( "width" ), 10.0 );
  assert( remapCanonicalParams( QStringLiteral( "FlatRoofHouse" ), cuboid, true ).value( QStringLiteral( "length" ) ) == 10.0 );
  assert( remapCanonicalParams( QStringLiteral( "PyramidalRoofHouse" ), cuboid, true ).value( QStringLiteral( "width" ) ) == 14.0 );
  assert( remapCanonicalParams( QStringLiteral( "FlatRoofHouse" ), cuboid, false ) == cuboid );

  QMap<QString, double> legacy = cuboid;
  legacy.remove( QStringLiteral( "geom_footprint_long" ) );
  assert( remapCanonicalParams( QStringLiteral( "FlatRoofHouse" ), legacy, true ) == legacy );

  QMap<QString, double> truncated = base;
  truncated.insert( QStringLiteral( "bottomLength" ), 14.0 );
  truncated.insert( QStringLiteral( "bottomWidth" ), 10.0 );
  truncated.insert( QStringLiteral( "topLengthRatio" ), 0.7 );
  truncated.insert( QStringLiteral( "topWidthRatio" ), 0.4 );
  const auto swappedTop = remapCanonicalParams( QStringLiteral( "FlatTopHipRoofHouse" ), truncated, true );
  assert( swappedTop.value( QStringLiteral( "bottomLength" ) ) == 10.0 );
  assert( swappedTop.value( QStringLiteral( "bottomWidth" ) ) == 14.0 );
  assert( swappedTop.value( QStringLiteral( "topLengthRatio" ) ) == 0.4 );
  assert( swappedTop.value( QStringLiteral( "topWidthRatio" ) ) == 0.7 );

  QMap<QString, double> indented = base;
  indented.insert( QStringLiteral( "outerLength" ), 14.0 );
  indented.insert( QStringLiteral( "outerWidth" ), 10.0 );
  indented.insert( QStringLiteral( "leftMarginRatio" ), 0.1 );
  indented.insert( QStringLiteral( "rightMarginRatio" ), 0.2 );
  indented.insert( QStringLiteral( "frontMarginRatio" ), 0.3 );
  indented.insert( QStringLiteral( "backMarginRatio" ), 0.4 );
  const auto swappedIndent = remapCanonicalParams( QStringLiteral( "RecessedRoofHouse" ), indented, true );
  assert( swappedIndent.value( QStringLiteral( "outerLength" ) ) == 10.0 );
  assert( swappedIndent.value( QStringLiteral( "outerWidth" ) ) == 14.0 );
  assert( swappedIndent.value( QStringLiteral( "leftMarginRatio" ) ) == 0.3 );
  assert( swappedIndent.value( QStringLiteral( "rightMarginRatio" ) ) == 0.4 );
  assert( swappedIndent.value( QStringLiteral( "frontMarginRatio" ) ) == 0.1 );
  assert( swappedIndent.value( QStringLiteral( "backMarginRatio" ) ) == 0.2 );

  for ( const auto &primitive : { QStringLiteral( "GableRoofHouse" ), QStringLiteral( "ShortRidgeHipRoofHouse" ) } )
  {
    for ( const double yaw : { 0.0, 90.0, 120.0, 180.0 } )
    {
      const double canonicalYaw = yaw >= 90.0 ? yaw - 180.0 : yaw;
      QMap<QString, double> roof = { { QStringLiteral( "poseRotateZ" ), canonicalYaw },
                                    { QStringLiteral( "ridgeRatio" ), yaw >= 90.0 ? 0.7 : 0.3 } };
      const auto original = paramsInMetadataFrame( primitive, roof, yaw );
      assert( std::abs( original.value( QStringLiteral( "ridgeRatio" ) ) - 0.3 ) < 1e-9 );
      assert( original.value( QStringLiteral( "poseRotateZ" ) ) == yaw );
      assert( paramsInMetadataFrame( primitive, roof, std::numeric_limits<double>::quiet_NaN() ) == roof );
    }
  }

  indented[QStringLiteral( "poseRotateZ" )] = -60.0;
  const auto halfTurn = paramsInMetadataFrame( QStringLiteral( "RecessedRoofHouse" ), indented, 120.0 );
  assert( halfTurn.value( QStringLiteral( "leftMarginRatio" ) ) == 0.2 );
  assert( halfTurn.value( QStringLiteral( "rightMarginRatio" ) ) == 0.1 );
  assert( halfTurn.value( QStringLiteral( "frontMarginRatio" ) ) == 0.4 );
  assert( halfTurn.value( QStringLiteral( "backMarginRatio" ) ) == 0.3 );
  for ( const double angle : { -90.0, 90.0 } )
  {
    indented[QStringLiteral( "poseRotateZ" )] = angle;
    const auto quarterTurn = paramsInMetadataFrame( QStringLiteral( "RecessedRoofHouse" ), indented, 0.0 );
    assert( quarterTurn.value( QStringLiteral( "outerLength" ) ) == 10.0 );
    assert( quarterTurn.value( QStringLiteral( "outerWidth" ) ) == 14.0 );
    assert( quarterTurn.value( QStringLiteral( "leftMarginRatio" ) ) == ( angle > 0 ? 0.4 : 0.3 ) );
    assert( quarterTurn.value( QStringLiteral( "frontMarginRatio" ) ) == ( angle > 0 ? 0.1 : 0.2 ) );
  }
  assert( paramsInMetadataFrame( QStringLiteral( "FlatRoofHouse" ), legacy, 120.0 ) == legacy );
}
