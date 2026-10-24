#include "ElaSuggestBoxPrivate.h"

#include "ElaBaseListView.h"
#include "ElaLineEdit.h"
#include "ElaSuggestBox.h"
#include "ElaSuggestBoxSearchViewContainer.h"
#include "ElaSuggestModel.h"
#include <QLayout>
#include <QAbstractAnimation>
#include <QPointer>
#include <QPropertyAnimation>
#include <QUuid>
ElaSuggestion::ElaSuggestion(QObject* parent)
    : QObject(parent)
{
    _pElaIcon = ElaIconType::None;
    _pSuggestText = "";
    _pSuggestKey = QUuid::createUuid().toString().remove("{").remove("}").remove("-");
    _pSuggestData = QVariantMap();
}

ElaSuggestion::~ElaSuggestion()
{
}

ElaSuggestBoxPrivate::ElaSuggestBoxPrivate(QObject* parent)
    : QObject{parent}
{
}

ElaSuggestBoxPrivate::~ElaSuggestBoxPrivate()
{
}

void ElaSuggestBoxPrivate::onThemeModeChanged(ElaThemeType::ThemeMode themeMode)
{
    _themeMode = themeMode;
    _searchEdit->removeAction(_themeMode == ElaThemeType::Light ? _darkSearchAction : _lightSearchAction);
    _searchEdit->addAction(_themeMode == ElaThemeType::Light ? _lightSearchAction : _darkSearchAction, QLineEdit::TrailingPosition);
    _searchEdit->update();
}

void ElaSuggestBoxPrivate::onSearchEditTextEdit(const QString& searchText)
{
    Q_Q(ElaSuggestBox);
    if (_resetting || !_searchViewBaseWidget || !_searchView) return;
    const auto version = _suggestionVersion;
    QPointer<ElaSuggestBoxPrivate> alive(this);
    if (searchText.isEmpty())
    {
        resetSearch();
        return;
    }
    // 新查询取代旧查询，旧收起动画不能稍后清空新一轮搜索结果。
    for (auto animation : findChildren<QAbstractAnimation*>()) { animation->stop(); animation->deleteLater(); }
    _lastSize = {};
    _isExpandAnimationFinished = true;
    _isCloseAnimationFinished = true;
    QVector<ElaSuggestion*> suggestionVector;
    for (const auto& suggest: _suggestionVector)
    {
        if (suggest->getSuggestText().contains(searchText, _pCaseSensitivity))
        {
            suggestionVector.append(suggest);
        }
    }
    if (!suggestionVector.isEmpty())
    {
        _searchModel->setSearchSuggestion(suggestionVector);
        if (!alive) return;
        if (version != _suggestionVersion) { resetSearch(); return; }
        // 复位信号可规范化输入；外层查询结束后以真实文本重新收敛结果。
        if (_searchEdit->text() != searchText) { onSearchEditTextEdit(_searchEdit->text()); return; }
        int rowCount = suggestionVector.count();
        if (rowCount > 4)
        {
            rowCount = 4;
        }
        if (!_searchViewBaseWidget->isVisible())
        {
            q->raise();
            if (!alive || !_searchViewBaseWidget) return;
            _searchViewBaseWidget->show();
            if (!alive) return;
            _searchViewBaseWidget->raise();
            QPoint cyclePoint = q->mapTo(q->window(), QPoint(-5, q->height()));
            _searchViewBaseWidget->move(cyclePoint);
            _startSizeAnimation(QSize(q->width() + 10, 0), QSize(q->width() + 10, 40 * rowCount + 16));
            _searchView->move(_searchView->x(), -(40 * rowCount + 16));
        }
        else
        {
            _startSizeAnimation(_searchViewBaseWidget->size(), QSize(q->width() + 12, 40 * rowCount + 16));
        }
        _startExpandAnimation();
    }
    else
    {
        resetSearch();
    }
}

void ElaSuggestBoxPrivate::onSearchViewClicked(const QModelIndex& index)
{
    Q_Q(ElaSuggestBox);
    if (!_searchView || !index.isValid() || index.model() != _searchModel || index.row() < 0 || index.row() >= _searchModel->rowCount())
    {
        return;
    }
    ElaSuggestion* suggest = _searchModel->getSearchSuggestion(index.row());
    if (!suggest) return;
    ElaSuggestBox::SuggestData data(suggest->getElaIcon(), suggest->getSuggestText(), suggest->getSuggestData());
    data.setSuggestKey(suggest->getSuggestKey());
    QPointer<ElaSuggestBoxPrivate> alive(this);
    _searchView->clearSelection();
    if (!alive) return;
    _searchEdit->setText(data.getSuggestText());
    if (!alive) return;
    _startCloseAnimation();
    if (alive) Q_EMIT q->suggestionClicked(data);
}

void ElaSuggestBoxPrivate::resetSearch()
{
    if (_resetting) return;
    _resetting = true;
    for (auto animation : findChildren<QAbstractAnimation*>()) { animation->stop(); animation->deleteLater(); }
    _lastSize = {};
    _isExpandAnimationFinished = true;
    _isCloseAnimationFinished = true;
    QPointer<ElaSuggestBoxPrivate> alive(this);
    _searchModel->clearSearchNode();
    if (alive && _searchViewBaseWidget) _searchViewBaseWidget->hide();
    if (alive) _resetting = false;
}

void ElaSuggestBoxPrivate::_startSizeAnimation(QSize oldSize, QSize newSize)
{
    if (_lastSize.isValid() && _lastSize == newSize)
    {
        return;
    }
    _shadowLayout->removeWidget(_searchView);
    QPropertyAnimation* expandAnimation = new QPropertyAnimation(_searchViewBaseWidget, "size", this);
    connect(expandAnimation, &QPropertyAnimation::valueChanged, this, [=]() {
        if (_searchView && _searchViewBaseWidget) _searchView->resize(_searchViewBaseWidget->size());
    });
    connect(expandAnimation, &QPropertyAnimation::finished, this, [=]() {
        if (_searchView && _searchViewBaseWidget) _shadowLayout->addWidget(_searchView);
    });
    expandAnimation->setDuration(300);
    expandAnimation->setEasingCurve(QEasingCurve::InOutSine);
    expandAnimation->setStartValue(oldSize);
    expandAnimation->setEndValue(newSize);
    expandAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    _lastSize = newSize;
}

void ElaSuggestBoxPrivate::_startExpandAnimation()
{
    if (!_isExpandAnimationFinished)
    {
        return;
    }
    _isCloseAnimationFinished = true;
    _isExpandAnimationFinished = false;
    QPropertyAnimation* expandAnimation = new QPropertyAnimation(_searchView, "pos", this);
    connect(expandAnimation, &QPropertyAnimation::finished, this, [=]() {
        _isExpandAnimationFinished = true;
        if (_searchView) _searchView->clearSelection();
    });
    expandAnimation->setDuration(300);
    expandAnimation->setEasingCurve(QEasingCurve::InOutSine);
    expandAnimation->setStartValue(_searchView->pos());
    expandAnimation->setEndValue(QPoint(8, 8));
    expandAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ElaSuggestBoxPrivate::_startCloseAnimation()
{
    if (!_isCloseAnimationFinished || !_searchView || !_searchViewBaseWidget)
    {
        return;
    }
    _isExpandAnimationFinished = true;
    _isCloseAnimationFinished = false;
    QPropertyAnimation* baseWidgetsAnimation = new QPropertyAnimation(_searchViewBaseWidget, "size", this);
    baseWidgetsAnimation->setDuration(300);
    baseWidgetsAnimation->setEasingCurve(QEasingCurve::InOutSine);
    baseWidgetsAnimation->setStartValue(_searchViewBaseWidget->size());
    baseWidgetsAnimation->setEndValue(QSize(_searchViewBaseWidget->width(), 0));
    baseWidgetsAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    QPropertyAnimation* closeAnimation = new QPropertyAnimation(_searchView, "pos", this);
    connect(closeAnimation, &QPropertyAnimation::finished, this, [=]() {
        _isCloseAnimationFinished = true;
        QPointer<ElaSuggestBoxPrivate> alive(this);
        _searchModel->clearSearchNode();
        if (alive && _searchViewBaseWidget) _searchViewBaseWidget->hide();
    });
    closeAnimation->setDuration(300);
    closeAnimation->setEasingCurve(QEasingCurve::InOutSine);
    closeAnimation->setStartValue(_searchView->pos());
    closeAnimation->setEndValue(QPoint(_searchView->pos().x(), -_searchView->height()));
    closeAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    _lastSize = baseWidgetsAnimation->endValue().toSize();
}
