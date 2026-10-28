/** @file pages.cpp
 * @brief 在 Ela 原生导航外壳中托管动态页面，按撤销贡献、完整删除和闭包释放的顺序清理。
 */
#include "fixed_tabs_p.hpp"
#include "management_p.hpp"
#include "services_p.hpp"
#include "workbench_p.hpp"
#include <ElaActionCommander.h>
#include <ElaApplication.h>
#include <ElaDialog.h>
#include <ElaIcon.h>
#include <ElaListView.h>
#include <ElaNavigationBar.h>
#include <ElaPlainTextEdit.h>
#include <ElaStatusBar.h>
#include <ElaTabBar.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToolButton.h>
#include <ElaWindow.h>
#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <cordis/qt/object.hpp>
#include <cordis/ui/ela_shell.hpp>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace cordis::ui::detail {
/** @brief 工作台页面标签只切换宿主页面，不允许拖出独立窗口。 */
class HostPageTabs final : public ElaTabBar {
public:
    /** @brief 页签保持插入顺序，页面销毁由 effect 管理。 */
    explicit HostPageTabs(QWidget* parent) : ElaTabBar(parent)
    {
        setMovable(false);
        setTabsClosable(false);
        setAcceptDrops(false);
        setExpanding(false);
        setTabSize(QSize(160, 36));
    }
protected:
    /** @brief 禁止第三方拖出逻辑，保留普通悬停效果。 */
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event->buttons() == Qt::NoButton) ElaTabBar::mouseMoveEvent(event);
    }
};
using Key = std::tuple<InstanceId, Epoch, std::string>;
struct ShellControl;
/** @brief 每个贡献保留独立工厂与 effect，不把 Qt parent 当作清理完成证明。 */
struct PageControl : std::enable_shared_from_this<PageControl> {
    std::weak_ptr<ShellControl> shell;
    ContextIdentity owner;
    PageSpec spec;
    std::function<Result<void>()> guard;
    QPointer<QWidget> widget;
    QPointer<ElaDialog> dialog;
    std::unique_ptr<QObject> detached;
    std::optional<qt::QObjectResource> resource;
    std::optional<EffectToken> effect;
    QString navigationKey;
    QPersistentModelIndex navigationIndex;
    bool navigation = false;
    bool revoked = false;
    bool retiring = false;
    bool watched = false;
    Promise<void> closed;
    /** @brief 贡献 id 在整个释放窗口中保留，防止旧页未销毁时复用键。 */
    Key key() const { return {owner.instance, owner.epoch, spec.id}; }
    /** @brief 分类也按实例/epoch 命名，不删除其他实例的同名分类。 */
    Key group() const { return {owner.instance, owner.epoch, spec.category}; }
    /** @brief 在完整资源确认之后释放工厂；宿主记录另等 effect 完整关闭。 */
    void finish();
    /** @brief 已开始的撤销只观察同一 Task，清理失败保留账目及闭包。 */
    Task<void> retire();
    /** @brief 返回不暴露 Ela key、QObject 或闭包的诊断。 */
    PageSnapshot snapshot() const;
};
/** @brief 一个实例代次的分类仅包含它自己的页面。 */
struct Group {
    QString nodeKey;
    QPersistentModelIndex index;
    std::size_t pages = 0;
    QString target;
};
/** @brief 原生关闭事件必须先交宿主异步停止流程，不直接退出事件循环。 */
class HostWindow final : public ElaWindow {
public:
    /** @brief 窗口持有控制块，facade 丢弃不导致跨线程销毁。 */
    explicit HostWindow(std::shared_ptr<ShellControl> state) : state_(std::move(state)) {}
    /** @brief 清理入口只在析构前使用，窗口消失后禁止再访问 Ela。 */
    ~HostWindow() override;
protected:
    /** @brief 忽略同步关闭，交给宿主等待 Kernel 和页面清理。 */
    void closeEvent(QCloseEvent* event) override;
private:
    std::shared_ptr<ShellControl> state_;
};
/** @brief 全部 UI 账目只由应用线程修改，关闭失败不能强制删除窗口。 */
struct ShellControl : std::enable_shared_from_this<ShellControl> {
    std::shared_ptr<qt::QtScheduler> scheduler;
    std::shared_ptr<ServicesControl> services;
    std::shared_ptr<ManagementControl> management;
    QPointer<HostWindow> window;
    std::optional<qt::QObjectResource> windowResource;
    QPointer<ElaNavigationBar> navigation;
    QPointer<QTreeView> navigationView;
    QPointer<HostPageTabs> tabs;
    QPointer<QStackedWidget> stack;
    QPointer<QWidget> safePage;
    QPointer<QWidget> managementPage;
    std::shared_ptr<WorkbenchControl> workbench;
    QString safeKey;
    QString managementKey;
    QString commandsKey;
    QString settingsKey;
    QString aboutKey;
    std::map<QString, std::weak_ptr<PageControl>> rows;
    std::map<Key, std::shared_ptr<PageControl>> pages;
    std::map<Key, Group> groups;
    std::function<void()> closeHandler;
    std::function<Result<void>()> configurationHandler;
    bool open = true;
    bool closing = false;
    bool starting = false;
    bool windowClosing = false;
    bool failed = false;
    bool closeRequested = false;
    bool servicesClosed = false;
    bool rememberPluginSelection = true;
    bool managementClosed = false;
    std::size_t navigationAccesses = 0;
    std::size_t waiting = 0;
    Promise<void> closed;
    /** @brief 管理页切换可能同步重入关闭，完整导航栈返回前不得销毁窗口。 */
    struct NavigationAccess {
        ShellControl& owner;
        /** @brief 调用方须在导航期间保留外壳控制块。 */
        explicit NavigationAccess(ShellControl& control) : owner(control) { ++owner.navigationAccesses; }
        /** @brief 最后一个导航栈返回后重新检查窗口关闭条件。 */
        ~NavigationAccess() { --owner.navigationAccesses; owner.checkClosed(); }
    };
    /** @brief 原生导航和页签共享稳定 key，反向同步选择不再次派发导航。 */
    void syncSelection(const QString& key)
    {
        if (!open || !stack || !stack->currentWidget() || stack->currentWidget()->property("ElaPageKey").toString() != key) return;
        auto found = rows.find(key);
        if (rememberPluginSelection && found != rows.end()) {
            if (auto page = found->second.lock(); page && !page->spec.category.empty()) {
                auto group = groups.find(page->group());
                if (group != groups.end()) {
                    group->second.target = key;
                    navigation->setExpanderNavigationTarget(group->second.nodeKey, key);
                }
            }
        }
        selectWorkbenchPage(workbench, key);
    }
    /** @brief 关闭最后一个视图后只显示空画布，页面对象仍由各自 effect 保留。 */
    void selectEmpty()
    {
        if (!open || !window) return;
        NavigationAccess access(*this);
        window->showEmptyWorkbench();
        if (open && stack->currentWidget()->objectName() == "ElaEmptyWorkbench") selectWorkbenchEmpty(workbench);
    }
    /** @brief 菜单、搜索与历史只接收当前外壳的键，插件页继续校验代次。 */
    Result<void> selectKey(const QString& key, bool rememberPlugin = true)
    {
        if (!open || !window) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell closing"));
        QScopedValueRollback<bool> remember(rememberPluginSelection, rememberPlugin);
        auto found = rows.find(key);
        if (found != rows.end()) {
            auto page = found->second.lock();
            if (page) return navigate(page);
        }
        if (key == managementKey)
            return selectManagement();
        if (key != safeKey && key != settingsKey && key != aboutKey)
            return Result<void>::failure(makeError(ErrorCode::StaleContext, "page is no longer registered"));
        NavigationAccess access(*this);
        navigation->navigation(key, false);
        if (!open) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell closed during navigation"));
        syncSelection(key);
        return Result<void>::success();
    }
    /** @brief 永久管理页的程序入口和导航信号共用关闭屏障，重入关门后不再改变选择。 */
    Result<void> selectManagement()
    {
        if (!open || !window) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell closing"));
        NavigationAccess access(*this);
        return showManagementDialog(management);
    }
    /** @brief 鼠标导航与 PageHandle 使用同一业务门禁，禁止旧代通过界面重新进入。 */
    Result<void> navigate(std::shared_ptr<PageControl> page)
    {
        if (!open || !window || page->revoked || page->retiring || (!page->navigation && !page->dialog) ||
            !page->widget)
            return Result<void>::failure(makeError(ErrorCode::ContextClosing, "page retiring"));
        auto allowed = page->guard();
        if (!allowed.hasValue()) return allowed;
        NavigationAccess access(*this);
        if (page->dialog) {
            page->dialog->show();
            if (open && page->dialog) {
                page->dialog->raise();
                page->dialog->activateWindow();
            }
            return open ? Result<void>::success()
                        : Result<void>::failure(
                              makeError(ErrorCode::ContextClosing, "shell closed during dialog navigation"));
        }
        navigation->navigation(page->navigationKey, false);
        syncSelection(page->navigationKey);
        if (!open) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell closed during navigation"));
        if (stack->currentWidget() == page->widget && page->navigationIndex.isValid()) navigationView->setCurrentIndex(page->navigationIndex);
        return Result<void>::success();
    }
    /** @brief 将窗口关闭请求隔离在 Qt 边界；宿主可通过 close() 重试/读取结果。 */
    void requestClose()
    {
        if (closing || closeRequested || !closeHandler) return;
        closeRequested = true;
        try { auto handler = closeHandler; handler(); }
        catch (...) { closeRequested = false; }
    }
    /** @brief 页面及命令 effect 全部成功后才销毁窗口与安全页。 */
    void checkClosed()
    {
        if (!closing || starting || failed || windowClosing || !servicesClosed || !managementClosed || navigationAccesses != 0 || waiting != 0 || !pages.empty()) return;
        windowClosing = true;
        closeHandler = {};
        configurationHandler = {};
        auto self = shared_from_this();
        cordis::detail::TaskAccess::observe(windowResource->close(), [self](Task<void>::Snapshot result) {
            if (result->hasValue()) self->closed.resolve();
            else self->closed.reject(result->error());
        });
    }
    /** @brief setup 尚在调用工厂时 effect 可能未返回；赋值后再补发关闭请求。 */
    void closePage(const std::shared_ptr<PageControl>& page)
    {
        if (!closing || page->watched || !page->effect) return;
        page->watched = true;
        ++waiting;
        auto self = shared_from_this();
        cordis::detail::TaskAccess::observe(page->effect->close(), [self](Task<CleanupReport>::Snapshot result) {
            --self->waiting;
            if (!result->hasValue() || !result->value().quiescent) {
                self->failed = true;
                auto error = makeError(ErrorCode::CleanupFailed, "page effect did not quiesce");
                if (result->hasValue()) error.causes = result->value().errors;
                else error.causes.push_back(result->error());
                self->closed.reject(std::move(error));
            }
            self->checkClosed();
        });
    }
    /** @brief 先统一关门，再使用记录副本请求关闭，容忍同步完成和回调重入。 */
    Task<void> close()
    {
        if (closing) return closed.task();
        open = false;
        closing = true;
        starting = true;
        closeWorkbench(workbench);
        auto self = shared_from_this();
        cordis::detail::TaskAccess::observe(closeManagement(management), [self](Task<void>::Snapshot result) {
            if (result->hasValue()) self->managementClosed = true;
            else { self->failed = true; self->closed.reject(result->error()); }
            self->checkClosed();
        });
        cordis::detail::TaskAccess::observe(closeUiServices(services), [self](Task<void>::Snapshot result) {
            if (result->hasValue()) self->servicesClosed = true;
            else { self->failed = true; self->closed.reject(result->error()); }
            self->checkClosed();
        });
        std::vector<std::shared_ptr<PageControl>> pending;
        for (const auto& item : pages) pending.push_back(item.second);
        for (const auto& page : pending) closePage(page);
        starting = false;
        checkClosed();
        return closed.task();
    }
};
/** @brief 输入事件在到达插件 QWidget 前重查 epoch；程序信号仍须由插件遵循 Context 契约。 */
class InputGate final : public QObject {
public:
    /** @brief 守卫属于外壳，应用退出前随窗口销毁。 */
    InputGate(QObject* parent, std::weak_ptr<ShellControl> state) : QObject(parent), state_(std::move(state)) {}
protected:
    /** @brief 停用的可见旧页也不能在异步清理窗口内继续接收用户输入。 */
    bool eventFilter(QObject* receiver, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::InputMethod:
        case QEvent::Wheel:
        case QEvent::Shortcut:
        case QEvent::ShortcutOverride:
        case QEvent::ContextMenu:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            break;
        default:
            return false;
        }
        auto state = state_.lock();
        QWidget* widget = nullptr;
        // QShortcut 等输入接收者本身不是 QWidget，但仍归属于页面对象树。
        for (auto object = receiver; object && !widget; object = object->parent())
            widget = qobject_cast<QWidget*>(object);
        if (!state || !widget) return false;
        for (const auto& item : state->pages) {
            const auto& page = item.second;
            if (page->widget && (page->widget == widget || page->widget->isAncestorOf(widget)))
                return !state->open || page->revoked || page->retiring || !page->guard || !page->guard().hasValue();
        }
        return false;
    }
private:
    std::weak_ptr<ShellControl> state_;
};
/** @brief QWidget 删除后 QPointer 自行失效，原生窗口不再承担页面导航入口。 */
HostWindow::~HostWindow() { state_->open = false; state_->window = nullptr; }
/** @brief Qt close 只触发宿主请求，不将隐藏窗口当成异步退出完成。 */
void HostWindow::closeEvent(QCloseEvent* event) { event->ignore(); state_->requestClose(); }
/** @brief 读取当前导航仅在窗口与贡献都存在时进行，安全页始终在窗口最后删除。 */
PageSnapshot PageControl::snapshot() const
{
    auto state = shell.lock();
    const bool current =
        dialog ? dialog->isVisible()
               : state && state->window && state->stack && navigation && state->stack->currentWidget() == widget;
    return {owner, spec.id, navigation, !widget.isNull(), current, revoked || retiring};
}
/** @brief 插件工厂析构可能重入；先维持 retiring 和保留 id，再释放捕获。 */
void PageControl::finish()
{
    spec.factory = {};
    guard = {};
    closed.resolve();
}
/** @brief 撤销宿主导航后解除 Qt parent；此后由 QObjectResource 唯一负责删除。 */
Task<void> PageControl::retire()
{
    if (retiring) return closed.task();
    revoked = true;
    retiring = true;
    auto state = shell.lock();
    std::weak_ptr<PageControl> weak = shared_from_this();
    // 保留贡献记录直到 effect 的完整关闭报告到达，避免外壳提前结束等待。
    cordis::detail::TaskAccess::observe(effect->close(), [weak](Task<CleanupReport>::Snapshot result) {
        if (!result->hasValue() || !result->value().quiescent) return;
        if (auto page = weak.lock()) if (auto owner = page->shell.lock()) {
            owner->pages.erase(page->key());
            owner->checkClosed();
        }
    });
    if (!state || !state->window) {
        closed.reject(makeError(ErrorCode::CleanupFailed, "Ela window disappeared before page cleanup"));
        return closed.task();
    }
    if (navigation && !widget) {
        closed.reject(makeError(ErrorCode::CleanupFailed, "page destroyed outside contribution cleanup"));
        return closed.task();
    }
    if (navigation) {
        if (!state->open && state->stack->currentWidget() == widget) state->window->showEmptyWorkbench();
        state->rows.erase(navigationKey);
        removeWorkbenchPage(state->workbench, navigationKey);
        {
            QSignalBlocker blocked(state->tabs);
            for (int i = 0; i < state->tabs->count(); ++i) if (state->tabs->tabData(i).toString() == navigationKey) { state->tabs->removeTab(i); break; }
        }
        state->window->removeNavigationNode(navigationKey);
        auto category = state->groups.find(group());
        if (category != state->groups.end() && category->second.target == navigationKey) {
            category->second.target.clear();
            for (const auto& entry : state->pages) {
                const auto& candidate = entry.second;
                if (candidate.get() == this || candidate->group() != group() || candidate->revoked || candidate->retiring || !candidate->navigation) continue;
                category->second.target = candidate->navigationKey; break;
            }
            state->navigation->setExpanderNavigationTarget(category->second.nodeKey, category->second.target);
        }
        navigationKey.clear();
        navigationIndex = {};
        navigation = false;
        if (!spec.category.empty()) --state->groups.at(group()).pages;
    }
    auto category = state->groups.find(group());
    if (!spec.category.empty() && category != state->groups.end() && category->second.pages == 0) {
        state->window->removeNavigationNode(category->second.nodeKey);
        state->groups.erase(category);
    }
    if (!widget) { finish(); return closed.task(); }
    auto container = dialog ? static_cast<QWidget*>(dialog.data()) : widget.data();
    container->hide();
    container->setParent(nullptr);
    if (!detached)
        detached.reset(container);
    try { resource.emplace(detached, state->scheduler); }
    catch (const std::exception& error) {
        closed.reject(makeError(ErrorCode::CleanupFailed, error.what()));
        return closed.task();
    }
    auto self = shared_from_this();
    cordis::detail::TaskAccess::observe(resource->close(), [self](Task<void>::Snapshot result) {
        if (!result->hasValue()) self->closed.reject(result->error());
        else self->finish();
    });
    return closed.task();
}
} // namespace cordis::ui::detail

namespace cordis::ui {
/** @brief owner 和 Context 门禁验证在预留 id 之前完成，工厂由 effect setup 捕获调用。 */
Result<PageHandle> UiPagesService::add(const Context& owner, PageSpec spec) const
{
    auto state = control_.lock();
    if (!state) return Result<PageHandle>::failure(makeError(ErrorCode::ContextClosing, "UI shell expired"));
    if (!state->scheduler->isCurrentThread()) return Result<PageHandle>::failure(makeError(ErrorCode::WrongThread, "UI add requires owner thread"));
    auto identity = owner.identity();
    if (!identity.hasValue()) return Result<PageHandle>::failure(identity.error());
    auto guard = owner.eventGuard(state->scheduler);
    if (!guard.hasValue()) return Result<PageHandle>::failure(guard.error());
    if (!state->open || !state->window) return Result<PageHandle>::failure(makeError(ErrorCode::ContextClosing, "UI shell closing"));
    if (spec.id.empty() || spec.title.empty() || !spec.factory)
        return Result<PageHandle>::failure(makeError(ErrorCode::InvalidArgument, "page id, title and factory required"));
    auto page = std::make_shared<detail::PageControl>();
    page->shell = state;
    page->owner = identity.value();
    page->spec = std::move(spec);
    page->guard = std::move(guard.value());
    if (state->pages.count(page->key())) return Result<PageHandle>::failure(makeError(ErrorCode::ServiceConflict, "duplicate page contribution"));
    state->pages.emplace(page->key(), page);
    auto effect = owner.effect([page, state](EffectSetupContext setup) {
        auto cleanup = setup.addCleanup([page] { return page->retire(); });
        if (!cleanup.hasValue()) { page->finish(); state->pages.erase(page->key()); return failedTask(cleanup.error()); }
        auto widget = page->spec.factory();
        page->widget = widget.get();
        page->detached.reset(widget.release());
        if (!page->widget || page->widget->parent() || page->widget->thread() != QApplication::instance()->thread() ||
            page->widget->testAttribute(Qt::WA_DeleteOnClose))
            return failedTask(makeError(ErrorCode::InvalidArgument, "page factory requires unparented owner-thread QWidget without auto delete"));
        auto allowed = page->guard();
        if (!allowed.hasValue()) return failedTask(allowed.error());
        if (!state->open) return failedTask(makeError(ErrorCode::ContextClosing, "shell closed during page creation"));
        if (page->spec.presentation == PagePresentation::Dialog) {
            auto dialog = new ElaDialog(state->window);
            page->dialog = dialog;
            dialog->setObjectName("CordisContributionDialog");
            dialog->setWindowTitle(QString::fromStdString(page->spec.title));
            dialog->setIsStayTop(false);
            dialog->resize(1100, 760);
            auto layout = new QVBoxLayout(dialog);
            layout->setContentsMargins(0, 36, 0, 0);
            layout->addWidget(page->widget);
            page->detached.release();
            return completedTask();
        }
        QString categoryKey;
        QModelIndex parentIndex;
        if (!page->spec.category.empty()) {
            auto group = state->groups.find(page->group());
            if (group == state->groups.end()) {
                auto added = state->window->addExpanderNode(QString::fromStdString(page->spec.title), categoryKey, ElaIconType::PuzzlePiece);
                if (added != ElaNavigationType::Success) return failedTask(makeError(ErrorCode::InvalidArgument, "Ela category registration failed"));
                auto model = state->navigationView->model();
                parentIndex = model->index(model->rowCount() - 1, 0);
                state->groups.emplace(page->group(), detail::Group{categoryKey, parentIndex, 0, {}});
                state->window->expandNavigationNode(categoryKey);
            } else {
                categoryKey = group->second.nodeKey;
                parentIndex = group->second.index;
            }
        }
        const auto title = QString::fromStdString(page->spec.title);
        auto added = categoryKey.isEmpty() ? state->window->addPageNode(title, page->widget, ElaIconType::PuzzlePiece)
            : state->window->addPageNode(title, page->widget, categoryKey, ElaIconType::PuzzlePiece);
        if (added != ElaNavigationType::Success) return failedTask(makeError(ErrorCode::InvalidArgument, "Ela page registration failed"));
        page->navigationKey = page->widget->property("ElaPageKey").toString();
        auto model = state->navigationView->model();
        page->navigationIndex = model->index(model->rowCount(parentIndex) - 1, 0, parentIndex);
        state->rows.emplace(page->navigationKey, page);
        page->navigation = true;
        page->detached.release();
        if (!categoryKey.isEmpty()) {
            auto& group = state->groups.at(page->group());
            if (++group.pages == 1) {
                group.target = page->navigationKey;
                state->navigation->setExpanderNavigationTarget(categoryKey, page->navigationKey);
            }
        }
        detail::registerWorkbenchPage(state->workbench, page->navigationKey, title, QString::fromStdString(page->spec.category), QString::number(page->owner.instance.value()));
        return completedTask();
    }, "ui.page:" + page->spec.id);
    if (!effect.hasValue()) { page->finish(); state->pages.erase(page->key()); return Result<PageHandle>::failure(effect.error()); }
    page->effect.emplace(effect.value());
    state->closePage(page);
    return Result<PageHandle>::success(PageHandle(page));
}
/** @brief 查询不创建新页面，不授予导航或清理权限。 */
Result<std::vector<PageSnapshot>> UiPagesService::snapshot() const
{
    auto state = control_.lock();
    if (!state) return Result<std::vector<PageSnapshot>>::success({});
    if (!state->scheduler->isCurrentThread()) return Result<std::vector<PageSnapshot>>::failure(makeError(ErrorCode::WrongThread, "UI snapshot requires owner thread"));
    std::vector<PageSnapshot> result;
    for (const auto& item : state->pages) result.push_back(item.second->snapshot());
    return Result<std::vector<PageSnapshot>>::success(std::move(result));
}
/** @brief effect 的历史 ready 结果不授予当前业务能力。 */
Task<void> PageHandle::ready() const { return control_->effect->ready(); }
/** @brief 同线程也重查 epoch，避免旧句柄在提供者恢复后重新导航。 */
Result<void> PageHandle::navigate() const
{
    auto state = control_->shell.lock();
    if (!state) return Result<void>::failure(makeError(ErrorCode::StaleContext, "page shell expired"));
    if (!state->scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "navigation requires owner thread"));
    return state->navigate(control_);
}
/** @brief 清理不要求业务 guard 仍有效，停用后的 Context 资源也必须能完成撤销。 */
Task<CleanupReport> PageHandle::close() const
{
    auto page = control_;
    if (auto state = page->shell.lock()) {
        if (!state->scheduler->isCurrentThread()) {
            Promise<CleanupReport> rejected;
            rejected.reject(makeError(ErrorCode::WrongThread, "page close requires owner thread"));
            return rejected.task();
        }
        // 清理仍异步排队；业务入口在返回关闭任务之前就应失效。
        page->revoked = true;
    }
    return page->effect->close();
}
/** @brief 已释放句柄仍保留 owner/id，但不保留页面与工厂。 */
Result<PageSnapshot> PageHandle::snapshot() const
{
    auto state = control_->shell.lock();
    if (state && !state->scheduler->isCurrentThread()) return Result<PageSnapshot>::failure(makeError(ErrorCode::WrongThread, "page snapshot requires owner thread"));
    return Result<PageSnapshot>::success(control_->snapshot());
}
/** @brief 外壳资源在构造时即登记 QObject 屏障；第三方全局资源由固定进程生命周期持有。 */
ElaShell::ElaShell(std::shared_ptr<qt::QtScheduler> scheduler, std::string preferencesFile)
{
    if (!qobject_cast<QApplication*>(QCoreApplication::instance()) || !scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen())
        throw std::invalid_argument("ElaShell requires QApplication and open owner scheduler");
    eApp->init();
    control_ = std::make_shared<detail::ShellControl>();
    control_->scheduler = std::move(scheduler);
    auto window = std::make_unique<detail::HostWindow>(control_);
    control_->window = window.get();
    window->setWindowTitle(QString::fromUtf8("Cordis 插件平台"));
    window->resize(1200, 800);
    window->setStackSwitchMode(ElaWindowType::None);
    window->setNavigationBarDisplayMode(ElaNavigationType::Compact);
    window->setIsAllowPageOpenInNewWindow(false);
    window->setIsDefaultClosed(false);
    window->setWindowButtonFlag(ElaAppBarType::RouteBackButtonHint, false);
    window->setWindowButtonFlag(ElaAppBarType::RouteForwardButtonHint, false);
    window->setIsNavigationBarEnable(true);
    window->setNavigationBarWidth(240);
    window->setUserInfoCardTitle(QString::fromUtf8("Cordis"));
    window->setUserInfoCardSubTitle(QString::fromUtf8("插件工作台"));
    window->setUserInfoCardPixmap(ElaIcon::getInstance()->getElaIcon(ElaIconType::PuzzlePiece, 48,
        eTheme->getThemeColor(ElaThemeType::Light, ElaThemeType::PrimaryNormal)).pixmap(64, 64));
    control_->navigation = window->findChild<ElaNavigationBar*>();
    control_->navigationView = window->findChild<QTreeView*>("ElaNavigationView");
    if (!control_->navigation || !control_->navigationView) throw std::runtime_error("pinned Ela navigation unavailable");
    control_->navigation->setIsRouteHistoryEnabled(false);
    auto safe = detail::createWelcomePage();
    window->addPageNode(QString::fromUtf8("概览"), safe, ElaIconType::House);
    control_->safePage = safe;
    control_->safeKey = safe->property("ElaPageKey").toString();
    control_->stack = qobject_cast<QStackedWidget*>(safe->parentWidget());
    if (!control_->stack) throw std::runtime_error("pinned Ela page stack unavailable");
    auto panel = new detail::FixedDiagnosticsTabs(nullptr);
    panel->setObjectName("CordisWorkbenchPanel"); panel->setMinimumHeight(110);
    auto commandList = new ElaListView(panel);
    commandList->setStyleSheet("ElaListView{background-color:transparent;border:0;}");
    commandList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    commandList->setItemHeight(32); commandList->setObjectName("CordisCommandList");
    panel->addTab(commandList, QString::fromUtf8("命令"));
    auto output = new ElaPlainTextEdit(panel);
    output->setObjectName("CordisWorkbenchOutput"); output->setReadOnly(true); output->setMaximumBlockCount(500);
    output->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}");
    panel->addTab(output, QString::fromUtf8("输出"));
    auto header = new QWidget;
    auto headerLayout = new QHBoxLayout(header); headerLayout->setContentsMargins(0, 0, 0, 0);
    control_->tabs = new detail::HostPageTabs(header); control_->tabs->setObjectName("CordisPageTabs");
    headerLayout->setSpacing(8);
    headerLayout->addWidget(control_->tabs, 1);
    auto togglePanel = new ElaToolButton(header);
    togglePanel->setObjectName("CordisTogglePanel"); togglePanel->setText(QString::fromUtf8("面板"));
    togglePanel->setElaIcon(ElaIconType::Terminal);
    togglePanel->setToolButtonStyle(Qt::ToolButtonIconOnly);
    togglePanel->setShortcut(QKeySequence("Ctrl+J"));
    togglePanel->setFixedSize(32, 30);
    togglePanel->setAccessibleName(QString::fromUtf8("切换底部工作区"));
    togglePanel->setToolTip(QString::fromUtf8("切换命令与输出（Ctrl+J）"));
    headerLayout->addWidget(togglePanel);
    window->setCentralCustomWidget(header);
    window->setWorkbenchWidgets(nullptr, panel);
    panel->hide();
    auto statusBar = new ElaStatusBar(window.get());
    auto notification = new ElaText(QString::fromUtf8("就绪"), 12, statusBar);
    notification->setObjectName("CordisNotification");
    notification->setTextFormat(Qt::PlainText);
    notification->setWordWrap(false);
    notification->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    notification->setMinimumWidth(0);
    notification->setMaximumHeight(24);
    statusBar->addWidget(notification, 1);
    window->setStatusBar(statusBar);
    control_->services = detail::createUiServices(control_->scheduler, commandList, notification, output);
    pages_ = std::shared_ptr<UiPagesService>(new UiPagesService(control_));
    commands_ = std::shared_ptr<CommandService>(new CommandService(control_->services));
    notifications_ = std::shared_ptr<NotificationService>(new NotificationService(control_->services));
    control_->management =
        detail::createManagement(window.get(), control_->scheduler, pages_, commands_, notifications_);
    control_->managementPage = detail::managementWidget(control_->management);
    control_->managementKey = "cordis:management";
    window->addFooterNode(QString::fromUtf8("命令与输出"), control_->commandsKey, 0, ElaIconType::Terminal);
    management_ = std::shared_ptr<ManagementController>(new ManagementController(control_->management));
    diagnostics_ = std::shared_ptr<DiagnosticsService>(new DiagnosticsService(control_->management));
    ElaActionCommander::getInstance()->setIsMessageDisplayEnable(false);
    std::weak_ptr<detail::ShellControl> weak = control_;
    std::weak_ptr<DiagnosticsService> weakDiagnostics = diagnostics_;
    control_->workbench = detail::createWorkbench(
        window.get(), control_->tabs, panel, output, togglePanel, control_->safeKey, control_->managementKey,
        {[weak](const QString& key, bool remember) {
             auto state = weak.lock();
             return state ? state->selectKey(key, remember)
                          : Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell expired"));
         },
         [weak] {
             if (auto state = weak.lock(); state && state->open) {
                 detail::ShellControl::NavigationAccess access(*state);
                 detail::refreshManagement(state->management);
             }
         },
         [weak] {
             if (auto state = weak.lock())
                 state->selectEmpty();
         },
         [weak, weakDiagnostics] {
             std::vector<detail::HomePlugin> plugins;
             auto state = weak.lock();
             auto diagnostics = weakDiagnostics.lock();
             if (!state || !state->open || !diagnostics)
                 return plugins;
             auto snapshot = diagnostics->snapshot();
             if (!snapshot.hasValue())
                 return plugins;
             for (const auto& row : snapshot.value().instances) {
                 QString status;
                 switch (row.instance.state) {
                 case InstanceState::Active:
                     status = QString::fromUtf8("运行中");
                     break;
                 case InstanceState::Pending:
                     status = QString::fromUtf8("等待依赖");
                     break;
                 case InstanceState::Loading:
                     status = QString::fromUtf8("启动中");
                     break;
                 case InstanceState::Unloading:
                     status = QString::fromUtf8("清理中");
                     break;
                 case InstanceState::Failed:
                     status = QString::fromUtf8("失败");
                     break;
                 case InstanceState::Disposed:
                     status = QString::fromUtf8("已停止");
                     break;
                 }
                 if (row.instance.state == InstanceState::Unloading && row.instance.failure)
                     status = QString::fromUtf8("清理受阻");
                 detail::HomePlugin item{QString::number(row.instance.instance.value()),
                                         QString::fromStdString(row.name), status};
                 item.status = detail::instanceStatus(row.instance.state, static_cast<bool>(row.instance.failure));
                 item.canStop = row.canStop && !row.canRetry;
                 item.canRetry = row.canRetry;
                 item.canLoad = row.canLoad;
                 item.busy = row.busy;
                 item.enabled = row.instance.state == InstanceState::Active ||
                                row.instance.state == InstanceState::Pending ||
                                row.instance.state == InstanceState::Loading;
                 for (const auto& entry : state->pages) {
                     const auto& page = entry.second;
                     if (page->owner.instance != row.instance.instance || !page->navigation || page->revoked ||
                         page->retiring)
                         continue;
                     item.title = QString::fromStdString(page->spec.title);
                     break;
                 }
                 plugins.push_back(std::move(item));
             }
             if (snapshot.value().configuration) {
                 const auto& source = *snapshot.value().configuration;
                 for (const auto& row : source.entries) {
                     QString status;
                     if (snapshot.value().configurationError)
                         status = QString::fromUtf8("配置状态不可用");
                     else if (!row.hasActual && row.error)
                         status = QString::fromUtf8("状态不可用");
                     else if (!row.enabled)
                         status = QString::fromUtf8("已停用");
                     else if (!row.effectiveEnabled)
                         status = QString::fromUtf8("父项停用");
                     else if (row.waitingForParent)
                         status = QString::fromUtf8("等待父项");
                     else if (!row.hasActual)
                         status = QString::fromUtf8("等待执行");
                     else
                         switch (row.state) {
                         case InstanceState::Active:
                             status = QString::fromUtf8("运行中");
                             break;
                         case InstanceState::Pending:
                             status = QString::fromUtf8("等待依赖");
                             break;
                         case InstanceState::Loading:
                             status = QString::fromUtf8("启动中");
                             break;
                         case InstanceState::Unloading:
                             status = row.error ? QString::fromUtf8("清理受阻") : QString::fromUtf8("清理中");
                             break;
                         case InstanceState::Failed:
                             status = QString::fromUtf8("失败");
                             break;
                         case InstanceState::Disposed:
                             status = QString::fromUtf8("已停止");
                             break;
                         }
                     detail::HomePlugin item{
                         "configuration:" + QString::fromStdString(row.path),
                         QString::fromStdString(row.plugin) + " · " + QString::fromStdString(row.path), status};
                     item.status = detail::instanceStatus(row.state, static_cast<bool>(row.error));
                     if (!row.hasActual || !row.enabled || !row.effectiveEnabled || row.waitingForParent)
                         item.status.icon =
                             !row.enabled || !row.effectiveEnabled ? ElaIconType::CircleStop : ElaIconType::Hourglass;
                     if (snapshot.value().configurationError || (!row.hasActual && row.error))
                         item.status.icon = ElaIconType::TriangleExclamation;
                     item.configuration = true;
                     item.revision = source.revision;
                     item.enabled = row.enabled;
                     item.busy = row.busy;
                     item.canStop = row.canSetEnabled && row.enabled;
                     item.canLoad = row.canSetEnabled && !row.enabled;
                     item.identity =
                         QString::fromUtf8("路径 %1 · %2")
                             .arg(QString::fromStdString(row.path))
                             .arg(row.instance.valid() ? QString::fromUtf8("实例 #%1").arg(row.instance.value())
                                                       : QString::fromUtf8("无实例"));
                     plugins.push_back(std::move(item));
                 }
             }
             return plugins;
         },
         [weak](const std::function<void()>& action) {
             if (auto state = weak.lock(); state && state->open) {
                 detail::ShellControl::NavigationAccess access(*state);
                 action();
             }
         },
         [weak](const QString& id, bool enabled, std::uint64_t revision) {
             auto state = weak.lock();
             if (!state || !state->open)
                 return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell closing"));
             detail::ShellControl::NavigationAccess access(*state);
             if (id.startsWith("configuration:"))
                 return detail::setManagementConfigurationEnabled(state->management, id.mid(14).toStdString(), enabled,
                                                                  revision);
             bool valid = false;
             const auto value = id.toULongLong(&valid);
             if (!valid || value == 0)
                 return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "invalid plugin instance"));
             return detail::setManagementEnabled(state->management, InstanceId(value), enabled);
         },
         [weak](const QString& id) {
             auto state = weak.lock();
             bool valid = false;
             const auto value = id.toULongLong(&valid);
             return state && valid && detail::hideManagementContribution(state->management, InstanceId(value));
         },
         [weak] {
             auto state = weak.lock();
             if (!state || !state->open)
                 return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell closing"));
             detail::ShellControl::NavigationAccess access(*state);
             return detail::showManagementNotifications(state->management);
         },
         [weak](const QString& id, bool configuration) {
             auto state = weak.lock();
             if (!state || !state->open)
                 return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell closing"));
             detail::ShellControl::NavigationAccess access(*state);
             return detail::showManagementDialog(state->management, id, configuration);
         },
         [weak] {
             auto state = weak.lock();
             if (!state || !state->open)
                 return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell closing"));
             detail::ShellControl::NavigationAccess access(*state);
             return detail::showAddPluginDialog(state->management);
         },
         [weak] {
             auto state = weak.lock();
             if (!state || !state->open || !state->configurationHandler)
                 return Result<void>::failure(
                     makeError(ErrorCode::InvalidArgument, "configuration file editor unavailable"));
             detail::ShellControl::NavigationAccess access(*state);
             auto callback = state->configurationHandler;
             return callback();
         },
         [weak, weakDiagnostics] {
             auto state = weak.lock();
             auto diagnostics = weakDiagnostics.lock();
             if (!state || !state->open || !diagnostics)
                 return false;
             auto data = diagnostics->snapshot();
             return data.hasValue() && !data.value().kernel.closing && data.value().configuration &&
                    data.value().configuration->open && data.value().configuration->canEdit &&
                    !data.value().configuration->availablePlugins.empty();
         },
         [weak] {
             auto state = weak.lock();
             return state && state->open && static_cast<bool>(state->configurationHandler);
         }},
        QString::fromStdString(preferencesFile));
    window->addFooterNode(QString::fromUtf8("设置"), detail::workbenchSettings(control_->workbench), control_->settingsKey, 0, ElaIconType::GearComplex);
    window->addFooterNode(QString::fromUtf8("关于"), detail::workbenchAbout(control_->workbench), control_->aboutKey, 0, ElaIconType::CircleInfo);
    for (const auto& entry : {std::make_pair(control_->safeKey, QString::fromUtf8("概览")),
                             std::make_pair(control_->settingsKey, QString::fromUtf8("设置")),
                             std::make_pair(control_->aboutKey, QString::fromUtf8("关于 Cordis"))})
        detail::registerWorkbenchPage(control_->workbench, entry.first, entry.second);
    detail::selectWorkbenchPage(control_->workbench, control_->safeKey);
    QObject::connect(togglePanel, &ElaToolButton::clicked, window.get(), [panel] { panel->setVisible(panel->isHidden()); });
    // ElaWindow 转发信号发生在堆栈切换之前；直接监听其后注册的原生信号，记录实际已切换页面。
    QObject::connect(control_->navigation.data(), &ElaNavigationBar::navigationNodeClicked, window.get(), [weak, panel](ElaNavigationType::NavigationNodeType, const QString& key, bool) {
        if (auto state = weak.lock()) {
            detail::ShellControl::NavigationAccess access(*state);
            if (key == state->commandsKey) panel->setVisible(panel->isHidden());
            else state->syncSelection(key);
        }
    });
    QObject::connect(control_->tabs, &QTabBar::currentChanged, window.get(), [weak](int index) {
        if (auto state = weak.lock()) {
            if (!state->open || index < 0) return;
            detail::ShellControl::NavigationAccess access(*state);
            state->navigation->navigation(state->tabs->tabData(index).toString(), false);
        }
    });
    control_->navigation->setNavigationGuard([weak](const QString& key) {
        auto state = weak.lock();
        if (!state) return false;
        if (key == state->safeKey) return true;
        if (!state->open) return false;
        auto found = state->rows.find(key);
        if (found == state->rows.end()) return true;
        auto page = found->second.lock();
        return page && !page->revoked && !page->retiring && page->guard && page->guard().hasValue();
    });
    // 固定版本只对鼠标发出导航信号；键盘 currentIndex 变化经同一原生入口派发。
    QObject::connect(control_->navigationView->selectionModel(), &QItemSelectionModel::currentChanged, window.get(), [weak](const QModelIndex& index, const QModelIndex&) {
        if (auto state = weak.lock()) {
            if (!state->open || !index.isValid()) return;
            detail::ShellControl::NavigationAccess access(*state);
            QMetaObject::invokeMethod(state->navigationView, "navigationClicked", Qt::DirectConnection, Q_ARG(QModelIndex, index));
        }
    });
    auto gate = new detail::InputGate(window.get(), weak);
    QCoreApplication::instance()->installEventFilter(gate);
    QObject::connect(window.get(), &ElaWindow::closeButtonClicked, window.get(), [weak] { if (auto state = weak.lock()) state->requestClose(); });
    std::unique_ptr<QObject> owned(window.release());
    control_->windowResource.emplace(owned, control_->scheduler);
}
/** @brief 只释放宿主句柄，Qt 对象继续由应用线程的守卫管理。 */
ElaShell::~ElaShell() = default;
/** @brief 共享服务 facade 不暴露 Ela 对象，也不形成服务/窗口所有权环。 */
std::shared_ptr<UiPagesService> ElaShell::pages() const { return pages_; }
/** @brief 命令服务只借用宿主后端，不把第三方控件传给插件。 */
std::shared_ptr<CommandService> ElaShell::commands() const { return commands_; }
/** @brief 通知服务只接收值类型，宿主关闭后拒绝提交。 */
std::shared_ptr<NotificationService> ElaShell::notifications() const { return notifications_; }
/** @brief 普通插件不获得管理控制器，宿主自行决定何时绑定 Kernel。 */
std::shared_ptr<ManagementController> ElaShell::management() const { return management_; }
/** @brief 只读服务的快照不包含 Kernel、Instance 或 Qt 对象。 */
std::shared_ptr<DiagnosticsService> ElaShell::diagnostics() const { return diagnostics_; }
/** @brief 永久管理页不依赖任意插件贡献，缺服务或插件失败时仍可进入。 */
Result<void> ElaShell::showManagement() const
{
    auto control = control_;
    if (!control->scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "management navigation requires owner thread"));
    return control->selectManagement();
}
/** @brief 显示窗口之前验证外壳门禁，关闭后不会重新打开已释放窗口。 */
Result<void> ElaShell::show() const
{
    if (!control_->scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "shell show requires owner thread"));
    if (!control_->open || !control_->window) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell closing"));
    control_->window->show();
    return Result<void>::success();
}
/** @brief 该回调只属于宿主，不作为普通插件的任意命令执行入口。 */
Result<void> ElaShell::onCloseRequested(std::function<void()> callback) const
{
    if (!control_->scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "shell close handler requires owner thread"));
    if (!control_->open) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell closing"));
    if (!callback) return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "empty close handler"));
    control_->closeHandler = std::move(callback);
    return Result<void>::success();
}
/** @brief 平台入口只有宿主可以配置，不把管理回调注册为插件服务。 */
Result<void> ElaShell::onConfigurationRequested(std::function<Result<void>()> callback) const
{
    if (!control_->scheduler->isCurrentThread())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "configuration handler requires owner thread"));
    if (!control_->open)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell closing"));
    if (!callback)
        return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "empty configuration handler"));
    control_->configurationHandler = std::move(callback);
    return Result<void>::success();
}
/** @brief 仅重置尚未进入销毁的关闭请求，允许保存冲突处理后再次正常退出。 */
Result<void> ElaShell::cancelCloseRequest() const
{
    auto control = control_;
    if (!control->scheduler->isCurrentThread())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "cancel close requires owner thread"));
    if (!control->open || control->closing)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "shell already closing"));
    control->closeRequested = false;
    return Result<void>::success();
}
/** @brief 关闭期间不停止 Qt 循环，等待贡献清理完成后才删除窗口树。 */
Task<void> ElaShell::close() const
{
    auto control = control_;
    if (!control->scheduler->isCurrentThread()) return failedTask(makeError(ErrorCode::WrongThread, "shell close requires owner thread"));
    return control->close();
}
} // namespace cordis::ui
