/** @file module_unload_replacement.cpp
 * @brief 真实状态替换终结后回收退役代；恢复与物理失败继续使用原事务及真实拥有者。
 */
#include "module_unload_replacement_p.hpp"

#include "../support/replacement_state_tree.hpp"
#include <set>

namespace unload_test {
namespace {
namespace tree = replacement_state_tree;
using StateHook = void(EVE_CALL*)(std::uint32_t);
std::array<std::function<void(std::uint32_t)>, 2> stateActions;
std::exception_ptr stateFailure;
template <class T> T phaseValue(Result<T> result, const char* phase)
{
    if (!result.hasValue())
        throw std::runtime_error(std::string(phase) + ": " + result.error().phase + ": " + result.error().message);
    return std::move(result).value();
}
void phaseValue(Result<void> result, const char* phase)
{
    if (!result.hasValue())
        throw std::runtime_error(std::string(phase) + ": " + result.error().phase + ": " + result.error().message);
}
void stateCallback(unsigned image, std::uint32_t phase)
{
    try {
        const auto action = stateActions[image];
        if (action)
            action(phase);
    } catch (...) {
        if (!stateFailure)
            stateFailure = std::current_exception();
    }
}
void EVE_CALL previousCallback(std::uint32_t phase)
{
    stateCallback(0, phase);
}
void EVE_CALL candidateCallback(std::uint32_t phase)
{
    stateCallback(1, phase);
}
void observe(ReplacementSubject& subject, unsigned image, std::function<void(std::uint32_t)> action)
{
    stateActions[image] = std::move(action);
    const StateHook callback = stateActions[image] ? (image ? candidateCallback : previousCallback) : nullptr;
    subject.scene.call<void(EVE_CALL*)(StateHook)>(subject.image(image), "eve_state_set_hook", callback);
}
void stopObserving(ReplacementSubject& subject)
{
    observe(subject, 0, {});
    observe(subject, 1, {});
    if (stateFailure)
        std::rethrow_exception(stateFailure);
}

struct StatefulTree {
    ReplacementSubject& subject;
    std::shared_ptr<tree::Observations> seen = std::make_shared<tree::Observations>();
    std::shared_ptr<tree::Owner> owner;
    std::vector<Instance> originals;
    std::vector<InstanceId> nativeIdentities;
    std::vector<std::shared_ptr<tree::Value>> builtinOriginals;
    const std::array<std::string, 2> nativeValues{{"11", "9007199254740993"}};

    explicit StatefulTree(ReplacementSubject& input)
        : subject(input), owner(std::make_shared<tree::Owner>(subject.scheduler))
    {
        subject.previous.emplace(requireValue(subject.kernel->registerRuntime("unload.state", *subject.previous)));
        owner->observations = seen;
        for (unsigned i = 0; i != 2; ++i) {
            const auto root = requireValue(subject.kernel->mount(*subject.previous, "\"sync\""));
            CHECK(wait(root.ready())->hasValue());
            originals.push_back(root);
            nativeIdentities.push_back(root.snapshot().value().instance);
            subject.setValue(0, i, nativeValues[i]);
        }
        const auto root = originals.front().whenActiveAfter().tryResult()->value();
        for (const std::string name : {"alpha", "beta"}) {
            auto original = tree::counter(seen, 1, name == "alpha");
            owner->recovery.emplace(name, original);
            owner->candidate.emplace(name, tree::counter(seen, 2, name == "alpha"));
            const auto child = requireValue(root.inject(original, name));
            CHECK(wait(child.ready())->hasValue());
            originals.push_back(child);
            builtinOriginals.push_back(seen->latest(name));
        }
        builtinOriginals[0]->json = "31";
        builtinOriginals[1]->json = "71";
        const auto consumer =
            requireValue(subject.kernel->registerRuntime("unload.consumer", tree::counter(seen, 1, false, true)));
        const auto child = requireValue(subject.kernel->mount(consumer, "consumer"));
        CHECK(wait(child.ready())->hasValue());
        originals.push_back(child);
        builtinOriginals.push_back(seen->latest("consumer"));
        builtinOriginals.back()->json = "93";
        CHECK(seen->latest("consumer")->dependency == "31");
        CHECK(subject.live(0) == 2 && subject.live(1) == 0);
        for (unsigned i = 0; i != 2; ++i) {
            const auto state = subject.scene.state(subject.image(i).generation);
            CHECK(state.loadPolicy == ModuleLoadPolicy::ApprovedNative && !state.nativePinned &&
                  state.resources.pinReasons.empty());
        }
    }
    ReplacementRequest request() const
    {
        auto result = subject.request();
        result.owners.emplace(nativeIdentities.front(), owner);
        return result;
    }
    void roles(bool committed, bool retained) const
    {
        CHECK(subject.kernel->replacements().value().empty() == !retained);
        for (unsigned i = 0; i != 2; ++i) {
            const auto current = subject.scene.state(subject.image(i).generation);
            CHECK(current.role == (i == 0 ? (committed ? GenerationRole::Retired : GenerationRole::Current)
                                          : (committed ? GenerationRole::Current : GenerationRole::Candidate)));
            CHECK(current.replacement.has_value() == retained);
        }
    }
    void mapped(const ReplacementReport& report, bool candidate) const
    {
        CHECK(report.instances.size() == originals.size());
        const unsigned image = candidate ? 1U : 0U;
        const auto actual = subject.modules[image]->snapshot().value().instances;
        CHECK(actual.size() == nativeIdentities.size() && subject.live(image) == nativeIdentities.size());
        for (unsigned i = 0; i != nativeIdentities.size(); ++i) {
            const auto mapping = std::find_if(report.instances.begin(), report.instances.end(),
                                              [&](const auto& row) { return row.previous == nativeIdentities[i]; });
            CHECK(mapping != report.instances.end() && mapping->current != mapping->previous);
            const auto native = std::find_if(actual.begin(), actual.end(),
                                             [&](const auto& row) { return row.instance == mapping->current; });
            CHECK(native != actual.end() && native->nativeCreated && !native->uncertain &&
                  native->state == NativeInstanceState::Started && !native->logs.empty());
            CHECK(native->logs.back().text == nativeValues[i] + (candidate ? "0" : ""));
            CHECK(phaseValue(subject.kernel->findInstance(mapping->current), "mapped native root")
                      .snapshot()
                      .value()
                      .state == InstanceState::Active);
        }
        const std::multiset<std::string> fields{subject.value(image, 0), subject.value(image, 1)};
        CHECK(fields == (std::multiset<std::string>{nativeValues[0] + (candidate ? "0" : ""),
                                                    nativeValues[1] + (candidate ? "0" : "")}));
        const auto rootMapping = std::find_if(report.instances.begin(), report.instances.end(), [&](const auto& row) {
            return row.previous == nativeIdentities.front();
        });
        CHECK(rootMapping != report.instances.end());
        CHECK(phaseValue(subject.kernel->findInstance(rootMapping->current), "mapped owner root")
                  .snapshot()
                  .value()
                  .children == 2);
        const auto graph = phaseValue(subject.kernel->services().dependencyGraph(), "mapped actual service graph");
        CHECK(graph.instances.size() == originals.size());
        for (const auto& original : builtinOriginals) {
            const auto mapping = std::find_if(report.instances.begin(), report.instances.end(), [&](const auto& row) {
                return row.previous == original->identity.instance;
            });
            CHECK(mapping != report.instances.end() && mapping->current != mapping->previous);
            const auto current = seen->latest(original->name);
            CHECK(current->identity.instance == mapping->current && current->identity.epoch == 1 &&
                  current->restores == 1);
            const bool upgraded = candidate && original->name != "consumer";
            CHECK(current->version == (upgraded ? 2U : 1U));
            CHECK(current->observed == original->json + (upgraded ? "0" : ""));
            const auto member = std::find_if(graph.instances.begin(), graph.instances.end(),
                                             [&](const auto& row) { return row.instance == mapping->current; });
            CHECK(member != graph.instances.end() && !member->publicationHeld);
            if (original->name == "consumer") {
                const auto instance =
                    phaseValue(subject.kernel->findInstance(mapping->current), "mapped consumer root");
                CHECK(instance.snapshot().value().state == InstanceState::Active &&
                      instance.ready().tryResult()->hasValue());
            } else {
                CHECK(current->ready); // Owner 观察动态成员的真实 ready；根由默认 builder 观察。
                CHECK(member->parent == rootMapping->current); // 动态子实例属于实际父，不属于 Kernel 根目录。
            }
        }
        CHECK(seen->latest("consumer")->dependency == (candidate ? "310" : "31"));
        for (const auto& original : originals)
            CHECK(original.snapshot().value().state == InstanceState::Disposed);
    }
    void continuingConsumer() const
    {
        const auto value = seen->latest("consumer");
        const auto instance = phaseValue(subject.kernel->findInstance(value->identity.instance), "consumer instance");
        const auto active = instance.whenActiveAfter().tryResult();
        CHECK(active);
        const auto context = phaseValue(*active, "consumer active context");
        std::string actual;
        const auto service = phaseValue(context.consume<tree::Value>({"state.tree"}), "consumer committed service");
        phaseValue(service.withService([&](tree::Value& provider) { actual = provider.json; }),
                   "consumer service call");
        CHECK(actual == seen->latest("alpha")->json && actual == value->dependency);
    }
};

void sourceEmpty(const ReplacementSubject& subject, unsigned image)
{
    const auto generation = native::findGeneration(subject.image(image).generation);
    CHECK(generation);
    std::lock_guard<std::mutex> lock(generation->mutex);
    CHECK(generation->runtimeOrigins.empty() && generation->instanceOrigins.empty());
    CHECK(generation->ledger.snapshot().records.empty());
}
void releaseHost(ReplacementSubject& subject, unsigned image)
{
    const auto closed = wait(subject.host(image).close());
    CHECK(closed && closed->hasValue() && closed->value().quiescent);
    CHECK(subject.host(image).snapshot().value().closed);
    (image ? subject.next : subject.previous).reset();
    subject.barrier();
    sourceEmpty(subject, image);
}
void cleanServing(ReplacementSubject& subject, unsigned image)
{
    CHECK(phaseValue(*wait(subject.kernel->close()), "serving kernel close").quiescent);
    CHECK(phaseValue(*wait(subject.host(image).close()), "serving native host close").quiescent);
    subject.previous.reset();
    subject.next.reset();
    subject.kernel.reset(); // 原 canonical Runtime 的真正拥有者，close 不撤销定义。
    subject.barrier();
    sourceEmpty(subject, image);
    CHECK(subject.scene.manager.tryUnload(subject.image(image).generation).hasValue());
    subject.scene.gone(subject.image(image));
    CHECK(subject.scheduler->close().hasValue());
}
void retained(StatefulTree& state)
{
    state.roles(false, true);
    for (unsigned i = 0; i != 2; ++i) {
        const auto& image = state.subject.image(i);
        const auto blocked = state.subject.scene.manager.tryUnload(image.generation);
        CHECK(!blocked.hasValue() && blocked.error().code == ErrorCode::ConfigConflict);
        CHECK(state.subject.scene.state(image.generation).nativeMapped && image.witness->read() == 0);
        state.subject.scene.protectedFile(image);
    }
}
BOOL WINAPI refuseRelease(HMODULE)
{
    SetLastError(ERROR_BUSY);
    return FALSE;
}
void applied(Scene& scene, bool retry)
{
    ReplacementSubject subject(scene);
    StatefulTree state(subject);
    auto operation = requireValue(ReplacementOperation::begin(state.request()));
    const auto completion = operation.settled();
    const auto result = wait(completion);
    CHECK(result && result->hasValue());
    const auto report = result->value();
    CHECK(report.outcome == ReplacementOutcome::Applied && report.committed && !report.recovered &&
          !report.resourcesRetained);
    subject.barrier();
    state.roles(true, false);
    state.mapped(report, true);
    CHECK(subject.live(0) == 0 && subject.count(0, 5) == 2 && subject.count(0, 6) == 2);
    scene.blocked(subject.image(0)); // 宿主仍明确持有旧 Runtime/活动会话。
    releaseHost(subject, 0);
    if (retry) {
        native::NativeUnloadSystem system;
        system.release = refuseRelease;
        CHECK(!native::unloadGeneration(subject.image(0).generation, system).hasValue());
        const auto failed = scene.state(subject.image(0).generation);
        CHECK(failed.role == GenerationRole::Retired && failed.nativeMapped && !failed.nativePinned &&
              failed.unloadFailure && failed.unloadFailure->phase == "module.unload");
        CHECK(subject.image(0).witness->read() == 0 && failed.resources.records.empty());
        scene.protectedFile(subject.image(0));
        const auto original = native::findGeneration(subject.image(0).generation);
        CHECK(original && original->image && original->artifactFile);
        CHECK(!failed.resources.accepting);
        state.mapped(report, true);
        state.continuingConsumer();
    }
    CHECK(scene.manager.tryUnload(subject.image(0).generation).hasValue());
    scene.gone(subject.image(0));
    state.mapped(report, true);
    state.continuingConsumer();
    subject.setValue(1, 0, "555");
    CHECK(subject.value(1, 0) == "555" && subject.live(1) == 2);
    CHECK(operation.snapshot().value().report.outcome == ReplacementOutcome::Applied &&
          operation.snapshot().value().report.committed && completion.tryResult()->value().committed);
    CHECK(operation.snapshot().value().report.instances.size() == report.instances.size());
    for (std::size_t i = 0; i != report.instances.size(); ++i) {
        const auto snapshot = operation.snapshot().value().report.instances[i];
        CHECK(snapshot.previous == report.instances[i].previous && snapshot.current == report.instances[i].current);
    }
    CHECK(wait(subject.host(0).close())->value().quiescent); // 保留的 closed facade 安全且无映像权限。
    cleanServing(subject, 1);
}
void recover(Scene& scene, const QString& name)
{
    ReplacementSubject subject(scene);
    StatefulTree state(subject);
    const bool cancelled = name == "replacement_cancel_restore";
    const bool failed = name == "replacement_recovery_failure";
    if (name == "replacement_restore_reject")
        subject.mode(1, 11);
    else if (name == "replacement_start_failure")
        subject.mode(1, 18);
    else if (failed)
        state.seen->failConsumers = 2;
    std::optional<ReplacementOperation> operation;
    bool sawRecovery = false, cancelAccepted = false;
    observe(subject, 0, [&](std::uint32_t phase) {
        if (phase != 2)
            return;
        CHECK(operation && operation->snapshot().value().phase == ReplacementPhase::Recovering);
        retained(state);
        sawRecovery = true;
    });
    if (cancelled)
        observe(subject, 1, [&](std::uint32_t phase) {
            if (phase == 2 && !cancelAccepted)
                cancelAccepted = operation->cancel().hasValue();
        });
    operation.emplace(requireValue(ReplacementOperation::begin(state.request())));
    const auto completion = operation->settled();
    const auto completed = wait(completion);
    stopObserving(subject);
    CHECK(completed && completed->hasValue() && sawRecovery);
    const auto report = completed->value();
    CHECK(!report.committed && report.recovered == !failed && report.resourcesRetained == failed);
    CHECK(report.outcome == (failed      ? ReplacementOutcome::Failed
                             : cancelled ? ReplacementOutcome::Cancelled
                                         : ReplacementOutcome::Restored));
    subject.barrier();
    if (failed) {
        CHECK(tree::hasError(report, ErrorCode::RecoveryFailed) && tree::hasError(report, ErrorCode::ActivationFailed));
        CHECK(report.instances.empty() && state.seen->failConsumers == 0 && state.owner->rebuilt == 2);
        retained(state);
        for (unsigned i = 0; i != 2; ++i) {
            const auto generation = scene.state(subject.image(i).generation);
            CHECK(!generation.nativePinned && generation.resources.pinReasons.empty() && !generation.businessFailure);
            CHECK(subject.host(i).snapshot().value().instances.empty() && subject.live(i) == 0);
            CHECK(scene.records(subject.image(i).generation, ResourceKind::Instance) == 0);
        }
        const auto consumer = state.seen->latest("consumer");
        CHECK(consumer->observed == "93" && consumer->dependency == "31" && consumer->restores == 1);
        CHECK(!operation->snapshot().value().deliveryBlocked);
        return; // 原 self-pin/Execution/预约拥有失败资源；不代替原宿主解除。
    }
    CHECK(!cancelled || cancelAccepted);
    CHECK(cancelled || tree::hasError(report, name == "replacement_start_failure" ? ErrorCode::ActivationFailed
                                                                                  : ErrorCode::ConfigInvalid));
    state.roles(false, false);
    state.mapped(report, false);
    CHECK(subject.live(1) == 0 && subject.count(1, 6) > 0);
    CHECK(!subject.host(0).snapshot().value().faulted && !subject.host(1).snapshot().value().faulted);
    releaseHost(subject, 1);
    CHECK(scene.manager.tryUnload(subject.image(1).generation).hasValue());
    scene.gone(subject.image(1));
    state.mapped(report, false);
    state.continuingConsumer();
    subject.setValue(0, 0, "23");
    CHECK(subject.value(0, 0) == "23" && subject.live(0) == 2);
    CHECK(operation->snapshot().value().report.recovered && completion.tryResult()->value().recovered);
    cleanServing(subject, 0);
}
void uncertainRestore(Scene& scene)
{
    ReplacementSubject subject(scene);
    StatefulTree state(subject);
    subject.mode(1, 18);
    subject.mode(0, 12);
    std::optional<ReplacementOperation> operation;
    bool sawRecovery = false;
    observe(subject, 0, [&](std::uint32_t phase) {
        if (phase != 2)
            return;
        CHECK(operation && operation->snapshot().value().phase == ReplacementPhase::Recovering);
        retained(state);
        sawRecovery = true;
    });
    operation.emplace(requireValue(ReplacementOperation::begin(state.request())));
    const auto completion = operation->settled();
    const auto report = wait(completion)->value();
    stopObserving(subject);
    CHECK(report.outcome == ReplacementOutcome::Failed && !report.committed && !report.recovered &&
          report.resourcesRetained && report.instances.empty() && tree::hasError(report, ErrorCode::AbiProtocol) &&
          tree::hasError(report, ErrorCode::RecoveryFailed) && sawRecovery);
    subject.barrier();
    retained(state);
    const auto generation = scene.state(subject.image(0).generation);
    CHECK(generation.state == ModuleState::Failed && !generation.nativePinned && generation.businessFailure &&
          generation.businessFailure->code == ErrorCode::AbiProtocol);
    CHECK(std::find(generation.resources.pinReasons.begin(), generation.resources.pinReasons.end(),
                    "nativeInstanceUncertain") != generation.resources.pinReasons.end());
    CHECK(scene.records(subject.image(0).generation, ResourceKind::Instance) > 0 && subject.live(0) > 0);
    const auto actual = subject.host(0).snapshot().value().instances;
    CHECK(std::any_of(actual.begin(), actual.end(), [](const auto& instance) {
        return instance.nativeCreated && instance.uncertain && instance.state == NativeInstanceState::Failed &&
               instance.failure && instance.failure->code == ErrorCode::AbiProtocol;
    }));
    CHECK(subject.live(1) == 0 && subject.count(0, 6) == 2);
    CHECK(!wait(subject.kernel->close())->value().quiescent);
    CHECK(scene.state(subject.image(0).generation).nativeMapped && subject.image(0).witness->read() == 0);
}
} // namespace
bool replacementCases(Scene& scene, const QString& name)
{
    if (name == "replacement_applied" || name == "replacement_release_retry")
        applied(scene, name == "replacement_release_retry");
    else if (name == "replacement_restore_reject" || name == "replacement_start_failure" ||
             name == "replacement_cancel_restore" || name == "replacement_recovery_failure")
        recover(scene, name);
    else if (name == "replacement_restore_pending")
        uncertainRestore(scene);
    else
        return false;
    return true;
}
} // namespace unload_test
