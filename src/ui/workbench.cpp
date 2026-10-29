/** @file workbench.cpp
 * @brief 以 Ela 原生控件实现 VS Code 分区中的命令中心、菜单、设置与有界稳定键导航。
 */
#include "workbench_p.hpp"

#include "appearance_p.hpp"
#include "home_p.hpp"
#include "preferences_p.hpp"
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

namespace cordis::ui::detail {
namespace {
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
    /** @brief 宽屏给予搜索更多空间，窄屏优先保留完整菜单和前进后退。 */
    void updateWidth()
    {
        if (search_)
            search_->setFixedWidth(std::clamp(window()->width() - 820, 160, 360));
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
    std::shared_ptr<OutputControl> logs;
    QPointer<QToolButton> togglePanel;
    QPointer<ElaSuggestBox> search;
    QPointer<ElaToolButton> back, forward;
    QPointer<QAction> backAction, forwardAction;
    QPointer<QAction> addPluginAction, configurationFileAction;
    QPointer<ElaMenuBar> menuBar;
    QPointer<QWidget> settings, about;
    QPointer<ElaText> location;
    QPointer<QTimer> homeTimer;
    QPointer<WorkbenchPreferences> preferences;
    QPointer<ElaLineEdit> settingsSearch;
    QPointer<ElaText> settingsEmpty;
    /** @brief 检索索引只含宿主文字，不保存设置值或插件对象。 */
    struct SettingsGroup {
        QPointer<QWidget> widget;
        QString keywords;
    };
    std::vector<SettingsGroup> settingsGroups;
    bool filteringSettings = false;
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
        if (addPluginAction)
            addPluginAction->setEnabled(callbacks.canAddPlugin && callbacks.canAddPlugin());
        if (!open)
            return;
        if (configurationFileAction)
            configurationFileAction->setEnabled(callbacks.hasConfigurationFile && callbacks.hasConfigurationFile());
        if (!open)
            return;
        auto plugins = callbacks.plugins ? callbacks.plugins() : std::vector<HomePlugin>{};
        if (!open || !window)
            return;
        QSet<QString> ids;
        for (const auto& plugin : plugins)
            ids.insert(plugin.id);
        for (const auto& entry : entries) {
            if (entry.plugin.isEmpty() || ids.contains(entry.plugin))
                continue;
            if (callbacks.hideContribution && callbacks.hideContribution(entry.plugin))
                continue;
            ids.insert(entry.plugin);
            HomePlugin value{entry.plugin, entry.title, QString::fromUtf8("界面已就绪")};
            value.enabled = true;
            value.status = instanceStatus(InstanceState::Active);
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
        menu->setObjectName("CordisTabContextMenu");
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
        add(QString::fromUtf8("关闭页签"), "CordisTabClose", {anchor}, false);
        menu->addSeparator();
        add(QString::fromUtf8("关闭其他页签"), "CordisTabCloseOthers", std::move(others), true);
        add(QString::fromUtf8("关闭右侧页签"), "CordisTabCloseRight", std::move(right), false);
        add(QString::fromUtf8("关闭全部页签"), "CordisTabCloseAll", std::move(all), false);
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
        auto page = scrollPage(QString::fromUtf8("设置"), window);
        settings = page;
        page->setObjectName("CordisSettingsPage");
        auto content = new QWidget;
        auto layout = new QVBoxLayout(content);
        layout->setSpacing(16);
        layout->setSizeConstraint(QLayout::SetMinimumSize);
        settingsSearch = new ElaLineEdit(content);
        settingsSearch->setObjectName("CordisSettingsSearch");
        settingsSearch->setAccessibleName(QString::fromUtf8("搜索工作台设置"));
        settingsSearch->setPlaceholderText(QString::fromUtf8("搜索设置：主题、导航、底部工作区…（Ctrl+F）"));
        settingsSearch->setClearButtonEnabled(true);
        settingsSearch->setMinimumHeight(32);
        layout->addWidget(settingsSearch);
        settingsEmpty = new ElaText(QString::fromUtf8("没有匹配的设置。清空搜索可显示全部设置。"), 13, content);
        settingsEmpty->setObjectName("CordisSettingsSearchEmpty");
        secondaryText(settingsEmpty);
        settingsEmpty->hide();
        layout->addWidget(settingsEmpty);
        auto appearance =
            card(layout, QString::fromUtf8("外观"), QString::fromUtf8("使用 Ela 主题与组件，调整工作台显示方式。"));
        auto theme = new ElaComboBox(content);
        theme->setAccessibleName(QString::fromUtf8("工作台主题"));
        theme->setObjectName("CordisSettingsTheme");
        theme->addItem(QString::fromUtf8("浅色"));
        theme->addItem(QString::fromUtf8("深色"));
        theme->setCurrentIndex(eTheme->getThemeMode() == ElaThemeType::Dark ? 1 : 0);
        theme->setMaximumWidth(240);
        appearance->addWidget(theme);
        std::weak_ptr<WorkbenchControl> weak = shared_from_this();
        QObject::connect(theme, QOverload<int>::of(&QComboBox::currentIndexChanged), page, [weak](int index) {
            if (auto self = weak.lock(); self && self->open)
                self->withWindowAccess(
                    [index] { eTheme->setThemeMode(index == 1 ? ElaThemeType::Dark : ElaThemeType::Light); });
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
        auto toggle = new KeyboardToggleSwitch(content);
        toggle->setObjectName("CordisSettingsPanel");
        row->addWidget(toggle);
        panels->addLayout(row);
        new VisibilityBinding(panel, toggle);
        QObject::connect(toggle, &ElaToggleSwitch::toggled, page, [weak](bool checked) {
            if (auto self = weak.lock(); self && self->open && self->panel)
                self->withWindowAccess([self, checked] { self->panel->setVisible(checked); });
        });
        toggle->setAccessibleName(QString::fromUtf8("显示底部工作区"));
        auto navigationRow = new QHBoxLayout;
        navigationRow->addWidget(new ElaText(QString::fromUtf8("插件导航"), 14, content));
        navigationRow->addStretch();
        auto navigationMode = new ElaComboBox(content);
        navigationMode->setObjectName("CordisSettingsNavigationMode");
        navigationMode->setAccessibleName(QString::fromUtf8("插件导航显示方式"));
        navigationMode->addItems({QString::fromUtf8("紧凑图标"), QString::fromUtf8("展开名称")});
        navigationMode->setCurrentIndex(window->getNavigationBarDisplayMode() == ElaNavigationType::Maximal ? 1 : 0);
        navigationRow->addWidget(navigationMode);
        panels->addLayout(navigationRow);
        QObject::connect(navigationMode, QOverload<int>::of(&QComboBox::currentIndexChanged), page, [weak](int index) {
            if (auto self = weak.lock(); self && self->open)
                self->withWindowAccess([self, index] {
                    self->window->setNavigationBarDisplayMode(index ? ElaNavigationType::Maximal
                                                                    : ElaNavigationType::Compact);
                });
        });
        QObject::connect(window, &ElaWindow::pNavigationBarDisplayModeChanged, navigationMode, [this, navigationMode] {
            const QSignalBlocker blocker(navigationMode);
            navigationMode->setCurrentIndex(window->getNavigationBarDisplayMode() == ElaNavigationType::Maximal ? 1
                                                                                                                : 0);
        });
        const auto spinRow = [&](const QString& text, const char* name, int low, int high, int value) {
            auto rowLayout = new QHBoxLayout;
            rowLayout->addWidget(new ElaText(text, 14, content));
            rowLayout->addStretch();
            auto spin = new ElaSpinBox(content);
            spin->setObjectName(QString::fromLatin1(name));
            spin->setAccessibleName(text);
            spin->setRange(low, high);
            spin->setSuffix(" px");
            spin->setValue(value);
            spin->setFixedWidth(140);
            rowLayout->addWidget(spin);
            panels->addLayout(rowLayout);
            return spin;
        };
        auto navigationWidth = spinRow(QString::fromUtf8("展开导航宽度"), "CordisSettingsNavigationWidth", 180, 360,
                                       window->getNavigationBarWidth());
        QObject::connect(navigationWidth, QOverload<int>::of(&QSpinBox::valueChanged), page, [weak](int value) {
            if (auto self = weak.lock(); self && self->open)
                self->withWindowAccess([self, value] { self->window->setNavigationBarWidth(value); });
        });
        QObject::connect(window, &ElaWindow::pNavigationBarWidthChanged, navigationWidth, [this, navigationWidth] {
            const QSignalBlocker blocker(navigationWidth);
            navigationWidth->setValue(window->getNavigationBarWidth());
        });
        auto panelHeight = spinRow(QString::fromUtf8("底部工作区高度"), "CordisSettingsPanelHeight", 120, 480,
                                   preferences->panelHeight());
        QObject::connect(panelHeight, QOverload<int>::of(&QSpinBox::valueChanged), page, [weak](int value) {
            if (auto self = weak.lock(); self && self->open)
                self->withWindowAccess([self, value] { self->preferences->setPanelHeight(value); });
        });
        if (auto splitter = window->findChild<QSplitter*>("ElaWorkbenchPanelSplitter"))
            QObject::connect(splitter, &QSplitter::splitterMoved, panelHeight, [this, panelHeight] {
                const QSignalBlocker blocker(panelHeight);
                panelHeight->setValue(preferences->panelHeight());
            });
        auto preferenceStatus = new ElaText(content);
        preferenceStatus->setObjectName("CordisPreferencesStatus");
        preferenceStatus->setTextPixelSize(12);
        preferenceStatus->setTextFormat(Qt::PlainText);
        secondaryText(preferenceStatus);
        panels->addWidget(preferenceStatus);
        preferences->setReporter([weak, label = QPointer<ElaText>(preferenceStatus)](const QString& text) {
            if (auto self = weak.lock(); self && self->open && label)
                self->withWindowAccess([label, text] {
                    if (label)
                        label->setText(text);
                });
        });
        auto reset = new ActionButton(QString::fromUtf8("恢复默认外观与布局"), content);
        reset->setObjectName("CordisSettingsReset");
        reset->setToolTip(QString::fromUtf8("恢复浅色、紧凑导航和默认尺寸；不会更改插件配置或运行状态。"));
        panels->addWidget(reset, 0, Qt::AlignLeft);
        QObject::connect(reset, &QPushButton::clicked, page, [weak, panelHeight] {
            if (auto self = weak.lock(); self && self->open)
                self->withWindowAccess([self, panelHeight] {
                    self->preferences->reset();
                    panelHeight->setValue(self->preferences->panelHeight());
                });
        });
        auto shortcuts =
            card(layout, QString::fromUtf8("快捷键"),
                 QString::fromUtf8("Alt+← / Alt+→  前进后退\nCtrl+P  搜索页面\nCtrl+Shift+P  打开命令面板\nCtrl+B / "
                                   "Ctrl+J  展开插件导航 / 切换底部工作区\nCtrl+W  关闭当前页签"));
        appearance->parentWidget()->setObjectName("CordisSettingsAppearanceSection");
        panels->parentWidget()->setObjectName("CordisSettingsLayoutSection");
        shortcuts->parentWidget()->setObjectName("CordisSettingsShortcutsSection");
        settingsGroups = {
            {appearance->parentWidget(), QString::fromUtf8("外观 主题 浅色 深色 appearance theme light dark")},
            {panels->parentWidget(),
             QString::fromUtf8("布局 工作区 底部 导航 高度 宽度 默认 偏好 layout panel navigation size preferences")},
            {shortcuts->parentWidget(),
             QString::fromUtf8("快捷键 搜索 命令 页签 导航 前进 后退 keyboard shortcuts search command")}};
        QObject::connect(settingsSearch, &QLineEdit::textChanged, page, [weak] {
            if (auto self = weak.lock())
                self->filterSettings();
        });
        const auto searchAction = [&](const char* name, const QKeySequence& shortcut, bool clear) {
            auto action = new QAction(page);
            action->setObjectName(QString::fromLatin1(name));
            action->setShortcut(shortcut);
            action->setShortcutContext(clear ? Qt::WidgetShortcut : Qt::WidgetWithChildrenShortcut);
            (clear ? static_cast<QWidget*>(settingsSearch.data()) : static_cast<QWidget*>(page))->addAction(action);
            actions.emplace_back(action);
            QObject::connect(action, &QAction::triggered, page, [weak, clear] {
                if (auto self = weak.lock(); self && self->open)
                    self->withWindowAccess([self, clear] {
                        if (clear)
                            self->settingsSearch->clear();
                        else {
                            self->settingsSearch->setFocus(Qt::ShortcutFocusReason);
                            self->settingsSearch->selectAll();
                        }
                    });
            });
        };
        searchAction("CordisFindSettings", QKeySequence("Ctrl+F"), false);
        searchAction("CordisClearSettingsSearch", QKeySequence(Qt::Key_Escape), true);
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
    /** @brief 搜索仅过滤卡片；可见性事件重入搜索时排队收敛到最新文字。 */
    void filterSettings()
    {
        if (!open || !settingsSearch || filteringSettings)
            return;
        auto self = shared_from_this();
        withWindowAccess([self] {
            if (!self->open || !self->settingsSearch)
                return;
            QScopedValueRollback<bool> filtering(self->filteringSettings, true);
            const auto query = self->settingsSearch->text().trimmed();
            const auto groups = self->settingsGroups;
            bool any = false;
            for (const auto& group : groups) {
                if (!self->open || !group.widget)
                    return;
                const bool matches = query.isEmpty() || group.keywords.contains(query, Qt::CaseInsensitive);
                any = any || matches;
                group.widget->setVisible(matches);
            }
            if (!self->open)
                return;
            self->settingsEmpty->setVisible(!any);
            if (!self->open)
                return;
            if (self->settingsSearch->text().trimmed() != query) {
                std::weak_ptr<WorkbenchControl> weak = self;
                QTimer::singleShot(0, self->window, [weak] {
                    if (auto value = weak.lock())
                        value->filterSettings();
                });
            }
        });
    }
};

QWidget* createWelcomePage()
{
    return new HomePage;
}

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
    bar->setObjectName("CordisMenuBar");
    bar->setFixedHeight(30);
    auto file = bar->addMenu(QString::fromUtf8("文件(&F)"));
    auto view = bar->addMenu(QString::fromUtf8("视图(&V)"));
    auto go = bar->addMenu(QString::fromUtf8("转到(&G)"));
    auto help = bar->addMenu(QString::fromUtf8("帮助(&H)"));
    bar->setFixedWidth(bar->sizeHint().width());
    window->setCustomWidget(ElaAppBarType::LeftArea, bar);
    auto center = new CommandCenter(window);
    center->setObjectName("CordisCommandCenter");
    auto centerLayout = new QHBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(4);
    auto back = new ElaToolButton(center);
    self->back = back;
    back->setObjectName("CordisNavigateBack");
    back->setElaIcon(ElaIconType::ArrowLeft);
    back->setToolTip(QString::fromUtf8("后退（Alt+←）"));
    back->setAccessibleName(QString::fromUtf8("后退"));
    back->setFixedSize(32, 30);
    back->setShortcut(QKeySequence("Alt+Left"));
    auto forward = new ElaToolButton(center);
    self->forward = forward;
    forward->setObjectName("CordisNavigateForward");
    forward->setElaIcon(ElaIconType::ArrowRight);
    forward->setToolTip(QString::fromUtf8("前进（Alt+→）"));
    forward->setAccessibleName(QString::fromUtf8("前进"));
    forward->setFixedSize(32, 30);
    forward->setShortcut(QKeySequence("Alt+Right"));
    auto search = new ElaSuggestBox(center);
    self->search = search;
    search->setObjectName("CordisPageSearch");
    search->setFixedHeight(30);
    center->setSearch(search);
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
                         if (auto search = state.panel->findChild<QLineEdit*>("CordisCommandFilter"))
                             search->setFocus();
                         else if (auto widget = state.panel->currentWidget())
                             widget->setFocus();
                     }
                 });
    self->action(view, QString::fromUtf8("清空输出"), "CordisClearOutput", {}, [](WorkbenchControl& state) {
        if (state.logs)
            clearOutput(state.logs);
        else if (state.output)
            state.output->clear();
    });
    self->action(view, QString::fromUtf8("复制全部输出"), "CordisCopyOutput", {}, [](WorkbenchControl& state) {
        if (state.logs)
            QApplication::clipboard()->setText(currentOutputText(state.logs));
        else if (state.output)
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
    platformAction(QString::fromUtf8("添加插件配置…"), "CordisAddPlugin", true);
    platformAction(QString::fromUtf8("配置文件…"), "CordisConfigurationFile", false);
    self->action(go, QString::fromUtf8("欢迎页"), "CordisOpenHome", {},
                 [](WorkbenchControl& state) { state.navigate(state.homeKey); });
    auto notifications =
        self->action(go, QString::fromUtf8("通知历史"), "CordisOpenNotifications", {}, [](WorkbenchControl& state) {
            if (!state.open || !state.callbacks.showNotifications)
                return;
            const auto show = state.callbacks.showNotifications;
            const auto result = show();
            if (!result.hasValue() && state.open && state.logs)
                (void)appendOutput(state.logs, {}, "platform", result.error().message, OutputLevel::Error);
        });
    self->action(help, QString::fromUtf8("关于 Cordis"), "CordisOpenAbout", {}, [](WorkbenchControl& state) {
        if (state.about)
            state.navigate(state.about->property("ElaPageKey").toString());
    });
    self->createSettings();
    for (const auto& entry : {std::make_pair("CordisWelcomePlugins", "CordisAddPlugin"),
                              std::make_pair("CordisWelcomeSettings", "CordisOpenSettings"),
                              std::make_pair("CordisWelcomeCommands", "CordisOpenCommands")}) {
        auto button = window->findChild<QAbstractButton*>(QString::fromLatin1(entry.first));
        auto action = window->findChild<QAction*>(QString::fromLatin1(entry.second));
        if (button && action)
            QObject::connect(button, &QAbstractButton::clicked, action, &QAction::trigger);
        if (button && action) {
            const auto sync = [button, action] {
                button->setEnabled(action->isEnabled());
                button->setToolTip(action->toolTip());
            };
            QObject::connect(action, &QAction::changed, button, sync);
            sync();
        }
    }
    auto panelTools = new QWidget(panel);
    panelTools->setObjectName("CordisPanelTools");
    auto toolsLayout = new QHBoxLayout(panelTools);
    toolsLayout->setContentsMargins(0, 0, 4, 0);
    toolsLayout->setSpacing(2);
    for (const auto& entry : {std::make_pair("CordisCopyPanelOutput", "CordisCopyOutput"),
                              std::make_pair("CordisClearPanelOutput", "CordisClearOutput")}) {
        auto button = new ElaToolButton(panelTools);
        const bool copy = QString::fromLatin1(entry.first) == "CordisCopyPanelOutput";
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
                    button->setVisible(self->panel->currentWidget() == self->output);
                    button->setEnabled(self->open && self->output);
                });
        };
        QObject::connect(panel, &QTabWidget::currentChanged, button, sync);
        sync(panel->currentIndex());
    }
    auto corner = new ElaToolButton(panelTools);
    corner->setObjectName("CordisHidePanel");
    corner->setElaIcon(ElaIconType::Xmark);
    corner->setToolTip(QString::fromUtf8("隐藏工作区（Ctrl+J）"));
    corner->setAccessibleName(QString::fromUtf8("隐藏底部工作区"));
    corner->setFixedSize(28, 28);
    toolsLayout->addWidget(corner);
    panel->setCornerWidget(panelTools, Qt::TopRightCorner);
    QObject::connect(corner, &QToolButton::clicked, window, [weak] {
        if (auto control = weak.lock(); control && control->open && control->togglePanel)
            control->togglePanel->click();
    });
    auto location = new ElaText(QString::fromUtf8("工作台"), 12, window);
    self->location = location;
    location->setTextFormat(Qt::PlainText);
    location->setObjectName("CordisCurrentPage");
    location->setWordWrap(false);
    location->setMaximumWidth(220);
    if (window->statusBar())
        window->statusBar()->addPermanentWidget(location);
    auto historyButton = new ElaToolButton(window);
    historyButton->setObjectName("CordisShowNotifications");
    historyButton->setElaIcon(ElaIconType::Bell);
    historyButton->setAccessibleName(QString::fromUtf8("查看通知历史"));
    historyButton->setToolTip(QString::fromUtf8("查看通知历史；收到新通知不会自动打开面板。"));
    historyButton->setFixedSize(26, 24);
    QObject::connect(historyButton, &QToolButton::clicked, notifications, &QAction::trigger);
    if (window->statusBar())
        window->statusBar()->addPermanentWidget(historyButton);
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
