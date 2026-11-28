/** @file fixed_tabs_p.hpp
 * @brief 宿主固定标签页复用 Ela 外观与主题，阻止拖出为未托管窗口。
 */
#pragma once
#include "theme_palette_p.hpp"
#include <ElaTabWidget.h>
#include <ElaTheme.h>
#include <QMouseEvent>
#include <QPalette>
#include <QTabBar>
#include <algorithm>
namespace eve::ui::detail {
/** @brief 固定诊断标签归宿主窗口所有，禁止 Ela 默认的拖出独立窗口行为。 */
class FixedDiagnosticsTabs final : public ElaTabWidget {
public:
    /** @brief 保留 Ela 外观和键鼠切页，阻止拖动与跨窗口接收标签。 */
    explicit FixedDiagnosticsTabs(QWidget* parent) : ElaTabWidget(parent)
    {
        setMovable(false);
        setTabsClosable(false);
        setTabSize(QSize(84, 34));
        tabBar()->setFixedHeight(34);
        tabBar()->setExpanding(false);
        tabBar()->setElideMode(Qt::ElideRight);
        setIsTabTransparent(true);
        setStyleSheet("QTabWidget::pane{border:0;background:transparent;}");
        applyTheme(eTheme->getThemeMode());
        QObject::connect(eTheme, &ElaTheme::themeModeChanged, this, [this](ElaThemeType::ThemeMode mode) { applyTheme(mode); });
        setAcceptDrops(false);
        tabBar()->setAcceptDrops(false);
        tabBar()->installEventFilter(this);
    }

    int addTab(QWidget* page, const QString& caption)
    {
        fitCaption(caption);
        return ElaTabWidget::addTab(page, caption);
    }

    void setTabText(int index, const QString& caption)
    {
        fitCaption(caption);
        ElaTabWidget::setTabText(index, caption);
    }

private:
    void fitCaption(const QString& caption)
    {
        auto size = getTabSize();
        // Ela's label inset and the stylesheet's horizontal padding both need room.
        const int width = std::min(160, tabBar()->fontMetrics().horizontalAdvance(caption) + 36);
        if (width > size.width()) {
            size.setWidth(width);
            setTabSize(size);
        }
    }

    /** @brief Qt 标签容器的调色板同步 Ela 主题，避免透明编辑器叠在原生白底上。 */
    void applyTheme(ElaThemeType::ThemeMode mode)
    {
        setPalette(workbenchContentPalette(palette(), mode));
        tabBar()->setStyleSheet(QStringLiteral(
            "QTabBar::tab{background:%1;color:%2;border:0;border-bottom:2px solid transparent;"
            "height:24px;padding:4px 10px;}QTabBar::tab:!selected{border-radius:4px;}"
            "QTabBar::tab:selected{background:transparent;border-bottom:2px solid %3;}")
            .arg(eTheme->getThemeColor(mode, ElaThemeType::BasicSelectedAlpha).name(QColor::HexArgb),
                 eTheme->getThemeColor(mode, ElaThemeType::BasicText).name(),
                 eTheme->getThemeColor(mode, ElaThemeType::PrimaryNormal).name()));
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
} // namespace eve::ui::detail
