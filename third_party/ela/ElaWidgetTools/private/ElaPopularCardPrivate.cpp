#include "ElaPopularCardPrivate.h"

#include <QTimer>

#include "ElaPopularCard.h"
#include "ElaPopularCardFloater.h"

ElaPopularCardPrivate::ElaPopularCardPrivate(QObject* parent)
    : QObject{parent}
{
}

ElaPopularCardPrivate::~ElaPopularCardPrivate()
{
}

void ElaPopularCardPrivate::_showFloater()
{
    if (!_floater || !_pCardFloatArea) { _floatTimer->stop(); return; }
    Q_Q(ElaPopularCard);
    q->update();
    _isFloating = true;
    _pHoverYOffset = 0;
    _floatTimer->stop();
    _floater->showFloater();
}
