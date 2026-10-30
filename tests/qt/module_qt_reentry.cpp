/** @file module_qt_reentry.cpp
 * @brief 真实工厂的描述/创建重入、创建中失活和构造串行门禁。
 */
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <cordis/module/qt_module.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cstring>
#include <iostream>
#include <optional>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
using namespace cordis;
using namespace cordis::module;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
class ConstructionFilter final : public QObject {
public:
    std::function<void()> enter;
    bool observed = false;
    std::string failure;

protected:
    bool eventFilter(QObject* receiver, QEvent* event) override
    {
        if (!observed && event->type() == QEvent::ChildAdded &&
            std::strcmp(receiver->metaObject()->className(), "Business") == 0) {
            observed = true;
            try {
                enter();
            } catch (const std::exception& error) {
                failure = error.what();
            } catch (...) {
                failure = "construction hook failed";
            }
        }
        return false;
    }
};
void pump(const std::function<bool()>& predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        CHECK(timer.elapsed() < 5000);
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        const auto mode = args[1];
        QTemporaryDir temporary(args[2] + "/run-XXXXXX");
        CHECK(temporary.isValid());
        temporary.setAutoRemove(false);
        const auto root = temporary.path();
        const auto manifest =
            testing::modules::install(args[3], root + "/plugin", "org.cordis.qt.example",
                                      QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"}, "qt-ui");
        auto loaded = loadQtModule(root.toStdString(), manifest.toStdString());
        CHECK(loaded.hasValue());
        auto scheduler = std::make_shared<qt::QtScheduler>();
        const auto library = (root + "/plugin/module.dll").toStdWString();
        const auto image = GetModuleHandleW(library.c_str());
        CHECK(image);
        const auto address = GetProcAddress(image, "cordis_qt_business_count");
        CHECK(address);
        int(__cdecl * count)(int) = nullptr;
        static_assert(sizeof(count) == sizeof(address), "entry");
        std::memcpy(&count, &address, sizeof(count));
        bool queryObserved = false, queryRejected = false;
        if (mode == "query") {
            app.setProperty("cordis.qt.factoryMode", "query_reentry");
            QTimer::singleShot(0, &app, [&] {
                auto nested = qtRuntime(loaded.value().generation, {"org.cordis.qt.page", {}, {}}, scheduler);
                queryObserved = true;
                queryRejected = !nested.hasValue() && nested.error().code == ErrorCode::ModuleLoad;
            });
        }
        auto runtime = qtRuntime(loaded.value().generation, {"org.cordis.qt.page", {}, {}}, scheduler);
        CHECK(runtime.hasValue());
        if (mode == "query") {
            CHECK(queryObserved && queryRejected && count(1) == 1 && count(2) == 0);
            CHECK(scheduler->close().hasValue());
            return 0;
        }
        std::optional<Instance> instance, second;
        std::optional<OperationHandle> update;
        auto other = std::make_shared<qt::QtScheduler>();
        auto secondRuntime = qtRuntime(loaded.value().generation, {"org.cordis.qt.settings", {}, {}}, other);
        CHECK(secondRuntime.hasValue());
        bool createRejected = false;
        ConstructionFilter filter;
        filter.enter = [&] {
            if (mode == "create_stop")
                instance->stop();
            else if (mode == "create_update") {
                auto changed = instance->update("new", 1);
                CHECK(changed.hasValue());
                update = changed.value();
            } else if (mode == "create_busy") {
                second = Instance::mount(secondRuntime.value(), other);
                pump([&] { return second->ready().isReady(); });
                const auto result = second->ready().tryResult();
                createRejected = !result->hasValue() && result->error().code == ErrorCode::ModuleLoad;
            } else
                CHECK(false);
        };
        app.installEventFilter(&filter);
        instance = Instance::mount(runtime.value(), scheduler);
        pump([&] { return filter.observed; });
        CHECK(filter.failure.empty());
        if (mode == "create_update") {
            CHECK(update);
            pump([&] { return update->target.isReady(); });
            CHECK(update->target.tryResult()->value().outcome == StableOutcome::Active);
        }
        if (mode == "create_busy") {
            pump([&] { return instance->ready().isReady(); });
            CHECK(instance->ready().tryResult()->hasValue());
        }
        auto stopped = instance->stop();
        pump([&] { return stopped.isReady(); });
        CHECK(stopped.tryResult()->value().quiescent);
        if (second) {
            auto closed = second->stop();
            pump([&] { return closed.isReady(); });
            CHECK(closed.tryResult()->value().quiescent);
        }
        const int expectedCreate = mode == "create_update" ? 2 : 1;
        const int expectedActivate = mode == "create_stop" ? 0 : 1;
        std::cout << "created=" << count(2) << " activated=" << count(3) << " destroyed=" << count(5)
                  << " children=" << count(6) << '\n';
        CHECK(count(2) == expectedCreate && count(3) == expectedActivate && count(5) == expectedCreate &&
              count(6) == expectedCreate && count(7) == 0);
        if (mode == "create_busy")
            CHECK(createRejected);
        CHECK(count(1) == 1);
        app.removeEventFilter(&filter);
        CHECK(scheduler->close().hasValue() && other->close().hasValue());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
