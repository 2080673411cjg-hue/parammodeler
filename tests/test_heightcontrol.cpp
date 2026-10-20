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
  QSlider outerHeightSlider;
  outerHeight.setRange( 0, 50 );
  outerHeight.setValue( 10 );
  outerHeightSlider.setRange( 0, 5000 );
  outerHeightSlider.setValue( 1000 );
  QObject::connect( &outerHeightSlider, &QSlider::valueChanged, &outerHeight,
                    [&]( int v ) { outerHeight.setValue( v / 100.0 ); } );
  QObject::connect( &outerHeight, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), &outerHeightSlider, [&]( double v ) {
    const QSignalBlocker guard( &outerHeightSlider ); outerHeightSlider.setValue( qRound( v * 100 ) );
  } );
  QObject::connect( depthSlider, &QSlider::valueChanged, depth, [depth]( int v ) { depth->setValue( v / 100.0 ); } );
  QObject::connect( depth, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), depthSlider, [depthSlider]( double v ) {
    const QSignalBlocker guard( depthSlider ); depthSlider->setValue( qRound( v * 100 ) );
  } );
  bindRecessDepthLimit( &outerHeight, depth, depthSlider, &outerHeightSlider, suspended, [&] { return !suspended; } );
  depth->setValue( 15 );
  check( depth->value() == 9.99 && depthSlider->maximum() == 5000, "depth cannot penetrate base with fixed slider scale" );
  depthSlider->setValue( 3000 );
  check( depth->value() == 9.99 && depthSlider->value() == 999, "rejected depth drag restores slider even without spin change" );
  depthSlider->setValue( 4000 );
  check( depthSlider->value() == 999, "repeated rejected depth drag remains synchronized" );
  depth->setValue( 6 );
  const double beforeManualResize = recessZ;
  outerHeight.setValue( 3 );
  check( outerHeight.value() == 6.01 && outerHeightSlider.value() == 601 && depth->value() == 6,
         "manual outer shrink stops above current recess depth" );
  check( depthSlider->value() == 600 && depthSlider->maximum() == 5000 && recessZ == beforeManualResize,
         "manual outer shrink neither moves depth thumb nor translates through depth control" );
  outerHeightSlider.setValue( 200 );
  check( outerHeight.value() == 6.01 && outerHeightSlider.value() == 601 && depth->value() == 6,
         "repeated rejected outer slider drag preserves depth" );
  outerHeight.setValue( 10 );
  check( depth->value() == 6 && depthSlider->value() == 600, "outer regrowth does not leave depth modified" );
  auto *outerHeightRow = new QHBoxLayout;
  layout->addLayout( outerHeightRow );
  double outerZ = 0;
  new ParamModelerHeightControl( &outerHeight, &outerHeightSlider, outerHeightRow, 0, 1, true,
    QStringLiteral( "base" ), QStringLiteral( "top" ), [&] { return !suspended; },
    [&]( double dz ) { outerZ += dz; return true; } );
  outerHeightSlider.setValue( 200 );
  check( std::abs( outerZ - 3.99 ) < 1e-9 && outerHeight.value() == 6.01 && depth->value() == 6,
         "fixed-top translation uses clamped outer height" );
  outerHeightSlider.setValue( 100 );
  check( std::abs( outerZ - 3.99 ) < 1e-9, "repeated rejected height drag has no translation drift" );
  outerHeight.setValue( 10 );
  check( std::abs( outerZ ) < 1e-9 && depth->value() == 6, "outer regrowth restores translation without changing depth" );
  const double beforeResize = recessZ;
  suspended = true;
  outerHeight.setValue( 3 );
  check( depth->value() == 2.99 && depthSlider->value() == 299 && depthSlider->maximum() == 5000,
         "bulk outer height reduction still clamps invalid depth without rescaling slider" );
  check( recessZ == beforeResize, "dependent depth clamp does not translate model" );
  outerHeight.setValue( 0 );
  check( depth->value() == 0 && !depth->isEnabled() && !depthSlider->isEnabled(), "zero outer height disables recess" );
  outerHeight.setValue( 12 );
  depth->setValue( 7.5 );
  suspended = false;
  check( depth->value() == 7.5 && depth->isEnabled(), "valid prediction survives bulk update" );
  depth->setValue( 8 );
  check( std::abs( recessZ - beforeResize - 0.5 ) < 1e-9, "depth baseline refreshed after limits and bulk update" );

  QDoubleSpinBox outerSize, innerSize, inset;
  QSlider outerSlider, innerSlider;
  outerSize.setRange( 0, 50 ); innerSize.setRange( 0, 50 );
  inset.setRange( 0, 1 ); inset.setDecimals( 6 );
  outerSlider.setRange( 0, 5000 ); innerSlider.setRange( 0, 5000 );
  outerSize.setValue( 20 ); innerSize.setValue( 8 ); inset.setValue( .25 );
  auto bind = []( QDoubleSpinBox *size, QSlider *slider ) {
    QObject::connect( slider, &QSlider::valueChanged, size, [size]( int value ) { size->setValue( value / 100.0 ); } );
    QObject::connect( size, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), slider, [slider]( double value ) {
      const QSignalBlocker blocker( slider ); slider->setValue( qRound( value * 100.0 ) );
    } );
  };
  bind( &outerSize, &outerSlider ); bind( &innerSize, &innerSlider );
  bindRecessFootprint( &innerSize, &innerSlider, &outerSize, &outerSlider, &inset, [&] { return !suspended; } );
  auto actualInset = [&] { return ( outerSize.value() - innerSize.value() ) * inset.value(); };
  outerSize.setValue( 24 );
  check( std::abs( actualInset() - 3 ) < 1e-5 && innerSize.value() == 8, "outer growth fixes inner position and size" );
  outerSlider.setValue( 2200 );
  check( std::abs( actualInset() - 3 ) < 2e-5, "outer slider fixes inset" );
  innerSize.setValue( 10 );
  check( std::abs( actualInset() - 3 ) < 2e-5, "inner growth still fixes corner" );
  outerSize.setValue( 9 );
  check( outerSize.value() >= actualInset() + innerSize.value() - 1e-5 && outerSlider.value() == qRound( outerSize.value() * 100 ),
         "outer shrink stops at inner far edge and updates slider" );
  check( innerSize.value() == 10 && std::abs( actualInset() - 3 ) < 2e-5, "outer limit does not resize or move recess" );
  outerSize.setValue( 25 );
  check( std::abs( actualInset() - 3 ) < 3e-5, "baseline follows limited outer edit" );
  inset.setValue( .5 );
  outerSize.setValue( 30 );
  check( std::abs( actualInset() - 7.5 ) < 1e-5, "explicit offset edit becomes new fixed position" );
  innerSlider.setValue( 4000 );
  check( innerSize.value() == 22.5 && innerSlider.value() == 2250 && std::abs( actualInset() - 7.5 ) < 1e-5,
         "inner slider respects far boundary" );
  suspended = true;
  outerSize.setValue( 12 ); innerSize.setValue( 4 ); inset.setValue( .25 );
  suspended = false;
  check( outerSize.value() == 12 && innerSize.value() == 4, "prediction batch bypasses corner constraints" );
  outerSize.setValue( 16 );
  check( std::abs( actualInset() - 2 ) < 1e-5, "batch updates refresh outer baseline" );
  innerSize.setValue( 6 );
  check( std::abs( actualInset() - 2 ) < 1e-5, "batch updates refresh inner baseline" );

  QMatrix4x4 rotation;
  QDoubleSpinBox roofRadius, topRadius;
  QSlider topRadiusSlider, roofRadiusSlider;
  roofRadiusSlider.setRange( 0, 60000 );
  roofRadius.setDecimals( 3 ); roofRadius.setRange( 0, 60 );
  topRadius.setRange( 0, 50 );
  bind( &topRadius, &topRadiusSlider );
  bindTowerTopRadius( &roofRadius, &topRadius, &topRadiusSlider, &roofRadiusSlider, [] { return false; } );
  check( !topRadius.isEnabled() && !topRadiusSlider.isEnabled(), "top radius disabled without eave" );
  roofRadius.setValue( 10 );
  check( topRadius.value() == 1 && topRadius.minimum() == .01 && topRadius.maximum() == 9.99, "top radius has only geometric limits" );
  topRadius.setValue( 5 );
  check( topRadius.value() == 5, "top radius above old percentage limit is retained" );
  topRadius.setValue( .01 );
  check( topRadius.value() == .01, "top radius below old percentage limit is retained" );
  topRadius.setValue( 8 );
  roofRadius.setValue( 6 );
  check( topRadius.value() == 5.99 && topRadiusSlider.value() == 599, "smaller eave visibly clamps top radius" );
  roofRadius.setValue( 60 );
  topRadiusSlider.setValue( 5500 );
  check( topRadius.value() == 55 && topRadiusSlider.maximum() == 5999, "top radius follows eave without fixed fifty metre cap" );
  roofRadius.setValue( 9.001 );
  check( topRadius.maximum() == 9, "strict radius limit uses closest representable value" );
  roofRadius.setValue( 0 );
  check( topRadius.value() == 0 && !topRadius.isEnabled(), "clearing eave clears top radius" );
  QDoubleSpinBox middleHeight, rise;
  QSlider riseSlider, middleHeightSlider;
  middleHeight.setRange( 0, 50 );
  rise.setDecimals( 3 );
  QObject::connect( &riseSlider, &QSlider::valueChanged, &rise, [&]( int v ) { rise.setValue( v / 1000.0 ); } );
  QObject::connect( &rise, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), &riseSlider, [&]( double v ) {
    const QSignalBlocker blocker( &riseSlider ); riseSlider.setValue( qRound( v * 1000 ) );
  } );
  bindTowerRise( &middleHeight, &rise, &riseSlider, &middleHeightSlider, [] { return false; } );
  check( !rise.isEnabled() && !riseSlider.isEnabled(), "rise disabled without middle height" );
  middleHeight.setValue( 2 );
  check( rise.value() == .2 && rise.maximum() == .5 && riseSlider.value() == 200, "rise default is ten percent of height in metres" );
  rise.setValue( .4 );
  middleHeight.setValue( 3 );
  check( rise.value() == .4 && rise.maximum() == .75, "height edit retains absolute rise" );
  middleHeight.setValue( 1 );
  check( rise.value() == .25 && riseSlider.maximum() == 12500 && riseSlider.value() == 250, "bulk height reduction clamps rise without rescaling slider" );
  riseSlider.setValue( 125 );
  check( rise.value() == .125, "rise slider uses millimetres" );
  rise.setValue( 0 );
  middleHeight.setValue( 4 );
  check( rise.value() == 0, "zero rise remains zero during valid height edits" );
  middleHeight.setValue( 0 );
  middleHeight.setValue( .01 );
  check( rise.maximum() == .002 && rise.value() == .001, "rise limit rounds down for tiny heights" );
  suspended = true;
  middleHeight.setValue( 2 ); rise.setValue( .33 );
  suspended = false;
  check( rise.value() == .33 && riseSlider.value() == 330, "new prediction can set height before rise" );
  QDoubleSpinBox radius, eaveRadius, baseHeight, eaveHeight;
  QSlider radiusSlider, heightSlider, baseRadiusSlider, baseHeightSlider;
  radius.setRange( 0, 50 );
  baseHeight.setRange( 0, 50 );
  eaveRadius.setDecimals( 3 );
  eaveHeight.setDecimals( 3 );
  bindEaveDimension( &radius, &eaveRadius, &radiusSlider, true, 60, &baseRadiusSlider, [] { return false; } );
  bindEaveDimension( &baseHeight, &eaveHeight, &heightSlider, false, 50, &baseHeightSlider, [] { return false; } );
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

  QDoubleSpinBox manualBaseR, manualBaseH, manualEaveR, manualEaveH, manualTopR, manualMidH, manualRise;
  QSlider manualBaseRSlider, manualBaseHSlider, manualEaveRSlider, manualEaveHSlider,
          manualTopRSlider, manualMidHSlider, manualRiseSlider;
  manualBaseR.setRange( 0, 50 ); manualBaseH.setRange( 0, 50 ); manualMidH.setRange( 0, 50 );
  manualEaveR.setDecimals( 3 ); manualEaveR.setRange( 0, 60 );
  manualEaveH.setDecimals( 3 ); manualEaveH.setRange( 0, 50 );
  manualTopR.setRange( 0, 60 ); manualRise.setDecimals( 3 ); manualRise.setRange( 0, 12.5 );
  manualBaseR.setValue( 8 ); manualBaseH.setValue( 2 ); manualEaveR.setValue( 9.6 );
  manualEaveH.setValue( .4 ); manualTopR.setValue( 1 ); manualMidH.setValue( 2 ); manualRise.setValue( .4 );
  auto bindScaled = []( QDoubleSpinBox *spin, QSlider *slider ) {
    const double units = std::pow( 10.0, spin->decimals() );
    slider->setRange( qRound( spin->minimum() * units ), qRound( spin->maximum() * units ) );
    slider->setValue( qRound( spin->value() * units ) );
    QObject::connect( slider, &QSlider::valueChanged, spin, [spin, units]( int value ) { spin->setValue( value / units ); } );
    QObject::connect( spin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), slider, [slider, units]( double value ) {
      const QSignalBlocker blocker( slider ); slider->setValue( qRound( value * units ) );
    } );
  };
  bindScaled( &manualBaseR, &manualBaseRSlider ); bindScaled( &manualBaseH, &manualBaseHSlider );
  bindScaled( &manualEaveR, &manualEaveRSlider ); bindScaled( &manualEaveH, &manualEaveHSlider );
  bindScaled( &manualTopR, &manualTopRSlider ); bindScaled( &manualMidH, &manualMidHSlider );
  bindScaled( &manualRise, &manualRiseSlider );
  auto manualEdit = [&] { return !suspended; };
  bindEaveDimension( &manualBaseR, &manualEaveR, &manualEaveRSlider, true, 60, &manualBaseRSlider, manualEdit );
  bindEaveDimension( &manualBaseH, &manualEaveH, &manualEaveHSlider, false, 50, &manualBaseHSlider, manualEdit );
  bindTowerTopRadius( &manualEaveR, &manualTopR, &manualTopRSlider, &manualEaveRSlider, manualEdit );
  bindTowerRise( &manualMidH, &manualRise, &manualRiseSlider, &manualMidHSlider, manualEdit );
  manualBaseRSlider.setValue( 900 );
  check( manualEaveR.value() == 9.6 && manualEaveRSlider.value() == 9600
         && manualEaveRSlider.minimum() == 0 && manualEaveRSlider.maximum() == 60000,
         "base radius edit preserves eave value and slider position" );
  manualBaseRSlider.setValue( 1200 );
  check( manualBaseR.value() == 9.59 && manualBaseRSlider.value() == 959 && manualEaveR.value() == 9.6,
         "oversized base stops at eave instead of enlarging eave" );
  manualBaseRSlider.setValue( 800 );
  check( manualEaveR.value() == 9.6 && manualEaveRSlider.value() == 9600,
         "shrinking base does not leave an inflated eave" );
  manualTopR.setValue( 9 );
  manualEaveRSlider.setValue( 8500 );
  check( manualEaveR.value() == 9.001 && manualEaveRSlider.value() == 9001 && manualTopR.value() == 9,
         "shrinking eave stops at top radius without changing top radius" );
  check( manualTopRSlider.value() == 900 && manualTopRSlider.maximum() == 5999,
         "top radius slider scale stays fixed" );
  manualTopR.setValue( 1 );
  manualEaveRSlider.setValue( 0 );
  manualEaveRSlider.setValue( 0 );
  check( manualEaveR.value() == 8.001 && manualEaveRSlider.value() == 8001,
         "repeated invalid eave drags cannot desynchronize slider" );
  manualBaseHSlider.setValue( 20 );
  check( manualBaseH.value() == .41 && manualEaveH.value() == .4 && manualEaveHSlider.value() == 400
         && manualEaveHSlider.maximum() == 50000, "base height edit preserves eave thickness and scale" );
  auto *manualRow = new QHBoxLayout;
  layout->addLayout( manualRow );
  manualRow->addWidget( &manualMidH ); manualRow->addWidget( &manualMidHSlider );
  double manualZ = 0;
  new ParamModelerHeightControl( &manualMidH, &manualMidHSlider, manualRow, 0, 1, true,
    QStringLiteral( "bottom" ), QStringLiteral( "top" ), manualEdit,
    [&]( double dz ) { manualZ += dz; return true; } );
  manualMidHSlider.setValue( 100 );
  check( manualMidH.value() == 1.6 && manualRise.value() == .4 && manualRiseSlider.value() == 400
         && manualRiseSlider.maximum() == 12500, "middle height stops without shrinking rise or moving rise slider" );
  check( std::abs( manualZ - .4 ) < 1e-9, "height compensation uses constrained edit, not attempted value" );
  manualMidHSlider.setValue( 100 );
  check( std::abs( manualZ - .4 ) < 1e-9, "repeated rejected height edit does not drift translation" );
  suspended = true;
  manualBaseR.setValue( 15 ); manualEaveR.setValue( 17 ); manualTopR.setValue( 4 );
  manualBaseH.setValue( 5 ); manualEaveH.setValue( .6 ); manualMidH.setValue( 1 ); manualRise.setValue( .15 );
  suspended = false;
  check( manualBaseR.value() == 15 && manualEaveR.value() == 17 && manualTopR.value() == 4
         && manualBaseH.value() == 5 && manualEaveH.value() == .6 && manualMidH.value() == 1 && manualRise.value() == .15,
         "prediction and randomization batches can replace all tower dimensions" );
  manualRise.setValue( 0 );
  manualMidH.setValue( 0 );
  manualMidH.setValue( 2 );
  check( manualRise.value() == 0, "manual height re-entry preserves an explicitly zero rise" );
  rotation.rotate( 30, 1, 0, 0 ); rotation.rotate( 20, 0, 1, 0 ); rotation.rotate( 171, 0, 0, 1 );
  const QVector3D oldTranslation( 2, 3, 4 );
  const QVector3D top = oldTranslation + rotation.map( QVector3D( 0, 0, 10 ) );
  const QVector3D nextTranslation = oldTranslation + rotation.mapVector( QVector3D( 0, 0, -2 ) );
  check( ( nextTranslation + rotation.map( QVector3D( 0, 0, 12 ) ) - top ).length() < 1e-5,
         "tilted model top compensation" );
  std::cout << "PASS: height-direction, recess-footprint, depth-limit, eave-size and geometry checks\n";
}
