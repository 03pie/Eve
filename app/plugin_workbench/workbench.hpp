/** @file workbench.hpp
 * @brief 声明由应用线程持有的桌面宿主及其启动、保存和关闭契约。
 */
#pragma once

#include <QString>
#include <eve/kernel.hpp>
#include <eve/loader/kernel_driver.hpp>
#include <eve/loader/session.hpp>
#include <eve/ui/configuration.hpp>
#include <eve/ui/ela_shell.hpp>
#include <optional>
#include <stdexcept>

namespace eve::workbench {
/** @brief 根据当前文件和旧版本同名文件的存在情况，解析工作台管理的工作区或偏好文件。 */
QString workbenchDataFile(const QString& applicationData, const QString& relativeFile);

/** @brief 保留底层预检错误说明，并提供与失败阶段对应的用户诊断。 */
class ModulePreflightError final : public std::runtime_error {
public:
    /** @brief 按配置依赖、清单或 ABI 检查结果选择诊断。 */
    explicit ModulePreflightError(const ErrorInfo& error);
    /** @brief 返回可写入启动日志和展示给用户的静态诊断。 */
    const char* diagnostic() const noexcept { return diagnostic_; }
private:
    const char* diagnostic_;
};

/** @brief 在应用线程组装一个工作区，调用方需保持事件循环运转直到 close() 完成。 */
class Workbench final {
public:
    /** @brief 在调用线程创建 Qt 调度器、内核和使用应用偏好目录的外壳。 */
    Workbench();
    /** @brief 打开一个工作区，已有配置取自磁盘，首次创建外部实例使用空配置对象。
     * @throws ModulePreflightError 工作区缺少所需模块或模块预检失败。
     */
    void open(const std::optional<QString>& manifestPath, const QString& workspaceDir);
    /** @brief 在应用目标前发布宿主提供的服务，发布句柄由本宿主持有到关闭。 */
    void publishUi();
    /** @brief 将打开时选定的文本交给会话预览并应用，返回运行协调的回执。 */
    loader::AcceptedTarget applyInitial();
    /** @brief 保存已接受的修改，然后依次关闭会话、内核和外壳。 */
    Task<void> close();
    std::shared_ptr<qt::QtScheduler> scheduler;
    std::shared_ptr<Kernel> kernel;
    ui::ElaShell shell;
    std::shared_ptr<loader::ConfigurationSession> session;
private:
    /** @brief 等待配置协调排空后再释放内核。 */
    void closeSession();
    /** @brief 仅在实例清理完成后关闭外壳，使清理诊断仍可写入日志。 */
    void closeKernel();
    /** @brief 等待 UI 服务关闭并释放提供者，最终结算宿主关闭回执。 */
    void closeShell();
    std::shared_ptr<ui::IConfigurationManagement> management_;
    std::vector<ProviderHandle> providers_;
    std::optional<ui::PageHandle> configPage_;
    std::string initial_;
    Promise<void> closed_;
    bool closing_ = false;
    bool published_ = false;
};
} // namespace eve::workbench
