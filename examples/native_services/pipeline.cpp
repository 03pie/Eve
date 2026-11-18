/** @file pipeline.cpp
 * @brief 复用原模块、目录、Driver/Session 和清理回执，只有一个宿主拥有者。
 */
#include "pipeline.hpp"
#include <cordis/loader/qt_store.hpp>
#include <exception>
#include <map>
#include <utility>

namespace native_services {
namespace {
using namespace cordis;
struct InitializationFailure {
    ErrorInfo error;
};
template <class T> T take(Result<T> result)
{
    if (!result.hasValue())
        throw InitializationFailure{result.error()};
    return std::move(result).value();
}
} // namespace
using namespace cordis;
using namespace cordis::module;
using namespace cordis::loader;

Result<std::shared_ptr<Pipeline>> Pipeline::open(const QString& root, const QString& sessionFile,
                                                 std::shared_ptr<qt::QtScheduler> scheduler)
{
    using Output = Result<std::shared_ptr<Pipeline>>;
    if (!scheduler)
        return Output::failure(makeError(ErrorCode::InvalidArgument, "pipeline requires a scheduler"));
    if (!scheduler->isCurrentThread())
        return Output::failure(makeError(ErrorCode::WrongThread, "pipeline requires scheduler owner thread"));
    if (!scheduler->isOpen())
        return Output::failure(makeError(ErrorCode::SchedulerClosed, "pipeline scheduler is closed"));
    std::shared_ptr<Pipeline> self;
    try {
        self = std::shared_ptr<Pipeline>(new Pipeline(std::move(scheduler)));
        self->initialize(root, sessionFile);
        return Output::success(self);
    } catch (const InitializationFailure& failure) {
        if (self)
            (void)self->close();
        return Output::failure(failure.error);
    } catch (const std::exception& failure) {
        if (self)
            (void)self->close();
        return Output::failure(makeError(ErrorCode::CallbackFailed, failure.what()));
    }
}
void Pipeline::initialize(const QString& root, const QString& sessionFile)
{
    const auto source = root.toUtf8().toStdString();
    counter_->generation = take(manager_->load(source, "counter/module.json")).generation;
    reporter_->generation = take(manager_->load(source, "reporter/module.json")).generation;
    counter_->session.emplace(
        take(NativeModule::open(*manager_, counter_->generation, scheduler_, kernel_->services())));
    reporter_->session.emplace(
        take(NativeModule::open(*manager_, reporter_->generation, scheduler_, kernel_->services())));
    counter_->runtime.emplace(
        take(counter_->session->runtime({"example.counter", {}, {{{"example.counter", 1}, true, {1, {"next"}}}}})));
    reporter_->runtime.emplace(take(reporter_->session->runtime({"example.reporter",
                                                                 {{{"example.counter", 1}, 1, {"next"}}},
                                                                 {{{"example.report", 1}, true, {1, {"read"}}}}})));
    (void)take(kernel_->registerRuntime("example.counter", *counter_->runtime));
    (void)take(kernel_->registerRuntime("example.reporter", *reporter_->runtime));
    const auto second = take(kernel_->services().createRealm());
    const auto gate = [](const std::shared_ptr<CurrentModule>& current) {
        const std::weak_ptr<const CurrentModule> weak = current;
        return [weak] {
            // 定义始终查询它自己的稳定记录；替换当前槽不会使旧定义误查另一代。
            const auto module = weak.lock();
            if (!module || !module->session)
                return Result<void>::failure(makeError(ErrorCode::ContextClosing, "module unavailable"));
            const auto state = module->session->snapshot();
            if (!state.hasValue())
                return Result<void>::failure(state.error());
            return state.value().closing || state.value().closed || state.value().faulted
                       ? Result<void>::failure(makeError(ErrorCode::ContextClosing, "module unavailable"))
                       : Result<void>::success();
        };
    };
    const auto schema = [](const char* failure) {
        return [failure](const std::string& config) {
            return config == "null" || config == failure
                       ? Result<void>::success()
                       : Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "unsupported example config"));
        };
    };
    definitions_ = take(ConfigurationDefinitions::create(
        {{{"example.counter", {}, schema("{\"failCleanup\":true}"), {"counter", "counter/module.json"}},
          *counter_->runtime,
          {},
          gate(counter_),
          128},
         {{"example.reporter",
           {{"example.counter", 1, 1, {"next"}}},
           schema("{\"failStart\":true}"),
           {"reporter", "reporter/module.json"}},
          *reporter_->runtime,
          {},
          gate(reporter_),
          128}}));
    driver_ = KernelReconcileDriver::configured(scheduler_, kernel_, definitions_,
                                                std::map<std::string, RealmId>{{"second", second}});
    session_ = std::make_shared<ConfigurationSession>(scheduler_, driver_,
                                                      std::make_shared<QtConfigurationWriter>(sessionFile),
                                                      take(readConfigurationFile(sessionFile)));
}
Task<PipelineCloseReport> Pipeline::close()
{
    if (!scheduler_->isCurrentThread()) {
        Promise<PipelineCloseReport> rejected;
        rejected.reject(makeError(ErrorCode::WrongThread, "pipeline close requires owner thread"));
        return rejected.task();
    }
    if (closing_)
        return stopped_.task();
    closing_ = true;
    const auto self = shared_from_this();
    pin_ = self;
    watch(session_ ? session_->close() : completedTask(), [self](const Result<void>& result) {
        self->sessionClosed_.emplace(result);
        self->closeKernel();
    });
    return stopped_.task();
}
void Pipeline::closeKernel()
{
    const auto self = shared_from_this();
    watch(kernel_->close(), [self](const Result<CleanupReport>& result) {
        self->kernelClosed_.emplace(result);
        self->closeCounter();
    });
}
void Pipeline::closeCounter()
{
    if (!counter_->session) {
        counterClosed_.emplace(Result<CleanupReport>::success({true, {}}));
        closeReporter();
        return;
    }
    const auto self = shared_from_this();
    watch(counter_->session->close(), [self](const Result<CleanupReport>& result) {
        self->counterClosed_.emplace(result);
        self->closeReporter();
    });
}
void Pipeline::closeReporter()
{
    if (!reporter_->session) {
        reporterClosed_.emplace(Result<CleanupReport>::success({true, {}}));
        finish();
        return;
    }
    const auto self = shared_from_this();
    watch(reporter_->session->close(), [self](const Result<CleanupReport>& result) {
        self->reporterClosed_.emplace(result);
        self->finish();
    });
}
void Pipeline::finish()
{
    const auto services = kernel_->services().snapshot();
    // 完整会话关闭不是工厂释放证明；先交还装配拥有者，再读取唯一目录的排空结果。
    session_.reset();
    driver_.reset();
    kernel_.reset();
    definitions_.reset();
    counter_->runtime.reset();
    reporter_->runtime.reset();
    const auto reporterDrain =
        reporter_->generation.valid() ? manager_->drain(reporter_->generation) : Result<void>::success();
    const auto counterDrain =
        counter_->generation.valid() ? manager_->drain(counter_->generation) : Result<void>::success();
    stopped_.resolve(PipelineCloseReport{*sessionClosed_, *kernelClosed_, *counterClosed_, *reporterClosed_, services,
                                         reporterDrain, counterDrain});
    pin_.reset();
}
} // namespace native_services
