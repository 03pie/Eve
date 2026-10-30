/** @file install_directory_p.hpp
 * @brief hash 安装的 Windows 临时目录所有者；只清理记录过身份的文件，不递归删除。
 */
#pragma once
#include "native_file_p.hpp"
#include <cordis/result.hpp>
#include <memory>
#include <utility>
#include <vector>

namespace cordis::module::detail {
class InstallHandle {
public:
    explicit InstallHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
    InstallHandle(InstallHandle&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    InstallHandle(const InstallHandle&) = delete;
    InstallHandle& operator=(const InstallHandle&) = delete;
    ~InstallHandle() { close(); }
    HANDLE get() const noexcept { return handle_; }
    bool valid() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }
    void close() noexcept
    {
        if (valid()) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE handle_;
};

/** @brief 锁文件以 CREATE_NEW 和 DELETE_ON_CLOSE 获得；不删除其他进程遗留或占有的锁。 */
Result<std::unique_ptr<InstallHandle>> lockModuleInstall(const QString& store, const std::string& hash);

class InstallDirectory {
public:
    static Result<std::unique_ptr<InstallDirectory>> create(const QString& store);
    InstallDirectory(const InstallDirectory&) = delete;
    InstallDirectory& operator=(const InstallDirectory&) = delete;
    ~InstallDirectory();
    const QString& path() const noexcept { return path_; }
    Result<void> copy(const OpenedPath& source, const QString& name);
    Result<void> publish(const QString& destination);
    Result<void> rollback();
    bool ownsDirectory(const OpenedPath& directory) const noexcept;
    bool ownsFile(const QString& name, const OpenedPath& file) const noexcept;

private:
    InstallDirectory(QString path, InstallHandle handle, FILE_ID_INFO identity);
    struct File {
        QString name;
        FILE_ID_INFO identity;
    };
    QString path_;
    InstallHandle directory_;
    FILE_ID_INFO identity_;
    std::vector<File> files_;
    bool published_ = false;
    bool cleanupAttempted_ = false;
};
} // namespace cordis::module::detail
