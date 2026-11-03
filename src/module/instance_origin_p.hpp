/** @file instance_origin_p.hpp
 * @brief 实际业务实例的来源随原有 Instance 租约保留；不从调用者编号猜测 Core 所有权。
 */
#pragma once
#include <cordis/instance.hpp>
#include <cordis/module/ledger.hpp>
#include <map>
#include <memory>
#include <optional>

namespace cordis::module::detail {
struct Generation;
struct InstanceOrigin {
    InstanceId instance;
    RuntimeId runtime;
    Epoch epoch;
    bool managed;
    OperationId operation{};
};
using InstanceOrigins = std::multimap<std::pair<InstanceId, Epoch>, InstanceOrigin>;
/** @brief 业务对象真实退出时，先移除宿主来源，再在目录锁外释放原 Instance 租约。 */
class InstanceOriginLease {
public:
    InstanceOriginLease(const InstanceOriginLease&) = delete;
    InstanceOriginLease& operator=(const InstanceOriginLease&) = delete;
    InstanceOriginLease(InstanceOriginLease&& other) noexcept;
    InstanceOriginLease& operator=(InstanceOriginLease&& other) noexcept;
    ~InstanceOriginLease();
    void reset() noexcept;

private:
    friend Result<InstanceOriginLease> retainInstanceOrigin(GenerationId, const std::string&, std::string, InstanceId,
                                                            Epoch, const Context*);
    InstanceOriginLease(std::shared_ptr<Generation> generation, InstanceOrigins::iterator row, ModuleLease lease);
    std::shared_ptr<Generation> generation_;
    std::optional<InstanceOrigins::iterator> row_;
    ModuleLease lease_;
};
/** @brief 由既有 GenerationAdmission 保护；只有实际 Context 可以证明托管来源。 */
Result<InstanceOriginLease> retainInstanceOrigin(GenerationId generation, const std::string& plugin, std::string label,
                                                 InstanceId instance, Epoch epoch, const Context* context);
} // namespace cordis::module::detail
