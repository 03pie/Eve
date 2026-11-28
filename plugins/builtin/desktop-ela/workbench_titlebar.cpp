#include "workbench_titlebar_p.hpp"
#include "desktop_text.hpp"

#include <ElaAppBar.h>
#include <ElaMenu.h>
#include <ElaMenuBar.h>
#include <ElaToolButton.h>
#include <ElaWindow.h>
#include <QEvent>
#include <QHBoxLayout>
#include <QScopedValueRollback>
#include <algorithm>
#include <initializer_list>
#include <stdexcept>

namespace eve::ui::detail {
namespace {
constexpr int controlHeight = 30;
constexpr int controlGap = 4;
constexpr int dragReserve = 48;
constexpr int compactQuickOpenWidth = 36;
constexpr int preferredQuickOpenWidth = 340;

// Ela's own two stretches provide the draggable surface between the interactive slots.
QWidget* slot(ElaWindow* window, const char* name, std::initializer_list<QWidget*> controls)
{
    auto area = new QWidget(window);
    area->setObjectName(name);
    area->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    auto layout = new QHBoxLayout(area);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(controlGap);
    for (auto control : controls) layout->addWidget(control, 0, Qt::AlignVCenter);
    return area;
}

int widthHint(QWidget* widget)
{
    return std::clamp(widget->sizeHint().width(), widget->minimumWidth(), widget->maximumWidth());
}

class WorkbenchTitlebar final : public QObject {
public:
    WorkbenchTitlebar(ElaWindow* window, ElaMenuBar* menus, ElaToolButton* back, ElaToolButton* forward,
                      ElaToolButton* quickOpen, ElaToolButton* navigation, ElaToolButton* panel)
        : QObject(window), appBar_(window->findChild<ElaAppBar*>("ElaAppBar", Qt::FindDirectChildrenOnly)),
          menus_(menus), back_(back), forward_(forward), quickOpen_(quickOpen), navigation_(navigation), panel_(panel)
    {
        if (!appBar_) throw std::logic_error("Pinned Ela application bar unavailable");

        // Keep the OS title and native app icon. Only hide the redundant label in the custom chrome.
        if (auto title = appBar_->findChild<QWidget*>("ElaAppBarTitle", Qt::FindDirectChildrenOnly)) {
            title->hide();
            connect(window, &QWidget::windowTitleChanged, title, [title] { title->hide(); });
        }
        window->setWindowButtonFlags(window->getWindowButtonFlags() &
            ~(ElaAppBarType::NavigationButtonHint | ElaAppBarType::StayTopButtonHint |
              ElaAppBarType::ThemeChangeButtonHint));

        overflow_ = new ElaToolButton(window);
        overflow_->setObjectName("EveMenuOverflow");
        overflow_->setElaIcon(ElaIconType::Ellipsis);
        overflow_->setAccessibleName(eve::desktopText("workbench.applicationMenu"));
        overflow_->setToolTip(eve::desktopText("workbench.applicationMenu"));
        overflow_->setFixedSize(42, controlHeight);
        auto overflowMenu = new ElaMenu(overflow_);
        overflowMenu->setObjectName("EveMenuOverflowPopup");
        for (auto action : menus_->actions()) overflowMenu->addAction(action);
        overflow_->setMenu(overflowMenu);

        for (auto button : {back_, forward_, navigation_, panel_}) button->setFixedSize(32, controlHeight);
        for (auto button : {back_, forward_, quickOpen_, navigation_, panel_, overflow_}) button->setFocusPolicy(Qt::TabFocus);
        quickOpen_->setFixedHeight(controlHeight);
        menus_->setFixedHeight(controlHeight);
        menus_->setMinimumWidth(0);
        menus_->setMaximumWidth(QWIDGETSIZE_MAX);
        left_ = slot(window, "EveTitleMenuControls", {menus_, overflow_});
        center_ = slot(window, "EveCommandCenter", {back_, forward_, quickOpen_});
        right_ = slot(window, "EveTitleLayoutControls", {navigation_, panel_});
        left_->setMinimumWidth(overflow_->width());
        center_->setMinimumWidth(compactQuickOpenWidth);
        window->setCustomWidget(ElaAppBarType::LeftArea, left_);
        window->setCustomWidget(ElaAppBarType::MiddleArea, center_);
        window->setCustomWidget(ElaAppBarType::RightArea, right_);
        left_->show();
        center_->show();
        right_->show();
        const auto alignContent = [bar = appBar_] {
            const int height = bar->getAppBarHeight();
            // An external desktop supplies its own chrome. Clipped controls would still accept focus.
            bar->setVisible(height > 0);
            // Ela lays out its native controls from the top of the content row.
            const int padding = std::max(0, height - controlHeight);
            bar->layout()->setContentsMargins(0, padding / 2, 0, padding - padding / 2);
        };
        connect(appBar_, &ElaAppBar::pAppBarHeightChanged, this, alignContent);
        alignContent();
        appBar_->installEventFilter(this);
        updateWidths();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == appBar_ &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest))
            updateWidths();
        return QObject::eventFilter(watched, event);
    }

private:
    int nativeWidth() const
    {
        // Discard the host slots; the remaining size hints are Ela's icon, window buttons and zero-width stretches.
        const auto layout = appBar_->layout();
        int width = layout->contentsMargins().left() + layout->contentsMargins().right();
        for (int i = 0; i < layout->count(); ++i) {
            const auto item = layout->itemAt(i);
            const auto widget = item->widget();
            if (widget && (widget == left_ || widget == center_ || widget == right_)) continue;
            width += item->sizeHint().width();
        }
        return width;
    }

    void updateWidths()
    {
        if (updating_) return;
        QScopedValueRollback<bool> updating(updating_, true);
        const int available = appBar_->width() - nativeWidth() - dragReserve;
        const int fullMenu = widthHint(menus_);
        const int navigation = widthHint(navigation_);
        const int panel = widthHint(panel_);
        const int fullRight = navigation + controlGap + panel;
        const int readableSearch = std::max(168, quickOpen_->fontMetrics().horizontalAdvance(quickOpen_->text()) +
                                                    quickOpen_->iconSize().width() + 28);
        const int preferredSearch = std::max(preferredQuickOpenWidth, readableSearch);
        const int history = widthHint(back_) + widthHint(forward_) + 2 * controlGap;
        // Re-expand the menu and history together only when they no longer shrink Quick Open.
        const bool full = available >= fullMenu + fullRight + history + preferredSearch;
        const int afterMenu = available - (full ? fullMenu : overflow_->width());
        const bool showPanel = afterMenu >= panel + compactQuickOpenWidth;
        const bool showNavigation = showPanel && afterMenu >= fullRight + compactQuickOpenWidth;
        const int right = showNavigation ? fullRight : showPanel ? panel : 0;
        const int search = afterMenu - right - (full ? history : 0);
        const bool showCaption = search >= readableSearch;

        menus_->setVisible(full);
        overflow_->setVisible(!full);
        navigation_->setVisible(showNavigation);
        panel_->setVisible(showPanel);
        back_->setVisible(full);
        forward_->setVisible(full);
        quickOpen_->setToolButtonStyle(showCaption ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
        quickOpen_->setFixedWidth(showCaption ? std::min(search, preferredSearch) : compactQuickOpenWidth);
    }

    ElaAppBar* appBar_;
    ElaMenuBar* menus_;
    ElaToolButton *back_, *forward_, *quickOpen_, *navigation_, *panel_, *overflow_;
    QWidget *left_, *center_, *right_;
    bool updating_ = false;
};
}

void installWorkbenchTitlebar(ElaWindow* window, ElaMenuBar* menus, ElaToolButton* back,
                              ElaToolButton* forward, ElaToolButton* quickOpen, ElaToolButton* navigation,
                              ElaToolButton* panel)
{
    new WorkbenchTitlebar(window, menus, back, forward, quickOpen, navigation, panel);
}
}
