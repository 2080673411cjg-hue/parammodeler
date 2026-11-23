#include "model_selection_generated.h"
#include <QApplication>
#include <QDialog>
#include <QFont>
#include <QLineEdit>
#include <QPushButton>
#include <QgsSettings.h>
#include <cstdlib>
#include <iostream>

static int checks = 0;
static void check( bool ok, const char *message )
{
  ++checks;
  if ( !ok ) { std::cerr << message << '\n'; std::exit( 1 ); }
}

int main( int argc, char **argv )
{
  QApplication app( argc, argv );
  app.setFont( QFont( QStringLiteral( "Microsoft YaHei" ), 9 ) );
  QTemporaryDir isolated;
  check( isolated.isValid(), "isolated settings directory" );
  QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, isolated.path() );
  QgsSettings settings;
  const QString towerKey = QStringLiteral( "parammodeler/ptv3TowerRegressionSuffix" );
  const QString commonKey = QStringLiteral( "parammodeler/ptv3RegressionSuffix" );
  const QString clsKey = QStringLiteral( "parammodeler/ptv3ClassifyLogDir" );
  const QString v14MigrationKey = QStringLiteral( "parammodeler/ptv3ClassifyDefaultsV14" );
  const QString towerMigrationKey = QStringLiteral( "parammodeler/ptv3TowerDefaultsV14ClearHeight" );
  const QString v14Dir = ParamModelerConfig::pointnetBaseDir() + QStringLiteral( "ptv3_simple/logs/ptv3_cls_v14_actual_points" );
  const QString v13Dir = ParamModelerConfig::pointnetBaseDir() + QStringLiteral( "ptv3_simple/logs/ptv3_cls_v13" );
  const QString baseline = QStringLiteral( "_v13_normals" );
  const QString towerDefault = QStringLiteral( "_v14_clear_height_normals" );
  check( ParamModelerConfig::ptv3RegressionSuffix() == baseline, "common default is v13" );
  check( ParamModelerConfig::ptv3TowerRegressionSuffix() == towerDefault, "tower defaults to direct cylinder-height regression" );

  for ( int i = 0; i < 13; ++i )
  {
    const auto &entry = ParamModelerPrimitiveNames::entries()[i];
    const QString name = QString::fromLatin1( entry.name );
    const QString suffix = name == QStringLiteral( "CurvedRoofRoundHouse" ) ? towerDefault : baseline;
    for ( const QString &alias : { name, QString::fromLatin1( entry.legacy ) } )
    {
      const auto config = regressionConfig( PointNetBackend::PTv3, alias );
      const QString expected = ParamModelerConfig::regressionLogBase( PointNetBackend::PTv3 ) +
                               QStringLiteral( "ptv3_reg_" ) + QString::fromLatin1( entry.slug ) + suffix;
      check( config.logDir == expected, "per-class local checkpoint path" );
      check( config.className == name, "canonical regression class" );
      check( modelPayload( config ).value( QStringLiteral( "model_suffix" ) ).toString() == suffix,
             "remote suffix matches the selected local checkpoint" );
    }
  }
  check( ParamModelerConfig::ptv3ClassifyLogDir() == v14Dir, "classification is v14" );
  settings.setValue( QStringLiteral( "parammodeler/ptv3ModelDefaultsV13" ), false );
  settings.setValue( v14MigrationKey, false );
  settings.setValue( towerMigrationKey, false );
  settings.setValue( clsKey, QStringLiteral( "E:/pointnet/ptv3_simple/logs/ptv3_cls_v10" ) );
  settings.setValue( commonKey, QStringLiteral( "_v10_normals" ) );
  settings.setValue( towerKey, QStringLiteral( "_v12_tower_baseline_normals" ) );
  check( ParamModelerConfig::ptv3TowerRegressionSuffix() == towerDefault, "old tower setting migrates on direct access" );
  check( ParamModelerConfig::ptv3RegressionSuffix() == baseline, "old common setting migrates to v13" );
  check( ParamModelerConfig::ptv3ClassifyLogDir() == v14Dir, "old classifier migrates to v14" );
  settings.setValue( v14MigrationKey, false );
  const QString previousDir = v13Dir + QLatin1Char( '/' );
  settings.setValue( clsKey, previousDir );
  settings.setValue( towerKey, QStringLiteral( "_v12_tower_baseline_normals" ) );
  check( ParamModelerConfig::ptv3ClassifyLogDir() == v14Dir, "saved v13 default migrates to v14" );
  check( ParamModelerConfig::ptv3TowerRegressionSuffix() == QStringLiteral( "_v12_tower_baseline_normals" ),
         "classifier migration preserves a tower override" );
  settings.setValue( clsKey, v13Dir );
  check( ParamModelerConfig::ptv3ClassifyLogDir() == v13Dir, "classification may be switched back after migration" );
  settings.setValue( v14MigrationKey, false );
  const QString customClsDir = QStringLiteral( "E:/pointnet/custom_classifier" );
  settings.setValue( clsKey, customClsDir );
  check( ParamModelerConfig::ptv3ClassifyLogDir() == customClsDir, "classifier migration preserves custom paths" );
  settings.setValue( towerMigrationKey, false );
  settings.setValue( towerKey, QStringLiteral( "_v12_tower_baseline_normals" ) );
  check( ParamModelerConfig::ptv3TowerRegressionSuffix() == QStringLiteral( "_v12_tower_baseline_normals" ),
         "tower migration preserves a custom checkpoint" );
  settings.setValue( towerMigrationKey, false );
  settings.setValue( towerKey, baseline );
  check( ParamModelerConfig::ptv3TowerRegressionSuffix() == towerDefault, "saved tower v13 default migrates to the new model" );
  settings.setValue( towerKey, baseline );
  settings.setValue( commonKey, QStringLiteral( "_v9_normals" ) );
  check( ParamModelerConfig::ptv3RegressionSuffix( QStringLiteral( "CurvedRoofRoundHouse" ) ) == baseline,
         "explicit tower selection is independent of the common suffix" );
  settings.setValue( towerKey, QString() );
  const auto inherited = regressionConfig( PointNetBackend::PTv3, QStringLiteral( "FourStageRoundTower" ) );
  check( inherited.logDir.endsWith( QStringLiteral( "/ptv3_reg_fourstage_v9_normals" ) ), "blank tower selection follows common suffix and legacy slug" );
  check( modelPayload( inherited ).value( QStringLiteral( "model_suffix" ) ).toString() == QStringLiteral( "_v9_normals" ), "blank setting uses common suffix remotely" );
  settings.setValue( towerKey, QStringLiteral( "_v10_normals" ) );
  check( regressionConfig( PointNetBackend::PTv3, QStringLiteral( "CurvedRoofRoundHouse" ) ).logDir.endsWith(
           QStringLiteral( "/ptv3_reg_curvedround_v10_normals" ) ), "explicitly restoring tower v10 works" );
  settings.setValue( towerKey, baseline );
  const auto selected = regressionConfig( PointNetBackend::PTv3, QStringLiteral( "CurvedRoofRoundHouse" ) );
  settings.setValue( towerKey, QStringLiteral( "_manual_trial_normals" ) );
  check( modelPayload( selected ).value( QStringLiteral( "model_suffix" ) ).toString() == baseline,
         "an in-flight request retains its selected checkpoint" );
  check( regressionConfig( PointNetBackend::PTv3, QStringLiteral( "CurvedRoofRoundHouse" ) ).logDir.endsWith(
           QStringLiteral( "/ptv3_reg_curvedround_manual_trial_normals" ) ), "custom setting applies to the next selection without fallback" );

  settings.setValue( towerKey, baseline );
  settings.setValue( commonKey, QStringLiteral( "_v10_normals" ) );
  QTimer::singleShot( 0, [&] {
    auto *dialog = qobject_cast<QDialog *>( QApplication::activeModalWidget() );
    check( dialog != nullptr, "actual settings dialog opened" );
    auto *edit = dialog->findChild<QLineEdit *>( QStringLiteral( "ptv3TowerSuffixEdit" ) );
    check( edit && edit->text() == baseline && edit->isVisible(), "tower override is visible in the settings dialog" );
    if ( argc > 1 )
      check( dialog->grab().save( QDir( QString::fromLocal8Bit( argv[1] ) ).filePath( QStringLiteral( "model_settings.png" ) ) ), "settings preview saved" );
    edit->clear();
    for ( auto *button : dialog->findChildren<QPushButton *>() )
      if ( button->text() == QStringLiteral( "确定" ) ) { button->click(); return; }
    check( false, "settings save button found" );
  } );
  ParamModelerConfig::showSettingsDialog( nullptr );
  check( ParamModelerConfig::ptv3TowerRegressionSuffix().isEmpty(), "saving an empty override stays empty" );
  check( ParamModelerConfig::ptv3RegressionSuffix( QStringLiteral( "CurvedRoofRoundHouse" ) ) == QStringLiteral( "_v10_normals" ), "saved empty override really restores v10" );

  QTimer::singleShot( 0, [&] {
    auto *dialog = qobject_cast<QDialog *>( QApplication::activeModalWidget() );
    check( dialog != nullptr, "settings reopen" );
    auto *edit = dialog->findChild<QLineEdit *>( QStringLiteral( "ptv3TowerSuffixEdit" ) );
    for ( auto *button : dialog->findChildren<QPushButton *>() )
      if ( button->text() == QStringLiteral( "恢复默认值" ) ) button->click();
    check( edit && edit->text() == towerDefault, "reset restores the new tower default" );
    dialog->reject();
  } );
  ParamModelerConfig::showSettingsDialog( nullptr );
  check( ParamModelerConfig::ptv3TowerRegressionSuffix().isEmpty(), "cancel does not save a reset" );

  settings.setValue( towerKey, towerDefault );
  const auto live = regressionConfig( PointNetBackend::PTv3, QStringLiteral( "CurvedRoofRoundHouse" ) );
  if ( argc > 1 )
  {
    QFile file( QDir( QString::fromLocal8Bit( argv[1] ) ).filePath( QStringLiteral( "model_payload.json" ) ) );
    check( file.open( QIODevice::WriteOnly ), "route payload file" );
    file.write( QJsonDocument( modelPayload( live ) ).toJson() );
  }
  std::cout << checks << " model routing/settings checks passed\n";
}
