/** @file pipeline.hpp
 * @brief 两个真实 Native 服务的宿主装配和异步退出，不包含场景或进程控制。
 */
#pragma once
#include "service_module.hpp"
#include <QString>
#include <eve/loader/kernel_driver.hpp>
#include <eve/loader/session.hpp>
#include <eve/module/manager.hpp>
#include <eve/qt/scheduler.hpp>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace native_services {
/** @brief 各阶段原始值结果；任务失败与已完成但不静止的清理保持区别。 */
struct PipelineCloseReport {
    eve::Result<void> session;
    eve::Result<eve::CleanupReport> kernel, counter, reporter;
    eve::Result<std::vector<eve::BindingSnapshot>> remainingServices;
    eve::Result<void> reporterDrain, counterDrain;
};
/** @brief 关闭期间自保留；投递失败保留尚未确认的实际拥有者。 */
class Pipeline : public std::enable_shared_from_this<Pipeline> {
public:
    /** @brief 借用调用方拥有的开放 owner 队列；装配失败的异步清理也沿该队列排空。 */
    static eve::Result<std::shared_ptr<Pipeline>> open(const QString& root, const QString& sessionFile,
                                                          std::shared_ptr<eve::qt::QtScheduler> scheduler);
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    std::shared_ptr<eve::qt::QtScheduler> scheduler() const { return scheduler_; }
    std::shared_ptr<eve::Kernel> kernel() const { return kernel_; }
    std::shared_ptr<eve::module::ModuleManager> manager() const { return manager_; }
    std::shared_ptr<eve::loader::KernelReconcileDriver> driver() const { return driver_; }
    std::shared_ptr<eve::loader::ConfigurationSession> session() const { return session_; }
    const CurrentModule& counter() const { return *counter_; }
    const CurrentModule& reporter() const { return *reporter_; }
    /** @brief 同步关 Session；等待各关闭阶段后释放本装配工厂再 drain，不关闭 Scheduler。
     * 调用方另持有的 Kernel/Driver/Session/Runtime 仍是实际拥有者，可能使 drain 返回错误。
     */
    eve::Task<PipelineCloseReport> close();

private:
    explicit Pipeline(std::shared_ptr<eve::qt::QtScheduler> scheduler) : scheduler_(std::move(scheduler)) {}
    void initialize(const QString& root, const QString& sessionFile);
    void closeKernel();
    void closeCounter();
    void closeReporter();
    void finish();
    template <class T, class F> void watch(const eve::Task<T>& task, F callback)
    {
        const auto done = stopped_;
        const auto receipt = task.thenOn(scheduler_, std::move(callback));
        // 回执可能在线程外失败，只完成线程安全 Promise；pin 与实际拥有者仍留在原处。
        eve::detail::TaskAccess::observe(receipt, [done](eve::Task<void>::Snapshot result) {
            if (!result->hasValue())
                done.reject(result->error());
        });
    }
    std::shared_ptr<eve::qt::QtScheduler> scheduler_;
    std::shared_ptr<eve::Kernel> kernel_ = std::make_shared<eve::Kernel>(scheduler_);
    std::shared_ptr<eve::module::ModuleManager> manager_ = std::make_shared<eve::module::ModuleManager>();
    std::shared_ptr<CurrentModule> counter_;
    std::shared_ptr<CurrentModule> reporter_;
    std::shared_ptr<const eve::loader::ConfigurationDefinitions> definitions_;
    std::shared_ptr<eve::loader::KernelReconcileDriver> driver_;
    std::shared_ptr<eve::loader::ConfigurationSession> session_;
    std::optional<eve::Result<void>> sessionClosed_;
    std::optional<eve::Result<eve::CleanupReport>> kernelClosed_, counterClosed_, reporterClosed_;
    eve::Promise<PipelineCloseReport> stopped_;
    std::shared_ptr<Pipeline> pin_;
    bool closing_ = false;
};
} // namespace native_services
