/** @file manifest.hpp
 * @brief Windows x64 原生模块的有界声明与只读产物预检；公开接口不暴露 Qt。
 */
#pragma once
#include <cordis/result.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cordis::module {
/** @brief 当前支持 c-abi / windows-x64 / ABI 1.0 / resident；不是已装载的 Runtime。 */
struct ModuleManifest {
    std::string moduleId;
    std::string moduleVersion;
    std::string library;
    std::string sha256;
    std::vector<std::string> plugins;
};

/** @brief 所有限额均须为正；文件读取采用分块而非完整映射 DLL。 */
struct PreflightLimits {
    std::size_t maximumManifestBytes = 64 * 1024;
    std::uint64_t maximumLibraryBytes = 256ULL * 1024 * 1024;
};

/** @brief 严格 JSON 声明解析；拒绝重复/未知字段及当前尚不支持的加载或卸载策略。 */
Result<ModuleManifest> parseManifest(const std::string& json, PreflightLimits limits = {});

/** @brief 一次文件预检的不可变快照；后续装载必须重新校验/固定产物，不能当作永久授权。 */
class PreflightArtifact {
public:
    /** @brief 复制一次预检快照，不重新读取文件。 */
    PreflightArtifact(const PreflightArtifact&) = default;
    /** @brief 移出快照后原值不能被误认为有效验证结果。 */
    PreflightArtifact(PreflightArtifact&& other) noexcept
        : manifest_(std::move(other.manifest_)), manifestPath_(std::move(other.manifestPath_)),
          libraryPath_(std::move(other.libraryPath_)), size_(other.size_), valid_(std::exchange(other.valid_, false))
    {
    }
    /** @brief 禁止部分覆盖已组合的声明和路径。 */
    PreflightArtifact& operator=(const PreflightArtifact&) = delete;
    /** @brief 新产物必须重新预检。 */
    PreflightArtifact& operator=(PreflightArtifact&&) = delete;
    /** @brief 移动后的来源返回 false；不监控后续文件变化。 */
    bool isValid() const noexcept { return valid_; }
    /** @brief 返回已通过当前声明规则的只读值。 */
    const ModuleManifest& manifest() const noexcept { return manifest_; }
    /** @brief 返回预检时声明文件的最终 UTF-8 路径。 */
    const std::string& manifestPath() const noexcept { return manifestPath_; }
    /** @brief 返回预检时产物文件的最终 UTF-8 路径。 */
    const std::string& libraryPath() const noexcept { return libraryPath_; }
    /** @brief 返回本次实际摘要计算覆盖的文件字节数。 */
    std::uint64_t libraryBytes() const noexcept { return size_; }

private:
    friend Result<PreflightArtifact> preflightModule(const std::string&, const std::string&, PreflightLimits);
    PreflightArtifact(ModuleManifest manifest, std::string manifestPath, std::string libraryPath, std::uint64_t size)
        : manifest_(std::move(manifest)), manifestPath_(std::move(manifestPath)), libraryPath_(std::move(libraryPath)),
          size_(size)
    {
    }
    ModuleManifest manifest_;
    std::string manifestPath_;
    std::string libraryPath_;
    std::uint64_t size_;
    bool valid_ = true;
};

/** @brief 根目录为带盘符或完整 UNC 共享的绝对 UTF-8 路径；声明可相对根目录，不能依赖当前盘。
 * 声明/库必须位于根内且能以拒绝写入/删除共享的句柄读取，路径/大小/摘要取自同一句柄。
 * 通过文件预算和 SHA256 后返回快照；从不装载或执行库，也不保留返回后的文件锁。
 */
Result<PreflightArtifact> preflightModule(const std::string& pluginRoot, const std::string& manifestPath,
                                          PreflightLimits limits = {});
} // namespace cordis::module
