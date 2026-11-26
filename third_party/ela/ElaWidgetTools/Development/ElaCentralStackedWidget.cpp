#include "ElaCentralStackedWidget.h"

#include "ElaTheme.h"
#include <QApplication>
#include <QDebug>
#include <QGraphicsBlurEffect>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPropertyAnimation>
#include <cmath>
#include <cstdint>
namespace {
/** @brief 保持已导出控件的布局；切换状态沿 QObject 父子所有权释放。 */
struct StackSwitchState : QObject {
    explicit StackSwitchState(QObject* parent) : QObject(parent) {}
    std::uint64_t revision = 0;
    QPointer<QWidget> target;
};
StackSwitchState* switchState(ElaCentralStackedWidget* owner)
{
    for (auto child : owner->children())
        if (auto state = dynamic_cast<StackSwitchState*>(child))
            return state;
    return new StackSwitchState(owner);
}
} // namespace

ElaCentralStackedWidget::ElaCentralStackedWidget(QWidget* parent)
    : QWidget(parent)
{
    new StackSwitchState(this);
    _pPopupAnimationYOffset = 0;
    _pScaleAnimationRatio = 1;
    _pScaleAnimationPixOpacity = 1;
    _pFlipAnimationRatio = 1;
    _pBlurAnimationRadius = 0;
    _pLastTargetIndex = 0;

    setObjectName("ElaCentralStackedWidget");
    setStyleSheet("#ElaCentralStackedWidget{background-color:transparent;}");

    _containerStackedWidget = new QStackedWidget(this);
    _containerStackedWidget->setObjectName("ElaCentralStackedWidget");
    _containerStackedWidget->setStyleSheet("#ElaCentralStackedWidget{background-color:transparent;}");

    _blurEffect = new QGraphicsBlurEffect(_containerStackedWidget);
    _blurEffect->setBlurHints(QGraphicsBlurEffect::BlurHint::QualityHint);
    _blurEffect->setBlurRadius(0);
    _blurEffect->setEnabled(false);
    _containerStackedWidget->setGraphicsEffect(_blurEffect);

    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setSpacing(0);
    _mainLayout->setContentsMargins(0, 0, 0, 0);
    _mainLayout->addWidget(_containerStackedWidget);

    _themeMode = eTheme->getThemeMode();
    connect(eTheme, &ElaTheme::themeModeChanged, this, &ElaCentralStackedWidget::onThemeModeChanged);
}

ElaCentralStackedWidget::~ElaCentralStackedWidget()
{
}

QStackedWidget* ElaCentralStackedWidget::getContainerStackedWidget() const
{
    return _containerStackedWidget;
}

void ElaCentralStackedWidget::setCustomWidget(QWidget* widget)
{
    if (!widget)
    {
        return;
    }
    if (_customWidget)
    {
        _mainLayout->removeWidget(_customWidget);
    }
    _mainLayout->insertWidget(0, widget);
    _customWidget = widget;
}

QWidget* ElaCentralStackedWidget::getCustomWidget() const
{
    return this->_customWidget;
}

void ElaCentralStackedWidget::onThemeModeChanged(ElaThemeType::ThemeMode themeMode)
{
    _themeMode = themeMode;
}

void ElaCentralStackedWidget::setIsTransparent(bool isTransparent)
{
    this->_isTransparent = isTransparent;
    update();
}

bool ElaCentralStackedWidget::getIsTransparent() const
{
    return _isTransparent;
}

void ElaCentralStackedWidget::setIsHasRadius(bool isHasRadius)
{
    this->_isHasRadius = isHasRadius;
    update();
}

void ElaCentralStackedWidget::cancelWindowStackSwitch()
{
    const QPointer<ElaCentralStackedWidget> alive(this);
    const QPointer<StackSwitchState> state(switchState(this));
    ++state->revision;
    state->target.clear();
    QList<QPointer<QPropertyAnimation>> animations;
    for (auto animation : findChildren<QPropertyAnimation*>(QString(), Qt::FindDirectChildrenOnly))
        animations.append(animation);
    for (auto animation : animations) {
        if (!alive)
            return;
        if (animation) {
            animation->stop();
            if (animation)
                animation->deleteLater();
        }
    }
    if (!alive)
        return;
    _targetStackPix = {};
    _currentStackPix = {};
    _blurEffect->setEnabled(false);
    if (!alive)
        return;
    _isDrawNewPix = false;
    update();
}

void ElaCentralStackedWidget::doWindowStackSwitch(ElaWindowType::StackSwitchMode stackSwitchMode, int nodeIndex,
                                                  bool isRouteBack)
{
    const QPointer<ElaCentralStackedWidget> alive(this);
    const QPointer<StackSwitchState> state(switchState(this));
    const QPointer<QWidget> stableTarget(_containerStackedWidget->widget(nodeIndex));
    if (!stableTarget)
        return;
    if (_pLastTargetIndex == nodeIndex &&
        (state->target == stableTarget || _containerStackedWidget->currentWidget() == stableTarget))
        return;
    cancelWindowStackSwitch();
    if (!alive || !state || !stableTarget)
        return;
    state->target = stableTarget;
    const auto revision = state->revision;
    const auto valid = [alive, state, stableTarget, revision] {
        return alive && state && stableTarget && state->revision == revision &&
               alive->_containerStackedWidget->indexOf(stableTarget) >= 0;
    };
    _pLastTargetIndex = nodeIndex;
    _stackSwitchMode = stackSwitchMode;
    switch (stackSwitchMode)
    {
    case ElaWindowType::None:
    {
        _containerStackedWidget->setCurrentWidget(stableTarget);
        if (!valid())
            return;
        break;
    }
    case ElaWindowType::Popup:
    {
        const QPointer<QWidget> targetWidget(stableTarget);
        _containerStackedWidget->setCurrentWidget(stableTarget);
        if (!valid())
            return;
        _getTargetStackPix();
        if (!valid())
            return;
        targetWidget->setVisible(false);
        if (!valid())
            return;
        QPropertyAnimation* popupAnimation = new QPropertyAnimation(this, "pPopupAnimationYOffset", this);
        connect(popupAnimation, &QPropertyAnimation::valueChanged, this, [=]() {
            if (!valid())
                return;
            update();
        });
        connect(popupAnimation, &QPropertyAnimation::finished, this, [=]() {
            if (!valid())
                return;
            _targetStackPix = QPixmap();
            targetWidget->setVisible(true);
            if (!valid())
                return;
        });
        popupAnimation->setEasingCurve(QEasingCurve::OutCubic);
        popupAnimation->setDuration(300);
        int targetWidgetY = _containerStackedWidget->mapToParent(QPoint(0, 0)).y();
        popupAnimation->setEndValue(targetWidgetY);
        targetWidgetY += 80;
        popupAnimation->setStartValue(targetWidgetY);
        popupAnimation->start(QAbstractAnimation::DeleteWhenStopped);
        break;
    }
    case ElaWindowType::Scale:
    {
        const QPointer<QWidget> targetWidget(stableTarget);
        _getCurrentStackPix();
        if (!valid())
            return;
        _containerStackedWidget->setCurrentWidget(stableTarget);
        if (!valid())
            return;
        targetWidget->setVisible(true);
        if (!valid())
            return;
        _getTargetStackPix();
        if (!valid())
            return;
        targetWidget->setVisible(false);
        if (!valid())
            return;
        _isDrawNewPix = false;
        QPropertyAnimation* currentPixZoomAnimation = new QPropertyAnimation(this, "pScaleAnimationRatio", this);
        connect(currentPixZoomAnimation, &QPropertyAnimation::valueChanged, this, [=]() {
            if (!valid())
                return;
            update();
        });
        connect(currentPixZoomAnimation, &QPropertyAnimation::finished, this, [=]() {
            if (!valid())
                return;
            _isDrawNewPix = true;
            QPropertyAnimation* targetPixZoomAnimation = new QPropertyAnimation(this, "pScaleAnimationRatio", this);
            connect(targetPixZoomAnimation, &QPropertyAnimation::valueChanged, this, [=]() {
                if (!valid())
                    return;
                update();
            });
            connect(targetPixZoomAnimation, &QPropertyAnimation::finished, this, [=]() {
                if (!valid())
                    return;
                _targetStackPix = QPixmap();
                _currentStackPix = QPixmap();
                targetWidget->setVisible(true);
                if (!valid())
                    return;
            });
            if (isRouteBack)
            {
                targetPixZoomAnimation->setStartValue(1.5);
                targetPixZoomAnimation->setEndValue(1);
            }
            else
            {
                // 放大
                targetPixZoomAnimation->setStartValue(0.85);
                targetPixZoomAnimation->setEndValue(1);
            }
            targetPixZoomAnimation->setDuration(300);
            targetPixZoomAnimation->setEasingCurve(QEasingCurve::OutCubic);
            targetPixZoomAnimation->start(QAbstractAnimation::DeleteWhenStopped);
        });
        if (isRouteBack)
        {
            // 缩小
            currentPixZoomAnimation->setStartValue(1);
            currentPixZoomAnimation->setEndValue(0.85);
        }
        else
        {
            // 放大
            currentPixZoomAnimation->setStartValue(1);
            currentPixZoomAnimation->setEndValue(1.15);
        }
        currentPixZoomAnimation->setDuration(150);
        currentPixZoomAnimation->start(QAbstractAnimation::DeleteWhenStopped);

        QPropertyAnimation* currentPixOpacityAnimation =
            new QPropertyAnimation(this, "pScaleAnimationPixOpacity", this);
        connect(currentPixZoomAnimation, &QPropertyAnimation::finished, this, [=]() {
            if (!valid())
                return;
            QPropertyAnimation* targetPixOpacityAnimation =
                new QPropertyAnimation(this, "pScaleAnimationPixOpacity", this);
            targetPixOpacityAnimation->setStartValue(0);
            targetPixOpacityAnimation->setEndValue(1);
            targetPixOpacityAnimation->setDuration(300);
            targetPixOpacityAnimation->start(QAbstractAnimation::DeleteWhenStopped);
        });
        currentPixOpacityAnimation->setStartValue(1);
        currentPixOpacityAnimation->setEndValue(0);
        currentPixOpacityAnimation->setDuration(150);
        currentPixOpacityAnimation->start(QAbstractAnimation::DeleteWhenStopped);
        break;
    }
    case ElaWindowType::Flip:
    {
        const QPointer<QWidget> targetWidget(stableTarget);
        _getCurrentStackPix();
        if (!valid())
            return;
        _containerStackedWidget->setCurrentWidget(stableTarget);
        if (!valid())
            return;
        _getTargetStackPix();
        if (!valid())
            return;
        targetWidget->setVisible(false);
        if (!valid())
            return;
        QPropertyAnimation* flipAnimation = new QPropertyAnimation(this, "pFlipAnimationRatio", this);
        connect(flipAnimation, &QPropertyAnimation::valueChanged, this, [=]() {
            if (!valid())
                return;
            update();
        });
        connect(flipAnimation, &QPropertyAnimation::finished, this, [=]() {
            if (!valid())
                return;
            _targetStackPix = QPixmap();
            _currentStackPix = QPixmap();
            targetWidget->setVisible(true);
            if (!valid())
                return;
        });
        flipAnimation->setEasingCurve(QEasingCurve::InOutSine);
        flipAnimation->setDuration(650);
        flipAnimation->setStartValue(0);
        flipAnimation->setEndValue(isRouteBack ? -180 : 180);
        flipAnimation->start(QAbstractAnimation::DeleteWhenStopped);
        break;
    }
    case ElaWindowType::Blur:
    {
        _targetStackPix = QPixmap();
        _blurEffect->setEnabled(true);
        QPropertyAnimation* blurAnimation = new QPropertyAnimation(this, "pBlurAnimationRadius", this);
        connect(blurAnimation, &QPropertyAnimation::valueChanged, this, [=]() {
            if (!valid())
                return;
            _blurEffect->setBlurRadius(_pBlurAnimationRadius);
        });
        connect(blurAnimation, &QPropertyAnimation::finished, this, [=]() {
            if (!valid())
                return;
            _blurEffect->setEnabled(false);
        });
        blurAnimation->setEasingCurve(QEasingCurve::InOutSine);
        blurAnimation->setDuration(350);
        blurAnimation->setStartValue(40);
        blurAnimation->setEndValue(2);
        blurAnimation->start(QAbstractAnimation::DeleteWhenStopped);
        QApplication::processEvents();
        if (!valid())
            return;
        _containerStackedWidget->setCurrentWidget(stableTarget);
        if (!valid())
            return;
        break;
    }
    }
}

void ElaCentralStackedWidget::paintEvent(QPaintEvent* event)
{
    QRect targetRect = this->rect();
    targetRect.adjust(1, 1, 10, 10);
    QPainter painter(this);
    painter.save();
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    if (!_isTransparent)
    {
        painter.setPen(QPen(ElaThemeColor(_themeMode, BasicBaseLine), 1.5));
        painter.setBrush(ElaThemeColor(_themeMode, WindowCentralStackBase));
        if (_isHasRadius)
        {
            painter.drawRoundedRect(targetRect, 10, 10);
        }
        else
        {
            painter.drawRect(targetRect);
        }
    }
    // 切换动画
    if (!_targetStackPix.isNull())
    {
        QPoint centralStackPos = _containerStackedWidget->mapToParent(QPoint(0, 0));
        QRect centralStackRect = QRect(centralStackPos.x(), centralStackPos.y(), _containerStackedWidget->width(), _containerStackedWidget->height());
        QPainterPath clipPath;
        clipPath.addRoundedRect(centralStackRect, 10, 10);
        painter.setClipPath(clipPath);
        switch (_stackSwitchMode)
        {
        case ElaWindowType::None:
        {
            break;
        }
        case ElaWindowType::Popup:
        {
            painter.drawPixmap(QRect(0, _pPopupAnimationYOffset, width(), _containerStackedWidget->height()), _targetStackPix);
            break;
        }
        case ElaWindowType::Scale:
        {
            painter.setOpacity(_pScaleAnimationPixOpacity);
            painter.translate(_containerStackedWidget->rect().center());
            painter.scale(_pScaleAnimationRatio, _pScaleAnimationRatio);
            painter.translate(-_containerStackedWidget->rect().center());
            painter.drawPixmap(centralStackRect, _isDrawNewPix ? _targetStackPix : _currentStackPix);
            break;
        }
        case ElaWindowType::Flip:
        {
            QTransform transform;
            transform.translate(centralStackRect.center().x(), 0);
            if (abs(_pFlipAnimationRatio) >= 90)
            {
                transform.rotate(-180 + _pFlipAnimationRatio, Qt::YAxis);
            }
            else
            {
                transform.rotate(_pFlipAnimationRatio, Qt::YAxis);
            }
            transform.translate(-centralStackRect.center().x(), 0);
            painter.setTransform(transform);
            if (abs(_pFlipAnimationRatio) >= 90)
            {
                painter.drawPixmap(centralStackRect, _targetStackPix);
            }
            else
            {
                painter.drawPixmap(centralStackRect, _currentStackPix);
            }
            break;
        }
        case ElaWindowType::Blur:
        {
            break;
        }
        }
    }
    painter.restore();
}

void ElaCentralStackedWidget::_getCurrentStackPix()
{
    const QPointer<ElaCentralStackedWidget> alive(this);
    const QPointer<StackSwitchState> state(switchState(this));
    const QPointer<QWidget> current(_containerStackedWidget->currentWidget());
    if (!current)
        return;
    const auto revision = state->revision;
    _targetStackPix = QPixmap();
    bool isTransparent = _isTransparent;
    _isTransparent = true;
    current->setVisible(true);
    if (!alive)
        return;
    if (!state || !current || state->revision != revision) {
        _isTransparent = isTransparent;
        return;
    }
    const auto pix = _containerStackedWidget->grab();
    if (!alive)
        return;
    if (!state || !current || state->revision != revision) {
        _isTransparent = isTransparent;
        return;
    }
    _currentStackPix = pix;
    current->setVisible(false);
    if (!alive)
        return;
    _isTransparent = isTransparent;
}

void ElaCentralStackedWidget::_getTargetStackPix()
{
    const QPointer<ElaCentralStackedWidget> alive(this);
    const QPointer<StackSwitchState> state(switchState(this));
    const auto revision = state->revision;
    _targetStackPix = QPixmap();
    bool isTransparent = _isTransparent;
    _isTransparent = true;
    const auto pix = _containerStackedWidget->grab();
    if (!alive)
        return;
    if (!state || state->revision != revision) {
        _isTransparent = isTransparent;
        return;
    }
    _targetStackPix = pix;
    _isTransparent = isTransparent;
}
