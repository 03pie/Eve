/** @file store.cpp
 * @brief 用显式 gate 验证 Store 的目标/保存分离、单写队列、冲突决策和退出排空。
 */
#include <cordis/loader/store.hpp>
#include <cordis/loader/coordinator.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace cordis;
using namespace cordis::loader;
using cordis::testing::FakeScheduler;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
/** @brief 所有目标经过真实解析及内置定义校验。 */
std::shared_ptr<const ValidatedCandidate> target(const std::string& value = "one")
{
    auto parsed = parseConfiguration("formatVersion: 1\nplugins: [{id: notes, plugin: demo, config: '" + value + "'}]").value();
    return std::make_shared<const ValidatedCandidate>(validateCandidate(std::move(parsed),
        {{"demo", {}, [](const std::string&) { return Result<void>::success(); }}}).value());
}
/** @brief 只用于调度测试的内容版本，不冒充真实 SHA256；Qt 集成测试使用真实文件哈希。 */
ConfigurationFileSnapshot missing() { return {{"memory/config.yaml", false, {}}, {}}; }
/** @brief 一个在途后端任务保留其不可变目标和启动时的磁盘基线。 */
struct HeldWrite {
    std::shared_ptr<const ValidatedCandidate> target;
    ConfigurationFileVersion expected;
    Promise<ConfigurationFileCommit> done;
    bool completed = false;
};
/** @brief 受控事务后端按完成顺序更新磁盘值，显式 gate 暴露并发和错误路径。 */
struct ControlledWriter final : IConfigurationWriter {
    ConfigurationFileSnapshot disk = missing();
    std::vector<std::shared_ptr<HeldWrite>> held;
    std::vector<ConfigurationFileVersion> expected;
    std::function<void()> onCommit;
    std::optional<ConfigurationFileCommit> response;
    bool hold = false, failNext = false, throwCommit = false, malformed = false;
    int calls = 0, inFlight = 0, maximumInFlight = 0, generation = 0;
    /** @brief 模拟外部完整替换，版本与内容一起改变。 */
    void external(std::string text)
    {
        ++generation;
        disk = {{"memory/config.yaml", true, std::string(64, static_cast<char>('a' + generation % 6))}, std::move(text)};
    }
    /** @brief 对完成时的磁盘做基线比较；改变版本不以旧工作项的目标冒充真实磁盘。 */
    void complete(const std::shared_ptr<HeldWrite>& work)
    {
        CHECK(!work->completed);
        work->completed = true;
        --inFlight;
        if (response) { work->done.resolve(*response); return; }
        if (failNext) { failNext = false; work->done.reject(makeError(ErrorCode::ConfigIo, "controlled I/O failure")); return; }
        if (malformed) { work->done.resolve({FileCommitOutcome::Committed, missing(), {}}); return; }
        if (work->expected.path != disk.version.path || work->expected.exists != disk.version.exists || work->expected.sha256 != disk.version.sha256) {
            work->done.resolve({FileCommitOutcome::Conflict, disk, makeError(ErrorCode::ConfigConflict, "external edit")});
            return;
        }
        const auto& text = work->target->source().normalizedYaml();
        const bool unchanged = disk.version.exists && disk.text == text;
        if (!unchanged) external(text);
        work->done.resolve({unchanged ? FileCommitOutcome::Unchanged : FileCommitOutcome::Committed, disk, {}});
    }
    /** @brief 后端入口可重入；同一时刻的在途计数必须始终不超过一。 */
    Task<ConfigurationFileCommit> commit(std::shared_ptr<const ValidatedCandidate> value, ConfigurationFileVersion base) override
    {
        ++calls;
        auto hook = std::move(onCommit); onCommit = {};
        if (hook) hook();
        if (throwCommit) throw std::runtime_error("writer threw");
        expected.push_back(base);
        auto work = std::make_shared<HeldWrite>(HeldWrite{std::move(value), std::move(base), {}, false});
        ++inFlight;
        maximumInFlight = std::max(maximumInFlight, inFlight);
        if (hold) held.push_back(work);
        else complete(work);
        return work->done.task();
    }
};
/** @brief 场景显式保留 owner、后端和 Store，测试不依赖 sleep。 */
struct Scenario {
    std::shared_ptr<FakeScheduler> scheduler = std::make_shared<FakeScheduler>();
    std::shared_ptr<ControlledWriter> writer = std::make_shared<ControlledWriter>();
    ConfigurationStore store;
    /** @brief 可缩小队列预算，检查 close 不额外占用已满槽位。 */
    explicit Scenario(StoreLimits limits = {}) : store(scheduler, writer, missing(), limits) {}
    /** @brief 泵送到显式 gate 或空闲状态。 */
    void pump() { scheduler->runUntilIdle().value(); }
    /** @brief 在 scheduler 关闭前确认全部目标已保存。 */
    void close() { auto done = store.close(); pump(); CHECK(done.isReady() && done.tryResult()->hasValue()); scheduler->close(); }
};
/** @brief 跟踪目标不执行 I/O，修订不得倒退或复用为不同对象。 */
void targets()
{
    Scenario scene;
    CHECK(!scene.store.snapshot().value().dirty && !scene.store.snapshot().value().persistedRevision);
    auto one = target();
    CHECK(!scene.store.save(0).hasValue());
    CHECK(!scene.store.track({}, 1).hasValue() && !scene.store.track(one, 0).hasValue());
    scene.store.track(one, 2).value();
    scene.store.track(one, 2).value();
    CHECK(scene.store.track(target(), 2).error().code == ErrorCode::ConfigConflict);
    CHECK(scene.store.track(one, 1).error().code == ErrorCode::ConfigConflict);
    auto moved = *one;
    auto retained = std::move(moved);
    CHECK(!scene.store.track(std::make_shared<const ValidatedCandidate>(std::move(moved)), 3).hasValue());
    CHECK(retained.source().isValid() && scene.writer->calls == 0);
    CHECK(scene.store.snapshot().value().desired == one && scene.store.snapshot().value().dirty);
    scene.close();
    CHECK(scene.store.snapshot().value().persistedRevision == 2);
}
/** @brief FIFO 保存时逐次更新磁盘基线，旧写入完成不能把新目标标成干净。 */
void serial()
{
    Scenario scene;
    scene.writer->hold = true;
    scene.store.track(target("A"), 1).value();
    auto a = scene.store.save(1).value(), duplicate = scene.store.save(1).value();
    scene.store.track(target("B"), 2).value();
    auto b = scene.store.save(2).value();
    CHECK(scene.writer->calls == 0 && scene.store.save(1).error().code == ErrorCode::ConfigConflict);
    scene.pump();
    CHECK(scene.writer->held.size() == 1 && scene.store.snapshot().value().queuedWrites == 1);
    CHECK(!scene.store.snapshot().value().persistedRevision && !a.isReady() && !b.isReady());
    scene.writer->complete(scene.writer->held[0]); scene.pump();
    CHECK(a.tryResult()->value().outcome == SaveOutcome::Written && a.tryResult() == duplicate.tryResult());
    CHECK(scene.store.snapshot().value().persistedRevision == 1 && scene.store.snapshot().value().dirty);
    CHECK(scene.writer->expected[1].sha256 == scene.writer->disk.version.sha256 && scene.writer->maximumInFlight == 1);
    scene.writer->complete(scene.writer->held[1]); scene.pump();
    CHECK(b.tryResult()->value().revision == 2 && !scene.store.snapshot().value().dirty);
    CHECK(scene.store.snapshot().value().persistedRevision == 2);
    scene.writer->hold = false;
    auto unchanged = scene.store.save(2).value(); scene.pump();
    CHECK(unchanged.tryResult()->value().outcome == SaveOutcome::Unchanged);
    CHECK(a.tryResult()->value().revision == 1);
    scene.close();
}
/** @brief 普通队列已满时拒绝多余保存，退出最终刷新在旧队列排空后使用同一槽位。 */
void limits()
{
    Scenario scene({1});
    scene.writer->hold = true;
    scene.store.track(target("A"), 1).value(); auto a = scene.store.save(1).value();
    scene.store.track(target("B"), 2).value();
    CHECK(scene.store.save(2).error().code == ErrorCode::StepLimitExceeded);
    auto close = scene.store.close(); scene.pump();
    CHECK(scene.writer->held.size() == 1 && !close.isReady());
    scene.writer->complete(scene.writer->held[0]); scene.pump();
    CHECK(a.isReady() && scene.writer->held.size() == 2 && !close.isReady());
    scene.writer->complete(scene.writer->held[1]); scene.pump();
    CHECK(close.tryResult()->hasValue() && scene.store.snapshot().value().persistedRevision == 2);
    CHECK(scene.writer->maximumInFlight == 1); scene.scheduler->close();
}
/** @brief I/O 失败停止整批后续写入，重试从原磁盘基线开始且不改写历史报告。 */
void failure()
{
    Scenario scene;
    scene.writer->hold = true;
    scene.store.track(target("A"), 1).value(); auto a = scene.store.save(1).value(); scene.pump();
    scene.store.track(target("B"), 2).value(); auto b = scene.store.save(2).value();
    scene.writer->failNext = true; scene.writer->complete(scene.writer->held[0]); scene.pump();
    CHECK(a.tryResult()->value().outcome == SaveOutcome::Failed && b.tryResult()->value().outcome == SaveOutcome::Skipped);
    CHECK(b.tryResult()->value().error->causes[0].code == ErrorCode::ConfigIo);
    CHECK(scene.writer->calls == 1 && scene.store.snapshot().value().dirty && !scene.store.snapshot().value().persistedRevision);
    scene.writer->hold = false;
    auto retry = scene.store.save(2).value(); scene.pump();
    CHECK(retry.tryResult()->value().outcome == SaveOutcome::Written && scene.store.snapshot().value().persistedRevision == 2);
    CHECK(!scene.store.snapshot().value().lastError && a.tryResult()->value().outcome == SaveOutcome::Failed);
    scene.close();
}
/** @brief 冲突保留两份来源及历史保存目标，只有宿主明确 rebase 后才用新磁盘版本提交。 */
void conflict()
{
    Scenario scene;
    scene.store.track(target("A"), 1).value(); auto a = scene.store.save(1).value(); scene.pump();
    CHECK(a.tryResult()->value().outcome == SaveOutcome::Written);
    const auto oldBaseline = scene.store.snapshot().value().baseline;
    scene.writer->external("invalid: [external");
    scene.store.track(target("B"), 2).value(); auto b = scene.store.save(2).value();
    scene.store.track(target("C"), 3).value(); auto c = scene.store.save(3).value(); scene.pump();
    CHECK(b.tryResult()->value().outcome == SaveOutcome::Conflict && c.tryResult()->value().outcome == SaveOutcome::Skipped);
    auto state = scene.store.snapshot().value();
    CHECK(state.baseline == oldBaseline && state.persistedRevision == 1 && state.external->text == "invalid: [external" && state.dirty);
    auto again = scene.store.save(3).value(); scene.pump();
    CHECK(again.tryResult()->value().outcome == SaveOutcome::Conflict && scene.writer->disk.text == "invalid: [external");
    CHECK(!scene.store.rebase(scene.writer->disk, 2).hasValue());
    scene.store.rebase(scene.writer->disk, 3).value();
    CHECK(scene.store.snapshot().value().dirty && scene.store.snapshot().value().persistedRevision == 1);
    auto retry = scene.store.save(3).value(); scene.pump();
    CHECK(retry.tryResult()->value().outcome == SaveOutcome::Written && !scene.store.snapshot().value().dirty);
    CHECK(b.tryResult()->value().external->text == "invalid: [external");
    scene.close();
}
/** @brief close 固定最终目标，拒绝新业务并等待在途写入及最后一次未排队目标刷新。 */
void shutdown()
{
    Scenario scene;
    scene.writer->hold = true;
    scene.store.track(target("A"), 1).value(); auto a = scene.store.save(1).value(); scene.pump();
    scene.store.track(target("B"), 2).value(); auto close = scene.store.close();
    CHECK(scene.store.track(target("C"), 3).error().code == ErrorCode::ContextClosing);
    CHECK(scene.store.save(2).error().code == ErrorCode::ContextClosing);
    CHECK(scene.store.rebase(missing(), 2).error().code == ErrorCode::ContextClosing);
    scene.pump(); CHECK(!close.isReady() && scene.writer->calls == 1);
    scene.writer->complete(scene.writer->held[0]); scene.pump();
    CHECK(a.isReady() && !close.isReady() && scene.writer->held.size() == 2);
    scene.writer->complete(scene.writer->held[1]); scene.pump();
    CHECK(close.tryResult()->hasValue() && close.tryResult() == scene.store.close().tryResult());
    CHECK(scene.store.snapshot().value().closed && !scene.store.snapshot().value().dirty);
    scene.scheduler->close();
    Scenario empty; empty.close(); CHECK(empty.writer->calls == 0);
}
/** @brief 退出失败不会报告刷新成功或无限重试，也不能在失败后偷偷提交队列中的新目标。 */
void closeFailure()
{
    Scenario scene;
    scene.store.track(target(), 1).value(); scene.writer->failNext = true;
    auto close = scene.store.close(); scene.pump();
    CHECK(!close.tryResult()->hasValue() && close.tryResult()->error().code == ErrorCode::ConfigIo);
    CHECK(scene.store.snapshot().value().dirty && scene.store.snapshot().value().closed && scene.writer->calls == 1);
    CHECK(scene.store.close().tryResult() == close.tryResult()); scene.scheduler->close();
    Scenario queued;
    queued.writer->hold = true;
    queued.store.track(target("A"), 1).value(); queued.store.save(1).value(); queued.pump();
    queued.store.track(target("B"), 2).value(); auto b = queued.store.save(2).value(); auto stop = queued.store.close();
    queued.writer->failNext = true; queued.writer->complete(queued.writer->held[0]); queued.pump();
    CHECK(b.tryResult()->value().outcome == SaveOutcome::Skipped && !stop.tryResult()->hasValue() && queued.writer->calls == 1);
    queued.scheduler->close();
}
/** @brief worker 回执无法投递时不从 worker 修改保存账目，全部未完成任务都可观察失败。 */
void delivery()
{
    Scenario queued;
    queued.store.track(target(), 1).value(); auto saved = queued.store.save(1).value(); queued.scheduler->close();
    CHECK(!saved.tryResult()->hasValue() && queued.store.snapshot().value().deliveryBlocked);
    CHECK(!queued.store.close().tryResult()->hasValue());
    Scenario scene;
    scene.writer->hold = true;
    scene.store.track(target("A"), 1).value(); auto a = scene.store.save(1).value(); scene.pump();
    scene.store.track(target("B"), 2).value(); auto b = scene.store.save(2).value(); scene.pump();
    scene.scheduler->close();
    auto done = scene.writer->held[0]->done;
    std::thread worker([done] { done.reject(makeError(ErrorCode::ConfigIo, "worker failure")); }); worker.join();
    CHECK(!a.tryResult()->hasValue() && !b.tryResult()->hasValue());
    CHECK(scene.store.snapshot().value().deliveryBlocked && !scene.store.snapshot().value().persistedRevision);
    CHECK(scene.store.save(2).error().code == ErrorCode::SchedulerClosed);
    CHECK(!scene.store.close().tryResult()->hasValue());
}
/** @brief 同步后端重入跟踪/保存/关闭，不能并发进入第二次事务。 */
void reentry()
{
    Scenario scene;
    std::optional<Task<SaveReport>> next;
    scene.store.track(target("A"), 1).value(); auto first = scene.store.save(1).value();
    scene.writer->onCommit = [&] { scene.store.track(target("B"), 2).value(); next = scene.store.save(2).value(); };
    scene.pump();
    CHECK(first.tryResult()->value().revision == 1 && next->tryResult()->value().revision == 2);
    CHECK(scene.writer->maximumInFlight == 1 && !scene.store.snapshot().value().dirty); scene.close();
    Scenario closing;
    std::optional<Task<void>> close;
    closing.store.track(target("A"), 1).value(); closing.store.save(1).value();
    closing.writer->onCommit = [&] { closing.store.track(target("B"), 2).value(); close = closing.store.close(); CHECK(!close->isReady()); };
    closing.pump();
    CHECK(close->tryResult()->hasValue() && closing.writer->calls == 2 && closing.store.snapshot().value().persistedRevision == 2);
    closing.scheduler->close();
}
/** @brief 非法后端结果、抛错、错误线程、忙时 rebase 和同步关闭 scheduler 均不能推进保存状态。 */
void guards()
{
    Scenario scene;
    scene.store.track(target(), 1).value(); scene.writer->malformed = true;
    auto malformed = scene.store.save(1).value(); scene.pump();
    CHECK(malformed.tryResult()->value().outcome == SaveOutcome::Failed && !scene.store.snapshot().value().persistedRevision);
    scene.writer->malformed = false; scene.writer->throwCommit = true;
    auto thrown = scene.store.save(1).value(); scene.pump();
    CHECK(thrown.tryResult()->value().outcome == SaveOutcome::Failed && scene.store.snapshot().value().dirty);
    scene.writer->throwCommit = false; scene.writer->hold = true;
    auto pending = scene.store.save(1).value(); scene.pump();
    CHECK(!scene.store.rebase(missing(), 1).hasValue());
    bool wrong = false;
    auto desired = target();
    std::thread worker([&] { wrong = scene.store.track(desired, 2).error().code == ErrorCode::WrongThread &&
        scene.store.save(1).error().code == ErrorCode::WrongThread && scene.store.rebase(missing(), 1).error().code == ErrorCode::WrongThread &&
        scene.store.snapshot().error().code == ErrorCode::WrongThread && scene.store.close().tryResult()->error().code == ErrorCode::WrongThread; });
    worker.join(); CHECK(wrong);
    scene.writer->complete(scene.writer->held[0]); scene.pump(); CHECK(pending.tryResult()->value().outcome == SaveOutcome::Written); scene.close();
    Scenario blocked;
    blocked.store.track(target(), 1).value(); auto save = blocked.store.save(1).value();
    blocked.writer->onCommit = [&] { blocked.scheduler->close(); };
    blocked.pump(); CHECK(!save.tryResult()->hasValue() && blocked.store.snapshot().value().deliveryBlocked);
    CHECK(!blocked.store.close().tryResult()->hasValue());
    bool rejected = false;
    try { ConfigurationStore invalid(std::make_shared<FakeScheduler>(), std::make_shared<ControlledWriter>(), {}); }
    catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}
/** @brief Unchanged 不能凭空创建文件、改写磁盘版本或目标文本，也不能使关闭虚报保存成功。 */
void unchangedProtocol()
{
    Scenario missingFile;
    auto desired = target("A");
    missingFile.store.track(desired, 1).value();
    missingFile.writer->response = ConfigurationFileCommit{FileCommitOutcome::Unchanged,
        {{"memory/config.yaml", true, std::string(64, 'a')}, desired->source().normalizedYaml()}, {}};
    auto save = missingFile.store.save(1).value(); missingFile.pump();
    CHECK(save.tryResult()->value().outcome == SaveOutcome::Failed);
    CHECK(missingFile.store.snapshot().value().dirty && !missingFile.store.snapshot().value().persistedRevision);
    auto close = missingFile.store.close(); missingFile.pump();
    CHECK(!close.tryResult()->hasValue() && missingFile.store.snapshot().value().closed);
    missingFile.scheduler->close();
    for (bool changesText : {false, true}) {
        Scenario scene;
        auto one = target("A");
        scene.store.track(one, 1).value(); scene.store.save(1).value(); scene.pump();
        const auto baseline = scene.store.snapshot().value().baseline;
        auto next = changesText ? target("B") : one;
        scene.store.track(next, 2).value();
        auto claimed = *baseline;
        claimed.text = next->source().normalizedYaml();
        if (!changesText) claimed.version.sha256 = std::string(64, 'f');
        scene.writer->response = ConfigurationFileCommit{FileCommitOutcome::Unchanged, claimed, {}};
        auto rejected = scene.store.save(2).value();
        scene.store.track(target("C"), 3).value(); auto skipped = scene.store.save(3).value(); scene.pump();
        CHECK(rejected.tryResult()->value().outcome == SaveOutcome::Failed && skipped.tryResult()->value().outcome == SaveOutcome::Skipped);
        CHECK(scene.store.snapshot().value().persistedRevision == 1 && scene.store.snapshot().value().baseline == baseline);
        CHECK(scene.store.snapshot().value().dirty);
        scene.writer->response.reset();
        scene.close();
    }
}
/** @brief facade 释放后已接受写入仍完成；排空后没有 Store/后端所有权环。 */
void lifetime()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    auto writer = std::make_shared<ControlledWriter>(); writer->hold = true;
    std::weak_ptr<ControlledWriter> weak = writer;
    auto store = std::make_unique<ConfigurationStore>(scheduler, writer, missing());
    store->track(target(), 1).value(); auto save = store->save(1).value(); scheduler->runUntilIdle().value();
    store.reset(); CHECK(!weak.expired() && !save.isReady());
    writer->complete(writer->held[0]); writer.reset(); scheduler->runUntilIdle().value();
    CHECK(save.tryResult()->value().outcome == SaveOutcome::Written && weak.expired()); scheduler->close();
}
/** @brief 测试协调器运行失败，不包含 Kernel 适配，仅验证保存不依赖运行结果。 */
struct FailingDriver final : IReconcileDriver {
    /** @brief 支持测试中的固定单根目标。 */
    Result<void> preflight(const ValidatedCandidate&) override { return Result<void>::success(); }
    /** @brief 返回一次会失败的业务动作。 */
    Result<std::vector<EntryOperation>> plan(const ValidatedCandidate&) override { return Result<std::vector<EntryOperation>>::success({{"notes", ChangeKind::Add}}); }
    /** @brief 激活失败仍形成安全稳定报告。 */
    Task<StepReport> apply(EntryOperation, std::shared_ptr<const ValidatedCandidate>, std::uint64_t) override
    { Promise<StepReport> done; done.resolve({StepOutcome::Failed, true, makeError(ErrorCode::ActivationFailed, "runtime failed")}); return done.task(); }
    /** @brief 没有实际运行成功的条目。 */
    Result<std::vector<RunningEntry>> snapshot() const override { return Result<std::vector<RunningEntry>>::success({}); }
    /** @brief 测试驱动没有剩余资源。 */
    Task<CleanupReport> close() override { Promise<CleanupReport> done; done.resolve({true, {}}); return done.task(); }
};
/** @brief 同一已接受目标可以同时具有运行失败结果和已保存修订，不等待运行完成才保存。 */
void coordinator()
{
    Scenario scene;
    ConfigurationCoordinator coordinator(scene.scheduler, std::make_shared<FailingDriver>());
    auto accepted = coordinator.accept(*target(), 0).value();
    auto desired = coordinator.snapshot().value();
    scene.store.track(desired.desired, desired.desiredRevision).value(); auto save = scene.store.save(accepted.revision).value();
    CHECK(!accepted.settled.isReady() && !save.isReady()); scene.pump();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Failed);
    CHECK(save.tryResult()->value().outcome == SaveOutcome::Written && scene.store.snapshot().value().persistedRevision == accepted.revision);
    CHECK(!scene.store.snapshot().value().dirty && coordinator.snapshot().value().running.empty());
    auto close = coordinator.close(); scene.pump(); CHECK(close.tryResult()->value().quiescent); scene.close();
}
/** @brief 每个 CTest 场景独立进程，gate 只由测试显式推进。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string name = argv[1];
        if (name == "targets") targets();
        else if (name == "serial") serial();
        else if (name == "limits") limits();
        else if (name == "failure") failure();
        else if (name == "conflict") conflict();
        else if (name == "shutdown") shutdown();
        else if (name == "close_failure") closeFailure();
        else if (name == "delivery") delivery();
        else if (name == "reentry") reentry();
        else if (name == "guards") guards();
        else if (name == "unchanged_protocol") unchangedProtocol();
        else if (name == "lifetime") lifetime();
        else if (name == "coordinator") coordinator();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
