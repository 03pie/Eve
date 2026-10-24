/** @file qt_store.hpp
 * @brief 将真实 Qt 文件事务接入 Store 写后端，文件 I/O 在 Store 调用线程同步完成。
 */
#pragma once
#include <cordis/loader/qt_file.hpp>
#include <cordis/loader/store.hpp>
#include <QString>

namespace cordis::loader {
/** @brief 无独立 worker 或持久文件句柄的事务后端；异步调度由 ConfigurationStore 提供。 */
class QtConfigurationWriter final : public IConfigurationWriter {
public:
    /** @brief 固定目的路径与文件预算，构造不创建或读取文件。 */
    explicit QtConfigurationWriter(QString path, ConfigurationFileLimits limits = {});
    /** @brief 完整执行文件事务后返回已完成 Task，Store 仍排队处理回执。 */
    Task<ConfigurationFileCommit> commit(std::shared_ptr<const ValidatedCandidate> target,
        ConfigurationFileVersion expected) override;
private:
    QString path_;
    ConfigurationFileLimits limits_;
};
} // namespace cordis::loader
