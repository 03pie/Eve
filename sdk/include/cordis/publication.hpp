/** @file publication.hpp
 * @brief 宿主显式控制一组托管实例的输出可见性；不拥有模块或替换策略。
 */
#pragma once
#include <cordis/result.hpp>
#include <cstddef>
#include <memory>

namespace cordis {
namespace detail {
struct PublicationControl;
}
enum class PublicationState { Open, Published, Aborted };
struct PublicationSnapshot {
    PublicationState state;
    std::size_t members;
    std::size_t heldBindings;
    bool deliveryBlocked;
};
/** @brief owner 线程的显式发布组，副本共享状态；析构不自动发布或执行异步清理。
 * 未提交输出只对组内依赖可见，成员仍沿用正常激活、租约和清理协议。
 * 宿主必须提交，或放弃后等待成员 stop / Kernel.close 的真实清理结果。
 */
class PublicationGroup {
public:
    /** @brief 全部成员就绪后一次公开输出；通知投递失败保留 Published 和错误，不能冒记成功或再次提交掩盖。 */
    Result<void> commit() const;
    /** @brief 立即冻结组内业务并请求成员停止；返回成功不代表清理完成。已放弃时幂等。 */
    Result<void> abort() const;
    /** @brief 移动后的句柄无效；诊断不隐式提交或清理。 */
    Result<PublicationSnapshot> snapshot() const;

private:
    friend class ServiceRegistry;
    explicit PublicationGroup(std::shared_ptr<detail::PublicationControl> control) : control_(std::move(control)) {}
    std::shared_ptr<detail::PublicationControl> control_;
};
} // namespace cordis
