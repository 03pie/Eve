/** @file workbench.cpp
 * @brief 以 Ela 原生控件实现 VS Code 分区中的命令中心、菜单、设置与有界稳定键导航。
 */
#include "workbench_p.hpp"

#include "home_p.hpp"
#include <ElaComboBox.h>
#include <ElaMenu.h>
#include <ElaMenuBar.h>
#include <ElaScrollPage.h>
#include <ElaScrollPageArea.h>
#include <ElaSuggestBox.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToggleSwitch.h>
#include <ElaToolButton.h>
#include <ElaWindow.h>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QHBoxLayout>
#include <QMap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace cordis::ui::detail {
namespace {
/** @brief Example 风格的带留白滚动页，避免使用固定高度挤压缩放后的内容。 */
ElaScrollPage* scrollPage(const QString& title, QWidget* parent = nullptr)
{
    auto page = new ElaScrollPage(parent);
    page->setWindowTitle(title);
    page->setPageTitlePixelSize(26);
    page->setContentsMargins(20, 5, 15, 10);
    return page;
}
/** @brief 多行卡片解除 Ela 默认单行设置项的 75px 固定高度，由内容布局决定高度。 */
QVBoxLayout* card(QVBoxLayout* layout, const QString& title, const QString& description)
{
    auto area = new ElaScrollPageArea;
    area->setMinimumHeight(0);
    area->setMaximumHeight(QWIDGETSIZE_MAX);
    area->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto body = new QVBoxLayout(area);
    body->setContentsMargins(20, 16, 20, 16);
    body->setSpacing(10);
    auto heading = new ElaText(title, 18, area);
    heading->setIsBold(true);
    body->addWidget(heading);
    auto text = new ElaText(description, 14, area);
    text->setWordWrap(true);
    text->setTextFormat(Qt::PlainText);
    body->addWidget(text);
    layout->addWidget(area);
    return body;
}
/** @brief 设置开关反映面板自己的显隐属性，窗口最小化或暂时隐藏不改变用户布局选择。 */
class VisibilityBinding final : public QObject {
public:
    /** @brief 开关归设置页所有，面板只借用到根窗口销毁。 */
    VisibilityBinding(QWidget* target, ElaToggleSwitch* toggle) : QObject(toggle), target_(target), toggle_(toggle)
    {
        target->installEventFilter(this);
        toggle->setIsToggled(!target->isHidden());
    }

protected:
    /** @brief 只同步值，不触发重复布局操作。 */
    bool eventFilter(QObject*, QEvent* event) override
    {
        if ((event->type() == QEvent::Show || event->type() == QEvent::Hide) && target_ && toggle_) {
            QSignalBlocker blocked(toggle_);
            toggle_->setIsToggled(!target_->isHidden());
        }
        return false;
    }

private:
    QPointer<QWidget> target_;
    QPointer<ElaToggleSwitch> toggle_;
};
} // namespace

/** @brief 搜索及历史只保存宿主 key/文本，不保留插件 QWidget 或页面工厂。 */
struct WorkbenchControl : std::enable_shared_from_this<WorkbenchControl> {
    /** @brief 建议键属于 ElaSuggestBox，导航键属于 ElaWindow，撤销时分别删除。 */
    struct Entry {
        QString key, title, suggestion, plugin;
    };
    /** @brief 单次重放标记只消费对应目标，嵌套的其他导航仍可形成新分支。 */
    struct Replay {
        QString key;
        std::size_t index;
    };
    QPointer<ElaWindow> window;
    QPointer<QTabBar> tabs;
    QPointer<QTabWidget> panel;
    QPointer<QPlainTextEdit> output;
    QPointer<QToolButton> togglePanel;
    QPointer<ElaSuggestBox> search;
    QPointer<ElaToolButton> back, forward;
    QPointer<QAction> backAction, forwardAction;
    QPointer<ElaMenuBar> menuBar;
    QPointer<QWidget> settings, about;
    QPointer<ElaText> location;
    QPointer<QTimer> homeTimer;
    std::vector<QPointer<QAction>> actions;
    std::vector<Entry> entries;
    QMap<QString, QString> lastPluginPages;
    std::vector<QString> history;
    std::size_t cursor = 0;
    std::optional<Replay> replay;
    QString homeKey, managementKey, current;
    WorkbenchActions callbacks;
    bool open = true;
    bool refreshingHome = false;
    /** @brief 拷贝访问桥后再调用，允许操作过程中重入关闭并清空原回调。 */
    void withWindowAccess(const std::function<void()>& operation)
    {
        if (!open || !callbacks.withWindowAccess)
            return;
        auto access = callbacks.withWindowAccess;
        access(operation);
    }
    /** @brief 已关闭或已撤销的键都不能再通过搜索/历史进入。 */
    bool contains(const QString& key) const
    {
        return std::any_of(entries.begin(), entries.end(), [&](const Entry& item) { return item.key == key; });
    }
    /** @brief 不在回调返回后借用其闭包，允许 Shell 关闭时清空动作。 */
    void navigate(const QString& key, bool rememberPlugin = true)
    {
        if (!open)
            return;
        if (key.isEmpty()) {
            if (callbacks.empty) {
                auto empty = callbacks.empty;
                empty();
            }
            return;
        }
        if (!contains(key) || !callbacks.navigate)
            return;
        auto callback = callbacks.navigate;
        callback(key, rememberPlugin);
    }
    /** @brief UI 状态由真实历史游标决定，未激活按钮不会接受快捷键。 */
    void updateHistory()
    {
        const bool canBack = open && !history.empty() && (current.isEmpty() || cursor > 0);
        const bool canForward = open && !current.isEmpty() && !history.empty() && cursor + 1 < history.size();
        if (back)
            back->setEnabled(canBack);
        if (forward)
            forward->setEnabled(canForward);
        if (backAction)
            backAction->setEnabled(canBack);
        if (forwardAction)
            forwardAction->setEnabled(canForward);
    }
    /** @brief 稳定键重放不经过全局 Ela 撤销域，因此不同窗口的历史互不影响。 */
    void traverse(int direction)
    {
        if (!open || history.empty() ||
            (direction < 0 ? (!current.isEmpty() && cursor == 0) : (current.isEmpty() || cursor + 1 >= history.size())))
            return;
        const auto index = direction < 0 ? (current.isEmpty() ? cursor : cursor - 1) : cursor + 1;
        const auto key = history[index];
        replay = Replay{key, index};
        navigate(key);
        if (replay && replay->key == key)
            replay.reset();
    }
    /** @brief 只记录有效页面；空画布不是页面，回退可恢复最近关闭的视图。 */
    void record(const QString& key)
    {
        current = key;
        if (replay && replay->key == key && replay->index < history.size() && history[replay->index] == key) {
            cursor = replay->index;
            replay.reset();
        } else if (history.empty() || history[cursor] != key) {
            if (!history.empty())
                history.resize(cursor + 1);
            history.push_back(key);
            if (history.size() > 128)
                history.erase(history.begin());
            cursor = history.size() - 1;
        }
        updateHistory();
    }
    /** @brief 卡片动态合并管理快照与尚未绑定 Kernel 的贡献，每个插件实例显示一张。 */
    void refreshHome()
    {
        if (!open || refreshingHome)
            return;
        auto self = shared_from_this();
        withWindowAccess([self] {
            if (!self->open || self->refreshingHome)
                return;
            QScopedValueRollback<bool> refreshing(self->refreshingHome, true);
            self->refreshHomeWithinAccess();
        });
    }
    /** @brief 仅在窗口访问屏障内合并快照并写入卡片；嵌套刷新由外层本次更新收敛。 */
    void refreshHomeWithinAccess()
    {
        if (!open || !window)
            return;
        auto page = dynamic_cast<HomePage*>(window->findChild<QWidget*>("CordisWelcomePage"));
        if (!page)
            return;
        auto plugins = callbacks.plugins ? callbacks.plugins() : std::vector<HomePlugin>{};
        QSet<QString> ids;
        for (const auto& plugin : plugins)
            ids.insert(plugin.id);
        for (const auto& entry : entries) {
            if (entry.plugin.isEmpty() || ids.contains(entry.plugin))
                continue;
            ids.insert(entry.plugin);
            auto last = std::find_if(entries.begin(), entries.end(), [&](const Entry& value) {
                return value.key == lastPluginPages.value(entry.plugin);
            });
            const auto& target = last == entries.end() ? entry : *last;
            plugins.push_back({entry.plugin, target.key, target.title, QString::fromUtf8("界面插件"),
                               QString::fromUtf8("点击打开插件，关闭页签后状态仍会保留。")});
        }
        std::weak_ptr<WorkbenchControl> weak = shared_from_this();
        page->setPlugins(plugins, [weak](const QString& key) {
            if (auto state = weak.lock())
                state->navigate(key);
        });
    }
    /** @brief 关闭页签仅关闭视图；再次从左侧打开时复用原页面和业务状态。 */
    void closeTab(int index)
    {
        if (!open || !tabs || index < 0 || index >= tabs->count())
            return;
        const auto key = tabs->tabData(index).toString();
        if (key == current) {
            std::vector<QString> alternatives;
            for (int i = index - 1; i >= 0; --i)
                alternatives.push_back(tabs->tabData(i).toString());
            for (int i = index + 1; i < tabs->count(); ++i)
                alternatives.push_back(tabs->tabData(i).toString());
            for (const auto& candidate : alternatives) {
                navigate(candidate, false);
                if (!open)
                    return;
                if (current != key)
                    break;
            }
            if (current == key)
                navigate({});
            if (current == key)
                return;
        }
        if (!open || !tabs)
            return;
        QSignalBlocker blocked(tabs);
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabData(i).toString() == key) {
                tabs->removeTab(i);
                break;
            }
    }
    /** @brief 菜单命令和快捷键使用同一个开放门禁；根窗口自动销毁 QAction。 */
    QAction* action(ElaMenu* menu, const QString& text, const char* name, const QKeySequence& shortcut,
                    std::function<void(WorkbenchControl&)> callback)
    {
        auto item = menu->addAction(text);
        item->setObjectName(QString::fromLatin1(name));
        item->setShortcut(shortcut);
        if (!shortcut.isEmpty())
            window->addAction(item);
        actions.emplace_back(item);
        std::weak_ptr<WorkbenchControl> weak = shared_from_this();
        QObject::connect(item, &QAction::triggered, window, [weak, callback = std::move(callback)] {
            if (auto self = weak.lock(); self && self->open)
                self->withWindowAccess([self, callback] {
                    if (self->open)
                        callback(*self);
                });
        });
        return item;
    }
    /** @brief 设置页复用 Example 的下拉框、开关和分区卡片，各项均连接真实工作台状态。 */
    void createSettings()
    {
        auto page = scrollPage(QString::fromUtf8("设置"), window);
        settings = page;
        page->setObjectName("CordisSettingsPage");
        auto content = new QWidget;
        auto layout = new QVBoxLayout(content);
        layout->setSpacing(16);
        layout->setSizeConstraint(QLayout::SetMinimumSize);
        auto appearance =
            card(layout, QString::fromUtf8("外观"), QString::fromUtf8("使用 Ela 主题与组件，调整工作台显示方式。"));
        auto theme = new ElaComboBox(content);
        theme->setObjectName("CordisSettingsTheme");
        theme->addItem(QString::fromUtf8("浅色"));
        theme->addItem(QString::fromUtf8("深色"));
        theme->setCurrentIndex(eTheme->getThemeMode() == ElaThemeType::Dark ? 1 : 0);
        appearance->addWidget(theme);
        std::weak_ptr<WorkbenchControl> weak = shared_from_this();
        QObject::connect(theme, QOverload<int>::of(&QComboBox::currentIndexChanged), page, [weak](int index) {
            if (auto self = weak.lock(); self && self->open)
                eTheme->setThemeMode(index == 1 ? ElaThemeType::Dark : ElaThemeType::Light);
        });
        QObject::connect(eTheme, &ElaTheme::themeModeChanged, theme, [theme](ElaThemeType::ThemeMode mode) {
            QSignalBlocker blocked(theme);
            theme->setCurrentIndex(mode == ElaThemeType::Dark ? 1 : 0);
        });
        auto panels = card(layout, QString::fromUtf8("工作区布局"),
                           QString::fromUtf8("左侧插件入口打开中央页面；资源树等内部布局由具体插件页面提供。"));
        auto row = new QHBoxLayout;
        row->addWidget(new ElaText(QString::fromUtf8("显示底部工作区"), 14, content));
        row->addStretch();
        auto toggle = new ElaToggleSwitch(content);
        toggle->setObjectName("CordisSettingsPanel");
        row->addWidget(toggle);
        panels->addLayout(row);
        new VisibilityBinding(panel, toggle);
        QObject::connect(toggle, &ElaToggleSwitch::toggled, page, [weak](bool checked) {
            if (auto self = weak.lock(); self && self->open && self->panel)
                self->panel->setVisible(checked);
        });
        auto shortcuts =
            card(layout, QString::fromUtf8("快捷键"),
                 QString::fromUtf8("Alt+← / Alt+→  前进后退\nCtrl+P  搜索页面\nCtrl+Shift+P  打开命令面板\nCtrl+B / "
                                   "Ctrl+J  展开插件导航 / 切换底部工作区\nCtrl+W  关闭当前页签"));
        Q_UNUSED(shortcuts);
        layout->addStretch();
        content->setWindowTitle(page->windowTitle());
        page->addCentralWidget(content);
        auto info = scrollPage(QString::fromUtf8("关于 Cordis"), window);
        about = info;
        info->setObjectName("CordisAboutPage");
        auto infoContent = new QWidget;
        auto infoLayout = new QVBoxLayout(infoContent);
        infoLayout->setSizeConstraint(QLayout::SetMinimumSize);
        card(infoLayout, QString::fromUtf8("Cordis 插件平台"),
             QString::fromUtf8(
                 "C++17 插件工作台\n界面基于项目内维护的 ElaWidgetTools，布局采用插件活动栏、中央页面与底部工作区。"));
        card(infoLayout, QString::fromUtf8("运行环境"),
             QString::fromUtf8(
                 "Qt %1\nElaWidgetTools 2.0.0\n参考 ElaWidgetToolsExample 的原生菜单、导航、搜索和设置交互。")
                 .arg(QString::fromLatin1(qVersion())));
        infoLayout->addStretch();
        infoContent->setWindowTitle(info->windowTitle());
        info->addCentralWidget(infoContent);
    }
};

QWidget* createWelcomePage()
{
    return new HomePage;
}

std::shared_ptr<WorkbenchControl> createWorkbench(ElaWindow* window, QTabBar* tabs, QTabWidget* panel,
                                                  QPlainTextEdit* output, QToolButton* togglePanel,
                                                  const QString& homeKey, const QString& managementKey,
                                                  WorkbenchActions callbacks)
{
    auto self = std::make_shared<WorkbenchControl>();
    self->window = window;
    self->tabs = tabs;
    self->panel = panel;
    self->output = output;
    self->togglePanel = togglePanel;
    self->homeKey = homeKey;
    self->managementKey = managementKey;
    self->callbacks = std::move(callbacks);
    std::weak_ptr<WorkbenchControl> weak = self;
    auto bar = new ElaMenuBar(window);
    self->menuBar = bar;
    bar->setObjectName("CordisMenuBar");
    bar->setFixedWidth(250);
    bar->setFixedHeight(30);
    auto file = bar->addMenu(QString::fromUtf8("文件(&F)"));
    auto view = bar->addMenu(QString::fromUtf8("视图(&V)"));
    auto go = bar->addMenu(QString::fromUtf8("转到(&G)"));
    auto help = bar->addMenu(QString::fromUtf8("帮助(&H)"));
    window->setCustomWidget(ElaAppBarType::LeftArea, bar);
    auto center = new QWidget(window);
    center->setObjectName("CordisCommandCenter");
    auto centerLayout = new QHBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(3);
    auto back = new ElaToolButton(center);
    self->back = back;
    back->setObjectName("CordisNavigateBack");
    back->setElaIcon(ElaIconType::ArrowLeft);
    back->setToolTip(QString::fromUtf8("后退（Alt+←）"));
    back->setFixedSize(32, 30);
    back->setShortcut(QKeySequence("Alt+Left"));
    auto forward = new ElaToolButton(center);
    self->forward = forward;
    forward->setObjectName("CordisNavigateForward");
    forward->setElaIcon(ElaIconType::ArrowRight);
    forward->setToolTip(QString::fromUtf8("前进（Alt+→）"));
    forward->setFixedSize(32, 30);
    forward->setShortcut(QKeySequence("Alt+Right"));
    auto search = new ElaSuggestBox(center);
    self->search = search;
    search->setObjectName("CordisPageSearch");
    search->setFixedSize(300, 30);
    search->setPlaceholderText(QString::fromUtf8("搜索页面  Ctrl+P"));
    centerLayout->addWidget(back);
    centerLayout->addWidget(forward);
    centerLayout->addWidget(search);
    window->setCustomWidget(ElaAppBarType::MiddleArea, center);
    QObject::connect(back, &QToolButton::clicked, window, [weak] {
        if (auto control = weak.lock())
            control->traverse(-1);
    });
    QObject::connect(forward, &QToolButton::clicked, window, [weak] {
        if (auto control = weak.lock())
            control->traverse(1);
    });
    QObject::connect(search, &ElaSuggestBox::suggestionClicked, window, [weak](const ElaSuggestBox::SuggestData& data) {
        if (auto control = weak.lock())
            control->navigate(data.getSuggestData().value("CordisPageKey").toString());
    });
    tabs->setTabsClosable(true);
    QObject::connect(tabs, &QTabBar::tabCloseRequested, window, [weak](int index) {
        if (auto control = weak.lock())
            control->closeTab(index);
    });
    self->action(file, QString::fromUtf8("关闭当前页签"), "CordisCloseView", QKeySequence("Ctrl+W"),
                 [](WorkbenchControl& state) {
                     if (state.tabs)
                         state.closeTab(state.tabs->currentIndex());
                 });
    self->action(file, QString::fromUtf8("设置"), "CordisOpenSettings", QKeySequence("Ctrl+,"),
                 [](WorkbenchControl& state) {
                     if (state.settings)
                         state.navigate(state.settings->property("ElaPageKey").toString());
                 });
    file->addSeparator();
    self->action(file, QString::fromUtf8("关闭窗口"), "CordisCloseWindow", {}, [](WorkbenchControl& state) {
        if (state.window)
            state.window->close();
    });
    self->action(view, QString::fromUtf8("展开 / 收起插件导航"), "CordisViewNavigation", QKeySequence("Ctrl+B"),
                 [](WorkbenchControl& state) {
                     if (state.window)
                         state.window->setNavigationBarDisplayMode(state.window->getNavigationBarDisplayMode() ==
                                                                           ElaNavigationType::Maximal
                                                                       ? ElaNavigationType::Compact
                                                                       : ElaNavigationType::Maximal);
                 });
    self->action(view, QString::fromUtf8("显示 / 隐藏底部工作区"), "CordisViewPanel", {}, [](WorkbenchControl& state) {
        if (state.togglePanel)
            state.togglePanel->click();
    });
    self->action(view, QString::fromUtf8("打开命令面板"), "CordisOpenCommands", QKeySequence("Ctrl+Shift+P"),
                 [](WorkbenchControl& state) {
                     if (state.panel) {
                         state.panel->show();
                         state.panel->setCurrentIndex(0);
                         if (auto widget = state.panel->currentWidget())
                             widget->setFocus();
                     }
                 });
    self->action(view, QString::fromUtf8("清空输出"), "CordisClearOutput", {}, [](WorkbenchControl& state) {
        if (state.output)
            state.output->clear();
    });
    self->action(view, QString::fromUtf8("复制全部输出"), "CordisCopyOutput", {}, [](WorkbenchControl& state) {
        if (state.output)
            QApplication::clipboard()->setText(state.output->toPlainText());
    });
    view->addSeparator();
    self->action(view, QString::fromUtf8("切换明暗主题"), "CordisToggleTheme", {}, [](WorkbenchControl&) {
        eTheme->setThemeMode(eTheme->getThemeMode() == ElaThemeType::Light ? ElaThemeType::Dark : ElaThemeType::Light);
    });
    self->action(view, QString::fromUtf8("刷新插件状态"), "CordisRefreshPlugins", QKeySequence("F5"),
                 [](WorkbenchControl& state) {
                     if (state.callbacks.refresh) {
                         auto refresh = state.callbacks.refresh;
                         refresh();
                     }
                 });
    self->backAction = self->action(go, QString::fromUtf8("后退"), "CordisMenuBack", {},
                                    [](WorkbenchControl& state) { state.traverse(-1); });
    self->forwardAction = self->action(go, QString::fromUtf8("前进"), "CordisMenuForward", {},
                                       [](WorkbenchControl& state) { state.traverse(1); });
    self->action(go, QString::fromUtf8("搜索页面"), "CordisQuickOpen", QKeySequence("Ctrl+P"),
                 [](WorkbenchControl& state) {
                     if (state.search)
                         state.search->focusSearch();
                 });
    self->action(go, QString::fromUtf8("插件管理"), "CordisOpenPlugins", {},
                 [](WorkbenchControl& state) { state.navigate(state.managementKey); });
    self->action(go, QString::fromUtf8("欢迎页"), "CordisOpenHome", {},
                 [](WorkbenchControl& state) { state.navigate(state.homeKey); });
    self->action(help, QString::fromUtf8("关于 Cordis"), "CordisOpenAbout", {}, [](WorkbenchControl& state) {
        if (state.about)
            state.navigate(state.about->property("ElaPageKey").toString());
    });
    self->createSettings();
    for (const auto& entry : {std::make_pair("CordisWelcomePlugins", "CordisOpenPlugins"),
                              std::make_pair("CordisWelcomeSettings", "CordisOpenSettings"),
                              std::make_pair("CordisWelcomeCommands", "CordisOpenCommands")}) {
        auto button = window->findChild<QAbstractButton*>(QString::fromLatin1(entry.first));
        auto action = window->findChild<QAction*>(QString::fromLatin1(entry.second));
        if (button && action)
            QObject::connect(button, &QAbstractButton::clicked, action, &QAction::trigger);
    }
    auto corner = new ElaToolButton(panel);
    corner->setObjectName("CordisHidePanel");
    corner->setElaIcon(ElaIconType::Xmark);
    corner->setToolTip(QString::fromUtf8("隐藏工作区（Ctrl+J）"));
    panel->setCornerWidget(corner, Qt::TopRightCorner);
    QObject::connect(corner, &QToolButton::clicked, window, [weak] {
        if (auto control = weak.lock(); control && control->open && control->togglePanel)
            control->togglePanel->click();
    });
    auto location = new ElaText(QString::fromUtf8("工作台"), 12, window);
    self->location = location;
    location->setTextFormat(Qt::PlainText);
    location->setObjectName("CordisCurrentPage");
    location->setWordWrap(false);
    if (window->statusBar())
        window->statusBar()->addPermanentWidget(location);
    auto timer = new QTimer(window);
    self->homeTimer = timer;
    timer->setInterval(500);
    QObject::connect(timer, &QTimer::timeout, window, [weak] {
        if (auto control = weak.lock())
            control->refreshHome();
    });
    timer->start();
    self->updateHistory();
    return self;
}

QWidget* workbenchSettings(const std::shared_ptr<WorkbenchControl>& control)
{
    return control->settings;
}
QWidget* workbenchAbout(const std::shared_ptr<WorkbenchControl>& control)
{
    return control->about;
}

void registerWorkbenchPage(const std::shared_ptr<WorkbenchControl>& control, const QString& key, const QString& title,
                           const QString& category, const QString& plugin)
{
    if (!control || !control->open || key.isEmpty() || control->contains(key) || !control->search)
        return;
    const auto label = category.isEmpty() ? title : title + QString::fromUtf8(" · ") + category;
    const auto suggestion = control->search->addSuggestion(label, {{"CordisPageKey", key}});
    control->entries.push_back({key, title, suggestion, plugin});
    control->refreshHome();
    if (control->open && control->search)
        control->search->setSearchText(control->search->getSearchText());
}

void selectWorkbenchPage(const std::shared_ptr<WorkbenchControl>& control, const QString& key, bool rememberPlugin)
{
    if (!control || !control->open || !control->tabs || !control->contains(key))
        return;
    control->current = key;
    auto entry = std::find_if(control->entries.begin(), control->entries.end(),
                              [&](const WorkbenchControl::Entry& item) { return item.key == key; });
    const auto title = entry->title;
    if (rememberPlugin && !entry->plugin.isEmpty())
        control->lastPluginPages.insert(entry->plugin, key);
    {
        QSignalBlocker blocked(control->tabs);
        int index = 0;
        while (index < control->tabs->count() && control->tabs->tabData(index).toString() != key)
            ++index;
        if (index == control->tabs->count()) {
            index = control->tabs->addTab(title);
            control->tabs->setTabData(index, key);
        }
        control->tabs->setCurrentIndex(index);
    }
    control->record(key);
    if (key == control->homeKey)
        control->refreshHome();
    if (control->location)
        control->location->setText(title);
}

void selectWorkbenchEmpty(const std::shared_ptr<WorkbenchControl>& control)
{
    if (!control || !control->open)
        return;
    control->current.clear();
    control->updateHistory();
    if (control->location)
        control->location->setText(QString::fromUtf8("未打开页面"));
}

void removeWorkbenchPage(const std::shared_ptr<WorkbenchControl>& control, const QString& key)
{
    if (!control)
        return;
    auto entry = std::find_if(control->entries.begin(), control->entries.end(),
                              [&](const WorkbenchControl::Entry& item) { return item.key == key; });
    if (entry == control->entries.end())
        return;
    const auto suggestion = entry->suggestion;
    const auto plugin = entry->plugin;
    if (control->open && control->tabs) {
        for (int i = 0; i < control->tabs->count(); ++i)
            if (control->tabs->tabData(i).toString() == key) {
                control->closeTab(i);
                break;
            }
    }
    entry = std::find_if(control->entries.begin(), control->entries.end(),
                         [&](const WorkbenchControl::Entry& item) { return item.key == key; });
    if (entry != control->entries.end())
        control->entries.erase(entry);
    if (!plugin.isEmpty() && std::none_of(control->entries.begin(), control->entries.end(),
                                          [&](const WorkbenchControl::Entry& item) { return item.plugin == plugin; }))
        control->lastPluginPages.remove(plugin);
    const auto oldCursor = control->cursor;
    std::size_t before = 0;
    for (std::size_t i = 0; i < control->history.size(); ++i)
        if (i < oldCursor && control->history[i] == key)
            ++before;
    control->history.erase(std::remove(control->history.begin(), control->history.end(), key), control->history.end());
    control->cursor = control->history.empty() ? 0 : std::min(oldCursor - before, control->history.size() - 1);
    control->replay.reset();
    if (!control->open)
        return;
    if (control->search)
        control->search->removeSuggestion(suggestion);
    if (control->open && control->search)
        control->search->setSearchText(control->search->getSearchText());
    if (control->current == key)
        control->navigate({});
    control->refreshHome();
    control->updateHistory();
}

void closeWorkbench(const std::shared_ptr<WorkbenchControl>& control)
{
    if (!control || !control->open)
        return;
    control->open = false;
    control->callbacks = {};
    control->replay.reset();
    for (const auto& action : control->actions)
        if (action)
            action->setEnabled(false);
    if (control->menuBar)
        control->menuBar->setEnabled(false);
    if (control->search) {
        control->search->setSearchText({});
        control->search->setEnabled(false);
    }
    if (control->homeTimer)
        control->homeTimer->stop();
    if (control->window)
        if (auto page = control->window->findChild<QWidget*>("CordisWelcomePage"))
            page->setEnabled(false);
    if (control->settings)
        control->settings->setEnabled(false);
    if (control->about)
        control->about->setEnabled(false);
    if (control->togglePanel)
        control->togglePanel->setEnabled(false);
    control->updateHistory();
}
} // namespace cordis::ui::detail
