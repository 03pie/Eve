/** @file qt_file.hpp
 * @brief 提供独立的 Qt 配置文件事务，不接管目标修订、运行状态或文件 watcher。
 */
#pragma once
#include <cordis/loader/file.hpp>
#include <cordis/loader/yaml.hpp>
#include <QString>

namespace cordis::loader {
/** @brief 同步有界读取本地绝对路径，保留原始字节及 SHA256；不存在是有效基线。 */
Result<ConfigurationFileSnapshot> readConfigurationFile(const QString& path, ConfigurationFileLimits limits = {});
/** @brief 持同路径协作锁比较磁盘基线，以 QSaveFile 提交有效候选；不运行事件循环，不允许直接覆盖回退。 */
Result<ConfigurationFileCommit> commitConfigurationFile(const QString& path, const ValidatedCandidate& target,
    const ConfigurationFileVersion& expected, ConfigurationFileLimits limits = {});
} // namespace cordis::loader
