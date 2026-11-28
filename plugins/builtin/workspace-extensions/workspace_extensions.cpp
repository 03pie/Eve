/** @file workspace_extensions.cpp
 * @brief Maps user-installed artifacts to workspace instances without rewriting saved installation paths.
 */
#include "desktop_text.hpp"
#include "workspace_extensions.hpp"
#include "startup_result.hpp"
#include <eve/extensions/qt_module.hpp>
#include <eve/module/qt_module.hpp>
#include <QFileInfo>
#include <algorithm>
#include <set>

namespace eve::workbench {
using extensions::Package;
using ui::ExtensionAction;
using ui::ExtensionItem;
namespace {
ExtensionItem describe(const Package& package)
{
    ExtensionItem item;
    item.id = package.moduleId;
    item.pluginId = package.pluginId;
    item.name = package.name.isEmpty() ? package.moduleId : package.name;
    item.publisher = package.publisher;
    item.description = package.description;
    item.readme = package.readme;
    item.changelog = package.changelog;
    item.defaultConfig = package.defaultConfig;
    item.categories = package.categories;
    item.homepage = package.homepage;
    item.repository = package.repository;
    item.issues = package.issues;
    item.license = package.license;
    item.packageBytes = package.packageBytes;
    item.installedAt = package.installedAt;
    item.version = package.version;
    item.packageHash = package.packageHash;
    item.icon = package.icon;
    item.kind = package.kind == "desktop" ? ui::ExtensionKind::Desktop : ui::ExtensionKind::Business;
    return item;
}
std::string moduleAlias(const loader::ParsedConfiguration& target, const Package& package)
{
    for (const auto& entry : target.structure().entries())
        if (entry.second.plugin == package.pluginId.toStdString())
            return entry.second.module;
    return package.moduleId.toStdString();
}
ErrorInfo unavailable(const QString& message) { return makeError(ErrorCode::ConfigConflict, message.toStdString()); }
}

Result<Runtime> registerWorkspaceExtensions(Kernel& kernel, WorkspaceInputs inputs,
    std::function<void(std::weak_ptr<WorkspaceExtensions>)> activated)
{
    return kernel.registerPlugin(workspaceExtensionsId, [inputs = std::move(inputs), activated = std::move(activated)](Context ctx) {
        auto log = ctx.consume<DesktopRunLogSink>({desktopRunLogSinkKey});
        if (!log.hasValue()) return failedTask(log.error());
        auto extensions = std::make_shared<WorkspaceExtensions>(ctx, inputs, std::move(log).value());
        auto provided = ctx.provide({extensionsService}, std::make_shared<ExtensionControlsHandle>(extensions->controls()));
        if (!provided.hasValue()) return failedTask(provided.error());
        auto owned = ctx.onCleanup([extensions](CleanupContext) { return completedTask(); });
        if (!owned.hasValue()) return failedTask(owned.error());
        activated(extensions);
        return completedTask();
    }, {ServiceRequirement::of<DesktopRunLogSink>({desktopRunLogSinkKey})},
       {ServiceOutput::of<ExtensionControlsHandle>({extensionsService})});
}

WorkspaceExtensions::WorkspaceExtensions(Context owner, WorkspaceInputs inputs, ServiceRef<DesktopRunLogSink> log)
    : owner_(owner), inputs_(std::move(inputs)), log_(std::move(log)), controls_(std::make_shared<ui::ExtensionControls>())
{
    // A retained UI action must pass its original instance's owner-thread and epoch checks.
    controls_->list = [this, owner] {
        using Output = Result<std::vector<ExtensionItem>>;
        auto active = owner.identity();
        if (!active.hasValue()) return Output::failure(active.error());
        if (inputs_.closing()) return Output::failure(unavailable(desktopText("extensions.workspaceClosed")));
        try { return Output::success(shared_from_this()->inventory()); }
        catch (const StartupError& error) { return Output::failure(error.detail); }
    };
    controls_->preview = [this, owner](const QString& path) {
        Promise<ui::ExtensionPreview> reply;
        auto active = owner.identity();
        if (!active.hasValue()) { reply.reject(active.error()); return reply.task(); }
        if (inputs_.closing()) {
            reply.reject(unavailable(desktopText("extensions.workspaceClosed")));
            return reply.task();
        }
        const auto preview = extensions::Catalog::preview(path);
        if (preview.hasValue()) {
            const auto inventory = catalog_.installed();
            if (!inventory.hasValue()) {
                reply.reject(inventory.error());
                return reply.task();
            }
            const auto& package = preview.value();
            ui::ExtensionPreview review{describe(package), path, package.packageHash, {}, {}};
            for (const auto& installed : inventory.value()) {
                if (installed.moduleId != package.moduleId) continue;
                review.installedVersion = installed.version;
                review.installedHash = installed.packageHash;
                break;
            }
            reply.resolve(std::move(review));
        } else {
            writeLog(eve::desktopText("extensions.extensionPackagePreflightFailed").toStdString(), ui::OutputLevel::Error,
                     {{}, "EXTENSION_PREVIEW_FAILED"});
            reply.reject(preview.error());
        }
        return reply.task();
    };
    const auto submit = [this, owner](ExtensionAction action, const QString& id, const QString& path,
                                        const QString& digest, std::optional<QString> expected) {
        auto active = owner.identity();
        if (!active.hasValue()) return failedTask(active.error());
        const auto instance = shared_from_this();
        try {
            auto task = instance->apply(action, id, path, digest, std::move(expected));
            task.thenOn(inputs_.scheduler, [this, id, owner](const Result<void>& result) {
                if (!owner.identity().hasValue() || inputs_.closing()) return;
                const auto current = shared_from_this();
                const auto text = result.hasValue() ? eve::desktopText("extensions.extensionActionCompleted")
                                                    : eve::desktopText("extensions.extensionActionFailed");
                current->writeLog(text.arg(id).toStdString(), result.hasValue() ? ui::OutputLevel::Info : ui::OutputLevel::Error,
                                  {{}, result.hasValue() ? "EXTENSION_ACTION_COMPLETED" : "EXTENSION_ACTION_FAILED"});
                if (auto view = current->inputs_.desktop()) (void)view->refresh();
            });
            return task;
        } catch (const StartupError& error) {
            instance->writeLog(eve::desktopText("extensions.theExtensionActionDidNotComplete").toStdString(),
                               ui::OutputLevel::Error, {{}, "EXTENSION_ACTION_FAILED"});
            return failedTask(error.detail);
        }
    };
    controls_->apply = [submit](auto action, const QString& id, const QString& path, const QString& digest) {
        return submit(action, id, path, digest, std::nullopt);
    };
    controls_->installReviewed = [submit](const ui::ExtensionPreview& review) {
        return submit(ExtensionAction::Install, review.item.id, review.path, review.sha256, review.installedHash);
    };
}

void WorkspaceExtensions::writeLog(std::string text, ui::OutputLevel level, ui::OutputDiagnostic diagnostic) const
{
    (void)log_.withService([&](DesktopRunLogSink& sink) {
        (void)sink.write(std::move(text), level, std::move(diagnostic));
    });
}

loader::ConfiguredPlugin WorkspaceExtensions::attach(const Package& package, const std::string& alias,
                                                     extensions::ExtensionSource source)
{
    const auto kernel = inputs_.kernel.lock();
    const auto desktop = inputs_.desktop();
    if (!kernel || !desktop) throw StartupError(unavailable(desktopText("extensions.workspaceIsUnavailable")));
    const auto plugin = package.pluginId.toStdString();
    const auto loaded = extensions::loadQtExtension(package.manifestPath, package.moduleId, package.pluginId, package.sha256, source);
    const auto generation = checked(loaded).generation;
    auto runtime = checked(module::qtRuntime(generation, {plugin,
        {ServiceRequirement::of<ui::UiPagesService>({pagesService}),
         ServiceRequirement::of<ui::CommandService>({commandsService}),
         ServiceRequirement::of<ui::OutputService>({outputService}),
         ServiceRequirement::of<std::shared_ptr<qt::QtScheduler>>({schedulerService})}, {}}, inputs_.scheduler));
    auto registered = checked(kernel->registerRuntime(plugin, runtime));
    const module::ModuleManager modules;
    auto origin = checked(modules.runtimeOrigin(registered.id()));
    if (!origin || origin->generation != generation)
        throw StartupError(unavailable(desktopText("extensions.registeredRuntimeDoesNotMatchItsModule")));
    checked(desktop->registerQtOutputRuntime(registered.id(), generation, origin->moduleId,
                                             package.version.toStdString(), origin->pluginId));
    auto object = [](const std::string& config) {
        return !config.empty() && config.front() == '{' ? Result<void>::success() :
            Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "External UI plugin config must be an object."));
    };
    loader::ConfiguredPlugin definition{{plugin, {{pagesService}, {commandsService}, {outputService}, {schedulerService}},
                                         object, {alias, "module.json"}}, std::move(runtime)};
    resident_.emplace(package.moduleId, Resident{package, definition});
    return definition;
}

std::vector<RuntimeId> WorkspaceExtensions::prepare(const loader::ParsedConfiguration& target,
                                                   const std::optional<Package>& developer)
{
    std::vector<RuntimeId> protectedIds;
    std::vector<loader::ConfiguredPlugin> present;
    std::set<std::string> known;
    if (developer) {
        present.push_back(attach(*developer, "extension", extensions::ExtensionSource::DevelopmentManifest));
        known.insert(developer->pluginId.toStdString());
    }
    const auto installed = catalog_.installed();
    if (!installed.hasValue()) {
        writeLog(eve::desktopText("extensions.couldNotReadTheUserExtensionCatalog").toStdString(), ui::OutputLevel::Error,
                 {{}, "EXTENSION_CATALOG_UNAVAILABLE"});
    } else {
        for (const auto& package : installed.value()) {
            const auto id = package.pluginId.toStdString();
            if (package.kind != "business" || known.count(id) || extensions::isLegacyPluginConflict(package.pluginId)) continue;
            const auto& entries = target.structure().entries();
            if (std::none_of(entries.begin(), entries.end(), [&](const auto& entry) { return entry.second.plugin == id; })) continue;
            const auto alias = moduleAlias(target, package);
            if (alias != "extension" && alias != package.moduleId.toStdString()) continue;
            try {
                present.push_back(attach(package, alias));
                known.insert(id);
            } catch (const StartupError&) {
                writeLog(eve::desktopText("extensions.couldNotLoadWorkspaceExtension").arg(package.pluginId).toStdString(),
                         ui::OutputLevel::Error, {{}, "EXTENSION_LOAD_FAILED"});
            }
        }
    }
    for (const auto& definition : present) protectedIds.push_back(definition.runtime.id());
    auto definitions = checked(loader::ConfigurationDefinitions::create(std::move(present)));
    driver_ = loader::KernelReconcileDriver::configured(inputs_.scheduler, inputs_.kernel.lock(), definitions);
    for (const auto& row : target.structure().entries()) {
        const auto& entry = row.second;
        if (known.count(entry.plugin) || entry.module == "builtin") continue;
        if (target.structure().modules().at(entry.module) != "module.json")
            throw StartupError(makeError(ErrorCode::ConfigInvalid, "Workspace extension manifest must be module.json."));
        checked(driver_->setPluginAvailability(entry.plugin, {entry.module, "module.json"}, {}));
        known.insert(entry.plugin);
        writeLog(eve::desktopText("extensions.missingWorkspaceExtension").arg(QString::fromStdString(entry.plugin)).toStdString(),
                 ui::OutputLevel::Warning, {{}, "EXTENSION_MISSING"});
    }
    return protectedIds;
}

std::vector<ExtensionItem> WorkspaceExtensions::inventory() const
{
    using A = ExtensionAction;
    const auto current = inputs_.selected();
    const auto preference = inputs_.requested();
    const auto next = preference.hasValue() ? preference.value() : QString{};
    auto view = inputs_.desktop();
    if (!view) throw StartupError(unavailable(desktopText("extensions.desktopUnavailable")));
    auto ela = view->description();
    const auto desktopStatus = [&](ExtensionItem& item) {
        item.current = item.id == current;
        item.pending = (item.id == next && current != next) || (item.current && !item.builtin &&
                       !activeDesktopHash_.isEmpty() && item.pending);
        item.state = item.pending ? eve::desktopText("extensions.takesEffectAfterRestart")
                                  : item.current ? eve::desktopText("extensions.currentDesktop") : eve::desktopText("extensions.available");
        if (item.pending) item.actions.push_back(A::Restart);
        if (item.id != next) item.actions.push_back(A::UseDesktop);
    };
    desktopStatus(ela);
    std::vector<ExtensionItem> items{ela};
    if (!session_) return items;
    const auto state = checked(session_->snapshot());
    const auto desired = state.coordinator.desired;
    const auto packages = checked(catalog_.installed());
    std::set<std::string> described;
    for (const auto& package : packages) {
        auto item = describe(package);
        const auto plugin = package.pluginId.toStdString();
        if (extensions::isLegacyPluginConflict(package.pluginId)) {
            described.insert(plugin);
            item.state = eve::desktopText("extensions.aBuiltInServiceNowUsesThis");
            item.missing = true;
            item.actions = {A::Uninstall};
            items.push_back(std::move(item));
            continue;
        }
        const auto live = resident_.find(package.moduleId);
        if (item.kind == ui::ExtensionKind::Desktop) {
            item.pending = activeDesktopHash_ != package.sha256;
            desktopStatus(item);
        } else {
            described.insert(plugin);
            bool enabled = false, disabled = false;
            if (desired) {
                for (const auto& row : desired->source().structure().entries()) {
                    if (row.second.plugin != plugin) continue;
                    ++item.instances;
                    if (row.second.disabled) disabled = true;
                    else enabled = true;
                }
            }
            item.missing = !QFileInfo::exists(package.manifestPath) ||
                           (desired && desired->pluginUnavailable(plugin));
            item.pending = live != resident_.end() && live->second.package.sha256 != package.sha256;
            if (item.pending) {
                item.state = eve::desktopText("extensions.installedRestartToReplace").arg(package.version, live->second.package.version);
                item.actions.push_back(A::Restart);
            } else if (item.missing) {
                item.state = eve::desktopText("extensions.repairRequired");
            } else {
                if (enabled && disabled) item.state = eve::desktopText("extensions.someInstancesEnabled");
                else if (enabled) item.state = eve::desktopText("extensions.enabledInThisWorkspace");
                else item.state = item.instances ? eve::desktopText("extensions.disabledInThisWorkspace") : eve::desktopText("extensions.installed");
                if (!item.instances || disabled) item.actions.push_back(A::Enable);
                if (live != resident_.end()) item.actions.push_back(A::AddInstance);
                if (item.instances) item.actions.push_back(A::Configure);
                if (enabled) item.actions.push_back(A::Disable);
            }
            if (item.instances) item.actions.push_back(A::ShowLogs);
        }
        item.actions.push_back(A::Install);
        if (!item.current) item.actions.push_back(A::Uninstall);
        items.push_back(std::move(item));
    }
    // A manifest supplied through --module has no catalog record. Its instance cards still have real
    // module identity and runtime actions, while package installation and removal belong to the user catalog.
    for (const auto& record : resident_) {
        const auto& package = record.second.package;
        const auto plugin = package.pluginId.toStdString();
        if (!package.packageHash.isEmpty() || described.count(plugin)) continue;
        auto item = describe(package);
        item.state = eve::desktopText("extensions.developmentMode");
        item.description = eve::desktopText("extensions.localDevelopmentModuleLoadedFromALaunch");
        if (desired) {
            const auto& entries = desired->source().structure().entries();
            item.instances = static_cast<int>(std::count_if(entries.begin(), entries.end(), [&](const auto& row) {
                return row.second.plugin == plugin;
            }));
        }
        item.actions = {A::AddInstance};
        if (item.instances) {
            item.actions.push_back(A::Configure);
            item.actions.push_back(A::ShowLogs);
        }
        described.insert(plugin);
        items.push_back(std::move(item));
    }
    if (desired) {
        std::map<std::string, ExtensionItem> missing;
        for (const auto& row : desired->source().structure().entries()) {
            const auto& entry = row.second;
            if (described.count(entry.plugin)) continue;
            const auto live = std::find_if(resident_.begin(), resident_.end(), [&](const auto& pair) {
                return pair.second.package.pluginId.toStdString() == entry.plugin && !pair.second.package.packageHash.isEmpty();
            });
            if (!desired->pluginUnavailable(entry.plugin) && live == resident_.end()) continue;
            auto& item = missing[entry.plugin];
            item.id = item.name = live != resident_.end() ? live->first : QString::fromStdString(entry.module);
            item.pluginId = QString::fromStdString(entry.plugin);
            item.legacyReference = entry.module == "extension" && live == resident_.end();
            if (item.legacyReference) {
                item.id = "extension/" + item.pluginId;
                item.name = item.pluginId;
            }
            item.state = eve::desktopText("extensions.workspaceReferenceIsMissingReinstallToRestore");
            item.missing = true;
            item.actions = {A::Install, A::ShowLogs};
            if (extensions::isLegacyPluginConflict(item.pluginId)) {
                item.state = eve::desktopText("extensions.aBuiltInServiceNowUsesThe");
                item.actions.clear();
            }
            if (!desired->pluginUnavailable(entry.plugin)) {
                item.pending = true;
                item.state = eve::desktopText("extensions.removedFromTheUserCatalogTakesEffect");
                item.actions.insert(item.actions.begin(), A::Restart);
            }
            ++item.instances;
        }
        for (auto& row : missing) items.push_back(std::move(row.second));
    }
    return items;
}

Task<void> WorkspaceExtensions::reconcile(const loader::ParsedConfiguration& desired)
{
    const auto accepted = checked(session_->apply(checked(session_->preview(desired.normalizedYaml()))));
    Promise<void> reply;
    accepted.settled.thenOn(inputs_.scheduler, [owner = owner_, session = session_, scheduler = inputs_.scheduler,
                                               reply, revision = accepted.revision](const Result<loader::PlanReport>& plan) {
        auto active = owner.identity();
        if (!active.hasValue()) { reply.reject(active.error()); return; }
        if (!plan.hasValue()) { reply.reject(plan.error()); return; }
        if (plan.value().outcome != loader::PlanOutcome::Applied && plan.value().outcome != loader::PlanOutcome::Pending) {
            reply.reject(plan.value().errors.empty() ? unavailable(desktopText("extensions.workspaceChangeDidNotFinish")) : plan.value().errors.front());
            return;
        }
        auto save = session->save(revision);
        if (!save.hasValue()) { reply.reject(save.error()); return; }
        save.value().thenOn(scheduler, [reply](const Result<loader::SaveReport>& result) {
            if (!result.hasValue()) reply.reject(result.error());
            else if (result.value().outcome == loader::SaveOutcome::Written || result.value().outcome == loader::SaveOutcome::Unchanged)
                reply.resolve();
            else reply.reject(result.value().error.value_or(unavailable(desktopText("extensions.workspaceChangesCouldNotBeSaved"))));
        });
    });
    return reply.task();
}

Task<void> WorkspaceExtensions::updateInstances(const Package& package, Activation action)
{
    const auto snapshot = checked(session_->snapshot());
    const auto target = snapshot.coordinator.desired;
    const auto plugin = package.pluginId.toStdString();
    const auto& source = target->source();
    auto resident = resident_.find(package.moduleId);
    if (resident != resident_.end() && resident->second.package.sha256 != package.sha256)
        return completedTask(); // The user catalog already selects the new image for the next process.
    if (resident == resident_.end()) {
        auto alias = moduleAlias(source, package);
        if (alias != package.moduleId.toStdString() && alias != "extension")
            throw StartupError(unavailable(desktopText("extensions.workspaceReferenceBelongsToADifferentModule")));
        auto definition = attach(package, alias);
        checked(driver_->setPluginAvailability(plugin, definition.configuration.module, definition));
    } else if (target->pluginUnavailable(plugin) || !driver_->configurationDefinitions()->module(plugin).hasValue()) {
        const auto& definition = resident->second.definition;
        checked(driver_->setPluginAvailability(plugin, definition.configuration.module, definition));
    }
    std::optional<loader::ParsedConfiguration> changed(source);
    bool referenced = false;
    for (const auto& row : source.structure().entries()) {
        if (row.second.plugin != plugin) continue;
        referenced = true;
        // Installing over a saved workspace revives its own intent. Explicit power actions edit that intent.
        if (action != Activation::AfterInstall && row.second.disabled == (action == Activation::Enable))
            changed.emplace(checked(loader::setEntryEnabled(*changed, row.first, action == Activation::Enable)));
    }
    if (!referenced && action != Activation::Disable) {
        auto id = plugin;
        for (int ordinal = 2; source.structure().entries().count(id); ++ordinal)
            id = plugin + "-" + std::to_string(ordinal);
        changed.emplace(checked(loader::addConfigurationEntry(*changed, id, plugin,
            package.defaultConfig.isEmpty() ? "{}" : package.defaultConfig.toStdString(), {},
            resident_.at(package.moduleId).definition.configuration.module)));
    }
    return reconcile(*changed);
}

Task<void> WorkspaceExtensions::apply(ExtensionAction action, const QString& id, const QString& path, const QString& digest,
                                      std::optional<QString> expectedInstalledHash)
{
    using A = ExtensionAction;
    if (inputs_.closing() || !session_ || !driver_) return failedTask(unavailable(desktopText("extensions.workspaceIsUnavailable")));
    const auto state = checked(session_->snapshot());
    if (!state.coordinator.desired || state.coordinator.activeRevision)
        return failedTask(unavailable(desktopText("extensions.workspaceChangesAreInProgressRetryWhen")));
    if (action == A::Restart) {
        auto result = inputs_.restart();
        return result.hasValue() ? completedTask() : failedTask(result.error());
    }
    if (action == A::UseDesktop && id == desktopModuleId) {
        checked(inputs_.select(id));
        return completedTask();
    }
    if (id == desktopModuleId || id.isEmpty()) return failedTask(unavailable(desktopText("extensions.protectedOrMissingExtensionIdentity")));
    if (action == A::Install) {
        const auto installed = checked(catalog_.install(path, digest, id, std::move(expectedInstalledHash)));
        return installed.kind == "business" ? updateInstances(installed, Activation::AfterInstall) : completedTask();
    }
    const auto inventory = checked(catalog_.installed());
    const auto it = std::find_if(inventory.begin(), inventory.end(), [&](const auto& item) { return item.moduleId == id; });
    if (it == inventory.end()) return failedTask(unavailable(desktopText("extensions.extensionIsNoLongerInstalled")));
    if (extensions::isLegacyPluginConflict(it->pluginId)) {
        if (action != A::Uninstall)
            return failedTask(unavailable(desktopText("extensions.thisExtensionConflictsWithABuiltIn")));
        checked(catalog_.remove(id, digest));
        if (it->kind == "desktop" && checked(inputs_.requested()) == id) checked(inputs_.select(desktopModuleId));
        return completedTask();
    }
    if (action == A::UseDesktop && it->kind == "desktop") {
        checked(inputs_.select(id));
        return completedTask();
    }
    if (action == A::Uninstall) {
        if (id == inputs_.selected()) return failedTask(unavailable(desktopText("extensions.switchAwayFromTheActiveDesktopBefore")));
        checked(catalog_.remove(id, digest));
        if (it->kind == "desktop") {
            if (checked(inputs_.requested()) == id) checked(inputs_.select(desktopModuleId));
            return completedTask();
        }
        const auto& source = state.coordinator.desired->source();
        const auto live = resident_.find(id);
        if (live != resident_.end() && live->second.package.packageHash.isEmpty()) return completedTask();
        checked(driver_->setPluginAvailability(it->pluginId.toStdString(), {moduleAlias(source, *it), "module.json"}, {}));
        return reconcile(source);
    }
    if (it->kind == "business" && (action == A::Enable || action == A::Disable))
        return updateInstances(*it, action == A::Enable ? Activation::Enable : Activation::Disable);
    return failedTask(unavailable(desktopText("extensions.thisActionIsUnavailableForTheSelected")));
}
}
