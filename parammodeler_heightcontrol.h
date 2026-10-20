#ifndef PARAMMODELER_HEIGHTCONTROL_H
#define PARAMMODELER_HEIGHTCONTROL_H

#include <QDoubleSpinBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QSlider>
#include <QToolButton>
#include <QToolTip>
#include <functional>
#include <utility>
#include <cmath>
#include <algorithm>

inline void bindRecessFootprint( QDoubleSpinBox *inner, QSlider *innerSlider,
                                 QDoubleSpinBox *outer, QSlider *outerSlider,
                                 QDoubleSpinBox *offset, const std::function<bool()> &enabled )
{
  inner->setKeyboardTracking( false );
  outer->setKeyboardTracking( false );
  QObject::connect( inner, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), inner,
    [inner, innerSlider, outer, offset, enabled, previous = inner->value()]( double value ) mutable {
      const double oldSize = previous;
      previous = value;
      if ( !enabled() ) return;
      const double fixedOffset = offset->value() * std::max( 0.0, outer->value() - oldSize );
      const double maxSize = std::max( 0.0, outer->value() - fixedOffset );
      if ( value > maxSize )
      {
        const double units = std::pow( 10.0, inner->decimals() );
        const QSignalBlocker spinBlocker( inner ), sliderBlocker( innerSlider );
        inner->setValue( std::floor( maxSize * units + 1e-8 ) / units );
        innerSlider->setValue( qRound( inner->value() * 100.0 ) );
        previous = inner->value();
      }
      const double movable = std::max( 0.0, outer->value() - inner->value() );
      offset->setValue( movable > 1e-6 ? qBound( 0.0, fixedOffset / movable, 1.0 ) : 0.0 );
    } );
  QObject::connect( outer, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), outer,
    [inner, outer, outerSlider, offset, enabled, previous = outer->value()]( double value ) mutable {
      const double oldSize = previous;
      previous = value;
      if ( !enabled() ) return;
      // Keep the physical inset, not its fraction of the available space.
      const double fixedOffset = offset->value() * std::max( 0.0, oldSize - inner->value() );
      const double minSize = fixedOffset + inner->value();
      if ( value < minSize )
      {
        const double units = std::pow( 10.0, outer->decimals() );
        const QSignalBlocker spinBlocker( outer ), sliderBlocker( outerSlider );
        outer->setValue( std::ceil( minSize * units - 1e-8 ) / units );
        outerSlider->setValue( qRound( outer->value() * 100.0 ) );
        previous = outer->value();
      }
      const double movable = std::max( 0.0, outer->value() - inner->value() );
      offset->setValue( movable > 1e-6 ? qBound( 0.0, fixedOffset / movable, 1.0 ) : 0.0 );
    } );
}

inline void bindFixedDimensionSlider( QDoubleSpinBox *spin, QSlider *slider, double maximum )
{
  const double units = std::pow( 10.0, spin->decimals() );
  {
    const QSignalBlocker blocker( slider );
    slider->setRange( 0, qRound( maximum * units ) );
    slider->setValue( qRound( spin->value() * units ) );
  }
  QObject::connect( slider, &QSlider::valueChanged, slider, [spin, slider, units]( int ) {
    // An out-of-range drag may leave the spin unchanged, emitting no valueChanged.
    const QSignalBlocker blocker( slider );
    slider->setValue( qRound( spin->value() * units ) );
  } );
}

inline void limitDimensionSourceEdit( QDoubleSpinBox *spin, QSlider *slider, double minimum, double maximum )
{
  const double units = std::pow( 10.0, spin->decimals() );
  minimum = std::ceil( minimum * units - 1e-8 ) / units;
  maximum = std::floor( maximum * units + 1e-8 ) / units;
  const double value = qBound( minimum, spin->value(), maximum );
  const QSignalBlocker spinBlocker( spin ), sliderBlocker( slider );
  spin->setValue( value );
  slider->setValue( qRound( spin->value() * units ) );
}

inline void bindRecessDepthLimit( QDoubleSpinBox *outerHeight, QDoubleSpinBox *depth,
                                  QSlider *slider, QSlider *heightSlider, bool &suspended,
                                  const std::function<bool()> &manualEdit )
{
  outerHeight->setKeyboardTracking( false );
  depth->setKeyboardTracking( false );
  bindFixedDimensionSlider( depth, slider, outerHeight->maximum() );
  const auto sync = [outerHeight, depth, slider, &suspended]() {
    const QScopedValueRollback<bool> guard( suspended, true );
    const QSignalBlocker sliderBlocker( slider );
    const double units = std::pow( 10.0, depth->decimals() );
    const double maximum = std::max( 0.0, std::floor( outerHeight->value() * units + 1e-8 ) - 1.0 ) / units;
    depth->setMaximum( maximum );
    slider->setValue( qRound( depth->value() * units ) );
    depth->setEnabled( maximum > 0.0 );
    slider->setEnabled( maximum > 0.0 );
  };
  QObject::connect( outerHeight, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), depth,
    [outerHeight, depth, heightSlider, manualEdit, sync]( double ) {
      if ( manualEdit() && depth->value() > 0.0 )
        limitDimensionSourceEdit( outerHeight, heightSlider,
          depth->value() + 1.0 / std::pow( 10.0, depth->decimals() ), outerHeight->maximum() );
      sync();
    } );
  sync();
}

inline void bindTowerTopRadius( QDoubleSpinBox *eaveRadius, QDoubleSpinBox *radius, QSlider *slider,
                                QSlider *eaveSlider, const std::function<bool()> &manualEdit )
{
  radius->setKeyboardTracking( false );
  eaveRadius->setKeyboardTracking( false );
  const double eaveMaximum = eaveSlider->maximum() / std::pow( 10.0, eaveRadius->decimals() );
  bindFixedDimensionSlider( radius, slider, eaveMaximum - 1.0 / std::pow( 10.0, radius->decimals() ) );
  const auto sync = [eaveRadius, radius, slider]() {
    const QSignalBlocker sliderBlocker( slider );
    const double units = std::pow( 10.0, radius->decimals() );
    const double maximum = std::max( 0.0, std::ceil( eaveRadius->value() * units - 1e-8 ) - 1.0 ) / units;
    const bool enabled = maximum >= 1.0 / units;
    const bool needsDefault = enabled && radius->maximum() == 0.0;
    radius->setRange( enabled ? 1.0 / units : 0.0, maximum );
    radius->setEnabled( enabled );
    slider->setEnabled( enabled );
    if ( needsDefault ) radius->setValue( std::min( 1.0, maximum ) );
    slider->setValue( qRound( radius->value() * units ) );
  };
  QObject::connect( eaveRadius, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), radius,
    [eaveRadius, eaveSlider, radius, manualEdit, sync]( double ) {
      if ( manualEdit() && radius->value() > 0.0 )
        limitDimensionSourceEdit( eaveRadius, eaveSlider,
          std::max( eaveRadius->minimum(), radius->value() + 1.0 / std::pow( 10.0, eaveRadius->decimals() ) ), eaveRadius->maximum() );
      sync();
    } );
  sync();
}

inline void bindTowerRise( QDoubleSpinBox *height, QDoubleSpinBox *rise, QSlider *slider,
                          QSlider *heightSlider, const std::function<bool()> &manualEdit )
{
  rise->setKeyboardTracking( false );
  height->setKeyboardTracking( false );
  bindFixedDimensionSlider( rise, slider, height->maximum() * 0.25 );
  const auto sync = [height, rise, slider, manualEdit]() {
    const QSignalBlocker sliderBlocker( slider );
    const double units = std::pow( 10.0, rise->decimals() );
    const double maximum = std::floor( height->value() * 0.25 * units + 1e-8 ) / units;
    const bool enabled = maximum > 0.0;
    const bool needsDefault = enabled && rise->maximum() == 0.0;
    rise->setRange( 0.0, maximum );
    rise->setEnabled( enabled );
    slider->setEnabled( enabled );
    if ( needsDefault && !manualEdit() ) rise->setValue( height->value() * 0.10 );
    slider->setValue( qRound( rise->value() * units ) );
  };
  QObject::connect( height, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), rise,
    [height, heightSlider, rise, manualEdit, sync]( double ) {
      if ( manualEdit() && rise->value() > 0.0 )
        limitDimensionSourceEdit( height, heightSlider, std::max( height->minimum(), rise->value() * 4.0 ), height->maximum() );
      sync();
    } );
  sync();
}

inline void bindEaveDimension( QDoubleSpinBox *base, QDoubleSpinBox *size, QSlider *slider,
                               bool outerRadius, double absoluteMax, QSlider *baseSlider,
                               const std::function<bool()> &manualEdit )
{
  size->setKeyboardTracking( false );
  base->setKeyboardTracking( false );
  bindFixedDimensionSlider( size, slider, absoluteMax );
  const auto sync = [base, size, slider, outerRadius, absoluteMax]() {
    const QSignalBlocker sliderBlocker( slider );
    const double units = std::pow( 10.0, size->decimals() );
    const double step = 1.0 / units;
    const double minimum = outerRadius ? std::ceil( ( base->value() + step ) * units - 1e-8 ) / units : step;
    const double maximum = std::floor( ( outerRadius ? absoluteMax : std::min( base->value() - step, absoluteMax ) ) * units + 1e-8 ) / units;
    const bool enabled = base->value() > 0.0 && minimum <= maximum;
    const bool needsDefault = size->value() <= 0.0;
    size->setEnabled( enabled );
    slider->setEnabled( enabled );
    size->setRange( enabled ? minimum : 0.0, enabled ? maximum : 0.0 );
    if ( enabled && needsDefault )
      size->setValue( outerRadius ? base->value() * 1.12 : std::min( base->value() * 0.20, 0.9 ) );
    slider->setValue( qRound( size->value() * units ) );
  };
  QObject::connect( base, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), size,
    [base, baseSlider, size, outerRadius, manualEdit, sync]( double ) {
      if ( manualEdit() && size->value() > 0.0 )
      {
        const double step = 1.0 / std::pow( 10.0, size->decimals() );
        const double minimum = outerRadius ? std::max( base->minimum(), 1.0 / std::pow( 10.0, base->decimals() ) )
                                          : std::max( base->minimum(), size->value() + step );
        const double maximum = outerRadius ? std::min( base->maximum(), size->value() - step ) : base->maximum();
        limitDimensionSourceEdit( base, baseSlider, minimum, maximum );
      }
      sync();
    } );
  sync();
}

// Endpoint derivatives describe ordinary heights (0,1) and recess depths (-1,0).
class ParamModelerHeightControl : public QObject
{
public:
  ParamModelerHeightControl( QDoubleSpinBox *spin, QSlider *slider, QHBoxLayout *row,
                            double lowerDerivative, double upperDerivative, bool keepUpper,
                            const QString &lowerName, const QString &upperName,
                            std::function<bool()> enabled, std::function<bool( double )> translate )
    : QObject( spin ), mPrevious( spin->value() )
  {
    spin->setKeyboardTracking( false );
    mButton = new QToolButton( spin->parentWidget() );
    mButton->setObjectName( spin->objectName() + QStringLiteral( "Direction" ) );
    mButton->setCheckable( true );
    mButton->setChecked( keepUpper );
    mButton->setFixedSize( 24, 24 );
    mButton->setAutoRaise( true );
    row->addWidget( mButton );
    auto showMode = [this, lowerName, upperName]( bool upper ) {
      mButton->setArrowType( upper ? Qt::DownArrow : Qt::UpArrow );
      const QString text = upper
        ? QCoreApplication::translate( "ParamModelerHeightControl", "Keep %1 fixed; increasing height moves the lower end down." ).arg( upperName )
        : QCoreApplication::translate( "ParamModelerHeightControl", "Keep %1 fixed; increasing height moves the upper end up." ).arg( lowerName );
      mButton->setToolTip( text );
      mButton->setAccessibleName( text );
    };
    connect( mButton, &QToolButton::toggled, this, showMode );
    showMode( keepUpper );
    connect( spin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this,
      [this, spin, slider, lowerDerivative, upperDerivative, enabled, translate]( double ) {
        const double value = spin->value();
        const double previous = mPrevious;
        mPrevious = value;
        if ( !enabled() || value == previous ) return;
        const double derivative = mButton->isChecked() ? upperDerivative : lowerDerivative;
        const double offset = ( previous - value ) * derivative;
        if ( offset == 0 || translate( offset ) ) return;
        // Reject the height change too when its matching translation cannot fit.
        const QSignalBlocker spinBlocker( spin ), sliderBlocker( slider );
        spin->setValue( previous );
        slider->setValue( qRound( previous * 100.0 ) );
        mPrevious = previous;
        QToolTip::showText( mButton->mapToGlobal( QPoint( 0, mButton->height() ) ),
                           QCoreApplication::translate( "ParamModelerHeightControl", "Height unchanged: translation limit reached." ) );
      } );
  }

  QToolButton *button() const { return mButton; }

private:
  double mPrevious;
  QToolButton *mButton = nullptr;
};

#endif
