#ifndef PARAMMODELER_HEIGHTCONTROL_H
#define PARAMMODELER_HEIGHTCONTROL_H

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QToolTip>
#include <functional>
#include <utility>
#include <cmath>

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
        ? tr( "Keep %1 fixed; increasing height moves the lower end down." ).arg( upperName )
        : tr( "Keep %1 fixed; increasing height moves the upper end up." ).arg( lowerName );
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
                           tr( "Height unchanged: translation limit reached." ) );
      } );
  }

  QToolButton *button() const { return mButton; }

private:
  double mPrevious;
  QToolButton *mButton = nullptr;
};

#endif
