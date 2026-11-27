/** @file notebook_state_runtime.cpp
 * @brief 实际 Notebook 的草稿、同步输入、Frozen Scope 和完整页面退出检查。
 */
#include "../../src/core/instance_state_p.hpp"
#include "notebook_state_scene_p.hpp"
#include <ElaWindow.h>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QVariant>

namespace notebook_test {
namespace {
Instance active(Scene& scene, const Imported& imported)
{
    NOTEBOOK_CHECK(wait(imported.parent.ready(), scene.scheduler)->hasValue() && *imported.child);
    return **imported.child;
}
QString showPage(Scene& scene, const Instance& instance)
{
    auto* page = scene.page(instance);
    NOTEBOOK_CHECK(page);
    const auto key = page->property("ElaPageKey").toString();
    NOTEBOOK_CHECK(!key.isEmpty());
    scene.window->navigation(key);
    until([&] { return page->isVisible(); });
    return key;
}
void publish(Scene& scene, const Instance& instance, const QString& text)
{
    const auto before = scene.outputCount(text.toStdString());
    scene.publish(instance)->click();
    until([&] { return scene.outputCount(text.toStdString()) == before + 1; });
    const auto entries = take(scene.shell.output()->snapshot());
    const auto owner = take(instance.snapshot()).instance;
    NOTEBOOK_CHECK(!entries.empty() && entries.back().owner && entries.back().owner->instance == owner &&
                   entries.back().text == text.toStdString());
}
void roundtrip(const QString& root, const QString& variant)
{
    Scene scene(root, variant);
    const auto importedText = QString::fromUtf8("restored 中文 😀\r\nnext line \"quoted\" \\ tail\r\n");
    const auto imported = scene.inject(draftState(importedText));
    const auto instance = active(scene, imported);
    const auto key = showPage(scene, instance);
    QPointer<QWidget> oldPage = scene.page(instance);
    QPointer<QPlainTextEdit> oldEditor = scene.editor(instance);
    QPointer<QPushButton> oldButton = scene.publish(instance);
    NOTEBOOK_CHECK(scene.pageCount() == 1 && scene.commandCount() == 1);
    NOTEBOOK_CHECK(oldEditor->toPlainText().contains(QString::fromUtf8("restored 中文")) &&
                   oldEditor->toPlainText() != "configuration seed");
    NOTEBOOK_CHECK(scene.capture(instance).json == draftJson(importedText));
    publish(scene, instance, importedText);
    const auto edited = QString::fromUtf8("last synchronous edit 中文\nnew draft");
    oldEditor->setPlainText(edited);
    // 故意不 wait/barrier/pump；上一条 textChanged 必须已同步进入唯一 draft。
    NOTEBOOK_CHECK(scene.capture(instance).json == draftJson(edited));
    publish(scene, instance, edited);
    scene.stop(imported);
    NOTEBOOK_CHECK(!oldPage && !oldEditor && !oldButton && scene.pageCount() == 0 && scene.commandCount() == 0);
    NOTEBOOK_CHECK(!scene.window->isNodePresentable(key));
    scene.window->navigation(key);
    barrier(scene.scheduler);
    NOTEBOOK_CHECK(scene.pageCount() == 0 && scene.commandCount() == 0 && !scene.page(instance));
    scene.close();
}
void empty(const QString& root)
{
    Scene scene(root, "a");
    const auto imported = scene.inject(draftState(QString{}));
    const auto instance = active(scene, imported);
    NOTEBOOK_CHECK(scene.editor(instance)->toPlainText().isEmpty());
    NOTEBOOK_CHECK(scene.capture(instance).json == "{\"draft\":\"\"}");
    scene.close();
}
void limits(const QString& root)
{
    Scene scene(root, "b");
    const QString boundary(32768, QChar(0x00e9));
    NOTEBOOK_CHECK(boundary.toUtf8().size() == 65536);
    const auto imported = scene.inject(draftState(boundary));
    const auto instance = active(scene, imported);
    NOTEBOOK_CHECK(scene.capture(instance).json == draftJson(boundary));
    const auto originalPins = scene.moduleState().resources.pinReasons;
    const QString oversized(65537, QLatin1Char('x'));
    scene.editor(instance)->setPlainText(oversized);
    {
        auto scope = scene.freeze();
        const auto rejected = eve::detail::InstanceStateAccess::capture(scope, instance);
        NOTEBOOK_CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::InvalidArgument);
        NOTEBOOK_CHECK(scene.editor(instance)->toPlainText() == oversized);
        const auto state = scene.moduleState();
        NOTEBOOK_CHECK(!state.failure && !state.businessFailure && state.resources.pinReasons == originalPins);
        take(scope.release());
    }
    scene.editor(instance)->setPlainText("shortened after limit");
    NOTEBOOK_CHECK(scene.capture(instance).json == "{\"draft\":\"shortened after limit\"}");
    scene.close();
}
void multi(const QString& root)
{
    Scene scene(root, "a");
    const auto first = scene.inject(draftState("first"));
    const auto one = active(scene, first);
    const auto key = showPage(scene, one);
    QPointer<QWidget> old = scene.page(one);
    const auto second = scene.inject(draftState("second"));
    const auto two = active(scene, second);
    NOTEBOOK_CHECK(scene.pageCount() == 2 && scene.commandCount() == 2);
    scene.editor(one)->setPlainText("only first changed");
    NOTEBOOK_CHECK(scene.capture(one).json == "{\"draft\":\"only first changed\"}");
    NOTEBOOK_CHECK(scene.capture(two).json == "{\"draft\":\"second\"}" && scene.editor(two)->toPlainText() == "second");
    scene.stop(first);
    NOTEBOOK_CHECK(!old && scene.pageCount() == 1 && scene.commandCount() == 1 && scene.page(two));
    scene.window->navigation(key);
    barrier(scene.scheduler);
    NOTEBOOK_CHECK(!scene.window->isNodePresentable(key) && scene.pageCount() == 1 && scene.commandCount() == 1);
    publish(scene, two, "second");
    scene.close();
}
void frozen(const QString& root)
{
    Scene scene(root, "b");
    const auto initial = QString::fromUtf8("model before freeze\r\n中文");
    const auto imported = scene.inject(draftState(initial));
    const auto instance = active(scene, imported);
    showPage(scene, instance);
    auto* editor = scene.editor(instance);
    const auto projection = editor->toPlainText();
    const auto before = take(scene.shell.output()->snapshot()).size();
    {
        auto scope = scene.freeze();
        editor->setPlainText("program signal while frozen");
        NOTEBOOK_CHECK(editor->toPlainText() == projection);
        QTest::keyClicks(editor, "physical input while frozen");
        NOTEBOOK_CHECK(editor->toPlainText() == projection);
        scene.publish(instance)->click();
        const auto captured = take(eve::detail::InstanceStateAccess::capture(scope, instance));
        NOTEBOOK_CHECK(captured && captured->json == draftJson(initial));
        NOTEBOOK_CHECK(take(scene.shell.output()->snapshot()).size() == before);
        take(scope.release());
    }
    barrier(scene.scheduler);
    NOTEBOOK_CHECK(take(scene.shell.output()->snapshot()).size() == before);
    editor->setPlainText("editing resumed");
    NOTEBOOK_CHECK(scene.capture(instance).json == "{\"draft\":\"editing resumed\"}");
    publish(scene, instance, "editing resumed");
    scene.close();
}
void rejections(const QString& root)
{
    Scene scene(root, "a");
    const auto good = scene.inject(draftState("unchanged good page"));
    const auto activeInstance = active(scene, good);
    QPointer<QWidget> retained = scene.page(activeInstance);
    const std::vector<StateSnapshot> invalid{{{"another.schema", 1}, "{\"draft\":\"bad\"}"},
                                             {{schemaId, 2}, "{\"draft\":\"bad\"}"},
                                             {{schemaId, 1}, "{\"draft\":0}"},
                                             {{schemaId, 1}, "{\"draft\":\"first\",\"draft\":\"last\"}"},
                                             {{schemaId, 1}, " {\"draft\":\"bad\"}"},
                                             draftState(QString(65537, QLatin1Char('x')))};
    for (const auto& state : invalid) {
        const auto rejected = scene.inject(state);
        NOTEBOOK_CHECK(!wait(rejected.parent.ready(), scene.scheduler)->hasValue());
        scene.stop(rejected);
        NOTEBOOK_CHECK(retained && scene.pageCount() == 1 && scene.commandCount() == 1);
        NOTEBOOK_CHECK(!scene.moduleState().businessFailure);
    }
    NOTEBOOK_CHECK(scene.capture(activeInstance).json == "{\"draft\":\"unchanged good page\"}");
    scene.close();
}
void startFailure(const QString& root)
{
    Scene scene(root, "b");
    const auto good = scene.inject(draftState("retained successful instance"));
    const auto original = active(scene, good);
    QPointer<QWidget> retained = scene.page(original);
    const auto rejected = scene.inject(draftState("imported then failed"),
                                       "{\"title\":\"Failure\",\"text\":\"wrong seed\",\"failStart\":true}");
    const auto ready = wait(rejected.parent.ready(), scene.scheduler);
    NOTEBOOK_CHECK(!ready->hasValue() && ready->error().code == ErrorCode::ActivationFailed);
    scene.stop(rejected);
    NOTEBOOK_CHECK(retained && scene.pageCount() == 1 && scene.commandCount() == 1 &&
                   !scene.moduleState().businessFailure);
    NOTEBOOK_CHECK(scene.capture(original).json == "{\"draft\":\"retained successful instance\"}");
    scene.close();
}
void legacy(const QString& root)
{
    Scene scene(root, "legacy");
    const auto normal = scene.inject();
    const auto instance = active(scene, normal);
    NOTEBOOK_CHECK(scene.editor(instance)->toPlainText() == "configuration seed");
    scene.editor(instance)->setPlainText("legacy edited normally");
    publish(scene, instance, "legacy edited normally");
    const auto rejected = scene.inject(draftState("state unavailable"));
    NOTEBOOK_CHECK(!wait(rejected.parent.ready(), scene.scheduler)->hasValue());
    scene.stop(rejected);
    NOTEBOOK_CHECK(scene.pageCount() == 1 && scene.commandCount() == 1);
    {
        auto scope = scene.freeze();
        NOTEBOOK_CHECK(!take(eve::detail::InstanceStateAccess::capture(scope, instance)));
        take(scope.release());
    }
    scene.close();
}
} // namespace
bool runtimeCase(const QString& name, const QString& root)
{
    if (name == "draft_a" || name == "draft_b")
        roundtrip(root, name == "draft_a" ? "a" : "b");
    else if (name == "draft_empty")
        empty(root);
    else if (name == "draft_limits")
        limits(root);
    else if (name == "draft_multi")
        multi(root);
    else if (name == "draft_frozen")
        frozen(root);
    else if (name == "draft_rejections")
        rejections(root);
    else if (name == "draft_start_failure")
        startFailure(root);
    else if (name == "legacy")
        legacy(root);
    else
        return false;
    return true;
}
} // namespace notebook_test
