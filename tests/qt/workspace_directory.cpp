/** @file workspace_directory.cpp
 * @brief 复制原进程目录的账目、来源及占用；返回值不携带任何执行资源。
 */
#include "../../src/module/catalog_registry_p.hpp"
#include "workspace_directory_p.hpp"
#include <QJsonObject>
#include <cstdint>
#include <mutex>

namespace workspace_directory_test {
namespace native = eve::module::detail;
using eve::module::ModuleState;
namespace {
QString text(const std::string& value)
{
    return QString::fromStdString(value);
}
QString number(std::uint64_t value)
{
    return QString::number(static_cast<qulonglong>(value));
}
} // namespace

DirectoryValues directory()
{
    DirectoryValues result;
    auto& catalog = native::moduleCatalog();
    std::lock_guard<std::mutex> catalogLock(catalog.mutex);
    for (const auto& entry : catalog.replacements) {
        const auto& value = *entry.second;
        result.reservations.append(QJsonObject{{"module", text(entry.first)},
                                               {"operation", number(value.operation.value())},
                                               {"current", number(value.current.value())},
                                               {"candidate", number(value.candidate.value())},
                                               {"released", value.released.load()}});
    }
    for (const auto& entry : catalog.readers) {
        result.readerOperations += entry.second.size();
        QJsonArray operations;
        for (const auto operation : entry.second)
            operations.append(number(operation.value()));
        result.readers.append(QJsonObject{{"module", text(entry.first)}, {"operations", operations}});
    }
    result.generations.reserve(catalog.generations.size());
    for (const auto& generation : catalog.generations) {
        std::lock_guard<std::mutex> generationLock(generation->mutex);
        GenerationValues value{};
        value.module = generation->data;
        value.module.loadPolicy = generation->policy;
        value.module.resources = generation->ledger.snapshot();
        value.module.replacement.reset();
        if (const auto reservation = generation->replacement.lock(); reservation && !reservation->released.load()) {
            value.reservation = reservation->operation;
            value.module.replacement = reservation->snapshot();
        }
        {
            std::lock_guard<std::mutex> signalLock(generation->businessSignal->mutex);
            if (generation->businessSignal->error) {
                value.module.state = ModuleState::Failed;
                value.module.businessFailure = generation->businessSignal->error;
            }
        }
        value.admissions = generation->admissions;
        value.loading = generation->loading;
        value.image = generation->image != nullptr;
        value.observationImage = generation->observationImage != nullptr;
        value.artifactFile = static_cast<bool>(generation->artifactFile);
        value.backend = static_cast<bool>(generation->backend);
        for (const auto& origin : generation->runtimeOrigins)
            value.runtimes.push_back(
                {origin.first, value.module.generation, value.module.manifest.moduleId, origin.second});
        for (const auto& origin : generation->instanceOrigins)
            value.instances.push_back(origin.second);
        result.generations.push_back(std::move(value));
    }
    return result;
}
} // namespace workspace_directory_test
