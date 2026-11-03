/** @file catalog_registry_p.hpp
 * @brief 唯一进程目录的私有共享状态，供装载、准入和准备提交共用。
 */
#pragma once
#include "catalog_p.hpp"
#include <set>

namespace cordis::module::detail {
struct ModuleCatalog {
    std::mutex mutex;
    std::vector<std::shared_ptr<Generation>> generations;
    std::map<std::string, std::shared_ptr<ReplacementReservationControl>> replacements;
    std::map<std::string, std::set<OperationId>> readers;
};
enum class ReusePurpose { Discovery, Staging };
ModuleCatalog& moduleCatalog();
/** @brief 调用方持目录锁。 */
std::shared_ptr<Generation> catalogGenerationLocked(ModuleCatalog& catalog, GenerationId id);
/** @brief 调用方持 generation.mutex；只检查宿主状态，不调用插件。 */
Result<void> generationReusable(const Generation& generation, ReusePurpose purpose);
/** @brief 调用方同时持 generation.mutex 和 businessSignal.mutex。 */
Result<void> generationReusableLockedSignal(const Generation& generation, ReusePurpose purpose);
} // namespace cordis::module::detail
