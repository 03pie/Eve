/** @file replacement_builder.hpp
 * @brief 单次构建阶段的弱权限；不持有 Kernel、工厂或整个替换作用域。
 */
#pragma once
#include <cordis/instance.hpp>
#include <memory>
#include <utility>

namespace cordis {
namespace detail {
struct ReplacementScopeControl;
struct PublicationControl;
} // namespace detail
class ReplacementBuilder {
public:
    /** @brief 只允许向取得许可时的发布组创建；组已准备/丢弃/发布或作用域关闭时拒绝。 */
    Result<Instance> stage(const Runtime&, std::string config = {}, RealmMap realms = {},
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
} // namespace cordis
