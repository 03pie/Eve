/** @file module_unload_lineage.cpp
 * @brief 真实候选装卸、当前代重建及原模块预约对同模块所有代次的保护。
 */
#include "module_unload_p.hpp"
#include <QCryptographicHash>

namespace unload_test {
namespace {
Artifact stage(Scene& scene, GenerationId predecessor, const QString& folder, bool distinct = false)
{
    const auto manifest = scene.prepare(6, folder);
    if (distinct) {
        // PE overlay 改变真实产物摘要；实际查询、模块 ID/版本和业务表仍由此 DLL 返回。
        const auto library = Scene::library(manifest);
        auto image = files::read(library);
        image.append("eve.unload.sibling");
        files::write(library, image);
        auto declared = QJsonDocument::fromJson(files::read(manifest)).object();
        declared["sha256"] = QString::fromLatin1(QCryptographicHash::hash(image, QCryptographicHash::Sha256).toHex());
        files::write(manifest, QJsonDocument(declared).toJson());
    }
    const auto created =
        scene.manager.stage(scene.root.toStdString(), manifest.toStdString(), predecessor, {}, scene.approval(manifest))
            .value();
    CHECK(!created.reused);
    auto artifact = scene.describe(created.generation, manifest);
    const auto current = scene.state(created.generation);
    CHECK(current.role == GenerationRole::Candidate && current.predecessor == predecessor &&
          current.manifest.moduleVersion == "2.0.0");
    CHECK(scene.call<int(EVE_CALL*)()>(artifact, "eve_fixture_queries") == 1);
    return artifact;
}
void unload(Scene& scene, const Artifact& artifact)
{
    CHECK(scene.manager.tryUnload(artifact.generation).hasValue());
    scene.gone(artifact);
}
void candidate(Scene& scene)
{
    const auto current = scene.load();
    const auto previous = stage(scene, current.generation, "next");
    const auto reused = scene.manager
                            .stage(scene.root.toStdString(), previous.manifest.toStdString(), current.generation, {},
                                   scene.approval(previous.manifest))
                            .value();
    CHECK(reused.reused && reused.generation == previous.generation);
    CHECK(scene.call<int(EVE_CALL*)()>(previous, "eve_fixture_queries") == 1);
    const auto history = scene.state(previous.generation);
    unload(scene, previous);
    CHECK(!scene.manager.reserveReplacement(current.generation, previous.generation).hasValue());
    CHECK(!native::acquireNativeSource(previous.generation).hasValue());
    const auto recreated = scene.manager
                               .stage(scene.root.toStdString(), previous.manifest.toStdString(), current.generation, {},
                                      scene.approval(previous.manifest))
                               .value();
    CHECK(!recreated.reused && recreated.generation != previous.generation &&
          recreated.generation != current.generation);
    const auto next = scene.describe(recreated.generation, previous.manifest);
    CHECK(scene.call<int(EVE_CALL*)()>(next, "eve_fixture_queries") == 1);
    CHECK(scene.state(next.generation).role == GenerationRole::Candidate &&
          scene.state(next.generation).predecessor == current.generation);
    CHECK(scene.state(current.generation).role == GenerationRole::Current &&
          scene.state(previous.generation).state == ModuleState::Unloaded &&
          history.role == GenerationRole::Candidate && history.generation == previous.generation);
    unload(scene, next);
    CHECK(previous.witness->read() == 1);
    unload(scene, current);
}
void current(Scene& scene)
{
    const auto previous = scene.load();
    const auto next = stage(scene, previous.generation, "next");
    unload(scene, previous);
    CHECK(scene.state(next.generation).role == GenerationRole::Candidate &&
          scene.state(next.generation).predecessor == previous.generation);
    const auto noCurrent = scene.manager.snapshot().value();
    CHECK(std::none_of(noCurrent.begin(), noCurrent.end(),
                       [](const auto& item) { return item.role == GenerationRole::Current; }));
    const auto loaded =
        scene.manager
            .load(scene.root.toStdString(), previous.manifest.toStdString(), {}, scene.approval(previous.manifest))
            .value();
    CHECK(!loaded.reused && loaded.generation != previous.generation && loaded.generation != next.generation);
    const auto recreated = scene.describe(loaded.generation, previous.manifest);
    CHECK(scene.call<int(EVE_CALL*)()>(recreated, "eve_fixture_queries") == 1);
    CHECK(scene.state(recreated.generation).role == GenerationRole::Current &&
          scene.state(next.generation).role == GenerationRole::Candidate &&
          scene.state(next.generation).predecessor == previous.generation);
    const auto stale = scene.manager.reserveReplacement(recreated.generation, next.generation);
    CHECK(!stale.hasValue() && stale.error().code == ErrorCode::ConfigConflict);
    unload(scene, next);
    unload(scene, recreated);
    CHECK(previous.witness->read() == 1);
}
void reservation(Scene& scene)
{
    const auto current = scene.load();
    const auto candidate = stage(scene, current.generation, "next");
    const auto sibling = stage(scene, current.generation, "sibling", true);
    CHECK(sibling.generation != candidate.generation && sibling.base != candidate.base &&
          scene.state(sibling.generation).manifest.sha256 != scene.state(candidate.generation).manifest.sha256);
    auto held = scene.manager.reserveReplacement(current.generation, candidate.generation).value();
    const auto pair = held.snapshot().value();
    CHECK(pair.current == current.generation && pair.candidate == candidate.generation);
    for (const auto& artifact : {current, candidate, sibling}) {
        const auto rejected = scene.manager.tryUnload(artifact.generation);
        CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::ConfigConflict);
        const auto state = scene.state(artifact.generation);
        CHECK(state.replacement && state.replacement->current == current.generation &&
              state.replacement->candidate == candidate.generation && state.resources.accepting &&
              state.state == ModuleState::Loaded && artifact.witness->read() == 0);
        scene.protectedFile(artifact);
    }
    held.reset();
    for (const auto& artifact : {current, candidate, sibling})
        CHECK(!scene.state(artifact.generation).replacement);
    unload(scene, sibling);
    unload(scene, candidate);
    unload(scene, current);
}
struct ReleaseCheck {
    HMODULE image = nullptr;
    std::function<void()> action;
    std::exception_ptr failure;
    bool entered = false;
};
thread_local ReleaseCheck* releaseCheck = nullptr;
BOOL WINAPI checkRelease(HMODULE image)
{
    auto* state = releaseCheck;
    if (state && state->image == image && !state->entered) {
        state->entered = true;
        try {
            auto action = std::move(state->action);
            state->action = {};
            action();
        } catch (...) {
            if (!state->failure)
                state->failure = std::current_exception();
        }
    }
    return FreeLibrary(image);
}
void sibling(Scene& scene)
{
    const auto current = scene.load();
    const auto removing = stage(scene, current.generation, "next");
    const auto other = stage(scene, current.generation, "sibling", true);
    CHECK(other.generation != removing.generation && other.base != removing.base &&
          scene.state(other.generation).manifest.sha256 != scene.state(removing.generation).manifest.sha256);
    auto before = scene.manager.reserveReplacement(current.generation, other.generation).value();
    before.reset();
    ReleaseCheck hook;
    hook.image = reinterpret_cast<HMODULE>(removing.base);
    hook.action = [&] {
        CHECK(scene.state(removing.generation).state == ModuleState::Unloading);
        CHECK(scene.state(current.generation).state == ModuleState::Loaded &&
              scene.state(other.generation).state == ModuleState::Loaded);
        const auto rejected = scene.manager.reserveReplacement(current.generation, other.generation);
        CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::ConfigConflict);
        CHECK(!scene.state(current.generation).replacement && !scene.state(other.generation).replacement);
    };
    struct Reset {
        ReleaseCheck* previous;
        ~Reset() { releaseCheck = previous; }
    } reset{std::exchange(releaseCheck, &hook)};
    native::NativeUnloadSystem system;
    system.release = checkRelease;
    const auto result = native::unloadGeneration(removing.generation, system);
    if (hook.failure)
        std::rethrow_exception(hook.failure);
    CHECK(hook.entered && result.hasValue());
    scene.gone(removing);
    auto after = scene.manager.reserveReplacement(current.generation, other.generation).value();
    CHECK(after.snapshot().value().candidate == other.generation);
    after.reset();
    unload(scene, other);
    unload(scene, current);
}
} // namespace
bool lineage(Scene& scene, const QString& name)
{
    if (name == "lineage_candidate")
        candidate(scene);
    else if (name == "lineage_current")
        current(scene);
    else if (name == "lineage_reservation")
        reservation(scene);
    else if (name == "lineage_sibling")
        sibling(scene);
    else
        return false;
    return true;
}
} // namespace unload_test
