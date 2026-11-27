/** @file ui_publication_p.hpp
 * @brief 页面、命令与 Ela 发布检查共用真实 Qt 等待和宿主拥有者。
 */
#pragma once
#include <ElaWindow.h>
#include <QApplication>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>
#include <eve/kernel.hpp>
#include <eve/ui/ela_shell.hpp>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition))                                                                                              \
            throw std::runtime_error(std::string(#condition) + " at " + __FILE__ + ":" + std::to_string(__LINE__));    \
    } while (false)

namespace presentation_test {
using namespace eve;
using namespace eve::ui;
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
    QTimer progress, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&progress, &QTimer::timeout, &loop, [&] {
        if (predicate())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    progress.start(0);
    deadline.start(8000);
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
    const auto result = task.tryResult();
    CHECK(result);
    return result;
}
inline void barrier(const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    Promise<void> reached;
    CHECK(scheduler->post({[reached] { reached.resolve(); },
                           [reached] { reached.reject(makeError(ErrorCode::SchedulerClosed, "barrier discarded")); }}));
    CHECK(wait(reached.task(), scheduler)->hasValue());
}
struct Scene {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    ElaShell shell{scheduler};
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    std::shared_ptr<UiPagesService> pages = shell.pages();
    std::shared_ptr<CommandService> commands = shell.commands();
    QPointer<ElaWindow> window;

    Scene()
    {
        take(shell.management()->attach(kernel));
        take(shell.show());
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* host = qobject_cast<ElaWindow*>(widget))
                window = host;
        CHECK(window);
    }
    template <class T> T* find(const char* name) const
    {
        CHECK(window);
        auto* found = window->findChild<T*>(QString::fromLatin1(name));
        CHECK(found);
        return found;
    }
    void close()
    {
        const auto stopped = wait(kernel->close(), scheduler);
        CHECK(stopped->hasValue() && stopped->value().quiescent);
        CHECK(take(pages->snapshot()).empty() && take(commands->snapshot()).empty());
        CHECK(wait(shell.close(), scheduler)->hasValue());
        CHECK(!window);
        kernel.reset();
        CHECK(take(scheduler->snapshot()).callbackErrors == 0);
        take(scheduler->close());
    }
};
} // namespace presentation_test
