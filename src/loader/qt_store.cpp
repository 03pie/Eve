/** @file qt_store.cpp
 * @brief 复用 Q4d1 的真实哈希比较和临时文件提交，不在 Store 内伪造成功回执。
 */
#include <cordis/loader/qt_store.hpp>
#include <utility>

namespace cordis::loader {
/** @brief 后端不持有文件锁，只有提交调用期间才创建事务资源。 */
QtConfigurationWriter::QtConfigurationWriter(QString path, ConfigurationFileLimits limits)
    : path_(std::move(path)), limits_(limits) {}
/** @brief 文件冲突保留业务报告，I/O 失败保持任务失败；上层负责停止后续写入。 */
Task<ConfigurationFileCommit> QtConfigurationWriter::commit(std::shared_ptr<const ValidatedCandidate> target,
    ConfigurationFileVersion expected)
{
    Promise<ConfigurationFileCommit> done;
    if (!target) {
        auto error = makeError(ErrorCode::ConfigInvalid, "file writer requires a target");
        error.phase = "loader.file.target";
        done.reject(std::move(error));
    } else {
        auto result = commitConfigurationFile(path_, *target, expected, limits_);
        if (result.hasValue()) done.resolve(std::move(result).value());
        else done.reject(result.error());
    }
    return done.task();
}
} // namespace cordis::loader
