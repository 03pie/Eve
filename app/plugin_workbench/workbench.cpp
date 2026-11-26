#include "workbench.hpp"
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWidget>
#include <cordis/loader/qt_store.hpp>
#include <cordis/module/qt_manifest.hpp>
#include <cordis/module/qt_module.hpp>

namespace cordis::workbench {
namespace {
template<class T> T checked(Result<T> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
void checked(Result<void> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
}

std::string initialConfiguration(const std::string& plugin, const QString& workspace)
{
    const QJsonObject config{{"dataRoot", QDir(workspace).absoluteFilePath("library")}};
    const QJsonObject instance{{"id", "main"}, {"plugin", QString::fromStdString(plugin)},
                               {"module", "extension"}, {"config", config}};
    const QJsonObject modules{{"extension", QJsonObject{{"manifest", "module.json"}}}};
    const QJsonObject document{{"formatVersion", 1}, {"modules", modules}, {"plugins", QJsonArray{instance}}};
    return QJsonDocument(document).toJson(QJsonDocument::Compact).toStdString();
}
Result<void> validateConfiguration(const std::string& text)
{
    const auto value = QJsonDocument::fromJson(QByteArray::fromStdString(text));
    if (!value.isObject())
        return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "External UI plugin config must be an object."));
    const auto root = value.object().value("dataRoot");
    if (!root.isUndefined() && (!root.isString() || !QDir::isAbsolutePath(root.toString())))
        return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "Plugin dataRoot must be an absolute directory."));
    return Result<void>::success();
}

const char* preflightDiagnostic(const ErrorInfo& error)
{
    if (error.phase == "module.manifest.read")
        return "QT_MANIFEST_READ_FAILED: 无法读取或完整校验模块清单。";
    if (error.code == ErrorCode::AbiMismatch)
        return "QT_MODULE_INCOMPATIBLE: 模块预检发现 ABI、加载器或工具链不兼容。";
    if (error.phase == "module.manifest")
        return "QT_MANIFEST_INVALID: 模块清单格式或字段校验失败。";
    return "QT_MODULE_PREFLIGHT_FAILED: 模块产物或元数据未通过预检。";
}
} // namespace

ModulePreflightError::ModulePreflightError(const ErrorInfo& error)
    : std::runtime_error(error.message), diagnostic_(preflightDiagnostic(error))
{
}

Workbench::Workbench()
    : scheduler(std::make_shared<qt::QtScheduler>()), kernel(std::make_shared<Kernel>(scheduler)),
      shell(scheduler, QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                           .filePath("preferences/workbench.json").toUtf8().toStdString())
{
}

void Workbench::open(const QString& manifestPath, const QString& workspaceDir)
{
    using namespace loader;
    checked(shell.startOutputArchive(workspaceDir.toStdString(), CORDIS_PRODUCT_VERSION));
    const QFileInfo source(manifestPath);
    if (source.fileName() != "module.json" || session)
        throw std::runtime_error("Select a module.json manifest for a new workbench.");
    const auto root = source.absolutePath().toUtf8().toStdString();
    const auto inspected = module::inspectQtModule(root, "module.json");
    if (!inspected.hasValue())
        throw ModulePreflightError(inspected.error());
    const auto& manifest = inspected.value().artifact.manifest();
    if (manifest.plugins.size() != 1 || manifest.loader != module::ModuleLoader::QtUi ||
        manifest.unloadPolicy != module::ModuleUnloadPolicy::Resident)
        throw std::runtime_error("This workbench accepts a resident Qt UI module declaring one plugin.");
    const auto id = manifest.plugins.front();
    const QDir directory(workspaceDir);
    if (!directory.mkpath("."))
        throw std::runtime_error("Cannot create the selected workspace directory.");
    const auto file = directory.absoluteFilePath("session.yaml");
    auto disk = checked(readConfigurationFile(file));
    initial_ = disk.version.exists ? disk.text : initialConfiguration(id, directory.absolutePath());
    const auto generation = checked(module::loadQtModule(root, "module.json")).generation;
    auto runtime = checked(module::qtRuntime(generation, {id,
        {ServiceRequirement::of<ui::UiPagesService>({"ui.pages"}),
         ServiceRequirement::of<ui::CommandService>({"ui.commands"}),
         ServiceRequirement::of<ui::OutputService>({"ui.output"}),
         ServiceRequirement::of<std::shared_ptr<qt::QtScheduler>>({"qt.scheduler"})}, {}}, scheduler));
    const auto registered = checked(kernel->registerRuntime(id, runtime));
    ConfiguredPlugin plugin{{id, {{"ui.pages"}, {"ui.commands"}, {"ui.output"}, {"qt.scheduler"}},
                             validateConfiguration, {"extension", "module.json"}},
                            runtime, {}, {}};
    auto definitions = checked(ConfigurationDefinitions::create({std::move(plugin)}));
    checked(definitions->validate(checked(parseConfiguration(initial_))));
    auto driver = KernelReconcileDriver::configured(scheduler, kernel, definitions);
    session = std::make_shared<ConfigurationSession>(scheduler, driver, std::make_shared<QtConfigurationWriter>(file),
                                                     std::move(disk));
    management_ = ui::createConfigurationManagement(session, scheduler);
    checked(shell.management()->attach(kernel));
    auto configuration = checked(kernel->registerPlugin("host.configuration", [this](Context ctx) {
        auto page = shell.pages()->add(ctx, {"configuration", "工作区配置", "host.configuration",
            [this] {
                auto widget = ui::createConfigurationPage(session, initial_);
                ui::bindConfigurationFeedback(widget.get(),
                    [this](std::string message, ui::OutputLevel level) {
                        auto result = shell.writeOutput(std::move(message), level);
                        return result.hasValue() ? result.value() : std::uint64_t{};
                    },
                    [this](std::uint64_t event, QWidget* origin) { (void)shell.showOutputEvent(event, origin); });
                return widget;
            }, ui::PagePresentation::Dialog});
        if (!page.hasValue())
            return failedTask(page.error());
        configPage_.emplace(std::move(page).value());
        return configPage_->ready();
    }));
    checked(kernel->mount(configuration));
    checked(shell.management()->attachConfiguration(management_, {registered.id(), configuration.id()}));
    checked(shell.onConfigurationRequested([this] {
        return configPage_ ? configPage_->navigate() :
            Result<void>::failure(makeError(ErrorCode::ContextClosing, "Configuration page unavailable."));
    }));
    checked(shell.onModulesRequested([this] { return shell.showManagement(); }));
}

void Workbench::publishUi()
{
    if (published_ || closing_)
        throw std::logic_error("UI service publication is already active or closing.");
    const auto publish = [this](const char* name, const auto& service) {
        providers_.push_back(checked(kernel->services().stage({name}, service)));
        checked(providers_.back().publish());
    };
    publish("ui.pages", shell.pages());
    publish("ui.commands", shell.commands());
    publish("ui.output", shell.output());
    publish("ui.notifications", shell.notifications());
    publish("qt.scheduler", std::make_shared<std::shared_ptr<qt::QtScheduler>>(scheduler));
    published_ = true;
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
    if (!session) {
        closeKernel();
        return;
    }
    session->close().thenOn(scheduler, [this](const Result<void>& result) {
        if (result.hasValue()) closeKernel();
        else closed_.reject(result.error());
    });
}

void Workbench::closeKernel()
{
    kernel->close().thenOn(scheduler, [this](const Result<CleanupReport>& result) {
        if (!result.hasValue()) closed_.reject(result.error());
        else if (!result.value().quiescent)
            closed_.reject(makeError(ErrorCode::CleanupFailed, "Plugin instances did not finish cleanup."));
        else {
            (void)shell.writeOutput("WORKBENCH_CLEANUP_COMPLETE: 插件与内核资源已清理。");
            closeShell();
        }
    });
}

void Workbench::closeShell()
{
    shell.close().thenOn(scheduler, [this](const Result<void>& result) {
        if (!result.hasValue()) {
            closed_.reject(result.error());
            return;
        }
        for (auto& provider : providers_) {
            const auto retired = provider.retire();
            if (!retired.hasValue()) { closed_.reject(retired.error()); return; }
            const auto released = provider.release();
            if (!released.hasValue()) { closed_.reject(released.error()); return; }
        }
        providers_.clear();
        configPage_.reset();
        management_.reset();
        session.reset();
        kernel.reset();
        closed_.resolve();
    });
}
} // namespace cordis::workbench
