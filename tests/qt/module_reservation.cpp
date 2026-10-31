/** @file module_reservation.cpp
 * @brief 双版本真实 DLL 上的进程级预约、创建准入、排空和重入。
 */
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <cordis/module/manager.hpp>
#include <cordis/module/native.hpp>
#include <cordis/module/runtime.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#ifdef CORDIS_TEST_QT_RESERVATION
#include <cordis/module/qt_module.hpp>
#include <cordis/qt/scheduler.hpp>
#endif
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <optional>
#include <thread>
#include <type_traits>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
using namespace cordis;
using namespace cordis::module;
namespace files = cordis::testing::modules;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
static_assert(!std::is_copy_constructible_v<ReplacementReservation> &&
              std::is_nothrow_move_constructible_v<ReplacementReservation>);
struct Scene {
    ModuleManager manager;
    bool qt;
    QString root;
    QStringList dlls;
    std::string oldPath, nextPath;
    GenerationId current, candidate;
    Scene(bool backend, QString directory, QStringList paths)
        : qt(backend), root(std::move(directory)), dlls(std::move(paths))
    {
        CHECK(dlls.size() == 5);
        oldPath = install(dlls[0], "old").toStdString();
        nextPath = install(dlls[1], "next", "2.0.0").toStdString();
        current = load(oldPath).value().generation;
        candidate = stage(nextPath).value().generation;
    }
    QString install(const QString& dll, const QString& dir, const QString& version = "1.0.0") const
    {
        return files::install(dll, root + '/' + dir, qt ? "org.cordis.qt.example" : "org.cordis.business",
                              qt ? QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"}
                                 : QJsonArray{"org.cordis.plugin"},
                              qt ? "qt-ui" : "c-abi", version);
    }
    Result<ModuleLoadReport> load(const std::string& path)
    {
#ifdef CORDIS_TEST_QT_RESERVATION
        if (qt)
            return loadQtModule(root.toStdString(), path);
#endif
        return manager.load(root.toStdString(), path);
    }
    Result<ModuleLoadReport> stage(const std::string& path)
    {
#ifdef CORDIS_TEST_QT_RESERVATION
        if (qt)
            return stageQtModule(root.toStdString(), path, current);
#endif
        return manager.stage(root.toStdString(), path, current);
    }
    ModuleGenerationSnapshot state(GenerationId id) const
    {
        const auto values = manager.snapshot().value();
        auto found =
            std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.generation == id; });
        CHECK(found != values.end());
        return *found;
    }
    ReplacementReservation reserve() { return manager.reserveReplacement(current, candidate).value(); }
    void checkReserved(bool reserved) const
    {
        for (auto id : {current, candidate}) {
            const auto value = state(id);
            CHECK(value.replacement.has_value() == reserved);
            if (reserved)
                CHECK(value.replacement->current == current && value.replacement->candidate == candidate);
        }
        CHECK(state(current).role == GenerationRole::Current && state(candidate).role == GenerationRole::Candidate);
    }
};
void generic(Scene& s, const QString& name)
{
    if (name == "guards") {
        ReplacementReservation empty;
        CHECK(!empty.snapshot().hasValue());
        empty.reset();
        for (auto pair : {std::pair{GenerationId{}, s.candidate}, std::pair{s.current, s.current},
                          std::pair{s.candidate, s.current}, std::pair{s.current, GenerationId{999999}}})
            CHECK(!s.manager.reserveReplacement(pair.first, pair.second).hasValue());
        bool wrong = false;
        std::thread worker([&] {
            auto value = s.manager.reserveReplacement(s.current, s.candidate);
            wrong = !value.hasValue() && value.error().code == ErrorCode::WrongThread;
        });
        worker.join();
        CHECK(wrong);
        auto first = s.reserve();
        auto copy = first.snapshot().value();
        ModuleManager other;
        CHECK(!other.reserveReplacement(s.current, s.candidate).hasValue());
        s.checkReserved(true);
        auto second = std::move(first);
        CHECK(!first.snapshot().hasValue() && second.snapshot().value().current == s.current);
        first = std::move(second);
        first.reset();
        first.reset();
        s.checkReserved(false);
        CHECK(copy.candidate == s.candidate);
    } else if (name == "released") {
        ReplacementReservation guard;
        {
            ModuleManager owner;
            guard = owner.reserveReplacement(s.current, s.candidate).value();
        }
        s.checkReserved(true);
        guard.reset();
        s.checkReserved(false);
        {
            auto abandoned = s.reserve();
        }
        s.checkReserved(false);
    } else if (name == "worker") {
        auto scope = s.reserve();
        bool conflict = false;
        std::thread worker([guard = std::move(scope), &s, &conflict]() mutable {
            ModuleManager other;
            auto result = other.reserveReplacement(s.current, s.candidate);
            conflict = !result.hasValue() && result.error().code == ErrorCode::ConfigConflict;
            CHECK(guard.snapshot().hasValue());
            guard.reset();
        });
        worker.join();
        CHECK(conflict);
        s.checkReserved(false);
        auto next = s.reserve();
    } else if (name == "drain") {
        auto scope = s.reserve();
        CHECK(s.manager.drain(s.current).error().code == ErrorCode::ConfigConflict &&
              s.manager.drain(s.candidate).error().code == ErrorCode::ConfigConflict);
        CHECK(s.stage(s.nextPath).error().code == ErrorCode::ConfigConflict);
        const auto third = s.install(s.dlls[2], "third", "2.0.0");
        CHECK(s.stage(third.toStdString()).error().code == ErrorCode::ConfigConflict);
        CHECK(!GetModuleHandleW((s.root + "/third/module.dll").toStdWString().c_str()));
        CHECK(s.load(s.oldPath).value().generation == s.current);
        scope.reset();
        if (!s.qt) {
            s.manager.drain(s.candidate).value();
            CHECK(s.state(s.candidate).state == ModuleState::Resident);
            CHECK(!s.manager.reserveReplacement(s.current, s.candidate).hasValue());
        }
    } else if (name == "parallel") {
        auto peerOld =
            files::install(s.dlls[3], s.root + "/peer-old", "org.cordis.peer", {"org.cordis.plugin"}, "c-abi");
        auto peerNext = files::install(s.dlls[4], s.root + "/peer-next", "org.cordis.peer", {"org.cordis.plugin"},
                                       "c-abi", "2.0.0");
        const auto a = s.manager.load(s.root.toStdString(), peerOld.toStdString()).value().generation;
        const auto b = s.manager.stage(s.root.toStdString(), peerNext.toStdString(), a).value().generation;
        auto first = s.reserve();
        auto second = s.manager.reserveReplacement(a, b).value();
        first.reset();
        CHECK(s.manager.drain(b).error().code == ErrorCode::ConfigConflict && s.state(b).replacement.has_value());
        second.reset();
        s.manager.drain(b).value();
    } else if (name == "race") {
        std::atomic<unsigned> arrived{0}, attempted{0}, won{0};
        std::atomic_bool go{false};
        auto enter = [&] {
            ModuleManager manager;
            ++arrived;
            while (!go.load())
                std::this_thread::yield();
            auto guard = manager.reserveReplacement(s.current, s.candidate);
            if (guard.hasValue())
                ++won;
            ++attempted;
            while (attempted.load() != 2)
                std::this_thread::yield();
        };
        std::thread a(enter), b(enter);
        while (arrived.load() != 2)
            std::this_thread::yield();
        go.store(true);
        a.join();
        b.join();
        CHECK(won.load() == 1);
        s.checkReserved(false);
    } else
        throw std::runtime_error("unknown generic case");
}
void native(Scene& s, const QString& name)
{
    auto scheduler = std::make_shared<testing::FakeScheduler>();
    auto services = std::make_shared<ServiceRegistry>(scheduler);
    auto run = [&] { scheduler->runUntilIdle(30000).value(); };
    auto old = NativeModule::open(s.manager, s.current, scheduler, *services).value();
    auto next = NativeModule::open(s.manager, s.candidate, scheduler, *services).value();
    if (name == "failure") {
        auto path = s.install(s.dlls[2], "bad", "2.0.0");
        auto bad = s.stage(path.toStdString()).value().generation;
        CHECK(!NativeModule::open(s.manager, bad, scheduler).hasValue());
        CHECK(!s.manager.reserveReplacement(s.current, bad).hasValue());
        s.checkReserved(false);
        auto healthy = s.reserve();
    } else if (name == "candidate_live") {
        auto instance = next.create("org.cordis.plugin", "\"sync\"", InstanceId{77}, 1).value();
        CHECK(s.manager.reserveReplacement(s.current, s.candidate).error().code == ErrorCode::ConfigConflict);
        auto closed = instance.close();
        run();
        CHECK(closed.tryResult()->value().quiescent);
        auto scope = s.reserve();
    } else {
        CHECK(name == "admission");
        auto first = old.create("org.cordis.plugin", "\"sync\"", InstanceId{77}, 1).value();
        auto started = first.start();
        run();
        CHECK(started.tryResult()->hasValue());
        auto runtime = old.runtime({"org.cordis.plugin", {}, {}}).value();
        auto scope = s.reserve();
        const auto before = s.state(s.current).resources.records.size();
        CHECK(old.create("org.cordis.plugin", "\"sync\"", InstanceId{78}, 1).error().code == ErrorCode::ConfigConflict);
        CHECK(next.create("org.cordis.plugin", "\"sync\"", InstanceId{79}, 1).error().code ==
              ErrorCode::ConfigConflict);
        CHECK(NativeModule::open(s.manager, s.current, scheduler).error().code == ErrorCode::ConfigConflict);
        CHECK(old.runtime({"org.cordis.plugin", {}, {}}).error().code == ErrorCode::ConfigConflict);
        auto actor = Instance::mount(runtime, scheduler, "\"sync\"", services);
        run();
        CHECK(actor.ready().tryResult()->error().code == ErrorCode::ConfigConflict);
        CHECK(s.state(s.current).resources.records.size() == before &&
              first.snapshot().value().state == NativeInstanceState::Started);
        scope.reset();
        actor.retry().value();
        run();
        CHECK(actor.snapshot().value().state == InstanceState::Active);
        auto stopped = actor.stop();
        run();
        CHECK(stopped.tryResult()->value().quiescent);
        old.create("org.cordis.plugin", "\"sync\"", InstanceId{78}, 1).value();
        next.create("org.cordis.plugin", "\"sync\"", InstanceId{79}, 1).value();
    }
    for (const auto& module : {old, next}) {
        auto closed = module.close();
        run();
        CHECK(closed.tryResult()->value().quiescent);
    }
    scheduler->close();
}
#ifdef CORDIS_TEST_QT_RESERVATION
template <class T> auto wait(const Task<T>& task)
{
    QElapsedTimer timer;
    timer.start();
    while (!task.isReady()) {
        CHECK(timer.elapsed() < 5000);
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
    return task.tryResult();
}
class ConstructionFilter : public QObject {
public:
    std::function<void()> enter;
    bool observed = false;

protected:
    bool eventFilter(QObject* receiver, QEvent* event) override
    {
        if (!observed && event->type() == QEvent::ChildAdded &&
            std::strcmp(receiver->metaObject()->className(), "Business") == 0) {
            observed = true;
            enter();
        }
        return false;
    }
};
void qtCase(Scene& s, const QString& name)
{
    auto scheduler = std::make_shared<qt::QtScheduler>();
    auto definition = [&](GenerationId id) { return qtRuntime(id, {"org.cordis.qt.page", {}, {}}, scheduler); };
    if (name == "stage_reentry") {
        bool observed = false, denied = false;
        std::optional<ReplacementReservation> unexpected;
        QTimer::singleShot(0, QCoreApplication::instance(), [&] {
            auto guard = s.manager.reserveReplacement(s.current, s.candidate);
            observed = true;
            denied = !guard.hasValue() && guard.error().code == ErrorCode::ConfigConflict;
            if (guard.hasValue())
                unexpected = std::move(guard).value();
        });
        auto path = s.install(s.dlls[2], "third", "2.0.0");
        auto third = s.stage(path.toStdString()).value().generation;
        CHECK(observed && denied);
        auto guard = s.reserve();
        CHECK(definition(third).error().code == ErrorCode::ConfigConflict && !s.state(third).businessFailure);
    } else if (name == "sibling_query_reentry") {
        auto path = s.install(s.dlls[2], "third", "2.0.0");
        auto third = s.stage(path.toStdString()).value().generation;
        QCoreApplication::instance()->setProperty("cordis.qt.factoryMode", "query_reentry");
        bool observed = false, denied = false;
        QTimer::singleShot(0, QCoreApplication::instance(), [&] {
            auto guard = s.manager.reserveReplacement(s.current, third);
            observed = true;
            denied = !guard.hasValue() && guard.error().code == ErrorCode::ConfigConflict;
        });
        definition(s.candidate).value();
        CHECK(observed && denied);
        auto scope = s.manager.reserveReplacement(s.current, third).value();
    } else if (name == "query_reentry") {
        QCoreApplication::instance()->setProperty("cordis.qt.factoryMode", "query_reentry");
        bool observed = false, denied = false;
        QTimer::singleShot(0, QCoreApplication::instance(), [&] {
            auto guard = s.manager.reserveReplacement(s.current, s.candidate);
            observed = true;
            denied = !guard.hasValue() && guard.error().code == ErrorCode::ConfigConflict;
        });
        definition(s.current).value();
        CHECK(observed && denied);
        auto scope = s.reserve();
    } else if (name == "create_reentry") {
        auto runtime = definition(s.current).value();
        bool denied = false;
        ConstructionFilter filter;
        filter.enter = [&] {
            auto guard = s.manager.reserveReplacement(s.current, s.candidate);
            denied = !guard.hasValue() && guard.error().code == ErrorCode::ConfigConflict;
        };
        QCoreApplication::instance()->installEventFilter(&filter);
        auto actor = Instance::mount(runtime, scheduler);
        CHECK(wait(actor.ready())->hasValue());
        QCoreApplication::instance()->removeEventFilter(&filter);
        CHECK(filter.observed && denied);
        {
            auto guard = s.reserve();
        }
        CHECK(wait(actor.stop())->value().quiescent);
    } else if (name == "failure") {
        auto path = s.install(s.dlls[2], "bad", "2.0.0");
        auto bad = s.stage(path.toStdString()).value().generation;
        CHECK(!definition(bad).hasValue());
        CHECK(!s.manager.reserveReplacement(s.current, bad).hasValue());
        s.checkReserved(false);
        auto healthy = s.reserve();
    } else if (name == "candidate_live") {
        auto runtime = definition(s.candidate).value();
        auto actor = Instance::mount(runtime, scheduler);
        CHECK(wait(actor.ready())->hasValue());
        CHECK(s.manager.reserveReplacement(s.current, s.candidate).error().code == ErrorCode::ConfigConflict);
        CHECK(wait(actor.stop())->value().quiescent);
        auto scope = s.reserve();
    } else {
        CHECK(name == "admission");
        auto old = definition(s.current).value(), next = definition(s.candidate).value();
        auto actor = Instance::mount(old, scheduler);
        CHECK(wait(actor.ready())->hasValue());
        auto scope = s.reserve();
        const auto before = s.state(s.current).resources.records.size();
        CHECK(definition(s.current).error().code == ErrorCode::ConfigConflict &&
              definition(s.candidate).error().code == ErrorCode::ConfigConflict);
        auto blocked = Instance::mount(next, scheduler);
        CHECK(wait(blocked.ready())->error().code == ErrorCode::ConfigConflict);
        CHECK(actor.snapshot().value().state == InstanceState::Active &&
              s.state(s.current).resources.records.size() == before);
        CHECK(wait(actor.stop())->value().quiescent);
        scope.reset();
        auto retry = blocked.retry().value();
        CHECK(wait(retry.target)->value().outcome == StableOutcome::Active);
        CHECK(wait(blocked.stop())->value().quiescent);
    }
    CHECK(scheduler->close().hasValue());
}
#endif
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        const bool qt = args[1].startsWith("qt_");
        const auto name = args[1].mid(qt ? 3 : 7);
        Scene scene(qt, args[2], args[3].split(';'));
        if (name == "admission" || name == "candidate_live" || name == "failure" || name.endsWith("reentry")) {
#ifdef CORDIS_TEST_QT_RESERVATION
            if (qt)
                qtCase(scene, name);
            else
#endif
                native(scene, name);
        } else
            generic(scene, name);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
