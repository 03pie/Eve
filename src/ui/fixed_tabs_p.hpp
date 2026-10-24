/** @file fixed_tabs_p.hpp
 * @brief 宿主固定标签页复用 Ela 外观与主题，阻止拖出为未托管窗口。
 */
#pragma once
#include <ElaTabWidget.h>
#include <ElaTheme.h>
#include <QTabBar>
#include <QMouseEvent>
#include <QPalette>
namespace cordis::ui::detail {
/** @brief 固定诊断标签归宿主窗口所有，禁止 Ela 默认的拖出独立窗口行为。 */
class FixedDiagnosticsTabs final : public ElaTabWidget {
public:
    /** @brief 保留 Ela 外观和键鼠切页，阻止拖动与跨窗口接收标签。 */
    explicit FixedDiagnosticsTabs(QWidget* parent) : ElaTabWidget(parent)
    {
        setMovable(false);
        setTabsClosable(false);
        setTabSize(QSize(132, 34));
        setIsTabTransparent(true);
        setStyleSheet("QTabWidget::pane{border:0;background:transparent;}");
        applyTheme(eTheme->getThemeMode());
        QObject::connect(eTheme, &ElaTheme::themeModeChanged, this, [this](ElaThemeType::ThemeMode mode) { applyTheme(mode); });
        setAcceptDrops(false);
        tabBar()->setAcceptDrops(false);
        tabBar()->installEventFilter(this);
    }
private:
    /** @brief Qt 标签容器的调色板同步 Ela 主题，避免透明编辑器叠在原生白底上。 */
    void applyTheme(ElaThemeType::ThemeMode mode)
    {
        auto colors = palette();
        colors.setColor(QPalette::Window, eTheme->getThemeColor(mode, ElaThemeType::WindowBase));
        colors.setColor(QPalette::Base, eTheme->getThemeColor(mode, ElaThemeType::BasicBase));
        colors.setColor(QPalette::Text, eTheme->getThemeColor(mode, ElaThemeType::BasicText));
        setPalette(colors);
    }
protected:
    /** @brief ElaTabBar 的拖出逻辑独立于 movable 开关，须在其 mouseMoveEvent 前拦截。 */
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == tabBar() && event->type() == QEvent::MouseMove &&
            static_cast<QMouseEvent*>(event)->buttons() != Qt::NoButton) return true;
        return ElaTabWidget::eventFilter(watched, event);
    }
};
} // namespace cordis::ui::detail
