/** @file pipeline.hpp
 * @brief 两个真实 Native 服务的宿主装配和异步退出，不包含场景或进程控制。
 */
#pragma once
#include "service_module.hpp"
#include <QString>
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/session.hpp>
#include <cordis/module/manager.hpp>
#include <cordis/qt/scheduler.hpp>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace native_services {
/** @brief 各阶段原始值结果；任务失败与已完成但不静止的清理保持区别。 */
struct PipelineCloseReport {
    cordis::Result<void> session;
    cordis::Result<cordis::CleanupReport> kernel, counter, reporter;
    cordis::Result<std::vector<cordis::BindingSnapshot>> remainingServices;
    cordis::Result<void> reporterDrain, counterDrain;
};
/** @brief 关闭期间自保留；投递失败保留尚未确认的实际拥有者。 */
class Pipeline : public std::enable_shared_from_this<Pipeline> {
public:
    /** @brief 借用调用方拥有的开放 owner 队列；装配失败的异步清理也沿该队列排空。 */
    static cordis::Result<std::shared_ptr<Pipeline>> open(const QString& root, const QString& sessionFile,
                                                          std::shared_ptr<cordis::qt::QtScheduler> scheduler);
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    std::shared_ptr<cordis::qt::QtScheduler> scheduler() const { return scheduler_; }
    std::shared_ptr<cordis::Kernel> kernel() const { return kernel_; }
    std::shared_ptr<cordis::module::ModuleManager> manager() const { return manager_; }
    std::shared_ptr<cordis::loader::KernelReconcileDriver> driver() const { return driver_; }
    std::shared_ptr<cordis::loader::ConfigurationSession> session() const { return session_; }
    const CurrentModule& counter() const { return *counter_; }
    const CurrentModule& reporter() const { return *reporter_; }
    /** @brief 同步关 Session；等待各关闭阶段后释放本装配工厂再 drain，不关闭 Scheduler。
     * 调用方另持有的 Kernel/Driver/Session/Runtime 仍是实际拥有者，可能使 drain 返回错误。
     */
    cordis::Task<PipelineCloseReport> close();

private:
    explicit Pipeline(std::shared_ptr<cordis::qt::QtScheduler> scheduler) : scheduler_(std::move(scheduler)) {}
    void initialize(const QString& root, const QString& sessionFile);
    void closeKernel();
    void closeCounter();
    void closeReporter();
    void finish();
    template <class T, class F> void watch(const cordis::Task<T>& task, F callback)
    {
        const auto done = stopped_;
        const auto receipt = task.thenOn(scheduler_, std::move(callback));
        // 回执可能在线程外失败，只完成线程安全 Promise；pin 与实际拥有者仍留在原处。
        cordis::detail::TaskAccess::observe(receipt, [done](cordis::Task<void>::Snapshot result) {
            if (!result->hasValue())
                done.reject(result->error());
        });
    }
    std::shared_ptr<cordis::qt::QtScheduler> scheduler_;
    std::shared_ptr<cordis::Kernel> kernel_ = std::make_shared<cordis::Kernel>(scheduler_);
    std::shared_ptr<cordis::module::ModuleManager> manager_ = std::make_shared<cordis::module::ModuleManager>();
    std::shared_ptr<CurrentModule> counter_;
    std::shared_ptr<CurrentModule> reporter_;
    std::shared_ptr<const cordis::loader::ConfigurationDefinitions> definitions_;
    std::shared_ptr<cordis::loader::KernelReconcileDriver> driver_;
    std::shared_ptr<cordis::loader::ConfigurationSession> session_;
    std::optional<cordis::Result<void>> sessionClosed_;
    std::optional<cordis::Result<cordis::CleanupReport>> kernelClosed_, counterClosed_, reporterClosed_;
    cordis::Promise<PipelineCloseReport> stopped_;
    std::shared_ptr<Pipeline> pin_;
    bool closing_ = false;
};
} // namespace native_services
