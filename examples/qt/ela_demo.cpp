/** @file ela_demo.cpp
 * @brief 展示 ui.pages/v1 的真实 Ela 页面贡献，关闭窗口前等待 Kernel 与外壳完整清理。
 */
#include <cordis/ui/ela_shell.hpp>
#include <cordis/kernel.hpp>
#include <QApplication>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
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
/** @brief 演示状态由 main 与正在执行的 continuation 持有，运行时定义仅借用弱引用。 */
struct Application : std::enable_shared_from_this<Application> {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    Kernel kernel{scheduler};
    ElaShell shell{scheduler};
    std::optional<Instance> provider;
    std::optional<Instance> notes;
    std::optional<PageHandle> page;
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
        kernel.close().thenOn(scheduler, [self](const Result<CleanupReport>& report) {
            if (!report.hasValue() || !report.value().quiescent) { self->fail("kernel retained resources"); return; }
            self->shell.close().thenOn(self->scheduler, [self](const Result<void>& result) {
                if (!result.hasValue() || !self->shell.pages()->snapshot().value().empty()) { self->fail("shell cleanup failed"); return; }
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
        auto service = shell.pages();
        auto ui = kernel.registerPlugin("ui", [service](Context context) {
            auto provided = context.provide({"ui.pages"}, service);
            return provided.hasValue() ? completedTask() : failedTask(provided.error());
        }, {}, {ServiceOutput::of<UiPagesService>({"ui.pages"})}).value();
        auto consumer = kernel.registerPlugin("notes", [weak](Context context) {
            auto self = weak.lock();
            if (!self) return failedTask(makeError(ErrorCode::StaleContext, "demo expired"));
            auto service = context.consume<UiPagesService>({"ui.pages"}).value();
            auto call = service.withService([&](UiPagesService& pages) {
                auto added = pages.add(context, {"notes", "笔记示例", "内置插件", [] {
                    auto widget = std::make_unique<QWidget>();
                    auto layout = new QVBoxLayout(widget.get());
                    auto label = new QLabel(QString::fromUtf8("此页面由内置插件通过 ui.pages/v1 创建。"), widget.get());
                    label->setWordWrap(true);
                    auto button = new QPushButton(QString::fromUtf8("点击计数：0"), widget.get());
                    auto count = std::make_shared<int>(0);
                    QObject::connect(button, &QPushButton::clicked, widget.get(), [button, count] {
                        button->setText(QString::fromUtf8("点击计数：%1").arg(++*count));
                    });
                    layout->addWidget(label); layout->addWidget(button); layout->addStretch();
                    return widget;
                }});
                if (!added.hasValue()) throw std::runtime_error(added.error().message);
                self->page.emplace(added.value());
            });
            if (!call.hasValue()) return failedTask(call.error());
            return self->page->ready();
        }, {ServiceRequirement::of<UiPagesService>({"ui.pages"})}).value();
        notes.emplace(kernel.mount(consumer).value());
        provider.emplace(kernel.mount(ui).value());
        shell.onCloseRequested([weak] { if (auto self = weak.lock()) self->stop(); }).value();
        shell.show().value();
        auto self = shared_from_this();
        notes->ready().thenOn(scheduler, [self, smoke](const Result<void>& result) {
            if (!result.hasValue() || !self->page->navigate().hasValue()) { self->fail("page activation failed"); return; }
            if (smoke) QTimer::singleShot(50, QCoreApplication::instance(), [self] {
                // 通过真实 QWidget::close 触发宿主的异步退出入口。
                for (auto window : QApplication::topLevelWidgets()) if (window->windowTitle() == QString::fromUtf8("Cordis 插件平台")) {
                    if (!window->grab().save(QCoreApplication::applicationDirPath() + "/ela-smoke.png")) { self->fail("preview capture failed"); return; }
                    window->close(); return;
                }
                self->fail("Ela window missing");
            });
        });
    }
};
} // namespace

/** @brief 默认交互运行，--smoke 通过真实关闭事件自动验证完整退出。 */
int main(int argc, char** argv)
{
    try {
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        const bool smoke = argc == 2 && std::string(argv[1]) == "--smoke";
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
