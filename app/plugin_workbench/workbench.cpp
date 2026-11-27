/** @file workbench.cpp
 * @brief 装配工作区的模块、配置会话与 UI 服务，协调保存和关闭。
 */
#include "workbench.hpp"
#include "workspace_extensions.hpp"
#include "startup_result.hpp"
#include "qt_system_p.hpp"
#include "runtime_log_archive_p.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>
#include <stdexcept>
#include <eve/loader/kernel_driver.hpp>
#include <eve/loader/qt_store.hpp>
#include <eve/loader/yaml.hpp>
#include <eve/module/qt_manifest.hpp>
#include <eve/module/qt_module.hpp>

namespace eve::workbench {
namespace {
/** @brief 首次选择外部模块时创建主实例，插件自行解释配置对象中的字段。 */
std::string initialConfiguration(const std::string& plugin)
{
    const auto empty = checked(loader::parseConfiguration(loader::emptyWorkspaceYaml));
    const auto initial = checked(loader::addConfigurationEntry(
        empty, "main", plugin, "{}", {}, {"extension", "module.json"}));
    return initial.normalizedYaml();
}
/** @brief 按预检阶段选择面向用户的诊断，便于从启动日志定位失败原因。 */
ErrorInfo preflightDiagnostic(ErrorInfo error)
{
    if (error.phase == "module.manifest.read")
        error.phase = "QT_MANIFEST_READ_FAILED";
    else if (error.code == ErrorCode::AbiMismatch)
        error.phase = "QT_MODULE_INCOMPATIBLE";
    else if (error.phase == "module.manifest")
        error.phase = "QT_MANIFEST_INVALID";
    else
        error.phase = "QT_MODULE_PREFLIGHT_FAILED";
    return error;
}

/** @brief 仅系统清单装配使用此分类。保留底层错误用于 stderr，持久日志使用固定说明。 */
ErrorInfo desktopDiagnostic(ErrorInfo error)
{
    const QDir bundled(QDir(QCoreApplication::applicationDirPath()).filePath("system/desktop"));
    if (!QFileInfo::exists(bundled.filePath("module.json")))
        error.phase = "WORKBENCH_DESKTOP_MANIFEST_MISSING";
    else if (error.phase == "workbench.desktop.identity")
        error.phase = "WORKBENCH_DESKTOP_IDENTITY_INVALID";
    else if (error.phase == "module.hash" && !QFileInfo::exists(bundled.filePath("eve_desktop.dll")))
        error.phase = "WORKBENCH_DESKTOP_LIBRARY_MISSING";
    else if (error.phase == "module.hash")
        error.phase = "WORKBENCH_DESKTOP_HASH_INVALID";
    else if (error.code == ErrorCode::AbiMismatch || error.phase == "module.image")
        error.phase = "WORKBENCH_DESKTOP_INCOMPATIBLE";
    else
        error.phase = "WORKBENCH_DESKTOP_LOAD_FAILED";
    return error;
}

/** @brief 在创建运行代之前核对清单类型、驻留策略和插件数目。 */
extensions::Package inspectConfiguredModule(const QString& path)
{
    const QFileInfo source(path);
    if (source.fileName() != "module.json")
        throw std::runtime_error("Select a module.json manifest for --module.");
    auto root = source.absolutePath().toUtf8().toStdString();
    const auto inspected = module::inspectQtModule(root, "module.json");
    if (!inspected.hasValue())
        throw StartupError(preflightDiagnostic(inspected.error()));
    const auto& manifest = inspected.value().artifact.manifest();
    if (manifest.plugins.size() != 1 || manifest.loader != module::ModuleLoader::QtUi ||
        manifest.unloadPolicy != module::ModuleUnloadPolicy::Resident)
        throw std::runtime_error("This workbench accepts a resident Qt UI module declaring one plugin.");
    if (manifest.moduleId == desktopModuleId || manifest.plugins.front() == desktopId ||
        manifest.plugins.front() == "host.configuration")
        throw std::runtime_error("The selected business module uses a reserved system identity.");
    extensions::Package package;
    package.moduleId = QString::fromStdString(manifest.moduleId);
    package.pluginId = QString::fromStdString(manifest.plugins.front());
    package.version = QString::fromStdString(manifest.moduleVersion);
    package.sha256 = QString::fromStdString(manifest.sha256);
    package.manifestPath = source.absoluteFilePath();
    package.defaultConfig = "{}";
    package.kind = "business";
    return package;
}
} // namespace

QString workbenchDataFile(const QString& applicationData, const QString& relativeFile)
{
    const QDir data(applicationData);
    const auto current = data.filePath(relativeFile);
    if (QFileInfo::exists(current) || data.dirName() != QStringLiteral("EvePluginWorkbench"))
        return current;
    // 按文件选择，避免首次保存某项偏好后遮住旧工作区。其它嵌入宿主仍使用自己的数据目录。
    const auto previous = QDir::cleanPath(data.filePath(QStringLiteral("../CordisPluginWorkbench/") + relativeFile));
    return QFileInfo(previous).isFile() ? previous : current;
}

Workbench::~Workbench() = default;

Workbench::Workbench(bool safeDesktop)
    : scheduler(std::make_shared<qt::QtScheduler>()), kernel(std::make_shared<Kernel>(scheduler)),
      bootstrap_(std::make_shared<DesktopBootstrap>()), selection_(extensions::Catalog::defaultRoot()),
      safeDesktop_(safeDesktop), extensions_(std::make_unique<WorkspaceExtensions>(*this))
{
    bootstrap_->extensions = extensions_->controls();
    bootstrap_->preferencesFile = workbenchDataFile(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
        QStringLiteral("preferences/workbench.json")).toUtf8().toStdString();
}

Result<std::uint64_t> Workbench::writeOutput(std::string text, ui::OutputLevel level,
                                             ui::OutputDiagnostic diagnostic) const
{
    if (!bootstrap_->archive)
        return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "No open run archive."));
    return bootstrap_->archive->writePlatform(std::move(text), level, std::move(diagnostic));
}

Task<void> Workbench::open(const std::optional<QString>& manifestPath, const QString& workspaceDir)
{
    if (bootstrap_->archive || closing_)
        return failedTask(makeError(ErrorCode::InvalidArgument, "This workbench cannot open another workspace."));
    Promise<void> ready;
    try {
        bootstrap_->archive = std::make_shared<ui::detail::RuntimeLogArchive>(workspaceDir, EVE_PRODUCT_VERSION);
        const auto publish = [this](const char* key, const auto& service) {
            providers_.push_back(checked(kernel->services().stage({key}, service)));
            checked(providers_.back().publish());
        };
        publish(schedulerService, std::make_shared<std::shared_ptr<qt::QtScheduler>>(scheduler));
        publish(desktopBootstrapKey, bootstrap_);
        const auto loaded = module::detail::loadSystemDesktopModule();
        if (!loaded.hasValue()) throw StartupError(desktopDiagnostic(loaded.error()));
        auto negotiated = module::qtRuntime(loaded.value().generation, desktopContract(), scheduler);
        if (!negotiated.hasValue()) throw StartupError(desktopDiagnostic(negotiated.error()));
        auto runtime = checked(kernel->registerRuntime(desktopId, negotiated.value()));
        desktopRuntime_ = runtime.id();
        const auto instance = checked(kernel->mount(runtime));
        instance.ready().thenOn(scheduler, [this, ready, manifestPath, workspaceDir,
                generation = loaded.value().generation](const Result<void>& active) {
            if (!active.hasValue()) {
                auto error = active.error();
                error.phase = "WORKBENCH_DESKTOP_ACTIVATION_FAILED";
                ready.reject(std::move(error));
                return;
            }
            if (closing_ || !desktop()) {
                ready.reject(makeError(ErrorCode::ContextClosing, "Desktop unavailable while opening workspace."));
                return;
            }
            try {
                checked(writeOutput(std::string(desktopId) + " · 模块 " + desktopModuleId
                    + " · 加载代 " + std::to_string(generation.value()), ui::OutputLevel::Info,
                    {{}, "WORKBENCH_DESKTOP_ACTIVE"}));
                mountSelectedDesktop().thenOn(scheduler, [this, ready, manifestPath, workspaceDir](const Result<void>& mounted) {
                    if (!mounted.hasValue()) { ready.reject(mounted.error()); return; }
                    try {
                        openWorkspace(manifestPath, workspaceDir).thenOn(scheduler, [ready](const Result<void>& outcome) {
                            if (outcome.hasValue()) ready.resolve();
                            else ready.reject(outcome.error());
                        });
                    } catch (const StartupError& error) { ready.reject(error.detail); }
                      catch (const std::exception& error) { ready.reject(makeError(ErrorCode::ConfigInvalid, error.what())); }
                });
            } catch (const StartupError& error) { ready.reject(error.detail); }
              catch (const std::exception& error) { ready.reject(makeError(ErrorCode::ConfigInvalid, error.what())); }
        });
    } catch (const StartupError& error) { ready.reject(error.detail); }
      catch (const std::exception& error) { ready.reject(makeError(ErrorCode::ModuleLoad, error.what())); }
    return ready.task();
}

Task<void> Workbench::mountSelectedDesktop()
{
    auto chosen = selection_.begin(safeDesktop_);
    if (!chosen.hasValue()) return failedTask(chosen.error());
    if (chosen.value() == desktopModuleId) return completedTask();
    Promise<void> ready;
    try {
        const auto installed = checked(extensions_->catalog().installed());
        const auto found = std::find_if(installed.begin(), installed.end(), [&](const auto& item) {
            return item.moduleId == chosen.value() && item.kind == "desktop";
        });
        if (found == installed.end()) throw StartupError(makeError(ErrorCode::DependencyMissing, "Selected desktop is missing."));
        auto runtime = checked(registerDesktopLayout(*kernel, scheduler, found->manifestPath, found->moduleId,
                                                     found->pluginId, found->sha256));
        bootstrap_->layoutRuntime = runtime.id();
        extensions_->activeDesktopHash_ = found->sha256;
        layout_ = checked(kernel->mount(runtime));
        layout_->ready().thenOn(scheduler, [this, ready](const Result<void>& result) {
            if (!result.hasValue()) ready.reject(selectedDesktopFailure(result.error()));
            else if (!desktop() || !desktop()->hasExternalLayout())
                ready.reject(selectedDesktopFailure(makeError(ErrorCode::ActivationFailed, "Selected desktop did not attach its window.")));
            else ready.resolve();
        });
    } catch (const StartupError& error) { ready.reject(selectedDesktopFailure(error.detail)); }
      catch (const std::exception& error) { ready.reject(selectedDesktopFailure(makeError(ErrorCode::ActivationFailed, error.what()))); }
    return ready.task();
}

Task<void> Workbench::openWorkspace(const std::optional<QString>& manifestPath, const QString& workspaceDir)
{
    using namespace loader;
    const auto selected = manifestPath ? std::optional{inspectConfiguredModule(*manifestPath)} : std::nullopt;
    const QDir directory(workspaceDir);
    if (!directory.mkpath("."))
        throw std::runtime_error("Cannot create the selected workspace directory.");
    const auto file = directory.absoluteFilePath("session.yaml");
    auto disk = checked(readConfigurationFile(file));
    if (disk.version.exists)
        initial_ = disk.text;
    else if (selected)
        initial_ = initialConfiguration(selected->pluginId.toStdString());
    else
        initial_ = emptyWorkspaceYaml;

    const auto target = checked(parseConfiguration(initial_));
    for (const auto& entry : target.structure().entries()) {
        if (entry.second.plugin == desktopId || entry.second.plugin == "host.configuration") {
            auto error = makeError(ErrorCode::ConfigInvalid, "System plugins cannot be managed through session.yaml.");
            error.phase = "WORKBENCH_RESERVED_PLUGIN";
            throw StartupError(std::move(error));
        }
    }
    auto protectedRuntimes = extensions_->prepare(target, selected);
    protectedRuntimes.push_back(desktopRuntime_);
    if (layout_) protectedRuntimes.push_back(bootstrap_->layoutRuntime);
    auto driver = extensions_->driver();
    checked(driver->configurationDefinitions()->validate(target));
    session = std::make_shared<ConfigurationSession>(scheduler, driver, std::make_shared<QtConfigurationWriter>(file),
                                                     std::move(disk));
    auto configuration = checked(kernel->registerPlugin("host.configuration", [this](Context ctx) {
        auto pages = ctx.consume<ui::UiPagesService>({pagesService});
        if (!pages.hasValue()) return failedTask(pages.error());
        auto view = desktop();
        if (!view) return failedTask(makeError(ErrorCode::ContextClosing, "Desktop unavailable."));
        return pages.value().invoke([view, ctx](ui::UiPagesService& service) {
            return view->addConfigurationPage(ctx, service);
        });
    }, {ServiceRequirement::of<ui::UiPagesService>({pagesService})}));
    protectedRuntimes.push_back(configuration.id());
    checked(desktop()->bindWorkspace(kernel, session, initial_, std::move(protectedRuntimes)));
    return checked(kernel->mount(configuration)).ready();
}

loader::AcceptedTarget Workbench::applyInitial()
{
    return checked(session->apply(checked(session->preview(initial_))));
}

Task<void> Workbench::close()
{
    if (closing_)
        return closed_.task();
    closing_ = true;
    closed_ = Promise<void>{};
    if (session) {
        const auto state = session->snapshot();
        if (!state.hasValue()) {
            closing_ = false;
            closed_.reject(state.error());
            return closed_.task();
        }
        if (state.value().store.dirty) {
            auto saved = session->save(state.value().coordinator.desiredRevision);
            if (!saved.hasValue()) {
                closing_ = false;
                closed_.reject(saved.error());
            } else {
                saved.value().thenOn(scheduler, [this](const Result<loader::SaveReport>& result) {
                    if (result.hasValue() && (result.value().outcome == loader::SaveOutcome::Written ||
                                              result.value().outcome == loader::SaveOutcome::Unchanged)) {
                        closeSession();
                        return;
                    }
                    closing_ = false;
                    closed_.reject(!result.hasValue() ? result.error() : result.value().error.value_or(
                        makeError(ErrorCode::ConfigConflict, "Workspace configuration could not be saved.")));
                });
            }
            return closed_.task();
        }
    }
    closeSession();
    return closed_.task();
}

void Workbench::closeSession()
{
    if (!session) { closeConsumers(); return; }
    session->close().thenOn(scheduler, [this](const Result<void>& result) {
        if (result.hasValue()) closeConsumers();
        else failCleanup(result.error(), "WORKBENCH_SESSION_CLEANUP_FAILED");
    });
}

void Workbench::failCleanup(ErrorInfo error, const char* phase)
{
    if (bootstrap_->archive) {
        ui::OutputDiagnostic diagnostic{{}, phase};
        if (error.operation.valid()) diagnostic.operation = error.operation;
        (void)writeOutput(std::string(phase) + " · 清理未完成", ui::OutputLevel::Error, std::move(diagnostic));
        bootstrap_->archive->finish(false);
    }
    closed_.reject(std::move(error));
}

void Workbench::closeConsumers()
{
    // Session.close drains managed entries. Also drain configuration and any independent
    // host-mounted consumers before the desktop begins closing its widget tree.
    auto snapshot = kernel->snapshot();
    if (!snapshot.hasValue()) { failCleanup(snapshot.error(), "WORKBENCH_CONSUMER_CLEANUP_FAILED"); return; }
    std::vector<Instance> consumers;
    for (const auto& row : snapshot.value().instances) {
        if (row.runtime == desktopRuntime_ || (layout_ && row.runtime == bootstrap_->layoutRuntime)) continue;
        auto found = kernel->findInstance(row.instance);
        if (!found.hasValue()) { failCleanup(found.error(), "WORKBENCH_CONSUMER_CLEANUP_FAILED"); return; }
        consumers.push_back(std::move(found).value());
    }
    if (consumers.empty()) { closeLayout(); return; }
    struct Drain { std::size_t remaining; std::optional<ErrorInfo> failure; };
    auto drain = std::make_shared<Drain>(Drain{consumers.size(), {}});
    for (const auto& consumer : consumers) {
        consumer.stop().thenOn(scheduler, [this, drain](const Result<CleanupReport>& result) {
            if (!result.hasValue()) drain->failure = result.error();
            else if (!result.value().quiescent)
                drain->failure = makeError(ErrorCode::CleanupFailed, "Desktop consumer did not finish cleanup.");
            if (--drain->remaining == 0) {
                if (drain->failure) failCleanup(*drain->failure, "WORKBENCH_CONSUMER_CLEANUP_FAILED");
                else closeLayout();
            }
        });
    }
}

void Workbench::closeLayout()
{
    if (!layout_) { closeKernel(); return; }
    layout_->stop().thenOn(scheduler, [this](const Result<CleanupReport>& result) {
        if (!result.hasValue() || !result.value().quiescent) {
            failCleanup(result.hasValue() ? makeError(ErrorCode::CleanupFailed, "Desktop layout did not close.")
                                          : result.error(), "WORKBENCH_LAYOUT_CLEANUP_FAILED");
            return;
        }
        layout_.reset();
        closeKernel();
    });
}

void Workbench::closeKernel()
{
    kernel->close().thenOn(scheduler, [this](const Result<CleanupReport>& result) {
        if (!result.hasValue()) { failCleanup(result.error(), "WORKBENCH_KERNEL_CLEANUP_FAILED"); return; }
        if (!result.value().quiescent) {
            failCleanup(makeError(ErrorCode::CleanupFailed, "Desktop or kernel did not finish cleanup."),
                        "WORKBENCH_KERNEL_CLEANUP_FAILED");
            return;
        }
        for (auto& provider : providers_) {
            auto retired = provider.retire();
            if (!retired.hasValue()) { failCleanup(retired.error(), "WORKBENCH_PROVIDER_CLEANUP_FAILED"); return; }
            auto released = provider.release();
            if (!released.hasValue()) { failCleanup(released.error(), "WORKBENCH_PROVIDER_CLEANUP_FAILED"); return; }
        }
        providers_.clear();
        session.reset();
        kernel.reset();
        if (bootstrap_->archive) {
            auto marker = writeOutput("WORKBENCH_CLEANUP_COMPLETE: 插件与内核资源已清理。", ui::OutputLevel::Info,
                {{}, "WORKBENCH_CLEANUP_COMPLETE"});
            bootstrap_->archive->finish(marker.hasValue());
            if (!marker.hasValue()) { closed_.reject(marker.error()); return; }
        }
        closed_.resolve();
    });
}
} // namespace eve::workbench
