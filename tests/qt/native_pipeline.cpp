/** @file native_pipeline.cpp
 * @brief 直接调用真实装配库，验证空关闭、共享工厂保留和跨线程入口。
 */
#include "pipeline.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

namespace {
using namespace eve;
using namespace eve::module;
using namespace native_services;
#define CHECK(value)                                                                                                   \
    do {                                                                                                               \
        if (!(value))                                                                                                  \
            throw std::runtime_error(std::string(#value) + " at " + std::to_string(__LINE__));                         \
    } while (false)
template <class T> T take(Result<T> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
void take(Result<void> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
}
void until(const std::function<bool()>& predicate)
{
    QEventLoop loop;
    QTimer poll, timeout;
    timeout.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (predicate())
            loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(1);
    timeout.start(8000);
    if (!predicate())
        loop.exec();
    CHECK(predicate());
}
template <class T>
typename Task<T>::Snapshot wait(const Task<T>& task, const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    auto delivered = std::make_shared<bool>(false);
    const auto receipt = task.thenOn(scheduler, [delivered](const Result<T>&) { *delivered = true; });
    until([&] { return receipt.isReady(); });
    CHECK(*delivered && receipt.tryResult()->hasValue());
    return task.tryResult();
}
void barrier(const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    Promise<void> done;
    CHECK(scheduler->post({[done] { done.resolve(); }, {}}));
    CHECK(wait(done.task(), scheduler)->hasValue());
}
void run(const QString& root, const std::string& mode)
{
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    auto scheduler = std::make_shared<qt::QtScheduler>();
    if (mode == "open_failure") {
        const auto missingQueue = Pipeline::open(root, scratch.filePath("pipeline.yaml"), {});
        CHECK(!missingQueue.hasValue() && missingQueue.error().code == ErrorCode::InvalidArgument);
        bool wrongOwner = false;
        std::thread worker([&] {
            const auto rejected = Pipeline::open(root, scratch.filePath("pipeline.yaml"), scheduler);
            wrongOwner = !rejected.hasValue() && rejected.error().code == ErrorCode::WrongThread;
        });
        worker.join();
        CHECK(wrongOwner);
        for (int attempt = 0; attempt < 3; ++attempt) {
            CHECK(
                !Pipeline::open(scratch.filePath("missing"), scratch.filePath("pipeline.yaml"), scheduler).hasValue());
            barrier(scheduler);
            until([&] { return take(scheduler->snapshot()).queued == 0; });
        }
        ModuleManager observer;
        CHECK(take(observer.snapshot()).empty());
        CHECK(scheduler->isOpen() && take(scheduler->snapshot()).callbackErrors == 0);
    }
    auto pipeline = take(Pipeline::open(root, scratch.filePath("pipeline.yaml"), scheduler));
    CHECK(pipeline->scheduler() == scheduler);
    auto manager = pipeline->manager();
    CHECK(scheduler && manager && pipeline->kernel() && pipeline->driver() && pipeline->session());
    CHECK(pipeline->counter().session && pipeline->counter().runtime && pipeline->reporter().runtime);
    const auto counter = pipeline->counter().generation;
    std::weak_ptr<Kernel> kernel = pipeline->kernel();
    std::weak_ptr<loader::KernelReconcileDriver> driver = pipeline->driver();
    std::weak_ptr<loader::ConfigurationSession> session = pipeline->session();
    if (mode == "discarded_close") {
        const auto closing = pipeline->close();
        std::weak_ptr<Pipeline> retainedOwner = pipeline;
        pipeline.reset();
        take(scheduler->close());
        CHECK(take(scheduler->snapshot()).discarded > 0);
        const auto result = closing.tryResult();
        CHECK(result && !result->hasValue() && result->error().code == ErrorCode::SchedulerClosed);
        CHECK(!retainedOwner.expired() && !kernel.expired() && !driver.expired() && !session.expired());
        const auto states = take(manager->snapshot());
        CHECK(std::any_of(states.begin(), states.end(), [](const auto& state) { return !state.resources.drained(); }));
        CHECK(take(scheduler->snapshot()).callbackErrors == 0);
        // 独立故障进程保留真实未确认装配，不伪报正常排空。
        return;
    }
    std::optional<Runtime> retained;
    if (mode == "retained_runtime")
        retained = pipeline->counter().runtime;
    if (mode == "wrong_thread") {
        bool rejected = false;
        std::thread worker([&] {
            const auto attempted = pipeline->close().tryResult();
            rejected = attempted && !attempted->hasValue() && attempted->error().code == ErrorCode::WrongThread;
        });
        worker.join();
        CHECK(rejected && pipeline->session() && pipeline->kernel());
    }
    const auto closing = pipeline->close(), again = pipeline->close();
    const auto result = wait(closing, scheduler);
    CHECK(result && result->hasValue());
    const auto repeated = wait(again, scheduler);
    CHECK(repeated && repeated->hasValue());
    CHECK(repeated->value().counterDrain.hasValue() == result->value().counterDrain.hasValue());
    const auto& report = result->value();
    CHECK(report.session.hasValue() && report.kernel.hasValue() && report.kernel.value().quiescent);
    CHECK(report.counter.hasValue() && report.counter.value().quiescent);
    CHECK(report.reporter.hasValue() && report.reporter.value().quiescent);
    CHECK(report.remainingServices.hasValue() && report.remainingServices.value().empty());
    CHECK(report.reporterDrain.hasValue());
    barrier(scheduler);
    CHECK(kernel.expired() && driver.expired() && session.expired());
    CHECK(!pipeline->kernel() && !pipeline->driver() && !pipeline->session());
    CHECK(!pipeline->counter().runtime && !pipeline->reporter().runtime);
    if (retained) {
        CHECK(!report.counterDrain.hasValue() && report.counterDrain.error().code == ErrorCode::ContextClosing);
        const auto before = take(manager->snapshot());
        const auto found =
            std::find_if(before.begin(), before.end(), [&](const auto& row) { return row.generation == counter; });
        CHECK(found != before.end() && !found->resources.drained());
        retained.reset();
        barrier(scheduler);
        take(manager->drain(counter));
        CHECK(!report.counterDrain.hasValue());
    } else
        CHECK(report.counterDrain.hasValue());
    for (const auto& state : take(manager->snapshot()))
        CHECK(state.state == ModuleState::Resident && state.nativePinned && state.resources.drained());
    std::weak_ptr<Pipeline> weak = pipeline;
    pipeline.reset();
    barrier(scheduler);
    CHECK(weak.expired());
    const auto queue = take(scheduler->snapshot());
    CHECK(queue.queued == 0 && queue.discarded == 0 && queue.callbackErrors == 0);
    take(scheduler->close());
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        CHECK(argc == 3);
        const auto mode = std::string(argv[2]);
        CHECK(mode == "empty_close" || mode == "retained_runtime" || mode == "wrong_thread" || mode == "open_failure" ||
              mode == "discarded_close");
        run(QDir(QString::fromLocal8Bit(argv[1])).absolutePath(), mode);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
