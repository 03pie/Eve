/** @file pages.cpp
 * @brief 在 Ela 外壳中使用宿主导航，按撤销贡献、完整删除和闭包释放的顺序清理。
 */
#include <cordis/ui/ela_shell.hpp>
#include <cordis/qt/object.hpp>
#include "services_p.hpp"
#include <ElaApplication.h>
#include <ElaWindow.h>
#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace cordis::ui::detail {
using Key = std::tuple<InstanceId, Epoch, std::string>;
struct ShellControl;
/** @brief 每个贡献保留独立工厂与 effect，不把 Qt parent 当作清理完成证明。 */
struct PageControl : std::enable_shared_from_this<PageControl> {
    std::weak_ptr<ShellControl> shell;
    ContextIdentity owner;
    PageSpec spec;
    std::function<Result<void>()> guard;
    QPointer<QWidget> widget;
    std::unique_ptr<QObject> detached;
    std::optional<qt::QObjectResource> resource;
    std::optional<EffectToken> effect;
    QTreeWidgetItem* navigationItem = nullptr;
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
    QTreeWidgetItem* item = nullptr;
    std::size_t pages = 0;
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
    QPointer<HostWindow> window;
    std::optional<qt::QObjectResource> windowResource;
    QPointer<QTreeWidget> navigation;
    QPointer<QStackedWidget> stack;
    QPointer<QWidget> safePage;
    QTreeWidgetItem* safeItem = nullptr;
    std::map<QTreeWidgetItem*, std::weak_ptr<PageControl>> rows;
    std::map<Key, std::shared_ptr<PageControl>> pages;
    std::map<Key, Group> groups;
    std::function<void()> closeHandler;
    bool open = true;
    bool closing = false;
    bool starting = false;
    bool windowClosing = false;
    bool failed = false;
    bool closeRequested = false;
    bool servicesClosed = false;
    std::size_t waiting = 0;
    Promise<void> closed;
    /** @brief 永久安全页不属于插件，其生命周期覆盖所有贡献。 */
    void selectSafe()
    {
        if (stack && safePage) stack->setCurrentWidget(safePage);
        if (navigation) navigation->setCurrentItem(safeItem);
    }
    /** @brief 鼠标导航与 PageHandle 使用同一业务门禁，禁止旧代通过界面重新进入。 */
    Result<void> navigate(std::shared_ptr<PageControl> page)
    {
        if (!open || !window || page->revoked || page->retiring || !page->navigation || !page->widget)
            return Result<void>::failure(makeError(ErrorCode::ContextClosing, "page retiring"));
        auto allowed = page->guard();
        if (!allowed.hasValue()) return allowed;
        stack->setCurrentWidget(page->widget);
        navigation->setCurrentItem(page->navigationItem);
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
        if (!closing || starting || failed || windowClosing || !servicesClosed || waiting != 0 || !pages.empty()) return;
        windowClosing = true;
        closeHandler = {};
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
        auto self = shared_from_this();
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
    const bool current = state && state->window && state->stack && navigation && state->stack->currentWidget() == widget;
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
        if (state->stack->currentWidget() == widget) state->selectSafe();
        state->rows.erase(navigationItem);
        delete navigationItem;
        navigationItem = nullptr;
        state->stack->removeWidget(widget);
        navigation = false;
        if (!spec.category.empty()) --state->groups.at(group()).pages;
    }
    auto category = state->groups.find(group());
    if (!spec.category.empty() && category != state->groups.end() && category->second.pages == 0) {
        delete category->second.item;
        state->groups.erase(category);
    }
    if (!widget) { finish(); return closed.task(); }
    widget->hide();
    widget->setParent(nullptr);
    if (!detached) detached.reset(widget.data());
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
        QTreeWidgetItem* categoryItem = nullptr;
        if (!page->spec.category.empty()) {
            auto group = state->groups.find(page->group());
            if (group == state->groups.end()) {
                categoryItem = new QTreeWidgetItem(state->navigation);
                categoryItem->setText(0, QString::fromStdString(page->spec.category));
                categoryItem->setExpanded(true);
                state->groups.emplace(page->group(), detail::Group{categoryItem, 0});
            } else categoryItem = group->second.item;
        }
        page->navigationItem = categoryItem ? new QTreeWidgetItem(categoryItem) : new QTreeWidgetItem(state->navigation);
        page->navigationItem->setText(0, QString::fromStdString(page->spec.title));
        state->rows.emplace(page->navigationItem, page);
        state->stack->addWidget(page->widget);
        page->navigation = true;
        page->detached.release();
        if (categoryItem) ++state->groups.at(page->group()).pages;
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
ElaShell::ElaShell(std::shared_ptr<qt::QtScheduler> scheduler)
{
    if (!qobject_cast<QApplication*>(QCoreApplication::instance()) || !scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen())
        throw std::invalid_argument("ElaShell requires QApplication and open owner scheduler");
    eApp->init();
    control_ = std::make_shared<detail::ShellControl>();
    control_->scheduler = std::move(scheduler);
    auto window = std::make_unique<detail::HostWindow>(control_);
    control_->window = window.get();
    window->setWindowTitle(QString::fromUtf8("Cordis 插件平台"));
    window->resize(960, 640);
    window->setStackSwitchMode(ElaWindowType::None);
    window->setNavigationBarDisplayMode(ElaNavigationType::Maximal);
    window->setIsAllowPageOpenInNewWindow(false);
    window->setIsDefaultClosed(false);
    window->setWindowButtonFlag(ElaAppBarType::RouteBackButtonHint, false);
    window->setWindowButtonFlag(ElaAppBarType::RouteForwardButtonHint, false);
    window->setIsNavigationBarEnable(false);
    auto body = new QWidget;
    auto columns = new QHBoxLayout(body);
    columns->setContentsMargins(12, 12, 12, 12);
    auto navigation = new QTreeWidget(body);
    navigation->setObjectName("CordisPageNavigation");
    navigation->setHeaderHidden(true);
    navigation->setFixedWidth(220);
    navigation->setStyleSheet("QTreeWidget{border:0;background:#f4f6f8;font-size:14px;} QTreeWidget::item{padding:10px;} QTreeWidget::item:selected{background:#dbeafe;color:#1e40af;}");
    auto stack = new QStackedWidget(body);
    control_->navigation = navigation;
    control_->stack = stack;
    auto safe = new QWidget(stack);
    auto layout = new QVBoxLayout(safe);
    auto title = new QLabel(QString::fromUtf8("Cordis 插件平台"), safe);
    title->setStyleSheet("font-size:24px;font-weight:600;");
    layout->addWidget(title);
    layout->addWidget(new QLabel(QString::fromUtf8("从左侧导航打开插件页面。关闭窗口会先等待插件清理完成。"), safe));
    layout->addStretch();
    stack->addWidget(safe);
    control_->safePage = safe;
    control_->safeItem = new QTreeWidgetItem(navigation);
    control_->safeItem->setText(0, QString::fromUtf8("概览"));
    navigation->setCurrentItem(control_->safeItem);
    auto sidebar = new QVBoxLayout;
    sidebar->addWidget(navigation, 2);
    sidebar->addWidget(new QLabel(QString::fromUtf8("插件命令"), body));
    auto commandList = new QListWidget(body);
    commandList->setObjectName("CordisCommandList");
    commandList->setFixedWidth(220);
    sidebar->addWidget(commandList, 1);
    auto content = new QVBoxLayout;
    content->addWidget(stack, 1);
    auto notification = new QLabel(QString::fromUtf8("就绪"), body);
    notification->setObjectName("CordisNotification");
    notification->setWordWrap(true);
    content->addWidget(notification);
    columns->addLayout(sidebar);
    columns->addLayout(content, 1);
    control_->services = detail::createUiServices(control_->scheduler, commandList, notification);
    window->addCentralWidget(body);
    window->setCurrentStackIndex(1);
    std::weak_ptr<detail::ShellControl> weak = control_;
    QObject::connect(navigation, &QTreeWidget::currentItemChanged, window.get(), [weak](QTreeWidgetItem* item, QTreeWidgetItem*) {
        if (auto state = weak.lock()) {
            auto found = state->rows.find(item);
            if (found == state->rows.end()) { if (item == state->safeItem) state->selectSafe(); return; }
            auto page = found->second.lock();
            if (!page || !state->navigate(page).hasValue()) state->selectSafe();
        }
    });
    auto gate = new detail::InputGate(window.get(), weak);
    QCoreApplication::instance()->installEventFilter(gate);
    QObject::connect(window.get(), &ElaWindow::closeButtonClicked, window.get(), [weak] { if (auto state = weak.lock()) state->requestClose(); });
    std::unique_ptr<QObject> owned(window.release());
    control_->windowResource.emplace(owned, control_->scheduler);
    pages_ = std::shared_ptr<UiPagesService>(new UiPagesService(control_));
    commands_ = std::shared_ptr<CommandService>(new CommandService(control_->services));
    notifications_ = std::shared_ptr<NotificationService>(new NotificationService(control_->services));
}
/** @brief 只释放宿主句柄，Qt 对象继续由应用线程的守卫管理。 */
ElaShell::~ElaShell() = default;
/** @brief 共享服务 facade 不暴露 Ela 对象，也不形成服务/窗口所有权环。 */
std::shared_ptr<UiPagesService> ElaShell::pages() const { return pages_; }
/** @brief 命令服务只借用宿主后端，不把第三方控件传给插件。 */
std::shared_ptr<CommandService> ElaShell::commands() const { return commands_; }
/** @brief 通知服务只接收值类型，宿主关闭后拒绝提交。 */
std::shared_ptr<NotificationService> ElaShell::notifications() const { return notifications_; }
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
/** @brief 关闭期间不停止 Qt 循环，等待贡献清理完成后才删除窗口树。 */
Task<void> ElaShell::close() const
{
    auto control = control_;
    if (!control->scheduler->isCurrentThread()) return failedTask(makeError(ErrorCode::WrongThread, "shell close requires owner thread"));
    return control->close();
}
} // namespace cordis::ui
