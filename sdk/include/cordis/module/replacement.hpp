/** @file replacement.hpp
 * @brief 当前/候选代的模块级排他预约，不执行运行替换或改变目录角色。
 */
#pragma once
#include <cordis/result.hpp>
#include <memory>
#include <string>
#include <utility>

namespace cordis {
class ReplacementScope;
}
namespace cordis::module {
namespace detail {
struct ReplacementReservationControl;
struct ReplacementAccess;
}
/** @brief 工厂的纯来源定位值；不持有模块或授予执行权限，不伪造 Runtime 身份。 */
struct ModuleFactorySource {
    GenerationId generation;
    std::string pluginId;
    friend bool operator==(const ModuleFactorySource& a, const ModuleFactorySource& b)
    {
        return a.generation == b.generation && a.pluginId == b.pluginId;
    }
    friend bool operator!=(const ModuleFactorySource& a, const ModuleFactorySource& b) { return !(a == b); }
};
/** @brief 预约的历史值，不授予权限或承诺读取后仍被占用。 */
struct ModuleReplacementSnapshot {
    std::string moduleId;
    GenerationId current;
    GenerationId candidate;
};
/** @brief 唯一预约句柄。预约本身无破坏性动作，reset/析构仅解除准入，可在任意线程执行。
 * 后续运行事务必须持有它直到提交/恢复结束；本对象不表示业务升级成功。
 */
class ReplacementReservation {
public:
    ReplacementReservation() noexcept = default;
    ReplacementReservation(const ReplacementReservation&) = delete;
    ReplacementReservation& operator=(const ReplacementReservation&) = delete;
    ReplacementReservation(ReplacementReservation&&) noexcept = default;
    ReplacementReservation& operator=(ReplacementReservation&& other) noexcept;
    ~ReplacementReservation();
    Result<ModuleReplacementSnapshot> snapshot() const;
    /** @brief 候选真实清理后仅向原 Core 作用域授权旧代重建，不解除预约或表示恢复完成。 */
    Result<void> authorizeRecovery(const ReplacementScope& scope) const;
    void reset() noexcept;

private:
    friend class ModuleManager;
    friend struct detail::ReplacementAccess;
    explicit ReplacementReservation(std::shared_ptr<detail::ReplacementReservationControl> control)
        : control_(std::move(control))
    {
    }
    std::shared_ptr<detail::ReplacementReservationControl> control_;
};
} // namespace cordis::module
