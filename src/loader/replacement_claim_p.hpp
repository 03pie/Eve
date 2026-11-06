/** @file replacement_claim_p.hpp
 * @brief Driver 私有逻辑占用；只保存权限与纯目标，不复制实例账目。
 */
#pragma once
#include <cordis/kernel.hpp>
#include <cordis/loader/yaml.hpp>
#include <memory>
#include <set>
namespace cordis::loader::detail {
struct DriverClaimControl;
struct DriverClaimLedger;
struct DriverCommitAccess;
class DriverReplacementClaim {
public:
    DriverReplacementClaim(const DriverReplacementClaim&) = delete;
    DriverReplacementClaim& operator=(const DriverReplacementClaim&) = delete;
    DriverReplacementClaim(DriverReplacementClaim&&) noexcept = default;
    DriverReplacementClaim& operator=(DriverReplacementClaim&&) = delete;
    ~DriverReplacementClaim();

private:
    friend class DriverReplacementClaims;
    friend struct DriverCommitAccess;
    explicit DriverReplacementClaim(std::shared_ptr<DriverClaimControl> control) : control_(std::move(control)) {}
    std::shared_ptr<DriverClaimControl> control_;
};
class DriverReplacementClaims {
public:
    DriverReplacementClaims() = default;
    DriverReplacementClaims(const DriverReplacementClaims&) = delete;
    DriverReplacementClaims& operator=(const DriverReplacementClaims&) = delete;
    Result<DriverReplacementClaim> reserve(OperationId, Task<void>, std::shared_ptr<const ValidatedCandidate>,
                                           std::set<std::string> paths, std::set<std::string> modules);
    Result<void> pathAccess(const std::string&) const;
    Result<void> targetAccess(const ValidatedCandidate&) const;
    Result<void> access(const std::set<std::string>& paths, const std::set<std::string>& modules,
                        const DriverReplacementClaim* own = nullptr) const;
    bool owns(const std::weak_ptr<DriverClaimControl>&, OperationId) const noexcept;

private:
    std::shared_ptr<DriverClaimLedger> ledger_;
};
} // namespace cordis::loader::detail
