/** @file session.cpp
 * @brief 真实 Kernel/Coordinator/Store 验证候选、期望启停、保存及关闭装配。
 */
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/session.hpp>
#include <cordis/testing/fake_scheduler.hpp>
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
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
/** @brief 只模拟文件回执，不代替 Qt 真实文件测试；版本为合法形状的受控值。 */
struct Writer final : IConfigurationWriter {
    ConfigurationFileSnapshot disk{{"memory/config.yaml", false, {}}, {}};
    std::function<void()> onCommit;
    bool hold = false;
    std::optional<Promise<ConfigurationFileCommit>> pending;
    std::shared_ptr<const ValidatedCandidate> target;
    ConfigurationFileVersion base;
    int calls = 0;
    /** @brief 完成时比较基线，不因目标已接受就覆盖外部文件。 */
    void complete()
    {
        if (base.exists != disk.version.exists || base.sha256 != disk.version.sha256) {
            pending->resolve(
                {FileCommitOutcome::Conflict, disk, makeError(ErrorCode::ConfigConflict, "external edit")});
            return;
        }
        disk = {{disk.version.path, true, std::string(64, static_cast<char>('a' + calls % 5))},
                target->source().normalizedYaml()};
        pending->resolve({FileCommitOutcome::Committed, disk, {}});
    }
    /** @brief 显式 gate 允许保存与后续目标并存，保存时不检查运行成功。 */
    Task<ConfigurationFileCommit> commit(std::shared_ptr<const ValidatedCandidate> value,
                                         ConfigurationFileVersion expected) override
    {
        ++calls;
        target = std::move(value);
        base = std::move(expected);
        pending.emplace();
        auto hook = std::exchange(onCommit, {});
        if (hook)
            hook();
        if (!hold)
            complete();
        return pending->task();
    }
};
/** @brief 虚拟投递边界重入宿主，真实工作仍遵循 FakeScheduler FIFO。 */
struct ReentrantScheduler final : IScheduler {
    testing::FakeScheduler queue;
    std::function<void()> onPost;
    /** @brief 一次性钩子允许在线性化接受后、返回调用方前请求关闭。 */
    bool post(ScheduledJob job) override
    {
        auto hook = std::exchange(onPost, {});
        if (hook)
            hook();
        return queue.post(std::move(job));
    }
    /** @brief 保持 owner 判断。 */
    bool isCurrentThread() const noexcept override { return queue.isCurrentThread(); }
    /** @brief 不伪报关闭。 */
    bool isOpen() const override { return queue.isOpen(); }
};
/** @brief 每项独立持有真实内核，schema 和 activate 的调用次数分别观察。 */
struct Scene {
    std::shared_ptr<testing::FakeScheduler> scheduler = std::make_shared<testing::FakeScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    std::shared_ptr<Writer> writer = std::make_shared<Writer>();
    std::shared_ptr<ConfigurationSession> session;
    int activations = 0;
    bool failActivation = false;
    bool failCleanup = false;
    std::function<void()> schemaHook;
    /** @brief 子实例与根共享固定定义，测试不会按候选注册变体。 */
    Scene(PluginModule module = {})
    {
        auto runtime =
            kernel
                ->registerPlugin("demo",
                                 [&](Context context) {
                                     ++activations;
                                     if (failCleanup)
                                         context
                                             .onCleanup([](CleanupContext) {
                                                 return failedTask(makeError(ErrorCode::CleanupFailed, "retained"));
                                             })
                                             .value();
                                     return failActivation
                                                ? failedTask(makeError(ErrorCode::ActivationFailed, "runtime failed"))
                                                : completedTask();
                                 })
                .value();
        auto driver = std::make_shared<KernelReconcileDriver>(scheduler, kernel,
                                                              std::vector<KernelPlugin>{{"demo", runtime, {}, module}});
        session =
            std::make_shared<ConfigurationSession>(scheduler, driver,
                                                   std::vector<PluginDefinition>{{"demo",
                                                                                  {},
                                                                                  [&](const std::string&) {
                                                                                      if (schemaHook)
                                                                                          schemaHook();
                                                                                      return Result<void>::success();
                                                                                  },
                                                                                  module}},
                                                   writer, writer->disk);
    }
    /** @brief 不用 sleep 猜测协议阶段。 */
    void pump() { scheduler->runUntilIdle().value(); }
    /** @brief 默认目标含子项及容易被错误文本替换的配置负载。 */
    ConfigurationDraft draft()
    {
        return session
            ->preview("formatVersion: 1\nplugins: [{id: r, plugin: demo, config: {disabled: false, id: r}, children: "
                      "[{id: c, plugin: demo, config: [a, b]}]}]")
            .value();
    }
    /** @brief 最后确认独立内核也静止，再关闭 scheduler。 */
    void close()
    {
        auto task = session->close();
        pump();
        CHECK(task.isReady() && task.tryResult()->hasValue());
        auto all = kernel->close();
        pump();
        CHECK(all.tryResult()->value().quiescent);
        scheduler->close();
    }
};
/** @brief 预览无副作用、跨会话/过期/已移动候选拒绝，应用才接受并跟踪 Store。 */
void previewCas()
{
    Scene scene, other;
    auto draft = scene.draft(), stale = scene.draft();
    CHECK(scene.activations == 0 && scene.writer->calls == 0 &&
          scene.session->snapshot().value().coordinator.desiredRevision == 0);
    CHECK(!other.session->apply(draft).hasValue());
    auto accepted = scene.session->apply(draft).value();
    scene.pump();
    auto state = scene.session->snapshot().value();
    CHECK(state.store.desiredRevision == accepted.revision && state.coordinator.desired == state.store.desired &&
          state.store.dirty && scene.activations == 2);
    CHECK(scene.session->apply(stale).error().code == ErrorCode::ConfigConflict);
    auto retained = std::move(stale);
    CHECK(!scene.session->apply(stale).hasValue() && retained.candidate().source().isValid());
    CHECK(!scene.session->preview("formatVersion: 1\nplugins: [{id: r, plugin: missing}]").hasValue());
    other.close();
    scene.close();
}
/** @brief 只改变本地 enabled；父级级联、配置负载及其他子项不被错误改写。 */
void toggles()
{
    Scene scene;
    scene.session->apply(scene.draft()).value();
    scene.pump();
    auto before = scene.session->snapshot().value().coordinator.desired->source().structure().entries().at("r").config;
    scene.session->setEnabled("r", false, 1).value();
    scene.pump();
    auto stopped = scene.session->snapshot().value();
    CHECK(stopped.coordinator.running.empty() &&
          stopped.coordinator.desired->source().structure().entries().at("r/c").effectiveDisabled);
    scene.session->setEnabled("r/c", true, 2).value();
    scene.pump();
    CHECK(scene.session->snapshot().value().coordinator.running.empty());
    CHECK(scene.session->setEnabled("r", true, 2).error().code == ErrorCode::ConfigConflict);
    scene.session->setEnabled("r", true, 3).value();
    scene.pump();
    CHECK(scene.session->snapshot().value().coordinator.running.size() == 2 && scene.activations == 4);
    CHECK(scene.session->snapshot().value().coordinator.desired->source().structure().entries().at("r").config ==
          before);
    CHECK(!scene.session->setEnabled("r/missing", false, 4).hasValue());
    scene.close();
}
/** @brief 保存旧修订不覆盖新目标 dirty；外部冲突保留原文，显式 rebase 后重试。 */
void saving()
{
    Scene scene;
    scene.session->apply(scene.draft()).value();
    scene.pump();
    scene.writer->hold = true;
    auto first = scene.session->save(1).value();
    scene.pump();
    scene.session->setEnabled("r", false, 1).value();
    scene.pump();
    scene.writer->complete();
    scene.pump();
    CHECK(first.tryResult()->value().revision == 1 && scene.session->snapshot().value().store.dirty);
    scene.writer->hold = false;
    scene.writer->disk = {{"memory/config.yaml", true, std::string(64, 'f')}, "external text"};
    auto conflict = scene.session->save(2).value();
    scene.pump();
    CHECK(conflict.tryResult()->value().outcome == SaveOutcome::Conflict && scene.writer->disk.text == "external text");
    CHECK(scene.session->snapshot().value().store.external->text == "external text");
    scene.session->rebaseExternal(2).value();
    auto retried = scene.session->save(2).value();
    scene.pump();
    CHECK(retried.tryResult()->value().outcome == SaveOutcome::Written &&
          !scene.session->snapshot().value().store.dirty);
    scene.close();
}
/** @brief 运行失败不回滚期望，也不阻止用户保存期望；重试保持真实结果分离。 */
void runtimeFailure()
{
    Scene scene;
    scene.failActivation = true;
    auto accepted = scene.session->apply(scene.draft()).value();
    scene.pump();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Failed);
    auto saved = scene.session->save(1).value();
    scene.pump();
    CHECK(saved.tryResult()->value().outcome == SaveOutcome::Written && !scene.session->snapshot().value().store.dirty);
    scene.failActivation = false;
    auto again = scene.session->apply(scene.session->preview(scene.writer->disk.text).value()).value();
    scene.pump();
    CHECK(again.settled.tryResult()->value().outcome == PlanOutcome::Applied &&
          scene.session->snapshot().value().store.dirty);
    scene.close();
}
/** @brief schema 重入关闭时拒绝候选；保存回调重入关闭仍等待在途写及驱动排空。 */
void reentry()
{
    Scene preview;
    std::optional<Task<void>> closed;
    preview.schemaHook = [&] { closed.emplace(preview.session->close()); };
    CHECK(!preview.session->preview("formatVersion: 1\nplugins: [{id: r, plugin: demo}]").hasValue());
    preview.pump();
    CHECK(closed->tryResult()->hasValue() && preview.activations == 0 && preview.writer->calls == 0);
    preview.close();
    Scene write;
    write.session->apply(write.draft()).value();
    write.pump();
    write.writer->hold = true;
    write.writer->onCommit = [&] { closed.emplace(write.session->close()); };
    auto saved = write.session->save(1).value();
    write.pump();
    CHECK(!closed->isReady() && !saved.isReady());
    write.writer->complete();
    write.pump();
    CHECK(closed->tryResult()->hasValue() && saved.tryResult()->hasValue() && write.session->snapshot().value().closed);
    write.close();

    auto scheduler = std::make_shared<ReentrantScheduler>();
    auto kernel = std::make_shared<Kernel>(scheduler);
    auto writer = std::make_shared<Writer>();
    auto runtime = kernel->registerPlugin("demo", [](Context) { return completedTask(); }).value();
    auto driver =
        std::make_shared<KernelReconcileDriver>(scheduler, kernel, std::vector<KernelPlugin>{{"demo", runtime}});
    auto session = std::make_shared<ConfigurationSession>(
        scheduler, driver,
        std::vector<PluginDefinition>{{"demo", {}, [](const std::string&) { return Result<void>::success(); }}}, writer,
        writer->disk);
    auto draft = session->preview("formatVersion: 1\nplugins: [{id: r, plugin: demo}]").value();
    scheduler->onPost = [&] { closed.emplace(session->close()); };
    auto accepted = session->apply(draft).value();
    scheduler->queue.runUntilIdle().value();
    CHECK(closed->tryResult()->hasValue() && session->snapshot().value().store.persistedRevision == accepted.revision);
    CHECK(writer->disk.text == session->snapshot().value().coordinator.desired->source().normalizedYaml());
    auto all = kernel->close();
    scheduler->queue.runUntilIdle().value();
    CHECK(all.tryResult()->value().quiescent);
    scheduler->queue.close();
}
/** @brief 错误线程及过早停 scheduler 不伪装成关闭成功。 */
void guards()
{
    Scene scene;
    bool wrong = false;
    std::thread worker([&] { wrong = scene.session->snapshot().error().code == ErrorCode::WrongThread; });
    worker.join();
    CHECK(wrong);
    scene.session->apply(scene.draft()).value();
    scene.pump();
    scene.scheduler->close();
    auto closed = scene.session->close();
    CHECK(closed.isReady() && !closed.tryResult()->hasValue());
}
/** @brief 写入成功也不能掩盖运行清理失败，两个独立证明必须同时成立。 */
void shutdownFailure()
{
    Scene scene;
    scene.failCleanup = true;
    scene.session->apply(scene.draft()).value();
    scene.pump();
    auto closed = scene.session->close();
    scene.pump();
    CHECK(closed.isReady() && !closed.tryResult()->hasValue() &&
          scene.session->snapshot().value().store.persistedRevision == 1);
    CHECK(!scene.session->snapshot().value().coordinator.running.empty());
    auto all = scene.kernel->close();
    scene.pump();
    CHECK(!all.tryResult()->value().quiescent);
    scene.scheduler->close();
}
/** @brief 冻结目录驱动追加；精确模块、CAS、未知定义和重入不能绕过完整预检。 */
void moduleAdd()
{
    Scene scene({"external", "plugins/notebook/module.json"});
    CHECK(!scene.session->addInstance("one", "unknown", "{}", 0).hasValue());
    auto added = scene.session->addInstance("one", "demo", "{integer: 9007199254740993}", 0).value();
    scene.pump();
    CHECK(added.settled.tryResult()->value().outcome == PlanOutcome::Applied && scene.activations == 1);
    auto state = scene.session->snapshot().value();
    CHECK(state.coordinator.desired->source().structure().modules().at("external") == "plugins/notebook/module.json");
    CHECK(state.coordinator.desired->source().structure().entries().at("one").module == "external");
    CHECK(state.coordinator.desired->source().normalizedYaml().find("9007199254740993") != std::string::npos);
    CHECK(!scene.session->addInstance("two", "demo", "{}", 0).hasValue());
    CHECK(!scene.session->addInstance("one", "demo", "{}", added.revision).hasValue());
    auto conflict =
        addConfigurationEntry(state.coordinator.desired->source(), "two", "demo", "{}", {}, {"external", "other.json"});
    CHECK(!conflict.hasValue());
    ErrorCode wrong = ErrorCode::InvalidArgument;
    std::thread worker([&] { wrong = scene.session->addInstance("two", "demo", "{}", added.revision).error().code; });
    worker.join();
    CHECK(wrong == ErrorCode::WrongThread);
    scene.schemaHook = [&] { scene.session->close(); };
    CHECK(!scene.session->addInstance("two", "demo", "{}", added.revision).hasValue());
    scene.pump();
    CHECK(scene.session->snapshot().value().coordinator.desiredRevision == added.revision && scene.activations == 1);
}
/** @brief 每项协议使用独立进程，失败产生明确断言。 */
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "preview")
            previewCas();
        else if (name == "toggle")
            toggles();
        else if (name == "save")
            saving();
        else if (name == "failure")
            runtimeFailure();
        else if (name == "reentry")
            reentry();
        else if (name == "guards")
            guards();
        else if (name == "shutdown_failure")
            shutdownFailure();
        else if (name == "module_add")
            moduleAdd();
        else
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
