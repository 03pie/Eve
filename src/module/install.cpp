/** @file install.cpp
 * @brief 复用 VerifiedSource 安装单 DLL 产物；事务不触碰模块目录、运行实例或配置。
 */
#include "artifact_p.hpp"
#include "install_directory_p.hpp"
#include "path_p.hpp"
#include <cordis/module/install.hpp>
#include <stdexcept>

namespace cordis::module {
namespace detail {
struct InstalledArtifactControl {
    std::unique_ptr<VerifiedSource> source;
    std::unique_ptr<OpenedPath> directory;
    bool reused = false;
};
} // namespace detail
namespace {
using detail::InstallDirectory;
using detail::InstalledArtifactControl;
using detail::OpenedPath;
using Pin = Result<std::shared_ptr<InstalledArtifactControl>>;
ErrorInfo failure(const char* phase, const std::string& message)
{
    auto error = makeError(ErrorCode::ModuleLoad, message);
    error.phase = phase;
    return error;
}
bool sameIdentity(const ModuleManifest& a, const ModuleManifest& b)
{
    return detail::sameManifest(a, b) && a.library == b.library;
}
bool exactFiles(const QString& directory, const QString& library)
{
    const auto entries =
        QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    return entries.size() == 2 && entries.contains("module.json") && entries.contains(library);
}
Pin pin(const QString& store, const QString& destination, const ModuleManifest& expected, PreflightLimits limits,
        const InstallDirectory* transaction)
{
    auto directory = std::make_unique<OpenedPath>(destination, true, false);
    const auto manifestPath = destination + "/module.json";
    const auto libraryName = QString::fromStdString(expected.library);
    const OpenedPath manifest(manifestPath, false, false), library(destination + '/' + libraryName, false, false);
    if (!directory->valid() || directory->finalPath() != destination || !manifest.valid() || !library.valid() ||
        manifest.finalPath() != manifestPath || library.finalPath() != destination + '/' + libraryName ||
        manifest.links() != 1 || library.links() != 1 || !exactFiles(destination, libraryName))
        return Pin::failure(
            failure("module.install.conflict", "hash directory is incomplete, redirected or contains foreign content"));
    auto verified =
        detail::verifyModuleSource(store.toUtf8().toStdString(), manifestPath.toUtf8().toStdString(), limits);
    if (!verified.hasValue())
        return Pin::failure(verified.error());
    auto fixed = std::move(verified).value();
    if (!sameIdentity(fixed->artifact.manifest(), expected) || !fixed->manifest->sameFile(manifest) ||
        !fixed->library->sameFile(library) ||
        (transaction && (!transaction->ownsDirectory(*directory) || !transaction->ownsFile("module.json", manifest) ||
                         !transaction->ownsFile(libraryName, library))))
        return Pin::failure(
            failure("module.install.conflict", "hash directory declaration or created file identity conflicts"));
    auto control = std::make_shared<InstalledArtifactControl>();
    control->source = std::move(fixed);
    control->directory = std::move(directory);
    control->reused = transaction == nullptr;
    return Pin::success(std::move(control));
}
} // namespace

InstalledArtifact::InstalledArtifact(std::shared_ptr<InstalledArtifactControl> control) : control_(std::move(control))
{
}
bool InstalledArtifact::isValid() const noexcept
{
    return control_ != nullptr;
}
const PreflightArtifact& InstalledArtifact::artifact() const
{
    if (!control_)
        throw std::logic_error("installed artifact has been moved");
    return control_->source->artifact;
}
bool InstalledArtifact::reused() const noexcept
{
    return control_ && control_->reused;
}

Result<InstalledArtifact> installModuleArtifact(const std::string& sourceRoot, const std::string& manifestPath,
                                                const std::string& storeRoot, PreflightLimits limits)
{
    using Output = Result<InstalledArtifact>;
    if (!detail::modulePathInput(storeRoot) || !detail::fullyQualifiedModulePath(QString::fromUtf8(storeRoot.c_str())))
        return Output::failure(failure("module.install.root", "store requires an absolute UTF-8 drive or UNC path"));
    const OpenedPath root(QString::fromUtf8(storeRoot.c_str()), true);
    const auto store = root.finalPath();
    std::vector<std::unique_ptr<OpenedPath>> storeGuards;
    if (store.isEmpty() || !detail::protectModuleDirectories(store, storeGuards) ||
        !root.sameFile(*storeGuards.back()) || root.finalPath() != store)
        return Output::failure(failure("module.install.root", "cannot protect installation store and ancestors"));
    auto verified = detail::verifyModuleSource(sourceRoot, manifestPath, limits);
    if (!verified.hasValue())
        return Output::failure(verified.error());
    auto source = std::move(verified).value();
    const auto& expected = source->artifact.manifest();
    const auto destination = store + '/' + QString::fromStdString(expected.sha256);
    // 所有写入者先获同一把锁，包括已有目录复用；不绕过正在进行的发布后验证。
    auto lock = detail::lockModuleInstall(store, expected.sha256);
    if (!lock.hasValue())
        return Output::failure(lock.error());
    const auto attributes = GetFileAttributesW(detail::nativeModulePath(destination).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        auto existing = pin(store, destination, expected, limits, nullptr);
        if (!existing.hasValue())
            return Output::failure(existing.error());
        return Output::success(InstalledArtifact(std::move(existing).value()));
    }
    const auto lookupError = GetLastError();
    if (lookupError != ERROR_FILE_NOT_FOUND && lookupError != ERROR_PATH_NOT_FOUND)
        return Output::failure(failure("module.install.conflict", "cannot inspect hash destination"));
    auto created = InstallDirectory::create(store);
    if (!created.hasValue())
        return Output::failure(created.error());
    auto transaction = std::move(created).value();
    const auto reject = [&](ErrorInfo error) {
        auto cleanup = transaction->rollback();
        if (!cleanup.hasValue())
            error.causes.push_back(cleanup.error());
        return Output::failure(std::move(error));
    };
    auto manifestCopy = transaction->copy(*source->manifest, "module.json");
    if (!manifestCopy.hasValue())
        return reject(manifestCopy.error());
    auto libraryCopy = transaction->copy(*source->library, QString::fromStdString(expected.library));
    if (!libraryCopy.hasValue())
        return reject(libraryCopy.error());
    auto checked = preflightModule(store.toUtf8().toStdString(),
                                   (transaction->path() + "/module.json").toUtf8().toStdString(), limits);
    if (!checked.hasValue())
        return reject(checked.error());
    if (!sameIdentity(checked.value().manifest(), expected) ||
        !exactFiles(transaction->path(), QString::fromStdString(expected.library)))
        return reject(failure("module.install.verify", "temporary installation changed or contains foreign content"));
    auto published = transaction->publish(destination);
    if (!published.hasValue())
        return reject(published.error());
    // 发布后重新按最终名称取保护并确认原生文件身份。失败时目录可能已对其他进程可见，保留证据而不撤回。
    auto installed = pin(store, destination, expected, limits, transaction.get());
    if (!installed.hasValue()) {
        auto error = failure("module.install.verify",
                             "published directory could not be protected: " + destination.toUtf8().toStdString());
        error.causes.push_back(installed.error());
        return Output::failure(std::move(error));
    }
    return Output::success(InstalledArtifact(std::move(installed).value()));
}
} // namespace cordis::module
