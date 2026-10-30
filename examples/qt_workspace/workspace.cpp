/** @file workspace.cpp
 * @brief 复用 Kernel/Session、既有配置管理和页面服务，不另建插件生命周期。
 */
#include "workspace.hpp"
#include "contract.hpp"
#include <QWidget>

namespace notebook {
using namespace cordis;
using namespace cordis::module;
using namespace cordis::loader;
using namespace cordis::ui;
std::shared_ptr<Workspace> Workspace::open(const QString& root, const QString& file)
{
    auto self = std::make_shared<Workspace>();
    self->pin = self;
    self->initialize(root, file);
    return self;
}
void Workspace::initialize(const QString& root, const QString& file)
{
    take(shell.management()->attach(kernel));
    auto publish = [&](const auto& value, const char* name) {
        auto provider = take(kernel->services().stage({name}, value));
        take(provider.publish());
        providers.push_back(std::move(provider));
    };
    publish(shell.pages(), "ui.pages");
    publish(shell.commands(), "ui.commands");
    publish(shell.output(), "ui.output");
    generation = take(loadQtModule(root.toStdString(), "notebook/module.json")).generation;
    runtime = take(qtRuntime(
        generation,
        {"example.notebook",
         {ServiceRequirement::of<UiPagesService>({"ui.pages"}), ServiceRequirement::of<CommandService>({"ui.commands"}),
          ServiceRequirement::of<OutputService>({"ui.output"})},
         {}},
        scheduler));
    take(kernel->registerRuntime("example.notebook", *runtime));
    const PluginModule module{"notebook", "notebook/module.json"};
    std::weak_ptr<ModuleManager> weakManager = manager;
    auto available = [weakManager, id = generation] {
        auto value = weakManager.lock();
        if (value) {
            auto snapshot = value->snapshot();
            if (!snapshot.hasValue())
                return Result<void>::failure(snapshot.error());
            for (const auto& row : snapshot.value())
                if (row.generation == id && !row.failure && !row.businessFailure &&
                    (row.state == ModuleState::Loaded || row.state == ModuleState::Serving))
                    return Result<void>::success();
        }
        return Result<void>::failure(makeError(ErrorCode::ModulePinned, "notebook module unavailable"));
    };
    driver = std::make_shared<KernelReconcileDriver>(
        scheduler, kernel, std::vector<KernelPlugin>{{"example.notebook", *runtime, {}, module, available}});
    const auto baseline = take(readConfigurationFile(file));
    initial = baseline.version.exists ? baseline.text : take(readConfigurationFile(root + "/workspace.yaml")).text;
    session = std::make_shared<ConfigurationSession>(
        scheduler, driver,
        std::vector<PluginDefinition>{{"example.notebook",
                                       {{"ui.pages"}, {"ui.commands"}, {"ui.output"}},
                                       [](const std::string& text) {
                                           auto checked = configuration(text);
                                           return checked.hasValue() ? Result<void>::success()
                                                                     : Result<void>::failure(checked.error());
                                       },
                                       module}},
        std::make_shared<QtConfigurationWriter>(file), baseline);
    configurationManagement = createConfigurationManagement(session, scheduler);
    std::weak_ptr<Workspace> weak = shared_from_this();
    auto definition = take(kernel->registerPlugin("host.notebook", [weak](Context context) {
        auto self = weak.lock();
        if (!self)
            return failedTask(makeError(ErrorCode::ContextClosing, "workspace unavailable"));
        self->configurationPage =
            take(self->shell.pages()->add(context, {"configuration", "配置文件", "host.configuration",
                                                    [session = self->session, initial = self->initial] {
                                                        return createConfigurationPage(session, initial);
                                                    },
                                                    PagePresentation::Dialog}));
        self->modulesPage =
            take(self->shell.pages()->add(context, {"modules", "模块诊断", "host.modules",
                                                    [manager = self->manager, diagnostics = self->shell.diagnostics()] {
                                                        return moduleDiagnostics(manager, diagnostics);
                                                    },
                                                    PagePresentation::Dialog}));
        return self->configurationPage->ready();
    }));
    take(shell.management()->attachConfiguration(configurationManagement, {runtime->id(), definition.id()}));
    host = take(kernel->mount(definition));
    take(shell.onConfigurationRequested([weak] {
        auto self = weak.lock();
        return self && self->configurationPage
                   ? self->configurationPage->navigate()
                   : Result<void>::failure(makeError(ErrorCode::ContextClosing, "configuration page unavailable"));
    }));
}
Task<void> Workspace::ready() const
{
    return host->ready();
}
Result<AcceptedTarget> Workspace::apply(const std::string& text)
{
    auto draft = session->preview(text);
    return draft.hasValue() ? session->apply(draft.value()) : Result<AcceptedTarget>::failure(draft.error());
}
Task<void> Workspace::close()
{
    if (closing)
        return stopped.task();
    stopped = Promise<void>{};
    closing = true;
    flushBeforeClose();
    return stopped.task();
}
void Workspace::flushBeforeClose()
{
    auto self = shared_from_this();
    auto snapshot = session->snapshot();
    if (!snapshot.hasValue()) {
        closing = false;
        stopped.reject(snapshot.error());
        return;
    }
    if (snapshot.value().store.dirty) {
        auto saved = session->save(snapshot.value().coordinator.desiredRevision);
        if (!saved.hasValue()) {
            closing = false;
            stopped.reject(saved.error());
            return;
        }
        watch(saved.value(), [self](const Result<SaveReport>& result) {
            if (result.hasValue() &&
                (result.value().outcome == SaveOutcome::Written || result.value().outcome == SaveOutcome::Unchanged))
                self->flushBeforeClose();
            else {
                self->closing = false;
                self->stopped.reject(
                    !result.hasValue() ? result.error()
                                       : result.value().error.value_or(makeError(
                                             ErrorCode::ConfigConflict,
                                             "configuration was not saved; resolve the disk conflict before closing")));
            }
        });
        return;
    }
    watch(session->close(), [self](const Result<void>& result) {
        if (!result.hasValue())
            self->stopped.reject(result.error());
        else
            self->closeKernel();
    });
}
void Workspace::closeKernel()
{
    auto self = shared_from_this();
    watch(kernel->close(), [self](const Result<CleanupReport>& result) {
        if (!result.hasValue())
            self->stopped.reject(result.error());
        else if (!result.value().quiescent)
            self->stopped.reject(makeError(ErrorCode::CleanupFailed, "kernel retained resources"));
        else
            self->closeShell();
    });
}
void Workspace::closeShell()
{
    auto self = shared_from_this();
    watch(shell.close(), [self](const Result<void>& result) {
        if (!result.hasValue()) {
            self->stopped.reject(result.error());
            return;
        }
        for (const auto& provider : self->providers) {
            take(provider.retire());
            take(provider.release());
        }
        self->providers.clear();
        self->host.reset();
        self->configurationPage.reset();
        self->modulesPage.reset();
        self->configurationManagement.reset();
        self->session.reset();
        self->driver.reset();
        self->kernel.reset();
        self->runtime.reset();
        self->stopped.resolve();
        self->pin.reset();
    });
}
} // namespace notebook
