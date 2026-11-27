/** @file notebook_state_scene.cpp
 * @brief 真实 Ui 服务、Context 导入和原 Frozen Scope 捕获，不复制事务或插件接口。
 */
#include "../../src/core/instance_state_p.hpp"
#include "notebook_state_scene_p.hpp"
#include <ElaWindow.h>
#include <QApplication>
#include <QPlainTextEdit>
#include <QPluginLoader>
#include <QPushButton>
#include <QVariant>
#include <algorithm>
#include <eve/module/qt_manifest.hpp>
#include <eve/module/qt_state_plugin.hpp>
#include <iostream>

namespace notebook_test {
Scene::Scene(const QString& root, const QString& variant)
{
    const auto inspection = take(module::inspectQtModule(root.toUtf8().toStdString(), manifest(variant)));
    const auto& expected = inspection.artifact.manifest();
    NOTEBOOK_CHECK(expected.moduleId == "org.eve.example.notebook" &&
                   expected.moduleVersion == (variant == "a"   ? "1.1.0"
                                              : variant == "b" ? "1.2.0"
                                                               : "1.0.0") &&
                   expected.loader == module::ModuleLoader::QtUi &&
                   expected.unloadPolicy == module::ModuleUnloadPolicy::Resident &&
                   expected.plugins == std::vector<std::string>{"example.notebook"});
    take(shell.management()->attach(kernel));
    const auto provide = [&](const char* name, const auto& service) {
        auto provider = take(kernel->services().stage({name}, service));
        take(provider.publish());
        providers.push_back(std::move(provider));
    };
    provide("ui.pages", shell.pages());
    provide("ui.commands", shell.commands());
    provide("ui.output", shell.output());
    generation = take(module::loadQtModule(root.toUtf8().toStdString(), manifest(variant))).generation;
    runtime.emplace(take(module::qtRuntime(generation,
                                           {"example.notebook",
                                            {ServiceRequirement::of<ui::UiPagesService>({"ui.pages"}),
                                             ServiceRequirement::of<ui::CommandService>({"ui.commands"}),
                                             ServiceRequirement::of<ui::OutputService>({"ui.output"})},
                                            {}},
                                           scheduler)));
    take(kernel->registerRuntime("notebook", *runtime));
    if (variant == "legacy") {
        NOTEBOOK_CHECK(!runtime->stateSchema());
        QPluginLoader loader(QString::fromUtf8(inspection.artifact.libraryPath().c_str()));
        NOTEBOOK_CHECK(loader.instance() && !qobject_cast<module::IQtStatePluginV1*>(loader.instance()));
    } else
        NOTEBOOK_CHECK(runtime->stateSchema() && runtime->stateSchema()->id == schemaId &&
                       runtime->stateSchema()->version == 1);
    take(shell.show());
    for (auto* widget : QApplication::topLevelWidgets())
        if (auto* candidate = qobject_cast<ElaWindow*>(widget))
            window = candidate;
    NOTEBOOK_CHECK(window);
}
Imported Scene::inject(std::optional<StateSnapshot> state, std::string config)
{
    const auto child = std::make_shared<std::optional<Instance>>();
    auto parent = take(kernel->registerPlugin(
        "notebook.importer." + std::to_string(parents.size()),
        [runtime = *runtime, child, state = std::move(state), config = std::move(config)](Context context) {
            auto imported = context.inject(runtime, config, {}, state);
            if (!imported.hasValue())
                return failedTask(imported.error());
            child->emplace(std::move(imported).value());
            return (*child)->ready();
        }));
    parents.push_back(parent);
    return {take(kernel->mount(parent)), child};
}
ReplacementScope Scene::freeze() const
{
    auto scope = take(kernel->reserveReplacement({runtime->id()}));
    take(scope.freeze());
    return scope;
}
StateSnapshot Scene::capture(const Instance& instance) const
{
    auto scope = freeze();
    const auto captured = take(eve::detail::InstanceStateAccess::capture(scope, instance));
    NOTEBOOK_CHECK(captured && captured->schema.id == schemaId && captured->schema.version == 1);
    take(scope.release());
    barrier(scheduler);
    return *captured;
}
QWidget* Scene::page(const Instance& instance) const
{
    const auto id = take(instance.snapshot()).instance;
    for (auto* widget : QApplication::allWidgets())
        if (widget->objectName() == "EveNotebookPage" &&
            widget->property("eve.instance").toULongLong() == id.value())
            return widget;
    return nullptr;
}
QPlainTextEdit* Scene::editor(const Instance& instance) const
{
    const auto widget = page(instance);
    NOTEBOOK_CHECK(widget);
    auto* found = widget->findChild<QPlainTextEdit*>("EveNotebookEditor");
    NOTEBOOK_CHECK(found);
    return found;
}
QPushButton* Scene::publish(const Instance& instance) const
{
    const auto widget = page(instance);
    NOTEBOOK_CHECK(widget);
    auto* found = widget->findChild<QPushButton*>("EveNotebookPublish");
    NOTEBOOK_CHECK(found);
    return found;
}
std::size_t Scene::pageCount() const
{
    const auto entries = take(shell.pages()->snapshot());
    return static_cast<std::size_t>(
        std::count_if(entries.begin(), entries.end(), [](const auto& entry) { return entry.id == "notebook"; }));
}
std::size_t Scene::commandCount() const
{
    const auto entries = take(shell.commands()->snapshot());
    return static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(),
                                                  [](const auto& entry) { return entry.id == "notebook.publish"; }));
}
std::size_t Scene::outputCount(const std::string& text) const
{
    const auto entries = take(shell.output()->snapshot());
    return static_cast<std::size_t>(
        std::count_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.text == text; }));
}
module::ModuleGenerationSnapshot Scene::moduleState() const
{
    const auto entries = take(manager.snapshot());
    const auto found =
        std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.generation == generation; });
    NOTEBOOK_CHECK(found != entries.end());
    return *found;
}
void Scene::stop(const Imported& imported)
{
    NOTEBOOK_CHECK(wait(imported.parent.stop(), scheduler)->value().quiescent);
}
void Scene::close()
{
    NOTEBOOK_CHECK(wait(kernel->close(), scheduler)->value().quiescent);
    NOTEBOOK_CHECK(pageCount() == 0 && commandCount() == 0);
    NOTEBOOK_CHECK(wait(shell.close(), scheduler)->hasValue() && !window);
    for (const auto& provider : providers) {
        take(provider.retire());
        take(provider.release());
    }
    providers.clear();
    parents.clear();
    runtime.reset();
    kernel.reset();
    barrier(scheduler);
    const auto state = moduleState();
    NOTEBOOK_CHECK(state.nativeMapped && state.nativePinned && !state.failure && !state.businessFailure);
    if (state.resources.records.size() != 1)
        for (const auto& record : state.resources.records)
            std::cerr << "retained Notebook resource: " << static_cast<int>(record.kind) << " " << record.label << '\n';
    NOTEBOOK_CHECK(state.resources.records.size() == 1 &&
                   state.resources.records[0].kind == module::ResourceKind::QObject &&
                   state.resources.records[0].label == "qt.loader.root");
    const auto queue = take(scheduler->snapshot());
    NOTEBOOK_CHECK(queue.queued == 0 && queue.discarded == 0 && queue.callbackErrors == 0);
    take(scheduler->close());
}
} // namespace notebook_test
