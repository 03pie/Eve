/** @file rebuild.cpp
 * @brief 隔离创建拥有单元，等待真实就绪，核对全部旧成员与真实新子树的对应。
 */
#include "operation_p.hpp"
#include <algorithm>
namespace cordis::detail {
namespace {
bool sameAdditionalInputs(const std::vector<ServiceRequirement>& a, const std::vector<ServiceRequirement>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i != a.size(); ++i)
        if (a[i].key.name != b[i].key.name || a[i].key.interfaceMajor != b[i].key.interfaceMajor ||
            a[i].key.realm != b[i].key.realm || a[i].type != b[i].type || a[i].minimumMinor != b[i].minimumMinor ||
            a[i].capabilities != b[i].capabilities)
            return false;
    return true;
}
struct DefaultBuild : std::enable_shared_from_this<DefaultBuild> {
    std::shared_ptr<ReplacementOperationControl> operation;
    std::shared_ptr<const ReplacementBuildPlan> plan;
    ReplacementBuilder builder;
    ReplacementRebuiltTree tree;
    Promise<ReplacementRebuiltTree> done;
    std::size_t remaining;
    DefaultBuild(std::shared_ptr<ReplacementOperationControl> control,
                 std::shared_ptr<const ReplacementBuildPlan> input, ReplacementBuilder permission)
        : operation(std::move(control)), plan(std::move(input)), builder(std::move(permission)),
          remaining(plan->members.size())
    {
    }
    void create(std::size_t key, std::optional<Context> parent)
    {
        if (done.task().isReady())
            return;
        const auto item = std::find_if(plan->members.begin(), plan->members.end(),
                                       [&](const auto& member) { return member.key == key; });
        if (item == plan->members.end() || !item->factory) {
            done.reject(replacementError(ErrorCode::ConfigConflict, "missing default construction input"));
            return;
        }
        if (parent)
            for (const auto& realm : item->realms.overrides()) {
                auto isolated = parent->isolate(realm.first, realm.second);
                if (!isolated.hasValue()) {
                    done.reject(isolated.error());
                    return;
                }
                parent = std::move(isolated).value();
            }
        auto created = parent ? parent->inject(*item->factory, item->config, item->additionalDependencies)
                              : builder.stage(*item->factory, item->config, item->realms, item->additionalDependencies);
        if (!created.hasValue()) {
            done.reject(created.error());
            return;
        }
        const auto instance = created.value();
        tree.members.push_back({key, instance});
        operation->watch(instance.ready(), [self = shared_from_this(), key, instance](const Result<void>& ready) {
            if (!ready.hasValue()) {
                self->done.reject(ready.error());
                return;
            }
            auto context = instance.whenActiveAfter().tryResult();
            if (!context || !context->hasValue()) {
                self->done.reject(
                    replacementError(ErrorCode::ProvisionIncomplete, "ready instance has no active context"));
                return;
            }
            for (const auto& child : self->plan->members)
                if (child.parent && *child.parent == key)
                    self->create(child.key, context->value());
            if (--self->remaining == 0)
                self->done.resolve(std::move(self->tree));
        });
    }
};
} // namespace
void ReplacementOperationControl::build(bool restoring)
{
    if (CoreReplacementAccess::kernelClosing(*execution->request.kernel))
        return;
    cancelled = cancelled || sessionClosing();
    if (cancelled && !restoring) {
        recover();
        return;
    }
    recovery = restoring;
    phase = restoring ? ReplacementPhase::Recovering : ReplacementPhase::Building;
    ++sequence;
    auto sources = module::detail::ReplacementAccess::checkSources(execution->reservation, restoring);
    if (!sources.hasValue()) {
        fail(sources.error());
        return;
    }
    auto inputs = CoreReplacementAccess::activateInputs(*execution->scope, *execution->request.kernel, restoring);
    if (!inputs.hasValue()) {
        fail(inputs.error());
        return;
    }
    execution->built.clear();
    execution->preparedMappings.clear();
    pending = execution->units.size();
    if (!pending) {
        built();
        return;
    }
    auto ordered = planGraph(restoring);
    if (!ordered.hasValue()) {
        fail(ordered.error());
        return;
    }
    auto permission = CoreReplacementAccess::builder(*execution->scope, *execution->request.kernel);
    if (!permission.hasValue()) {
        fail(permission.error());
        return;
    }
    const auto builder = std::move(permission).value();
    const auto expected = sequence;
    for (const auto index : ordered.value()) {
        if (sequence != expected || delivery->terminal.load())
            return;
        if (CoreReplacementAccess::kernelClosing(*execution->request.kernel))
            return;
        const auto& unit = execution->units[index];
        if (!unit.owner) {
            buildDefault(index, builder);
            continue;
        }
        const auto plan = recovery ? unit.recovery : unit.candidate;
        auto task = unit.owner->rebuild(builder, plan);
        watch(task, [self = shared_from_this(), index](const Result<ReplacementRebuiltTree>& result) {
            if (!result.hasValue()) {
                self->fail(result.error());
                return;
            }
            const auto& unit = self->execution->units[index];
            self->acceptTree(self->recovery ? unit.recovery : unit.candidate, result.value());
        });
    }
}
void ReplacementOperationControl::buildDefault(std::size_t index, ReplacementBuilder builder)
{
    const auto& unit = execution->units[index];
    auto build = std::make_shared<DefaultBuild>(
        shared_from_this(), std::make_shared<const ReplacementBuildPlan>(recovery ? unit.recovery : unit.candidate),
        std::move(builder));
    const auto plan = build->plan;
    watch(build->done.task(), [self = shared_from_this(), index](const Result<ReplacementRebuiltTree>& result) {
        if (!result.hasValue()) {
            self->fail(result.error());
            return;
        }
        const auto& unit = self->execution->units[index];
        self->acceptTree(self->recovery ? unit.recovery : unit.candidate, result.value());
    });
    const auto root =
        std::find_if(plan->members.begin(), plan->members.end(), [](const auto& item) { return !item.parent; });
    if (root == plan->members.end()) {
        build->done.reject(replacementError(ErrorCode::ConfigConflict, "construction root is missing"));
        return;
    }
    build->create(root->key, {});
}
void ReplacementOperationControl::acceptTree(const ReplacementBuildPlan& plan, const ReplacementRebuiltTree& tree)
{
    if (tree.members.size() != plan.members.size()) {
        fail(replacementError(ErrorCode::ConfigConflict, "owner omitted reconstruction members"));
        return;
    }
    std::set<std::size_t> required;
    for (const auto& item : plan.members)
        required.insert(item.key);
    for (const auto& item : tree.members)
        if (!required.erase(item.key) || !execution->built.emplace(item.key, item.instance).second) {
            fail(replacementError(ErrorCode::ConfigConflict, "owner returned duplicate or foreign member keys"));
            return;
        }
    if (--pending == 0)
        built();
}
void ReplacementOperationControl::built()
{
    if (cancelled && !recovery) {
        fail(replacementError(ErrorCode::OperationCancelled, "replacement cancelled"));
        return;
    }
    auto prepared =
        execution->scope->prepareCommit(recovery ? ReplacementCommitMode::Recovery : ReplacementCommitMode::Candidate);
    if (!prepared.hasValue()) {
        fail(prepared.error());
        return;
    }
    auto valid = validateBuilt();
    if (!valid.hasValue()) {
        fail(valid.error());
        return;
    }
    commit();
}
Result<void> ReplacementOperationControl::validateBuilt()
{
    auto actual = CoreReplacementAccess::inspect(*execution->scope, *execution->request.kernel);
    if (!actual.hasValue())
        return Result<void>::failure(actual.error());
    std::map<InstanceId, const ReplacementInput*> inputs;
    for (const auto& member : actual.value().members)
        inputs.emplace(member.input.instance, &member.input);
    std::map<std::size_t, InstanceId> identities;
    std::set<InstanceId> used;
    for (const auto& item : execution->built) {
        auto snapshot = item.second.snapshot();
        if (!snapshot.hasValue())
            return Result<void>::failure(snapshot.error());
        if (!inputs.count(snapshot.value().instance) || !used.insert(snapshot.value().instance).second)
            return Result<void>::failure(
                replacementError(ErrorCode::ConfigConflict, "owner result is not a unique actual candidate"));
        identities.emplace(item.first, snapshot.value().instance);
    }
    execution->preparedMappings.clear();
    for (const auto& unit : execution->units)
        for (const auto& item : (recovery ? unit.recovery : unit.candidate).members) {
            const auto found = identities.find(item.key);
            if (found == identities.end())
                return Result<void>::failure(
                    replacementError(ErrorCode::ConfigConflict, "required candidate mapping is missing"));
            const auto& input = *inputs.at(found->second);
            const auto parent = item.parent ? identities.at(*item.parent) : InstanceId{};
            if (input.parent != parent || (item.factory && input.runtime != item.factory->id()))
                return Result<void>::failure(
                    replacementError(ErrorCode::ConfigConflict, "candidate factory or actual parent differs"));
            if (input.config != item.config || input.realms.overrides() != item.realms.overrides())
                return Result<void>::failure(
                    replacementError(ErrorCode::ConfigConflict, "candidate configuration or realm differs"));
            if (item.factory && !sameAdditionalInputs(input.additionalDependencies, item.additionalDependencies))
                return Result<void>::failure(
                    replacementError(ErrorCode::ConfigConflict, "candidate additional dependencies differ"));
            if (!item.contract)
                return Result<void>::failure(replacementError(ErrorCode::ConfigConflict, "future contract is missing"));
            auto actualContract = serviceContract(input);
            if (!actualContract.hasValue())
                return Result<void>::failure(actualContract.error());
            if (!sameContract(*item.contract, actualContract.value()))
                return Result<void>::failure(
                    replacementError(ErrorCode::ConfigConflict, "actual service contract differs from preflight"));
            auto origin = factorySource(*execution->request.modules, input.runtime);
            if (!origin.hasValue())
                return Result<void>::failure(origin.error());
            if (origin.value() != item.source)
                return Result<void>::failure(
                    replacementError(ErrorCode::ConfigConflict, "actual factory source differs from preflight"));
            if (item.previous)
                execution->preparedMappings.push_back({item.previous->instance, input.instance});
        }
    if (inputs.size() != used.size())
        return Result<void>::failure(replacementError(ErrorCode::ConfigConflict, "undeclared reconstruction members"));
    return Result<void>::success();
}
} // namespace cordis::detail
