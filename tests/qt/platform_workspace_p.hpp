/** @file platform_workspace_p.hpp
 * @brief 单一实际 Workspace 的有限装配与关闭检查，不复制插件或事务实现。
 */
#pragma once
#include "workspace.hpp"
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#define PLATFORM_CHECK(value)                                                                                          \
    do {                                                                                                               \
        if (!(value))                                                                                                  \
            throw std::runtime_error(std::string(#value) + " at " + __FILE__ + ":" + std::to_string(__LINE__));        \
    } while (false)

class QWidget;
namespace platform_workspace_test {
using namespace cordis;
using namespace cordis::loader;
using namespace cordis::module;
using namespace cordis::ui;
using notebook::take;

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
    deadline.start(10000);
    loop.exec();
    PLATFORM_CHECK(predicate());
}
template <class T>
typename Task<T>::Snapshot wait(const Task<T>& task, const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    const auto delivered = std::make_shared<bool>(false);
    const auto receipt = task.thenOn(scheduler, [delivered](const Result<T>&) { *delivered = true; });
    until([&] { return receipt.isReady(); });
    PLATFORM_CHECK(*delivered && receipt.tryResult()->hasValue());
    const auto result = task.tryResult();
    PLATFORM_CHECK(result);
    return result;
}
inline void barrier(const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    Promise<void> reached;
    PLATFORM_CHECK(
        scheduler->post({[reached] { reached.resolve(); },
                         [reached] { reached.reject(makeError(ErrorCode::SchedulerClosed, "barrier discarded")); }}));
    PLATFORM_CHECK(wait(reached.task(), scheduler)->hasValue());
}
inline std::string utf8(const QString& value)
{
    return value.toUtf8().toStdString();
}

struct Scene {
    QTemporaryDir scratch;
    QString store, file;
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<notebook::Workspace> state;

    Scene();
    void open(const QString& sourceBin);
    AcceptedTarget apply(const std::string& text, PlanOutcome expected = PlanOutcome::Applied);
    std::vector<PageSnapshot> pages() const;
    QWidget* page(InstanceId instance) const;
    void edit(InstanceId instance, const QString& text) const;
    void business(std::size_t count = 1, bool separateRealms = false) const;
    void verifyClosed(bool counterDrained = true) const;
    void release();
};

void verifySources(const Scene& scene, const std::string& version);
void installSession(Scene& scene, const QString& sourceBin, const std::string& variant, bool invalidCounter = false);
bool runtimeCase(const QString& name, const QString& sourceBin);
bool closeCase(const QString& name, const QString& sourceBin);
} // namespace platform_workspace_test
