/** @file qt_module.cpp
 * @brief 独立 Qt 装载后端；使用共同固定来源、进程目录及受限映像映射。
 */
#include "qt_manifest_p.hpp"
#include "qt_module_p.hpp"
#include "qt_system_p.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QPluginLoader>
#include <QPointer>
#include <QThread>
#include <eve/build_info.hpp>
#include <eve/module/qt_manifest.hpp>
#include <eve/module/qt_module.hpp>

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
constexpr char desktopManifest[] = "system/desktop/module.json";
constexpr char desktopLibrary[] = "system/desktop/eve_desktop.dll";

bool isSystemDesktop(const PreflightArtifact& artifact, const std::string& root)
{
    const QDir application(QString::fromUtf8(root.c_str()));
    const auto matches = [&](const std::string& pinned, const char* relative) {
        return QString::compare(QString::fromUtf8(pinned.c_str()), application.filePath(QString::fromLatin1(relative)),
                                Qt::CaseInsensitive) == 0;
    };
    const auto& manifest = artifact.manifest();
    return manifest.moduleId == detail::systemDesktopModuleId && manifest.plugins.size() == 1 &&
           manifest.plugins.front() == detail::systemDesktopPluginId && manifest.library == "eve_desktop.dll" &&
           matches(artifact.manifestPath(), desktopManifest) && matches(artifact.libraryPath(), desktopLibrary);
}

Result<ModuleLoadReport> loadGeneration(const std::string& root, const std::string& path, PreflightLimits limits,
                                        GenerationId predecessor, ImageDependencies dependencies)
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
    if (dependencies == ImageDependencies::ApplicationBundle && !isSystemDesktop(artifact, root)) {
        auto failure = error(ErrorCode::AbiMismatch, "The installed desktop module has an unexpected identity or path.");
        failure.phase = "workbench.desktop.identity";
        return Output::failure(std::move(failure));
    }
    // The private host/desktop C++ contract ships together even while the public SDK stays binary compatible.
    if (dependencies == ImageDependencies::ApplicationBundle && artifact.manifest().moduleVersion != EVE_DESKTOP_VERSION)
        return Output::failure(error(ErrorCode::AbiMismatch, "The installed desktop version differs from the launcher."));
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
Result<ModuleLoadReport> detail::loadSystemDesktopModule()
{
    if (!QCoreApplication::instance())
        return Result<ModuleLoadReport>::failure(error(ErrorCode::WrongThread, "Desktop requires an application owner."));
    const OpenedPath installation(QCoreApplication::applicationDirPath(), true);
    return loadGeneration(installation.finalPath().toUtf8().toStdString(), desktopManifest, {}, {},
                          ImageDependencies::ApplicationBundle);
}
Result<ModuleLoadReport> loadQtModule(const std::string& root, const std::string& path, PreflightLimits limits)
{
    return loadGeneration(root, path, limits, {}, ImageDependencies::ModuleDirectory);
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
