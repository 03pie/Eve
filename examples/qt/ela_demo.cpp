/** @file ela_demo.cpp
 * @brief 展示页面、命令及纯值通知，关闭窗口前等待 Kernel 与外壳完整清理。
 */
#include <cordis/ui/ela_shell.hpp>
#include <cordis/kernel.hpp>
#include <QApplication>
#include <QAction>
#include <QFontDatabase>
#include <ElaText.h>
#include <ElaPushButton.h>
#include <ElaPlainTextEdit.h>
#include <ElaPopularCard.h>
#include <ElaScrollPage.h>
#include <ElaScrollPageArea.h>
#include <ElaTheme.h>
#include <ElaTreeView.h>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QPushButton>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace cordis;
using namespace cordis::ui;
/** @brief 资源树属于笔记插件的根 QWidget，只切换插件内部内容，不参与宿主导航。 */
std::unique_ptr<QWidget> createNotesPage()
{
    auto root = std::make_unique<QWidget>();
    auto rootLayout = new QHBoxLayout(root.get()); rootLayout->setContentsMargins(0, 0, 0, 0);
    auto splitter = new QSplitter(Qt::Horizontal, root.get()); rootLayout->addWidget(splitter);
    auto sidebar = new QWidget(splitter); sidebar->setMinimumWidth(150);
    sidebar->setObjectName("CordisDemoNotesSidebar");
    auto sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->addWidget(new ElaText(QString::fromUtf8("笔记资源"), 14, sidebar));
    auto tree = new ElaTreeView(sidebar); tree->setObjectName("CordisDemoNotesTree");
    tree->setHeaderHidden(true); tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setItemHeight(32); tree->setStyleSheet("ElaTreeView{background-color:transparent;border:0;}");
    auto model = new QStandardItemModel(tree); tree->setModel(model);
    for (const auto& title : {"临时笔记", "使用说明"}) model->appendRow(new QStandardItem(QString::fromUtf8(title)));
    sidebarLayout->addWidget(tree, 1);
    auto stack = new QStackedWidget(splitter);
    auto notes = new ElaScrollPage(stack); notes->setWindowTitle(QString::fromUtf8("临时笔记"));
    auto content = new QWidget; content->setWindowTitle(notes->windowTitle()); auto layout = new QVBoxLayout(content);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    auto area = new ElaScrollPageArea(content); auto body = new QVBoxLayout(area);
    area->setMinimumHeight(0); area->setMaximumHeight(QWIDGETSIZE_MAX);
    area->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    body->setContentsMargins(20, 16, 20, 16);
    auto label = new ElaText(QString::fromUtf8("切换页面或关闭页签会保留本次运行的内容。停止插件后清空。"), area);
    label->setWordWrap(true); label->setTextPixelSize(14); label->setTextFormat(Qt::PlainText);
    auto button = new ElaPushButton(QString::fromUtf8("点击计数：0"), area);
    auto count = std::make_shared<int>(0);
    QObject::connect(button, &QPushButton::clicked, root.get(), [button, count] {
        button->setText(QString::fromUtf8("点击计数：%1").arg(++*count));
    });
    body->addWidget(label); body->addWidget(button); layout->addWidget(area);
    auto editor = new ElaPlainTextEdit(content); editor->setObjectName("CordisDemoNotes");
    editor->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}");
    editor->setPlaceholderText(QString::fromUtf8("在这里输入临时笔记……"));
    editor->setMinimumHeight(180); layout->addWidget(editor); layout->addStretch();
    notes->addCentralWidget(content); stack->addWidget(notes);
    auto guide = new ElaScrollPage(stack); guide->setWindowTitle(QString::fromUtf8("使用说明"));
    auto guideContent = new QWidget; guideContent->setWindowTitle(guide->windowTitle()); auto guideLayout = new QVBoxLayout(guideContent);
    guideLayout->setSizeConstraint(QLayout::SetMinimumSize);
    auto guideArea = new ElaScrollPageArea(guideContent); auto guideBody = new QVBoxLayout(guideArea);
    guideArea->setMinimumHeight(0); guideArea->setMaximumHeight(QWIDGETSIZE_MAX);
    guideArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    guideBody->setContentsMargins(20, 16, 20, 16);
    auto text = new ElaText(QString::fromUtf8("左侧笔记资源树属于此插件；全局活动栏只负责打开插件页面。\n\n顶部前进/后退切换宿主页面，不改变笔记内部选择。\n\n底部“显示笔记提醒”命令会打开插件并写入通知与输出。\n\n当前笔记仅保存在内存中，尚未接入文件保存。"), guideArea);
    text->setWordWrap(true); text->setTextFormat(Qt::PlainText); text->setTextPixelSize(14);
    guideBody->addWidget(text); guideLayout->addWidget(guideArea); guideLayout->addStretch();
    guide->addCentralWidget(guideContent); stack->addWidget(guide);
    QObject::connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, root.get(), [stack](const QModelIndex& index) {
        if (index.isValid()) stack->setCurrentIndex(index.row());
    });
    tree->setCurrentIndex(model->index(0, 0));
    splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1); splitter->setSizes({190, 800});
    return root;
}
/** @brief 演示状态由 main 与正在执行的 continuation 持有，运行时定义仅借用弱引用。 */
struct Application : std::enable_shared_from_this<Application> {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ElaShell shell{scheduler};
    std::optional<Instance> provider;
    std::optional<Instance> notes;
    std::optional<PageHandle> page;
    std::optional<CommandHandle> command;
    std::optional<Instance> waiting;
    std::optional<Instance> failed;
    int failureAttempts = 0;
    bool stopping = false;
    bool finished = false;
    /** @brief 错误保持非零退出，不伪造 Kernel 已静止。 */
    void fail(const char* message) { std::cerr << message << '\n'; QCoreApplication::exit(EXIT_FAILURE); }
    /** @brief 正常退出只在内核、页面和窗口均静止后停止 scheduler。 */
    void stop()
    {
        if (stopping) return;
        stopping = true;
        auto self = shared_from_this();
        kernel->close().thenOn(scheduler, [self](const Result<CleanupReport>& report) {
            if (!report.hasValue() || !report.value().quiescent) { self->fail("kernel retained resources"); return; }
            self->shell.close().thenOn(self->scheduler, [self](const Result<void>& result) {
                if (!result.hasValue() || !self->shell.pages()->snapshot().value().empty() || !self->shell.commands()->snapshot().value().empty()) { self->fail("shell cleanup failed"); return; }
                if (!self->scheduler->close().hasValue()) { self->fail("scheduler close failed"); return; }
                self->finished = true;
                std::cout << "Kernel, contributions and Ela window closed\n";
                QCoreApplication::exit(EXIT_SUCCESS);
            });
        });
    }
    /** @brief UI 提供者与声明依赖的消费者通过服务租约连接；插件不获得 ElaWindow。 */
    void start(bool smoke)
    {
        std::weak_ptr<Application> weak = shared_from_this();
        shell.management()->attach(kernel).value();
        auto service = shell.pages();
        auto commands = shell.commands();
        auto notices = shell.notifications();
        auto diagnostics = shell.diagnostics();
        auto ui = kernel->registerPlugin("ui", [service, commands, notices, diagnostics](Context context) {
            context.provide({"ui.pages"}, service).value();
            context.provide({"ui.commands"}, commands).value();
            context.provide({"ui.notifications"}, notices).value();
            context.provide({"diagnostics"}, diagnostics).value();
            return completedTask();
        }, {}, {ServiceOutput::of<UiPagesService>({"ui.pages"}), ServiceOutput::of<CommandService>({"ui.commands"}), ServiceOutput::of<NotificationService>({"ui.notifications"}), ServiceOutput::of<DiagnosticsService>({"diagnostics"})}).value();
        auto consumer = kernel->registerPlugin("notes", [weak](Context context) {
            auto self = weak.lock();
            if (!self) return failedTask(makeError(ErrorCode::StaleContext, "demo expired"));
            auto service = context.consume<UiPagesService>({"ui.pages"}).value();
            auto call = service.withService([&](UiPagesService& pages) {
                auto added = pages.add(context, {"notes", "笔记示例", "内置插件", createNotesPage});
                if (!added.hasValue()) throw std::runtime_error(added.error().message);
                self->page.emplace(added.value());
            });
            if (!call.hasValue()) return failedTask(call.error());
            auto notices = context.consume<NotificationService>({"ui.notifications"}).value();
            auto registered = context.consume<CommandService>({"ui.commands"}).value().withService([&](CommandService& commands) {
                auto page = *self->page;
                self->command = commands.add(context, {"notes.remind", "显示笔记提醒", [page, notices, context] {
                    auto navigated = page.navigate();
                    if (!navigated.hasValue()) return failedTask(navigated.error());
                    auto posted = notices.withService([&](NotificationService& service) {
                        service.post(context, {NotificationLevel::Info, "笔记提醒：页面已打开，可以开始记录。", {}}).value();
                    });
                    return posted.hasValue() ? completedTask() : failedTask(posted.error());
                }, {}}).value();
            });
            if (!registered.hasValue()) return failedTask(registered.error());
            return self->command->ready();
        }, {ServiceRequirement::of<UiPagesService>({"ui.pages"}), ServiceRequirement::of<CommandService>({"ui.commands"}), ServiceRequirement::of<NotificationService>({"ui.notifications"})}).value();
        auto missing = kernel->registerPlugin("缺少存储依赖", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"demo.storage"})}).value();
        auto unstable = kernel->registerPlugin("失败重试示例", [weak](Context) {
            auto self = weak.lock();
            if (!self) return failedTask(makeError(ErrorCode::StaleContext, "demo expired"));
            if (++self->failureAttempts == 1) {
                auto error = makeError(ErrorCode::ActivationFailed, "首次启动失败，可在管理页重试");
                error.phase = "demo.activate";
                return failedTask(std::move(error));
            }
            return completedTask();
        }).value();
        notes.emplace(kernel->mount(consumer).value());
        provider.emplace(kernel->mount(ui).value());
        waiting.emplace(kernel->mount(missing).value());
        failed.emplace(kernel->mount(unstable).value());
        shell.onCloseRequested([weak] { if (auto self = weak.lock()) self->stop(); }).value();
        shell.show().value();
        auto self = shared_from_this();
        notes->ready().thenOn(scheduler, [self, smoke](const Result<void>& result) {
            if (!result.hasValue()) { self->fail("page activation failed"); return; }
            if (!smoke) { self->shell.management()->refresh().value(); return; }
            if (smoke) self->command->execute().thenOn(self->scheduler, [self](const Result<void>& invoked) {
                if (!invoked.hasValue() || self->shell.notifications()->snapshot().value().empty()) { self->fail("command notification failed"); return; }
                self->shell.management()->refresh().value();
                self->shell.showManagement().value();
                QTimer::singleShot(50, QCoreApplication::instance(), [self] { self->captureAndClose(); });
            });
        });
    }
    /** @brief 截图包含命令及通知，再通过真实 QWidget::close 触发异步退出。 */
    void captureAndClose()
    {
        auto data = shell.diagnostics()->snapshot();
        if (!data.hasValue() || data.value().instances.size() != 4 || waiting->snapshot().value().state != InstanceState::Pending || failed->snapshot().value().state != InstanceState::Failed) {
            fail("management diagnostics missing pending/failed examples"); return;
        }
        for (auto window : QApplication::topLevelWidgets()) if (window->windowTitle() == QString::fromUtf8("Cordis 插件平台")) {
            if (!window->grab().save(QCoreApplication::applicationDirPath() + (eTheme->getThemeMode() == ElaThemeType::Dark ? "/ela-smoke-dark.png" : "/ela-smoke.png"))) { fail("preview capture failed"); return; }
            QPointer<QWidget> guarded(window);
            auto self = shared_from_this();
            auto home = window->findChild<QAction*>("CordisOpenHome");
            if (!home) { fail("home action missing"); return; }
            home->trigger();
            QTimer::singleShot(100, QCoreApplication::instance(), [self, guarded] {
                if (!guarded || guarded->findChildren<ElaPopularCard*>().size() != 4) { self->fail("home plugin catalogue incomplete"); return; }
                if (!guarded || !guarded->grab().save(QCoreApplication::applicationDirPath() + (eTheme->getThemeMode() == ElaThemeType::Dark ? "/ela-home-smoke-dark.png" : "/ela-home-smoke.png"))) { self->fail("home preview capture failed"); return; }
                self->page->navigate().value();
                QTimer::singleShot(350, QCoreApplication::instance(), [self, guarded] {
                    if (!guarded || !guarded->grab().save(QCoreApplication::applicationDirPath() + (eTheme->getThemeMode() == ElaThemeType::Dark ? "/ela-notes-smoke-dark.png" : "/ela-notes-smoke.png"))) { self->fail("plugin preview capture failed"); return; }
                    guarded->close();
                });
            });
            return;
        }
        fail("Ela window missing");
    }
};
} // namespace

/** @brief 默认交互运行，--smoke/--smoke-dark 分别检查明暗预览和真实关闭事件的完整退出。 */
int main(int argc, char** argv)
{
    try {
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        const bool dark = argc == 2 && std::string(argv[1]) == "--smoke-dark";
        const bool smoke = dark || (argc == 2 && std::string(argv[1]) == "--smoke");
        eTheme->setThemeMode(dark ? ElaThemeType::Dark : ElaThemeType::Light);
#ifdef Q_OS_WIN
        // offscreen 不枚举 Windows 字体；预览显式加载系统中文字体，交互模式仍用系统字体库。
        if (smoke && QApplication::platformName() == "offscreen")
            QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc");
#endif
        auto state = std::make_shared<Application>();
        state->start(smoke);
        if (smoke) QTimer::singleShot(10000, &app, [] { QCoreApplication::exit(EXIT_FAILURE); });
        const int result = app.exec();
        return result == EXIT_SUCCESS && state->finished ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
}
