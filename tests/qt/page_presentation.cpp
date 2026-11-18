/** @file page_presentation.cpp
 * @brief 真实页面完整准备后等待原发布组，清理和恢复仍走原作用域。
 */
#include "ui_publication_p.hpp"
#include <ElaSuggestBox.h>
#include <QAbstractItemModel>
#include <QLineEdit>
#include <QTabBar>
#include <QTest>
#include <QTreeView>
#include <algorithm>
#include <cordis/replacement_scope.hpp>
#include <optional>
#include <vector>

namespace {
using namespace presentation_test;
struct Trace {
    unsigned created = 0, destroyed = 0;
    std::vector<PageHandle> pages;
    std::vector<QPointer<QWidget>> widgets;
};
class Page final : public QWidget {
public:
    explicit Page(Trace& trace) : trace_(trace)
    {
        ++trace_.created;
        auto* edit = new QLineEdit(this);
        edit->setObjectName("PublicationEditor");
        edit->setText("original");
    }
    ~Page() override { ++trace_.destroyed; }

private:
    Trace& trace_;
};
Runtime runtime(Scene& scene, Trace& trace, const std::string& title, bool failing = false, unsigned count = 1,
                PagePresentation presentation = PagePresentation::Tab)
{
    return Runtime([&scene, &trace, title, failing, count, presentation](Context context) {
        for (unsigned index = 0; index != count; ++index) {
            auto added = take(scene.pages->add(context, {title + std::to_string(index), title, "publication",
                                                         [&trace, failing]() -> std::unique_ptr<QWidget> {
                                                             if (failing)
                                                                 return {};
                                                             auto page = std::make_unique<Page>(trace);
                                                             trace.widgets.push_back(page.get());
                                                             return page;
                                                         },
                                                         presentation}));
            trace.pages.push_back(added);
        }
        return trace.pages.back().ready();
    });
}
Runtime registered(Scene& scene, Trace& trace, const std::string& title, bool failing = false, unsigned count = 1,
                   PagePresentation presentation = PagePresentation::Tab)
{
    return take(scene.kernel->registerRuntime(title, runtime(scene, trace, title, failing, count, presentation)));
}
void hidden(Scene& scene, const Trace& trace)
{
    CHECK(trace.created && trace.created == trace.widgets.size());
    for (const auto& page : trace.pages) {
        const auto snapshot = take(page.snapshot());
        CHECK(snapshot.widgetAlive && !snapshot.navigationPresent && !snapshot.current);
        CHECK(!page.navigate().hasValue());
    }
    for (const auto& widget : trace.widgets) {
        CHECK(widget && !widget->isVisible());
        const auto key = widget->property("ElaPageKey").toString();
        CHECK(!key.isEmpty() && !scene.window->getNavigationNodeIndex(key).isValid());
        CHECK(!scene.window->isNodePresentable(key));
        scene.window->navigation(key);
        CHECK(!widget->isVisible());
    }
}
void held()
{
    Scene scene;
    Trace trace, ordinary;
    auto definition = registered(scene, trace, "candidate", false, 2);
    auto group = take(scene.kernel->holdPublication());
    auto instance = take(scene.kernel->mountHeld(group, definition));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    CHECK(take(instance.snapshot()).state == InstanceState::Active);
    hidden(scene, trace);
    auto* search = scene.find<ElaSuggestBox>("CordisPageSearch");
    auto* suggestions = search->findChild<QAbstractItemModel*>();
    auto* edit = search->findChild<QLineEdit*>();
    auto* tabs = scene.find<QTabBar>("CordisPageTabs");
    CHECK(suggestions && edit);
    const auto tabsBefore = tabs->count();
    search->setSearchText("candidate");
    CHECK(suggestions->rowCount() == 0);
    CHECK(QMetaObject::invokeMethod(edit, "returnPressed", Qt::DirectConnection));
    CHECK(tabs->count() == tabsBefore && scene.window->findChildren<QWidget*>("CordisPluginCard").empty());
    auto normal = take(scene.kernel->mount(registered(scene, ordinary, "ordinary")));
    CHECK(wait(normal.ready(), scene.scheduler)->hasValue());
    take(ordinary.pages.front().navigate());
    CHECK(take(ordinary.pages.front().snapshot()).current);
    take(group.commit());
    barrier(scene.scheduler);
    CHECK(trace.created == 2 && trace.destroyed == 0);
    search->setSearchText("candidate");
    CHECK(suggestions->rowCount() == 2);
    const auto tabsBeforeSelection = tabs->count();
    CHECK(QMetaObject::invokeMethod(edit, "returnPressed", Qt::DirectConnection));
    CHECK(tabs->count() == tabsBeforeSelection + 1);
    CHECK(std::any_of(trace.pages.begin(), trace.pages.end(),
                      [](const PageHandle& page) { return take(page.snapshot()).current; }));
    CHECK(trace.created == 2);
    for (const auto& page : trace.pages) {
        CHECK(take(page.snapshot()).navigationPresent);
        take(page.navigate());
        CHECK(take(page.snapshot()).current);
    }
    const auto secondKey = trace.widgets.back()->property("ElaPageKey").toString();
    CHECK(wait(trace.pages.front().close(), scene.scheduler)->value().quiescent);
    CHECK(!trace.widgets.front() && trace.widgets.back());
    CHECK(scene.window->getNavigationNodeIndex(secondKey).isValid());
    take(trace.pages.back().navigate()); // 首个贡献退出不能使共享分类的其余页面失效。
    scene.close();
    CHECK(trace.destroyed == 2 && ordinary.destroyed == 1);
}
void aborted()
{
    Scene scene;
    Trace trace;
    auto group = take(scene.kernel->holdPublication());
    auto instance = take(scene.kernel->mountHeld(group, registered(scene, trace, "aborted")));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    hidden(scene, trace);
    take(group.abort());
    CHECK(wait(instance.stop(), scene.scheduler)->value().quiescent);
    CHECK(trace.destroyed == 1 && !trace.widgets.front());
    CHECK(!take(trace.pages.front().snapshot()).widgetAlive);
    CHECK(!trace.pages.front().navigate().hasValue());
    CHECK(take(scene.pages->snapshot()).empty());
    scene.close();
}
void factoryFailure()
{
    Scene scene;
    Trace trace;
    auto group = take(scene.kernel->holdPublication());
    auto instance = take(scene.kernel->mountHeld(group, registered(scene, trace, "rejected", true)));
    CHECK(!wait(instance.ready(), scene.scheduler)->hasValue());
    CHECK(trace.created == 0 && trace.pages.size() == 1);
    CHECK(!group.commit().hasValue());
    take(group.abort());
    CHECK(wait(instance.stop(), scene.scheduler)->value().quiescent);
    CHECK(!take(trace.pages.front().snapshot()).widgetAlive);
    scene.close();
}
void input()
{
    Scene scene;
    Trace trace;
    auto group = take(scene.kernel->holdPublication());
    auto instance = take(scene.kernel->mountHeld(group, registered(scene, trace, "input")));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    auto* editor = trace.widgets.front()->findChild<QLineEdit*>("PublicationEditor");
    CHECK(editor);
    QTest::keyClicks(editor, "hidden");
    CHECK(editor->text() == "original");
    take(group.commit());
    barrier(scene.scheduler);
    take(trace.pages.front().navigate());
    QTest::keyClicks(editor, "active");
    CHECK(editor->text() == "originalactive");
    scene.close();
    CHECK(trace.destroyed == 1);
}
void stoppedBeforeRefresh()
{
    Scene scene;
    Trace trace;
    auto group = take(scene.kernel->holdPublication());
    auto instance = take(scene.kernel->mountHeld(group, registered(scene, trace, "stopped")));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    take(group.commit());
    const auto stopped = instance.stop();
    CHECK(!trace.pages.front().navigate().hasValue());
    CHECK(wait(stopped, scene.scheduler)->value().quiescent);
    barrier(scene.scheduler);
    CHECK(!trace.widgets.front() && trace.destroyed == 1);
    scene.close();
}
void registrationClose()
{
    Scene scene;
    Trace trace;
    auto group = take(scene.kernel->holdPublication());
    auto* model = scene.find<QTreeView>("ElaNavigationView")->model();
    bool entering = false;
    std::optional<Task<void>> closed;
    QObject::connect(model, &QAbstractItemModel::modelAboutToBeReset, scene.window, [&] {
        if (!entering) {
            entering = true;
            closed = scene.shell.close();
        }
    });
    auto instance = take(scene.kernel->mountHeld(group, registered(scene, trace, "close")));
    (void)wait(instance.ready(), scene.scheduler);
    CHECK(entering && closed);
    CHECK(wait(instance.stop(), scene.scheduler)->value().quiescent);
    CHECK(wait(*closed, scene.scheduler)->hasValue());
    CHECK(trace.created == trace.destroyed);
    scene.close();
}
void scope(bool recover)
{
    Scene scene;
    Trace original, candidate;
    auto previous = take(scene.kernel->registerRuntime("scope", runtime(scene, original, "previous")));
    auto next = runtime(scene, candidate, "next", recover);
    auto instance = take(scene.kernel->mount(previous));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    take(original.pages.front().navigate());
    auto transaction = take(scene.kernel->reserveReplacement({previous.id()}));
    take(transaction.prepareDefinitions({{previous.id(), next}}));
    CHECK(wait(transaction.stop(), scene.scheduler)->value().quiescent);
    CHECK(!original.widgets.front() && original.destroyed == 1);
    auto staged = take(transaction.stage(next));
    const auto ready = wait(staged.ready(), scene.scheduler);
    if (recover) {
        CHECK(!ready->hasValue());
        CHECK(wait(transaction.discard(), scene.scheduler)->value().quiescent);
        auto restored = take(transaction.stage(previous));
        CHECK(wait(restored.ready(), scene.scheduler)->hasValue());
        CHECK(original.pages.size() == 2 && !original.pages.back().navigate().hasValue());
        take(transaction.prepareCommit(ReplacementCommitMode::Recovery));
    } else {
        CHECK(ready->hasValue());
        hidden(scene, candidate);
        take(transaction.prepareCommit());
    }
    take(transaction.commitPrepared());
    auto& active = recover ? original : candidate;
    CHECK(!active.pages.back().navigate().hasValue()); // 元数据提交中的 Published 尚未完成对外交付。
    take(transaction.release());
    barrier(scene.scheduler);
    CHECK(!original.pages.front().navigate().hasValue());
    take(active.pages.back().navigate());
    CHECK(take(active.pages.back().snapshot()).current);
    CHECK(take(scene.kernel->replacements()).empty());
    scene.close();
    CHECK(original.created == original.destroyed && candidate.created == candidate.destroyed);
}
void home()
{
    Scene scene;
    const auto definition = take(scene.kernel->registerPlugin("home", [](Context) { return completedTask(); }));
    auto ordinary = take(scene.kernel->mount(definition));
    CHECK(wait(ordinary.ready(), scene.scheduler)->hasValue());
    auto group = take(scene.kernel->holdPublication());
    auto held = take(scene.kernel->mountHeld(group, definition));
    CHECK(wait(held.ready(), scene.scheduler)->hasValue());
    until([&] { return scene.window->findChildren<QWidget*>("CordisPluginCard").size() == 1; });
    CHECK(take(scene.shell.diagnostics()->snapshot()).instances.size() == 2);
    take(group.commit());
    until([&] { return scene.window->findChildren<QWidget*>("CordisPluginCard").size() == 2; });
    scene.close();
}
void dialog()
{
    Scene scene;
    Trace trace;
    auto group = take(scene.kernel->holdPublication());
    auto instance =
        take(scene.kernel->mountHeld(group, registered(scene, trace, "dialog", false, 1, PagePresentation::Dialog)));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    const QPointer<QWidget> window = scene.find<QWidget>("CordisContributionDialog");
    CHECK(trace.created == 1 && !window->isVisible() && !trace.widgets.front()->isVisible());
    CHECK(!trace.pages.front().navigate().hasValue());
    take(group.commit());
    barrier(scene.scheduler);
    take(trace.pages.front().navigate());
    CHECK(window->isVisible() && trace.widgets.front()->isVisible() && take(trace.pages.front().snapshot()).current);
    CHECK(!take(trace.pages.front().snapshot()).navigationPresent);
    CHECK(wait(instance.stop(), scene.scheduler)->value().quiescent);
    CHECK(!window && trace.destroyed == 1 && !trace.widgets.front());
    CHECK(scene.window); // 贡献须自行完整销毁，不能依靠根窗口析构兜底。
    scene.close();
}
void observerDiscarded()
{
    Scene scene;
    struct Contributions {
        std::optional<PageHandle> page;
        std::optional<CommandHandle> command;
        QPointer<QWidget> widget;
    };
    const auto state = std::make_shared<Contributions>();
    const auto calls = std::make_shared<unsigned>(0);
    auto definition = take(scene.kernel->registerPlugin(
        "discarded", [pages = scene.pages, commands = scene.commands, state, calls](Context context) {
            state->command.emplace(take(commands->add(context, {"discarded",
                                                                "discarded",
                                                                [calls] {
                                                                    ++*calls;
                                                                    return completedTask();
                                                                },
                                                                {}})));
            const std::weak_ptr<Contributions> weak = state;
            state->page.emplace(take(pages->add(context, {"discarded", "discarded", {}, [weak] {
                                                              auto widget = std::make_unique<QWidget>();
                                                              if (auto held = weak.lock())
                                                                  held->widget = widget.get();
                                                              return widget;
                                                          }})));
            return state->page->ready();
        }));
    auto group = take(scene.kernel->holdPublication());
    auto instance = take(scene.kernel->mountHeld(group, definition));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    CHECK(state->widget && state->command && state->page);
    const auto key = state->widget->property("ElaPageKey").toString();
    barrier(scene.scheduler);
    CHECK(take(group.snapshot()).state == PublicationState::Open && !scene.window->isNodePresentable(key));
    const auto before = take(scene.scheduler->snapshot());
    CHECK(before.queued == 0);
    const auto evaluations = take(scene.kernel->services().dependencyEvaluationCount());
    // 本实例没有这个依赖；仍须通知 Context::presentation 的真实通用观察者。
    auto external = take(scene.kernel->services().stage({"unrelated-presentation"}, std::make_shared<int>(1)));
    take(external.publish());
    const auto accepted = take(scene.scheduler->snapshot());
    CHECK(accepted.queued > 0 && accepted.accepted > before.accepted);
    CHECK(take(scene.kernel->services().dependencyEvaluationCount()) == evaluations);
    take(group.commit());
    CHECK(scene.window->isNodePresentable(key));
    // 实际已接受的 Qt 作业尚未进入 owner；close 会调用它们的 discarded，不伪造原子诊断。
    take(scene.scheduler->close());
    CHECK(take(scene.scheduler->snapshot()).discarded > 0);
    take(external.retire());
    take(external.release());
    CHECK(take(external.snapshot()).state == BindingState::Released &&
          take(scene.kernel->services().snapshot()).empty());
    CHECK(take(scene.scheduler->snapshot()).callbackErrors == 0);
    CHECK(take(state->command->snapshot()).deliveryBlocked);
    CHECK(!scene.window->isNodePresentable(key));
    CHECK(!state->page->navigate().hasValue() && !state->command->enabled().hasValue());
    CHECK(!take(state->page->snapshot()).navigationPresent && !state->widget->isVisible() && *calls == 0);
    const auto closing = scene.kernel->close();
    if (const auto report = closing.tryResult())
        CHECK(!report->hasValue() || !report->value().quiescent);
    CHECK(take(scene.kernel->snapshot()).closing);
    // 故障进程保留未确认的真实资源；不将已关闭调度器上的退出写成正常静止。
}
} // namespace
bool pagePresentationCase(const QString& name)
{
    if (name == "pages_held")
        held();
    else if (name == "pages_aborted")
        aborted();
    else if (name == "pages_factory_failure")
        factoryFailure();
    else if (name == "pages_input")
        input();
    else if (name == "pages_stopped")
        stoppedBeforeRefresh();
    else if (name == "pages_registration_close")
        registrationClose();
    else if (name == "pages_scope_commit")
        scope(false);
    else if (name == "pages_scope_recover")
        scope(true);
    else if (name == "pages_home")
        home();
    else if (name == "pages_dialog")
        dialog();
    else if (name == "observer_discarded")
        observerDiscarded();
    else
        return false;
    return true;
}
