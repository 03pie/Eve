/** @file value_publication.cpp
 * @brief 真实发布组中的纯值接纳、序号、容量和窗口关闭，不伪造发布结果。
 */
#include "ui_publication_p.hpp"
#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <algorithm>
#include <optional>
#include <vector>

namespace {
using namespace presentation_test;
struct Owner {
    std::shared_ptr<std::optional<Context>> context;
    Instance instance;
};
Owner owner(Scene& scene, const PublicationGroup* group = nullptr)
{
    static unsigned next = 0;
    auto context = std::make_shared<std::optional<Context>>();
    const auto definition =
        take(scene.kernel->registerPlugin("values." + std::to_string(++next), [context](Context captured) {
            context->emplace(std::move(captured));
            return completedTask();
        }));
    auto instance = group ? take(scene.kernel->mountHeld(*group, definition)) : take(scene.kernel->mount(definition));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue() && *context);
    return {std::move(context), std::move(instance)};
}
std::size_t outputCount(Scene& scene, const std::string& text)
{
    const auto history = take(scene.shell.output()->snapshot());
    return static_cast<std::size_t>(
        std::count_if(history.begin(), history.end(), [&](const auto& entry) { return entry.text == text; }));
}
std::uint64_t write(Scene& scene, const Owner& caller, const std::string& text, const std::string& channel = "log")
{
    return take(scene.shell.output()->write(**caller.context, channel, text));
}
std::uint64_t post(Scene& scene, const Owner& caller, const std::string& text)
{
    return take(scene.shell.notifications()->post(**caller.context, {NotificationLevel::Info, text, {}}));
}
void held()
{
    Scene scene;
    auto group = take(scene.kernel->holdPublication());
    auto caller = owner(scene, &group);
    auto* targets = scene.find<QComboBox>("CordisOutputTarget");
    auto* label = scene.find<QLabel>("CordisNotification");
    const auto before = take(scene.shell.output()->snapshot());
    const auto status = label->text(), tooltip = label->toolTip();
    const auto selection = targets->currentData();
    unsigned changed = 0;
    auto connection = QObject::connect(scene.find<QPlainTextEdit>("CordisWorkbenchOutput"),
                                       &QPlainTextEdit::textChanged, targets, [&] { ++changed; });
    const auto outputSequence = write(scene, caller, "held-output");
    const auto noticeSequence = post(scene, caller, "held-notice");
    CHECK(outputSequence > before.back().sequence && noticeSequence == 1);
    barrier(scene.scheduler);
    CHECK(take(scene.shell.output()->snapshot()).size() == before.size());
    CHECK(take(scene.shell.notifications()->snapshot()).empty());
    CHECK(targets->count() == 2 && targets->currentData() == selection && changed == 0);
    CHECK(label->text() == status && label->toolTip() == tooltip);
    take(group.commit());
    // 只读结算不触 Qt；已复制的原观察作业仍须随后刷新 dirty 投影。
    CHECK(outputCount(scene, "held-output") == 1);
    CHECK(take(scene.shell.notifications()->snapshot()).front().sequence == noticeSequence);
    CHECK(targets->count() == 2 && label->text() == status);
    barrier(scene.scheduler);
    CHECK(targets->count() == 3 && targets->currentData() == selection);
    CHECK(label->text().contains("held-notice") && outputCount(scene, "held-notice") == 1);
    QObject::disconnect(connection);
    scene.close();
}
void order()
{
    Scene scene;
    auto firstGroup = take(scene.kernel->holdPublication()), secondGroup = take(scene.kernel->holdPublication());
    auto first = owner(scene, &firstGroup), second = owner(scene, &secondGroup), ordinary = owner(scene);
    const auto firstOutput = write(scene, first, "older-output");
    const auto secondOutput = write(scene, second, "newer-output");
    CHECK(post(scene, first, "older-notice") == 1 && post(scene, second, "newer-notice") == 2);
    take(secondGroup.commit());
    barrier(scene.scheduler);
    CHECK(scene.find<QLabel>("CordisNotification")->text().contains("newer-notice"));
    take(firstGroup.commit());
    barrier(scene.scheduler);
    const auto history = take(scene.shell.output()->snapshot());
    CHECK(std::is_sorted(history.begin(), history.end(),
                         [](const auto& a, const auto& b) { return a.sequence < b.sequence; }));
    CHECK(std::any_of(history.begin(), history.end(),
                      [&](const auto& row) { return row.sequence == firstOutput && row.text == "older-output"; }));
    CHECK(std::any_of(history.begin(), history.end(),
                      [&](const auto& row) { return row.sequence == secondOutput && row.text == "newer-output"; }));
    const auto notices = take(scene.shell.notifications()->snapshot());
    CHECK(notices.size() == 2 && notices[0].sequence == 1 && notices[1].sequence == 2);
    CHECK(scene.find<QLabel>("CordisNotification")->text().contains("newer-notice"));
    CHECK(outputCount(scene, "older-notice") == 1 && outputCount(scene, "newer-notice") == 1);
    auto aborted = take(scene.kernel->holdPublication());
    auto rejected = owner(scene, &aborted);
    const auto gap = write(scene, rejected, "aborted-output");
    CHECK(post(scene, rejected, "aborted-notice") == 3);
    take(aborted.abort());
    barrier(scene.scheduler);
    CHECK(!scene.shell.output()->write(**ordinary.context, "bad\nchannel", "bad").hasValue());
    CHECK(!scene.shell.notifications()->post(**ordinary.context, {NotificationLevel::Info, "\xff", {}}).hasValue());
    CHECK(write(scene, ordinary, "after-gap") == gap + 1);
    CHECK(post(scene, ordinary, "after-notice-gap") == 4);
    CHECK(outputCount(scene, "aborted-output") == 0 && outputCount(scene, "aborted-notice") == 0);
    scene.close();
}
void stopped()
{
    Scene scene;
    auto group = take(scene.kernel->holdPublication());
    auto caller = std::make_optional(owner(scene, &group));
    write(scene, *caller, "commit-then-stop");
    post(scene, *caller, "notice-then-stop");
    take(group.commit());
    const auto context = caller->context;
    const auto stopped = caller->instance.stop();
    caller.reset();
    group = take(scene.kernel->holdPublication());
    // stop 已同步关闭本代业务，但这些值的原组成功事实不能随 owner 消失。
    CHECK(!scene.shell.output()->write(**context, "log", "late").hasValue());
    CHECK(outputCount(scene, "commit-then-stop") == 1);
    CHECK(take(scene.shell.notifications()->snapshot()).back().notification.text == "notice-then-stop");
    CHECK(wait(stopped, scene.scheduler)->value().quiescent);
    CHECK(!(*context)->identity().hasValue());
    CHECK(outputCount(scene, "notice-then-stop") == 1);
    scene.close();
}
void closeBeforeRefresh()
{
    Scene scene;
    auto committed = take(scene.kernel->holdPublication()), open = take(scene.kernel->holdPublication());
    auto accepted = owner(scene, &committed), abandoned = owner(scene, &open);
    write(scene, accepted, "close-output");
    post(scene, accepted, "close-notice");
    write(scene, abandoned, "still-held-output");
    post(scene, abandoned, "still-held-notice");
    auto* action = scene.find<QAction>("CordisClearOutput");
    CHECK(action->isEnabled());
    unsigned closeAttempts = 0;
    bool rejected = true;
    QObject::connect(action, &QAction::changed, action, [&] {
        if (action->isEnabled())
            return;
        ++closeAttempts;
        const auto notification =
            scene.shell.notifications()->post(**accepted.context, {NotificationLevel::Info, "close-must-reject", {}});
        const auto command = scene.commands->add(
            **accepted.context, {"close-must-reject", "close-must-reject", [] { return completedTask(); }, {}});
        rejected = rejected && !notification.hasValue() && notification.error().code == ErrorCode::ContextClosing &&
                   !command.hasValue() && command.error().code == ErrorCode::ContextClosing;
    });
    take(committed.commit());
    // 不推进 observer；Shell 必须先纯结算通知镜像，再关 Output。
    const auto closed = scene.shell.close();
    CHECK(closeAttempts > 0);
    CHECK(rejected);
    CHECK(outputCount(scene, "close-output") == 1 && outputCount(scene, "close-notice") == 1);
    CHECK(outputCount(scene, "still-held-output") == 0 && outputCount(scene, "still-held-notice") == 0);
    const auto notices = take(scene.shell.notifications()->snapshot());
    CHECK(notices.size() == 1 && notices.front().notification.text == "close-notice");
    CHECK(wait(closed, scene.scheduler)->hasValue() && !scene.window);
    scene.close();
}
void clear()
{
    Scene scene;
    auto group = take(scene.kernel->holdPublication());
    auto caller = owner(scene, &group);
    write(scene, caller, "before-commit");
    take(group.commit());
    const auto later = write(scene, caller, "after-commit");
    const auto history = take(scene.shell.output()->snapshot());
    const auto row =
        std::find_if(history.begin(), history.end(), [&](const auto& entry) { return entry.sequence == later; });
    CHECK(row != history.end());
    auto* targets = scene.find<QComboBox>("CordisOutputTarget");
    targets->setCurrentIndex(0);
    scene.find<QAction>("CordisClearOutput")->trigger();
    barrier(scene.scheduler);
    CHECK(outputCount(scene, "before-commit") == 1 && outputCount(scene, "after-commit") == 1);
    CHECK(scene.find<QPlainTextEdit>("CordisWorkbenchOutput")->toPlainText().isEmpty());
    CHECK(write(scene, caller, "after-clear") == later + 1);
    auto future = take(scene.kernel->holdPublication());
    auto futureCaller = owner(scene, &future);
    post(scene, futureCaller, "future-mirror");
    scene.find<QAction>("CordisClearOutput")->trigger();
    take(future.commit());
    barrier(scene.scheduler);
    CHECK(outputCount(scene, "future-mirror") == 1);
    scene.close();
}
void reentry()
{
    Scene scene;
    auto ordinary = owner(scene);
    for (std::size_t index = 1; index < OutputService::historyLimit; ++index)
        write(scene, ordinary, "old-history");
    auto group = take(scene.kernel->holdPublication());
    auto caller = owner(scene, &group);
    write(scene, caller, "batch-output");
    post(scene, caller, "batch-notice-one");
    post(scene, caller, "batch-notice-two");
    std::optional<Task<void>> closed;
    auto* editor = scene.find<QPlainTextEdit>("CordisWorkbenchOutput");
    QObject::connect(editor, &QPlainTextEdit::textChanged, editor, [&] {
        if (!closed)
            closed.emplace(scene.shell.close());
    });
    take(group.commit());
    until([&] { return closed.has_value(); });
    CHECK(wait(*closed, scene.scheduler)->hasValue() && !scene.window);
    const auto notices = take(scene.shell.notifications()->snapshot());
    CHECK(notices.size() == 2 && notices[0].notification.text == "batch-notice-one" &&
          notices[1].notification.text == "batch-notice-two");
    CHECK(outputCount(scene, "batch-output") == 1 && outputCount(scene, "batch-notice-one") == 1 &&
          outputCount(scene, "batch-notice-two") == 1);
    scene.close();
}
void count()
{
    Scene scene;
    auto ordinary = owner(scene);
    auto group = take(scene.kernel->holdPublication());
    auto first = owner(scene, &group), second = owner(scene, &group);
    std::uint64_t last = 0;
    for (std::size_t index = 0; index < OutputService::pendingCountLimit; ++index)
        last = write(scene, index % 2 ? first : second, "pending-count");
    CHECK(!scene.shell.output()->write(**first.context, "log", "overflow").hasValue());
    for (std::size_t index = 0; index < NotificationService::pendingCountLimit; ++index)
        CHECK(post(scene, index % 2 ? first : second, "pending-count-notice") == index + 1);
    CHECK(!scene.shell.notifications()->post(**second.context, {NotificationLevel::Info, "overflow", {}}).hasValue());
    // 候选满额不阻断当前业务；失败提交不占号，不淘汰已公开记录。
    CHECK(write(scene, ordinary, "ordinary-at-capacity") == last + 1);
    CHECK(post(scene, ordinary, "ordinary-notice-at-capacity") == NotificationService::pendingCountLimit + 1);
    CHECK(scene.find<QComboBox>("CordisOutputTarget")->count() == 3);
    CHECK(outputCount(scene, "pending-count") == 0 && take(scene.shell.notifications()->snapshot()).size() == 1);
    take(group.abort());
    barrier(scene.scheduler);
    CHECK(post(scene, ordinary, "capacity-rejected-no-number") == NotificationService::pendingCountLimit + 2);
    CHECK(outputCount(scene, "ordinary-at-capacity") == 1 && outputCount(scene, "pending-count-notice") == 0);
    scene.close();
}
void bytes()
{
    Scene scene;
    auto ordinary = owner(scene);
    auto group = take(scene.kernel->holdPublication());
    auto caller = owner(scene, &group);
    const std::string payload(OutputService::textLimit, 'x');
    std::size_t accepted = 0;
    std::uint64_t last = 0;
    for (; accepted < OutputService::pendingCountLimit; ++accepted) {
        const auto result = scene.shell.output()->write(**caller.context, "log", payload);
        if (!result.hasValue())
            break;
        last = result.value();
    }
    CHECK(accepted > 200 && accepted < 300);
    CHECK(write(scene, ordinary, "byte-refusal-no-number") == last + 1);
    const auto noticeCount = NotificationService::pendingByteLimit / NotificationService::textLimit;
    for (std::size_t index = 0; index < noticeCount; ++index)
        CHECK(post(scene, caller, payload) == index + 1);
    CHECK(!scene.shell.notifications()->post(**caller.context, {NotificationLevel::Info, payload, {}}).hasValue());
    CHECK(post(scene, ordinary, "notice-byte-refusal-no-number") == noticeCount + 1);
    CHECK(outputCount(scene, payload) == 0);
    take(group.abort());
    barrier(scene.scheduler);
    CHECK(outputCount(scene, payload) == 0);
    scene.close();
}
void targets()
{
    Scene scene;
    auto group = take(scene.kernel->holdPublication());
    auto first = owner(scene, &group), second = owner(scene, &group);
    std::uint64_t last = 0;
    for (std::size_t index = 0; index < OutputService::pendingTargetLimit; ++index)
        last = write(scene, index % 2 ? first : second, "target-pending", std::to_string(index));
    CHECK(!scene.shell.output()->write(**first.context, "extra-target", "overflow").hasValue());
    CHECK(write(scene, first, "existing-target", "1") == last + 1);
    CHECK(scene.find<QComboBox>("CordisOutputTarget")->count() == 2);
    take(group.commit());
    barrier(scene.scheduler);
    // The two published owners each contribute a source even when both choose the same channel labels.
    auto targets=scene.find<QComboBox>("CordisOutputTarget");
    CHECK(targets->count()==4 && targets->findData("platform")>=0);
    CHECK(outputCount(scene, "existing-target") == 1);
    scene.close();
}
} // namespace

bool valuePublicationCase(const QString& name)
{
    if (name == "values_held")
        held();
    else if (name == "values_order")
        order();
    else if (name == "values_stopped")
        stopped();
    else if (name == "values_close")
        closeBeforeRefresh();
    else if (name == "values_clear")
        clear();
    else if (name == "values_reentry")
        reentry();
    else if (name == "values_count")
        count();
    else if (name == "values_bytes")
        bytes();
    else if (name == "values_targets")
        targets();
    else
        return false;
    return true;
}
