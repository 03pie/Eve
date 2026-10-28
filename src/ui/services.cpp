/** @file services.cpp
 * @brief 管理命令调用保留及 effect 屏障；通知展示只持宿主值数据。
 */
#include "services_p.hpp"
#include <cordis/ui/commands.hpp>
#include <cordis/ui/notifications.hpp>
#include <QLabel>
#include <QPlainTextEdit>
#include <QListView>
#include <QStandardItemModel>
#include <QPointer>
#include <atomic>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace cordis::ui::detail {
using CommandKey = std::tuple<InstanceId, Epoch, std::string>;
/** @brief 控制块保留回调直到已接受调用及谓词全部返回，不把列表行当作插件资源。 */
struct CommandControl : std::enable_shared_from_this<CommandControl> {
    std::weak_ptr<ServicesControl> host;
    std::shared_ptr<qt::QtScheduler> scheduler;
    ContextIdentity owner;
    CommandSpec spec;
    std::function<Result<void>()> guard;
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
        drained.resolve();
    }
    /** @brief 同步禁止新执行，清理时移除宿主行并等待调用。 */
    Task<void> retire();
    /** @brief 统一由 owner 排队启动，即使回调返回已完成任务也不内联完成。 */
    Task<void> execute();
};
/** @brief 宿主 UI 后端仅由应用线程写入，通知序号和历史都是宿主值。 */
struct ServicesControl : std::enable_shared_from_this<ServicesControl> {
    std::shared_ptr<qt::QtScheduler> scheduler;
    QPointer<QListView> commands;
    QStandardItemModel* model = nullptr;
    QPointer<QLabel> notification;
    QPointer<QPlainTextEdit> output;
    std::map<CommandKey, std::shared_ptr<CommandControl>> records;
    std::map<QStandardItem*, std::weak_ptr<CommandControl>> rows;
    std::deque<NotificationSnapshot> history;
    std::uint64_t sequence = 0;
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
    /** @brief 外壳关门或展示控件消失后禁止新贡献、通知和调用。 */
    Result<void> business() const
    {
        auto checked = thread();
        if (!checked.hasValue()) return checked;
        if (!scheduler->isOpen()) return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "UI scheduler closed"));
        if (!open || !commands || !notification) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "UI services closing"));
        return Result<void>::success();
    }
    /** @brief 校验后保存纯值，并以纯文本更新宿主状态区；不保留 Context 或回调。 */
    Result<std::uint64_t> append(ContextIdentity owner, Notification value)
    {
        auto checked = business();
        if (!checked.hasValue()) return Result<std::uint64_t>::failure(checked.error());
        if (value.text.empty() || value.text.size() > NotificationService::textLimit ||
            (value.level != NotificationLevel::Info && value.level != NotificationLevel::Warning && value.level != NotificationLevel::Error) ||
            sequence == std::numeric_limits<std::uint64_t>::max())
            return Result<std::uint64_t>::failure(makeError(ErrorCode::InvalidArgument, "invalid notification or exhausted sequence"));
        history.push_back({sequence + 1, owner, std::move(value)});
        ++sequence;
        // 输出控件会同步发出 textChanged；重入提交不能改变本次提交的返回序号。
        const auto submittedSequence = sequence;
        if (history.size() > NotificationService::historyLimit) history.pop_front();
        const auto& latest = history.back().notification;
        const QString prefix = latest.level == NotificationLevel::Error ? QString::fromUtf8("错误：") :
            latest.level == NotificationLevel::Warning ? QString::fromUtf8("提示：") : QString::fromUtf8("消息：");
        const auto text = prefix + QString::fromStdString(latest.text);
        notification->setText(text);
        if (notification)
            notification->setToolTip(text);
        if (output) output->appendPlainText(text);
        return Result<std::uint64_t>::success(submittedSequence);
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
        cordis::detail::TaskAccess::observe(record->effect->close(), [self](Task<CleanupReport>::Snapshot result) {
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
        cordis::detail::TaskAccess::observe(receipt, [weak](Task<void>::Snapshot outcome) {
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
    std::weak_ptr<CommandControl> weak = shared_from_this();
    cordis::detail::TaskAccess::observe(effect->close(), [weak](Task<CleanupReport>::Snapshot report) {
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
/** @brief 只有宿主列表的激活信号可以查找内部命令；连接不持任何插件回调。 */
std::shared_ptr<ServicesControl> createUiServices(std::shared_ptr<qt::QtScheduler> scheduler,
                                               QListView* commands, QLabel* notification, QPlainTextEdit* output)
{
    auto state = std::make_shared<ServicesControl>();
    state->scheduler = std::move(scheduler);
    state->commands = commands;
    state->model = new QStandardItemModel(commands);
    commands->setModel(state->model);
    state->notification = notification;
    state->output = output;
    notification->setTextFormat(Qt::PlainText);
    std::weak_ptr<ServicesControl> weak = state;
    QObject::connect(commands, &QListView::activated, commands, [weak](const QModelIndex& index) {
        auto owner = weak.lock();
        if (!owner) return;
        auto found = owner->rows.find(owner->model->itemFromIndex(index));
        if (found == owner->rows.end()) return;
        auto record = found->second.lock();
        if (!record) return;
        const auto identity = record->owner;
        record->execute().thenOn(owner->scheduler, [weak, identity](const Result<void>& result) {
            if (!result.hasValue()) if (auto host = weak.lock()) {
                auto text = result.error().message.substr(0, NotificationService::textLimit);
                if (text.empty()) text = "命令执行失败";
                host->append(identity, {NotificationLevel::Error, std::move(text), result.error().operation});
            }
        });
    });
    return state;
}
/** @brief 外壳关闭先统一关门，使用副本请求所有 effect，容忍同步完成和重入。 */
Task<void> closeUiServices(const std::shared_ptr<ServicesControl>& control)
{
    if (control->closing) return control->closed.task();
    control->open = false;
    control->closing = true;
    control->starting = true;
    std::vector<std::shared_ptr<CommandControl>> records;
    for (const auto& item : control->records) records.push_back(item.second);
    for (const auto& record : records) control->closeRecord(record);
    control->starting = false;
    control->checkClosed();
    return control->closed.task();
}
} // namespace cordis::ui::detail

namespace cordis::ui {
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
    auto checked = host->business();
    if (!checked.hasValue()) return Result<std::uint64_t>::failure(checked.error());
    auto identity = owner.identity();
    if (!identity.hasValue()) return Result<std::uint64_t>::failure(identity.error());
    auto guard = owner.eventGuard(host->scheduler);
    if (!guard.hasValue()) return Result<std::uint64_t>::failure(guard.error());
    return host->append(identity.value(), std::move(notification));
}
/** @brief 返回值复制历史，不附带任何插件对象或观察者回调。 */
Result<std::vector<NotificationSnapshot>> NotificationService::snapshot() const
{
    auto host = control_.lock();
    if (!host) return Result<std::vector<NotificationSnapshot>>::success({});
    auto checked = host->thread();
    if (!checked.hasValue()) return Result<std::vector<NotificationSnapshot>>::failure(checked.error());
    return Result<std::vector<NotificationSnapshot>>::success({host->history.begin(), host->history.end()});
}
} // namespace cordis::ui
