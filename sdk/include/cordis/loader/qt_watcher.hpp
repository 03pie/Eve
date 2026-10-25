/** @file qt_watcher.hpp
 * @brief 文件监听只发布有界磁盘原文，不改变会话目标、保存基线或编辑内容。
 */
#pragma once
#include <cordis/loader/qt_io.hpp>
namespace cordis::loader {
namespace detail {
struct FileWatchControl;
}
/** @brief 外部变化序号用于显式载入/忽略的 CAS；物理通知次数不是内容版本。 */
struct ConfigurationWatchSnapshot {
    std::uint64_t changeSequence = 0;
    std::size_t observations = 0;
    std::shared_ptr<const ConfigurationFileSnapshot> external;
    std::optional<ErrorInfo> error;
    bool reading = false, watchingDirectory = false, watchingFile = false;
    bool closing = false, closed = false, deliveryBlocked = false;
};
/** @brief owner 监听文件及父目录，原子替换后重建文件监听，内容读取走专用 I/O。 */
class QtConfigurationWatcher {
public:
    /** @brief initial 须来自同一 io 的 read 回执，保留读取时配对的提交序号；首轮复读覆盖注册窗口。 */
    QtConfigurationWatcher(std::shared_ptr<qt::QtScheduler> scheduler, std::shared_ptr<QtConfigurationIo> io,
                           const ConfigurationDiskObservation& initial, unsigned debounceMilliseconds = 75);
    /** @brief 释放 facade 请求关门，显式 close 才能观察监听及回执静止。 */
    ~QtConfigurationWatcher();
    /** @brief 禁止复制监听所有者。 */
    QtConfigurationWatcher(const QtConfigurationWatcher&) = delete;
    /** @brief 不覆盖未关闭的监听。 */
    QtConfigurationWatcher& operator=(const QtConfigurationWatcher&) = delete;
    /** @brief owner 读取最新通知，刷新不读取磁盘或自动接受 YAML。 */
    Result<ConfigurationWatchSnapshot> snapshot() const;
    /** @brief owner 显式忽略/载入后确认该通知，不改变 Store 基线；旧序号拒绝。 */
    Result<void> acknowledge(std::uint64_t expectedChangeSequence);
    /** @brief owner 请求一次有界复读；重复请求合并为至多一次后续读取。 */
    Result<void> refresh();
    /** @brief owner 关门并等待已接受读取的 owner 回执，完成后不再发布变化。 */
    Task<void> close();

private:
    std::shared_ptr<detail::FileWatchControl> control_;
};
} // namespace cordis::loader
