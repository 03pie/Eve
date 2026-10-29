/** @file panel.cpp
 * @brief 真正执行已注册命令、隔离输出与恢复运行问题，不以演示文本代替行为。
 */
#include <cordis/kernel.hpp>
#include <cordis/ui/ela_shell.hpp>
#include <ElaTheme.h>
#include <ElaWindow.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QEventLoop>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QSplitter>
#include <QTreeWidget>
#include <QToolButton>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <algorithm>

using namespace cordis;
using namespace cordis::ui;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
void until(const std::function<bool()>& condition)
{
    QEventLoop loop; QTimer poll, timeout; timeout.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(1); timeout.start(6000); if (!condition()) loop.exec(); CHECK(condition());
}
template<class T> void wait(const Task<T>& task) { until([&] { return task.isReady(); }); CHECK(task.tryResult()->hasValue()); }
struct Scene {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ElaShell shell{scheduler};
    QPointer<ElaWindow> window;
    Scene()
    {
        shell.management()->attach(kernel).value(); shell.show().value();
        for (auto widget : QApplication::topLevelWidgets()) if (auto candidate = qobject_cast<ElaWindow*>(widget)) window = candidate;
        CHECK(window);
    }
    template<class T> T* find(const char* name) { auto result = window->findChild<T*>(name); CHECK(result); return result; }
    void capture(const QString& name)
    {
        window->resize(1000, 700); find<QTabWidget>("CordisWorkbenchPanel")->show();
        find<QSplitter>("ElaWorkbenchPanelSplitter")->setSizes({360, 260}); QTest::qWait(40);
        CHECK(window->grab().save(QCoreApplication::applicationDirPath() + "/ui-panel-" + name + ".png"));
    }
    void close() { auto core = kernel->close(); wait(core); CHECK(core.tryResult()->value().quiescent); wait(shell.close()); CHECK(!window); CHECK(scheduler->snapshot().value().callbackErrors == 0); scheduler->close().value(); }
};
void commands()
{
    Scene s;
    Promise<void> gate; int calls = 0;
    auto runtime = s.kernel->registerPlugin("panel.commands", [&](Context ctx) {
        return s.shell.commands()->add(ctx, {"panel.run", "真正执行", [&] { ++calls; return gate.task(); }, {}}).value().ready();
    }).value();
    auto instance = s.kernel->mount(runtime).value(); wait(instance.ready());
    s.find<QAction>("CordisOpenCommands")->trigger();
    auto filter = s.find<QLineEdit>("CordisCommandFilter"); auto list = s.find<QListView>("CordisCommandList");
    filter->setText("panel.run"); CHECK(list->model()->rowCount() == 1);
    QTest::keyClick(filter, Qt::Key_Return); CHECK(calls == 0);
    until([&] { return calls == 1; });
    CHECK(s.find<QLabel>("CordisCommandResult")->text().contains(QString::fromUtf8("运行中")));
    s.capture("commands");
    gate.resolve(); until([&] { return s.find<QLabel>("CordisCommandResult")->text().contains(QString::fromUtf8("执行完成")); });
    const auto mode = eTheme->getThemeMode();
    filter->setText("CordisToggleTheme"); CHECK(list->model()->rowCount() == 1);
    s.find<QPushButton>("CordisRunCommand")->click(); CHECK(eTheme->getThemeMode() != mode);
    filter->setText("nothing-matches"); CHECK(list->model()->rowCount() == 0);
    s.find<QPushButton>("CordisRunCommand")->click(); CHECK(calls == 1);
    wait(instance.stop()); filter->setText("panel.run"); CHECK(list->model()->rowCount() == 0);
    auto history = s.shell.output()->snapshot().value();
    CHECK(std::any_of(history.begin(), history.end(), [](const auto& entry) { return entry.text.find("panel.run") != std::string::npos || entry.text.find("执行完成") != std::string::npos; }));
    s.close();
}
void identity()
{
    Scene s; std::vector<InstanceId> called;
    auto runtime = s.kernel->registerPlugin("same.command", [&](Context ctx) {
        return s.shell.commands()->add(ctx, {"same.id", "同名命令", [&, id = ctx.identity().value().instance] { called.push_back(id); return completedTask(); }, {}}).value().ready();
    }).value();
    auto first = s.kernel->mount(runtime).value(), second = s.kernel->mount(runtime).value(); wait(first.ready()); wait(second.ready());
    auto filter = s.find<QLineEdit>("CordisCommandFilter"); auto list = s.find<QListView>("CordisCommandList");
    filter->setText("same.id"); CHECK(list->model()->rowCount() == 2);
    list->setCurrentIndex(list->model()->index(1, 0)); QTest::keyClick(filter, Qt::Key_Return);
    until([&] { return called.size() == 1; }); CHECK(called.back() == second.snapshot().value().instance);
    wait(first.stop()); CHECK(list->model()->rowCount() == 1);
    list->setCurrentIndex(list->model()->index(0, 0)); QTest::keyClick(filter, Qt::Key_Return);
    until([&] { return called.size() == 2; }); CHECK(called.back() == second.snapshot().value().instance);
    s.close();
}
void outputs()
{
    Scene s; std::vector<Context> contexts;
    auto runtime = s.kernel->registerPlugin("panel.output", [&](Context ctx) { contexts.push_back(ctx); return completedTask(); }).value();
    auto first = s.kernel->mount(runtime).value(), second = s.kernel->mount(runtime).value(); wait(first.ready()); wait(second.ready());
    auto output = s.shell.output();
    output->write(contexts[0], "同名通道", "first <b>literal</b>").value();
    output->write(contexts[1], "同名通道", "second").value();
    auto target = s.find<QComboBox>("CordisOutputTarget"); auto editor = s.find<QPlainTextEdit>("CordisWorkbenchOutput");
    CHECK(target->count() == 3 && target->currentIndex() == 0);
    target->setCurrentIndex(1); CHECK(editor->toPlainText().contains("first <b>literal</b>") && !editor->toPlainText().contains("second"));
    s.find<QAction>("CordisClearOutput")->trigger(); CHECK(editor->toPlainText().isEmpty());
    target->setCurrentIndex(2); CHECK(editor->toPlainText().contains("second"));
    s.find<QTabWidget>("CordisWorkbenchPanel")->setCurrentWidget(editor); s.capture("output");
    target->setCurrentIndex(1); CHECK(editor->toPlainText().isEmpty());
    target->setCurrentIndex(2); s.find<QToolButton>("CordisOutputFollow")->setChecked(false);
    auto cursor = editor->textCursor(); cursor.setPosition(editor->toPlainText().indexOf("second"));
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 6); editor->setTextCursor(cursor);
    output->write(contexts[1], "同名通道", "append without losing selection").value();
    CHECK(editor->textCursor().selectedText() == "second");
    output->write(contexts[0], "另一个目标", "inactive output").value();
    CHECK(editor->textCursor().selectedText() == "second");
    CHECK(!output->write(contexts[0], "bad\nname", "invalid").hasValue());
    CHECK(!output->write(contexts[0], "log", std::string(4097, 'x')).hasValue());
    CHECK(!output->write(contexts[0], "log", std::string("\xff", 1)).hasValue());
    bool rejected = false; std::thread worker([&] { rejected = !output->write(contexts[0], "log", "worker").hasValue(); }); worker.join(); CHECK(rejected);
    wait(first.stop()); CHECK(!output->write(contexts[0], "log", "stale").hasValue());
    output->write(contexts[1], "同名通道", "still live").value();
    CHECK(output->snapshot().value().back().text == "still live");
    std::string longMessage;
    for (int i = 0; i < 2000; ++i) longMessage += "中";
    s.shell.writeOutput(longMessage, OutputLevel::Error).value();
    const auto clipped = output->snapshot().value().back().text;
    CHECK(clipped.size() <= OutputService::textLimit && QString::fromStdString(clipped).toUtf8().toStdString() == clipped);
    s.close(); CHECK(!output->snapshot().value().empty());
}
void reentry()
{
    Scene s; std::optional<Context> context;
    auto runtime = s.kernel->registerPlugin("reentry", [&](Context ctx) { context = ctx; return completedTask(); }).value();
    auto instance = s.kernel->mount(runtime).value(); wait(instance.ready());
    auto output = s.shell.output(); output->write(*context, "log", "seed").value();
    s.find<QComboBox>("CordisOutputTarget")->setCurrentIndex(1);
    auto editor = s.find<QPlainTextEdit>("CordisWorkbenchOutput");
    bool entered = false; std::uint64_t nested = 0;
    auto connection = QObject::connect(editor, &QPlainTextEdit::textChanged, editor, [&] {
        if (entered) return; entered = true; nested = output->write(*context, "log", "nested").value();
    });
    auto outer = output->write(*context, "log", "outer").value(); QObject::disconnect(connection);
    CHECK(nested == outer + 1 && editor->toPlainText().contains("nested"));
    std::optional<Task<void>> closed; bool retained = false;
    connection = QObject::connect(editor, &QPlainTextEdit::textChanged, editor, [&] {
        if (closed) return; closed.emplace(s.shell.close());
        QEventLoop loop; QTimer::singleShot(20, &loop, &QEventLoop::quit); loop.exec();
        retained = s.window && !closed->isReady();
    });
    CHECK(output->write(*context, "log", "closing").hasValue());
    CHECK(closed && retained); wait(*closed); CHECK(!s.window);
    wait(s.kernel->close()); s.scheduler->close().value();
}
void limits()
{
    Scene s; std::optional<Context> context;
    auto runtime = s.kernel->registerPlugin("limits", [&](Context ctx) { context = ctx; return completedTask(); }).value();
    auto instance = s.kernel->mount(runtime).value(); wait(instance.ready());
    auto output = s.shell.output();
    for (int i = 0; i < 1010; ++i) output->write(*context, "log", std::to_string(i)).value();
    CHECK(output->snapshot().value().size() == OutputService::historyLimit);
    for (int i = 0; i < 70; ++i) output->write(*context, std::to_string(i), "channel").value();
    CHECK(s.find<QComboBox>("CordisOutputTarget")->count() == static_cast<int>(OutputService::channelLimit));
    CHECK(output->snapshot().value().back().text == "channel"); s.close();
}
/** @brief 清空时的同步文本信号可切换目标，另一目标的历史不得被连带删除。 */
void clearReentry()
{
    Scene s; std::optional<Context> context;
    auto runtime = s.kernel->registerPlugin("clear", [&](Context ctx) { context = ctx; return completedTask(); }).value();
    auto instance = s.kernel->mount(runtime).value(); wait(instance.ready());
    auto output = s.shell.output();
    output->write(*context, "a", "clear-a").value(); output->write(*context, "b", "retain-b").value();
    auto targets = s.find<QComboBox>("CordisOutputTarget"); targets->setCurrentIndex(1);
    auto editor = s.find<QPlainTextEdit>("CordisWorkbenchOutput");
    bool entered = false;
    auto connection = QObject::connect(editor, &QPlainTextEdit::textChanged, editor, [&] {
        if (entered) return;
        entered = true; targets->setCurrentIndex(2);
    });
    s.find<QAction>("CordisClearOutput")->trigger(); QObject::disconnect(connection);
    CHECK(entered);
    const auto history = output->snapshot().value();
    CHECK(std::none_of(history.begin(), history.end(), [](const auto& entry) { return entry.text == "clear-a"; }));
    CHECK(std::any_of(history.begin(), history.end(), [](const auto& entry) { return entry.text == "retain-b"; }));
    CHECK(editor->toPlainText().contains("retain-b"));
    s.shell.writeOutput("clear-platform").value(); targets->setCurrentIndex(0); entered = false;
    connection = QObject::connect(editor, &QPlainTextEdit::textChanged, editor, [&] {
        if (entered) return;
        entered = true; targets->setCurrentIndex(2);
    });
    auto filter = s.find<QLineEdit>("CordisCommandFilter"); filter->setText("CordisClearOutput");
    QTest::keyClick(filter, Qt::Key_Return); QObject::disconnect(connection);
    CHECK(entered);
    const auto commandHistory = output->snapshot().value();
    CHECK(std::none_of(commandHistory.begin(), commandHistory.end(), [](const auto& entry) { return entry.text == "clear-platform"; }));
    CHECK(std::any_of(commandHistory.begin(), commandHistory.end(), [](const auto& entry) { return entry.text == "retain-b"; }));
    s.close();
}
/** @brief 复制当前目标必须覆盖全部保留记录，不能只复制旧控件的 500 行窗口。 */
void outputHistory()
{
    Scene s; s.find<QAction>("CordisClearOutput")->trigger();
    for (int i = 0; i < 600; ++i) s.shell.writeOutput("retained-line-" + std::to_string(i)).value();
    s.find<QAction>("CordisCopyOutput")->trigger();
    const auto copied = QApplication::clipboard()->text();
    CHECK(copied.contains("retained-line-0\n") && copied.contains("retained-line-599"));
    std::string multiline;
    for (int i = 0; i < 3000; ++i) multiline += '\n';
    for (int i = 0; i < 3; ++i) s.shell.writeOutput(multiline).value();
    s.find<QAction>("CordisCopyOutput")->trigger();
    CHECK(QApplication::clipboard()->text().contains("retained-line-0\n"));
    CHECK(s.find<QPlainTextEdit>("CordisWorkbenchOutput")->document()->blockCount() <= 5000);
    s.close();
}
/** @brief 可变宿主配置源用于验证同一路径错误更新，不执行伪造的 Core 操作。 */
struct ProblemSource final : IConfigurationManagement {
    ConfigurationManagementSnapshot value;
    Result<ConfigurationManagementSnapshot> snapshot() const override { return Result<ConfigurationManagementSnapshot>::success(value); }
    Result<Task<ConfigurationActionReport>> setEnabled(const std::string&, bool, std::uint64_t) override
    { return Result<Task<ConfigurationActionReport>>::failure(makeError(ErrorCode::InvalidArgument, "read-only fixture")); }
    Result<Task<ConfigurationActionReport>> reconcile(const std::string&, std::uint64_t) override
    { return Result<Task<ConfigurationActionReport>>::failure(makeError(ErrorCode::InvalidArgument, "read-only fixture")); }
};
/** @brief 同一问题保持选中时，列表和原因详情必须来自同一个新快照。 */
void problemUpdates()
{
    Scene s;
    auto runtime = s.kernel->registerPlugin("configured", [](Context) { return completedTask(); }).value();
    auto source = std::make_shared<ProblemSource>(); source->value.open = true;
    ManagedConfigurationEntry entry; entry.path = "path"; entry.plugin = "configured";
    entry.error = makeError(ErrorCode::ConfigConflict, "first problem"); source->value.entries.push_back(entry);
    s.shell.management()->attachConfiguration(source, {runtime.id()}).value();
    auto tree = s.find<QTreeWidget>("CordisProblemsTree");
    until([&] { return tree->topLevelItemCount() > 0; });
    tree->setCurrentItem(tree->topLevelItem(0)->child(0));
    auto detail = s.find<QPlainTextEdit>("CordisProblemDetails"); CHECK(detail->toPlainText().contains("first problem"));
    source->value.entries[0].error = makeError(ErrorCode::ConfigConflict, "updated problem");
    until([&] { return tree->topLevelItem(0)->child(0)->text(1).contains("updated problem"); });
    CHECK(tree->currentItem() == tree->topLevelItem(0)->child(0));
    CHECK(detail->toPlainText().contains("updated problem") && !detail->toPlainText().contains("first problem"));
    s.close();
}
void problems()
{
    Scene s; int runs = 0;
    auto runtime = s.kernel->registerPlugin("broken.provider", [&](Context) {
        return ++runs == 1 ? failedTask(makeError(ErrorCode::ActivationFailed, "real activation failure")) : completedTask();
    }).value();
    auto instance = s.kernel->mount(runtime).value(); until([&] { return instance.ready().isReady(); });
    CHECK(!instance.ready().tryResult()->hasValue());
    auto tree = s.find<QTreeWidget>("CordisProblemsTree");
    until([&] { return tree->topLevelItemCount() > 0; });
    CHECK(tree->topLevelItem(0)->childCount() == 1);
    auto item = tree->topLevelItem(0)->child(0); tree->setCurrentItem(item);
    CHECK(s.find<QPlainTextEdit>("CordisProblemDetails")->toPlainText().contains("real activation failure"));
    s.find<QTabWidget>("CordisWorkbenchPanel")->setCurrentWidget(s.find<QWidget>("CordisProblemsPanel")); s.capture("problems");
    auto retry = instance.retry().value(); wait(retry.target);
    until([&] { return tree->topLevelItemCount() == 0; });
    CHECK(s.find<QLabel>("CordisProblemsStatus")->text().contains(QString::fromUtf8("没有问题")));
    s.close();
}
int main(int argc, char** argv)
{
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    try {
#ifdef Q_OS_WIN
        if (QApplication::platformName() == "offscreen")
            CHECK(QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc") >= 0);
#endif
        CHECK(argc == 2); const std::string name = argv[1];
        if (name == "commands") commands(); else if (name == "identity") identity(); else if (name == "outputs") outputs(); else if (name == "limits") limits(); else if (name == "problems") problems(); else if (name == "reentry") reentry(); else if (name == "clear_reentry") clearReentry(); else if (name == "output_history") outputHistory(); else if (name == "problem_updates") problemUpdates(); else return 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
