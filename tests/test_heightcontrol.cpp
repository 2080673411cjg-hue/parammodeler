#include "../parammodeler_heightcontrol.h"
#include <QApplication>
#include <QMatrix4x4>
#include <QVBoxLayout>
#include <iostream>
#include <cstdlib>

static void check( bool ok, const char *name )
{
  if ( !ok ) { std::cerr << "FAIL: " << name << '\n'; std::exit( 1 ); }
}

int main( int argc, char **argv )
{
  QApplication app( argc, argv );
  QWidget panel;
  auto *layout = new QVBoxLayout( &panel );
  auto *row = new QHBoxLayout;
  layout->addLayout( row );
  auto *spin = new QDoubleSpinBox( &panel );
  auto *slider = new QSlider( Qt::Horizontal, &panel );
  spin->setRange( 0, 50 ); spin->setSingleStep( 0.01 ); spin->setValue( 10 );
  slider->setRange( 0, 5000 ); slider->setValue( 1000 );
  row->addWidget( slider ); row->addWidget( spin );
  QObject::connect( slider, &QSlider::valueChanged, spin, [spin]( int v ) { spin->setValue( v / 100.0 ); } );
  QObject::connect( spin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), slider, [slider]( double v ) {
    const QSignalBlocker guard( slider ); slider->setValue( qRound( v * 100 ) );
  } );
  bool suspended = false, allowed = true;
  double z = 2;
  auto *control = new ParamModelerHeightControl( spin, slider, row, 0, 1, false,
    QStringLiteral( "base" ), QStringLiteral( "top" ), [&] { return !suspended; },
    [&]( double dz ) { if ( !allowed ) return false; z += dz; return true; } );
  spin->setValue( 12 );
  check( z == 2 && control->button()->arrowType() == Qt::UpArrow, "default base fixed" );
  control->button()->click();
  check( z == 2 && spin->value() == 12 && control->button()->arrowType() == Qt::DownArrow, "toggle without movement" );
  spin->setValue( 15 );
  check( z == -1 && z + spin->value() == 14, "growth downward" );
  spin->setValue( 9 );
  check( z == 5 && z + spin->value() == 14, "shrink upward" );
  slider->setValue( 1200 );
  check( z == 2 && z + spin->value() == 14, "slider top fixed" );
  spin->stepUp();
  check( std::abs( z + spin->value() - 14 ) < 1e-9, "fine step top fixed" );
  suspended = true;
  spin->setValue( 20 );
  const double before = z;
  suspended = false;
  spin->setValue( 21 );
  check( std::abs( z - before + 1 ) < 1e-9, "bulk update bypass and baseline refresh" );
  allowed = false;
  const double beforeLimit = z;
  spin->setValue( 22 );
  check( z == beforeLimit && spin->value() == 21 && slider->value() == 2100, "translation limit rollback" );

  auto *depthRow = new QHBoxLayout;
  layout->addLayout( depthRow );
  auto *depth = new QDoubleSpinBox( &panel );
  auto *depthSlider = new QSlider( Qt::Horizontal, &panel );
  depth->setRange( 0, 50 ); depth->setValue( 3 ); depthSlider->setRange( 0, 5000 );
  depthRow->addWidget( depth ); depthRow->addWidget( depthSlider );
  double recessZ = 0;
  auto *recess = new ParamModelerHeightControl( depth, depthSlider, depthRow, -1, 0, true,
    QStringLiteral( "floor" ), QStringLiteral( "rim" ), [&] { return !suspended; },
    [&]( double dz ) { recessZ += dz; return true; } );
  depth->setValue( 5 );
  check( recessZ == 0 && recess->button()->arrowType() == Qt::DownArrow, "recess default rim fixed" );
  recess->button()->click();
  depth->setValue( 6 );
  check( recessZ == 1 && recessZ + 10 - depth->value() == 5, "recess floor fixed" );

  QDoubleSpinBox outerHeight;
  outerHeight.setRange( 0, 50 );
  outerHeight.setValue( 10 );
  QObject::connect( depthSlider, &QSlider::valueChanged, depth, [depth]( int v ) { depth->setValue( v / 100.0 ); } );
  QObject::connect( depth, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), depthSlider, [depthSlider]( double v ) {
    const QSignalBlocker guard( depthSlider ); depthSlider->setValue( qRound( v * 100 ) );
  } );
  bindRecessDepthLimit( &outerHeight, depth, depthSlider, suspended );
  depth->setValue( 15 );
  check( depth->value() == 9.99 && depthSlider->maximum() == 999, "depth cannot penetrate base" );
  const double beforeResize = recessZ;
  outerHeight.setValue( 3 );
  check( depth->value() == 2.99 && depthSlider->value() == 299, "outer height reduction clamps depth and slider" );
  check( recessZ == beforeResize, "dependent depth clamp does not translate model" );
  outerHeight.setValue( 0 );
  check( depth->value() == 0 && !depth->isEnabled() && !depthSlider->isEnabled(), "zero outer height disables recess" );
  suspended = true;
  outerHeight.setValue( 12 );
  depth->setValue( 7.5 );
  suspended = false;
  check( depth->value() == 7.5 && depth->isEnabled(), "valid prediction survives bulk update" );
  depth->setValue( 8 );
  check( std::abs( recessZ - beforeResize - 0.5 ) < 1e-9, "depth baseline refreshed after limits and bulk update" );

  QMatrix4x4 rotation;
  QDoubleSpinBox radius, eaveRadius, baseHeight, eaveHeight;
  QSlider radiusSlider, heightSlider;
  radius.setRange( 0, 50 );
  baseHeight.setRange( 0, 50 );
  eaveRadius.setDecimals( 3 );
  eaveHeight.setDecimals( 3 );
  bindEaveDimension( &radius, &eaveRadius, &radiusSlider, true, 60 );
  bindEaveDimension( &baseHeight, &eaveHeight, &heightSlider, false, 50 );
  check( !eaveRadius.isEnabled() && !eaveHeight.isEnabled(), "eave sizes disabled without base" );
  radius.setValue( 8 );
  baseHeight.setValue( 2 );
  check( eaveRadius.value() == 8.96 && eaveHeight.value() == 0.4, "eave defaults are physical dimensions" );
  eaveRadius.setValue( 20 );
  check( eaveRadius.value() == 20 && radiusSlider.maximum() == 60000, "manual eave radius is independent of sampling limits" );
  eaveRadius.setValue( 7 );
  check( eaveRadius.value() == 8.001, "eave radius must remain outside the base" );
  eaveHeight.setValue( 0.05 );
  check( eaveHeight.value() == 0.05, "thin manual eave remains valid" );
  eaveHeight.setValue( 0.4 );
  baseHeight.setValue( 3 );
  check( eaveHeight.value() == 0.4, "base edit retains a valid absolute eave height" );
  baseHeight.setValue( 20 );
  check( eaveHeight.value() == 0.4 && eaveHeight.maximum() == 19.999, "tall base retains thin eave without height restriction" );
  eaveHeight.setValue( 25 );
  check( eaveHeight.value() == 19.999, "eave must leave a positive cylinder height" );
  radius.setValue( 0 );
  radius.setValue( 0.01 );
  check( eaveRadius.minimum() == 0.011 && eaveRadius.maximum() == 60, "tiny base uses representable valid dimensions" );
  radius.setValue( 0 );
  check( !eaveRadius.isEnabled() && eaveRadius.value() == 0, "clearing base clears its eave dimension" );
  rotation.rotate( 30, 1, 0, 0 ); rotation.rotate( 20, 0, 1, 0 ); rotation.rotate( 171, 0, 0, 1 );
  const QVector3D oldTranslation( 2, 3, 4 );
  const QVector3D top = oldTranslation + rotation.map( QVector3D( 0, 0, 10 ) );
  const QVector3D nextTranslation = oldTranslation + rotation.mapVector( QVector3D( 0, 0, -2 ) );
  check( ( nextTranslation + rotation.map( QVector3D( 0, 0, 12 ) ) - top ).length() < 1e-5,
         "tilted model top compensation" );
  std::cout << "PASS: 27 height-direction, depth-limit, eave-size and geometry checks\n";
}
