/** @file value_publication_faults.cpp
 * @brief 真实发布放弃、Qt 丢弃及 Scope 中页面/命令/纯值的共同提交与恢复。
 */
#include "ui_publication_p.hpp"
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <algorithm>
#include <cordis/replacement_scope.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace presentation_test;

/** @brief 同时检查原通知历史、直接输出和通知镜像，不以状态栏代替历史证据。 */
void values(Scene& scene, const std::string& tag, std::size_t expected)
{
    const auto notifications = take(scene.shell.notifications()->snapshot());
    const auto output = take(scene.shell.output()->snapshot());
    CHECK(std::count_if(notifications.begin(), notifications.end(), [&](const NotificationSnapshot& value) {
              return value.notification.text == tag + "-notice";
          }) == static_cast<std::ptrdiff_t>(expected));
    for (const auto& text : {tag + "-output", tag + "-notice"})
        CHECK(std::count_if(output.begin(), output.end(), [&](const OutputEntry& value) {
                  return value.text == text;
              }) == static_cast<std::ptrdiff_t>(expected));
}

/** @brief 故障进程中的激活/清理只捕获真实服务，不借用已经返回的 Scene 栈。 */
Runtime valueRuntime(Scene& scene, const std::string& tag, bool cleanupFailure = false)
{
    return take(
        scene.kernel->registerPlugin(tag, [output = scene.shell.output(), notifications = scene.shell.notifications(),
                                           tag, cleanupFailure](Context context) {
            take(output->write(context, "value", tag + "-output"));
            take(notifications->post(context, {NotificationLevel::Info, tag + "-notice", {}}));
            if (cleanupFailure)
                take(context.onCleanup([](CleanupContext) {
                    return failedTask(makeError(ErrorCode::CleanupFailed, "deliberate value cleanup failure"));
                }));
            return completedTask();
        }));
}

void abortFailure()
{
    Scene scene;
    const auto label = scene.find<QLabel>("CordisNotification");
    const auto before = label->text();
    const auto group = take(scene.kernel->holdPublication());
    auto instance = take(scene.kernel->mountHeld(group, valueRuntime(scene, "abort-failure", true)));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    CHECK(take(scene.kernel->services().snapshot()).empty());
    values(scene, "abort-failure", 0);
    CHECK(label->text() == before && scene.find<QComboBox>("CordisOutputTarget")->count() == 1);
    take(group.abort());
    CHECK(take(group.snapshot()).state == PublicationState::Aborted);
    const auto stopped = wait(instance.stop(), scene.scheduler);
    CHECK(stopped->hasValue() && !stopped->value().quiescent && !stopped->value().errors.empty());
    values(scene, "abort-failure", 0);
    CHECK(label->text() == before && scene.find<QComboBox>("CordisOutputTarget")->count() == 1);
    CHECK(take(scene.kernel->services().snapshot()).empty());
    const auto closed = wait(scene.kernel->close(), scene.scheduler);
    CHECK(closed->hasValue() && !closed->value().quiescent);
    CHECK(take(scene.scheduler->snapshot()).callbackErrors == 0);
    // 永久清理失败保留原实例 pin；独立故障进程不调用要求静止的 Scene::close。
}

void discarded()
{
    Scene scene;
    auto ordinary = take(scene.kernel->mount(valueRuntime(scene, "retained")));
    CHECK(wait(ordinary.ready(), scene.scheduler)->hasValue());
    values(scene, "retained", 1);
    const auto beforeOutput = take(scene.shell.output()->snapshot()).size();
    const auto beforeNotices = take(scene.shell.notifications()->snapshot()).size();
    const auto label = scene.find<QLabel>("CordisNotification");
    const auto before = label->text();
    const auto targets = scene.find<QComboBox>("CordisOutputTarget")->count();
    const auto group = take(scene.kernel->holdPublication());
    auto instance = take(scene.kernel->mountHeld(group, valueRuntime(scene, "discarded")));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    values(scene, "discarded", 0);
    take(group.commit());
    // QtScheduler 同线程也不内联；尚未执行的原 watch 作业由真实 close 丢弃。
    take(scene.scheduler->close());
    const auto scheduler = take(scene.scheduler->snapshot());
    CHECK(scheduler.discarded > 0 && scheduler.callbackErrors == 0);
    const auto output = scene.shell.output()->snapshot();
    const auto notifications = scene.shell.notifications()->snapshot();
    CHECK(!output.hasValue() && output.error().code == ErrorCode::SchedulerClosed);
    CHECK(!notifications.hasValue() && notifications.error().code == ErrorCode::SchedulerClosed);
    CHECK(label->text() == before && scene.find<QComboBox>("CordisOutputTarget")->count() == targets);
    CHECK(!scene.find<QPlainTextEdit>("CordisWorkbenchOutput")->toPlainText().contains("discarded"));
    const auto closing = scene.kernel->close();
    if (const auto report = closing.tryResult())
        CHECK(!report->hasValue() || !report->value().quiescent);
    CHECK(take(scene.kernel->snapshot()).closing);
    // QObjectResource 仍通过实际 Qt DeferredDelete 确认；不用已关闭 scheduler 观察回执。
    const auto shellClosed = scene.shell.close();
    until([&] { return shellClosed.isReady(); });
    CHECK(shellClosed.tryResult()->hasValue() && !scene.window);
    CHECK(take(scene.shell.output()->snapshot()).size() == beforeOutput);
    CHECK(take(scene.shell.notifications()->snapshot()).size() == beforeNotices);
    values(scene, "retained", 1);
    values(scene, "discarded", 0);
    CHECK(take(scene.scheduler->snapshot()).callbackErrors == 0);
}

struct Run {
    ContextIdentity owner;
    std::string tag;
    std::optional<PageHandle> page;
    std::optional<CommandHandle> command;
    QPointer<QWidget> widget;
    bool prepared = false, externallyBlocked = false;
};
struct Trace {
    unsigned created = 0, destroyed = 0, executions = 0;
    std::vector<std::shared_ptr<Run>> runs;
};
/** @brief 真实页面本身保留计数拥有者，任何故障退出都不借用栈上的析构探针。 */
class Page final : public QWidget {
public:
    explicit Page(std::shared_ptr<Trace> trace) : trace_(std::move(trace)) { ++trace_->created; }
    ~Page() override { ++trace_->destroyed; }

private:
    std::shared_ptr<Trace> trace_;
};
/** @brief 激活只在两个真实 ready 完成后结算，失败分支也先证明完整准备与隐藏。 */
struct Readiness {
    std::shared_ptr<Run> run;
    unsigned pending = 2;
    bool failing = false;
    std::optional<ErrorInfo> failure;
    Promise<void> result;

    void complete(const Result<void>& outcome)
    {
        if (!outcome.hasValue() && !failure)
            failure = outcome.error();
        if (--pending)
            return;
        if (failure) {
            result.reject(*failure);
            return;
        }
        const auto page = run->page->snapshot();
        run->prepared = page.hasValue() && page.value().widgetAlive && run->widget;
        run->externallyBlocked = page.hasValue() && !page.value().navigationPresent && !page.value().current &&
                                 !run->command->enabled().hasValue();
        if (failing)
            result.reject(makeError(ErrorCode::ActivationFailed, "candidate rejected after UI preparation"));
        else
            result.resolve();
    }
};
Runtime mixedRuntime(Scene& scene, const std::shared_ptr<Trace>& trace, std::string title, bool failing = false)
{
    return Runtime([pages = scene.pages, commands = scene.commands, output = scene.shell.output(),
                    notifications = scene.shell.notifications(), scheduler = scene.scheduler, trace,
                    title = std::move(title), failing](Context context) {
        auto run = std::make_shared<Run>();
        run->owner = take(context.identity());
        run->tag = title + "-" + std::to_string(trace->runs.size() + 1);
        trace->runs.push_back(run);
        const std::weak_ptr<Trace> weakTrace = trace;
        const std::weak_ptr<Run> weakRun = run;
        run->page.emplace(take(pages->add(context, {"page", title, "value publication", [weakTrace, weakRun] {
                                                        auto page = std::make_unique<Page>(weakTrace.lock());
                                                        if (auto owner = weakRun.lock())
                                                            owner->widget = page.get();
                                                        return page;
                                                    }})));
        run->command.emplace(take(commands->add(context, {"command",
                                                          title,
                                                          [weakTrace] {
                                                              if (auto state = weakTrace.lock())
                                                                  ++state->executions;
                                                              return completedTask();
                                                          },
                                                          {}})));
        take(output->write(context, "value", run->tag + "-output"));
        take(notifications->post(context, {NotificationLevel::Info, run->tag + "-notice", {}}));
        auto ready = std::make_shared<Readiness>();
        ready->run = run;
        ready->failing = failing;
        (void)run->page->ready().thenOn(scheduler, [ready](const Result<void>& result) { ready->complete(result); });
        (void)run->command->ready().thenOn(scheduler, [ready](const Result<void>& result) { ready->complete(result); });
        return ready->result.task();
    });
}
void unpublished(Scene& scene, const std::shared_ptr<Run>& run)
{
    CHECK(run->prepared && run->externallyBlocked);
    CHECK(run->widget && !run->widget->isVisible());
    CHECK(!run->page->navigate().hasValue() && !run->command->enabled().hasValue());
    CHECK(!run->command->execute().tryResult()->hasValue());
    values(scene, run->tag, 0);
}
void scope(bool recover)
{
    Scene scene;
    const auto previousTrace = std::make_shared<Trace>(), nextTrace = std::make_shared<Trace>();
    const auto previous =
        take(scene.kernel->registerRuntime("values-scope", mixedRuntime(scene, previousTrace, "previous")));
    const auto next = mixedRuntime(scene, nextTrace, "candidate", recover);
    auto original = take(scene.kernel->mount(previous));
    CHECK(wait(original.ready(), scene.scheduler)->hasValue());
    const auto old = previousTrace->runs.front();
    CHECK(old->prepared);
    take(old->page->navigate());
    CHECK(wait(old->command->execute(), scene.scheduler)->hasValue() && previousTrace->executions == 1);
    values(scene, old->tag, 1);
    auto transaction = take(scene.kernel->reserveReplacement({previous.id()}));
    take(transaction.prepareDefinitions({{previous.id(), next}}));
    CHECK(wait(transaction.stop(), scene.scheduler)->value().quiescent);
    CHECK(!old->widget && previousTrace->destroyed == 1);
    CHECK(!old->page->navigate().hasValue() && !old->command->enabled().hasValue());
    auto staged = take(transaction.stage(next));
    const auto activated = wait(staged.ready(), scene.scheduler);
    const auto candidate = nextTrace->runs.front();
    CHECK(candidate->prepared && candidate->externallyBlocked);
    values(scene, candidate->tag, 0);
    auto active = candidate;
    auto activeInstance = staged;
    if (recover) {
        CHECK(!activated->hasValue() && activated->error().code == ErrorCode::ActivationFailed);
        CHECK(wait(transaction.discard(), scene.scheduler)->value().quiescent);
        CHECK(!candidate->widget && nextTrace->created == nextTrace->destroyed);
        CHECK(!candidate->page->navigate().hasValue() && !candidate->command->enabled().hasValue());
        auto restored = take(transaction.stage(previous));
        CHECK(wait(restored.ready(), scene.scheduler)->hasValue() && previousTrace->runs.size() == 2);
        active = previousTrace->runs.back();
        activeInstance = restored;
        unpublished(scene, active);
        take(transaction.prepareCommit(ReplacementCommitMode::Recovery));
    } else {
        CHECK(activated->hasValue());
        unpublished(scene, candidate);
        take(transaction.prepareCommit());
    }
    take(transaction.commitPrepared());
    CHECK(take(transaction.snapshot()).state == ReplacementScopeState::Committed);
    CHECK(!active->page->navigate().hasValue() && !active->command->enabled().hasValue());
    values(scene, active->tag, 0); // Published 仍在原共同提交交付屏障中。
    take(transaction.release());
    barrier(scene.scheduler);
    CHECK(take(scene.kernel->replacements()).empty());
    CHECK(take(activeInstance.snapshot()).state == InstanceState::Active);
    CHECK(take(activeInstance.snapshot()).instance == active->owner.instance);
    const auto kernel = take(scene.kernel->snapshot());
    CHECK(kernel.names.size() == 1 && kernel.names.front().id == (recover ? previous.id() : next.id()));
    take(active->page->navigate());
    CHECK(take(active->page->snapshot()).current && take(active->command->enabled()));
    CHECK(wait(active->command->execute(), scene.scheduler)->hasValue());
    CHECK((recover ? previousTrace : nextTrace)->executions == (recover ? 2U : 1U));
    values(scene, old->tag, 1);
    values(scene, active->tag, 1);
    if (recover)
        values(scene, candidate->tag, 0);
    CHECK(!old->page->navigate().hasValue() && !old->command->enabled().hasValue());
    scene.close();
    CHECK(previousTrace->created == previousTrace->destroyed && nextTrace->created == nextTrace->destroyed);
}
} // namespace

bool valuePublicationFaultCase(const QString& name)
{
    if (name == "values_abort_failure")
        abortFailure();
    else if (name == "values_discarded")
        discarded();
    else if (name == "values_scope_commit")
        scope(false);
    else if (name == "values_scope_recover")
        scope(true);
    else
        return false;
    return true;
}
