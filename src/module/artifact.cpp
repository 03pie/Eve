/** @file artifact.cpp
 * @brief 共享路径固定、二次摘要确认及 PE 校验，不执行后端装载。
 */
#include "artifact_p.hpp"
#include "path_p.hpp"
#include <QFileInfo>
#include <algorithm>

namespace cordis::module::detail {
bool sameManifest(const ModuleManifest& left, const ModuleManifest& right)
{
    auto a = left.plugins, b = right.plugins;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return left.moduleId == right.moduleId && left.moduleVersion == right.moduleVersion &&
           left.sha256 == right.sha256 && left.loader == right.loader && a == b;
}
namespace {
ErrorInfo error(ErrorCode code, const char* phase, const char* message)
{
    auto result = makeError(code, message);
    result.phase = phase;
    return result;
}
bool compatibleImage(const OpenedPath& artifact)
{
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    return artifact.readAt(0, reinterpret_cast<char*>(&dos), sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
           dos.e_lfanew >= static_cast<LONG>(sizeof(dos)) &&
           artifact.readAt(static_cast<std::uint64_t>(dos.e_lfanew), reinterpret_cast<char*>(&nt), sizeof(nt)) &&
           nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           (nt.FileHeader.Characteristics & IMAGE_FILE_DLL) != 0 &&
           nt.FileHeader.SizeOfOptionalHeader >= sizeof(nt.OptionalHeader) &&
           nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
}
} // namespace
Result<std::unique_ptr<VerifiedSource>> verifyModuleSource(const std::string& pluginRoot,
                                                           const std::string& manifestPath, PreflightLimits limits)
{
    using Output = Result<std::unique_ptr<VerifiedSource>>;
    auto first = preflightModule(pluginRoot, manifestPath, limits);
    if (!first.hasValue())
        return Output::failure(first.error());
    const auto& initial = first.value();
    auto artifact = std::make_unique<OpenedPath>(QString::fromUtf8(initial.libraryPath().c_str()), false);
    auto manifestGuard = std::make_unique<OpenedPath>(QString::fromUtf8(initial.manifestPath().c_str()), false);
    std::vector<std::unique_ptr<OpenedPath>> directories;
    if (!artifact->valid() || !manifestGuard->valid() ||
        !protectModuleDirectories(QFileInfo(QString::fromUtf8(initial.libraryPath().c_str())).absolutePath(),
                                  directories) ||
        !protectModuleDirectories(QFileInfo(QString::fromUtf8(initial.manifestPath().c_str())).absolutePath(),
                                  directories))
        return Output::failure(
            error(ErrorCode::ModuleLoad, "module.source", "cannot protect module source or ancestor directories"));
    auto checked = preflightModule(pluginRoot, manifestPath, limits);
    if (!checked.hasValue())
        return Output::failure(checked.error());
    const auto& verified = checked.value();
    if (initial.manifestPath() != verified.manifestPath() || initial.libraryPath() != verified.libraryPath() ||
        !sameManifest(initial.manifest(), verified.manifest()) ||
        artifact->finalPath().toUtf8().toStdString() != verified.libraryPath() ||
        manifestGuard->finalPath().toUtf8().toStdString() != verified.manifestPath())
        return Output::failure(
            error(ErrorCode::ModuleLoad, "module.source", "source changed while acquiring load guards"));
    if (!artifact->sameFile(*artifact))
        return Output::failure(
            error(ErrorCode::ModuleLoad, "module.source", "stable 128-bit file identity is unavailable"));
    if (!compatibleImage(*artifact))
        return Output::failure(
            error(ErrorCode::ModuleLoad, "module.image", "artifact is not a supported AMD64 PE32+ DLL"));
    auto result = std::make_unique<VerifiedSource>(verified);
    result->library = std::move(artifact);
    result->manifest = std::move(manifestGuard);
    result->directories = std::move(directories);
    return Output::success(std::move(result));
}
} // namespace cordis::module::detail
