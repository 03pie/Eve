/** @file qt_module.cpp
 * @brief 独立 Qt 装载后端；使用共同固定来源、进程目录及受限映像映射。
 */
#include "qt_manifest_p.hpp"
#include "qt_module_p.hpp"
#include "qt_artifact_load_p.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPluginLoader>
#include <QPointer>
#include <QThread>
#include <eve/build_info.hpp>
#include <eve/module/qt_manifest.hpp>
#include <eve/module/qt_module.hpp>
#include <functional>

namespace eve::module {
namespace {
ErrorInfo error(ErrorCode code, std::string message)
{
    auto value = makeError(code, std::move(message));
    value.phase = "module.qt.load";
    return value;
}
using detail::QtResidency;
using detail::ImageDependencies;
using ArtifactCheck = std::function<Result<void>(const PreflightArtifact&)>;

Result<ModuleLoadReport> loadGeneration(const std::string& root, const std::string& path, PreflightLimits limits,
                                        GenerationId predecessor, ImageDependencies dependencies,
                                        const ArtifactCheck& check = {})
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
    if (check) {
        auto accepted = check(artifact);
        if (!accepted.hasValue())
            return Output::failure(accepted.error());
    }
    if (artifact.manifest().loader != ModuleLoader::QtUi ||
        artifact.manifest().unloadPolicy != ModuleUnloadPolicy::Resident)
        return Output::failure(error(ErrorCode::AbiMismatch, "Qt backend requires resident qt-ui loader"));
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
    auto mapped = detail::mapGenerationImage(generation, dependencies);
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
Result<ModuleLoadReport> detail::loadBundledQtModule(const BundledQtModule& expected)
{
    if (!QCoreApplication::instance())
        return Result<ModuleLoadReport>::failure(error(ErrorCode::WrongThread, "Bundled Qt module requires an application owner."));
    const OpenedPath installation(QCoreApplication::applicationDirPath(), true);
    const auto applicationPath = installation.finalPath();
    const auto check = [&](const PreflightArtifact& artifact) {
        const QFileInfo manifestFile(QDir(applicationPath), QString::fromStdString(expected.manifestPath));
        const auto libraryFile = manifestFile.dir().filePath(QString::fromStdString(expected.libraryName));
        const auto& manifest = artifact.manifest();
        if (manifest.moduleId != expected.moduleId || manifest.plugins.size() != 1 ||
            manifest.plugins.front() != expected.pluginId || manifest.library != expected.libraryName ||
            manifestFile.filePath().compare(QString::fromStdString(artifact.manifestPath()), Qt::CaseInsensitive) != 0 ||
            libraryFile.compare(QString::fromStdString(artifact.libraryPath()), Qt::CaseInsensitive) != 0) {
            auto failure = error(ErrorCode::AbiMismatch, "Bundled module has an unexpected identity or path.");
            failure.phase = "module.bundle.identity";
            return Result<void>::failure(std::move(failure));
        }
        if (manifest.moduleVersion != expected.version)
            return Result<void>::failure(error(ErrorCode::AbiMismatch, "Bundled module version differs from the selected version."));
        return Result<void>::success();
    };
    return loadGeneration(applicationPath.toStdString(), expected.manifestPath, {}, {},
                          ImageDependencies::ApplicationBundle, check);
}
Result<ModuleLoadReport> loadQtModule(const std::string& root, const std::string& path, PreflightLimits limits)
{
    return loadGeneration(root, path, limits, {}, ImageDependencies::ModuleDirectory);
}
Result<ModuleLoadReport> detail::loadInstalledQtModule(const std::string& root, const std::string& moduleId,
    const std::string& pluginId, const std::string& digest)
{
    return loadGeneration(root, "module.json", {}, {}, ImageDependencies::ModuleDirectory, [&](const PreflightArtifact& artifact) {
        const auto& identity = artifact.manifest();
        if (identity.moduleId != moduleId || identity.plugins.size() != 1 ||
            identity.plugins.front() != pluginId || identity.sha256 != digest)
            return Result<void>::failure(error(ErrorCode::AbiMismatch, "Module differs from the selected artifact."));
        return Result<void>::success();
    });
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
    return loadGeneration(root, path, limits, predecessor, ImageDependencies::ModuleDirectory);
}
} // namespace eve::module
