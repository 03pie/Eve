/** @file replacement_builder.hpp
 * @brief 单次构建阶段的弱权限；不持有 Kernel、工厂或整个替换作用域。
 */
#pragma once
#include <eve/instance.hpp>
#include <cstddef>
#include <memory>
#include <utility>

namespace eve {
namespace detail {
struct ReplacementScopeControl;
struct PublicationControl;
} // namespace detail
class ReplacementBuilder {
public:
    /** @brief 只允许向取得许可时的发布组创建；组已准备/丢弃/发布或作用域关闭时拒绝。 */
    Result<Instance> stage(const Runtime&, std::string config = {}, RealmMap realms = {},
                           std::vector<ServiceRequirement> additionalDependencies = {},
                           std::optional<StateSnapshot> state = {}) const;
    /** @brief 使用宿主计划中本阶段的一次性成员准入；自动接收对应原始状态。 */
    Result<Instance> stageMember(std::size_t key, const Runtime&, std::string config = {}, RealmMap realms = {},
                                 std::vector<ServiceRequirement> additionalDependencies = {}) const;
    /** @brief 在真实父 Context 下按计划 key 创建子成员；不能替换为普通 inject 绕过状态。 */
    Result<Instance> injectMember(std::size_t key, const Context& parent, const Runtime&, std::string config = {},
                                  std::vector<ServiceRequirement> additionalDependencies = {}) const;

private:
    friend struct detail::ReplacementScopeControl;
    ReplacementBuilder(std::weak_ptr<detail::ReplacementScopeControl> owner,
                       std::weak_ptr<detail::PublicationControl> publication)
        : owner_(std::move(owner)), publication_(std::move(publication))
    {
    }
    std::weak_ptr<detail::ReplacementScopeControl> owner_;
    std::weak_ptr<detail::PublicationControl> publication_;
};
} // namespace eve
