/** @file install_directory.cpp
 * @brief Windows 创建新文件、按句柄发布目录与限定所有权的回收。
 */
#include "install_directory_p.hpp"
#include <QFileInfo>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cstddef>

namespace cordis::module::detail {
namespace {
ErrorInfo failure(const char* phase, const std::string& message, DWORD native = ERROR_SUCCESS)
{
    auto error = makeError(ErrorCode::ModuleLoad, message);
    error.phase = phase;
    if (native != ERROR_SUCCESS)
        error.message += " (Windows error " + std::to_string(native) + ")";
    return error;
}
bool identity(HANDLE handle, FILE_ID_INFO& result)
{
    static constexpr BYTE zero[16]{};
    return GetFileInformationByHandleEx(handle, FileIdInfo, &result, sizeof(result)) &&
           std::memcmp(result.FileId.Identifier, zero, sizeof(zero)) != 0;
}
bool sameIdentity(const FILE_ID_INFO& a, const FILE_ID_INFO& b)
{
    return a.VolumeSerialNumber == b.VolumeSerialNumber &&
           std::memcmp(a.FileId.Identifier, b.FileId.Identifier, sizeof(a.FileId.Identifier)) == 0;
}
bool regular(HANDLE handle, bool directory)
{
    BY_HANDLE_FILE_INFORMATION info{};
    return GetFileType(handle) == FILE_TYPE_DISK && GetFileInformationByHandle(handle, &info) &&
           !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory;
}
bool dispose(HANDLE handle)
{
    FILE_DISPOSITION_INFO disposition{TRUE};
    return SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
}
} // namespace

Result<std::unique_ptr<InstallHandle>> lockModuleInstall(const QString& store, const std::string& hash)
{
    using Output = Result<std::unique_ptr<InstallHandle>>;
    const auto path = nativeModulePath(store + "/.cordis-install-" + QString::fromStdString(hash) + ".lock");
    InstallHandle handle(
        CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle.valid())
        return Output::failure(failure("module.install.lock", "cannot acquire hash installation lock", GetLastError()));
    return Output::success(std::make_unique<InstallHandle>(std::move(handle)));
}

InstallDirectory::InstallDirectory(QString path, InstallHandle handle, FILE_ID_INFO id)
    : path_(std::move(path)), directory_(std::move(handle)), identity_(id)
{
    files_.reserve(2);
}
Result<std::unique_ptr<InstallDirectory>> InstallDirectory::create(const QString& store)
{
    using Output = Result<std::unique_ptr<InstallDirectory>>;
    const auto path = store + "/.cordis-install-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto native = nativeModulePath(path);
    if (!CreateDirectoryW(native.c_str(), nullptr))
        return Output::failure(
            failure("module.install.create", "cannot create installation directory", GetLastError()));
    InstallHandle handle(CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                     nullptr));
    FILE_ID_INFO id{};
    // 创建目录与打开句柄之间发生异常时没有足够所有权证明，保留路径供诊断，不按路径猜测删除。
    if (!handle.valid())
        return Output::failure(failure("module.install.create",
                                       "cannot own created directory: " + path.toUtf8().toStdString(), GetLastError()));
    if (!regular(handle.get(), true) || !identity(handle.get(), id)) {
        return Output::failure(failure("module.install.create",
                                       "created directory identity is unavailable: " + path.toUtf8().toStdString()));
    }
    return Output::success(std::unique_ptr<InstallDirectory>(new InstallDirectory(path, std::move(handle), id)));
}
InstallDirectory::~InstallDirectory()
{
    if (!published_ && !cleanupAttempted_) {
        try {
            rollback();
        } catch (...) {
            // 仅异常退栈的最后保障；正常失败路径显式 rollback 并返回清理诊断。
        }
    }
}
Result<void> InstallDirectory::copy(const OpenedPath& source, const QString& name)
{
    if (published_ || cleanupAttempted_ || !source.valid() || files_.size() >= 2 || name.isEmpty() ||
        name == QLatin1String(".") || name == QLatin1String("..") || name.contains(QLatin1Char('/')) ||
        name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char(':')) || name.contains(QChar::Null))
        return Result<void>::failure(failure("module.install.copy", "invalid copy ownership or file name"));
    const auto native = nativeModulePath(path_ + '/' + name);
    InstallHandle target(CreateFileW(native.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES | DELETE, 0, nullptr,
                                     CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!target.valid())
        return Result<void>::failure(
            failure("module.install.copy", "cannot create new installation file", GetLastError()));
    FILE_ID_INFO id{};
    if (!regular(target.get(), false) || !identity(target.get(), id)) {
        auto error = failure("module.install.copy", "created file identity is unavailable");
        if (!dispose(target.get()))
            error.causes.push_back(failure("module.install.rollback", "cannot remove created file", GetLastError()));
        return Result<void>::failure(std::move(error));
    }
    files_.push_back({name, id});
    std::array<char, 64 * 1024> buffer{};
    std::uint64_t offset = 0;
    while (offset < source.size()) {
        const auto bytes = static_cast<DWORD>(std::min<std::uint64_t>(buffer.size(), source.size() - offset));
        if (!source.readAt(offset, buffer.data(), bytes))
            return Result<void>::failure(failure("module.install.copy", "fixed source read failed"));
        DWORD written = 0;
        while (written < bytes) {
            DWORD count = 0;
            if (!WriteFile(target.get(), buffer.data() + written, bytes - written, &count, nullptr) || count == 0)
                return Result<void>::failure(
                    failure("module.install.copy", "installation write failed", GetLastError()));
            written += count;
        }
        offset += bytes;
    }
    if (!FlushFileBuffers(target.get()) || !source.unchanged())
        return Result<void>::failure(failure("module.install.copy", "flush or fixed source confirmation failed"));
    return Result<void>::success();
}
Result<void> InstallDirectory::publish(const QString& destination)
{
    if (published_ || cleanupAttempted_ || files_.size() != 2 || QDir::cleanPath(destination) != destination ||
        QFileInfo(destination).absolutePath() != QFileInfo(path_).absolutePath())
        return Result<void>::failure(failure("module.install.publish", "incomplete installation ownership"));
    // 同卷绝对路径，ReplaceIfExists=FALSE。必须先关闭子文件读句柄，Windows 才能重命名目录。
    const auto target = nativeModulePath(destination);
    const auto bytes = target.size() * sizeof(wchar_t);
    const auto total = offsetof(FILE_RENAME_INFO, FileName) + bytes + sizeof(wchar_t);
    std::vector<std::byte> buffer(total);
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(bytes);
    std::memcpy(rename->FileName, target.data(), bytes);
    if (!SetFileInformationByHandle(directory_.get(), FileRenameInfo, rename, static_cast<DWORD>(buffer.size())))
        return Result<void>::failure(
            failure("module.install.publish", "cannot publish without replacing existing content", GetLastError()));
    published_ = true;
    path_ = destination;
    directory_.close();
    return Result<void>::success();
}
Result<void> InstallDirectory::rollback()
{
    if (published_)
        return Result<void>::failure(
            failure("module.install.rollback", "published content cannot be rolled back by installer"));
    if (cleanupAttempted_)
        return Result<void>::failure(failure("module.install.rollback", "cleanup already attempted"));
    cleanupAttempted_ = true;
    auto error = failure("module.install.rollback",
                         "cannot fully reclaim owned temporary directory: " + path_.toUtf8().toStdString());
    for (const auto& file : files_) {
        const auto native = nativeModulePath(path_ + '/' + file.name);
        InstallHandle handle(CreateFileW(native.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
                                         OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!handle.valid() && GetLastError() == ERROR_FILE_NOT_FOUND)
            continue;
        FILE_ID_INFO current{};
        if (!handle.valid() || !regular(handle.get(), false) || !identity(handle.get(), current) ||
            !sameIdentity(current, file.identity) || !dispose(handle.get()))
            error.causes.push_back(failure("module.install.rollback.file",
                                           "cannot reclaim owned file: " + file.name.toUtf8().toStdString()));
    }
    // 外来条目、重解析点或仍在使用的文件令删除失败；不递归扫描、接管或删除这些条目。
    if (!dispose(directory_.get()))
        error.causes.push_back(
            failure("module.install.rollback.directory", "cannot remove owned empty directory", GetLastError()));
    directory_.close();
    if (!error.causes.empty())
        return Result<void>::failure(std::move(error));
    return Result<void>::success();
}
bool InstallDirectory::ownsDirectory(const OpenedPath& directory) const noexcept
{
    return directory.sameFile(identity_);
}
bool InstallDirectory::ownsFile(const QString& name, const OpenedPath& file) const noexcept
{
    for (const auto& owned : files_)
        if (owned.name == name)
            return file.sameFile(owned.identity);
    return false;
}
} // namespace cordis::module::detail
