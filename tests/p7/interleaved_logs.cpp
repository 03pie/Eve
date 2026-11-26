/** @brief Load two Qt plugins, invoke actual palette commands, and reconcile their visible output with disk. */
#include "workbench.hpp"
#include "../support/module_artifacts.hpp"
#include <ElaWindow.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <cordis/module/qt_module.hpp>
#include <iostream>

using namespace cordis;
using namespace cordis::ui;
namespace files = cordis::testing::modules;

namespace {
void check(bool value, const char* reason)
{
    if (!value) throw std::runtime_error(reason);
}
template<class T> T take(Result<T> result)
{
    if (!result.hasValue()) throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
void until(const std::function<bool()>& condition, const char* reason)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        check(timer.elapsed() < 8000, reason);
        QTest::qWait(5);
    }
}
void wait(const Task<void>& task)
{
    until([&] { return task.isReady(); }, "task not settled");
    take(*task.tryResult());
}
template<class T> T* control(ElaWindow* window, const char* name)
{
    auto* result = window->findChild<T*>(name);
    check(result, name);
    return result;
}
void save(const QString& path, const QJsonObject& receipt)
{
    files::write(path, QJsonDocument(receipt).toJson());
}
const QStringList markers{"A1", "B1", "A2", "B2"};
QString pluginFor(const QString& marker) { return marker.startsWith('A') ? "u7.alpha" : "u7.bravo"; }
QStringList eventLines(QPlainTextEdit* panel)
{
    return panel->toPlainText().split('\n').filter("  U7 event ");
}
struct Host {
    workbench::Workbench app;
    bool closed = false;
    void close()
    {
        wait(app.close());
        check(take(app.scheduler->snapshot()).callbackErrors == 0, "scheduler callback errors");
        take(app.scheduler->close());
        closed = true;
    }
    ~Host()
    {
        if (closed) return;
        try { close(); }
        catch (const std::exception& error) { std::cerr << "cleanup: " << error.what() << '\n'; }
    }
};

void run(const QString& dll, const QDir& evidence)
{
    const QStringList ids{"u7.alpha", "u7.bravo"};
    const auto manifest = files::install(dll, evidence.filePath("module"), "org.cordis.test.logs",
                                         QJsonArray::fromStringList(ids), "qt-ui");
    const auto loaded = take(module::loadQtModule(evidence.path().toStdString(), manifest.toStdString()));
    Host host;
    take(host.app.shell.startOutputArchive(evidence.filePath("workspace").toStdString(), "U7"));
    take(host.app.shell.management()->attach(host.app.kernel));
    host.app.publishUi();
    QJsonObject identities;
    for (const auto& id : ids) {
        const module::QtPlugin contract{id.toStdString(),
            {ServiceRequirement::of<CommandService>({"ui.commands"}), ServiceRequirement::of<OutputService>({"ui.output"})}, {}};
        const auto runtime = take(module::qtRuntime(loaded.generation, contract, host.app.scheduler));
        const auto registration = take(host.app.kernel->registerRuntime(contract.id, runtime));
        const auto instance = take(host.app.kernel->mount(registration));
        wait(instance.ready());
        const auto state = take(instance.snapshot());
        check(state.state == InstanceState::Active, "missing active plugin");
        identities[id] = QJsonObject{{"runtime", QString::number(state.runtime.value())},
                                     {"instance", QString::number(state.instance.value())}, {"epoch", QString::number(state.epoch)}};
    }
    check(identities[ids[0]].toObject()["instance"] != identities[ids[1]].toObject()["instance"], "duplicate instance");
    ElaWindow* window = nullptr;
    for (auto* widget : QApplication::topLevelWidgets())
        if (auto* shell = qobject_cast<ElaWindow*>(widget)) window = shell;
    check(window, "missing shell");
    window->setAttribute(Qt::WA_ShowWithoutActivating);
    take(host.app.shell.show());
    control<QAction>(window, "CordisOpenOutput")->trigger();
    auto* editor = control<QPlainTextEdit>(window, "CordisWorkbenchOutput");
    auto* selector = control<QComboBox>(window, "CordisOutputTarget");
    auto* commandFilter = control<QLineEdit>(window, "CordisCommandFilter");
    auto* list = control<QListView>(window, "CordisCommandList");
    const auto runId = control<QComboBox>(window, "CordisOutputRun")->currentData().toString();
    check(!QUuid(runId).isNull(), "missing archive run ID");
    check(selector->findData("plugin/u7.alpha") < 0 && selector->findData("plugin/u7.bravo") < 0, "silent source visible");
    for (const auto& marker : markers) {
        control<QAction>(window, "CordisOpenCommands")->trigger();
        commandFilter->setText("U7 publish " + marker.left(1));
        until([&] { return commandFilter->isVisible() && list->model()->rowCount() == 1; }, "no unique plugin command");
        check(list->currentIndex().data().toString().contains(commandFilter->text()), "wrong palette command");
        control<QPushButton>(window, "CordisRunCommand")->click();
        until([&] { return eventLines(editor).join('\n').contains("U7 event " + marker); }, "plugin command did not publish");
    }
    const auto all = eventLines(editor);
    check(all.size() == markers.size(), "unexpected count of interleaved events");
    for (int i = 0; i < markers.size(); ++i)
        check(all[i].endsWith("U7 event " + markers[i]), "interleaved output ordering");
    control<QAction>(window, "CordisOpenSettings")->trigger();
    control<QSpinBox>(window, "CordisSettingsPanelHeight")->setValue(300);
    control<QLineEdit>(window, "CordisOutputSearch")->setText("U7 event");
    until([&] { return editor->toPlainText().trimmed() == all.join('\n'); }, "visible search missed an event");
    QTextCursor first(editor->document()), last(editor->document());
    last.movePosition(QTextCursor::End);
    check(editor->viewport()->rect().intersects(editor->cursorRect(first)) &&
          editor->viewport()->rect().intersects(editor->cursorRect(last)), "four marker rows must fit in screenshot");
    QJsonObject views{{"all", QJsonArray::fromStringList(all)}};
    check(window->grab().save(evidence.filePath("all.png")), "all source screenshot");
    for (const auto& id : ids) {
        const auto index = selector->findData("plugin/" + id);
        check(index > 0, "missing plugin source");
        selector->setCurrentIndex(index);
        const auto expected = all.filter(id + " #");
        check(expected.size() == 2, "unexpected source name or instance");
        until([&] { return eventLines(editor).join('\n') == expected.join('\n'); }, "filtered text contains another source");
        check(editor->isVisible(), "output pane hidden");
        views[id] = QJsonArray::fromStringList(expected);
        check(window->grab().save(evidence.filePath(id + ".png")), "single source screenshot");
    }
    selector->setCurrentIndex(0);
    until([&] { return eventLines(editor).join('\n') == all.join('\n'); }, "return to all sources failed");
    host.close();
    const QDir archive(evidence.filePath("workspace/.cordis/logs/" + runId));
    const auto run = QJsonDocument::fromJson(files::read(archive.filePath("run.json"))).object();
    check(run.contains("ended"), "archive was not closed normally");
    QJsonArray events;
    quint64 lastId = 0;
    for (const auto& segment : archive.entryList({"events-*.jsonl"}, QDir::Files, QDir::Name)) {
        for (const auto& line : files::read(archive.filePath(segment)).trimmed().split('\n')) {
            QJsonParseError error;
            const auto row = QJsonDocument::fromJson(line, &error).object();
            check(error.error == QJsonParseError::NoError && !row.isEmpty(), "malformed JSONL");
            if (!row["text"].toString().startsWith("U7 event ")) continue;
            const int i = events.size();
            check(i < markers.size(), "extra disk event");
            const auto id = pluginFor(markers[i]);
            const auto identity = identities[id].toObject();
            const auto instance = identity["instance"].toString(), epoch = identity["epoch"].toString();
            const auto eventId = row["event"].toString().toULongLong();
            check(row["text"] == "U7 event " + markers[i] && row["source"] == "plugin/" + id && row["name"] == id &&
                  row["instance"] == identity["instance"] && row["epoch"] == identity["epoch"] &&
                  row["channel"] == "plugin/" + instance + "/" + epoch + "/shared" && eventId > lastId,
                  "JSONL source, event, instance or channel mismatch");
            check(all[i].startsWith("[" + row["sequence"].toString() + "] ") && all[i].endsWith(
                  id + " #" + instance + "/" + epoch + "  " + row["text"].toString()), "UI and JSONL disagree");
            lastId = eventId;
            events.append(row);
        }
    }
    check(events.size() == 4, "missing disk events");
    save(evidence.filePath("receipt.json"), {{"run", runId}, {"pid", QString::number(QCoreApplication::applicationPid())},
        {"qpa", QApplication::platformName()}, {"module", QJsonDocument::fromJson(files::read(manifest)).object()},
        {"generation", QString::number(loaded.generation.value())}, {"identities", identities}, {"views", views}, {"events", events}});
}
} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    try {
        const auto args = application.arguments();
        check(args.size() == 3 && QDir().mkpath(args[2]), "usage: interleaved_logs fixture.dll evidence-parent");
        QTemporaryDir evidence(QDir(args[2]).absoluteFilePath("interleaved-XXXXXX"));
        check(evidence.isValid(), "cannot create evidence directory");
        evidence.setAutoRemove(false);
        QStandardPaths::setTestModeEnabled(true);
        application.setApplicationName("CordisU7-" + QFileInfo(evidence.path()).fileName());
        std::cout << evidence.path().toStdString() << std::endl;
        run(args[1], QDir(evidence.path()));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
