/** @file services.cpp
 * @brief 管理命令调用保留及 effect 屏障；通知展示只持宿主值数据。
 */
#include "notifications_p.hpp"
#include "services_p.hpp"
#include "panel_p.hpp"
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <atomic>
#include <eve/ui/commands.hpp>
#include <eve/ui/notifications.hpp>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace eve::ui::detail {
using CommandKey = std::tuple<InstanceId, Epoch, std::string>;
/** @brief 原命令模型同时按搜索和宿主纯呈现门禁过滤，不求值插件谓词。 */
class CommandFilter final : public QSortFilterProxyModel {
public:
    explicit CommandFilter(QObject* parent) : QSortFilterProxyModel(parent) {}
    std::function<bool(const QModelIndex&)> presentation;
    void refresh() { invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override
    {
        return QSortFilterProxyModel::filterAcceptsRow(row, parent) &&
               (!presentation || presentation(sourceModel()->index(row, 0, parent)));
    }
};
/** @brief 控制块保留回调直到已接受调用及谓词全部返回，不把列表行当作插件资源。 */
struct CommandControl : std::enable_shared_from_this<CommandControl> {
    std::weak_ptr<ServicesControl> host;
    std::shared_ptr<qt::QtScheduler> scheduler;
    ContextIdentity owner;
    CommandSpec spec;
    std::function<Result<void>()> guard;
    std::function<bool()> presentation;
    std::shared_ptr<ScheduledJob> observation;
    std::optional<EffectToken> effect;
    QStandardItem* row = nullptr;
    bool revoked = false;
    bool retiring = false;
    bool draining = false;
    bool evaluating = false;
    bool watched = false;
    std::size_t calls = 0;
    std::atomic_bool blocked{false};
    Promise<void> drained;
    /** @brief 相同命令名在不同实例及代次互不冲突。 */
    CommandKey key() const { return {owner.instance, owner.epoch, spec.id}; }
    /** @brief 查询不执行谓词，不复制插件闭包。 */
    CommandSnapshot snapshot() const { return {owner, spec.id, spec.title, revoked, calls, blocked.load()}; }
    /** @brief 模型仅查询原发布事实与宿主值，不进入插件 enabled 或业务 guard。 */
    bool presentable() const { return !revoked && !retiring && !blocked.load() && presentation && presentation(); }
    /** @brief 业务入口先检查线程、宿主及本代门禁。 */
    Result<void> allowed() const;
    /** @brief 谓词执行期间占用调用保留，重入退役不能提前销毁自身闭包。 */
    Result<bool> enabled();
    /** @brief 调用完成后尝试释放插件闭包，捕获析构返回前不报告静止。 */
    void checkDrained()
    {
        if (!retiring || calls != 0 || draining) return;
        draining = true;
        spec.execute = {};
        spec.enabled = {};
        guard = {};
        presentation = {};
        observation.reset();
        drained.resolve();
    }
    /** @brief 同步禁止新执行，清理时移除宿主行并等待调用。 */
    Task<void> retire();
    /** @brief 统一由 owner 排队启动，即使回调返回已完成任务也不内联完成。 */
    Task<void> execute();
};
/** @brief 命令记录归本后端，通知控制块独立拥有候选值及历史。 */
struct ServicesControl : std::enable_shared_from_this<ServicesControl> {
    std::shared_ptr<qt::QtScheduler> scheduler;
    QPointer<QListView> commands;
    QStandardItemModel* model = nullptr;
    std::shared_ptr<NotificationControl> notifications;
    std::shared_ptr<OutputControl> logs;
    WindowAccess windowAccess;
    QPointer<QLabel> commandResult;
    QPointer<CommandFilter> filter;
    std::map<QStandardItem*, QPointer<QAction>> platformCommands;
    std::uint64_t commandSequence = 0;
    /** @brief 运行结果由命令面板承接，输出通道另保留完整执行记录。 */
    void commandStatus(const QString& text)
    {
        if (windowAccess) {
            auto access = windowAccess;
            access([this, text] { if (open && commandResult) commandResult->setText(text); });
        }
    }
    void activate(const QModelIndex& index);
    /** @brief 刷新原模型的投影；重入关闭时窗口访问栈仍保护控件。 */
    void refreshPresentation()
    {
        if (!open || !windowAccess)
            return;
        auto self = shared_from_this();
        auto access = windowAccess;
        access([self] {
            if (!self->open || !self->filter)
                return;
            self->filter->refresh();
            if (self->open && self->commands && self->filter && !self->commands->currentIndex().isValid())
                self->commands->setCurrentIndex(self->filter->index(0, 0));
        });
    }
    std::map<CommandKey, std::shared_ptr<CommandControl>> records;
    std::map<QStandardItem*, std::weak_ptr<CommandControl>> rows;
    bool open = true;
    bool starting = false;
    bool closing = false;
    bool failed = false;
    std::size_t waiting = 0;
    Promise<void> closed;
    /** @brief 状态和历史查询只要求 owner，业务另要求开放 scheduler 和完整展示控件。 */
    Result<void> thread() const
    {
        if (!scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "UI services require owner thread"));
        return Result<void>::success();
    }
    /** @brief 外壳关门或命令控件消失后禁止新贡献和调用。 */
    Result<void> business() const
    {
        auto checked = thread();
        if (!checked.hasValue()) return checked;
        if (!scheduler->isOpen()) return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "UI scheduler closed"));
        if (!open || !commands)
            return Result<void>::failure(makeError(ErrorCode::ContextClosing, "UI services closing"));
        return Result<void>::success();
    }
    /** @brief 贡献记录保留到 effect 完整报告，不能只凭内部 drained 完成关闭。 */
    void checkClosed()
    {
        if (closing && !starting && !failed && waiting == 0 && records.empty()) closed.resolve();
    }
    /** @brief 外壳关闭观察每个命令 effect，错误包含原始清理原因。 */
    void closeRecord(const std::shared_ptr<CommandControl>& record)
    {
        if (!closing || record->watched || !record->effect) return;
        record->watched = true;
        record->revoked = true;
        ++waiting;
        auto self = shared_from_this();
        eve::detail::TaskAccess::observe(record->effect->close(), [self](Task<CleanupReport>::Snapshot result) {
            --self->waiting;
            if (!result->hasValue() || !result->value().quiescent) {
                self->failed = true;
                auto error = makeError(ErrorCode::CleanupFailed, "command effect did not quiesce");
                if (result->hasValue()) error.causes = result->value().errors;
                else error.causes.push_back(result->error());
                self->closed.reject(std::move(error));
            }
            self->checkClosed();
        });
    }
};
/** @brief 每个已接受调用自保留；投递阻断时保留资源并发布原子诊断。 */
struct CommandCall : std::enable_shared_from_this<CommandCall> {
    std::shared_ptr<CommandControl> record;
    std::shared_ptr<CommandCall> pin;
    Promise<void> result;
    /** @brief 源结果只在 owner 消费；先释放调用占用，再完成返回任务。 */
    void finish(const Result<void>& outcome)
    {
        auto held = std::move(record);
        --held->calls;
        held->checkDrained();
        if (outcome.hasValue()) result.resolve();
        else result.reject(outcome.error());
        pin.reset();
    }
    /** @brief 即使 worker 完成源任务，也只由原子标志报告投递错误。 */
    void watch(const Task<void>& receipt)
    {
        std::weak_ptr<CommandControl> weak = record;
        eve::detail::TaskAccess::observe(receipt, [weak](Task<void>::Snapshot outcome) {
            if (!outcome->hasValue()) if (auto owner = weak.lock()) owner->blocked.store(true);
        });
    }
    /** @brief 在真正执行前求值 enabled 并重查门禁，异常不穿过 Qt。 */
    void run()
    {
        auto enabled = record->enabled();
        if (!enabled.hasValue()) { finish(Result<void>::failure(enabled.error())); return; }
        if (!enabled.value()) { finish(Result<void>::failure(makeError(ErrorCode::InvalidArgument, "command disabled"))); return; }
        auto self = shared_from_this();
        try {
            auto source = record->spec.execute();
            watch(source.thenOn(record->scheduler, [self](const Result<void>& outcome) { self->finish(outcome); }));
        } catch (const std::exception& error) {
            finish(Result<void>::failure(makeError(ErrorCode::CallbackFailed, error.what())));
        } catch (...) {
            finish(Result<void>::failure(makeError(ErrorCode::CallbackFailed, "command callback threw")));
        }
    }
};
/** @brief host 弱引用过期后，历史句柄不能重新获得业务能力。 */
Result<void> CommandControl::allowed() const
{
    if (!scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "command requires owner thread"));
    auto state = host.lock();
    if (!state) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "command host expired"));
    auto checked = state->business();
    if (!checked.hasValue()) return checked;
    if (revoked || !guard) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "command revoked"));
    if (!presentable())
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "command is not externally published"));
    return guard();
}
/** @brief 谓词重入关闭后即使返回 true，也不得进入 execute 回调。 */
Result<bool> CommandControl::enabled()
{
    auto checked = allowed();
    if (!checked.hasValue()) return Result<bool>::failure(checked.error());
    if (evaluating) return Result<bool>::failure(makeError(ErrorCode::InvalidArgument, "recursive command predicate"));
    ++calls;
    evaluating = true;
    auto result = [&]() -> Result<bool> {
        try { return Result<bool>::success(!spec.enabled || spec.enabled()); }
        catch (const std::exception& error) { return Result<bool>::failure(makeError(ErrorCode::CallbackFailed, error.what())); }
        catch (...) { return Result<bool>::failure(makeError(ErrorCode::CallbackFailed, "command predicate threw")); }
    }();
    auto rechecked = allowed();
    evaluating = false;
    --calls;
    checkDrained();
    return rechecked.hasValue() ? std::move(result) : Result<bool>::failure(rechecked.error());
}
/** @brief 先撤销列表项，等待全部调用后释放插件回调；effect 完成后才删除宿主索引。 */
Task<void> CommandControl::retire()
{
    if (retiring) return drained.task();
    revoked = true;
    retiring = true;
    observation.reset();
    std::weak_ptr<CommandControl> weak = shared_from_this();
    eve::detail::TaskAccess::observe(effect->close(), [weak](Task<CleanupReport>::Snapshot report) {
        if (!report->hasValue() || !report->value().quiescent) return;
        if (auto record = weak.lock()) if (auto state = record->host.lock()) {
            state->records.erase(record->key());
            state->checkClosed();
        }
    });
    if (auto state = host.lock()) {
        state->rows.erase(row);
        if (state->commands && row) state->model->removeRow(row->row());
    }
    row = nullptr;
    checkDrained();
    return drained.task();
}
/** @brief 接受即保留调用；拒绝的初始投递可同步释放，已接受后丢弃则保守保留。 */
Task<void> CommandControl::execute()
{
    auto checked = allowed();
    if (!checked.hasValue()) return failedTask(checked.error());
    auto call = std::make_shared<CommandCall>();
    call->record = shared_from_this();
    call->pin = call;
    ++calls;
    auto result = call->result.task();
    bool accepted = false;
    try {
        accepted = scheduler->post({[call] { call->run(); }, [call] { call->record->blocked.store(true); }});
    } catch (...) {}
    if (!accepted) call->finish(Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "command dispatch rejected")));
    return result;
}
/** @brief 过滤后必须映射回源记录，防止同名或移动行调用错误实例。 */
void ServicesControl::activate(const QModelIndex& index)
{
    if (!open || !index.isValid()) return;
    const auto source = filter ? filter->mapToSource(index) : index;
    auto item = model->itemFromIndex(source);
    const auto platform = platformCommands.find(item);
    if (platform != platformCommands.end()) {
        QPointer<QAction> action = platform->second;
        if (!action || !action->isEnabled() || !action->isVisible()) {
            commandStatus(QString::fromUtf8("当前命令不可用。")); return;
        }
        const auto title = action->text();
        const auto execution = ++commandSequence;
        dismissCommandPanel(commands);
        if (!open || !action)
            return;
        action->trigger();
        if (!open) return;
        if (logs) (void)appendOutput(logs, {}, "platform", (QString::fromUtf8("已调用平台命令：") + title).toStdString());
        if (commandSequence == execution) commandStatus(QString::fromUtf8("已调用：") + title);
        return;
    }
    auto found = rows.find(item);
    auto record = found == rows.end() ? nullptr : found->second.lock();
    if (!record) return;
    // 缓存的 QModelIndex 也不能在未发布时泄露标题、运行状态或输出记录。
    if (!record->allowed().hasValue())
        return;
    const auto identity = record->owner;
    const auto title = record->spec.title;
    const auto execution = ++commandSequence;
    dismissCommandPanel(commands);
    if (!open)
        return;
    commandStatus(QString::fromUtf8("运行中：") + QString::fromStdString(title));
    if (logs) (void)appendOutput(logs, identity, "命令", "开始执行：" + title);
    std::weak_ptr<ServicesControl> weak = shared_from_this();
    record->execute().thenOn(scheduler, [weak, identity, title, execution](const Result<void>& result) {
        auto host = weak.lock(); if (!host || !host->open) return;
        const auto text = result.hasValue() ? "执行完成：" + title : "执行失败：" + title + " · " + (result.error().message.empty() ? "未提供错误说明" : result.error().message);
        if (host->commandSequence == execution) host->commandStatus(QString::fromStdString(text));
        if (host->logs) (void)appendOutput(host->logs, identity, "命令", text, result.hasValue() ? OutputLevel::Info : OutputLevel::Error);
        if (!result.hasValue()) {
            auto message = result.error().message;
            if (message.size() > NotificationService::textLimit) {
                auto end = NotificationService::textLimit;
                while (end && (static_cast<unsigned char>(message[end]) & 0xc0) == 0x80)
                    --end;
                message.resize(end);
            }
            if (message.empty()) message = "命令执行失败";
            (void)appendNotification(host->notifications, identity,
                                     {NotificationLevel::Error, std::move(message), result.error().operation});
        }
    });
}
/** @brief 只有宿主列表的激活信号可以查找内部命令；连接不持任何插件回调。 */
std::shared_ptr<ServicesControl> createUiServices(std::shared_ptr<qt::QtScheduler> scheduler, QListView* commands,
                                                  QLabel* notification)
{
    auto state = std::make_shared<ServicesControl>();
    state->scheduler = std::move(scheduler);
    state->commands = commands;
    state->model = new QStandardItemModel(commands);
    auto filter = new CommandFilter(commands);
    state->filter = filter;
    filter->setSourceModel(state->model);
    filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    filter->setFilterRole(Qt::UserRole + 1);
    commands->setModel(filter);
    state->notifications = createNotifications(state->scheduler, notification);
    std::weak_ptr<ServicesControl> weak = state;
    filter->presentation = [weak](const QModelIndex& source) {
        auto host = weak.lock();
        if (!host || !host->open)
            return false;
        auto item = host->model->itemFromIndex(source);
        if (host->platformCommands.count(item))
            return true;
        const auto found = host->rows.find(item);
        auto record = found == host->rows.end() ? nullptr : found->second.lock();
        return record && record->presentable();
    };
    QObject::connect(commands, &QListView::activated, commands, [weak](const QModelIndex& index) {
        if (auto owner = weak.lock()) owner->activate(index);
    });
    return state;
}
void configureNotificationCenter(const std::shared_ptr<ServicesControl>& state, QWidget* window,
                                 std::function<QString(InstanceId)> sourceName)
{
    installNotificationCenter(state->notifications, window, std::move(sourceName));
}
Result<void> showNotificationCenter(const std::shared_ptr<ServicesControl>& state, QWidget* origin)
{
    return toggleNotificationCenter(state->notifications, origin);
}
void setNotificationArchive(const std::shared_ptr<ServicesControl>& state, std::shared_ptr<RuntimeLogArchive> archive)
{
    watchNotificationArchive(state->notifications, std::move(archive));
}
void setNotificationMuted(const std::shared_ptr<ServicesControl>& state, bool muted)
{
    muteNotifications(state->notifications, muted);
}
void configureCommandPanel(const std::shared_ptr<ServicesControl>& state, QWidget* page,
    const std::vector<QAction*>& actions, std::shared_ptr<OutputControl> output, WindowAccess access)
{
    state->logs = std::move(output); state->windowAccess = std::move(access);
    configureNotifications(state->notifications, state->logs, state->windowAccess);
    state->commandResult = page->findChild<QLabel*>("EveCommandResult");
    std::weak_ptr<ServicesControl> weak = state;
    for (auto action : actions) {
        if (!action || action->isSeparator() || !action->isVisible()) continue;
        auto row = new QStandardItem;
        row->setEditable(false); state->platformCommands.emplace(row, action);
        const auto sync = [weak, row, action] {
            if (auto host = weak.lock(); host && host->open) {
                row->setText(action->text() + QString::fromUtf8("  · 平台"));
                row->setData(action->text() + " " + action->objectName() + " platform 平台", Qt::UserRole + 1);
                row->setToolTip(action->objectName() + "\n" + action->toolTip());
                row->setEnabled(action->isEnabled() && action->isVisible());
            }
        };
        state->model->appendRow(row); sync();
        QObject::connect(action, &QAction::changed, page, sync);
    }
    auto search = page->findChild<QLineEdit*>("EveCommandFilter");
    const auto select = [weak] {
        if (auto host = weak.lock(); host && host->open && host->commands && host->filter) {
            if (!host->commands->currentIndex().isValid()) host->commands->setCurrentIndex(host->filter->index(0, 0));
        }
    };
    QObject::connect(search, &QLineEdit::textChanged, page, [weak, select](const QString& text) {
        if (auto host = weak.lock(); host && host->open && host->filter) {
            host->filter->setFilterFixedString(text.trimmed()); select();
            host->commandStatus(host->filter->rowCount() ? QString::fromUtf8("%1 个匹配命令；按回车执行。").arg(host->filter->rowCount()) : QString::fromUtf8("没有匹配的命令。"));
        }
    });
    const auto run = [weak, select] {
        select(); if (auto host = weak.lock(); host && host->commands) host->activate(host->commands->currentIndex());
    };
    QObject::connect(search, &QLineEdit::returnPressed, page, run);
    for (const auto key : {Qt::Key_Down, Qt::Key_Up}) {
        auto shortcut = new QShortcut(QKeySequence(key), search); shortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(shortcut, &QShortcut::activated, page, [weak, key] {
            if (auto host = weak.lock(); host && host->open && host->commands && host->filter) {
                auto row = host->commands->currentIndex().row() + (key == Qt::Key_Down ? 1 : -1);
                if (row >= 0 && row < host->filter->rowCount()) host->commands->setCurrentIndex(host->filter->index(row, 0));
            }
        });
    }
    if (auto button = page->findChild<QPushButton*>("EveRunCommand")) QObject::connect(button, &QPushButton::clicked, page, run);
    select();
}
/** @brief 先结算原输出，再结算通知镜像；外壳已关呈现门时不触发控件。 */
void settleUiValues(const std::shared_ptr<ServicesControl>& control)
{
    settleOutput(control->logs, false);
    settleNotifications(control->notifications, false);
}
/** @brief 外壳关闭先统一关门，使用副本请求所有 effect，容忍同步完成和重入。 */
Task<void> closeUiServices(const std::shared_ptr<ServicesControl>& control)
{
    if (control->closing) return control->closed.task();
    control->open = false;
    control->closing = true;
    control->starting = true;
    closeNotifications(control->notifications);
    std::vector<std::shared_ptr<CommandControl>> records;
    for (const auto& item : control->records) records.push_back(item.second);
    for (const auto& record : records) control->closeRecord(record);
    control->starting = false;
    control->checkClosed();
    return control->closed.task();
}
} // namespace eve::ui::detail

namespace eve::ui {
/** @brief 列表初始化纳入 effect setup，Qt 插入信号重入时清理也必须等初始化返回。 */
Result<CommandHandle> CommandService::add(const Context& owner, CommandSpec spec) const
{
    auto host = control_.lock();
    if (!host) return Result<CommandHandle>::failure(makeError(ErrorCode::ContextClosing, "command host expired"));
    auto checked = host->business();
    if (!checked.hasValue()) return Result<CommandHandle>::failure(checked.error());
    const auto context = owner;
    auto identity = context.identity();
    if (!identity.hasValue()) return Result<CommandHandle>::failure(identity.error());
    auto guard = context.eventGuard(host->scheduler);
    if (!guard.hasValue()) return Result<CommandHandle>::failure(guard.error());
    if (spec.id.empty() || spec.title.empty() || !spec.execute)
        return Result<CommandHandle>::failure(makeError(ErrorCode::InvalidArgument, "command id, title and callback required"));
    auto record = std::make_shared<detail::CommandControl>();
    record->host = host;
    record->scheduler = host->scheduler;
    record->owner = identity.value();
    record->spec = std::move(spec);
    record->guard = std::move(guard.value());
    std::weak_ptr<detail::CommandControl> weak = record;
    ScheduledJob changed{[weak] {
                             auto record = weak.lock();
                             if (!record || record->retiring || record->revoked)
                                 return;
                             if (record->presentation && record->presentation())
                                 record->observation.reset();
                             if (auto host = record->host.lock())
                                 host->refreshPresentation();
                         },
                         [weak] {
                             if (auto record = weak.lock())
                                 record->blocked.store(true);
                         }};
    auto presentation = context.presentation(host->scheduler, std::move(changed));
    if (!presentation.hasValue())
        return Result<CommandHandle>::failure(presentation.error());
    record->presentation = std::move(presentation.value().visible);
    record->observation = std::move(presentation.value().observation);
    // std::function 的移动可执行用户代码；预留索引及创建 effect 前重查全部门禁。
    auto stillOpen = host->business();
    if (!stillOpen.hasValue()) return Result<CommandHandle>::failure(stillOpen.error());
    auto stillCurrent = record->guard();
    if (!stillCurrent.hasValue()) return Result<CommandHandle>::failure(stillCurrent.error());
    if (host->records.count(record->key())) return Result<CommandHandle>::failure(makeError(ErrorCode::ServiceConflict, "duplicate command contribution"));
    host->records.emplace(record->key(), record);
    auto effect = context.effect([record, host](EffectSetupContext setup) {
        auto result = setup.addCleanup([record] { return record->retire(); });
        if (!result.hasValue()) return failedTask(result.error());
        record->row = new QStandardItem(QString::fromStdString(record->spec.title));
        record->row->setEditable(false);
        record->row->setText(QString::fromStdString(record->spec.title) + QString::fromUtf8("  · 插件 #%1 / 代 %2").arg(record->owner.instance.value()).arg(record->owner.epoch));
        record->row->setData(QString::fromStdString(record->spec.title + " " + record->spec.id) + QString(" %1 %2").arg(record->owner.instance.value()).arg(record->owner.epoch), Qt::UserRole + 1);
        record->row->setToolTip(QString::fromUtf8("双击或按回车执行"));
        host->rows.emplace(record->row, record);
        host->model->appendRow(record->row);
        return completedTask();
    }, "ui.command:" + record->spec.id);
    if (!effect.hasValue()) { host->records.erase(record->key()); return Result<CommandHandle>::failure(effect.error()); }
    record->effect = effect.value();
    host->closeRecord(record);
    return Result<CommandHandle>::success(CommandHandle(record));
}
/** @brief 快照不触发插件代码，关闭后仍可定位保留的调用。 */
Result<std::vector<CommandSnapshot>> CommandService::snapshot() const
{
    auto host = control_.lock();
    if (!host) return Result<std::vector<CommandSnapshot>>::success({});
    auto checked = host->thread();
    if (!checked.hasValue()) return Result<std::vector<CommandSnapshot>>::failure(checked.error());
    std::vector<CommandSnapshot> result;
    for (const auto& item : host->records) result.push_back(item.second->snapshot());
    return Result<std::vector<CommandSnapshot>>::success(std::move(result));
}
/** @brief ready 只观察历史结果，不重新开放命令。 */
Task<void> CommandHandle::ready() const { return control_->effect->ready(); }
/** @brief 本地保留控制块，允许谓词释放外部最后一个句柄。 */
Result<bool> CommandHandle::enabled() const { auto held = control_; return held->enabled(); }
/** @brief 执行不借用 facade 生命周期，真正回调由宿主队列启动。 */
Task<void> CommandHandle::execute() const { auto held = control_; return held->execute(); }
/** @brief 错误线程不得更改 revoked；异步关闭本身不依赖业务 Context 仍有效。 */
Task<CleanupReport> CommandHandle::close() const
{
    auto held = control_;
    if (!held->scheduler->isCurrentThread()) return held->effect->close();
    held->revoked = true;
    return held->effect->close();
}
/** @brief 快照只在 owner 读取，blocked 使用原子值接受投递错误诊断。 */
Result<CommandSnapshot> CommandHandle::snapshot() const
{
    if (!control_->scheduler->isCurrentThread()) return Result<CommandSnapshot>::failure(makeError(ErrorCode::WrongThread, "command snapshot requires owner thread"));
    return Result<CommandSnapshot>::success(control_->snapshot());
}
/** @brief 调用方 Context 只用于当次校验，通知记录不保存它或插件服务租约。 */
Result<std::uint64_t> NotificationService::post(const Context& owner, Notification notification) const
{
    auto host = control_.lock();
    if (!host) return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "notification host expired"));
    auto checked = host->thread();
    if (!checked.hasValue()) return Result<std::uint64_t>::failure(checked.error());
    if (!host->open)
        return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "UI services closing"));
    auto identity = owner.identity();
    if (!identity.hasValue()) return Result<std::uint64_t>::failure(identity.error());
    auto guard = owner.eventGuard(host->scheduler);
    if (!guard.hasValue()) return Result<std::uint64_t>::failure(guard.error());
    const auto status = detail::prepareNotification(host->notifications, notification);
    if (!status.hasValue())
        return Result<std::uint64_t>::failure(status.error());
    auto source = detail::notificationSource(host->notifications, identity.value());
    if (!source->publication) {
        std::weak_ptr<detail::NotificationControl> weak = host->notifications;
        auto presentation = owner.presentation(host->scheduler, source->observe([weak] {
            if (auto state = weak.lock())
                detail::settleNotifications(state);
        }));
        if (!presentation.hasValue())
            return Result<std::uint64_t>::failure(presentation.error());
        source->publication = std::move(presentation.value().publication);
        source->observation = std::move(presentation.value().observation);
    }
    return detail::submitNotification(host->notifications, identity.value(), std::move(notification),
                                      std::move(source));
}
/** @brief 返回值复制历史，不附带任何插件对象或观察者回调。 */
Result<std::vector<NotificationSnapshot>> NotificationService::snapshot() const
{
    auto host = control_.lock();
    if (!host) return Result<std::vector<NotificationSnapshot>>::success({});
    return detail::notificationSnapshot(host->notifications);
}
} // namespace eve::ui
