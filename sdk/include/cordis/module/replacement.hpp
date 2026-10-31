/** @file replacement.hpp
 * @brief 当前/候选代的模块级排他预约，不执行运行替换或改变目录角色。
 */
#pragma once
#include <cordis/result.hpp>
#include <memory>
#include <string>
#include <utility>

namespace cordis::module {
namespace detail {
struct ReplacementReservationControl;
}
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
    void reset() noexcept;

private:
    friend class ModuleManager;
    explicit ReplacementReservation(std::shared_ptr<detail::ReplacementReservationControl> control)
        : control_(std::move(control))
    {
    }
    std::shared_ptr<detail::ReplacementReservationControl> control_;
};
} // namespace cordis::module
