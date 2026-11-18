/** @file bootstrap.cpp
 * @brief 固定业务产物的实际安装/来源准入；全部模块交给同一个 Workspace。
 */
#include "source_p.hpp"
#include <QDir>
#include <cordis/module/install.hpp>

namespace notebook {
using namespace cordis;
using namespace cordis::loader;
using namespace cordis::module;
namespace {
std::string initialPlatform(const std::map<std::string, std::string>& modules)
{
    // 三个新建默认值均为固定业务输入；现有文件从不经此生成器重写。
    return "formatVersion: 1\nmodules:\n  counter: {manifest: " + modules.at("counter") +
           "}\n  reporter: {manifest: " + modules.at("reporter") +
           "}\n  notebook: {manifest: " + modules.at("notebook") +
           "}\nplugins:\n  - {id: counter, plugin: example.counter, module: counter, config: null}\n"
           "  - {id: reporter, plugin: example.reporter, module: reporter, config: null}\n"
           "  - {id: notes, plugin: example.notebook, module: notebook, config: {title: 插件笔记, text: "
           "\"来自统一业务平台的草稿。\"}}\n";
}
} // namespace
ConfiguredPlugin Workspace::openNotebook(const PluginModule& source, bool stateful)
{
    const auto loaded = detail::require(loadQtModule(pluginRoot_, source.manifest));
    notebook_ = std::make_shared<NotebookModule>(loaded.generation);
    auto plugin = detail::prepareNotebook(notebook_, manager, scheduler, source, stateful);
    (void)detail::require(kernel->registerRuntime("example.notebook", *notebook_->runtime));
    return plugin;
}
void Workspace::initialize(const QString& root, const QString& store, const QString& file, bool qualification)
{
    publishUi();
    auto baseline = detail::require(readConfigurationFile(file));
    std::vector<ConfiguredPlugin> plugins;
    // 只保护安装到实际加载移交的准备期；运行来源由原模块目录持有。
    std::vector<InstalledArtifact> installations;
    if (!qualification) {
        pluginRoot_ = QDir(root).absolutePath().toUtf8().toStdString();
        initial = baseline.version.exists ? baseline.text
                                          : detail::require(readConfigurationFile(root + "/workspace.yaml")).text;
        plugins.push_back(openNotebook({"notebook", "notebook/module.json"}, false));
    } else {
        if (!QDir::isAbsolutePath(store) || !QDir(store).exists())
            throw detail::InitializationFailure{
                makeError(ErrorCode::InvalidArgument, "qualification store must be an existing absolute directory")};
        pluginRoot_ = QDir(store).absolutePath().toUtf8().toStdString();
        const auto sourceRoot = QDir(root).absolutePath().toUtf8().toStdString();
        const auto artifacts = detail::qualificationArtifacts(sourceRoot);
        for (const auto& artifact : artifacts)
            if (artifact.identity.loader == ModuleLoader::CAbi)
                approvals_.push_back({artifact.identity.moduleId, artifact.identity.sha256});
        std::map<std::string, std::string> locators;
        if (baseline.version.exists) {
            initial = baseline.text;
            const auto source = detail::require(parseConfiguration(initial));
            locators = source.structure().modules();
            for (const auto* name : {"counter", "reporter", "notebook"})
                if (!locators.count(name))
                    throw detail::InitializationFailure{
                        makeError(ErrorCode::ConfigInvalid,
                                  "qualification session requires counter, reporter and notebook modules")};
        } else {
            for (const auto& artifact : artifacts)
                if (artifact.identity.moduleVersion == "1.1.0") {
                    auto installed = detail::require(installModuleArtifact(sourceRoot, artifact.manifest, pluginRoot_));
                    locators.emplace(artifact.alias, installed.artifact().manifest().sha256 + "/module.json");
                    installations.push_back(std::move(installed));
                }
            initial = initialPlatform(locators);
        }
        for (const auto* name : {"counter", "reporter", "notebook"})
            detail::requireApproved(detail::reviewBusinessArtifact(name, pluginRoot_, locators.at(name)), approvals_);
        const auto prepare = [&](native_services::ServiceKind kind, const char* name,
                                 std::shared_ptr<native_services::CurrentModule>& record) {
            const auto generation =
                detail::require(manager->load(pluginRoot_, locators.at(name), {}, approvals_)).generation;
            record = std::make_shared<native_services::CurrentModule>(kind, generation);
            detail::require(native_services::prepareServiceModule(record, *manager, scheduler, kernel->services()));
            if (kind == native_services::ServiceKind::Counter) {
                const auto& schema = record->runtime->stateSchema();
                if (!schema || schema->id != "org.cordis.example.counter" || schema->version != 1)
                    throw detail::InitializationFailure{
                        makeError(ErrorCode::AbiMismatch, "qualification counter state schema differs")};
            }
            const auto plugin =
                detail::require(native_services::configuredServicePlugin(record, {name, locators.at(name)}));
            (void)detail::require(kernel->registerRuntime(plugin.configuration.id, *record->runtime));
            plugins.push_back(plugin);
        };
        prepare(native_services::ServiceKind::Counter, "counter", counter_);
        prepare(native_services::ServiceKind::Reporter, "reporter", reporter_);
        plugins.push_back(openNotebook({"notebook", locators.at("notebook")}, true));
    }
    initializeHost(file, std::move(baseline), std::move(plugins), qualification);
}
} // namespace notebook
