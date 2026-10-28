/** @file native_file_p.hpp
 * @brief 预检与原生装载共用的私有文件句柄，不进入公开 SDK。
 */
#pragma once
#include <QDir>
#include <QString>
#include <cstdint>
#include <cstring>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace cordis::module::detail {
/** @brief 一次预检持续持有同一原生句柄；路径、元数据和字节不能在重开文件时脱节。 */
class OpenedPath {
public:
    explicit OpenedPath(const QString& path, bool directory)
    {
        const auto native = QDir::toNativeSeparators(path).toStdWString();
        handle_ = CreateFileW(native.c_str(), directory ? FILE_READ_ATTRIBUTES : GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            error_ = GetLastError();
            return;
        }
        if (GetFileType(handle_) != FILE_TYPE_DISK) {
            error_ = ERROR_INVALID_DATA;
            return;
        }
        if (!GetFileInformationByHandle(handle_, &initial_)) {
            error_ = GetLastError();
            return;
        }
        if (((initial_.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory) {
            error_ = ERROR_DIRECTORY;
            return;
        }
        valid_ = true;
        identityAvailable_ = GetFileInformationByHandleEx(handle_, FileIdInfo, &identity_, sizeof(identity_)) != FALSE;
    }
    OpenedPath(const OpenedPath&) = delete;
    OpenedPath& operator=(const OpenedPath&) = delete;
    ~OpenedPath()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }
    bool valid() const noexcept { return valid_; }
    DWORD error() const noexcept { return error_; }
    std::uint64_t size() const noexcept
    {
        return (static_cast<std::uint64_t>(initial_.nFileSizeHigh) << 32) | initial_.nFileSizeLow;
    }
    /** @brief Qt 的规范路径不保证展开 junction，查询必须针对稍后实际读取的句柄。 */
    QString finalPath() const
    {
        if (!valid_)
            return {};
        const auto required = GetFinalPathNameByHandleW(handle_, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (required == 0 || required > 32768)
            return {};
        std::wstring buffer(required, L'\0');
        const auto written =
            GetFinalPathNameByHandleW(handle_, buffer.data(), required, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (written == 0 || written >= required)
            return {};
        auto resolved = QString::fromWCharArray(buffer.data(), static_cast<int>(written));
        if (resolved.startsWith(QStringLiteral("\\\\?\\UNC\\")))
            resolved = QStringLiteral("\\\\") + resolved.mid(8);
        else if (resolved.startsWith(QStringLiteral("\\\\?\\")))
            resolved = resolved.mid(4);
        return QDir::fromNativeSeparators(resolved);
    }
    bool read(char* buffer, DWORD capacity, DWORD& bytes) const noexcept
    {
        return ReadFile(handle_, buffer, capacity, &bytes, nullptr) != FALSE;
    }
    /** @brief 从同一句柄读取 PE 头；范围不越过已经固定的文件大小。 */
    bool readAt(std::uint64_t offset, char* buffer, DWORD capacity) const noexcept
    {
        if (offset > size() || capacity > size() - offset)
            return false;
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN))
            return false;
        DWORD done = 0;
        while (done < capacity) {
            DWORD bytes = 0;
            if (!read(buffer + done, capacity - done, bytes) || bytes == 0)
                return false;
            done += bytes;
        }
        return true;
    }
    bool unchanged() const noexcept
    {
        BY_HANDLE_FILE_INFORMATION current{};
        return GetFileInformationByHandle(handle_, &current) &&
               current.dwVolumeSerialNumber == initial_.dwVolumeSerialNumber &&
               current.nFileIndexHigh == initial_.nFileIndexHigh && current.nFileIndexLow == initial_.nFileIndexLow &&
               current.nFileSizeHigh == initial_.nFileSizeHigh && current.nFileSizeLow == initial_.nFileSizeLow &&
               CompareFileTime(&current.ftLastWriteTime, &initial_.ftLastWriteTime) == 0;
    }
    /** @brief 比较两个已固定句柄的文件身份，不以路径拼写或模块名猜测同一文件。 */
    bool sameFile(const OpenedPath& other) const noexcept
    {
        static constexpr BYTE zero[16]{};
        return valid_ && other.valid_ && identityAvailable_ && other.identityAvailable_ &&
               std::memcmp(identity_.FileId.Identifier, zero, sizeof(zero)) != 0 &&
               identity_.VolumeSerialNumber == other.identity_.VolumeSerialNumber &&
               std::memcmp(identity_.FileId.Identifier, other.identity_.FileId.Identifier, sizeof(zero)) == 0;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION initial_{};
    DWORD error_ = ERROR_SUCCESS;
    bool valid_ = false;
    FILE_ID_INFO identity_{};
    bool identityAvailable_ = false;
};

} // namespace cordis::module::detail
