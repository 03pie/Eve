/** @file workspace_extensions.cpp
 * @brief Maps user-installed artifacts to workspace instances without rewriting saved installation paths.
 */
#include "workspace_extensions.hpp"
#include "workbench.hpp"
#include "startup_result.hpp"
#include "qt_system_p.hpp"
#include <eve/module/qt_module.hpp>
#include <QFileInfo>
#include <QTimer>
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
ErrorInfo unavailable(const char* message) { return makeError(ErrorCode::ConfigConflict, message); }
}

WorkspaceExtensions::WorkspaceExtensions(Workbench& host)
    : host_(host), controls_(std::make_shared<ui::ExtensionControls>())
{
    std::weak_ptr<const int> lifetime = lifetime_;
    controls_->list = [this, lifetime] {
        using Output = Result<std::vector<ExtensionItem>>;
        if (lifetime.expired()) return Output::failure(unavailable("Workspace closed."));
        try { return Output::success(inventory()); }
        catch (const StartupError& error) { return Output::failure(error.detail); }
    };
    controls_->preview = [this, lifetime](const QString& path) {
        Promise<ui::ExtensionPreview> reply;
        if (lifetime.expired() || host_.closing_) {
            reply.reject(unavailable("Workspace closed."));
            return reply.task();
        }
        const auto preview = extensions::Catalog::preview(path);
        if (preview.hasValue()) reply.resolve({describe(preview.value()), path, preview.value().packageHash});
        else {
            (void)host_.writeOutput("扩展安装包未通过预检。", ui::OutputLevel::Error, {{}, "EXTENSION_PREVIEW_FAILED"});
            reply.reject(preview.error());
        }
        return reply.task();
    };
    controls_->apply = [this, lifetime](auto action, const QString& id, const QString& path, const QString& digest) {
        if (lifetime.expired()) return failedTask(unavailable("Workspace closed."));
        try {
            auto task = apply(action, id, path, digest);
            task.thenOn(host_.scheduler, [this, id, lifetime](const Result<void>& result) {
                if (lifetime.expired()) return;
                const auto text = result.hasValue() ? QStringLiteral("扩展操作已完成 · ") : QStringLiteral("扩展操作失败 · ");
                (void)host_.writeOutput((text + id).toStdString(), result.hasValue() ? ui::OutputLevel::Info : ui::OutputLevel::Error,
                                       {{}, result.hasValue() ? "EXTENSION_ACTION_COMPLETED" : "EXTENSION_ACTION_FAILED"});
                if (auto view = host_.desktop()) (void)view->refresh();
            });
            return task;
        } catch (const StartupError& error) {
            (void)host_.writeOutput("扩展操作未完成。", ui::OutputLevel::Error, {{}, "EXTENSION_ACTION_FAILED"});
            return failedTask(error.detail);
        }
    };
}

loader::ConfiguredPlugin WorkspaceExtensions::attach(const Package& package, const std::string& alias, bool pinned)
{
    const auto root = QFileInfo(package.manifestPath).absolutePath().toStdString();
    const auto plugin = package.pluginId.toStdString();
    const auto loaded = pinned ? module::detail::loadInstalledQtModule(root, package.moduleId.toStdString(),
        plugin, package.sha256.toStdString()) : module::loadQtModule(root, "module.json");
    const auto generation = checked(loaded).generation;
    auto runtime = checked(module::qtRuntime(generation, {plugin,
        {ServiceRequirement::of<ui::UiPagesService>({pagesService}),
         ServiceRequirement::of<ui::CommandService>({commandsService}),
         ServiceRequirement::of<ui::OutputService>({outputService}),
         ServiceRequirement::of<std::shared_ptr<qt::QtScheduler>>({schedulerService})}, {}}, host_.scheduler));
    auto registered = checked(host_.kernel->registerRuntime(plugin, runtime));
    const module::ModuleManager modules;
    auto origin = checked(modules.runtimeOrigin(registered.id()));
    if (!origin || origin->generation != generation)
        throw StartupError(unavailable("Registered runtime does not match its module generation."));
    checked(host_.desktop()->registerQtOutputRuntime(registered.id(), generation, origin->moduleId,
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
        present.push_back(attach(*developer, "extension", false));
        known.insert(developer->pluginId.toStdString());
    }
    const auto installed = catalog_.installed();
    if (!installed.hasValue()) {
        (void)host_.writeOutput("无法读取用户扩展目录。", ui::OutputLevel::Error, {{}, "EXTENSION_CATALOG_UNAVAILABLE"});
    } else {
        for (const auto& package : installed.value()) {
            const auto id = package.pluginId.toStdString();
            if (package.kind != "business" || known.count(id)) continue;
            const auto& entries = target.structure().entries();
            if (std::none_of(entries.begin(), entries.end(), [&](const auto& entry) { return entry.second.plugin == id; })) continue;
            const auto alias = moduleAlias(target, package);
            if (alias != "extension" && alias != package.moduleId.toStdString()) continue;
            try {
                present.push_back(attach(package, alias, true));
                known.insert(id);
            } catch (const StartupError&) {
                (void)host_.writeOutput("工作区扩展加载失败 · " + id, ui::OutputLevel::Error, {{}, "EXTENSION_LOAD_FAILED"});
            }
        }
    }
    for (const auto& definition : present) protectedIds.push_back(definition.runtime.id());
    auto definitions = checked(loader::ConfigurationDefinitions::create(std::move(present)));
    driver_ = loader::KernelReconcileDriver::configured(host_.scheduler, host_.kernel, definitions);
    for (const auto& row : target.structure().entries()) {
        const auto& entry = row.second;
        if (known.count(entry.plugin) || entry.module == "builtin") continue;
        if (target.structure().modules().at(entry.module) != "module.json")
            throw StartupError(makeError(ErrorCode::ConfigInvalid, "Workspace extension manifest must be module.json."));
        checked(driver_->setPluginAvailability(entry.plugin, {entry.module, "module.json"}, {}));
        known.insert(entry.plugin);
        (void)host_.writeOutput("工作区扩展缺失 · " + entry.plugin, ui::OutputLevel::Warning, {{}, "EXTENSION_MISSING"});
    }
    return protectedIds;
}

std::vector<ExtensionItem> WorkspaceExtensions::inventory() const
{
    using A = ExtensionAction;
    const auto current = host_.selection().selected();
    const auto preference = host_.selection().requested();
    const auto next = preference.hasValue() ? preference.value() : QString{};
    ExtensionItem ela;
    ela.id = desktopModuleId;
    ela.name = QStringLiteral("Ela 桌面");
    ela.description = QStringLiteral("Eve 内置工作台桌面");
    ela.publisher = "Eve";
    ela.version = EVE_PRODUCT_VERSION;
    ela.pluginId = desktopId;
    ela.builtin = true;
    ela.kind = ui::ExtensionKind::Desktop;
    const auto desktopStatus = [&](ExtensionItem& item) {
        item.current = item.id == current;
        item.pending = (item.id == next && current != next) || (item.current && !item.builtin &&
                       !activeDesktopHash_.isEmpty() && item.pending);
        item.state = item.pending ? QStringLiteral("重启后生效") : item.current ? QStringLiteral("当前桌面") : QStringLiteral("可切换");
        if (item.pending) item.actions.push_back(A::Restart);
        if (item.id != next) item.actions.push_back(A::UseDesktop);
    };
    desktopStatus(ela);
    std::vector<ExtensionItem> items{ela};
    if (!host_.session) return items;
    const auto state = checked(host_.session->snapshot());
    const auto desired = state.coordinator.desired;
    const auto packages = checked(catalog_.installed());
    std::set<std::string> described;
    for (const auto& package : packages) {
        auto item = describe(package);
        const auto plugin = package.pluginId.toStdString();
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
                item.state = QStringLiteral("已安装 %1，重启后替换 %2").arg(package.version, live->second.package.version);
                item.actions.push_back(A::Restart);
            } else if (item.missing) {
                item.state = QStringLiteral("需要修复");
            } else {
                item.state = enabled ? QStringLiteral("当前工作区已启用") : item.instances ? QStringLiteral("当前工作区已禁用") : QStringLiteral("已安装");
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
            item.state = QStringLiteral("工作区引用缺失，重新安装可恢复");
            item.missing = true;
            item.actions = {A::Install, A::ShowLogs};
            if (!desired->pluginUnavailable(entry.plugin)) {
                item.pending = true;
                item.state = QStringLiteral("已从用户目录卸载，本窗口重启后生效");
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
    const auto accepted = checked(host_.session->apply(checked(host_.session->preview(desired.normalizedYaml()))));
    Promise<void> reply;
    accepted.settled.thenOn(host_.scheduler, [this, reply, revision = accepted.revision](const Result<loader::PlanReport>& plan) {
        if (!plan.hasValue()) { reply.reject(plan.error()); return; }
        if (plan.value().outcome != loader::PlanOutcome::Applied && plan.value().outcome != loader::PlanOutcome::Pending) {
            reply.reject(plan.value().errors.empty() ? unavailable("Workspace change did not finish.") : plan.value().errors.front());
            return;
        }
        auto save = host_.session->save(revision);
        if (!save.hasValue()) { reply.reject(save.error()); return; }
        save.value().thenOn(host_.scheduler, [reply](const Result<loader::SaveReport>& result) {
            if (!result.hasValue()) reply.reject(result.error());
            else if (result.value().outcome == loader::SaveOutcome::Written || result.value().outcome == loader::SaveOutcome::Unchanged)
                reply.resolve();
            else reply.reject(result.value().error.value_or(unavailable("Workspace changes could not be saved.")));
        });
    });
    return reply.task();
}

Task<void> WorkspaceExtensions::updateInstances(const Package& package, Activation action)
{
    const auto snapshot = checked(host_.session->snapshot());
    const auto target = snapshot.coordinator.desired;
    const auto plugin = package.pluginId.toStdString();
    const auto& source = target->source();
    auto resident = resident_.find(package.moduleId);
    if (resident != resident_.end() && resident->second.package.sha256 != package.sha256)
        return completedTask(); // The user catalog already selects the new image for the next process.
    if (resident == resident_.end()) {
        auto alias = moduleAlias(source, package);
        if (alias != package.moduleId.toStdString() && alias != "extension")
            throw StartupError(unavailable("Workspace reference belongs to a different module."));
        auto definition = attach(package, alias, true);
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

Task<void> WorkspaceExtensions::apply(ExtensionAction action, const QString& id, const QString& path, const QString& digest)
{
    using A = ExtensionAction;
    if (host_.closing_ || !host_.session || !driver_) return failedTask(unavailable("Workspace is unavailable."));
    const auto state = checked(host_.session->snapshot());
    if (!state.coordinator.desired || state.coordinator.activeRevision)
        return failedTask(unavailable("Workspace changes are in progress. Retry when they finish."));
    if (action == A::Restart) {
        if (!host_.restartRequested) return failedTask(unavailable("Restart is unavailable in this host."));
        QTimer::singleShot(0, host_.restartRequested);
        return completedTask();
    }
    if (action == A::UseDesktop && id == desktopModuleId) {
        checked(host_.selection().request(id));
        return completedTask();
    }
    if (id == desktopModuleId || id.isEmpty()) return failedTask(unavailable("Protected or missing extension identity."));
    if (action == A::Install) {
        const auto installed = checked(catalog_.install(path, digest, id));
        return installed.kind == "business" ? updateInstances(installed, Activation::AfterInstall) : completedTask();
    }
    const auto inventory = checked(catalog_.installed());
    const auto it = std::find_if(inventory.begin(), inventory.end(), [&](const auto& item) { return item.moduleId == id; });
    if (it == inventory.end()) return failedTask(unavailable("Extension is no longer installed."));
    if (action == A::UseDesktop && it->kind == "desktop") {
        checked(host_.selection().request(id));
        return completedTask();
    }
    if (action == A::Uninstall) {
        if (id == host_.selection().selected()) return failedTask(unavailable("Switch away from the active desktop before uninstalling it."));
        checked(catalog_.remove(id, digest));
        if (it->kind == "desktop") {
            if (checked(host_.selection().requested()) == id) checked(host_.selection().request(desktopModuleId));
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
    return failedTask(unavailable("This action is unavailable for the selected extension."));
}
}
