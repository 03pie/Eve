/** @file module_qt_workspace.cpp
 * @brief 使用未加测试导出的业务 UI DLL，验证真实配置、页面操作、诊断与退出。
 */
#include "workspace.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <iostream>
using namespace cordis;
using namespace cordis::module;
using namespace cordis::loader;
using namespace cordis::ui;
using notebook::take;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
void pump(const std::function<bool()>& ready)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready()) {
        CHECK(timer.elapsed() < 5000);
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
}
template <class T> T wait(const Task<T>& task)
{
    pump([&] { return task.isReady(); });
    return take(*task.tryResult());
}
std::string source(const std::string& config = "{title: First, text: initial}", bool multi = false)
{
    auto text = "formatVersion: 1\nmodules: {notebook: {manifest: notebook/module.json}}\nplugins:\n"
                "  - {id: notes, plugin: example.notebook, module: notebook, config: " +
                config + "}\n";
    if (multi)
        text +=
            "  - {id: other, plugin: example.notebook, module: notebook, config: {title: Second, text: separate}}\n";
    return text;
}
std::size_t records(const notebook::Workspace& state, ResourceKind kind)
{
    const auto modules = take(state.manager->snapshot());
    const auto& rows = modules.at(0).resources.records;
    return static_cast<std::size_t>(
        std::count_if(rows.begin(), rows.end(), [=](const auto& row) { return row.kind == kind; }));
}
std::vector<PageSnapshot> pages(const notebook::Workspace& state)
{
    auto values = take(state.shell.pages()->snapshot());
    values.erase(std::remove_if(values.begin(), values.end(), [](const auto& row) { return row.id != "notebook"; }),
                 values.end());
    return values;
}
QWidget* page(InstanceId id)
{
    for (auto widget : QApplication::allWidgets())
        if (widget->objectName() == "CordisNotebookPage" &&
            widget->property("cordis.instance").toULongLong() == id.value())
            return widget;
    return nullptr;
}
void edit(notebook::Workspace& state, InstanceId id, const QString& text)
{
    auto widget = page(id);
    CHECK(widget);
    auto editor = widget->findChild<QPlainTextEdit*>("CordisNotebookEditor");
    auto button = widget->findChild<QPushButton*>("CordisNotebookPublish");
    CHECK(editor && button);
    editor->setPlainText(text);
    button->click();
    pump([&] {
        auto entries = take(state.shell.output()->snapshot());
        return !entries.empty() && entries.back().text == text.toStdString();
    });
    const auto output = take(state.shell.output()->snapshot());
    CHECK(!output.empty() && output.back().owner->instance == id && output.back().text == text.toStdString());
}
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try {
        CHECK(app.arguments().size() == 3);
        const auto name = app.arguments()[2];
        QTemporaryDir scratch;
        CHECK(scratch.isValid());
        auto state = notebook::Workspace::open(app.arguments()[1], scratch.filePath("session.yaml"));
        wait(state->ready());
        auto draft = take(state->session->preview(source()));
        CHECK(records(*state, ResourceKind::Instance) == 0 && pages(*state).empty());
        auto config = name == "start_failure"     ? "{title: Failure, failStart: true}"
                      : name == "cleanup_failure" ? "{title: Retained, failCleanup: true}"
                                                  : "{title: First, text: initial}";
        auto accepted = take(state->apply(source(config, name == "multi")));
        auto report = wait(accepted.settled);
        CHECK(report.outcome == (name == "start_failure" ? PlanOutcome::Failed : PlanOutcome::Applied));
        if (name == "start_failure") {
            CHECK(pages(*state).empty() && records(*state, ResourceKind::Instance) == 0);
            CHECK(!take(state->manager->snapshot()).at(0).businessFailure);
        } else {
            auto rows = pages(*state);
            CHECK(rows.size() == (name == "multi" ? 2u : 1u));
            const auto owner = rows.at(0).owner;
            edit(*state, owner.instance, QString::fromUtf8("来自真实 DLL 的编辑内容"));
            CHECK(take(state->shell.commands()->snapshot()).size() == rows.size());
            if (name == "multi") {
                CHECK(rows[0].owner.instance != rows[1].owner.instance);
                edit(*state, rows[1].owner.instance, "other content");
                CHECK(page(rows[0].owner.instance)->findChild<QPlainTextEdit*>()->toPlainText() != "other content");
            } else if (name == "update") {
                QPointer<QWidget> old = page(owner.instance);
                auto action = take(state->configurationManagement->updateConfiguration(
                    "notes", "{title: Changed, text: replacement}", accepted.revision));
                CHECK(wait(action).outcome == ConfigurationOutcome::Applied && !old);
                auto next = pages(*state);
                CHECK(next.size() == 1 &&
                      (next[0].owner.instance != owner.instance || next[0].owner.epoch > owner.epoch));
                CHECK(page(next[0].owner.instance)->findChild<QPlainTextEdit*>()->toPlainText() == "replacement");
            } else if (name == "toggle") {
                auto disabled = take(state->session->setEnabled("notes", false, accepted.revision));
                CHECK(wait(disabled.settled).outcome == PlanOutcome::Applied && pages(*state).empty());
                CHECK(take(state->shell.commands()->snapshot()).empty() &&
                      records(*state, ResourceKind::Instance) == 0);
                auto enabled = take(state->session->setEnabled("notes", true, disabled.revision));
                CHECK(wait(enabled.settled).outcome == PlanOutcome::Applied);
                CHECK(pages(*state).size() == 1 && pages(*state)[0].owner.instance != owner.instance);
            } else if (name == "add") {
                auto action = take(state->configurationManagement->addInstance("added", "example.notebook",
                                                                               "{title: Added}", accepted.revision));
                CHECK(wait(action).outcome == ConfigurationOutcome::Applied && pages(*state).size() == 2);
                const auto& modules =
                    take(state->session->snapshot()).coordinator.desired->source().structure().modules();
                CHECK(modules.at("notebook") == "notebook/module.json");
                CHECK(!state->configurationManagement->addInstance("stale", "example.notebook", "{}", accepted.revision)
                           .hasValue());
            } else if (name == "save") {
                CHECK(wait(take(state->session->save(accepted.revision))).outcome == SaveOutcome::Written);
                const auto disk = take(readConfigurationFile(scratch.filePath("session.yaml")));
                CHECK(disk.version.exists &&
                      disk.text == take(state->session->snapshot()).coordinator.desired->source().normalizedYaml());
            } else if (name == "diagnostics") {
                auto widget = notebook::moduleDiagnostics(state->manager);
                auto text = widget->findChild<QPlainTextEdit*>("CordisModuleDiagnosticsText");
                CHECK(text && text->isReadOnly() && text->toPlainText().contains("org.cordis.example.notebook"));
                CHECK(text->toPlainText().contains("qt.instance.") && text->toPlainText().contains("nativeProcessPin"));
                widget->findChild<QPushButton*>("CordisModuleDiagnosticsRefresh")->click();
                CHECK(records(*state, ResourceKind::Instance) == 1);
            } else if (name == "guards") {
                CHECK(take(state->shell.diagnostics()->snapshot()).instances.empty());
                CHECK(!state->apply(source("{title: 12}")).hasValue());
                auto invalid = source();
                auto position = invalid.find("notebook/module.json");
                invalid.replace(position, std::string("notebook/module.json").size(), "other/module.json");
                CHECK(!state->apply(invalid).hasValue() && pages(*state).size() == 1);
                CHECK(!state->configurationManagement->addInstance("notes", "example.notebook", "{}", accepted.revision)
                           .hasValue());
            } else if (name == "output_limit") {
                auto widget = page(owner.instance);
                const auto before = take(state->shell.output()->snapshot()).size();
                widget->findChild<QPlainTextEdit*>()->setPlainText(QString(5000, 'x'));
                widget->findChild<QPushButton*>()->click();
                auto feedback = widget->findChild<QLabel*>("CordisNotebookFeedback");
                CHECK(feedback);
                pump([&] { return feedback->text().contains(QString::fromUtf8("输出失败")); });
                CHECK(take(state->shell.output()->snapshot()).size() == before);
            } else
                CHECK(name == "normal" || name == "cleanup_failure" || name == "close_conflict");
        }
        if (name == "close_conflict") {
            QFile external(scratch.filePath("session.yaml"));
            CHECK(external.open(QIODevice::WriteOnly));
            CHECK(external.write("external file\n") == 14);
            external.close();
            auto failed = state->close();
            pump([&] { return failed.isReady(); });
            CHECK(!failed.tryResult()->hasValue() && !take(state->session->snapshot()).closing);
            CHECK(pages(*state).size() == 1 && records(*state, ResourceKind::Instance) == 1);
            take(state->session->rebaseExternal(accepted.revision));
        }
        auto closed = state->close();
        if (name == "cleanup_failure") {
            pump([&] { return closed.isReady(); });
            CHECK(!closed.tryResult()->hasValue() && records(*state, ResourceKind::Instance) == 1);
            CHECK(pages(*state).empty() && take(state->shell.commands()->snapshot()).empty());
            CHECK(!state->manager->drain(state->generation).hasValue());
            auto widget = notebook::moduleDiagnostics(state->manager, state->shell.diagnostics());
            auto text = widget->findChild<QPlainTextEdit*>()->toPlainText();
            CHECK(text.contains("qt.instance.") && text.contains("notebook cleanup failure requested"));
        } else {
            wait(closed);
            pump([&] { return take(state->scheduler->snapshot()).queued == 0; });
            CHECK(records(*state, ResourceKind::Instance) == 0 && records(*state, ResourceKind::ExternalHandle) == 0);
            CHECK(records(*state, ResourceKind::QObject) == 1 && !state->manager->drain(state->generation).hasValue());
            CHECK(take(state->scheduler->snapshot()).callbackErrors == 0);
            take(state->scheduler->close());
        }
        std::cout << "Qt workspace case passed: " << name.toStdString() << '\n';
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
