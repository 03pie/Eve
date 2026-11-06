/** @file operation_p.hpp
 * @brief 唯一宿主替换状态；预检、创建和提交分别实现，不复制低层运行账目。
 */
#pragma once
#include "../core/instance_ownership_p.hpp"
#include "../loader/configuration_commit_p.hpp"
#include "../loader/replacement_target_p.hpp"
#include "../module/replacement_commit_p.hpp"
#include <atomic>
#include <cordis/replacement_operation.hpp>
#include <deque>
#include <set>
#include <thread>
namespace cordis::detail {
struct OperationDelivery {
    std::atomic_bool blocked{false}, terminal{false};
    std::shared_ptr<std::atomic_bool> scopeFailure;
    Promise<ReplacementReport> done;
    void fail(ErrorInfo error)
    {
        if (terminal.load())
            return;
        blocked.store(true);
        if (scopeFailure)
            scopeFailure->store(true);
        done.reject(std::move(error));
    }
};
struct ConfigurationWork {
    loader::detail::DriverDefinitionChange change;
    std::shared_ptr<const loader::ValidatedCandidate> original;
    std::set<std::string> paths;
};
struct BuildUnit {
    InstanceId original;
    ReplacementBuildPlan candidate, recovery;
    std::shared_ptr<IReplacementOwner> owner;
};
Result<void> planConstruction(ReplacementBuildPlan&, const std::vector<ReplacementDynamicMember>&,
                              std::size_t& nextKey);
Result<ReplacementServiceContract> serviceContract(const Runtime&, const std::vector<ServiceRequirement>&,
                                                   const RealmMap&);
Result<ReplacementServiceContract> serviceContract(const ReplacementInput&);
bool sameContract(const ReplacementServiceContract&, const ReplacementServiceContract&);
Result<std::optional<module::ModuleFactorySource>> factorySource(const module::ModuleManager&, RuntimeId);
struct ConfiguredSource {
    std::size_t participant;
    std::string path;
    InstanceId previous;
};
struct Execution {
    ReplacementRequest request;
    std::shared_ptr<ReplacementScope> scope;
    module::ReplacementReservation reservation;
    OriginalReplacementView original;
    std::vector<ConfigurationWork> configurations;
    std::vector<loader::detail::DriverReplacementClaim> driverClaims;
    std::vector<BuildUnit> units;
    std::vector<std::optional<ConfiguredSource>> configured;
    std::map<std::size_t, Instance> built;
    std::vector<ReplacementInstanceMapping> preparedMappings;
    std::vector<loader::detail::SessionReplacementHold> sessions;
    std::shared_ptr<module::detail::ReplacementSourceEvents> sourceEvents;
};
struct ReplacementOperationControl : std::enable_shared_from_this<ReplacementOperationControl> {
    std::unique_ptr<Execution> execution;
    std::shared_ptr<OperationDelivery> delivery = std::make_shared<OperationDelivery>();
    std::shared_ptr<ReplacementOperationControl> pin;
    std::thread::id owner = std::this_thread::get_id();
    ReplacementPhase phase = ReplacementPhase::Prepared;
    ReplacementReport report;
    bool cancelled = false, recovery = false;
    bool draining = false;
    std::deque<std::function<void()>> jobs;
    std::size_t sequence = 0, pending = 0;
    Result<void> prepare(ReplacementRequest);
    Result<std::vector<std::size_t>> planGraph(bool restoring,
                                               CoreReplacementAccess::InputBindings* inputs = nullptr) const;
    Result<void> describeSources(ReplacementBuildPlan&) const;
    Result<void> sealSources();
    Result<void> borrowInputs();
    Result<void> releaseClaims();
    ErrorInfo abortPreparation(ErrorInfo);
    void observeKernelClose();
    void observeSessionClose();
    void observeInputs();
    bool sessionClosing() const;
    void cancel();
    void start();
    void recover();
    void dispatch(std::function<void()>);
    void build(bool restoring);
    void buildDefault(std::size_t, ReplacementBuilder);
    void acceptTree(const ReplacementBuildPlan&, const ReplacementRebuiltTree&);
    void built();
    Result<void> validateBuilt();
    void commit();
    void fail(ErrorInfo);
    void settle(ReplacementOutcome, bool retained);
    template <class T, class F> void watchInterruption(const Task<T>& task, F callback)
    {
        const auto weak = weak_from_this();
        const auto gate = delivery;
        const auto scheduler = execution->request.scheduler;
        TaskAccess::observe(task, [weak, gate, scheduler, callback](typename Task<T>::Snapshot result) {
            if (gate->terminal.load())
                return;
            try {
                if (scheduler->post({[weak, callback, result] {
                                         if (auto self = weak.lock())
                                             self->dispatch([self, callback, result] { callback(*self, *result); });
                                     },
                                     [gate] {
                                         gate->fail(makeError(ErrorCode::SchedulerClosed,
                                                              "replacement interruption discarded"));
                                     }}))
                    return;
            } catch (...) {
            }
            gate->fail(makeError(ErrorCode::SchedulerClosed, "replacement interruption rejected"));
        });
    }
    template <class T, class F> void watch(const Task<T>& task, F callback)
    {
        const auto self = shared_from_this();
        const auto expected = sequence;
        const auto gate = delivery;
        const auto scheduler = execution->request.scheduler;
        TaskAccess::observe(task, [self, scheduler, expected, gate,
                                   callback = std::move(callback)](typename Task<T>::Snapshot result) mutable {
            if (gate->terminal.load())
                return;
            try {
                if (scheduler->post(
                        {[self, expected, callback, result]() mutable {
                             self->dispatch([self, expected, callback = std::move(callback), result]() mutable {
                                 if (self->sequence != expected || self->delivery->blocked.load())
                                     return;
                                 if (CoreReplacementAccess::kernelClosing(*self->execution->request.kernel))
                                     return;
                                 self->cancelled = self->cancelled || self->sessionClosing();
                                 callback(*result);
                             });
                         },
                         [gate] {
                             gate->fail(makeError(ErrorCode::SchedulerClosed, "replacement continuation discarded"));
                         }}))
                    return;
            } catch (...) {
            }
            gate->fail(makeError(ErrorCode::SchedulerClosed, "replacement continuation rejected"));
        });
    }
};
inline ErrorInfo replacementError(ErrorCode code, const char* message)
{
    auto error = makeError(code, message);
    error.phase = "replacement.operation";
    return error;
}
} // namespace cordis::detail
