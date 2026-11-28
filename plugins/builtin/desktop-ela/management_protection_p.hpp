/** @file management_protection_p.hpp
 * @brief 固定逻辑注册名的宿主值策略；当前代集合只从本次 Kernel 快照临时派生。
 */
#pragma once
#include <algorithm>
#include <eve/kernel.hpp>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace eve::ui::detail {
class ProtectionPolicy {
public:
    static Result<ProtectionPolicy> bind(const KernelSnapshot& core, const std::vector<RuntimeId>& requested)
    {
        using Output = Result<ProtectionPolicy>;
        const std::set<RuntimeId> ids(requested.begin(), requested.end());
        if (ids.empty() || ids.size() != requested.size())
            return Output::failure(makeError(ErrorCode::InvalidArgument, "invalid or duplicate protected runtime"));
        ProtectionPolicy policy;
        for (const auto id : ids) {
            const auto found =
                std::find_if(core.names.begin(), core.names.end(), [id](const auto& slot) { return slot.id == id; });
            if (!id.valid() || found == core.names.end() || !policy.slots_.emplace(found->name, id).second)
                return Output::failure(makeError(ErrorCode::InvalidArgument, "foreign protected runtime"));
        }
        return Output::success(std::move(policy));
    }
    bool matches(const std::set<RuntimeId>& ids) const
    {
        return ids.size() == slots_.size() &&
               std::all_of(slots_.begin(), slots_.end(), [&](const auto& slot) { return ids.count(slot.second) != 0; });
    }
    Result<std::set<RuntimeId>> resolve(const KernelSnapshot& core) const
    {
        using Output = Result<std::set<RuntimeId>>;
        std::set<RuntimeId> ids;
        for (const auto& fixed : slots_) {
            const auto found = std::find_if(core.names.begin(), core.names.end(),
                                            [&](const auto& slot) { return slot.name == fixed.first; });
            if (found == core.names.end() || !found->id.valid())
                return Output::failure(makeError(ErrorCode::ConfigConflict, "protected runtime slot unavailable"));
            ids.insert(fixed.second);
            ids.insert(found->id);
        }
        return Output::success(std::move(ids));
    }

private:
    // 初始身份只兼容原 stopped 缓存，不保留换代过程的其他 RuntimeId。
    std::map<std::string, RuntimeId> slots_;
};
} // namespace eve::ui::detail
