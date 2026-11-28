/** @file desktop_activation.cpp
 * @brief 在应用线程创建官方桌面，本代清理排空窗口及其贡献。
 */
#include "desktop_text.hpp"
#include "desktop_contract.hpp"
#include <eve/ui/ela_shell.hpp>
#include <QWidget>
#include <stdexcept>

namespace eve::workbench {
namespace {
class ElaDesktop final : public DesktopView, public desktop::LayoutServiceV1, public std::enable_shared_from_this<ElaDesktop> {
public:
    ElaDesktop(const std::shared_ptr<qt::QtScheduler>& scheduler, DesktopBootstrap& host)
        : shell(scheduler, host.preferencesFile), scheduler_(scheduler), host_(host) {}
    ui::ExtensionItem description() const override
    {
        ui::ExtensionItem ela;
        ela.id = desktopModuleId;
        ela.name = eve::desktopText("desktop.elaDesktop");
        ela.description = eve::desktopText("desktop.eveSBuiltInWorkbenchDesktop");
        ela.readme = eve::desktopText("desktop.workspaceTheHomePageShowsExtensionInstances");
        ela.publisher = "Eve";
        ela.categories = QStringList{eve::desktopText("desktop.desktop")};
        ela.repository = QStringLiteral("https://github.com/03pie/Eve");
        ela.version = EVE_PRODUCT_VERSION;
        ela.pluginId = desktopId;
        ela.builtin = true;
        ela.kind = ui::ExtensionKind::Desktop;
        for (const auto& service : desktopContract().outputs)
            ela.desktopServices << QString::fromStdString(service.key.name);
        return ela;
    }
    Result<void> show() override { return shell.show(); }
    bool hasExternalLayout() const override { return shell.hasExternalLayout(); }
    bool externalLayoutVisible() const override { return shell.externalLayoutVisible(); }
    Result<void> attach(const Context& owner, desktop::Window window) override
    {
        auto allowed = authorizeLayout(owner);
        return allowed.hasValue() ? shell.attachLayout(owner, std::move(window)) : allowed;
    }
    Result<void> invoke(const Context& owner, desktop::Action action) override
    {
        auto allowed = authorizeLayout(owner);
        return allowed.hasValue() ? shell.invokeLayoutAction(action) : allowed;
    }
    Result<void> cancelCloseRequest() override { return shell.cancelCloseRequest(); }
    Result<void> onCloseRequested(std::function<void()> callback) override
    { return shell.onCloseRequested(std::move(callback)); }
    Result<void> refresh() override { return shell.management()->refresh(); }
    std::shared_ptr<ui::DiagnosticsService> diagnostics() const override { return shell.diagnostics(); }
    std::shared_ptr<ui::UiPagesService> pages() const override { return shell.pages(); }
    std::shared_ptr<ui::OutputService> output() const override { return shell.output(); }
    Result<void> showOutputEvent(std::uint64_t sequence, QWidget* origin) override
    { return shell.showOutputEvent(sequence, origin); }
    Result<void> registerQtOutputRuntime(RuntimeId runtime, GenerationId generation,
            const std::string& module, const std::string& version, const std::string& plugin) override
    { return shell.registerQtOutputRuntime(runtime, generation, module, version, plugin); }

    Result<void> bindWorkspace(const std::shared_ptr<Kernel>& kernel,
        const std::shared_ptr<loader::ConfigurationSession>& session, std::string initial,
        std::vector<RuntimeId> protectedRuntimes) override
    {
        session_ = session;
        initial_ = std::move(initial);
        management_ = ui::createConfigurationManagement(session, scheduler_);
        auto attached = shell.management()->attach(kernel);
        if (!attached.hasValue()) return attached;
        auto configured = shell.management()->attachConfiguration(management_, std::move(protectedRuntimes));
        if (!configured.hasValue()) return configured;
        auto modules = shell.onModulesRequested([this] { return shell.showManagement(); });
        if (!modules.hasValue()) return modules;
        return shell.onConfigurationRequested([this] {
            return configuration_ ? configuration_->navigate() :
                Result<void>::failure(makeError(ErrorCode::ContextClosing, "Configuration page unavailable."));
        });
    }

    Result<Runtime> registerConfiguration(Kernel& kernel) override
    {
        return kernel.registerPlugin(configurationPluginId, [weak = weak_from_this()](Context ctx) {
            auto view = weak.lock();
            if (!view) return failedTask(makeError(ErrorCode::ContextClosing, "Desktop unavailable."));
            auto pages = ctx.consume<ui::UiPagesService>({pagesService});
            if (!pages.hasValue()) return failedTask(pages.error());
            return pages.value().invoke([view, ctx](ui::UiPagesService& service) {
                return view->addConfigurationPage(ctx, service);
            });
        }, {ServiceRequirement::of<ui::UiPagesService>({pagesService})});
    }

    Task<void> addConfigurationPage(Context owner, ui::UiPagesService& service)
    {
        auto page = service.add(owner, {"configuration", eve::desktopText("desktop.workspaceConfiguration").toStdString(), configurationPluginId, [this] {
            auto widget = ui::createConfigurationPage(session_, initial_);
            ui::bindConfigurationFeedback(widget.get(),
                [this](std::string message, ui::OutputLevel level, ui::OutputDiagnostic diagnostic) {
                    auto event = shell.writeOutput(std::move(message), level, std::move(diagnostic));
                    return event.hasValue() ? event.value() : std::uint64_t{};
                }, [this](std::uint64_t event, QWidget* origin) { (void)shell.showOutputEvent(event, origin); });
            return widget;
        }, ui::PagePresentation::Dialog});
        if (!page.hasValue()) return failedTask(page.error());
        configuration_ = std::move(page).value();
        return configuration_->ready();
    }

    ui::ElaShell shell;
private:
    std::shared_ptr<qt::QtScheduler> scheduler_;
    DesktopBootstrap& host_;
    Result<void> authorizeLayout(const Context& owner) const
    {
        auto caller = owner.source();
        if (!caller.hasValue()) return Result<void>::failure(caller.error());
        if (!host_.layoutRuntime.valid() || caller.value().runtime != host_.layoutRuntime)
            return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "Unselected desktop runtime."));
        return Result<void>::success();
    }
    std::shared_ptr<loader::ConfigurationSession> session_;
    std::string initial_;
    std::shared_ptr<ui::IConfigurationManagement> management_;
    std::optional<ui::PageHandle> configuration_;
};
}

Task<void> activateDesktop(Context context)
{
    auto schedulerRef = context.consume<std::shared_ptr<qt::QtScheduler>>({schedulerService});
    if (!schedulerRef.hasValue()) return failedTask(schedulerRef.error());
    std::shared_ptr<qt::QtScheduler> scheduler;
    auto read = schedulerRef.value().withService([&](auto& shared) { scheduler = shared; });
    if (!read.hasValue()) return failedTask(read.error());
    auto bootstrap = context.consume<DesktopBootstrap>({desktopBootstrapKey});
    if (!bootstrap.hasValue()) return failedTask(bootstrap.error());
    auto archiveRef = context.consume<ui::detail::DesktopArchiveHandle>({desktopLogServiceKey});
    if (!archiveRef.hasValue()) return failedTask(archiveRef.error());
    ui::detail::DesktopArchiveHandle archive;
    auto journal = archiveRef.value().withService([&](const auto& run) { archive = run; });
    if (!journal.hasValue()) return failedTask(journal.error());
    auto extensionsRef = context.consume<ExtensionControlsHandle>({extensionsService});
    if (!extensionsRef.hasValue()) return failedTask(extensionsRef.error());
    ExtensionControlsHandle extensions;
    auto controls = extensionsRef.value().withService([&](const auto& handle) { extensions = handle; });
    if (!controls.hasValue()) return failedTask(controls.error());
    Task<void> result = completedTask();
    auto activated = bootstrap.value().withService([&](DesktopBootstrap& host) {
        auto desktop = std::make_shared<ElaDesktop>(scheduler, host);
        auto cleanup = context.onCleanup([desktop, scheduler](CleanupContext) {
            auto closed = desktop->shell.close();
            // Keep the owner alive through the last deferred QWidget deletion.
            closed.thenOn(scheduler, [desktop](const Result<void>&) {});
            return closed;
        });
        if (!cleanup.hasValue()) {
            // Activation failed before ownership transferred to the core's cleanup scope.
            Promise<void> rejected;
            desktop->shell.close().thenOn(scheduler, [desktop, rejected, error = cleanup.error()](const Result<void>& closed) {
                rejected.reject(closed.hasValue() ? error : closed.error());
            });
            result = rejected.task();
            return;
        }
        auto attached = ui::detail::attachDesktopArchive(desktop->shell, archive);
        if (!attached.hasValue()) { result = failedTask(attached.error()); return; }
        // The core commits the declared output set only after successful activation.
        const auto publish = [&](const char* key, const auto& service) {
            auto provided = context.provide({key}, service);
            if (!provided.hasValue()) result = failedTask(provided.error());
            return provided.hasValue();
        };
        if (publish(pagesService, desktop->shell.pages()) &&
            publish(commandsService, desktop->shell.commands()) &&
            publish(outputService, desktop->shell.output()) &&
            publish(notificationsService, desktop->shell.notifications()) &&
            publish(eve::desktop::layoutService, std::static_pointer_cast<eve::desktop::LayoutServiceV1>(desktop))) {
            // Binding builds the initial inventory, which reads this desktop's description.
            host.window = desktop;
            auto bound = desktop->shell.bindExtensions(extensions);
            if (!bound.hasValue()) {
                host.window.reset();
                result = failedTask(bound.error());
            }
        }
    });
    return activated.hasValue() ? result : failedTask(activated.error());
}
}
