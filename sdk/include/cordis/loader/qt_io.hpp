/** @file qt_io.hpp
 * @brief 专用单线程执行配置文件事务，显式关闭等待真实线程退出。
 */
#pragma once
#include <cordis/loader/qt_file.hpp>
#include <cordis/loader/store.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
namespace cordis::loader {
namespace detail {
struct FileIoControl;
}
/** @brief 任务完成和线程静止分别记录；已完成回执不等于 worker 已退出。 */
struct ConfigurationIoSnapshot {
    std::size_t pending = 0, completed = 0;
    std::uint64_t commitSequence = 0;
    std::optional<ConfigurationFileVersion> lastCommitted;
    bool closing = false, closed = false;
};
/** @brief 读取时配对的自身提交元数据，避免 owner 晚到回执误用之后的写入版本。 */
struct ConfigurationDiskObservation {
    ConfigurationFileSnapshot disk;
    std::uint64_t commitSequence = 0;
    std::optional<ConfigurationFileVersion> lastCommitted;
};
/** @brief 固定路径的后台读写器；仅接收不可变宿主值，不在 worker 运行插件逻辑。 */
class QtConfigurationIo final : public IConfigurationWriter {
public:
    /** @brief owner 构造不读写文件，线程按首个请求启动，pending 包含正在执行的任务。 */
    QtConfigurationIo(std::shared_ptr<qt::QtScheduler> scheduler, QString path, ConfigurationFileLimits limits = {},
                      std::size_t maximumPending = 16);
    /** @brief 最后一个 facade 释放时关门；正常退出须显式等待 close，避免应用析构同步排空。 */
    ~QtConfigurationIo() override;
    /** @brief 禁止复制后台执行器身份。 */
    QtConfigurationIo(const QtConfigurationIo&) = delete;
    /** @brief 禁止覆盖仍有线程及回执的执行器。 */
    QtConfigurationIo& operator=(const QtConfigurationIo&) = delete;
    /** @brief owner 排队读取原文及 hash，文件缺失仍是有效值；不解析或接受目标。 */
    Task<ConfigurationDiskObservation> read();
    /** @brief owner 排队真实文件事务，沿用原比较/协作锁/禁止直接覆盖的语义。 */
    Task<ConfigurationFileCommit> commit(std::shared_ptr<const ValidatedCandidate> target,
                                         ConfigurationFileVersion expected) override;
    /** @brief owner 读取队列及自身提交版本，不触发 I/O。 */
    Result<ConfigurationIoSnapshot> snapshot() const;
    /** @brief 装配时核对同一个 owner scheduler。 */
    bool usesScheduler(const std::shared_ptr<qt::QtScheduler>& scheduler) const noexcept;
    /** @brief owner 同步关门，排空所有已接受请求并等待专用线程完成 join，再释放 Qt 上下文。 */
    Task<void> close();

private:
    std::shared_ptr<detail::FileIoControl> control_;
};
} // namespace cordis::loader
