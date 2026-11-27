/** @file workbench.cpp
 * @brief 以 Ela 原生控件实现 VS Code 分区中的命令中心、菜单、设置与有界稳定键导航。
 */
#include "workbench_p.hpp"
#include "panel_p.hpp"

#include "appearance_p.hpp"
#include "home_p.hpp"
#include "preferences_p.hpp"
#include "settings_page_p.hpp"
#include "extensions_page_p.hpp"
#include <ElaPushButton.h>
#include <eve/build_info.hpp>
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaMenu.h>
#include <ElaMenuBar.h>
#include <ElaScrollPage.h>
#include <ElaScrollPageArea.h>
#include <ElaSpinBox.h>
#include <ElaSuggestBox.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToggleSwitch.h>
#include <ElaToolButton.h>
#include <ElaWindow.h>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QEvent>
#include <QHBoxLayout>
#include <QHash>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
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

namespace eve::ui::detail {
namespace {
// QTabWidget calculates the corner position from sizeHint(), including while
// secondary actions are hidden. Keep its size and hit targets in agreement.
class PanelTools final : public QWidget {
public:
    using QWidget::QWidget;
    QSize sizeHint() const override { return QSize(92,32); }
    QSize minimumSizeHint() const override { return sizeHint(); }
};

class StatusBellButton final : public ElaToolButton {
public:
    using ElaToolButton::ElaToolButton;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        ElaToolButton::paintEvent(event);
        if (!hasFocus())
            return;
        QPainter painter(this);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::BasicText), 1, Qt::DotLine));
        painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 3, 3);
    }
};

/** @brief 窄窗口缩短搜索框，给菜单和系统窗口按钮保留空间，快捷键入口始终可用。 */
class CommandCenter final : public QWidget {
public:
    /** @brief 只观察宿主尺寸，过滤器随窗口内的命令中心一起释放。 */
    explicit CommandCenter(QWidget* host) : QWidget(host) { host->installEventFilter(this); }
    /** @brief 绑定 Ela 搜索框，不接管其建议模型和键盘交互。 */
    void setSearch(ElaSuggestBox* search)
    {
        search_ = search;
        updateWidth();
    }

protected:
    /** @brief 窗口缩放只调整几何，不改变用户输入或建议列表。 */
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Resize)
            updateWidth();
        return QWidget::eventFilter(watched, event);
    }

private:
    /** @brief 窄屏将有菜单入口的辅助按钮折叠，保留搜索、命令及窗口控制。 */
    void updateWidth()
    {
        if (!search_)
            return;
        auto host = qobject_cast<ElaWindow*>(window());
        if (!host)
            return;
        const int width = host->width();
        const bool compact = width < 980;
        for (const auto id : {"EveNavigateBack", "EveNavigateForward"})
            if (auto button = findChild<QToolButton*>(id))
                button->setVisible(!compact);
        const bool showWindowTools = width >= 790;
        for (auto flag : {ElaAppBarType::StayTopButtonHint, ElaAppBarType::ThemeChangeButtonHint})
            if (host->getWindowButtonFlags().testFlag(flag) != showWindowTools)
                host->setWindowButtonFlag(flag, showWindowTools);
        search_->setFixedWidth(std::clamp(width - (compact ? 690 : 835), 72, 360));
    }
    QPointer<ElaSuggestBox> search_;
};
/** @brief Example 风格的带留白滚动页，避免使用固定高度挤压缩放后的内容。 */
ElaScrollPage* scrollPage(const QString& title, QWidget* parent = nullptr)
{
    auto page = new ElaScrollPage(parent);
    page->setWindowTitle(title);
    page->setPageTitlePixelSize(24);
    page->setContentsMargins(20, 12, 20, 16);
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
    secondaryText(text);
    body->addWidget(text);
    layout->addWidget(area);
    return body;
}
} // namespace

/** @brief 搜索及历史只保存宿主 key/文本，不保留插件 QWidget 或页面工厂。 */
struct WorkbenchControl : std::enable_shared_from_this<WorkbenchControl> {
    /** @brief 建议键属于 ElaSuggestBox，导航键属于 ElaWindow，撤销时分别删除。 */
    struct Entry {
        QString key, title, suggestion, plugin;
        std::function<bool()> presentation;
        bool presentable() const { return !presentation || presentation(); }
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
    std::shared_ptr<OutputControl> logs;
    QPointer<QToolButton> togglePanel;
    QPointer<ElaSuggestBox> search;
    QPointer<ElaToolButton> back, forward;
    QPointer<QAction> backAction, forwardAction;
    QPointer<QAction> addPluginAction, configurationFileAction, modulesAction;
    QPointer<ElaMenuBar> menuBar;
    QPointer<ElaMenu> fileMenu, viewMenu;
    QPointer<QWidget> settings, about;
    QPointer<ElaText> location;
    QPointer<QTimer> homeTimer;
    QPointer<WorkbenchPreferences> preferences;
    QPointer<ElaMenu> tabContextMenu;
    std::uint64_t tabMenuGeneration = 0;
    std::uint64_t nextTabVersion = 0;
    QHash<QString, std::uint64_t> tabVersions;
    /** @brief 每次重开页签都有新代次，旧菜单不能操作重开的同名视图。 */
    struct TabReference {
        QString key;
        std::uint64_t version;
    };
    std::vector<QPointer<QAction>> actions;
    std::vector<Entry> entries;
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
        return std::any_of(entries.begin(), entries.end(),
                           [&](const Entry& item) { return item.key == key && item.presentable(); });
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
    /** @brief 历史保留稳定 key，跳过已经不允许呈现的记录。 */
    std::optional<std::size_t> historyTarget(int direction) const
    {
        if (history.empty() || (direction > 0 && current.isEmpty()))
            return {};
        auto at = static_cast<std::ptrdiff_t>(cursor);
        if (direction > 0 || !current.isEmpty())
            at += direction;
        while (at >= 0 && static_cast<std::size_t>(at) < history.size()) {
            if (contains(history[static_cast<std::size_t>(at)]))
                return static_cast<std::size_t>(at);
            at += direction;
        }
        return {};
    }
    /** @brief UI 状态由同一个可进入历史目标决定，未激活按钮不会接受快捷键。 */
    void updateHistory()
    {
        const bool canBack = open && historyTarget(-1).has_value();
        const bool canForward = open && historyTarget(1).has_value();
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
        if (!open)
            return;
        const auto target = historyTarget(direction);
        if (!target)
            return;
        const auto index = *target;
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
    /** @brief 宿主模块入口只依赖 handler；晚绑定与定时刷新共用同一动作。 */
    void refreshModules()
    {
        if (!open || !modulesAction)
            return;
        const auto available = callbacks.hasModules;
        const bool enabled = available && available();
        if (open && modulesAction)
            modulesAction->setEnabled(enabled);
    }
    /** @brief 仅在窗口访问屏障内合并快照并写入卡片；嵌套刷新由外层本次更新收敛。 */
    void refreshHomeWithinAccess()
    {
        if (!open || !window)
            return;
        auto page = dynamic_cast<HomePage*>(window->findChild<QWidget*>("EveWelcomePage"));
        if (!page)
            return;
        if (configurationFileAction)
            configurationFileAction->setEnabled(callbacks.hasConfigurationFile && callbacks.hasConfigurationFile());
        if (!open)
            return;
        refreshModules();
        if (!open)
            return;
        // 配置回调可能关闭外壳或撤销页面；仅在它返回后读取当前页面目录。
        const auto readCatalogue = callbacks.plugins;
        auto catalogue = readCatalogue ? readCatalogue() : HomeCatalogue{};
        if (!open || !window)
            return;
        if (addPluginAction)
            addPluginAction->setEnabled(catalogue.canAddPlugin);
        if (!open || !window)
            return;
        auto& plugins = catalogue.plugins;
        QSet<QString> ids;
        for (const auto& plugin : plugins)
            ids.insert(plugin.id);
        for (const auto& entry : entries) {
            if (!catalogue.allowFallback || !entry.presentable() || entry.plugin.isEmpty() ||
                ids.contains(entry.plugin) || catalogue.hidden.contains(entry.plugin))
                continue;
            ids.insert(entry.plugin);
            HomePlugin value{entry.plugin, entry.title, QString::fromUtf8("界面已就绪")};
            value.enabled = true;
            // A contributed page alone does not provide a lifecycle snapshot.
            plugins.push_back(std::move(value));
        }
        page->setPlugins(plugins, callbacks.power, callbacks.withWindowAccess, callbacks.managePlugin);
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
                tabVersions.remove(key);
                break;
            }
    }
    /** @brief 按稳定键查找当前页签；菜单打开后行号可能因撤销而变化。 */
    int tabIndex(const QString& key) const
    {
        if (tabs)
            for (int index = 0; index < tabs->count(); ++index)
                if (tabs->tabData(index).toString() == key)
                    return index;
        return -1;
    }
    /** @brief 菜单只保存打开时的键快照，新插入或重新创建的页面不受旧菜单影响。 */
    void showTabMenu(int index, const QPoint& globalPosition)
    {
        if (!open || !tabs || index < 0 || index >= tabs->count())
            return;
        if (tabContextMenu)
            tabContextMenu->close();
        const auto generation = ++tabMenuGeneration;
        const auto anchorKey = tabs->tabData(index).toString();
        const TabReference anchor{anchorKey, tabVersions.value(anchorKey)};
        std::vector<TabReference> all, others, right;
        for (int row = 0; row < tabs->count(); ++row) {
            const auto key = tabs->tabData(row).toString();
            const TabReference reference{key, tabVersions.value(key)};
            all.push_back(reference);
            if (row != index)
                others.push_back(reference);
            if (row > index)
                right.push_back(reference);
        }
        auto menu = new ElaMenu(window);
        tabContextMenu = menu;
        menu->setObjectName("EveTabContextMenu");
        menu->setAttribute(Qt::WA_DeleteOnClose);
        const auto add = [&](const QString& text, const char* name, std::vector<TabReference> keys, bool keepAnchor) {
            auto item = menu->addAction(text);
            item->setObjectName(QString::fromLatin1(name));
            item->setEnabled(!keys.empty());
            std::weak_ptr<WorkbenchControl> weak = shared_from_this();
            QObject::connect(
                item, &QAction::triggered, window, [weak, anchor, keys = std::move(keys), keepAnchor, generation] {
                    if (auto self = weak.lock(); self && self->open && self->tabMenuGeneration == generation)
                        self->withWindowAccess([self, anchor, keys, keepAnchor] {
                            if (self->tabVersions.value(anchor.key) != anchor.version || self->tabIndex(anchor.key) < 0)
                                return;
                            if (keepAnchor) {
                                self->navigate(anchor.key, false);
                                if (!self->open || self->current != anchor.key)
                                    return;
                            }
                            for (auto it = keys.rbegin(); it != keys.rend() && self->open; ++it) {
                                if (self->tabVersions.value(it->key) != it->version)
                                    continue;
                                const auto row = self->tabIndex(it->key);
                                if (row >= 0)
                                    self->closeTab(row);
                            }
                        });
                });
        };
        add(QString::fromUtf8("关闭页签"), "EveTabClose", {anchor}, false);
        menu->addSeparator();
        add(QString::fromUtf8("关闭其他页签"), "EveTabCloseOthers", std::move(others), true);
        add(QString::fromUtf8("关闭右侧页签"), "EveTabCloseRight", std::move(right), false);
        add(QString::fromUtf8("关闭全部页签"), "EveTabCloseAll", std::move(all), false);
        menu->popup(globalPosition);
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
        std::weak_ptr<WorkbenchControl> weak = shared_from_this();
        settings = createSettingsPage(window, panel, preferences, [weak](const std::function<void()>& operation) {
            if (auto self = weak.lock()) self->withWindowAccess(operation);
        });
        auto info = scrollPage(QString::fromUtf8("关于 Eve"), window);
        about = info;
        info->setObjectName("EveAboutPage");
        auto infoContent = new QWidget;
        auto infoLayout = new QVBoxLayout(infoContent);
        infoLayout->setSizeConstraint(QLayout::SetMinimumSize);
        card(infoLayout, QString::fromUtf8("Eve 插件平台"),
             QString::fromUtf8(
                 "版本 %1\nC++17 插件工作台").arg(QString::fromStdString(std::string(buildInfo().version))));
        card(infoLayout, QString::fromUtf8("运行环境"),
             QString::fromUtf8(
                 "Qt %1\nElaWidgetTools 2.0.0")
                 .arg(QString::fromLatin1(qVersion())));
        infoLayout->addStretch();
        infoContent->setWindowTitle(info->windowTitle());
        info->addCentralWidget(infoContent);
    }

};

std::shared_ptr<WorkbenchControl> createWorkbench(ElaWindow* window, QTabBar* tabs, QTabWidget* panel,
                                                  QPlainTextEdit* output, QToolButton* togglePanel,
                                                  const QString& homeKey, const QString& managementKey,
                                                  WorkbenchActions callbacks, const QString& preferencesFile,
                                                  std::shared_ptr<OutputControl> logs)
{
    auto self = std::make_shared<WorkbenchControl>();
    self->window = window;
    self->tabs = tabs;
    self->panel = panel;
    self->output = output;
    self->logs = std::move(logs);
    self->togglePanel = togglePanel;
    self->homeKey = homeKey;
    self->managementKey = managementKey;
    self->callbacks = std::move(callbacks);
    self->preferences = new WorkbenchPreferences(window, panel, preferencesFile);
    std::weak_ptr<WorkbenchControl> weak = self;
    if (auto splitter = window->findChild<QSplitter*>("ElaWorkbenchPanelSplitter")) {
        // 原生 Windows 分隔条不随 Ela 调色板切换；显式使用当前主题背景和悬停强调色。
        auto handle = splitter->handle(1);
        const auto update = [weak, handle](ElaThemeType::ThemeMode mode) {
            if (auto state = weak.lock(); state && state->open)
                state->withWindowAccess([handle, mode] {
                    handle->setStyleSheet(QStringLiteral("QSplitterHandle{background:%1;}"
                                                         "QSplitterHandle:hover{background:%2;}")
                                              .arg(eTheme->getThemeColor(mode, ElaThemeType::WindowBase).name(),
                                                   eTheme->getThemeColor(mode, ElaThemeType::PrimaryNormal).name()));
                });
        };
        QObject::connect(eTheme, &ElaTheme::themeModeChanged, handle, update);
        update(eTheme->getThemeMode());
    }
    auto bar = new ElaMenuBar(window);
    self->menuBar = bar;
    bar->setObjectName("EveMenuBar");
    bar->setFixedHeight(30);
    auto file = bar->addMenu(QString::fromUtf8("文件(&F)"));
    auto view = bar->addMenu(QString::fromUtf8("视图(&V)"));
    auto go = bar->addMenu(QString::fromUtf8("转到(&G)"));
    auto help = bar->addMenu(QString::fromUtf8("帮助(&H)"));
    self->fileMenu = file;
    self->viewMenu = view;
    bar->setFixedWidth(bar->sizeHint().width());
    window->setCustomWidget(ElaAppBarType::LeftArea, bar);
    auto center = new CommandCenter(window);
    center->setObjectName("EveCommandCenter");
    auto centerLayout = new QHBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(4);
    auto back = new ElaToolButton(center);
    self->back = back;
    back->setObjectName("EveNavigateBack");
    back->setElaIcon(ElaIconType::ArrowLeft);
    back->setToolTip(QString::fromUtf8("后退（Alt+←）"));
    back->setAccessibleName(QString::fromUtf8("后退"));
    back->setFixedSize(32, 30);
    auto forward = new ElaToolButton(center);
    self->forward = forward;
    forward->setObjectName("EveNavigateForward");
    forward->setElaIcon(ElaIconType::ArrowRight);
    forward->setToolTip(QString::fromUtf8("前进（Alt+→）"));
    forward->setAccessibleName(QString::fromUtf8("前进"));
    forward->setFixedSize(32, 30);
    auto search = new ElaSuggestBox(center);
    self->search = search;
    search->setSuggestionFilter([weak](const ElaSuggestBox::SuggestData& data) {
        auto self = weak.lock();
        return self && self->open && self->contains(data.getSuggestData().value("EvePageKey").toString());
    });
    search->setObjectName("EvePageSearch");
    search->setFixedHeight(30);
    center->setSearch(search);
    search->setPlaceholderText(QString::fromUtf8("搜索页面  Ctrl+P"));
    centerLayout->addWidget(back);
    centerLayout->addWidget(forward);
    centerLayout->addWidget(search);
    auto paletteButton = new ElaToolButton(center);
    paletteButton->setObjectName("EveTitleCommands");
    paletteButton->setElaIcon(ElaIconType::Terminal);
    paletteButton->setAccessibleName(QString::fromUtf8("搜索命令"));
    paletteButton->setToolTip(QString::fromUtf8("搜索命令（Ctrl+Shift+P）"));
    paletteButton->setFixedSize(32, 30);
    centerLayout->addWidget(paletteButton);
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
            control->navigate(data.getSuggestData().value("EvePageKey").toString());
    });
    tabs->setTabsClosable(true);
    tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(tabs, &QWidget::customContextMenuRequested, window, [weak](const QPoint& position) {
        if (auto self = weak.lock(); self && self->open)
            self->withWindowAccess([self, position] {
                self->showTabMenu(self->tabs->tabAt(position), self->tabs->mapToGlobal(position));
            });
    });
    QObject::connect(tabs, &QTabBar::tabCloseRequested, window, [weak](int index) {
        if (auto control = weak.lock())
            control->closeTab(index);
    });
    self->action(file, QString::fromUtf8("关闭当前页签"), "EveCloseView", QKeySequence("Ctrl+W"),
                 [](WorkbenchControl& state) {
                     if (state.tabs)
                         state.closeTab(state.tabs->currentIndex());
                 });
    self->action(file, QString::fromUtf8("设置"), "EveOpenSettings", QKeySequence("Ctrl+,"),
                 [](WorkbenchControl& state) {
                     if (state.settings)
                         state.navigate(state.settings->property("ElaPageKey").toString());
                 });
    file->addSeparator();
    self->action(file, QString::fromUtf8("关闭窗口"), "EveCloseWindow", {}, [](WorkbenchControl& state) {
        if (state.window)
            state.window->close();
    });
    self->action(view, QString::fromUtf8("展开 / 收起插件导航"), "EveViewNavigation", QKeySequence("Ctrl+B"),
                 [](WorkbenchControl& state) {
                     if (state.window)
                         state.window->setNavigationBarDisplayMode(state.window->getNavigationBarDisplayMode() ==
                                                                           ElaNavigationType::Maximal
                                                                       ? ElaNavigationType::Compact
                                                                       : ElaNavigationType::Maximal);
                 });
    self->action(view, QString::fromUtf8("显示 / 隐藏底部工作区"), "EveViewPanel", {}, [](WorkbenchControl& state) {
        if (state.togglePanel)
            state.togglePanel->click();
    });
    auto openCommands = self->action(view, QString::fromUtf8("搜索命令"), "EveOpenCommands", QKeySequence("Ctrl+Shift+P"),
                 [](WorkbenchControl& state) { showCommandPanel(state.window, state.search); });
    QObject::connect(paletteButton, &QToolButton::clicked, openCommands, &QAction::trigger);
    self->action(view, QString::fromUtf8("输出"), "EveOpenOutput", {}, [](WorkbenchControl& state) {
        showOutput(state.logs, {}, {}, QApplication::focusWidget());
    });
    self->action(view, QString::fromUtf8("查看上次运行日志"), "EvePreviousOutput", {}, [](WorkbenchControl& state) {
        showPreviousOutput(state.logs, QApplication::focusWidget());
    });
    self->action(view, QString::fromUtf8("打开日志目录"), "EveOpenLogDirectory", {}, [](WorkbenchControl& state) {
        openOutputDirectory(state.logs);
    });
    self->action(view, QString::fromUtf8("清空输出"), "EveClearOutput", {}, [](WorkbenchControl& state) {
        if (state.logs)
            clearOutput(state.logs);
        else if (state.output)
            state.output->clear();
    });
    self->action(view, QString::fromUtf8("复制全部输出"), "EveCopyOutput", {}, [](WorkbenchControl& state) {
        if (state.logs)
            QApplication::clipboard()->setText(currentOutputText(state.logs));
        else if (state.output)
            QApplication::clipboard()->setText(state.output->toPlainText());
    });
    view->addSeparator();
    auto stayTop = self->action(view, QString::fromUtf8("窗口置顶"), "EveStayTop", {}, [](WorkbenchControl& state) {
        state.window->setIsStayTop(!state.window->getIsStayTop());
    });
    stayTop->setCheckable(true);
    stayTop->setChecked(window->getIsStayTop());
    QObject::connect(window, &ElaWindow::pIsStayTopChanged, stayTop,
                     [window, stayTop] { stayTop->setChecked(window->getIsStayTop()); });
    self->action(view, QString::fromUtf8("切换明暗主题"), "EveToggleTheme", {}, [](WorkbenchControl&) {
        eTheme->setThemeMode(eTheme->getThemeMode() == ElaThemeType::Light ? ElaThemeType::Dark : ElaThemeType::Light);
    });
    self->action(view, QString::fromUtf8("刷新插件状态"), "EveRefreshPlugins", QKeySequence("F5"),
                 [](WorkbenchControl& state) {
                     if (state.callbacks.refresh) {
                         auto refresh = state.callbacks.refresh;
                         refresh();
                     }
                 });
    self->backAction = self->action(go, QString::fromUtf8("后退"), "EveMenuBack", QKeySequence("Alt+Left"),
                                    [](WorkbenchControl& state) { state.traverse(-1); });
    self->forwardAction = self->action(go, QString::fromUtf8("前进"), "EveMenuForward", QKeySequence("Alt+Right"),
                                       [](WorkbenchControl& state) { state.traverse(1); });
    self->action(go, QString::fromUtf8("搜索页面"), "EveQuickOpen", QKeySequence("Ctrl+P"),
                 [](WorkbenchControl& state) {
                     if (state.search)
                         state.search->focusSearch();
                 });
    self->action(go, QString::fromUtf8("插件管理"), "EveOpenPlugins", {},
                 [](WorkbenchControl& state) {
                     if (state.callbacks.navigate)
                         state.callbacks.navigate(state.managementKey, false);
                 })
        ->setVisible(false);
    const auto platformAction = [self, file](const QString& text, const char* name, bool add) {
        auto action = self->action(file, text, name, {}, [add](WorkbenchControl& state) {
            const auto callback = add ? state.callbacks.addPlugin : state.callbacks.configurationFile;
            if (!callback)
                return;
            auto result = callback();
            if (!result.hasValue() && state.open && state.logs)
                (void)appendOutput(state.logs, {}, "platform", result.error().message, OutputLevel::Error);
        });
        action->setEnabled(false);
        action->setToolTip(add ? QString::fromUtf8("从已注册插件添加配置实例；需要工作台绑定配置会话。")
                               : QString::fromUtf8("打开平台配置文件窗口；此入口由宿主提供。"));
        if (add)
            self->addPluginAction = action;
        else
            self->configurationFileAction = action;
    };
    file->addSeparator();
    platformAction(QString::fromUtf8("添加插件实例…"), "EveAddPlugin", true);
    platformAction(QString::fromUtf8("工作区配置…"), "EveConfigurationFile", false);
    self->modulesAction =
        self->action(file, QString::fromUtf8("模块管理…"), "EveOpenModules", {}, [](WorkbenchControl& state) {
            const auto callback = state.callbacks.modules;
            if (!callback)
                return;
            const auto result = callback();
            if (!result.hasValue() && state.open && state.logs)
                (void)appendOutput(state.logs, {}, "platform", result.error().message, OutputLevel::Error);
        });
    self->modulesAction->setEnabled(false);
    self->modulesAction->setToolTip(QString::fromUtf8("打开宿主提供的模块管理窗口。"));
    self->action(go, QString::fromUtf8("欢迎页"), "EveOpenHome", {},
                 [](WorkbenchControl& state) { state.navigate(state.homeKey); });
    auto notifications =
        self->action(go, QString::fromUtf8("通知历史"), "EveOpenNotifications", {}, [](WorkbenchControl& state) {
            if (!state.open || !state.callbacks.showNotifications)
                return;
            const auto show = state.callbacks.showNotifications;
            const auto result = show();
            if (!result.hasValue() && state.open && state.logs)
                (void)appendOutput(state.logs, {}, "platform", result.error().message, OutputLevel::Error);
        });
    self->action(help, QString::fromUtf8("关于 Eve"), "EveOpenAbout", {}, [](WorkbenchControl& state) {
        if (state.about)
            state.navigate(state.about->property("ElaPageKey").toString());
    });
    self->createSettings();
    if (auto button = window->findChild<QAbstractButton*>("EveWelcomePlugins")) {
        auto action = self->addPluginAction.data();
        if (action) {
            QObject::connect(button, &QAbstractButton::clicked, action, &QAction::trigger);
            const auto sync = [button, action] {
                button->setEnabled(action->isEnabled());
                button->setToolTip(action->toolTip());
            };
            QObject::connect(action, &QAction::changed, button, sync);
            sync();
        }
    }
    auto panelTools = new PanelTools(panel);
    panelTools->setObjectName("EvePanelTools");
    auto toolsLayout = new QHBoxLayout(panelTools);
    toolsLayout->setContentsMargins(0, 0, 4, 0);
    toolsLayout->setSpacing(2);
    toolsLayout->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
    for (const auto& entry : {std::make_pair("EveCopyPanelOutput", "EveCopyOutput"),
                              std::make_pair("EveClearPanelOutput", "EveClearOutput")}) {
        auto button = new ElaToolButton(panelTools);
        const bool copy = QString::fromLatin1(entry.first) == "EveCopyPanelOutput";
        button->setObjectName(QString::fromLatin1(entry.first));
        button->setElaIcon(copy ? ElaIconType::Copy : ElaIconType::TrashCan);
        button->setFixedSize(28, 28);
        button->setAccessibleName(copy ? QString::fromUtf8("复制当前目标输出") : QString::fromUtf8("清空当前目标输出"));
        button->setToolTip(button->accessibleName());
        toolsLayout->addWidget(button);
        auto action = window->findChild<QAction*>(QString::fromLatin1(entry.second));
        QObject::connect(button, &QToolButton::clicked, action, &QAction::trigger);
        const auto sync = [weak, button](int) {
            if (auto self = weak.lock(); self && self->open && self->panel)
                self->withWindowAccess([self, button] {
                    button->setVisible(self->panel->currentWidget() == self->output && self->output && self->output->width()>=1080);
                    button->setEnabled(self->open && self->output);
                });
        };
        QObject::connect(panel, &QTabWidget::currentChanged, button, sync);
        sync(panel->currentIndex());
    }
    auto corner = new ElaToolButton(panelTools);
    corner->setObjectName("EveHidePanel");
    corner->setElaIcon(ElaIconType::Xmark);
    corner->setToolTip(QString::fromUtf8("隐藏工作区（Ctrl+J）"));
    corner->setAccessibleName(QString::fromUtf8("隐藏底部工作区"));
    corner->setFixedSize(28, 28);
    toolsLayout->addWidget(corner);
    panelTools->setFixedSize(panelTools->sizeHint());
    panel->setCornerWidget(panelTools, Qt::TopRightCorner);
    QObject::connect(corner, &QToolButton::clicked, window, [weak] {
        if (auto control = weak.lock(); control && control->open && control->togglePanel)
            control->togglePanel->click();
    });
    auto status = window->statusBar();
    auto contextStrip = new QWidget(status);
    auto contextLayout = new QHBoxLayout(contextStrip);
    contextLayout->setContentsMargins(8, 0, 0, 0);
    contextLayout->setSpacing(10);
    auto location = new StatusBarText(QStringLiteral("工作台"), contextStrip);
    self->location = location;
    location->setObjectName("EveCurrentPage");
    location->setMaximumWidth(220);
    contextLayout->addWidget(location, 0, Qt::AlignVCenter);
    auto historyButton = new StatusBellButton(contextStrip);
    historyButton->setObjectName("EveShowNotifications");
    historyButton->setElaIcon(ElaIconType::Bell);
    historyButton->setIconSize(QSize(19, 19));
    historyButton->setFont(location->font());
    historyButton->setIsTransparent(true);
    historyButton->setAccessibleName(QString::fromUtf8("通知中心"));
    historyButton->setToolTip(QString::fromUtf8("打开通知中心"));
    historyButton->setFocusPolicy(Qt::TabFocus);
    historyButton->setMinimumWidth(30);
    historyButton->setFixedHeight(24);
    QObject::connect(historyButton, &QToolButton::clicked, notifications, &QAction::trigger);
    contextLayout->addWidget(historyButton, 0, Qt::AlignVCenter);
    status->addPermanentWidget(contextStrip);
    auto timer = new QTimer(window);
    self->homeTimer = timer;
    timer->setInterval(500);
    QObject::connect(timer, &QTimer::timeout, window, [weak] {
        if (auto control = weak.lock())
            control->refreshHome();
    });
    timer->start();
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, window, [weak] {
        if (auto control = weak.lock())
            control->refreshHome();
    });
    self->updateHistory();
    return self;
}

std::vector<QAction*> bindWorkbenchExtensions(const std::shared_ptr<WorkbenchControl>& control, ExtensionsPage* page)
{
    const auto key = page->property("ElaPageKey").toString();
    const QPointer<ExtensionsPage> guarded(page);
    auto open = control->action(control->viewMenu, QObject::tr("扩展"), "EveOpenExtensions", QKeySequence("Ctrl+Shift+X"),
        [key](WorkbenchControl& state) { state.navigate(key); });
    auto install = control->action(control->fileMenu, QObject::tr("从扩展安装包安装…"), "EveInstallExtension", {},
        [key, guarded](WorkbenchControl& state) {
            state.navigate(key);
            if (state.open && guarded) guarded->choosePackage();
        });
    auto settings = static_cast<SettingsPage*>(control->settings.data());
    settings->addGroup("EveSettingsDesktopSection", QObject::tr("桌面"), QStringLiteral("desktop shell"));
    auto desktop = new ElaPushButton(QObject::tr("选择桌面…"), settings);
    desktop->setObjectName("EveSettingsChooseDesktop");
    settings->addRow(QObject::tr("启动时使用的桌面外壳"), desktop, QObject::tr("扩展 外壳 默认 切换 桌面"));
    std::weak_ptr<WorkbenchControl> weak = control;
    QObject::connect(desktop, &QPushButton::clicked, settings, [weak, key, guarded] {
        if (auto state = weak.lock(); state && state->open)
            state->withWindowAccess([&] {
                state->navigate(key);
                if (state->open && guarded) guarded->showDesktops();
            });
    });
    settings->finish();
    return {open, install};
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
                           const QString& category, const QString& plugin, std::function<bool()> presentation)
{
    if (!control || !control->open || key.isEmpty() || !control->search)
        return;
    if (std::any_of(control->entries.begin(), control->entries.end(),
                    [&](const WorkbenchControl::Entry& item) { return item.key == key; }))
        return;
    const auto label = category.isEmpty() ? title : title + QString::fromUtf8(" · ") + category;
    // Entry 在建议登记前已带初始门禁；模型通知重入也不能看到 Held 贡献。
    control->entries.push_back({key, title, {}, plugin, std::move(presentation)});
    const auto suggestion = control->search->addSuggestion(label, {{"EvePageKey", key}});
    auto entry = std::find_if(control->entries.begin(), control->entries.end(),
                              [&](const WorkbenchControl::Entry& item) { return item.key == key; });
    if (entry == control->entries.end())
        return;
    entry->suggestion = suggestion;
    control->refreshHome();
    if (control->open && control->search)
        control->search->setSearchText(control->search->getSearchText());
}

void refreshWorkbenchPresentation(const std::shared_ptr<WorkbenchControl>& control)
{
    if (!control || !control->open)
        return;
    control->withWindowAccess([control] {
        if (!control->open)
            return;
        if (control->search)
            control->search->setSearchText(control->search->getSearchText());
        if (!control->open)
            return;
        control->refreshHomeWithinAccess();
        if (control->open)
            control->updateHistory();
    });
}

void refreshWorkbenchModules(const std::shared_ptr<WorkbenchControl>& control)
{
    if (!control || !control->open)
        return;
    control->withWindowAccess([control] { control->refreshModules(); });
}

void selectWorkbenchPage(const std::shared_ptr<WorkbenchControl>& control, const QString& key)
{
    if (!control || !control->open || !control->tabs || !control->contains(key))
        return;
    control->current = key;
    auto entry = std::find_if(control->entries.begin(), control->entries.end(),
                              [&](const WorkbenchControl::Entry& item) { return item.key == key; });
    const auto title = entry->title;
    {
        QSignalBlocker blocked(control->tabs);
        int index = 0;
        while (index < control->tabs->count() && control->tabs->tabData(index).toString() != key)
            ++index;
        if (index == control->tabs->count()) {
            index = control->tabs->addTab(title);
            control->tabs->setTabData(index, key);
            control->tabVersions.insert(key, ++control->nextTabVersion);
        }
        control->tabs->setCurrentIndex(index);
    }
    control->record(key);
    if (key == control->homeKey)
        control->refreshHome();
    if (control->location)
        control->location->setText(title);
    if (control->location)
        control->location->setToolTip(title);
}

void selectWorkbenchEmpty(const std::shared_ptr<WorkbenchControl>& control)
{
    if (!control || !control->open)
        return;
    control->current.clear();
    control->updateHistory();
    if (control->location) {
        control->location->setText(QString::fromUtf8("未打开页面"));
        if (control->location)
            control->location->setToolTip(QString());
    }
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
    ++control->tabMenuGeneration;
    if (control->tabContextMenu)
        control->tabContextMenu->close();
    if (control->preferences && !control->preferences->flush())
        qWarning().noquote() << QString::fromUtf8("界面偏好保存失败；插件配置保存状态不受影响。");
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
        if (auto page = control->window->findChild<QWidget*>("EveWelcomePage"))
            page->setEnabled(false);
    if (control->settings)
        control->settings->setEnabled(false);
    if (control->about)
        control->about->setEnabled(false);
    if (control->togglePanel)
        control->togglePanel->setEnabled(false);
    control->updateHistory();
}
} // namespace eve::ui::detail
