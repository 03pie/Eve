/** @file reclaim.cpp
 * @brief Reclaim a retired, immutable Windows module only when no process pins its directory or DLL.
 */
#include "package_p.hpp"
#include "../module/install_directory_p.hpp"
#include "../module/path_p.hpp"
#include <eve/module/manifest.hpp>
#include <QCryptographicHash>
#include <QUuid>
#include <array>

namespace eve::extensions::detail {
namespace {
using module::detail::InstallHandle;
InstallHandle openForRemoval(const QString& path, bool directory)
{
    return InstallHandle(CreateFileW(module::detail::nativeModulePath(path).c_str(),
        (directory ? FILE_READ_ATTRIBUTES : GENERIC_READ) | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
}
bool regular(const InstallHandle& file, bool directory)
{
    BY_HANDLE_FILE_INFORMATION info{};
    return file.valid() && GetFileType(file.get()) == FILE_TYPE_DISK && GetFileInformationByHandle(file.get(), &info) &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory &&
        (directory || info.nNumberOfLinks == 1);
}
bool dispose(const InstallHandle& file)
{
    FILE_DISPOSITION_INFO info{TRUE};
    return SetFileInformationByHandle(file.get(), FileDispositionInfo, &info, sizeof(info)) != FALSE;
}
}

bool preserveDamagedArtifact(const QString& store, const QString& sha256, const QString& library)
{
    std::vector<std::unique_ptr<module::detail::OpenedPath>> parents;
    if (!module::detail::protectModuleDirectories(store, parents)) return false;
    const auto path = QDir(store).filePath(sha256);
    auto folder = openForRemoval(path, true);
    if (!regular(folder, true)) return false;
    const auto members = QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const auto& name : members) {
        if (name != "module.json" && name != library) return false;
        auto child = openForRemoval(QDir(path).filePath(name), false);
        if (!regular(child, false)) return false;
    }
    // Leave the old bytes intact for inspection. A mapped or redirected directory cannot be taken over.
    const auto backup = path + QStringLiteral(".damaged-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    return module::detail::renameDirectoryWithoutReplacement(folder.get(), backup);
}

bool reclaimArtifact(const QString& store, const QString& sha256)
{
    // Pin the ancestors before resolving children. A junction or concurrently renamed parent fails closed.
    std::vector<std::unique_ptr<module::detail::OpenedPath>> parents;
    if (!module::detail::protectModuleDirectories(store, parents)) return false;
    const auto path = QDir(store).filePath(sha256);
    auto folder = openForRemoval(path, true);
    if (!folder.valid()) return GetLastError() == ERROR_FILE_NOT_FOUND;
    if (!regular(folder, true)) return false;
    auto manifest = openForRemoval(path + "/module.json", false);
    if (!regular(manifest, false)) return false;
    const module::PreflightLimits limits;
    QByteArray bytes(static_cast<int>(limits.maximumManifestBytes) + 1, Qt::Uninitialized);
    DWORD count = 0;
    if (!ReadFile(manifest.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) ||
        count > limits.maximumManifestBytes) return false;
    bytes.resize(static_cast<int>(count));
    const auto parsed = module::parseManifest(bytes.toStdString());
    if (!parsed.hasValue() || QString::fromStdString(parsed.value().sha256) != sha256) return false;
    const auto name = QString::fromStdString(parsed.value().library);
    auto dll = openForRemoval(path + '/' + name, false);
    if (!regular(dll, false)) return false;
    const auto members = QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    if (members.size() != 2 || !members.contains(name) || !members.contains("module.json")) return false;
    // Check both contents and file identity on the same protected handles used for deletion.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 64 * 1024> chunk;
    quint64 total = 0;
    do {
        if (!ReadFile(dll.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &count, nullptr)) return false;
        total += count;
        if (total > limits.maximumLibraryBytes) return false;
        hash.addData(chunk.data(), static_cast<int>(count));
    } while (count != 0);
    if (hash.result().toHex() != sha256.toLatin1() || !dispose(dll)) return false;
    if (!dispose(manifest)) {
        FILE_DISPOSITION_INFO keep{FALSE};
        SetFileInformationByHandle(dll.get(), FileDispositionInfo, &keep, sizeof(keep));
        return false;
    }
    dll.close();
    manifest.close();
    return dispose(folder);
}
} // namespace eve::extensions::detail
