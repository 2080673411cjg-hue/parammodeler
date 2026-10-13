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

inline void bindRecessDepthLimit( QDoubleSpinBox *outerHeight, QDoubleSpinBox *depth,
                                  QSlider *slider, bool &suspended )
{
  const auto sync = [outerHeight, depth, slider, &suspended]() {
    const QScopedValueRollback<bool> guard( suspended, true );
    const QSignalBlocker sliderBlocker( slider );
    const double units = std::pow( 10.0, depth->decimals() );
    const double maximum = std::max( 0.0, std::floor( outerHeight->value() * units + 1e-8 ) - 1.0 ) / units;
    depth->setMaximum( maximum );
    slider->setMaximum( static_cast<int>( std::floor( maximum * 100.0 + 1e-8 ) ) );
    slider->setValue( qRound( depth->value() * 100.0 ) );
    depth->setEnabled( maximum > 0.0 );
    slider->setEnabled( maximum > 0.0 );
  };
  QObject::connect( outerHeight, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), depth,
                    [sync]( double ) { sync(); } );
  sync();
}

inline void bindEaveDimension( QDoubleSpinBox *base, QDoubleSpinBox *size, QSlider *slider,
                               bool outerRadius, double absoluteMax )
{
  size->setKeyboardTracking( false );
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
    slider->setRange( qRound( size->minimum() * units ), qRound( size->maximum() * units ) );
    slider->setValue( qRound( size->value() * units ) );
  };
  QObject::connect( base, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), size,
                    [sync]( double ) { sync(); } );
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
