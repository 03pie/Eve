/** @file config_workbench.cpp
 * @brief 同一 Ela 工作台内编辑、运行和保存配置；示例只通过会话管理配置实例。
 */
#include <ElaText.h>
#include <ElaTheme.h>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/qt_watcher.hpp>
#include <cordis/ui/configuration.hpp>
#include <cordis/ui/ela_shell.hpp>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
using namespace cordis;
using namespace cordis::loader;
using namespace cordis::ui;
namespace {
/** @brief 宿主保留会话，页面和 Runtime 只借用宿主，退出顺序不可逆转。 */
struct Workbench : std::enable_shared_from_this<Workbench> {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ElaShell shell{scheduler};
    /** @brief 界面偏好独立于配置 Session；smoke 使用空路径，不写本机设置。 */
    explicit Workbench(const std::string& preferences) : shell(scheduler, preferences) {}
    std::shared_ptr<ConfigurationSession> session;
    std::shared_ptr<QtConfigurationIo> io;
    std::shared_ptr<QtConfigurationWatcher> watcher;
    std::shared_ptr<IConfigurationManagement> configurationManagement;
    std::optional<PageHandle> configurationPage;
    QPointer<QWidget> configurationWidget;
    bool closing = false, finished = false;
    int smokeStage = 0;
    /** @brief 出错返回非零，不能伪造已清理或已保存。 */
    void fail(const char* message)
    {
        std::cerr << message << '\n';
        QCoreApplication::exit(EXIT_FAILURE);
    }
    /** @brief 关闭前先保存；冲突保留可编辑会话及窗口，由用户显式解决。 */
    void flushBeforeClose()
    {
        auto snapshot = session->snapshot().value();
        if (snapshot.store.dirty) {
            auto saved = session->save(snapshot.coordinator.desiredRevision);
            if (!saved.hasValue()) {
                keepOpen();
                return;
            }
            auto self = shared_from_this();
            saved.value().thenOn(scheduler, [self](const Result<SaveReport>& result) {
                if (!result.hasValue() || (result.value().outcome != SaveOutcome::Written &&
                                           result.value().outcome != SaveOutcome::Unchanged)) {
                    self->keepOpen();
                    return;
                }
                self->flushBeforeClose();
            });
            return;
        }
        auto self = shared_from_this();
        watcher->close().thenOn(scheduler, [self](const Result<void>& watched) {
            if (!watched.hasValue()) {
                self->fail("file watcher did not drain");
                return;
            }
            self->finishSession();
        });
    }
    /** @brief 会话的最终保存完成后才能停止后台 I/O；随后继续 Kernel/UI 退出链。 */
    void finishSession()
    {
        auto self = shared_from_this();
        session->close().thenOn(scheduler, [self](const Result<void>& saved) {
            if (!saved.hasValue()) {
                self->fail("configuration shutdown failed");
                return;
            }
            self->io->close().thenOn(self->scheduler, [self](const Result<void>& joined) {
                if (!joined.hasValue()) {
                    self->fail("file I/O did not join");
                    return;
                }
                self->finishKernel();
            });
        });
    }
    /** @brief 文件线程 join 后排空其余根及窗口，最后关闭调度器。 */
    void finishKernel()
    {
        auto self = shared_from_this();
        kernel->close().thenOn(scheduler, [self](const Result<CleanupReport>& core) {
            if (!core.hasValue() || !core.value().quiescent) {
                self->fail("kernel did not quiesce");
                return;
            }
            self->shell.close().thenOn(self->scheduler, [self](const Result<void>& ui) {
                if (!ui.hasValue() || !self->scheduler->close().hasValue()) {
                    self->fail("workbench cleanup failed");
                    return;
                }
                self->finished = true;
                QCoreApplication::exit(EXIT_SUCCESS);
            });
        });
    }
    /** @brief 恢复关闭按钮，配置页展示原始磁盘冲突而不自动覆盖。 */
    void keepOpen()
    {
        (void)shell.writeOutput("配置尚未保存，已保留工作台供处理冲突或错误。", OutputLevel::Warning);
        closing = false;
        shell.cancelCloseRequest().value();
        if (configurationPage)
            configurationPage->navigate().value();
    }
    /** @brief 多次关闭请求不能并发执行退出链。 */
    void stop()
    {
        if (!closing) {
            (void)shell.writeOutput("正在保存配置并关闭工作台。");
            closing = true;
            flushBeforeClose();
        }
    }
    /** @brief 真正文件路径绑定到 Store，载入原文只进入编辑区，不自动接受或写盘。 */
    void start(const QString& path, bool smoke)
    {
        io = std::make_shared<QtConfigurationIo>(scheduler, path);
        auto self = shared_from_this();
        io->read().thenOn(scheduler, [self, smoke](const Result<ConfigurationDiskObservation>& result) {
            if (!result.hasValue()) {
                self->fail("configuration read failed");
                return;
            }
            self->startPrepared(smoke, result.value());
        });
    }
    /** @brief 异步读取后的 owner 装配不自动接受目标，监听与文件后端由宿主保活。 */
    void startPrepared(bool smoke, const ConfigurationDiskObservation& initialDisk)
    {
        (void)shell.writeOutput(initialDisk.disk.version.exists ? "已读取磁盘配置，正在装配配置会话。" : "配置文件尚不存在，已建立默认配置会话。");
        auto weak = weak_from_this();
        auto notes =
            kernel
                ->registerPlugin("demo.notes",
                                 [weak](Context context) {
                                     auto self = weak.lock();
                                     if (!self)
                                         return failedTask(makeError(ErrorCode::ContextClosing, "host expired"));
                                     auto output = self->shell.output();
                                     auto written = output->write(context, "demo.notes", "笔记插件已启动。");
                                     if (!written.hasValue()) return failedTask(written.error());
                                     self->shell.commands()->add(context,
                                         {"notes.showConfiguration", "笔记：输出当前配置",
                                          [output, context] {
                                              auto written = output->write(context, "demo.notes", "当前配置：" + context.config());
                                              return written.hasValue() ? completedTask() : failedTask(written.error());
                                          }, {}}).value();
                                     auto page =
                                         self->shell.pages()
                                             ->add(context,
                                                   {"notes", "笔记示例", "notes",
                                                    [text = context.config()] {
                                                        auto widget = std::make_unique<QWidget>();
                                                        auto layout = new QVBoxLayout(widget.get());
                                                        auto title = new ElaText(
                                                            QString::fromUtf8("配置拥有的笔记插件"), 24, widget.get());
                                                        layout->addWidget(title);
                                                        auto value = new ElaText(QString::fromUtf8("当前配置：\n") +
                                                                                     QString::fromStdString(text),
                                                                                 widget.get());
                                                        value->setWordWrap(true);
                                                        value->setTextPixelSize(14);
                                                        value->setTextFormat(Qt::PlainText);
                                                        layout->addWidget(value);
                                                        layout->addStretch();
                                                        return widget;
                                                    }})
                                             .value();
                                     return page.ready();
                                 })
                .value();
        const auto& baseline = initialDisk.disk;
        watcher = std::make_shared<QtConfigurationWatcher>(scheduler, io, initialDisk);
        const auto initial = baseline.version.exists
                                 ? baseline.text
                                 : std::string("formatVersion: 1\nplugins:\n  - id: notes\n    plugin: demo.notes\n    "
                                               "config: {title: 我的笔记}\n");
        auto definitions =
            ConfigurationDefinitions::create(
                {{{"demo.notes", {}, [](const std::string&) { return Result<void>::success(); }}, notes}})
                .value();
        auto driver = KernelReconcileDriver::configured(scheduler, kernel, definitions);
        session = std::make_shared<ConfigurationSession>(
            scheduler, driver,
            io, baseline);
        auto host =
            kernel
                ->registerPlugin("host.configuration",
                                 [weak, initial](Context context) {
                                     auto self = weak.lock();
                                     if (!self)
                                         return failedTask(makeError(ErrorCode::ContextClosing, "host expired"));
                                     auto page = self->shell.pages()
                                                     ->add(context, {"configuration", "配置管理", "configuration",
                                                                     [weak, initial] {
                                                                         auto state = weak.lock();
                                                                         if (!state)
                                                                             return std::unique_ptr<QWidget>{};
                                                                         auto widget = createConfigurationPage(
                                                                             state->session, initial, state->watcher);
                                                                         state->configurationWidget = widget.get();
                                                                         return widget;
                                                                     },
                                                                     PagePresentation::Dialog})
                                                     .value();
                                     self->configurationPage.emplace(page);
                                     return page.ready();
                                 })
                .value();
        // 统一管理页/Home；配置及内部宿主定义不能绕过目标走手动实例接口。
        configurationManagement = createConfigurationManagement(session, scheduler);
        shell.management()->attach(kernel).value();
        shell.management()->attachConfiguration(configurationManagement, {notes.id(), host.id()}).value();
        shell
            .onConfigurationRequested([weak] {
                auto state = weak.lock();
                return state && state->configurationPage
                           ? state->configurationPage->navigate()
                           : Result<void>::failure(
                                 makeError(ErrorCode::ContextClosing, "configuration editor unavailable"));
            })
            .value();
        auto instance = kernel->mount(host).value();
        auto self = shared_from_this();
        shell
            .onCloseRequested([weak] {
                if (auto value = weak.lock())
                    value->stop();
            })
            .value();
        instance.ready().thenOn(scheduler, [self, smoke](const Result<void>& ready) {
            if (!ready.hasValue()) {
                self->fail("configuration page failed");
                return;
            }
            self->shell.show().value();
            if (smoke)
                self->configurationPage->navigate().value();
            if (smoke) {
                auto timer = new QTimer(self->configurationWidget);
                timer->setInterval(10);
                QObject::connect(timer, &QTimer::timeout, self->configurationWidget, [self, timer] {
                    if (self->smokeStage == 0) {
                        self->configurationWidget->findChild<QPushButton*>("CordisConfigPreview")->click();
                        self->configurationWidget->findChild<QPushButton*>("CordisConfigApply")->click();
                        ++self->smokeStage;
                    } else if (self->smokeStage == 1) {
                        auto state = self->session->snapshot().value();
                        if (!state.latestPlan)
                            return;
                        if (state.latestPlan->outcome != PlanOutcome::Applied) {
                            self->fail("target not applied");
                            return;
                        }
                        self->configurationWidget->findChild<QPushButton*>("CordisConfigSave")->click();
                        ++self->smokeStage;
                    } else if (!self->session->snapshot().value().store.dirty) {
                        const auto store = self->session->snapshot().value().store;
                        auto summary = self->configurationWidget->findChild<QLabel*>("CordisConfigSaveStatus");
                        if (store.persistedRevision != 1 || store.writing || !summary ||
                            !summary->text().contains(QString::fromUtf8("已保存")))
                            return;
                        timer->stop();
                        auto name = eTheme->getThemeMode() == ElaThemeType::Dark ? "/config-workbench-dark.png"
                                                                                 : "/config-workbench-light.png";
                        if (!self->configurationWidget->window()->grab().save(QCoreApplication::applicationDirPath() +
                                                                              name)) {
                            self->fail("configuration screenshot failed");
                            return;
                        }
                        self->configurationWidget->window()->parentWidget()->close();
                    }
                });
                timer->start();
            }
        });
    }
};
} // namespace
/** @brief 默认持久路径在本机应用数据目录；smoke 始终使用独立临时文件。 */
int main(int argc, char** argv)
{
    try {
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        app.setApplicationName("CordisConfigWorkbench");
        bool smoke = false, dark = false;
        QString path;
        for (int i = 1; i < argc; ++i) {
            const std::string value = argv[i];
            if (value == "--smoke" || value == "--smoke-dark") {
                smoke = true;
                dark = value == "--smoke-dark";
            } else if (value == "--file" && i + 1 < argc)
                path = QString::fromLocal8Bit(argv[++i]);
            else
                throw std::invalid_argument("use --file <path>, --smoke or --smoke-dark");
        }
        QTemporaryDir temporary;
        if (smoke) {
            if (!path.isEmpty() || !temporary.isValid())
                throw std::invalid_argument("smoke requires its own temporary file");
            path = temporary.filePath("cordis.yaml");
        }
        if (path.isEmpty()) {
            auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
            if (!QDir().mkpath(directory))
                throw std::runtime_error("cannot create configuration directory");
            path = directory + "/cordis.yaml";
        }
        path = QFileInfo(path).absoluteFilePath();
        eTheme->setThemeMode(dark ? ElaThemeType::Dark : ElaThemeType::Light);
#ifdef Q_OS_WIN
        if (smoke && QApplication::platformName() == "offscreen")
            QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc");
#endif
        const auto preferences =
            smoke ? std::string{}
                  : (QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ui-preferences.json")
                        .toUtf8()
                        .toStdString();
        auto state = std::make_shared<Workbench>(preferences);
        state->start(path, smoke);
        if (smoke)
            QTimer::singleShot(15000, &app, [] { QCoreApplication::exit(EXIT_FAILURE); });
        const auto result = app.exec();
        return result == EXIT_SUCCESS && state->finished ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
