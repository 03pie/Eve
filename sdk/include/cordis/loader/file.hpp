/** @file file.hpp
 * @brief 文件事务的宿主值类型，不依赖 Qt、运行实例或文件解析器。
 */
#pragma once
#include <cordis/error.hpp>
#include <cstddef>
#include <optional>
#include <string>

namespace cordis::loader {
/** @brief 文件层的独立字节预算；文本解析仍须另外遵守 YamlLimits。 */
struct ConfigurationFileLimits {
    std::size_t maximumBytes = 8 * 1024 * 1024;
};
/** @brief 原始文件的内容版本；不存在与空文件不同，path 绑定规范绝对目标。 */
struct ConfigurationFileVersion {
    std::string path;
    bool exists = false;
    std::string sha256;
};
/** @brief 只保存实际读取的字节，不把磁盘文本当成已验证或已接受的配置。 */
struct ConfigurationFileSnapshot {
    ConfigurationFileVersion version;
    std::string text;
};
/** @brief Unchanged 要求磁盘原文/版本均与期望基线一致；Conflict 保留外部文件原文。 */
enum class FileCommitOutcome { Committed, Unchanged, Conflict };
/** @brief 冲突属于可供宿主展示的结果，error 为 ConfigConflict；其他 I/O 错误由 Result 失败报告。 */
struct ConfigurationFileCommit {
    FileCommitOutcome outcome = FileCommitOutcome::Committed;
    ConfigurationFileSnapshot disk;
    std::optional<ErrorInfo> error;
};
} // namespace cordis::loader
