/** Exercise the desktop host's real scheduler and kernel with no desktop module or QWidget application. */
#include <eve/kernel.hpp>
#include <eve/qt/scheduler.hpp>
#include <eve/ui/pages.hpp>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <iostream>

using namespace eve;

namespace {
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

template<class T> void wait(const Task<T>& task, const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    const auto observed = task.thenOn(scheduler, [&](const Result<T>&) { loop.quit(); });
    timeout.start(5000);
    loop.exec();
    CHECK(observed.isReady() && task.isReady() && task.tryResult()->hasValue());
}

void exercise()
{
    auto scheduler = std::make_shared<qt::QtScheduler>();
    Kernel kernel(scheduler);
    bool desktopConsumerEntered = false, released = false;
    int result = 0;
    const auto pageConsumer = kernel.registerPlugin("test.needs.desktop", [&](Context) {
        desktopConsumerEntered = true;
        return completedTask();
    }, {ServiceRequirement::of<ui::UiPagesService>({"ui.pages"})}).value();
    const auto waiting = kernel.mount(pageConsumer).value();
    const auto service = kernel.registerPlugin("test.headless.provider", [&](Context context) {
        context.provide<int>({"test.value"}, std::make_shared<int>(42)).value();
        context.onCleanup([&](CleanupContext) {
            released = true;
            return completedTask();
        }).value();
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"test.value"})}).value();
    const auto consumer = kernel.registerPlugin("test.headless.consumer", [&](Context context) {
        const auto ref = context.consume<int>({"test.value"}).value();
        return completedTask().thenOn(scheduler, [ref, &result](const Result<void>&) {
            ref.withService([&](int& value) { result = value; }).value();
        });
    }, {ServiceRequirement::of<int>({"test.value"})}).value();
    auto reader = kernel.mount(consumer).value();
    auto provider = kernel.mount(service).value();
    wait(reader.ready(), scheduler);
    CHECK(result == 42 && provider.snapshot().value().state == InstanceState::Active);
    CHECK(waiting.snapshot().value().state == InstanceState::Pending && !desktopConsumerEntered);
    CHECK(!kernel.services().find<ui::UiPagesService>({"ui.pages"}).hasValue());
    const auto closed = kernel.close();
    wait(closed, scheduler);
    CHECK(closed.tryResult()->value().quiescent && released && !desktopConsumerEntered);
    scheduler->close().value();
}
} // namespace

int main(int argc, char** argv)
{
    const QCoreApplication application(argc, argv);
    try {
        exercise();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
