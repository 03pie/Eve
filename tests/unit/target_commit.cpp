/** @file target_commit.cpp
 * @brief 隔离验证目标/保存账目的同步提交，不代替 Core、模块或完整运行替换测试。
 */
#include "../../src/loader/target_commit_p.hpp"
#include <cordis/testing/fake_scheduler.hpp>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cordis;
using namespace cordis::loader;
using cordis::loader::detail::TargetCommitAccess;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
struct Scheduler final : IScheduler {
    testing::FakeScheduler queue;
    mutable int boundaries = 0;
    mutable std::function<void()> onOpen;
    bool post(ScheduledJob job) override
    {
        ++boundaries;
        return queue.post(std::move(job));
    }
    bool isCurrentThread() const noexcept override
    {
        ++boundaries;
        return queue.isCurrentThread();
    }
    bool isOpen() const override
    {
        ++boundaries;
        auto hook = std::exchange(onOpen, {});
        if (hook)
            hook();
        return queue.isOpen();
    }
};
struct Driver final : IReconcileDriver {
    bool hold = false;
    int checks = 0;
    Promise<StepReport> pending;
    Result<void> preflight(const ValidatedCandidate&) override
    {
        ++checks;
        return Result<void>::success();
    }
    Result<std::vector<EntryOperation>> plan(const ValidatedCandidate&) override
    {
        return Result<std::vector<EntryOperation>>::success(
            hold ? std::vector<EntryOperation>{{"item", ChangeKind::Restart}} : std::vector<EntryOperation>{});
    }
    Task<StepReport> apply(EntryOperation, std::shared_ptr<const ValidatedCandidate>, std::uint64_t) override
    {
        return pending.task();
    }
    Result<std::vector<RunningEntry>> snapshot() const override
    {
        return Result<std::vector<RunningEntry>>::success({});
    }
    Task<CleanupReport> close() override
    {
        Promise<CleanupReport> done;
        done.resolve({true, {}});
        return done.task();
    }
};
struct Writer final : IConfigurationWriter {
    ConfigurationFileSnapshot disk{{"memory/config.yaml", false, {}}, {}};
    bool hold = false;
    int calls = 0;
    std::shared_ptr<const ValidatedCandidate> writing;
    Promise<ConfigurationFileCommit> pending;
    void complete()
    {
        disk = {{disk.version.path, true, std::string(64, static_cast<char>('a' + calls % 5))},
                writing->source().normalizedYaml()};
        pending.resolve({FileCommitOutcome::Committed, disk, {}});
    }
    Task<ConfigurationFileCommit> commit(std::shared_ptr<const ValidatedCandidate> value,
                                         ConfigurationFileVersion) override
    {
        ++calls;
        writing = std::move(value);
        pending = Promise<ConfigurationFileCommit>();
        if (!hold)
            complete();
        return pending.task();
    }
};
ValidatedCandidate candidate(const char* value)
{
    return validateCandidate(
               parseConfiguration(std::string("formatVersion: 1\nplugins: [{id: item, plugin: p, config: ") + value +
                                  "}]\n")
                   .value(),
               {{"p", {}, [](const std::string&) { return Result<void>::success(); }}})
        .value();
}
struct Scene {
    std::shared_ptr<Scheduler> scheduler = std::make_shared<Scheduler>();
    std::shared_ptr<Driver> driver = std::make_shared<Driver>();
    std::shared_ptr<Writer> writer = std::make_shared<Writer>();
    ConfigurationCoordinator coordinator{scheduler, driver};
    ConfigurationStore store{scheduler, writer, writer->disk};
    void run() { scheduler->queue.runUntilIdle(10000).value(); }
    AcceptedTarget accept(const char* value, std::uint64_t revision)
    {
        auto result = coordinator.accept(candidate(value), revision).value();
        store.track(coordinator.snapshot().value().desired, result.revision).value();
        return result;
    }
    void close()
    {
        writer->hold = false;
        auto a = coordinator.close();
        auto b = store.close();
        run();
        CHECK(a.tryResult()->value().quiescent && b.tryResult()->hasValue());
    }
};
void commit()
{
    Scene s;
    auto first = s.accept("old", 0);
    s.run();
    CHECK(first.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto saved = s.store.save(1).value();
    s.run();
    CHECK(saved.tryResult()->hasValue() && !s.store.snapshot().value().dirty);
    auto next = std::make_shared<const ValidatedCandidate>(candidate("new"));
    {
        auto prepared = TargetCommitAccess::prepare(s.coordinator, s.store, next, 1).value();
        CHECK(prepared.ready() && prepared.revision() == 2);
        CHECK(!s.coordinator.accept(candidate("nested"), 1).hasValue());
        CHECK(!s.store.track(next, 2).hasValue() && !s.store.save(1).hasValue());
        bool workerReady = true;
        std::thread worker([&] { workerReady = prepared.ready(); });
        worker.join();
        CHECK(!workerReady);
        const int boundaries = s.scheduler->boundaries, checks = s.driver->checks;
        CHECK(prepared.ready());
        prepared.publish();
        prepared.publish();
        CHECK(s.scheduler->boundaries == boundaries && s.driver->checks == checks && s.writer->calls == 1);
        auto runtime = s.coordinator.snapshot().value();
        auto disk = s.store.snapshot().value();
        CHECK(runtime.desiredRevision == 2 && disk.desiredRevision == 2 && runtime.desired == next &&
              disk.desired == next);
        CHECK(!runtime.activeRevision && disk.dirty && disk.persistedRevision == 1 && disk.persisted != next);
        CHECK(first.settled.tryResult()->value().revision == 1);
    }
    auto third = s.accept("later", 2);
    s.run();
    CHECK(third.settled.tryResult()->value().revision == 3);
    s.close();
}
void abortPreparation()
{
    Scene s;
    s.accept("old", 0);
    s.run();
    const auto old = s.store.snapshot().value().desired;
    auto next = std::make_shared<const ValidatedCandidate>(candidate("new"));
    {
        auto prepared = TargetCommitAccess::prepare(s.coordinator, s.store, next, 1).value();
        CHECK(prepared.ready());
    }
    CHECK(s.coordinator.snapshot().value().desired == old && s.store.snapshot().value().desired == old);
    CHECK(!TargetCommitAccess::prepare(s.coordinator, s.store, next, 0).hasValue());
    CHECK(!TargetCommitAccess::prepare(s.coordinator, s.store, {}, 1).hasValue());
    ConfigurationStore foreign(s.scheduler, s.writer, s.writer->disk);
    foreign.track(std::make_shared<const ValidatedCandidate>(*old), 1).value();
    CHECK(!TargetCommitAccess::prepare(s.coordinator, foreign, next, 1).hasValue());
    auto otherScheduler = std::make_shared<Scheduler>();
    auto otherWriter = std::make_shared<Writer>();
    ConfigurationStore other(otherScheduler, otherWriter, otherWriter->disk);
    other.track(old, 1).value();
    CHECK(!TargetCommitAccess::prepare(s.coordinator, other, next, 1).hasValue());
    auto otherDone = other.close();
    otherScheduler->queue.runUntilIdle().value();
    CHECK(otherDone.tryResult()->hasValue());
    s.accept("retry", 1);
    s.run();
    auto done = foreign.close();
    s.run();
    CHECK(done.tryResult()->hasValue());
    s.close();
}
void busy()
{
    Scene s;
    s.driver->hold = true;
    auto accepted = s.accept("old", 0);
    auto next = std::make_shared<const ValidatedCandidate>(candidate("new"));
    CHECK(!TargetCommitAccess::prepare(s.coordinator, s.store, next, 1).hasValue());
    s.run();
    CHECK(!TargetCommitAccess::prepare(s.coordinator, s.store, next, 1).hasValue());
    s.driver->pending.resolve({StepOutcome::Applied, true, {}});
    s.run();
    CHECK(accepted.settled.tryResult()->hasValue());
    std::optional<Task<CleanupReport>> closed;
    {
        auto prepared = TargetCommitAccess::prepare(s.coordinator, s.store, next, 1).value();
        closed.emplace(s.coordinator.close());
        CHECK(!prepared.ready() && !closed->tryResult());
    }
    s.run();
    CHECK(closed->tryResult()->value().quiescent && s.store.snapshot().value().desiredRevision == 1);
    s.close();
}
void writing()
{
    Scene s;
    s.accept("old", 0);
    s.run();
    s.writer->hold = true;
    auto saved = s.store.save(1).value();
    s.run();
    CHECK(s.store.snapshot().value().writing);
    auto next = std::make_shared<const ValidatedCandidate>(candidate("new"));
    {
        auto prepared = TargetCommitAccess::prepare(s.coordinator, s.store, next, 1).value();
        CHECK(prepared.ready());
        prepared.publish();
    }
    s.writer->complete();
    s.run();
    auto state = s.store.snapshot().value();
    CHECK(saved.tryResult()->value().revision == 1 && state.persistedRevision == 1 && state.desiredRevision == 2);
    CHECK(state.dirty && state.persisted != next && state.desired == next);
    auto current = s.store.save(2).value();
    s.run();
    s.writer->complete();
    s.run();
    CHECK(current.tryResult()->value().revision == 2 && !s.store.snapshot().value().dirty);
    s.close();
}
void reentry()
{
    Scene s;
    s.accept("old", 0);
    s.run();
    auto next = std::make_shared<const ValidatedCandidate>(candidate("new"));
    // 第一次开放性查询属于 Coordinator，第二次来自 Store；后者关闭先前已准备的账目。
    s.scheduler->onOpen = [&] { s.scheduler->onOpen = [&] { s.coordinator.close(); }; };
    CHECK(!TargetCommitAccess::prepare(s.coordinator, s.store, next, 1).hasValue());
    s.run();
    CHECK(s.coordinator.close().tryResult()->value().quiescent);
    CHECK(s.store.snapshot().value().desiredRevision == 1 && s.writer->calls == 0);
    s.close();
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "commit")
            commit();
        else if (name == "abort")
            abortPreparation();
        else if (name == "busy")
            busy();
        else if (name == "writing")
            writing();
        else if (name == "reentry")
            reentry();
        else
            throw std::runtime_error("unknown target commit case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
