/** @file install.hpp
 * @brief 固定来源复制到不可覆盖的 hash 目录；不装载、执行或切换模块。
 */
#pragma once
#include <cordis/module/manifest.hpp>
#include <memory>

namespace cordis::module {
namespace detail {
struct InstalledArtifactControl;
}
/** @brief 安装完成后持有声明、DLL 及目录的只读保护；副本共享同一保护。
 * 跨线程传递不带 Qt 对象或运行代次；最后一个副本释放后文件不再受本凭据保护。
 * 装载方仍须正常预检/固定来源，不能把快照当成永久授权。
 */
class InstalledArtifact {
public:
    InstalledArtifact(const InstalledArtifact&) = default;
    InstalledArtifact(InstalledArtifact&&) noexcept = default;
    InstalledArtifact& operator=(const InstalledArtifact&) = default;
    InstalledArtifact& operator=(InstalledArtifact&&) noexcept = default;
    /** @brief 移动后的凭据无效。 */
    bool isValid() const noexcept;
    /** @brief 返回安装目标的已验证快照；移动后的凭据抛出 std::logic_error。 */
    const PreflightArtifact& artifact() const;
    /** @brief 是否复用了验证通过的已有 hash 目录；无效凭据返回 false。 */
    bool reused() const noexcept;

private:
    explicit InstalledArtifact(std::shared_ptr<detail::InstalledArtifactControl> control);
    friend Result<InstalledArtifact> installModuleArtifact(const std::string&, const std::string&, const std::string&,
                                                           PreflightLimits);
    std::shared_ptr<detail::InstalledArtifactControl> control_;
};

/** @brief 将已固定的声明和其单个 DLL 复制到 <storeRoot>/<sha256>/，声明名固定为 module.json。
 * storeRoot 必须是已存在的带盘符或完整 UNC 共享的绝对 UTF-8 路径；来源规则与 preflightModule 相同。
 * 分块同步 IO；不要求 QCoreApplication，可在宿主自有 worker 上调用并在退出前等待它完成。
 * 临时目录完整验证后以不覆盖方式整体发布；相同 hash 的已有目录须声明身份一致才可复用。
 * 同 hash 安装互斥，忙时返回错误；失败仅回收本事务拥有的临时文件，不递归删除外部内容。
 * 仅安装声明内的 DLL 和 JSON，不隐式复制依赖、配置、资源旁文件或扫描包目录。
 */
Result<InstalledArtifact> installModuleArtifact(const std::string& sourceRoot, const std::string& manifestPath,
                                                const std::string& storeRoot, PreflightLimits limits = {});
} // namespace cordis::module
