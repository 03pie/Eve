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
#include <QTabWidget>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QSplitter>
#include <QTreeWidget>
#include <QToolButton>
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
    CHECK(target->count()==3 && target->currentData().toString().isEmpty());
    const int plugin=target->findData("plugin/panel.output"); CHECK(plugin>=0);
    target->setCurrentIndex(plugin);
    CHECK(editor->toPlainText().contains("first <b>literal</b>") && editor->toPlainText().contains("second"));
    auto scope=s.find<QComboBox>("CordisOutputInstance");
    scope->setCurrentIndex(scope->findData(QString::number(first.snapshot().value().instance.value())));
    CHECK(editor->toPlainText().contains("first <b>literal</b>") && !editor->toPlainText().contains("second"));
    scope->setCurrentIndex(scope->findData(QString::number(second.snapshot().value().instance.value())));
    CHECK(editor->toPlainText().contains("second") && !editor->toPlainText().contains("literal"));
    s.find<QTabWidget>("CordisWorkbenchPanel")->setCurrentWidget(editor); s.capture("output");
    s.find<QToolButton>("CordisOutputFollow")->setChecked(false);
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
    s.find<QComboBox>("CordisOutputTarget")->setCurrentIndex(0);
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
    CHECK(s.find<QComboBox>("CordisOutputTarget")->findData("plugin/limits") >= 0);
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
    auto targets=s.find<QComboBox>("CordisOutputTarget");
    targets->setCurrentIndex(targets->findData("plugin/clear"));
    auto editor=s.find<QPlainTextEdit>("CordisWorkbenchOutput");
    bool entered=false;
    const auto connection=QObject::connect(editor,&QPlainTextEdit::textChanged,editor,[&]{
        if(entered) return;
        entered=true; targets->setCurrentIndex(targets->findData("platform"));
    });
    s.find<QAction>("CordisClearOutput")->trigger();QObject::disconnect(connection);
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
    Scene s; s.find<QAction>("CordisClearOutput")->trigger();
    for (int i = 0; i < 600; ++i) s.shell.writeOutput("retained-line-" + std::to_string(i)).value();
    s.find<QAction>("CordisCopyOutput")->trigger();
    const auto copied = QApplication::clipboard()->text();
    CHECK(copied.contains("retained-line-0\n") && copied.contains("retained-line-599"));
    auto* editor = s.find<QPlainTextEdit>("CordisWorkbenchOutput");
    auto* panel = s.find<QTabWidget>("CordisWorkbenchPanel");
    panel->setCurrentWidget(editor);
    panel->show();
    until([&] { return editor->blockCount() == 600; });
    s.shell.writeOutput(std::string(2199, '\n') + "middle").value();
    s.shell.writeOutput(std::string(2198, '\n') + "tail").value();
    until([&] { return editor->blockCount() == 4999; });
    CHECK(editor->maximumBlockCount() == 5000);

    s.find<QToolButton>("CordisOutputFollow")->setChecked(false);
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
    s.find<QAction>("CordisCopyOutput")->trigger();
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
QAction* outputMenuAction(Scene& scene,const QString& name)
{
    auto menu=scene.find<QToolButton>("CordisOutputMore")->menu();
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
    s.find<QAction>("CordisPreviousOutput")->trigger();
    CHECK(s.find<QComboBox>("CordisOutputRun")->count()==2);
    auto editor=s.find<QPlainTextEdit>("CordisWorkbenchOutput");
    auto search=s.find<QLineEdit>("CordisOutputSearch");
    auto level=s.find<QComboBox>("CordisOutputLevel");
    auto sources=s.find<QComboBox>("CordisOutputTarget");
    sources->setCurrentIndex(sources->findData("plugin/archive.example"));
    auto instances=s.find<QComboBox>("CordisOutputInstance");
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
    outputMenuAction(s,QString::fromUtf8("读取前一页"))->trigger();
    CHECK(editor->toPlainText().count("sample-row-")==400);
    outputMenuAction(s,QString::fromUtf8("读取前一页"))->trigger();
    CHECK(editor->toPlainText().count("sample-row-")==600);
    for(int page=0;page<4;++page) outputMenuAction(s,QString::fromUtf8("读取前一页"))->trigger();
    CHECK(editor->toPlainText().count("sample-row-")==1000);
    CHECK(editor->toPlainText().contains("sample-row-1\n") && !editor->toPlainText().contains("sample-row-1311"));
    outputMenuAction(s,QString::fromUtf8("返回最新一页"))->trigger();
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
    outputMenuAction(s,QString::fromUtf8("导出已筛选记录…"))->trigger();
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
    outputMenuAction(s, QString::fromUtf8("导出已筛选记录…"))->trigger();
    CHECK(!QFileInfo::exists(archivePath));
    s.find<QAction>("CordisClearOutput")->trigger();CHECK(editor->toPlainText().isEmpty());
    s.find<QAction>("CordisPreviousOutput")->trigger();CHECK(editor->toPlainText().contains("sample-row-1311"));
    CHECK(digest()==before);
    s.close();
}
void responsive()
{
    Scene s;auto panel=s.find<QTabWidget>("CordisWorkbenchPanel");auto editor=s.find<QPlainTextEdit>("CordisWorkbenchOutput");
    s.window->resize(700,700);
    QCoreApplication::processEvents();
    const auto menu=s.find<QMenuBar>("CordisMenuBar");
    const auto search=s.find<QWidget>("CordisPageSearch");
    const auto endOfMenu=menu->actionGeometry(menu->actions().last()).topRight();
    CHECK(menu->mapTo(s.window,endOfMenu).x()<search->mapTo(s.window,QPoint(0,0)).x());
    s.find<QAction>("CordisOpenOutput")->trigger();
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
        CHECK(s.find<QComboBox>("CordisOutputRun")->isVisible()==(width>=760));
        CHECK(s.find<QLineEdit>("CordisOutputSearch")->isVisible()==(width>=1080));
        CHECK(s.find<QComboBox>("CordisOutputTarget")->isVisible() && s.find<QToolButton>("CordisOutputMore")->isVisible());
        CHECK(editor->textCursor().selectedText()==selected);
        const auto height=panel->height();panel->setCurrentIndex(1);QCoreApplication::processEvents();CHECK(panel->height()==height);
        panel->setCurrentIndex(0);QCoreApplication::processEvents();CHECK(panel->height()==height);
        for(const char* id:{"CordisHidePanel","CordisCopyPanelOutput","CordisClearPanelOutput"}) {
            auto button=s.find<QToolButton>(id);
            if(width<1080 && button->objectName()!="CordisHidePanel") {CHECK(!button->isVisible());continue;}
            auto center=button->mapTo(panel,button->rect().center());
            if(!panel->rect().contains(center) || panel->childAt(center)!=button) {
                const auto parent=button->parentWidget()->geometry();
                std::cerr << id << " at width " << width << " center "<<center.x()<<","<<center.y()
                          <<" parent "<<parent.x()<<","<<parent.y()<<" "<<parent.width()<<"x"<<parent.height()
                          <<" hit "<<(panel->childAt(center)?panel->childAt(center)->objectName().toStdString():"none")<<'\n';
            }
            CHECK(panel->rect().contains(center) && panel->childAt(center)==button);
        }
        const auto folder=qEnvironmentVariable("CORDIS_UI_EVIDENCE_DIR");
        if(!folder.isEmpty()) {
            const auto filename=QString("output-%1-%2-%3.png").arg(mode==ElaThemeType::Dark?"dark":"light").arg(width).arg(s.window->devicePixelRatioF());
            CHECK(s.window->grab().save(QDir(folder).filePath(filename)));
        }
      }
    }
    outputMenuAction(s,QString::fromUtf8("复制本条"))->trigger();
    CHECK(QApplication::clipboard()->text().contains("buffer prepared\n[123] continuation"));
    s.find<QAction>("CordisOpenSettings")->trigger();
    auto origin=s.find<QLineEdit>("CordisSettingsSearch");
    origin->setFocus();origin->setText("developer");origin->setSelection(7,-4);
    s.shell.showOutputEvent(seq,origin).value();
    CHECK(editor->textCursor().selectedText()==selected);
    QTest::keyClick(s.find<QComboBox>("CordisOutputTarget"),Qt::Key_Escape);
    until([&]{return !panel->isVisible() && QApplication::focusWidget()==origin;});
    CHECK(origin->selectedText()=="elop" && origin->cursorPosition()==3);
    s.shell.showOutputEvent(seq+10000,origin).value();
    CHECK(s.find<QLabel>("CordisOutputFeedback")->text().contains(QString::fromUtf8("找不到该日志事件")));
    s.close();
}
void throughput()
{
    Scene s;s.window->resize(1360,800);s.find<QAction>("CordisOpenOutput")->trigger();
    auto editor=s.find<QPlainTextEdit>("CordisWorkbenchOutput");
    auto search=s.find<QLineEdit>("CordisOutputSearch");
    auto follow=s.find<QToolButton>("CordisOutputFollow");
    CHECK(search->isVisible());follow->setChecked(false);
    std::optional<Context> a,b;
    const auto first=s.kernel->registerPlugin("traffic.A",[&](Context ctx){a=ctx;return completedTask();}).value();
    const auto second=s.kernel->registerPlugin("traffic.B",[&](Context ctx){b=ctx;return completedTask();}).value();
    auto i=s.kernel->mount(first).value(),j=s.kernel->mount(second).value();wait(i.ready());wait(j.ready());
    auto output=s.shell.output();int updates=0;
    QObject::connect(editor,&QPlainTextEdit::textChanged,editor,[&]{++updates;});
    QElapsedTimer total;total.start();std::vector<qint64> latency;
    auto source=s.find<QComboBox>("CordisOutputTarget");
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
    auto table=s.find<QTableView>("CordisInstanceTable");
    auto origin=s.find<QPushButton>("CordisManagementOpenLog");
    auto dialog=origin->window();
    table->selectRow(0);origin->setFocus();
    until([&]{return dialog->isActiveWindow() && origin->isEnabled();});
    std::cout << "management active, log action enabled\n";
    QTest::keyClick(origin,Qt::Key_Space);
    auto targets=s.find<QComboBox>("CordisOutputTarget");
    auto output=s.find<QPlainTextEdit>("CordisWorkbenchOutput");
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
    auto notifications=s.find<QPushButton>("CordisManagementOpenNotifications");
    s.shell.notifications()->post(*context,{NotificationLevel::Warning,"managed notification event",{}}).value();
    auto list=s.find<QListView>("CordisNotificationList");
    auto center=s.find<QWidget>("CordisNotificationCenter");
    auto panel=s.find<QTabWidget>("CordisWorkbenchPanel");
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
    const auto folder=qEnvironmentVariable("CORDIS_UI_EVIDENCE_DIR");
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
    QTest::keyClick(s.find<QPushButton>("CordisNotificationOpenLog"),Qt::Key_Space);
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
    QTemporaryDir workspace;
    CHECK(workspace.isValid());
    QImage pattern(120,80,QImage::Format_RGB32);
    for (int y=0; y<pattern.height(); ++y)
        for (int x=0; x<pattern.width(); ++x)
            pattern.setPixelColor(x,y, (x/32+y/24)%2 ? QColor(35,82,130) : QColor(37,167,128));
    const auto background=workspace.filePath("background.png");
    CHECK(pattern.save(background));
    const auto evidence=qEnvironmentVariable("CORDIS_UI_EVIDENCE_DIR");
    bool passed=true;
    for (const int material : {0,4}) {
        const auto preferences=workspace.filePath(QString("preferences-%1.json").arg(material));
        QFile file(preferences);CHECK(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"version",1},{"theme",0},{"material",material},
                                            {"background",material ? background : QString{}}}).toJson());
        file.close();
        Scene scene(preferences);
        scene.window->resize(1180,760);
        auto plugin=scene.kernel->registerPlugin("检查来源",[](Context){
            return failedTask(makeError(ErrorCode::ActivationFailed,"无法载入示例配置"));
        }).value();
        auto instance=scene.kernel->mount(plugin).value();until([&]{return instance.ready().isReady();});
        auto panel=scene.find<QTabWidget>("CordisWorkbenchPanel");
        auto output=scene.find<QPlainTextEdit>("CordisWorkbenchOutput");
        auto tree=scene.find<QTreeWidget>("CordisProblemsTree");
        auto detail=scene.find<QPlainTextEdit>("CordisProblemDetails");
        auto select=scene.find<QComboBox>("CordisOutputTarget");
        auto notice=scene.find<QLabel>("CordisOutputFeedback");
        scene.find<QAction>("CordisOpenOutput")->trigger();
        scene.find<QSplitter>("ElaWorkbenchPanelSplitter")->setSizes({330,280});
        scene.find<QToolButton>("CordisOutputFollow")->setChecked(false);
        scene.shell.writeOutput("工具区显示与主题同步").value();
        until([&]{return tree->topLevelItemCount()>0;});
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
            const auto surface=[&](const QPixmap& image,QWidget* widget,const QPoint& point,const char* name) {
                const auto root=widget->mapTo(widget->window(),point);
                const auto p=image.toImage().pixelColor(qRound(root.x()*image.devicePixelRatioF()),
                                                        qRound(root.y()*image.devicePixelRatioF()));
                const auto luma=qGray(p.rgb());
                const bool good=p.alpha()>0 && (dark ? luma<135 : luma>145);
                if(!good)std::cerr<<label.toStdString()<<" "<<name<<" #"<<p.name().toStdString()
                                  <<" alpha="<<p.alpha()<<std::endl;
                passed &= good;
            };
            const auto readable=[&](const QPixmap& frame,QWidget* widget,QRect area,QPoint sample,const char* name,double minimum=4.5) {
                const auto image=frame.toImage();
                const auto scale=frame.devicePixelRatioF();
                const auto offset=widget->mapTo(widget->window(),QPoint{});
                const auto luminance=[&](QPoint local){
                    const auto pos=local+offset;
                    const auto color=image.pixelColor(qRound(pos.x()*scale),qRound(pos.y()*scale));
                    const auto linear=[](qreal v){return v<=0.04045 ? v/12.92 : std::pow((v+0.055)/1.055,2.4);};
                    return 0.2126*linear(color.redF())+0.7152*linear(color.greenF())+0.0722*linear(color.blueF());
                };
                const auto base=luminance(sample);
                int count=0;
                area=area.intersected(widget->rect());
                for(int y=area.top();y<=area.bottom();++y)
                    for(int x=area.left();x<=area.right();++x){
                        const auto glyph=luminance({x,y});
                        if((std::max(base,glyph)+0.05)/(std::min(base,glyph)+0.05)>=minimum)++count;
                    }
                if(count<5)std::cerr<<label.toStdString()<<" "<<name<<" high-contrast glyph pixels "<<count<<std::endl;
                passed &= count>=5;
            };
            panel->setCurrentWidget(output);
            auto frame=capture(output,"output");
            surface(frame,output->viewport(),output->viewport()->rect().bottomRight()-QPoint(25,15),"output viewport");
            CHECK(notice->isVisible());
            readable(frame,notice,notice->rect(),notice->rect().topRight(), "output feedback");
            const auto textColor=eTheme->getThemeColor(mode,ElaThemeType::BasicText);
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
            auto more=scene.find<QToolButton>("CordisOutputMore")->menu();
            more->popup(scene.window->mapToGlobal(QPoint(180,120)));
            QTest::qWait(425);
            frame=capture(more,"filter-menu");
            surface(frame,more,QPoint(more->width()/2,more->actionGeometry(more->actions().first()).center().y()),"filters menu");
            more->hide();
            panel->setCurrentWidget(scene.find<QWidget>("CordisProblemsPanel"));
            tree->setCurrentItem(tree->topLevelItem(0)->child(0));
            frame=capture(tree,"problems");
            surface(frame,tree->viewport(),tree->viewport()->rect().bottomRight()-QPoint(12,12),"problem tree");
            surface(frame,detail->viewport(),detail->viewport()->rect().bottomRight()-QPoint(12,12),"problem detail");
            surface(frame,tree->header()->viewport(),QPoint(190,tree->header()->height()/2),"problem header");
            auto header=tree->header()->viewport();
            readable(frame,header,QRect(4,2,142,header->height()-4),QPoint(190,header->height()/2),"header labels");
            readable(frame,detail->viewport(),QRect(2,2,162,23),detail->viewport()->rect().center(),"problem details text");
            auto inspectRow=[&](QTreeWidgetItem* item,const char* name){
                auto row=tree->visualItemRect(item);
                readable(frame,tree->viewport(),row.adjusted(22,2,-45,-2),
                         QPoint(row.right()-24,row.center().y()),name);
            };
            inspectRow(tree->topLevelItem(0),"unselected problem group");
            inspectRow(tree->currentItem(),"selected problem row");
            if(tree->header()->palette().color(QPalette::ButtonText)!=textColor) {
                std::cerr<<label.toStdString()<<" problem header text "<<tree->header()->palette().color(QPalette::ButtonText).name().toStdString()<<std::endl;
                passed=false;
            }
            tree->setCurrentItem(tree->topLevelItem(0));
            auto related=scene.find<QPushButton>("CordisProblemLog");
            CHECK(!related->isEnabled());
            frame=capture(tree,"problems-disabled");
            // Disabled Ela controls intentionally use their quieter native text token.
            readable(frame,related,related->rect().adjusted(6,4,-6,-4),QPoint(4,related->height()/2),
                     "disabled related-log button",dark ? 3.0 : 1.6);
            scene.find<QAction>("CordisOpenCommands")->trigger();
            auto list=scene.find<QListView>("CordisCommandList");
            auto search=scene.find<QLineEdit>("CordisCommandFilter");
            search->setText("CordisOpenOutput");
            CHECK(list->model()->rowCount()==1);
            frame=capture(list,"commands");
            surface(frame,list->viewport(),list->viewport()->rect().center(),"command list");
            search->setText("nonexistent-test-command");
            CHECK(list->model()->rowCount()==0);
            // Ela's combo box emits a buttonless focus event when its close animation completes.
            QMouseEvent focusNotification(QEvent::MouseButtonPress,QPoint(-1,-1),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(scene.window,&focusNotification);
            frame=capture(list,"commands-empty");
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
