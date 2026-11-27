/** @file native_source_p.hpp
 * @brief 已验证代次的私有原生入口访问；仅供宿主，不出现在公开管理器中。
 */
#pragma once
#include <eve/module/ledger.hpp>
#include <eve/module/manager.hpp>
#include <eve/module/manifest.hpp>
#include <eve_abi/plugin.h>
#include <eve_abi/state.h>
#include <memory>
#include <optional>

namespace eve::module::detail {
struct BackendResidency;
struct NativeSource {
    GenerationId generation;
    ModuleManifest manifest;
    GenerationLedger ledger;
    EveQueryFnV1 query;
    ModuleLease session;
    EveQueryStateFnV1 queryState = nullptr;
};
Result<NativeSource> acquireNativeSource(GenerationId generation);
/** @brief 已持实际 generation admission 时登记后端缓存；只接受首次登记或同一所有者。 */
Result<void> retainNativeBackend(GenerationId generation, const std::shared_ptr<BackendResidency>& backend);
void recordNativeBusiness(GenerationId generation, const std::optional<ErrorInfo>& failure);
void recordNativePhase(GenerationId generation, ModuleState phase, const std::optional<ErrorInfo>& failure = {});
} // namespace eve::module::detail
