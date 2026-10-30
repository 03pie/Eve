/** @file workspace.hpp
 * @brief UI DLL 示例的宿主装配；配置、模块管理与退出只归宿主，不进入插件 SDK。
 */
#pragma once
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/qt_store.hpp>
#include <cordis/module/qt_module.hpp>
#include <cordis/ui/configuration.hpp>
#include <cordis/ui/ela_shell.hpp>

namespace notebook {
template <class T> T take(cordis::Result<T> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
inline void take(cordis::Result<void> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
}
/** @brief 示例宿主保活至明确退出；测试通过同一装配及真实公共 API 驱动。 */
struct Workspace : std::enable_shared_from_this<Workspace> {
    static std::shared_ptr<Workspace> open(const QString& root, const QString& file);
    std::shared_ptr<cordis::qt::QtScheduler> scheduler = std::make_shared<cordis::qt::QtScheduler>();
    std::shared_ptr<cordis::Kernel> kernel = std::make_shared<cordis::Kernel>(scheduler);
    cordis::ui::ElaShell shell{scheduler};
    std::shared_ptr<cordis::module::ModuleManager> manager = std::make_shared<cordis::module::ModuleManager>();
    cordis::GenerationId generation;
    std::shared_ptr<cordis::loader::KernelReconcileDriver> driver;
    std::shared_ptr<cordis::loader::ConfigurationSession> session;
    std::shared_ptr<cordis::ui::IConfigurationManagement> configurationManagement;
    std::optional<cordis::ui::PageHandle> configurationPage, modulesPage;
    std::string initial;
    cordis::Task<void> ready() const;
    cordis::Result<cordis::loader::AcceptedTarget> apply(const std::string& text);
    cordis::Task<void> close();

private:
    void initialize(const QString& root, const QString& file);
    void flushBeforeClose();
    void closeKernel();
    void closeShell();
    std::vector<cordis::ProviderHandle> providers;
    std::optional<cordis::Instance> host;
    std::optional<cordis::Runtime> runtime;
    std::shared_ptr<Workspace> pin;
    cordis::Promise<void> stopped;
    bool closing = false;
    template <class T, class F> void watch(const cordis::Task<T>& task, F action)
    {
        auto done = stopped;
        auto receipt = task.thenOn(scheduler, std::move(action));
        cordis::detail::TaskAccess::observe(receipt, [done](typename cordis::Task<void>::Snapshot result) {
            if (!result->hasValue())
                done.reject(result->error());
        });
    }
};
std::unique_ptr<QWidget> moduleDiagnostics(const std::shared_ptr<cordis::module::ModuleManager>& manager,
                                           std::shared_ptr<cordis::ui::DiagnosticsService> diagnostics = {});
} // namespace notebook
