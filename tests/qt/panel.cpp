/** @file panel.cpp
 * @brief 真正执行已注册命令、隔离输出与恢复运行问题，不以演示文本代替行为。
 */
#include <eve/kernel.hpp>
#include <eve/ui/ela_shell.hpp>
#include <ElaTheme.h>
#include <ElaWindow.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QMenu>
#include <QMenuBar>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QSplitter>
#include <QTreeWidget>
#include <QToolButton>
#include <QToolTip>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <set>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

using namespace eve;
using namespace eve::ui;
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
    ElaShell shell;
    QPointer<ElaWindow> window;
    explicit Scene(const QString& preferences = {}) : shell(scheduler, preferences.toStdString())
    {
        shell.management()->attach(kernel).value(); shell.show().value();
        for (auto widget : QApplication::topLevelWidgets()) if (auto candidate = qobject_cast<ElaWindow*>(widget)) window = candidate;
        CHECK(window);
    }
    template<class T> T* find(const char* name) { auto result = window->findChild<T*>(name); CHECK(result); return result; }
    void capture(const QString& name)
    {
        window->resize(1000, 700); find<QTabWidget>("EveWorkbenchPanel")->show();
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
    s.find<QAction>("EveOpenCommands")->trigger();
    auto filter = s.find<QLineEdit>("EveCommandFilter"); auto list = s.find<QListView>("EveCommandList");
    filter->setText("panel.run"); CHECK(list->model()->rowCount() == 1);
    QTest::keyClick(filter, Qt::Key_Return); CHECK(calls == 0);
    until([&] { return calls == 1; });
    CHECK(s.find<QLabel>("EveCommandResult")->text().contains(QString::fromUtf8("Running:")));
    s.capture("commands");
    gate.resolve(); until([&] { return s.find<QLabel>("EveCommandResult")->text().contains(QString::fromUtf8("Completed:")); });
    const auto mode = eTheme->getThemeMode();
    filter->setText("EveToggleTheme"); CHECK(list->model()->rowCount() == 1);
    s.find<QPushButton>("EveRunCommand")->click(); CHECK(eTheme->getThemeMode() != mode);
    filter->setText("nothing-matches"); CHECK(list->model()->rowCount() == 0);
    s.find<QPushButton>("EveRunCommand")->click(); CHECK(calls == 1);
    wait(instance.stop()); filter->setText("panel.run"); CHECK(list->model()->rowCount() == 0);
    auto history = s.shell.output()->snapshot().value();
    CHECK(std::any_of(history.begin(), history.end(), [](const auto& entry) { return entry.text.find("panel.run") != std::string::npos || entry.text.find("Completed:") != std::string::npos; }));
    s.close();
}
void identity()
{
    Scene s; std::vector<InstanceId> called;
    auto runtime = s.kernel->registerPlugin("same.command", [&](Context ctx) {
        return s.shell.commands()->add(ctx, {"same.id", "同名命令", [&, id = ctx.identity().value().instance] { called.push_back(id); return completedTask(); }, {}}).value().ready();
    }).value();
    auto first = s.kernel->mount(runtime).value(), second = s.kernel->mount(runtime).value(); wait(first.ready()); wait(second.ready());
    auto filter = s.find<QLineEdit>("EveCommandFilter"); auto list = s.find<QListView>("EveCommandList");
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
    auto target = s.find<QComboBox>("EveOutputTarget"); auto editor = s.find<QPlainTextEdit>("EveWorkbenchOutput");
    CHECK(target->count()==3 && target->currentData().toString().isEmpty());
    const int plugin=target->findData("plugin/panel.output"); CHECK(plugin>=0);
    target->setCurrentIndex(plugin);
    CHECK(editor->toPlainText().contains("first <b>literal</b>") && editor->toPlainText().contains("second"));
    auto scope=s.find<QComboBox>("EveOutputInstance");
    scope->setCurrentIndex(scope->findData(QString::number(first.snapshot().value().instance.value())));
    CHECK(editor->toPlainText().contains("first <b>literal</b>") && !editor->toPlainText().contains("second"));
    scope->setCurrentIndex(scope->findData(QString::number(second.snapshot().value().instance.value())));
    CHECK(editor->toPlainText().contains("second") && !editor->toPlainText().contains("literal"));
    s.find<QTabWidget>("EveWorkbenchPanel")->setCurrentWidget(editor); s.capture("output");
    s.find<QToolButton>("EveOutputFollow")->setChecked(false);
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
    s.find<QComboBox>("EveOutputTarget")->setCurrentIndex(0);
    auto editor = s.find<QPlainTextEdit>("EveWorkbenchOutput");
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
    CHECK(s.find<QComboBox>("EveOutputTarget")->findData("plugin/limits") >= 0);
    std::set<std::string> channels;
    const auto retained=output->snapshot().value();
    for (const auto& entry:retained) channels.insert(entry.channel);
    CHECK(channels.size() <= OutputService::channelLimit);
    CHECK(output->snapshot().value().back().text == "channel"); s.close();
}
/** @brief 清空时的同步文本信号可切换目标，另一目标的历史不得被连带删除。 */
void clearReentry()
{
    Scene s; std::optional<Context> context;
    auto runtime = s.kernel->registerPlugin("clear", [&](Context ctx) { context = ctx; return completedTask(); }).value();
    auto instance = s.kernel->mount(runtime).value(); wait(instance.ready());
    auto output = s.shell.output();
    output->write(*context,"a","clear-a").value();
    s.shell.writeOutput("retain-platform").value();
    auto targets=s.find<QComboBox>("EveOutputTarget");
    targets->setCurrentIndex(targets->findData("plugin/clear"));
    auto editor=s.find<QPlainTextEdit>("EveWorkbenchOutput");
    bool entered=false;
    const auto connection=QObject::connect(editor,&QPlainTextEdit::textChanged,editor,[&]{
        if(entered) return;
        entered=true; targets->setCurrentIndex(targets->findData("platform"));
    });
    s.find<QAction>("EveClearOutput")->trigger();QObject::disconnect(connection);
    CHECK(entered && editor->toPlainText().contains("retain-platform"));
    targets->setCurrentIndex(targets->findData("plugin/clear"));
    CHECK(!editor->toPlainText().contains("clear-a"));
    const auto retained=output->snapshot().value();
    CHECK(std::any_of(retained.begin(),retained.end(),[](const auto& entry){return entry.text=="clear-a";}));
    s.close();
}
/** @brief 复制当前目标必须覆盖全部保留记录，不能只复制旧控件的 500 行窗口。 */
void outputHistory()
{
    Scene s; s.find<QAction>("EveClearOutput")->trigger();
    for (int i = 0; i < 600; ++i) s.shell.writeOutput("retained-line-" + std::to_string(i)).value();
    s.find<QAction>("EveCopyOutput")->trigger();
    const auto copied = QApplication::clipboard()->text();
    CHECK(copied.contains("retained-line-0\n") && copied.contains("retained-line-599"));
    auto* editor = s.find<QPlainTextEdit>("EveWorkbenchOutput");
    auto* panel = s.find<QTabWidget>("EveWorkbenchPanel");
    panel->setCurrentWidget(editor);
    panel->show();
    until([&] { return editor->blockCount() == 600; });
    s.shell.writeOutput(std::string(2199, '\n') + "middle").value();
    s.shell.writeOutput(std::string(2198, '\n') + "tail").value();
    until([&] { return editor->blockCount() == 4999; });
    CHECK(editor->maximumBlockCount() == 5000);

    s.find<QToolButton>("EveOutputFollow")->setChecked(false);
    const auto selection = editor->document()->find("retained-line-300");
    CHECK(!selection.isNull());
    editor->setTextCursor(selection);
    auto* scroll = editor->verticalScrollBar();
    scroll->setValue(290);
    CHECK(scroll->value() == 290 && scroll->maximum() > 290);
    s.shell.writeOutput("at-limit").value();
    until([&] { return editor->blockCount() == 5000; });
    CHECK(editor->toPlainText().contains("retained-line-0\n"));
    CHECK(editor->textCursor().selectedText() == "retained-line-300" && scroll->value() == 290);

    s.shell.writeOutput("over-limit").value();
    until([&] { return editor->toPlainText().endsWith("over-limit"); });
    CHECK(editor->blockCount() == 5000);
    CHECK(!editor->toPlainText().contains("retained-line-0\n"));
    CHECK(editor->toPlainText().contains("retained-line-1\n"));
    CHECK(editor->textCursor().selectedText() == "retained-line-300" && scroll->value() == 290);

    // 头部恰好截进一条多行记录时，同一条记录中的剩余文字仍应保持选中。
    const auto multilineSelection = editor->document()->find("middle");
    CHECK(!multilineSelection.isNull());
    editor->setTextCursor(multilineSelection);
    s.shell.writeOutput(std::string(600, '\n') + "partial-record").value();
    until([&] { return editor->toPlainText().endsWith("partial-record"); });
    CHECK(editor->blockCount() == 5000 && editor->textCursor().selectedText() == "middle");
    s.find<QAction>("EveCopyOutput")->trigger();
    CHECK(QApplication::clipboard()->text().contains("retained-line-0\n"));
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
    auto tree = s.find<QTreeWidget>("EveProblemsTree");
    until([&] { return tree->topLevelItemCount() > 0; });
    tree->setCurrentItem(tree->topLevelItem(0)->child(0));
    auto detail = s.find<QPlainTextEdit>("EveProblemDetails"); CHECK(detail->toPlainText().contains("first problem"));
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
    auto tree = s.find<QTreeWidget>("EveProblemsTree");
    until([&] { return tree->topLevelItemCount() > 0; });
    CHECK(tree->topLevelItem(0)->childCount() == 1);
    auto item = tree->topLevelItem(0)->child(0); tree->setCurrentItem(item);
    CHECK(s.find<QPlainTextEdit>("EveProblemDetails")->toPlainText().contains("real activation failure"));
    s.find<QTabWidget>("EveWorkbenchPanel")->setCurrentWidget(s.find<QWidget>("EveProblemsPanel")); s.capture("problems");
    auto retry = instance.retry().value(); wait(retry.target);
    until([&] { return tree->topLevelItemCount() == 0; });
    CHECK(s.find<QLabel>("EveProblemsStatus")->text().contains(QString::fromUtf8("No current problems")));
    s.close();
}
QAction* outputMenuAction(Scene& scene,const QString& name)
{
    auto menu=scene.find<QToolButton>("EveOutputMore")->menu();
    CHECK(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));
    for(auto a:menu->actions()) if(a->text()==name) return a;
    throw std::runtime_error("missing output action");
}
/** @brief 两次真实归档会话，按原生控件筛选、翻页并导出完整范围。 */
void historicalFilters()
{
    QTemporaryDir workspace; CHECK(workspace.isValid());
    QString firstId, secondId, firstEpoch;
    {
        Scene s; s.shell.startOutputArchive(workspace.path().toStdString(),"test-a").value();
        std::vector<Context> contexts;
        auto plugin=s.kernel->registerPlugin("archive.example",[&](Context c){contexts.push_back(c);return completedTask();}).value();
        auto first=s.kernel->mount(plugin).value();wait(first.ready());
        auto second=s.kernel->mount(plugin).value();wait(second.ready());
        firstId=QString::number(contexts[0].identity().value().instance.value());
        firstEpoch=QString::number(contexts[0].identity().value().epoch);
        secondId=QString::number(contexts[1].identity().value().instance.value());
        s.shell.output()->write(contexts[0],"same","early instance only").value();
        for (int i=0;i<210;++i) s.shell.output()->write(contexts[1],"same","recent instance "+std::to_string(i)).value();
        for(int i=1;i<=1311;++i) s.shell.writeOutput("sample-row-"+std::to_string(i),OutputLevel::Warning).value();
        s.shell.writeOutput("other-row fixture-only-token",OutputLevel::Error).value();
        s.close();
    }
    QDirIterator segments(workspace.path(), {"events-*.jsonl"}, QDir::Files, QDirIterator::Subdirectories);
    CHECK(segments.hasNext());
    const auto segment=segments.next();
    const auto digest=[&] {
        QFile file(segment);CHECK(file.open(QIODevice::ReadOnly));
        return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);
    };
    const auto before=digest();
    Scene s;
    s.shell.startOutputArchive(workspace.path().toStdString(),"test-b").value();
    s.find<QAction>("EvePreviousOutput")->trigger();
    CHECK(s.find<QComboBox>("EveOutputRun")->count()==2);
    auto editor=s.find<QPlainTextEdit>("EveWorkbenchOutput");
    auto search=s.find<QLineEdit>("EveOutputSearch");
    auto level=s.find<QComboBox>("EveOutputLevel");
    auto sources=s.find<QComboBox>("EveOutputTarget");
    sources->setCurrentIndex(sources->findData("plugin/archive.example"));
    auto instances=s.find<QComboBox>("EveOutputInstance");
    CHECK(!editor->toPlainText().contains("early instance only"));
    CHECK(instances->findData(firstId)>=0 && instances->findData(secondId)>=0);
    instances->setCurrentIndex(instances->findData(firstId+"/"+firstEpoch));
    CHECK(editor->toPlainText().contains("early instance only") && !editor->toPlainText().contains("recent instance"));
    search->setText("deliberately empty");
    CHECK(editor->toPlainText().isEmpty() && instances->findData(secondId)>=0);
    search->clear();
    instances->setCurrentIndex(instances->findData(secondId));
    CHECK(editor->toPlainText().contains("recent instance 209") && !editor->toPlainText().contains("early instance only"));
    sources->setCurrentIndex(0);
    const auto warningIndex=level->findData(static_cast<int>(OutputLevel::Warning));
    const auto errorIndex=level->findData(static_cast<int>(OutputLevel::Error));
    CHECK(level->count()==5 && warningIndex>=0 && errorIndex>=0);
    search->setText("sample-row-");level->setCurrentIndex(warningIndex);
    CHECK(editor->toPlainText().count("sample-row-")==200);
    outputMenuAction(s,QString::fromUtf8("Load Previous Page"))->trigger();
    CHECK(editor->toPlainText().count("sample-row-")==400);
    outputMenuAction(s,QString::fromUtf8("Load Previous Page"))->trigger();
    CHECK(editor->toPlainText().count("sample-row-")==600);
    for(int page=0;page<4;++page) outputMenuAction(s,QString::fromUtf8("Load Previous Page"))->trigger();
    CHECK(editor->toPlainText().count("sample-row-")==1000);
    CHECK(editor->toPlainText().contains("sample-row-1\n") && !editor->toPlainText().contains("sample-row-1311"));
    outputMenuAction(s,QString::fromUtf8("Go to Latest Page"))->trigger();
    CHECK(editor->toPlainText().count("sample-row-")==200 && editor->toPlainText().contains("sample-row-1311"));
    CHECK(!editor->toPlainText().contains("other-row"));
    level->setCurrentIndex(errorIndex);CHECK(editor->toPlainText().isEmpty());
    level->setCurrentIndex(warningIndex);
    const auto path=workspace.filePath("filtered.txt");
    QTimer::singleShot(10,[path]{
        for(auto top:QApplication::topLevelWidgets()) if(auto dlg=qobject_cast<QFileDialog*>(top)) {
            dlg->selectFile(path);
            QMetaObject::invokeMethod(dlg,"accept",Qt::QueuedConnection);return;
        }
    });
    outputMenuAction(s,QString::fromUtf8("Export Filtered Records…"))->trigger();
    QFile exported(path);CHECK(exported.open(QIODevice::ReadOnly));
    const auto bytes=exported.readAll();CHECK(!bytes.contains("fixture-only-token"));
    auto rows=bytes.split('\n');rows.removeLast();
    CHECK(rows.size()==1311);
    for(int i=0;i<rows.size();++i) CHECK(rows[i].endsWith("sample-row-"+QByteArray::number(i+1)));
    const auto archivePath = QFileInfo(segment).absoluteDir().filePath("unwanted-export.txt");
    QTimer::singleShot(10, [archivePath] {
        for (auto* top : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QFileDialog*>(top)) {
                dialog->selectFile(archivePath);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
                return;
            }
    });
    outputMenuAction(s, QString::fromUtf8("Export Filtered Records…"))->trigger();
    CHECK(!QFileInfo::exists(archivePath));
    s.find<QAction>("EveClearOutput")->trigger();CHECK(editor->toPlainText().isEmpty());
    s.find<QAction>("EvePreviousOutput")->trigger();CHECK(editor->toPlainText().contains("sample-row-1311"));
    CHECK(digest()==before);
    s.close();
}
void responsive()
{
    Scene s;auto panel=s.find<QTabWidget>("EveWorkbenchPanel");auto editor=s.find<QPlainTextEdit>("EveWorkbenchOutput");
    auto menu = s.find<QMenuBar>("EveMenuBar");
    auto overflow = s.find<QToolButton>("EveMenuOverflow");
    auto quickOpen = s.find<QToolButton>("EveCommandCenterButton");
    s.window->resize(560, 700);
    until([&] { return overflow->isVisible() && !menu->isVisible(); });
    CHECK(quickOpen->isVisible());
    CHECK(overflow->mapTo(s.window, overflow->rect().topRight()).x() < quickOpen->mapTo(s.window, QPoint()).x());
    CHECK(overflow->menu());
    for (auto* header : menu->actions())
        CHECK(overflow->menu()->actions().contains(header));
    QTest::mouseClick(quickOpen, Qt::LeftButton);
    auto quickPanel = s.find<QWidget>("EveCommandPanel");
    CHECK(quickPanel->isVisible() && s.window->rect().contains(quickPanel->geometry()));
    QTest::keyClick(s.find<QLineEdit>("EveCommandFilter"), Qt::Key_Escape);
    s.window->resize(1200, 700);
    until([&] { return menu->isVisible() && !overflow->isVisible(); });
    s.find<QAction>("EveOpenOutput")->trigger();
    const auto seq=s.shell.writeOutput("buffer prepared\n[123] continuation").value();
    s.shell.showOutputEvent(seq,nullptr).value();
    const auto selected=editor->textCursor().selectedText();
    CHECK(selected.contains("buffer prepared") && selected.endsWith("continuation"));
    for(const auto mode:{ElaThemeType::Light,ElaThemeType::Dark}) {
      eTheme->setThemeMode(mode);
      for(const int width:{759,760,1079,1080}) {
        s.window->resize(width+200,800);QCoreApplication::processEvents();
        s.window->resize(s.window->width()+width-editor->width(),800);QCoreApplication::processEvents();
        CHECK(editor->width()==width);
        CHECK(s.find<QComboBox>("EveOutputRun")->isVisible()==(width>=760));
        CHECK(s.find<QLineEdit>("EveOutputSearch")->isVisible()==(width>=1080));
        CHECK(s.find<QComboBox>("EveOutputTarget")->isVisible() && s.find<QToolButton>("EveOutputMore")->isVisible());
        CHECK(editor->textCursor().selectedText()==selected);
        const auto height=panel->height();panel->setCurrentIndex(1);QCoreApplication::processEvents();CHECK(panel->height()==height);
        panel->setCurrentIndex(0);QCoreApplication::processEvents();CHECK(panel->height()==height);
        for(const char* id:{"EveHidePanel","EveCopyPanelOutput","EveClearPanelOutput"}) {
            auto button=s.find<QToolButton>(id);
            if(width<1080 && button->objectName()!="EveHidePanel") {CHECK(!button->isVisible());continue;}
            auto center=button->mapTo(panel,button->rect().center());
            if(!panel->rect().contains(center) || panel->childAt(center)!=button) {
                const auto parent=button->parentWidget()->geometry();
                std::cerr << id << " at width " << width << " center "<<center.x()<<","<<center.y()
                          <<" parent "<<parent.x()<<","<<parent.y()<<" "<<parent.width()<<"x"<<parent.height()
                          <<" hit "<<(panel->childAt(center)?panel->childAt(center)->objectName().toStdString():"none")<<'\n';
            }
            CHECK(panel->rect().contains(center) && panel->childAt(center)==button);
        }
        const auto folder=qEnvironmentVariable("EVE_UI_EVIDENCE_DIR");
        if(!folder.isEmpty()) {
            const auto filename=QString("output-%1-%2-%3.png").arg(mode==ElaThemeType::Dark?"dark":"light").arg(width).arg(s.window->devicePixelRatioF());
            CHECK(s.window->grab().save(QDir(folder).filePath(filename)));
        }
      }
    }
    outputMenuAction(s,QString::fromUtf8("Copy Record"))->trigger();
    CHECK(QApplication::clipboard()->text().contains("buffer prepared\n[123] continuation"));
    s.find<QAction>("EveOpenSettings")->trigger();
    auto origin=s.find<QLineEdit>("EveSettingsSearch");
    origin->setFocus();origin->setText("developer");origin->setSelection(7,-4);
    s.shell.showOutputEvent(seq,origin).value();
    CHECK(editor->textCursor().selectedText()==selected);
    QTest::keyClick(s.find<QComboBox>("EveOutputTarget"),Qt::Key_Escape);
    until([&]{return !panel->isVisible() && QApplication::focusWidget()==origin;});
    CHECK(origin->selectedText()=="elop" && origin->cursorPosition()==3);
    s.shell.showOutputEvent(seq+10000,origin).value();
    CHECK(s.find<QLabel>("EveOutputFeedback")->text().contains(QString::fromUtf8("could not be found")));
    s.close();
}
void throughput()
{
    Scene s;s.window->resize(1360,800);s.find<QAction>("EveOpenOutput")->trigger();
    auto editor=s.find<QPlainTextEdit>("EveWorkbenchOutput");
    auto search=s.find<QLineEdit>("EveOutputSearch");
    auto follow=s.find<QToolButton>("EveOutputFollow");
    CHECK(search->isVisible());follow->setChecked(false);
    std::optional<Context> a,b;
    const auto first=s.kernel->registerPlugin("traffic.A",[&](Context ctx){a=ctx;return completedTask();}).value();
    const auto second=s.kernel->registerPlugin("traffic.B",[&](Context ctx){b=ctx;return completedTask();}).value();
    auto i=s.kernel->mount(first).value(),j=s.kernel->mount(second).value();wait(i.ready());wait(j.ready());
    auto output=s.shell.output();int updates=0;
    QObject::connect(editor,&QPlainTextEdit::textChanged,editor,[&]{++updates;});
    QElapsedTimer total;total.start();std::vector<qint64> latency;
    auto source=s.find<QComboBox>("EveOutputTarget");
    for(int batch=0;batch<100;++batch) {
        for(int n=0;n<1000;++n) output->write(n%2?*a:*b,"worker","traffic-"+std::to_string(batch*1000+n)).value();
        QCoreApplication::processEvents();
        source->setCurrentIndex(source->findData(batch%2?"plugin/traffic.A":"plugin/traffic.B"));
        search->setFocus();search->setText("traffi");
        QElapsedTimer response;response.start();QTest::keyClick(search,Qt::Key_C);
        editor->viewport()->repaint();QCoreApplication::processEvents();
        latency.push_back(response.elapsed());
        CHECK(editor->toPlainText().contains("traffic-"));
    }
    std::sort(latency.begin(),latency.end());
    std::cout<<"records=100000 elapsed_ms="<<total.elapsed()<<" display_changes="<<updates<<" input_to_repaint_p95_ms="<<latency[94]
             <<" retained="<<output->snapshot().value().size()<<" archive_page_size=200"<<std::endl;
    CHECK(updates<6000 && output->snapshot().value().size()==OutputService::historyLimit);
    s.close();
}
void crossWindow()
{
    Scene s;
    std::optional<Context> context;
    auto plugin=s.kernel->registerPlugin("managed.source",[&](Context ctx){context=ctx;return completedTask();}).value();
    auto instance=s.kernel->mount(plugin).value();wait(instance.ready());
    s.shell.output()->write(*context,"log","managed instance log").value();
    s.shell.management()->refresh().value();
    s.shell.showManagement().value();
    auto table=s.find<QTableView>("EveInstanceTable");
    auto origin=s.find<QPushButton>("EveManagementOpenLog");
    auto dialog=origin->window();
    table->selectRow(0);origin->setFocus();
    until([&]{return dialog->isActiveWindow() && origin->isEnabled();});
    std::cout << "management active, log action enabled\n";
    QTest::keyClick(origin,Qt::Key_Space);
    auto targets=s.find<QComboBox>("EveOutputTarget");
    auto output=s.find<QPlainTextEdit>("EveWorkbenchOutput");
    until([&]{return s.window->isActiveWindow() && targets->hasFocus() && output->isVisible();});
    std::cout << "output active, source selector focused\n";
    CHECK(output->toPlainText().contains("managed instance log"));
    CHECK(!dialog->isVisible());
    const auto local=output->viewport()->mapTo(s.window,output->viewport()->rect().center());
    auto at=s.window->childAt(local);
    CHECK(at && (at==output || output->isAncestorOf(at)));
#ifdef Q_OS_WIN
    if (QApplication::platformName()=="windows") {
        auto hwnd=reinterpret_cast<HWND>(s.window->winId());
        const auto scale=s.window->devicePixelRatioF();
        POINT clientPoint{qRound(local.x()*scale),qRound(local.y()*scale)};
        POINT screenPoint=clientPoint;
        CHECK(ClientToScreen(hwnd,&screenPoint));
        CHECK(!IsWindowVisible(reinterpret_cast<HWND>(dialog->winId())) && IsWindowVisible(hwnd));
        auto hit=ChildWindowFromPointEx(hwnd,clientPoint,CWP_SKIPINVISIBLE|CWP_SKIPTRANSPARENT);
        CHECK(hit && GetAncestor(hit,GA_ROOT)==hwnd);
        CHECK(SendMessageW(hwnd,WM_NCHITTEST,0,MAKELPARAM(screenPoint.x,screenPoint.y))==HTCLIENT);
        // Global Z order includes unrelated foreground apps when launched by a
        // background runner. Check the desktop hit only when our window owns it.
        if (GetForegroundWindow()==hwnd)
            CHECK(GetAncestor(WindowFromPoint(screenPoint),GA_ROOT)==hwnd);
    }
#endif
    std::cout << "output viewport hit\n";
    QTest::keyClick(targets,Qt::Key_Escape);
    until([&]{return dialog->isVisible() && dialog->isActiveWindow() && origin->hasFocus();});
#ifdef Q_OS_WIN
    if (QApplication::platformName()=="windows")
        CHECK(IsWindowVisible(reinterpret_cast<HWND>(dialog->winId())));
#endif
    CHECK(table->currentIndex().row()==0);
    auto notifications=s.find<QPushButton>("EveManagementOpenNotifications");
    s.shell.notifications()->post(*context,{NotificationLevel::Warning,"managed notification event",{}}).value();
    auto list=s.find<QListView>("EveNotificationList");
    auto center=s.find<QWidget>("EveNotificationCenter");
    auto panel=s.find<QTabWidget>("EveWorkbenchPanel");
    const auto openCenter=[&]{
        notifications->setFocus();
        QTest::keyClick(notifications,Qt::Key_Space);
        until([&]{return !dialog->isVisible() && s.window->isActiveWindow() &&
                          center->isVisible() && list->hasFocus();});
        CHECK(panel->isHidden());
        auto point=list->viewport()->mapTo(s.window,list->viewport()->rect().center());
        auto hit=s.window->childAt(point);CHECK(hit && center->isAncestorOf(hit));
#ifdef Q_OS_WIN
        if (QApplication::platformName()=="windows")
            CHECK(!IsWindowVisible(reinterpret_cast<HWND>(dialog->winId())));
#endif
    };
    openCenter();
    const auto folder=qEnvironmentVariable("EVE_UI_EVIDENCE_DIR");
    if (!folder.isEmpty()) {
        const auto name=QString("management-notification-center-%1.png").arg(s.window->devicePixelRatioF());
        CHECK(s.window->grab().save(QDir(folder).filePath(name)));
    }
    QTest::keyClick(list,Qt::Key_Escape);
    until([&]{return dialog->isActiveWindow() && notifications->hasFocus() && !center->isVisible();});
    std::cout << "notification center visible, Esc restored management button\n";
    openCenter();
    int dialogShows=0;
    struct ShowCounter : QObject {
        int& shows;
        explicit ShowCounter(int& count):shows(count){}
        bool eventFilter(QObject*,QEvent* e) override {
            if(e->type()==QEvent::Show) ++shows;
            return false;
        }
    } counter(dialogShows);
    dialog->installEventFilter(&counter);
    QTest::keyClick(s.find<QPushButton>("EveNotificationOpenLog"),Qt::Key_Space);
    until([&]{return !center->isVisible() && !dialog->isVisible() && targets->hasFocus() &&
                      output->textCursor().selectedText().contains("managed notification event");});
    CHECK(dialogShows==0);
    QTest::keyClick(targets,Qt::Key_Escape);
    until([&]{return dialog->isVisible() && dialog->isActiveWindow() && notifications->hasFocus();});
    CHECK(dialogShows==1 && table->currentIndex().row()==0);
    std::cout << "notification event selected, no intermediate tool show, Esc restored original button and row\n";
    s.close();
}
/** Verify the composited widget, including transparent viewports and the native popup windows. */
void panelThemes()
{
    const auto applicationTipPalette = QToolTip::palette();
    QTemporaryDir workspace;
    CHECK(workspace.isValid());
    QImage pattern(120,80,QImage::Format_RGB32);
    for (int y=0; y<pattern.height(); ++y)
        for (int x=0; x<pattern.width(); ++x)
            pattern.setPixelColor(x,y, (x/32+y/24)%2 ? QColor(35,82,130) : QColor(37,167,128));
    const auto background=workspace.filePath("background.png");
    CHECK(pattern.save(background));
    const auto evidence=qEnvironmentVariable("EVE_UI_EVIDENCE_DIR");
    bool passed=true;
    for (const int material : {0,4}) {
        const auto preferences=workspace.filePath(QString("preferences-%1.json").arg(material));
        QFile file(preferences);CHECK(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{
            {"workbench.windowMaterial", material ? "acrylic" : "normal"},
            {"workbench.panelHeight", 280},
            {"workbench.wallpaper", material ? background : QString{}}}).toJson());
        file.close();
        Scene scene(preferences);
        scene.window->resize(1180,760);
        auto plugin=scene.kernel->registerPlugin("检查来源",[](Context){
            return failedTask(makeError(ErrorCode::ActivationFailed,"无法载入示例配置"));
        }).value();
        auto instance=scene.kernel->mount(plugin).value();until([&]{return instance.ready().isReady();});
        auto panel=scene.find<QTabWidget>("EveWorkbenchPanel");
        auto output=scene.find<QPlainTextEdit>("EveWorkbenchOutput");
        auto tree=scene.find<QTreeWidget>("EveProblemsTree");
        auto detail=scene.find<QPlainTextEdit>("EveProblemDetails");
        auto notifications=scene.find<QWidget>("EveNotificationCenter");
        auto commands=scene.find<QWidget>("EveCommandPanel");
        auto select=scene.find<QComboBox>("EveOutputTarget");
        auto notice=scene.find<QLabel>("EveOutputFeedback");
        scene.find<QAction>("EveOpenOutput")->trigger();
        scene.find<QToolButton>("EveOutputFollow")->setChecked(false);
        scene.shell.writeOutput("工具区显示与主题同步").value();
        until([&]{return tree->topLevelItemCount()>0;});
        const auto* tabs = panel->findChild<QTabBar*>();
        const int problems = panel->indexOf(scene.find<QWidget>("EveProblemsPanel"));
        CHECK(tabs && problems >= 0);
        CHECK(tabs->tabRect(problems).width() >= tabs->fontMetrics().horizontalAdvance(tabs->tabText(problems)) + 20);
        for (const auto mode : {ElaThemeType::Light,ElaThemeType::Dark,ElaThemeType::Light}) {
            eTheme->setThemeMode(mode);
            const bool dark=mode==ElaThemeType::Dark;
            const auto label=QString("%1-%2").arg(material ? "acrylic":"normal",dark ? "dark":"light");
            const auto capture=[&](QWidget* widget,const QString& name) {
                QTest::qWait(25);
                auto frame=widget->window()->grab();
                if (!evidence.isEmpty()) CHECK(frame.save(QDir(evidence).filePath(label+"-"+name+".png")));
                return frame;
            };
            const auto surface=[&](const QPixmap& image,QWidget* widget,const QPoint& point,const char* name,
                                    bool showsWallpaper = false) {
                const auto root=widget->mapTo(widget->window(),point);
                const auto p=image.toImage().pixelColor(qRound(root.x()*image.devicePixelRatioF()),
                                                        qRound(root.y()*image.devicePixelRatioF()));
                const auto luma=qGray(p.rgb());
                // Both test wallpaper colors have more green than red, even through the Ela theme tint.
                const bool hasWallpaper = !showsWallpaper || !scene.window->property("EveWallpaperVisible").toBool()
                    || p.green() > p.red() + 1;
                const bool good=p.alpha()>0 && (dark ? luma<135 : luma>145) && hasWallpaper;
                if(!good)std::cerr<<label.toStdString()<<" "<<name<<" #"<<p.name().toStdString()
                                  <<" alpha="<<p.alpha()<<std::endl;
                passed &= good;
            };
            const auto readable=[&](const QPixmap& frame,QWidget* widget,QRect area,QPoint sample,const char* name,
                                    double minimum=4.5,QColor expectedInk={}) {
                const auto image=frame.toImage();
                const auto scale=frame.devicePixelRatioF();
                const auto offset=widget->mapTo(widget->window(),QPoint{});
                const auto pixel=[&](QPoint local){
                    const auto pos=local+offset;
                    return image.pixelColor(qRound(pos.x()*scale),qRound(pos.y()*scale));
                };
                const auto luminance=[](QColor color){
                    const auto linear=[](qreal v){return v<=0.04045 ? v/12.92 : std::pow((v+0.055)/1.055,2.4);};
                    return 0.2126*linear(color.redF())+0.7152*linear(color.greenF())+0.0722*linear(color.blueF());
                };
                const auto base=luminance(pixel(sample));
                int count=0;
                area=area.intersected(widget->rect());
                for(int y=area.top();y<=area.bottom();++y)
                    for(int x=area.left();x<=area.right();++x){
                        const auto color=pixel({x,y});
                        if(expectedInk.isValid() && std::max({std::abs(color.red()-expectedInk.red()),
                            std::abs(color.green()-expectedInk.green()),std::abs(color.blue()-expectedInk.blue())})>48)
                            continue;
                        const auto glyph=luminance(color);
                        if((std::max(base,glyph)+0.05)/(std::min(base,glyph)+0.05)>=minimum)++count;
                    }
                if(count<5)std::cerr<<label.toStdString()<<" "<<name<<" high-contrast glyph pixels "<<count<<std::endl;
                passed &= count>=5;
            };
            panel->setCurrentWidget(output);
            auto frame=capture(output,"output");
            surface(frame,output->viewport(),output->viewport()->rect().bottomRight()-QPoint(25,15),"output viewport",true);
            CHECK(notice->isVisible());
            readable(frame,notice,notice->rect(),notice->rect().topRight(), "output feedback");
            const auto textColor=eTheme->getThemeColor(mode,ElaThemeType::BasicText);
            const auto popupColor=eTheme->getThemeColor(mode,ElaThemeType::PopupBase);
            const auto checkPopup = [&](QWidget* popup, const QPixmap& frame) {
                CHECK(popup->isVisible());
                QImage shape(popup->size(), QImage::Format_ARGB32_Premultiplied);
                shape.fill(Qt::transparent);
                popup->render(&shape, {}, {}, QWidget::RenderFlags{});
                CHECK(shape.pixelColor(0, 0).alpha() == 0);
                CHECK(shape.pixelColor(shape.rect().bottomRight()).alpha() == 0);
                const auto point = popup->mapTo(scene.window, QPoint(4, 4));
                const auto scale = frame.devicePixelRatioF();
                const auto color = frame.toImage().pixelColor(qRound(point.x() * scale), qRound(point.y() * scale));
                if (color != popupColor)
                    std::cerr << label.toStdString() << " " << popup->objectName().toStdString() << " "
                              << color.name(QColor::HexArgb).toStdString() << " expected "
                              << popupColor.name(QColor::HexArgb).toStdString() << '\n';
                passed &= color == popupColor;
            };
            scene.find<QToolButton>("EveShowNotifications")->click();
            until([&] { return notifications->isVisible(); });
            checkPopup(notifications, capture(notifications, "notifications"));
            scene.find<QPushButton>("EveNotificationClose")->click();
            CHECK(!notifications->isVisible());
            CHECK(QToolTip::palette() == applicationTipPalette);
            const auto checkTipFill = [&](QWidget* tip, const QString& message, QColor expected) {
                const auto image = tip->grab().toImage();
                bool hasFill = false;
                for (int y = 0; y < image.height() && !hasFill; ++y)
                    for (int x = 0; x < image.width() && !hasFill; ++x)
                        hasFill = image.pixelColor(x, y) == expected;
                if (!hasFill && !image.isNull())
                    std::cerr << label.toStdString() << " tooltip " << message.toStdString() << " fill "
                              << image.pixelColor(image.width() - 3, image.height() / 2).name(QColor::HexArgb).toStdString()
                              << " expected " << expected.name(QColor::HexArgb).toStdString() << '\n';
                CHECK(hasFill);
            };
            const auto showTip = [&](QWidget* source, const QString& message, QColor expected) {
                QToolTip::showText(output->mapToGlobal(QPoint(40, 40)), message, source);
                QLabel* tip = nullptr;
                until([&] {
                    for (auto* top : QApplication::topLevelWidgets()) {
                        auto* tipLabel = qobject_cast<QLabel*>(top);
                        if (tipLabel && tipLabel->isVisible() && tipLabel->windowType() == Qt::ToolTip &&
                            tipLabel->objectName() == QLatin1String("qtooltip_label") && tipLabel->text() == message) {
                            tip = tipLabel;
                            return true;
                        }
                    }
                    return false;
                });
                checkTipFill(tip, message, expected);
                return tip;
            };
            auto* hostTip = showTip(output, QStringLiteral("Eve"), popupColor);
            CHECK(showTip(output, QStringLiteral("Updated output"), popupColor) == hostTip);
            QWidget pluginSource(output);
            const QColor pluginColor("#28684f");
            pluginSource.setStyleSheet(QStringLiteral("QToolTip{background-color:#28684f;color:white;}"));
            CHECK(showTip(&pluginSource, QStringLiteral("Plugin"), pluginColor) == hostTip);
            CHECK(showTip(output, QStringLiteral("Output"), popupColor) == hostTip);
            const auto otherMode = dark ? ElaThemeType::Light : ElaThemeType::Dark;
            eTheme->setThemeMode(otherMode);
            CHECK(QToolTip::isVisible());
            checkTipFill(hostTip, QStringLiteral("Output"), eTheme->getThemeColor(otherMode, ElaThemeType::PopupBase));
            eTheme->setThemeMode(mode);
            checkTipFill(hostTip, QStringLiteral("Output"), popupColor);
            QToolTip::hideText();
            until([] { return !QToolTip::isVisible(); });
            if(notice->palette().color(QPalette::WindowText)!=textColor) {
                std::cerr<<label.toStdString()<<" output feedback text "<<notice->palette().color(QPalette::WindowText).name().toStdString()<<std::endl;
                passed=false;
            }
            select->showPopup();
            QTest::qWait(425);
            auto popup=select->view();
            frame=capture(popup,"source");
            surface(frame,popup->viewport(),QPoint(popup->viewport()->width()*3/4,popup->viewport()->height()/2),"source popup");
            select->hidePopup();
            auto more=scene.find<QToolButton>("EveOutputMore")->menu();
            more->popup(scene.window->mapToGlobal(QPoint(180,120)));
            QTest::qWait(425);
            frame=capture(more,"filter-menu");
            surface(frame,more,QPoint(more->width()/2,more->actionGeometry(more->actions().first()).center().y()),"filters menu");
            more->hide();
            panel->setCurrentWidget(scene.find<QWidget>("EveProblemsPanel"));
            tree->setCurrentItem(tree->topLevelItem(0)->child(0));
            frame=capture(tree,"problems");
            surface(frame,tree->viewport(),tree->viewport()->rect().bottomRight()-QPoint(12,12),"problem tree",true);
            surface(frame,detail->viewport(),detail->viewport()->rect().bottomRight()-QPoint(12,12),"problem detail",true);
            surface(frame,tree->header()->viewport(),QPoint(190,tree->header()->height()/2),"problem header",true);
            auto header=tree->header()->viewport();
            readable(frame,header,QRect(4,2,142,header->height()-4),QPoint(190,header->height()/2),"header labels",4.5,textColor);
            readable(frame,detail->viewport(),QRect(2,2,162,23),detail->viewport()->rect().center(),"problem details text");
            auto inspectRow=[&](QTreeWidgetItem* item,const char* name){
                auto row=tree->visualItemRect(item);
                readable(frame,tree->viewport(),row.adjusted(22,2,-45,-2),
                         QPoint(row.right()-24,row.center().y()),name);
            };
            inspectRow(tree->topLevelItem(0),"unselected problem group");
            inspectRow(tree->currentItem(),"selected problem row");
            tree->setCurrentItem(tree->topLevelItem(0));
            auto related=scene.find<QPushButton>("EveProblemLog");
            CHECK(!related->isEnabled());
            frame=capture(tree,"problems-disabled");
            // Disabled Ela controls intentionally use their quieter native text token.
            readable(frame,related,related->rect().adjusted(6,4,-6,-4),QPoint(4,related->height()/2),
                     "disabled related-log button",dark ? 3.0 : 1.6);
            scene.find<QAction>("EveOpenCommands")->trigger();
            auto list=scene.find<QListView>("EveCommandList");
            auto search=scene.find<QLineEdit>("EveCommandFilter");
            search->setText("EveOpenOutput");
            CHECK(list->model()->rowCount()==1);
            frame=capture(list,"commands");
            surface(frame,list->viewport(),list->viewport()->rect().center(),"command list");
            search->setText("nonexistent-test-command");
            CHECK(list->model()->rowCount()==0);
            // Ela's combo box emits a buttonless focus event when its close animation completes.
            QMouseEvent focusNotification(QEvent::MouseButtonPress,QPoint(-1,-1),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(scene.window,&focusNotification);
            frame=capture(list,"commands-empty");
            checkPopup(commands, frame);
            auto commandPanel=list->parentWidget();
            auto center=list->viewport()->mapTo(scene.window,list->viewport()->rect().center());
            CHECK(commandPanel->isVisible() && commandPanel->isAncestorOf(scene.window->childAt(center)));
            surface(frame,list->viewport(),list->viewport()->rect().center(),"empty command list");
            QTest::keyClick(search,Qt::Key_Escape);
        }
        scene.close();
    }
    CHECK(passed);
}
int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    try {
#ifdef Q_OS_WIN
        if (QApplication::platformName() == "offscreen")
            CHECK(QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc") >= 0);
#endif
        CHECK(argc == 2); const std::string name = argv[1];
        if(name=="historical_filters"){historicalFilters();return 0;}
        if(name=="throughput"){throughput();return 0;}
        if(name=="responsive"){responsive();return 0;}
        if(name=="cross_window"){crossWindow();return 0;}
        if(name=="themes"){panelThemes();return 0;}
        if (name == "commands") commands(); else if (name == "identity") identity(); else if (name == "outputs") outputs(); else if (name == "limits") limits(); else if (name == "problems") problems(); else if (name == "reentry") reentry(); else if (name == "clear_reentry") clearReentry(); else if (name == "output_history") outputHistory(); else if (name == "problem_updates") problemUpdates(); else return 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
