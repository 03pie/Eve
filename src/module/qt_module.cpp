/** @file qt_module.cpp
 * @brief 独立 Qt 装载后端；使用共同固定来源、进程目录及受限映像映射。
 */
#include "qt_manifest_p.hpp"
#include "qt_module_p.hpp"
#include <QCoreApplication>
#include <QPluginLoader>
#include <QPointer>
#include <QThread>
#include <cordis/build_info.hpp>
#include <cordis/module/qt_manifest.hpp>
#include <cordis/module/qt_module.hpp>

namespace cordis::module {
namespace {
ErrorInfo error(ErrorCode code, std::string message)
{
    auto value = makeError(code, std::move(message));
    value.phase = "module.qt.load";
    return value;
}
using detail::QtResidency;
Result<ModuleLoadReport> loadGeneration(const std::string& root, const std::string& path, PreflightLimits limits,
                                        GenerationId predecessor)
{
    using Output = Result<ModuleLoadReport>;
    auto app = QCoreApplication::instance();
    if (!app || QCoreApplication::closingDown() || QThread::currentThread() != app->thread())
        return Output::failure(error(ErrorCode::WrongThread, "Qt module loading requires application owner"));
    // Also bind root-only hosts to the actual shared Core, before restricted dependency loading.
    const auto runtime = buildInfo();
    const auto host = qtToolchain();
    if (runtime.version != std::string_view(host.sdkVersion) || runtime.compiler != std::string_view(host.compiler) ||
        runtime.pointerBits != host.pointerBits)
        return Output::failure(error(ErrorCode::AbiMismatch, "shared Core runtime differs from Qt SDK"));
    auto source = detail::verifyModuleSource(root, path, limits);
    if (!source.hasValue())
        return Output::failure(source.error());
    const auto& artifact = source.value()->artifact;
    if (artifact.manifest().loader != ModuleLoader::QtUi)
        return Output::failure(error(ErrorCode::AbiMismatch, "Qt backend requires qt-ui loader"));
    auto backend = std::make_shared<QtResidency>(artifact.libraryPath());
    auto metadata = detail::verifyQtMetadata(artifact, backend->loader);
    if (!metadata.hasValue())
        return Output::failure(metadata.error());
    auto reservation = detail::reserveGeneration(*source.value(), predecessor);
    if (!reservation.hasValue())
        return Output::failure(reservation.error());
    const auto generation = reservation.value().generation;
    if (!reservation.value().created) {
        std::shared_ptr<QtResidency> retained;
        {
            std::lock_guard<std::mutex> lock(generation->mutex);
            retained = std::dynamic_pointer_cast<QtResidency>(generation->backend);
        }
        if (!retained || retained->root.isNull() || !retained->loader.isLoaded())
            return detail::failGeneration(generation,
                                          error(ErrorCode::ModulePinned, "resident Qt root is unavailable"));
        return Output::success({generation->data.generation, true});
    }
    // Reserve before entering any module code. Loading re-entry sees 'loading', never a usable generation.
    {
        std::lock_guard<std::mutex> lock(generation->mutex);
        generation->backend = backend;
    }
    auto mapped = detail::mapGenerationImage(generation);
    if (!mapped.hasValue())
        return detail::failGeneration(generation, mapped.error());
    auto rootLease = generation->ledger.acquire(ResourceKind::QObject, "qt.loader.root");
    if (!rootLease.hasValue())
        return detail::failGeneration(generation, rootLease.error());
    backend->rootLease = std::move(rootLease.value());
    auto call = generation->ledger.acquire(ResourceKind::Call, "qt.loader.instance");
    if (!call.hasValue())
        return detail::failGeneration(generation, call.error());
    try {
        backend->loader.setLoadHints(QLibrary::PreventUnloadHint | QLibrary::ResolveAllSymbolsHint);
        // The actual image was already mapped with restricted Windows dependency search.
        // Qt adds its own loader reference to this same protected, process-pinned image.
        backend->root = backend->loader.instance();
    } catch (...) {
        return detail::failGeneration(generation, error(ErrorCode::AbiProtocol, "exception escaped Qt root creation"));
    }
    if (!backend->root || !backend->loader.isLoaded())
        return detail::failGeneration(generation,
                                      error(ErrorCode::ModuleLoad, backend->loader.errorString().toStdString()));
    if (backend->root->thread() != app->thread() || backend->root->parent())
        return detail::failGeneration(
            generation, error(ErrorCode::AbiProtocol, "Qt root must remain unparented on application owner"));
    call.value().reset();
    {
        std::lock_guard<std::mutex> lock(generation->mutex);
        generation->data.state = ModuleState::Loaded;
        generation->loading = false;
    }
    return Output::success({generation->data.generation, false});
}
} // namespace
Result<ModuleLoadReport> loadQtModule(const std::string& root, const std::string& path, PreflightLimits limits)
{
    return loadGeneration(root, path, limits, {});
}
Result<ModuleLoadReport> stageQtModule(const std::string& root, const std::string& path, GenerationId predecessor,
                                       PreflightLimits limits)
{
    auto app = QCoreApplication::instance();
    if (!app || QCoreApplication::closingDown() || QThread::currentThread() != app->thread())
        return Result<ModuleLoadReport>::failure(
            error(ErrorCode::WrongThread, "Qt staging requires application owner"));
    if (!predecessor.valid())
        return Result<ModuleLoadReport>::failure(
            error(ErrorCode::InvalidArgument, "candidate requires a current predecessor"));
    return loadGeneration(root, path, limits, predecessor);
}
} // namespace cordis::module
