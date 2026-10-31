/** @file catalog_p.hpp
 * @brief 两类装载后端共享的唯一进程目录；不执行插件代码或管理业务对象。
 */
#pragma once
#include "artifact_p.hpp"
#include "failure_p.hpp"
#include "reservation_p.hpp"
#include <cordis/module/manager.hpp>
#include <mutex>

namespace cordis::module::detail {
/** @brief 后端特有的驻留所有者；目录保留至进程退出，不在静态析构中关闭加载器。 */
struct BackendResidency {
    virtual ~BackendResidency() = default;
};
struct Generation {
    Generation(const PreflightArtifact& artifact, std::unique_ptr<OpenedPath> file);
    ModuleGenerationSnapshot copy();
    GenerationLedger ledger;
    std::shared_ptr<GenerationFailureSignal> businessSignal = std::make_shared<GenerationFailureSignal>();
    std::mutex mutex;
    ModuleGenerationSnapshot data;
    const std::unique_ptr<OpenedPath> artifactFile;
    bool loading = true;
    std::size_t admissions = 0;
    std::weak_ptr<ReplacementReservationControl> replacement;
    HMODULE image = nullptr;
    std::shared_ptr<BackendResidency> backend;
};
struct GenerationReservation {
    std::shared_ptr<Generation> generation;
    bool created;
};
Result<GenerationReservation> reserveGeneration(VerifiedSource& source, GenerationId predecessor = {});
std::shared_ptr<Generation> findGeneration(GenerationId id);
std::vector<std::shared_ptr<Generation>> allGenerations();
Result<ModuleLoadReport> failGeneration(const std::shared_ptr<Generation>& generation, ErrorInfo failure);
/** @brief 受限搜索取得本目录映射、永久 PIN 并核对实际映像；不能在目录锁内调用。 */
Result<void> mapGenerationImage(const std::shared_ptr<Generation>& generation);
} // namespace cordis::module::detail
