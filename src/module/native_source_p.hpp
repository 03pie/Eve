/** @file native_source_p.hpp
 * @brief 已验证代次的私有原生入口访问；仅供宿主，不出现在公开管理器中。
 */
#pragma once
#include <cordis/module/ledger.hpp>
#include <cordis/module/manager.hpp>
#include <cordis/module/manifest.hpp>
#include <cordis_abi/plugin.h>
#include <cordis_abi/state.h>
#include <optional>

namespace cordis::module::detail {
struct NativeSource {
    GenerationId generation;
    ModuleManifest manifest;
    GenerationLedger ledger;
    CordisQueryFnV1 query;
    ModuleLease session;
    CordisQueryStateFnV1 queryState = nullptr;
};
Result<NativeSource> acquireNativeSource(GenerationId generation);
void recordNativeBusiness(GenerationId generation, const std::optional<ErrorInfo>& failure);
void recordNativePhase(GenerationId generation, ModuleState phase, const std::optional<ErrorInfo>& failure = {});
} // namespace cordis::module::detail
