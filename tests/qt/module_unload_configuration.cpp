/** @file module_unload_configuration.cpp
 * @brief 共同替换提交后显式回收旧代，配置拥有者和业务结果独立于物理终结。
 */
#include "module_unload_replacement_p.hpp"
#include <QDir>
#include <algorithm>
#include <array>
#include <cordis/loader/qt_store.hpp>
#include <map>
#include <set>
#include <vector>

namespace unload_test {
namespace {
using namespace cordis::loader;

thread_local unsigned failedReleases = 0;
thread_local HMODULE failedImage = nullptr;
BOOL WINAPI rejectRelease(HMODULE image)
{
    ++failedReleases;
    failedImage = image;
    SetLastError(ERROR_BUSY);
    return FALSE;
}

void configured(Scene& scene, bool keepDefinitions, bool releaseFailure)
{
    ReplacementSubject subject(scene);
    subject.previous.emplace(requireValue(subject.kernel->registerRuntime("configured", *subject.previous)));
    const auto previousRuntime = subject.previous->id();
    const auto previousManifest = QDir(scene.root).relativeFilePath(subject.image(0).manifest).toStdString();
    const auto nextManifest = QDir(scene.root).relativeFilePath(subject.image(1).manifest).toStdString();
    auto schema = [](const std::string&) { return Result<void>::success(); };
    auto oldDefinitions = requireValue(ConfigurationDefinitions::create(
        {{{"org.cordis.plugin", {}, schema, {"business", previousManifest}}, *subject.previous}}));
    auto newDefinitions = requireValue(
        oldDefinitions->replacing({{{"org.cordis.plugin", {}, schema, {"business", nextManifest}}, *subject.next}}));
    auto driver = KernelReconcileDriver::configured(subject.scheduler, subject.kernel, oldDefinitions);
    const auto file = scene.root + "/configuration.yaml";
    auto session =
        std::make_shared<ConfigurationSession>(subject.scheduler, driver, std::make_shared<QtConfigurationWriter>(file),
                                               requireValue(readConfigurationFile(file)));
    auto text = [](const std::string& manifest) {
        return "formatVersion: 1\nmodules: {business: {manifest: " + manifest +
               "}}\nplugins: [{id: alpha, plugin: org.cordis.plugin, module: business, config: sync}, "
               "{id: beta, plugin: org.cordis.plugin, module: business, config: sync}]\n";
    };
    // Draft、目标和已终结任务均保留，不能将纯历史值误当成旧工厂拥有者。
    const auto initialDraft = requireValue(session->preview(text(previousManifest)));
    const auto initial = requireValue(session->apply(initialDraft));
    const auto initialResult = wait(initial.settled);
    CHECK(initialResult->hasValue() && initialResult->value().outcome == PlanOutcome::Applied);
    CHECK(wait(requireValue(session->save(1)))->hasValue());
    const auto initialDisk = requireValue(readConfigurationFile(file));
    const auto initialTarget = requireValue(session->snapshot()).coordinator.desired;
    const auto before = requireValue(driver->snapshot());
    const auto nativeBefore = requireValue(subject.host(0).snapshot()).instances;
    CHECK(before.size() == 2 && nativeBefore.size() == 2 && subject.live(0) == 2 && subject.live(1) == 0);
    const std::array<std::string, 2> values{{"11", "9007199254740993"}};
    std::map<InstanceId, std::string> previousValues;
    std::vector<Instance> oldInstances;
    for (unsigned i = 0; i != 2; ++i) {
        subject.setValue(0, i, values[i]);
        CHECK(subject.value(0, i) == values[i]);
        previousValues.emplace(nativeBefore[i].instance, values[i]);
        oldInstances.push_back(requireValue(subject.kernel->findInstance(nativeBefore[i].instance)));
    }
    const auto target = std::make_shared<const ValidatedCandidate>(
        requireValue(newDefinitions->validate(requireValue(parseConfiguration(text(nextManifest))))));
    auto operation = requireValue(ReplacementOperation::begin([&] {
        auto request = subject.request();
        request.configurations.push_back({session, driver, newDefinitions, target, 1});
        return request;
    }()));
    const auto settled = operation.settled();
    const auto result = wait(settled);
    CHECK(result->hasValue());
    const auto report = result->value();
    CHECK(report.outcome == ReplacementOutcome::Applied && report.committed && !report.recovered &&
          !report.resourcesRetained && report.errors.empty() && report.instances.size() == 2);
    subject.barrier();
    CHECK(subject.live(0) == 0 && subject.live(1) == 2);
    CHECK(scene.state(subject.image(0).generation).role == GenerationRole::Retired &&
          scene.state(subject.image(1).generation).role == GenerationRole::Current);
    CHECK(!scene.state(subject.image(0).generation).replacement &&
          !scene.state(subject.image(1).generation).replacement && subject.kernel->replacements().value().empty());
    CHECK(wait(requireValue(session->save(2)))->hasValue());
    const auto disk = requireValue(readConfigurationFile(file));
    CHECK(disk.version.exists && disk.text == target->source().normalizedYaml() &&
          disk.version.sha256 != initialDisk.version.sha256);
    const auto after = requireValue(driver->snapshot());
    auto committedDefinitions = driver->configurationDefinitions();
    const auto committedTarget = requireValue(session->snapshot()).coordinator.desired;
    CHECK(committedTarget && committedDefinitions->owns(*committedTarget) &&
          committedTarget->source().normalizedYaml() == target->source().normalizedYaml());

    auto intact = [&] {
        const auto current = requireValue(session->snapshot());
        CHECK(!current.closing && !current.closed && !current.deliveryBlocked && !current.coordinator.closing &&
              !current.coordinator.unsafe && !current.coordinator.deliveryBlocked);
        CHECK(current.coordinator.desiredRevision == 2 && current.coordinator.desired == committedTarget &&
              !current.coordinator.activeRevision && !current.coordinator.runningError && !current.executionError);
        CHECK(current.store.desiredRevision == 2 && current.store.desired == committedTarget &&
              current.store.persistedRevision == 2 && current.store.persisted == committedTarget &&
              !current.store.dirty && !current.store.writing && current.store.queuedWrites == 0 &&
              !current.store.lastError && !current.store.closing && !current.store.closed);
        CHECK(driver->configurationDefinitions() == committedDefinitions &&
              committedDefinitions->owns(*current.coordinator.desired));
        const auto kernel = requireValue(subject.kernel->snapshot());
        CHECK(!kernel.closing && !kernel.deliveryBlocked && kernel.runtimes == 1 && kernel.names.size() == 1 &&
              kernel.names.front().name == "configured" && kernel.names.front().id == subject.next->id() &&
              kernel.instances.size() == 2);
        const auto actualDisk = requireValue(readConfigurationFile(file));
        CHECK(actualDisk.text == disk.text && actualDisk.version.sha256 == disk.version.sha256 &&
              actualDisk.version.exists == disk.version.exists && actualDisk.version.path == disk.version.path);
        const auto rows = requireValue(driver->snapshot());
        const auto nativeRows = requireValue(subject.host(1).snapshot()).instances;
        CHECK(rows.size() == 2 && nativeRows.size() == 2 && subject.live(1) == 2 && after.size() == 2);
        std::set<std::string> actualValues{subject.value(1, 0), subject.value(1, 1)};
        CHECK(actualValues == (std::set<std::string>{values[0] + "0", values[1] + "0"}));
        for (std::size_t i = 0; i != rows.size(); ++i) {
            const auto& row = rows[i];
            CHECK(row.path == after[i].path && row.instance == after[i].instance && row.epoch == after[i].epoch &&
                  row.state == InstanceState::Active && row.configuration.plugin == "org.cordis.plugin" &&
                  row.configuration.module == "business" && row.configuration.config == "\"sync\"");
            const auto previous =
                std::find_if(before.begin(), before.end(), [&](const auto& entry) { return entry.path == row.path; });
            CHECK(previous != before.end());
            const auto mapping = std::find_if(report.instances.begin(), report.instances.end(),
                                              [&](const auto& entry) { return entry.previous == previous->instance; });
            CHECK(mapping != report.instances.end() && mapping->current == row.instance &&
                  mapping->current != mapping->previous);
            const auto actual = std::find_if(nativeRows.begin(), nativeRows.end(),
                                             [&](const auto& entry) { return entry.instance == row.instance; });
            CHECK(actual != nativeRows.end() && actual->state == NativeInstanceState::Started &&
                  actual->nativeCreated && !actual->uncertain && !actual->logs.empty() &&
                  actual->logs.back().text == previousValues.at(previous->instance) + "0");
            CHECK(requireValue(subject.kernel->findInstance(row.instance)).snapshot().value().state ==
                  InstanceState::Active);
        }
        for (const auto& instance : oldInstances)
            CHECK(requireValue(instance.snapshot()).state == InstanceState::Disposed);
        const auto latest = requireValue(operation.snapshot());
        CHECK(latest.phase == ReplacementPhase::Committed && latest.report.outcome == ReplacementOutcome::Applied &&
              latest.report.committed && !latest.report.recovered && !latest.report.resourcesRetained &&
              latest.report.errors.empty() && latest.report.operation == report.operation &&
              latest.report.previous == report.previous && latest.report.candidate == report.candidate &&
              latest.report.instances.size() == report.instances.size());
        for (std::size_t i = 0; i != report.instances.size(); ++i)
            CHECK(latest.report.instances[i].previous == report.instances[i].previous &&
                  latest.report.instances[i].current == report.instances[i].current);
        CHECK(settled.tryResult() == result && initial.settled.tryResult() == initialResult);
        CHECK(initialTarget->source().normalizedYaml() == initialDisk.text &&
              initialDraft.candidate().source().normalizedYaml() == initialDisk.text);
        CHECK(scene.state(subject.image(1).generation).role == GenerationRole::Current &&
              !scene.state(subject.image(1).generation).businessFailure);
    };
    intact();
    CHECK(wait(subject.host(0).close())->value().quiescent);
    subject.previous.reset();
    subject.barrier();
    if (keepDefinitions) {
        const auto held = scene.state(subject.image(0).generation);
        CHECK(oldDefinitions.use_count() == 1 && held.resources.pinReasons.empty() &&
              held.resources.records.size() == 1 &&
              held.resources.records.front().kind == ResourceKind::ExternalHandle &&
              held.resources.records.front().label == "runtime.definition.org.cordis.plugin");
        const auto origin = requireValue(scene.manager.runtimeOrigin(previousRuntime));
        CHECK(origin && origin->generation == subject.image(0).generation);
        scene.blocked(subject.image(0));
        CHECK(!scene.state(subject.image(0).generation).unloadFailure);
        intact();
    }
    oldDefinitions.reset();
    subject.barrier();
    CHECK(!requireValue(scene.manager.runtimeOrigin(previousRuntime)) &&
          scene.state(subject.image(0).generation).resources.records.empty());
    if (releaseFailure) {
        failedReleases = 0;
        failedImage = nullptr;
        native::NativeUnloadSystem system;
        system.release = rejectRelease;
        const auto failed = native::unloadGeneration(subject.image(0).generation, system);
        CHECK(!failed.hasValue() && failed.error().code == ErrorCode::ModulePinned && failedReleases == 1 &&
              failedImage == reinterpret_cast<HMODULE>(subject.image(0).base));
        const auto retained = scene.state(subject.image(0).generation);
        CHECK(retained.role == GenerationRole::Retired && retained.state == ModuleState::Resident &&
              retained.nativeMapped && !retained.nativePinned && retained.unloadFailure &&
              retained.unloadFailure->code == failed.error().code && !retained.businessFailure &&
              retained.resources.records.empty() && retained.resources.pinReasons.empty() &&
              !retained.resources.accepting && subject.image(0).witness->read() == 0);
        scene.protectedFile(subject.image(0));
        intact();
    }
    CHECK(scene.manager.tryUnload(subject.image(0).generation).hasValue());
    scene.gone(subject.image(0));
    intact();
    CHECK(requireValue(subject.host(0).snapshot()).closed && wait(subject.host(0).close())->value().quiescent);
    // 新 DLL 在旧映像离开后仍处理真实有状态业务，随后恢复探针值再复核所有配置历史。
    const auto firstValue = subject.value(1, 0);
    subject.setValue(1, 0, "321");
    CHECK(subject.value(1, 0) == "321");
    subject.setValue(1, 0, firstValue);
    CHECK(requireValue(session->preview(text(nextManifest))).baseRevision() == 2);
    CHECK(wait(requireValue(session->save(2)))->hasValue());
    intact();

    CHECK(wait(session->close())->hasValue());
    CHECK(wait(subject.kernel->close())->value().quiescent);
    CHECK(wait(subject.host(1).close())->value().quiescent);
    subject.barrier();
    session.reset();
    driver.reset();
    newDefinitions.reset();
    committedDefinitions.reset();
    subject.next.reset();
    subject.kernel.reset();
    subject.barrier();
    CHECK(scene.manager.tryUnload(subject.image(1).generation).hasValue());
    scene.gone(subject.image(1));
    CHECK(requireValue(readConfigurationFile(file)).text == disk.text);
    CHECK(settled.tryResult() == result &&
          requireValue(operation.snapshot()).report.outcome == ReplacementOutcome::Applied);
    CHECK(subject.scheduler->close().hasValue());
}
} // namespace

bool configuredReplacementCases(Scene& scene, const QString& name)
{
    if (name != "configured" && name != "configured_definitions" && name != "configured_unload_failure")
        return false;
    configured(scene, name == "configured_definitions", name == "configured_unload_failure");
    return true;
}
} // namespace unload_test
