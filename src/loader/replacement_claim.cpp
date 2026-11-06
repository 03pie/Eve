/** @file replacement_claim.cpp
 * @brief 规范路径/模块占用与普通目标门禁，复用配置预览，不授予运行权限。
 */
#include "replacement_claim_p.hpp"
#include <atomic>
#include <map>
#include <thread>
namespace cordis::loader::detail {
namespace {
bool overlaps(const std::set<std::string>& roots, const std::string& path)
{
    for (auto current = path; !current.empty();) {
        if (roots.count(current))
            return true;
        const auto slash = current.rfind('/');
        if (slash == std::string::npos)
            break;
        current.resize(slash);
    }
    const auto prefix = path + '/';
    const auto descendant = roots.lower_bound(prefix);
    return descendant != roots.end() && descendant->compare(0, prefix.size(), prefix) == 0;
}
ErrorInfo conflict(OperationId operation, const std::string& path = {})
{
    auto error = makeError(ErrorCode::ConfigConflict, "configuration path or module reserved by replacement");
    error.phase = "loader.replacement.claim";
    error.operation = operation;
    if (!path.empty())
        error.entryPath = path;
    return error;
}
} // namespace
struct DriverClaimLedger {
    std::thread::id thread = std::this_thread::get_id();
    std::atomic_bool blocked{false};
    std::map<OperationId, std::weak_ptr<DriverClaimControl>> claims;
    Result<void> check() const
    {
        if (thread != std::this_thread::get_id())
            return Result<void>::failure(makeError(ErrorCode::WrongThread, "driver claim requires owner"));
        if (blocked.load())
            return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "driver claim release unavailable"));
        return Result<void>::success();
    }
};
struct DriverClaimControl {
    std::shared_ptr<DriverClaimLedger> ledger;
    OperationId operation;
    Task<void> released;
    std::shared_ptr<const ValidatedCandidate> baseline;
    std::set<std::string> paths, modules;
};
DriverReplacementClaim::~DriverReplacementClaim()
{
    const auto value = control_;
    if (!value)
        return;
    if (value->ledger->thread != std::this_thread::get_id()) {
        value->ledger->blocked.store(true);
        return;
    }
    const auto at = value->ledger->claims.find(value->operation);
    if (at != value->ledger->claims.end() && at->second.lock() == value)
        value->ledger->claims.erase(at);
}
bool DriverReplacementClaims::owns(const std::weak_ptr<DriverClaimControl>& proof, OperationId operation) const noexcept
{
    if (!ledger_ || ledger_->thread != std::this_thread::get_id() || ledger_->blocked.load())
        return false;
    const auto value = proof.lock();
    if (!value || value->ledger != ledger_ || value->operation != operation || value->released.isReady())
        return false;
    const auto at = ledger_->claims.find(operation);
    return at != ledger_->claims.end() && at->second.lock() == value;
}
Result<void> DriverReplacementClaims::pathAccess(const std::string& path) const
{
    if (!ledger_)
        return Result<void>::success();
    auto admitted = ledger_->check();
    if (!admitted.hasValue())
        return admitted;
    for (const auto& entry : ledger_->claims)
        if (const auto value = entry.second.lock(); value && !value->released.isReady() && overlaps(value->paths, path))
            return Result<void>::failure(conflict(value->operation, path));
    return Result<void>::success();
}
Result<void> DriverReplacementClaims::access(const std::set<std::string>& paths, const std::set<std::string>& modules,
                                             const DriverReplacementClaim* own) const
{
    if (!ledger_)
        return Result<void>::success();
    auto admitted = ledger_->check();
    if (!admitted.hasValue())
        return admitted;
    for (const auto& entry : ledger_->claims) {
        const auto value = entry.second.lock();
        if (!value || value->released.isReady() || (own && own->control_ == value))
            continue;
        for (const auto& path : paths)
            if (overlaps(value->paths, path))
                return Result<void>::failure(conflict(value->operation, path));
        for (const auto& module : modules)
            if (value->modules.count(module))
                return Result<void>::failure(conflict(value->operation));
    }
    return Result<void>::success();
}
Result<void> DriverReplacementClaims::targetAccess(const ValidatedCandidate& target) const
{
    if (!ledger_)
        return Result<void>::success();
    auto admitted = ledger_->check();
    if (!admitted.hasValue())
        return admitted;
    for (const auto& entry : ledger_->claims) {
        const auto value = entry.second.lock();
        if (!value || value->released.isReady())
            continue;
        const auto& before = value->baseline->source().structure();
        const auto& after = target.source().structure();
        for (const auto& change : previewChanges(before, after).entries)
            if (change.desiredChanged && overlaps(value->paths, change.path))
                return Result<void>::failure(conflict(value->operation, change.path));
        for (const auto& module : value->modules) {
            const auto previous = before.modules().find(module), next = after.modules().find(module);
            if ((previous == before.modules().end()) != (next == after.modules().end()) ||
                (previous != before.modules().end() && previous->second != next->second))
                return Result<void>::failure(conflict(value->operation));
        }
    }
    return Result<void>::success();
}
Result<DriverReplacementClaim> DriverReplacementClaims::reserve(OperationId operation, Task<void> released,
                                                                std::shared_ptr<const ValidatedCandidate> baseline,
                                                                std::set<std::string> paths,
                                                                std::set<std::string> modules)
{
    using Output = Result<DriverReplacementClaim>;
    if (!ledger_)
        ledger_ = std::make_shared<DriverClaimLedger>();
    auto admitted = access(paths, modules);
    if (!admitted.hasValue())
        return Output::failure(admitted.error());
    if (!operation.valid() || !baseline || released.isReady() || ledger_->claims.count(operation))
        return Output::failure(makeError(ErrorCode::ConfigConflict, "driver claim source is no longer available"));
    std::set<std::string> roots;
    for (const auto& path : paths)
        if (!path.empty() && !overlaps(roots, path))
            roots.insert(path);
    auto value = std::make_shared<DriverClaimControl>(DriverClaimControl{
        ledger_, operation, std::move(released), std::move(baseline), std::move(roots), std::move(modules)});
    DriverReplacementClaim result(value);
    std::map<OperationId, std::weak_ptr<DriverClaimControl>> node;
    node.emplace(operation, value);
    auto current = access(value->paths, value->modules);
    if (!current.hasValue())
        return Output::failure(current.error());
    if (value->released.isReady() || ledger_->claims.count(operation))
        return Output::failure(makeError(ErrorCode::ConfigConflict, "driver claim invalidated during preparation"));
    ledger_->claims.merge(node);
    return Output::success(std::move(result));
}
} // namespace cordis::loader::detail
