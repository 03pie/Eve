/** @file workbench.cpp
 * @brief 装配工作区的模块、配置会话与 UI 服务，协调保存和关闭。
 */
#include "workbench.hpp"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QWidget>
#include <algorithm>
#include <eve/loader/qt_store.hpp>
#include <eve/loader/yaml.hpp>
#include <eve/module/qt_manifest.hpp>
#include <eve/module/qt_module.hpp>

namespace eve::workbench {
namespace {
constexpr char pagesService[] = "ui.pages";
constexpr char commandsService[] = "ui.commands";
constexpr char outputService[] = "ui.output";
constexpr char schedulerService[] = "qt.scheduler";

/** @brief 装配失败时将诊断交给应用入口，成功时取走已创建的宿主值。 */
template<class T> T checked(Result<T> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
/** @brief 将无返回值的装配失败交给应用入口。 */
void checked(Result<void> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
}

/** @brief 首次选择外部模块时创建主实例，插件自行解释配置对象中的字段。 */
std::string initialConfiguration(const std::string& plugin)
{
    const auto empty = checked(loader::parseConfiguration(loader::emptyWorkspaceYaml));
    const auto initial = checked(loader::addConfigurationEntry(
        empty, "main", plugin, "{}", {}, {"extension", "module.json"}));
    return initial.normalizedYaml();
}
/** @brief Loader 已将值编码为规范 JSON，此处只检查顶层类型，保留插件字段和数字原文。 */
Result<void> validateConfiguration(const std::string& text)
{
    if (text.empty() || text.front() != '{')
        return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "External UI plugin config must be an object."));
    return Result<void>::success();
}

/** @brief 按预检阶段选择面向用户的诊断，便于从启动日志定位失败原因。 */
const char* preflightDiagnostic(const ErrorInfo& error)
{
    if (error.phase == "workbench.module.required")
        return "WORKBENCH_MODULE_REQUIRED: 该工作区引用外部模块。请使用 --module 指定清单，或通过 --workspace 打开另一个工作区。";
    if (error.phase == "module.manifest.read")
        return "QT_MANIFEST_READ_FAILED: 无法读取或完整校验模块清单。";
    if (error.code == ErrorCode::AbiMismatch)
        return "QT_MODULE_INCOMPATIBLE: 模块预检发现 ABI、加载器或工具链不兼容。";
    if (error.phase == "module.manifest")
        return "QT_MANIFEST_INVALID: 模块清单格式或字段校验失败。";
    return "QT_MODULE_PREFLIGHT_FAILED: 模块产物或元数据未通过预检。";
}

/** @brief 预检后交给宿主的实际模块目录和清单声明的唯一插件标识。 */
struct ModuleInput {
    std::string root;
    std::string plugin;
};

/** @brief 在创建运行代之前核对清单类型、驻留策略和插件数目。 */
ModuleInput inspectConfiguredModule(const QString& path)
{
    const QFileInfo source(path);
    if (source.fileName() != "module.json")
        throw std::runtime_error("Select a module.json manifest for --module.");
    auto root = source.absolutePath().toUtf8().toStdString();
    const auto inspected = module::inspectQtModule(root, "module.json");
    if (!inspected.hasValue())
        throw ModulePreflightError(inspected.error());
    const auto& manifest = inspected.value().artifact.manifest();
    if (manifest.plugins.size() != 1 || manifest.loader != module::ModuleLoader::QtUi ||
        manifest.unloadPolicy != module::ModuleUnloadPolicy::Resident)
        throw std::runtime_error("This workbench accepts a resident Qt UI module declaring one plugin.");
    return {std::move(root), manifest.plugins.front()};
}

/** @brief 将同一加载代登记到内核和输出来源目录，配置依赖与 Runtime 共用服务名。 */
loader::ConfiguredPlugin configureModule(const ModuleInput& input, Kernel& kernel, const ui::ElaShell& shell,
                                         const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    const auto generation = checked(module::loadQtModule(input.root, "module.json")).generation;
    auto runtime = checked(module::qtRuntime(generation, {input.plugin,
        {ServiceRequirement::of<ui::UiPagesService>({pagesService}),
         ServiceRequirement::of<ui::CommandService>({commandsService}),
         ServiceRequirement::of<ui::OutputService>({outputService}),
         ServiceRequirement::of<std::shared_ptr<qt::QtScheduler>>({schedulerService})}, {}}, scheduler));
    const auto registered = checked(kernel.registerRuntime(input.plugin, runtime));
    const module::ModuleManager modules;
    const auto origin = checked(modules.runtimeOrigin(registered.id()));
    const auto actual = checked(modules.snapshot());
    const auto loaded = std::find_if(actual.begin(), actual.end(),
                                     [generation](const auto& row) { return row.generation == generation; });
    if (!origin || origin->generation != generation || loaded == actual.end())
        throw std::logic_error("Qt runtime could not be matched to its loaded generation.");
    checked(shell.registerQtOutputRuntime(registered.id(), generation, origin->moduleId,
                                          loaded->manifest.moduleVersion, origin->pluginId));
    return {{input.plugin, {{pagesService}, {commandsService}, {outputService}, {schedulerService}},
             validateConfiguration, {"extension", "module.json"}},
            std::move(runtime), {}, {}};
}
} // namespace

ModulePreflightError::ModulePreflightError(const ErrorInfo& error)
    : std::runtime_error(error.message), diagnostic_(preflightDiagnostic(error))
{
}

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

Workbench::Workbench()
    : scheduler(std::make_shared<qt::QtScheduler>()), kernel(std::make_shared<Kernel>(scheduler)),
      shell(scheduler, workbenchDataFile(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
                                         QStringLiteral("preferences/workbench.json")).toUtf8().toStdString())
{
}

void Workbench::open(const std::optional<QString>& manifestPath, const QString& workspaceDir)
{
    using namespace loader;
    if (session)
        throw std::logic_error("This workbench already owns a workspace.");
    checked(shell.startOutputArchive(workspaceDir.toStdString(), EVE_PRODUCT_VERSION));
    const auto selected = manifestPath ? std::optional{inspectConfiguredModule(*manifestPath)} : std::nullopt;
    const QDir directory(workspaceDir);
    if (!directory.mkpath("."))
        throw std::runtime_error("Cannot create the selected workspace directory.");
    const auto file = directory.absoluteFilePath("session.yaml");
    auto disk = checked(readConfigurationFile(file));
    if (disk.version.exists)
        initial_ = disk.text;
    else if (selected)
        initial_ = initialConfiguration(selected->plugin);
    else
        initial_ = emptyWorkspaceYaml;

    const auto target = checked(parseConfiguration(initial_));
    if (!selected && (!target.structure().modules().empty() || !target.structure().entries().empty())) {
        auto error = makeError(ErrorCode::DependencyMissing,
            "该工作区引用外部模块。请使用 --module 指定清单，或通过 --workspace 打开另一个工作区。");
        error.phase = "workbench.module.required";
        throw ModulePreflightError(error);
    }
    std::vector<RuntimeId> protectedRuntimes;
    std::vector<ConfiguredPlugin> plugins;
    if (selected) {
        plugins.push_back(configureModule(*selected, *kernel, shell, scheduler));
        protectedRuntimes.push_back(plugins.back().runtime.id());
    }
    auto definitions = checked(ConfigurationDefinitions::create(std::move(plugins)));
    checked(definitions->validate(target));
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
                    [this](std::string message, ui::OutputLevel level, ui::OutputDiagnostic diagnostic) {
                        auto result = shell.writeOutput(std::move(message), level, std::move(diagnostic));
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
    protectedRuntimes.push_back(configuration.id());
    checked(shell.management()->attachConfiguration(management_, std::move(protectedRuntimes)));
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
    publish(pagesService, shell.pages());
    publish(commandsService, shell.commands());
    publish(outputService, shell.output());
    publish("ui.notifications", shell.notifications());
    publish(schedulerService, std::make_shared<std::shared_ptr<qt::QtScheduler>>(scheduler));
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
            (void)shell.writeOutput("WORKBENCH_CLEANUP_COMPLETE: 插件与内核资源已清理。", ui::OutputLevel::Info,
                                    {{}, "WORKBENCH_CLEANUP_COMPLETE"});
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
} // namespace eve::workbench
