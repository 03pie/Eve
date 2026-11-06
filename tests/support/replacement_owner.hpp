/** @file replacement_owner.hpp
 * @brief 实际拥有者协议的成功、创建前拒绝及恢复失败检查。
 */
#pragma once
#include <cordis/replacement_operation.hpp>
#include <functional>
class OperationRejectingScheduler final : public cordis::IScheduler {
public:
    explicit OperationRejectingScheduler(std::shared_ptr<cordis::IScheduler> delegate) : delegate_(std::move(delegate))
    {
    }
    std::atomic_bool reject{false};
    bool post(cordis::ScheduledJob job) override { return !reject.load() && delegate_->post(std::move(job)); }
    bool isCurrentThread() const noexcept override { return delegate_->isCurrentThread(); }
    bool isOpen() const override { return delegate_->isOpen(); }

private:
    std::shared_ptr<cordis::IScheduler> delegate_;
};
class OperationOwnerProbe final : public cordis::IReplacementOwner {
public:
    std::shared_ptr<cordis::IScheduler> scheduler;
    bool rejectCandidate = false, rejectRecovery = false;
    bool rejectRecoveryAfterCreate = false, dropCandidateDependencies = false, suspendCandidate = false;
    cordis::Promise<void> entered;
    cordis::Promise<cordis::ReplacementRebuiltTree> pending;
    int checked = 0, rebuilt = 0;
    std::function<void()> preflightHook;
    std::optional<cordis::ReplacementBuilder> observedBuilder;
    std::optional<cordis::Runtime> lastFactory;
    cordis::Result<cordis::ReplacementOwnerPreflight> preflight(const cordis::ReplacementBuildPlan& candidate,
                                                                const cordis::ReplacementBuildPlan& recovery) override
    {
        CHECK(candidate.members.size() == 1 && recovery.members.size() == 1);
        CHECK(candidate.members[0].previous && candidate.members[0].factory && recovery.members[0].factory);
        ++checked;
        if (preflightHook) {
            auto hook = std::move(preflightHook);
            preflightHook = {};
            hook();
        }
        return cordis::Result<cordis::ReplacementOwnerPreflight>::success({});
    }
    cordis::Task<cordis::ReplacementRebuiltTree> rebuild(cordis::ReplacementBuilder builder,
                                                         cordis::ReplacementBuildPlan plan) override
    {
        using namespace cordis;
        ++rebuilt;
        observedBuilder = builder;
        lastFactory = plan.members[0].factory;
        Promise<ReplacementRebuiltTree> done;
        if (!plan.recovery && suspendCandidate) {
            entered.resolve();
            return pending.task();
        }
        if ((plan.recovery && rejectRecovery) || (!plan.recovery && rejectCandidate)) {
            done.reject(makeError(ErrorCode::ActivationFailed, "owner refused before creation"));
            return done.task();
        }
        const auto& root = plan.members[0];
        auto created = builder.stage(*root.factory, root.config, root.realms,
                                     !plan.recovery && dropCandidateDependencies ? std::vector<ServiceRequirement>{}
                                                                                 : root.additionalDependencies);
        if (!created.hasValue()) {
            done.reject(created.error());
            return done.task();
        }
        const auto instance = created.value();
        auto receipt = instance.ready().thenOn(
            scheduler, [done, instance, key = root.key,
                        fail = plan.recovery && rejectRecoveryAfterCreate](const Result<void>& ready) {
                if (!ready.hasValue())
                    done.reject(ready.error());
                else if (fail)
                    done.reject(makeError(ErrorCode::ActivationFailed, "recovery failed after creation"));
                else
                    done.resolve({{{key, instance}}});
            });
        cordis::detail::TaskAccess::observe(receipt, [done](Task<void>::Snapshot result) {
            if (!result->hasValue())
                done.reject(result->error());
        });
        return done.task();
    }
};
template <class SceneType, class Wait>
bool operationOwnerCase(SceneType& scene, const cordis::Runtime& old, const cordis::Runtime& next,
                        const std::shared_ptr<cordis::IScheduler>& scheduler,
                        const std::shared_ptr<cordis::Kernel>& kernel, Wait waitFor, const QString& mode)
{
    using namespace cordis;
    std::vector<ServiceRequirement> additional;
    if (mode == "operation_owner_contract") {
        auto provider =
            kernel
                ->registerPlugin("owner-provider",
                                 [](Context context) {
                                     context.provide<int>({"owner.input"}, std::make_shared<int>(7)).value();
                                     return completedTask();
                                 },
                                 {}, {ServiceOutput::of<int>({"owner.input"})})
                .value();
        CHECK(waitFor(kernel->mount(provider).value().ready())->hasValue());
        additional.push_back(ServiceRequirement::of<int>({"owner.input"}));
    }
    auto root = kernel->mount(old, scene.qt ? "normal" : "\"sync\"", {}, additional).value();
    CHECK(waitFor(root.ready())->hasValue());
    auto owner = std::make_shared<OperationOwnerProbe>();
    owner->scheduler = scheduler;
    owner->rejectCandidate = mode != "operation_owner" && mode != "operation_owner_contract" &&
                             mode != "operation_owner_delivery" && mode != "operation_owner_kernel_close";
    owner->rejectRecovery = mode == "operation_owner_recovery_failure";
    owner->rejectRecoveryAfterCreate = mode == "operation_owner_recovery_after_create_failure";
    owner->dropCandidateDependencies = mode == "operation_owner_contract";
    owner->suspendCandidate = mode == "operation_owner_delivery" || mode == "operation_owner_kernel_close";
    ReplacementRequest request;
    request.scheduler = scheduler;
    request.kernel = kernel;
    request.modules = scene.managerOwner;
    request.previous = scene.current;
    request.candidate = scene.candidate;
    request.definitions.push_back({old.id(), next});
    request.owners.emplace(root.snapshot().value().instance, owner);
    auto started = ReplacementOperation::begin(std::move(request));
    if (!started.hasValue())
        throw std::runtime_error(started.error().message);
    auto operation = std::move(started).value();
    if (mode == "operation_owner_kernel_close_prepared" || mode == "operation_owner_kernel_close_stopping") {
        std::optional<Task<CleanupReport>> closed;
        if (mode.endsWith("_prepared"))
            closed = kernel->close();
        else
            CHECK(scheduler->post({[&closed, kernel] { closed = kernel->close(); }, {}}));
        const auto completed = waitFor(operation.settled());
        CHECK(closed && waitFor(*closed)->value().quiescent);
        CHECK(completed && completed->hasValue());
        CHECK(completed->value().outcome == ReplacementOutcome::Cancelled && !completed->value().resourcesRetained);
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
        CHECK(owner->rebuilt == 0);
        return false;
    }
    if (mode == "operation_owner_kernel_close") {
        CHECK(waitFor(owner->entered.task())->hasValue());
        CHECK(!operation.settled().isReady());
        CHECK(waitFor(kernel->close())->value().quiescent);
        auto completed = waitFor(operation.settled());
        CHECK(completed && completed->hasValue());
        CHECK(completed->value().outcome == ReplacementOutcome::Cancelled);
        CHECK(!completed->value().resourcesRetained && !completed->value().committed && !completed->value().recovered);
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
        CHECK(!owner->observedBuilder->stage(*owner->lastFactory).hasValue());
        owner->pending.reject(makeError(ErrorCode::ActivationFailed, "late owner completion"));
        CHECK(waitFor(operation.settled())->value().outcome == ReplacementOutcome::Cancelled);
        return false;
    }
    if (owner->suspendCandidate) {
        CHECK(waitFor(owner->entered.task())->hasValue());
        const auto gate = std::dynamic_pointer_cast<OperationRejectingScheduler>(scheduler);
        CHECK(gate);
        gate->reject.store(true);
        std::thread worker(
            [owner] { owner->pending.reject(makeError(ErrorCode::ActivationFailed, "worker finished")); });
        worker.join();
        gate->reject.store(false);
        const auto failed = operation.settled().tryResult();
        CHECK(failed && !failed->hasValue() && failed->error().code == ErrorCode::SchedulerClosed);
        CHECK(operation.snapshot().value().deliveryBlocked && operation.snapshot().value().report.resourcesRetained);
        CHECK(scheduler->isOpen() &&
              !owner->observedBuilder->stage(*owner->lastFactory, scene.qt ? "normal" : "\"sync\"").hasValue());
        CHECK(root.snapshot().value().state == InstanceState::Disposed);
        CHECK(waitFor(kernel->close())->value().quiescent);
        return true;
    }
    auto completed = waitFor(operation.settled());
    CHECK(completed && completed->hasValue() && owner->checked == 1);
    const auto report = completed->value();
    if (owner->rejectRecovery || owner->rejectRecoveryAfterCreate) {
        CHECK(report.outcome == ReplacementOutcome::Failed && report.resourcesRetained && !report.committed &&
              !report.recovered);
        CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                          [](const auto& error) { return error.code == ErrorCode::RecoveryFailed; }));
        CHECK(scene.state(scene.current).replacement && scene.state(scene.candidate).replacement);
        CHECK(!owner->observedBuilder->stage(*owner->lastFactory, scene.qt ? "normal" : "\"sync\"").hasValue());
        const auto resources = scene.state(scene.current).resources.records;
        CHECK(std::none_of(resources.begin(), resources.end(),
                           [](const auto& record) { return record.kind == module::ResourceKind::Instance; }));
        CHECK(waitFor(kernel->close())->value().quiescent);
        return true;
    }
    const bool restored = owner->rejectCandidate || owner->dropCandidateDependencies;
    if (report.outcome != (restored ? ReplacementOutcome::Restored : ReplacementOutcome::Applied))
        for (const auto& error : report.errors)
            std::cerr << error.message << " [" << error.phase << "]\n";
    CHECK(report.outcome == (restored ? ReplacementOutcome::Restored : ReplacementOutcome::Applied));
    CHECK(report.recovered == restored && !report.resourcesRetained);
    CHECK(owner->rebuilt == (restored ? 2 : 1));
    if (owner->dropCandidateDependencies)
        CHECK(kernel->findInstance(report.instances[0].current).value().dependencies().value().size() == 1);
    CHECK(report.instances.size() == 1 && report.instances[0].previous == root.snapshot().value().instance);
    CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
    CHECK(waitFor(kernel->close())->value().quiescent);
    return false;
}
