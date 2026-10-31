/** @file definition_driver_p.hpp
 * @brief 在可重入预检两侧核对实际定义来源，不运行 schema 或修改任何目标。
 */
#pragma once
#include <cordis/loader/definition_driver.hpp>
namespace cordis::loader::detail {
inline Result<void> checkDefinitionSource(const IReconcileDriver& driver, const ValidatedCandidate& target)
{
    const auto configured = dynamic_cast<const IConfiguredReconcileDriver*>(&driver);
    const auto definitions = configured ? configured->configurationDefinitions() : nullptr;
    if (definitions ? definitions->owns(target) : !target.hasDefinitionIdentity())
        return Result<void>::success();
    auto error = makeError(ErrorCode::ConfigConflict, "configuration was validated against different definitions");
    error.phase = "loader.definitions";
    return Result<void>::failure(std::move(error));
}
} // namespace cordis::loader::detail
