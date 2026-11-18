/** @file notebook_state_p.hpp
 * @brief Notebook 实际状态检查的轻量等待、值构造和用例入口。
 */
#pragma once
#include <QEventLoop>
#include <QString>
#include <QTimer>
#include <cordis/migration_state.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cordis/task.hpp>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#define NOTEBOOK_CHECK(value)                                                                                          \
    do {                                                                                                               \
        if (!(value))                                                                                                  \
            throw std::runtime_error(std::string(#value) + " at " + __FILE__ + ":" + std::to_string(__LINE__));        \
    } while (false)

namespace notebook_test {
using namespace cordis;
inline constexpr char schemaId[] = "org.cordis.example.notebook.draft";
template <class T> T take(Result<T> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().phase + ": " + result.error().message);
    return std::move(result).value();
}
inline void take(Result<void> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().phase + ": " + result.error().message);
}
inline void until(const std::function<bool()>& predicate)
{
    if (predicate())
        return;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (predicate())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(1);
    deadline.start(8000);
    loop.exec();
    NOTEBOOK_CHECK(predicate());
}
template <class T>
typename Task<T>::Snapshot wait(const Task<T>& task, const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    const auto delivered = std::make_shared<bool>(false);
    const auto receipt = task.thenOn(scheduler, [delivered](const Result<T>&) { *delivered = true; });
    until([&] { return receipt.isReady(); });
    NOTEBOOK_CHECK(*delivered && receipt.tryResult()->hasValue());
    const auto result = task.tryResult();
    NOTEBOOK_CHECK(result);
    return result;
}
inline void barrier(const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    Promise<void> reached;
    NOTEBOOK_CHECK(
        scheduler->post({[reached] { reached.resolve(); },
                         [reached] { reached.reject(makeError(ErrorCode::SchedulerClosed, "barrier discarded")); }}));
    NOTEBOOK_CHECK(wait(reached.task(), scheduler)->hasValue());
}
std::string manifest(const QString& variant);
std::string draftJson(const QString& text);
StateSnapshot draftState(const QString& text);
bool runtimeCase(const QString& name, const QString& root);
bool protocolCase(const QString& name, const QString& root);
} // namespace notebook_test
