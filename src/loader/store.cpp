/** @file store.cpp
 * @brief owner 队列维护目标/历史保存状态，失败停止后续写入，投递阻断只跨线程终结 Promise。
 */
#include <cordis/loader/store.hpp>
#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace cordis::loader::detail {
/** @brief 统一文件保存阶段，不将配置负载带入诊断。 */
ErrorInfo storeError(ErrorCode code, std::string message)
{
    auto error = makeError(code, std::move(message));
    error.phase = "loader.store";
    return error;
}
/** @brief 校验后端值的形状；实际 SHA256 与路径绑定仍由事务后端负责。 */
bool validDisk(const ConfigurationFileSnapshot& disk)
{
    if (disk.version.path.empty()) return false;
    if (!disk.version.exists) return disk.version.sha256.empty() && disk.text.empty();
    return disk.version.sha256.size() == 64 && std::all_of(disk.version.sha256.begin(), disk.version.sha256.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}
/** @brief 无变化回执须与既有基线完全一致，不能以此隐式采用另一份磁盘版本。 */
bool sameDisk(const ConfigurationFileSnapshot& left, const ConfigurationFileSnapshot& right)
{
    return left.version.path == right.version.path && left.version.exists == right.version.exists &&
        left.version.sha256 == right.version.sha256 && left.text == right.text;
}
/** @brief 跨线程故障不读取 owner 队列；换出登记后在锁外终结任务。 */
struct StoreDelivery {
    std::atomic_bool blocked{false};
    std::mutex mutex;
    std::map<std::uint64_t, Promise<SaveReport>> waiting;
    Promise<void> closed;
    /** @brief 提交 owner 工作项前登记，和故障发布互斥。 */
    bool add(std::uint64_t id, Promise<SaveReport> done)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (blocked.load()) return false;
        waiting.emplace(id, std::move(done));
        return true;
    }
    /** @brief 工作项仍强持 Promise，登记移除不在锁内销毁最后一个观察者。 */
    void remove(std::uint64_t id)
    {
        std::lock_guard<std::mutex> lock(mutex);
        waiting.erase(id);
    }
    /** @brief 首次故障关闭所有未完成回执，保留成功完成的历史任务。 */
    void fail(ErrorInfo error)
    {
        std::map<std::uint64_t, Promise<SaveReport>> removed;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (blocked.exchange(true)) return;
            removed.swap(waiting);
        }
        error.phase = "loader.store.delivery";
        for (const auto& item : removed) item.second.reject(error);
        closed.reject(std::move(error));
    }
};
/** @brief 排队时固定目标，磁盘基线在真正开始写入时读取最新获确认的值。 */
struct SaveWork {
    std::uint64_t id, revision;
    std::shared_ptr<const ValidatedCandidate> target;
    Promise<SaveReport> done;
};
/** @brief 所有业务账目只在 owner 修改；后端在途或投递阻断时保持控制块存活。 */
struct StoreControl : std::enable_shared_from_this<StoreControl> {
    std::shared_ptr<IScheduler> scheduler;
    std::shared_ptr<IConfigurationWriter> writer;
    StoreLimits limits;
    std::shared_ptr<StoreDelivery> delivery = std::make_shared<StoreDelivery>();
    std::shared_ptr<StoreControl> keepAlive;
    std::shared_ptr<const ValidatedCandidate> desired, persisted;
    std::uint64_t desiredRevision = 0, nextId = 0;
    std::optional<std::uint64_t> persistedRevision;
    std::shared_ptr<const ConfigurationFileSnapshot> baseline, external;
    std::optional<ErrorInfo> lastError, closeError;
    std::deque<std::shared_ptr<SaveWork>> queue;
    std::shared_ptr<SaveWork> active;
    bool dirty = false, closing = false, closed = false, finalFlush = false;
    bool scheduled = false, pumping = false, requested = false;
    /** @brief 诊断允许 scheduler 停止后读取，但任何方法仍须在 owner 调用。 */
    Result<void> thread() const
    {
        if (!scheduler->isCurrentThread()) return Result<void>::failure(storeError(ErrorCode::WrongThread, "store requires owner thread"));
        return Result<void>::success();
    }
    /** @brief 在修改目标或队列前检查业务入口。 */
    Result<void> business() const
    {
        auto checked = thread();
        if (!checked.hasValue()) return checked;
        if (closing) return Result<void>::failure(storeError(ErrorCode::ContextClosing, "store closing"));
        if (delivery->blocked.load() || !scheduler->isOpen()) return Result<void>::failure(storeError(ErrorCode::SchedulerClosed, "store delivery unavailable"));
        return Result<void>::success();
    }
    /** @brief 同步后端可能重入并关闭 scheduler，发布账目前重新核对投递门禁。 */
    bool deliveryOpen()
    {
        if (delivery->blocked.load()) return false;
        if (scheduler->isOpen()) return true;
        delivery->fail(storeError(ErrorCode::SchedulerClosed, "scheduler closed during save"));
        return false;
    }
    /** @brief 至多一个 owner 泵送任务，后端同步重入只记录下一次请求。 */
    void schedule()
    {
        if (closed || delivery->blocked.load()) return;
        requested = true;
        if (scheduled || pumping) return;
        scheduled = true;
        auto self = shared_from_this();
        keepAlive = self;
        ScheduledJob job;
        job.run = [self] {
            self->scheduled = false;
            try { self->pump(); }
            catch (const std::exception& error) { self->delivery->fail(storeError(ErrorCode::CallbackFailed, error.what())); }
            catch (...) { self->delivery->fail(storeError(ErrorCode::CallbackFailed, "store pump threw")); }
        };
        job.discarded = [gate = delivery] { gate->fail(storeError(ErrorCode::SchedulerClosed, "store job discarded")); };
        try {
            if (!scheduler->post(std::move(job))) delivery->fail(storeError(ErrorCode::SchedulerClosed, "store post rejected"));
        } catch (const std::exception& error) { delivery->fail(storeError(ErrorCode::SchedulerClosed, error.what())); }
        catch (...) { delivery->fail(storeError(ErrorCode::SchedulerClosed, "store post threw")); }
    }
    /** @brief 找到尚未结束的同修订请求，避免双击提交生成重复写入。 */
    std::shared_ptr<SaveWork> find(std::uint64_t revision) const
    {
        if (active && active->revision == revision) return active;
        for (const auto& work : queue) if (work->revision == revision) return work;
        return {};
    }
    /** @brief 按当前目标建立不可变工作项，登记完成后才发布到队列。 */
    Result<std::shared_ptr<SaveWork>> enqueue()
    {
        if (nextId == std::numeric_limits<std::uint64_t>::max())
            return Result<std::shared_ptr<SaveWork>>::failure(storeError(ErrorCode::StepLimitExceeded, "save identifier exhausted"));
        auto work = std::make_shared<SaveWork>(SaveWork{nextId + 1, desiredRevision, desired, {}});
        if (!delivery->add(work->id, work->done))
            return Result<std::shared_ptr<SaveWork>>::failure(storeError(ErrorCode::SchedulerClosed, "store delivery unavailable"));
        try { queue.push_back(work); }
        catch (...) { delivery->remove(work->id); throw; }
        nextId = work->id;
        return Result<std::shared_ptr<SaveWork>>::success(std::move(work));
    }
    /** @brief 移除 owner 工作项后完成历史报告，参数按值强持以容忍索引被清空。 */
    void finish(std::shared_ptr<SaveWork> work, SaveReport report)
    {
        if (active == work) active.reset();
        delivery->remove(work->id);
        work->done.resolve(std::move(report));
    }
    /** @brief 写入错误先摘下整批后续项，完成观察者重入的新请求不被误归入旧批次。 */
    void failed(std::shared_ptr<SaveWork> work, SaveReport report)
    {
        dirty = true;
        lastError = report.error;
        const auto cause = report.error;
        if (closing) { closeError = report.error; finalFlush = false; }
        auto skipped = std::move(queue);
        queue.clear();
        finish(work, std::move(report));
        for (const auto& item : skipped) {
            auto error = storeError(ErrorCode::OperationCancelled, "save skipped after an earlier write failed");
            if (cause) error.causes.push_back(*cause);
            finish(item, {item->revision, SaveOutcome::Skipped, std::move(error), {}});
        }
    }
    /** @brief 只有完整成功回执能推进历史保存状态与下一次磁盘基线。 */
    void complete(const std::shared_ptr<SaveWork>& work, const Result<ConfigurationFileCommit>& result)
    {
        if (!deliveryOpen()) return;
        SaveReport report{work->revision, SaveOutcome::Failed, {}, {}};
        if (!result.hasValue()) report.error = result.error();
        else {
            const auto& commit = result.value();
            const bool valid = validDisk(commit.disk) && commit.disk.version.path == baseline->version.path;
            const bool unchangedValid = commit.outcome != FileCommitOutcome::Unchanged || sameDisk(commit.disk, *baseline);
            if (valid && commit.outcome == FileCommitOutcome::Conflict) {
                external = std::make_shared<const ConfigurationFileSnapshot>(commit.disk);
                report.outcome = SaveOutcome::Conflict;
                report.external = external;
                report.error = storeError(ErrorCode::ConfigConflict, "configuration changed on disk");
                if (commit.error) report.error->causes.push_back(*commit.error);
            } else if (valid && (commit.outcome == FileCommitOutcome::Committed || commit.outcome == FileCommitOutcome::Unchanged) &&
                       unchangedValid && !commit.error && commit.disk.version.exists && commit.disk.text == work->target->source().normalizedYaml()) {
                auto confirmed = std::make_shared<const ConfigurationFileSnapshot>(commit.disk);
                baseline = std::move(confirmed);
                persisted = work->target;
                persistedRevision = work->revision;
                dirty = desiredRevision != work->revision;
                external.reset();
                lastError.reset();
                report.outcome = commit.outcome == FileCommitOutcome::Committed ? SaveOutcome::Written : SaveOutcome::Unchanged;
            } else report.error = storeError(ErrorCode::ConfigIo, "writer returned an invalid commit report");
        }
        if (report.error) failed(work, std::move(report));
        else finish(work, std::move(report));
        schedule();
    }
    /** @brief 每轮只启动一次提交；close 的最终刷新在已接受队列排空后才占用槽位。 */
    void pump()
    {
        if (pumping) { requested = true; return; }
        pumping = true;
        requested = false;
        /** @brief 同步后端调用退出后再排队，禁止嵌套事件循环并发执行写入。 */
        struct Exit {
            StoreControl& state;
            /** @brief 泵送栈完全退出后恢复排队。 */
            ~Exit() { state.pumping = false; if (state.requested) state.schedule(); }
        } exit{*this};
        if (!deliveryOpen() || active) return;
        if (queue.empty() && closing && finalFlush) {
            finalFlush = false;
            if (dirty && desired) {
                auto queued = enqueue();
                if (!queued.hasValue()) closeError = queued.error();
            }
        }
        if (queue.empty()) {
            if (closing && !closed) {
                closed = true;
                if (closeError) delivery->closed.reject(*closeError);
                else if (dirty) delivery->closed.reject(lastError.value_or(storeError(ErrorCode::ConfigIo, "latest target is not saved")));
                else delivery->closed.resolve();
            }
            keepAlive.reset();
            return;
        }
        active = queue.front();
        queue.pop_front();
        auto work = active;
        auto self = shared_from_this();
        try {
            auto task = writer->commit(work->target, baseline->version);
            auto receipt = task.thenOn(scheduler, [self, work](const Result<ConfigurationFileCommit>& result) { self->complete(work, result); });
            cordis::detail::TaskAccess::observe(receipt, [gate = delivery](Task<void>::Snapshot result) {
                if (!result->hasValue()) gate->fail(result->error());
            });
        } catch (const std::exception& error) { complete(work, Result<ConfigurationFileCommit>::failure(storeError(ErrorCode::ConfigIo, error.what()))); }
        catch (...) { complete(work, Result<ConfigurationFileCommit>::failure(storeError(ErrorCode::ConfigIo, "writer threw"))); }
    }
};
} // namespace cordis::loader::detail

namespace cordis::loader {
/** @brief 校验 owner 和基线形状，构造不会解析或接受磁盘配置。 */
ConfigurationStore::ConfigurationStore(std::shared_ptr<IScheduler> scheduler, std::shared_ptr<IConfigurationWriter> writer,
    ConfigurationFileSnapshot baseline, StoreLimits limits)
{
    if (!scheduler || !writer || !scheduler->isCurrentThread() || !scheduler->isOpen() || !limits.maximumWrites || !detail::validDisk(baseline))
        throw std::invalid_argument("store requires open owner scheduler, writer, disk baseline and positive queue limit");
    auto state = std::make_shared<detail::StoreControl>();
    state->scheduler = std::move(scheduler);
    state->writer = std::move(writer);
    state->limits = limits;
    state->baseline = std::make_shared<const ConfigurationFileSnapshot>(std::move(baseline));
    control_ = std::move(state);
}
/** @brief facade 释放不取消已经请求的保存，未排队目标由显式 close 刷新。 */
ConfigurationStore::~ConfigurationStore() = default;
/** @brief Store 只跟踪已接受目标，不读取或等待任何运行实例结果。 */
Result<void> ConfigurationStore::track(std::shared_ptr<const ValidatedCandidate> target, std::uint64_t revision)
{
    auto state = control_;
    auto checked = state->business();
    if (!checked.hasValue()) return checked;
    if (!target || !target->source().isValid() || !revision)
        return Result<void>::failure(detail::storeError(ErrorCode::ConfigInvalid, "store requires a valid accepted target and nonzero revision"));
    if (revision < state->desiredRevision || (revision == state->desiredRevision && target != state->desired))
        return Result<void>::failure(detail::storeError(ErrorCode::ConfigConflict, "accepted target revision is stale or reused"));
    if (revision == state->desiredRevision) return Result<void>::success();
    state->desired = std::move(target);
    state->desiredRevision = revision;
    state->dirty = true;
    return Result<void>::success();
}
/** @brief 只固定当前目标，不固定旧磁盘基线；前一份成功保存会供下一份使用。 */
Result<Task<SaveReport>> ConfigurationStore::save(std::uint64_t expected)
{
    auto state = control_;
    auto checked = state->business();
    if (!checked.hasValue()) return Result<Task<SaveReport>>::failure(checked.error());
    if (!state->desired || expected != state->desiredRevision)
        return Result<Task<SaveReport>>::failure(detail::storeError(ErrorCode::ConfigConflict, "desired revision conflict or no accepted target"));
    if (auto existing = state->find(expected)) return Result<Task<SaveReport>>::success(existing->done.task());
    if (state->queue.size() + (state->active ? 1U : 0U) >= state->limits.maximumWrites)
        return Result<Task<SaveReport>>::failure(detail::storeError(ErrorCode::StepLimitExceeded, "store write queue is full"));
    auto work = state->enqueue();
    if (!work.hasValue()) return Result<Task<SaveReport>>::failure(work.error());
    state->schedule();
    return Result<Task<SaveReport>>::success(work.value()->done.task());
}
/** @brief 更换期望基线是明确的宿主冲突决策，不自动把磁盘内容登记为已保存目标。 */
Result<void> ConfigurationStore::rebase(ConfigurationFileSnapshot disk, std::uint64_t expected)
{
    auto state = control_;
    auto checked = state->business();
    if (!checked.hasValue()) return checked;
    if (state->desiredRevision != expected || state->active || !state->queue.empty())
        return Result<void>::failure(detail::storeError(ErrorCode::ConfigConflict, "rebase requires matching revision and idle write queue"));
    if (!detail::validDisk(disk) || disk.version.path != state->baseline->version.path)
        return Result<void>::failure(detail::storeError(ErrorCode::InvalidArgument, "invalid or differently bound disk baseline"));
    auto baseline = std::make_shared<const ConfigurationFileSnapshot>(std::move(disk));
    state->baseline = std::move(baseline);
    state->external.reset();
    state->lastError.reset();
    state->dirty = static_cast<bool>(state->desired);
    return Result<void>::success();
}
/** @brief 所有快照对象均为宿主不可变值，不调用写后端。 */
Result<StoreSnapshot> ConfigurationStore::snapshot() const
{
    auto state = control_;
    auto checked = state->thread();
    if (!checked.hasValue()) return Result<StoreSnapshot>::failure(checked.error());
    return Result<StoreSnapshot>::success({state->desiredRevision, state->desired, state->persistedRevision, state->persisted,
        state->baseline, state->external, state->lastError, state->queue.size(), static_cast<bool>(state->active),
        state->dirty, state->closing, state->closed, state->delivery->blocked.load()});
}
/** @brief 关闭固定最终目标，失败只终结一次，不无限重试或隐式覆盖外部文件。 */
Task<void> ConfigurationStore::close()
{
    auto state = control_;
    auto checked = state->thread();
    if (!checked.hasValue()) return failedTask(checked.error());
    if (!state->closing) {
        state->closing = true;
        state->finalFlush = state->dirty && state->desired && !state->find(state->desiredRevision);
        state->schedule();
    }
    return state->delivery->closed.task();
}
} // namespace cordis::loader
