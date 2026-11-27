/** @file catalog_p.hpp
 * @brief 两类装载后端共享的唯一进程目录；不执行插件代码或管理业务对象。
 */
#pragma once
#include "artifact_p.hpp"
#include "failure_p.hpp"
#include "instance_origin_p.hpp"
#include "reservation_p.hpp"
#include <eve/module/manager.hpp>
#include <map>
#include <mutex>

namespace eve::module::detail {
/** @brief 后端特有所有者；目录锁外显式退役，析构不代替加载器终结。 */
struct BackendResidency {
    virtual ~BackendResidency() = default;
    virtual Result<void> retire()
    {
        return Result<void>::failure(
            makeError(ErrorCode::ModulePinned, "backend does not support explicit retirement"));
    }
};
/** @brief 每线程一个进程唯一值，不随 std::thread::id 复用；耗尽明确失败。 */
std::uint64_t moduleThreadIdentity();
struct Generation {
    Generation(const PreflightArtifact& artifact, std::unique_ptr<OpenedPath> file, ModuleLoadPolicy selected);
    const ModuleLoadPolicy policy;
    const std::thread::id ownerThread = std::this_thread::get_id();
    const std::uint64_t ownerIdentity = moduleThreadIdentity();
    ModuleGenerationSnapshot copy();
    Result<void> sealEmpty(bool rejectPinned) { return ledger.sealEmpty(rejectPinned); }
    GenerationLedger ledger;
    std::shared_ptr<GenerationFailureSignal> businessSignal = std::make_shared<GenerationFailureSignal>();
    std::mutex mutex;
    ModuleGenerationSnapshot data;
    std::unique_ptr<OpenedPath> artifactFile;
    bool loading = true;
    std::size_t admissions = 0;
    std::weak_ptr<ReplacementReservationControl> replacement;
    /** @brief 仅保存宿主值；执行引用由 Runtime 激活闭包的来源租约持有。 */
    std::map<RuntimeId, std::string> runtimeOrigins;
    InstanceOrigins instanceOrigins;
    HMODULE image = nullptr;
    /** @brief 目标已离开、其他映像 probe 释放失败时保留的本方引用；永久不确定 pin。 */
    HMODULE observationImage = nullptr;
    std::shared_ptr<BackendResidency> backend;
};
struct GenerationReservation {
    std::shared_ptr<Generation> generation;
    bool created;
};
Result<GenerationReservation> reserveGeneration(VerifiedSource& source, GenerationId predecessor = {},
                                                ModuleLoadPolicy policy = ModuleLoadPolicy::Resident);
std::shared_ptr<Generation> findGeneration(GenerationId id);
std::vector<std::shared_ptr<Generation>> allGenerations();
Result<ModuleLoadReport> failGeneration(const std::shared_ptr<Generation>& generation, ErrorInfo failure);
/** @brief 受限搜索取得本目录映射，按首次策略 PIN 并核对实际映像；不能在目录锁内调用。 */
Result<void> mapGenerationImage(const std::shared_ptr<Generation>& generation);
} // namespace eve::module::detail
