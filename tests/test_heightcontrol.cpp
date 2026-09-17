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
    QStringLiteral( "floor" ), QStringLiteral( "rim" ), [] { return true; },
    [&]( double dz ) { recessZ += dz; return true; } );
  depth->setValue( 5 );
  check( recessZ == 0 && recess->button()->arrowType() == Qt::DownArrow, "recess default rim fixed" );
  recess->button()->click();
  depth->setValue( 6 );
  check( recessZ == 1 && recessZ + 10 - depth->value() == 5, "recess floor fixed" );

  QMatrix4x4 rotation;
  rotation.rotate( 30, 1, 0, 0 ); rotation.rotate( 20, 0, 1, 0 ); rotation.rotate( 171, 0, 0, 1 );
  const QVector3D oldTranslation( 2, 3, 4 );
  const QVector3D top = oldTranslation + rotation.map( QVector3D( 0, 0, 10 ) );
  const QVector3D nextTranslation = oldTranslation + rotation.mapVector( QVector3D( 0, 0, -2 ) );
  check( ( nextTranslation + rotation.map( QVector3D( 0, 0, 12 ) ) - top ).length() < 1e-5,
         "tilted model top compensation" );
  std::cout << "PASS: 11 height-direction interaction and geometry checks\n";
}
