/** @file appearance_p.hpp
 * @brief 统一宿主页的文字层级与主操作颜色，继续由 Ela 管理控件交互和主题。
 */
#pragma once
#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToggleSwitch.h>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QString>

namespace cordis::ui::detail {
/** @brief 继承 Ela 的开关绘制与动画，补齐 Space/Enter 操作和可见键盘焦点。 */
class KeyboardToggleSwitch final : public ElaToggleSwitch {
public:
    /** @brief 可以通过 Tab 进入，但刷新真实开关状态不会移动焦点。 */
    explicit KeyboardToggleSwitch(QWidget* parent) : ElaToggleSwitch(parent) { setFocusPolicy(Qt::StrongFocus); }

protected:
    /** @brief 一次按键只提交一次操作，自动重复不触发额外管理请求。 */
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            event->accept();
            if (isEnabled() && !event->isAutoRepeat())
                setIsToggled(!getIsToggled());
            return;
        }
        ElaToggleSwitch::keyPressEvent(event);
    }
    /** @brief 焦点环采用 Ela 强调色，不替换原生滑块或动画。 */
    void paintEvent(QPaintEvent* event) override
    {
        ElaToggleSwitch::paintEvent(event);
        if (hasFocus()) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::PrimaryNormal), 1.5));
            painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), height() / 2.0, height() / 2.0);
        }
    }
};
/** @brief Ela 按钮增加可见键盘焦点，按下/悬停/禁用仍使用 Ela 绘制。 */
class ActionButton final : public ElaPushButton {
public:
    /** @brief 不改变调用方信号和按钮能力。 */
    ActionButton(const QString& text, QWidget* parent) : ElaPushButton(text, parent) {}

protected:
    /** @brief 键盘导航可明确辨认当前操作。 */
    void paintEvent(QPaintEvent* event) override
    {
        ElaPushButton::paintEvent(event);
        if (hasFocus()) {
            QPainter painter(this);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(
                QPen(eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::BasicText), 1, Qt::DotLine));
            painter.drawRoundedRect(rect().adjusted(3, 3, -4, -4), 3, 3);
        }
    }
};
/** @brief 次级说明随 Ela 明暗主题更新，避免依赖固定灰色或降低整个控件的不透明度。 */
inline void secondaryText(ElaText* text)
{
    const auto update = [text](ElaThemeType::ThemeMode mode) {
        auto color = eTheme->getThemeColor(mode, ElaThemeType::BasicDetailsText);
        const auto foreground = eTheme->getThemeColor(mode, ElaThemeType::BasicText);
        color.setRed((color.red() * 3 + foreground.red()) / 4);
        color.setGreen((color.green() * 3 + foreground.green()) / 4);
        color.setBlue((color.blue() * 3 + foreground.blue()) / 4);
        // ElaText 绘制时会恢复基础调色板，叶子控件的样式表负责保留次级文字色。
        text->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(color.name()));
    };
    update(eTheme->getThemeMode());
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, text, update);
}

/** @brief 一个操作区只强调主要动作；禁用、按下和悬停仍采用 Ela 的原生绘制。 */
inline void primaryButton(ElaPushButton* button)
{
    button->setLightDefaultColor(eTheme->getThemeColor(ElaThemeType::Light, ElaThemeType::PrimaryNormal));
    button->setLightHoverColor(eTheme->getThemeColor(ElaThemeType::Light, ElaThemeType::PrimaryHover));
    button->setLightPressColor(eTheme->getThemeColor(ElaThemeType::Light, ElaThemeType::PrimaryPress));
    button->setDarkDefaultColor(eTheme->getThemeColor(ElaThemeType::Dark, ElaThemeType::PrimaryNormal));
    button->setDarkHoverColor(eTheme->getThemeColor(ElaThemeType::Dark, ElaThemeType::PrimaryHover));
    button->setDarkPressColor(eTheme->getThemeColor(ElaThemeType::Dark, ElaThemeType::PrimaryPress));
    button->setLightTextColor(eTheme->getThemeColor(ElaThemeType::Light, ElaThemeType::BasicTextInvert));
    button->setDarkTextColor(eTheme->getThemeColor(ElaThemeType::Dark, ElaThemeType::BasicTextInvert));
}
} // namespace cordis::ui::detail
