/** @file module_state_qt.cpp
 * @brief 真实 Qt DLL 状态协议、初始导入及嵌套事件下的对象删除保护。
 */
#include "../../src/core/instance_state_p.hpp"
#include "../../src/module/catalog_p.hpp"
#include "../../src/qt/object_call_p.hpp"
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QVariant>
#include <algorithm>
#include <eve/kernel.hpp>
#include <eve/module/qt_module.hpp>
#include <eve/qt/object.hpp>
#include <eve_abi/state.h>
#include <cstring>
#include <exception>
#include <iostream>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
using namespace eve;
using namespace eve::module;
namespace files = eve::testing::modules;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
namespace {
void events()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
template <class T> typename Task<T>::Snapshot wait(const Task<T>& task)
{
    QElapsedTimer timer;
    timer.start();
    while (!task.isReady()) {
        CHECK(timer.elapsed() < 5000);
        events();
        QThread::msleep(1);
    }
    return task.tryResult();
}
std::function<void(int)> hook;
std::exception_ptr hookFailure;
void EVE_CALL callback(int phase)
{
    try {
        if (hook)
            hook(phase);
    } catch (...) {
        hookFailure = std::current_exception();
    }
}
struct Scene {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ModuleManager manager;
    GenerationId generation;
    std::optional<ModuleLease> inspection;
    HMODULE image = nullptr;
    Scene(const QString& root, const QString& fixture)
    {
        const auto manifest = files::install(fixture, root + "/plugin", "org.eve.qt.example",
                                             QJsonArray{"org.eve.qt.page", "org.eve.qt.settings"}, "qt-ui");
        generation = loadQtModule(root.toStdString(), manifest.toStdString()).value().generation;
        const auto path = QDir::toNativeSeparators(root + "/plugin/module.dll").toStdWString();
        image = GetModuleHandleW(path.c_str());
        CHECK(image);
    }
    template <class F> F function(const char* name) const
    {
        const auto address = GetProcAddress(image, name);
        CHECK(address);
        F result;
        static_assert(sizeof(result) == sizeof(address), "Windows function pointer");
        std::memcpy(&result, &address, sizeof(result));
        return result;
    }
    template <class F, class... Args> auto probe(const char* name, Args... args) const
    {
        auto source = eve::module::detail::findGeneration(generation);
        CHECK(source);
        ModuleLease lease;
        if (source->ledger.snapshot().accepting)
            lease = source->ledger.acquire(ResourceKind::Call, "test.qt.state.probe").value();
        else
            CHECK(inspection); // 协商失败已封账，检查使用失败前取得的真实 Call。
        return function<F>(name)(args...);
    }
    int count(int index) const { return probe<int(EVE_CALL*)(int)>("eve_qt_state_count", index); }
    QObject* object(int index) const { return probe<QObject*(EVE_CALL*)(int)>("eve_qt_state_object", index); }
    void observe(std::function<void(int)> value)
    {
        hook = std::move(value);
        probe<void(EVE_CALL*)(void(EVE_CALL*)(int))>("eve_qt_state_hook", hook ? &callback : nullptr);
    }
    void mode(const QString& value) { QCoreApplication::instance()->setProperty("eve.qt.stateMode", value); }
    Runtime runtime(const std::string& plugin = "org.eve.qt.page")
    {
        return qtRuntime(generation, {plugin, {}, {}}, scheduler).value();
    }
    std::size_t records(ResourceKind kind) const
    {
        const auto snapshot = eve::module::detail::findGeneration(generation)->ledger.snapshot();
        return static_cast<std::size_t>(std::count_if(snapshot.records.begin(), snapshot.records.end(),
                                                      [&](const ResourceRecord& value) { return value.kind == kind; }));
    }
    void faulted() const
    {
        const auto data = manager.snapshot().value().front();
        CHECK(data.state == ModuleState::Failed && data.businessFailure &&
              data.businessFailure->code == ErrorCode::AbiProtocol);
        CHECK(std::find(data.resources.pinReasons.begin(), data.resources.pinReasons.end(), "qt.businessUncertain") !=
              data.resources.pinReasons.end());
        CHECK(records(ResourceKind::Call) == 0);
    }
    ~Scene() { hook = {}; }
};
Instance mount(Scene& s, const Runtime& runtime, StateSnapshot input = {{"counter", 1}, "9007199254740993"})
{
    return Instance::mount(runtime, s.scheduler, "\"need-restore\"", {}, {}, {}, std::move(input));
}
void roundtrip(Scene& s)
{
    auto runtime = s.runtime();
    auto restored = mount(s, runtime);
    CHECK(wait(restored.ready())->hasValue());
    CHECK(s.object(0)->property("activatedState").toByteArray() == "9007199254740993");
    CHECK(s.count(1) == 1 && s.count(2) == 1 && s.count(4) == 1);
    CHECK(wait(restored.stop())->value().quiescent && s.count(5) == 1 && s.count(6) == 1);
    auto registered = s.kernel->registerRuntime("state", runtime).value();
    auto first = s.kernel->mount(registered).value(), second = s.kernel->mount(registered).value();
    CHECK(wait(first.ready())->hasValue() && wait(second.ready())->hasValue());
    s.object(1)->setProperty("state", "11");
    s.object(2)->setProperty("state", "22");
    auto scope = s.kernel->reserveReplacement({registered.id()}).value();
    scope.freeze().value();
    auto a = eve::detail::InstanceStateAccess::capture(scope, first).value();
    auto b = eve::detail::InstanceStateAccess::capture(scope, second).value();
    CHECK(a && b && a->json == "11" && b->json == "22" && a->schema.version == 2);
    s.mode("capture_busy");
    CHECK(eve::detail::InstanceStateAccess::capture(scope, first).error().code == ErrorCode::ConfigConflict);
    CHECK(s.manager.snapshot().value()[0].state == ModuleState::Serving);
    s.mode({});
    CHECK(eve::detail::InstanceStateAccess::capture(scope, first).value()->json == "11");
    scope.release().value();
    CHECK(wait(s.kernel->close())->value().quiescent && s.count(5) == 3 && s.count(6) == 3);
}
void origin(Scene& s)
{
    std::optional<Runtime> definition{s.runtime()};
    const auto id = definition->id();
    std::optional<Runtime> copy{*definition};
    definition.reset();
    auto instance = mount(s, *copy);
    copy.reset();
    CHECK(wait(instance.ready())->hasValue());
    CHECK(s.manager.runtimeOrigin(id).value()->generation == s.generation);
    CHECK(wait(instance.stop())->value().quiescent);
    CHECK(!s.manager.runtimeOrigin(id).value() && s.count(5) == 1);
}
void failure(Scene& s, const QString& name)
{
    auto runtime = s.runtime();
    auto instance =
        mount(s, runtime, {{name == "schema_id" ? "another" : "counter", name == "schema_version" ? 3U : 1U}, "57"});
    auto ready = wait(instance.ready());
    CHECK(!ready->hasValue());
    const bool protocol = name.endsWith("pending") || name.endsWith("throw") || name == "missing_iid";
    CHECK(ready->error().code == (protocol ? ErrorCode::AbiProtocol : ErrorCode::ConfigInvalid));
    const auto report = wait(instance.stop());
    const bool created = name.startsWith("restore") || name == "missing_iid";
    CHECK(s.count(4) == 0 && s.count(7) == (created ? 1 : 0));
    if (protocol) {
        s.faulted();
        CHECK(report->value().quiescent == !created);
        if (created)
            CHECK(s.records(ResourceKind::Instance) == 1);
    } else {
        CHECK(report->value().quiescent && s.records(ResourceKind::Instance) == 0);
        CHECK(s.manager.snapshot().value()[0].state == ModuleState::Loaded);
        CHECK(s.count(5) == (created ? 1 : 0) && s.count(6) == (created ? 1 : 0));
    }
}
void capture(Scene& s, const QString& name)
{
    auto runtime = s.kernel->registerRuntime("state", s.runtime()).value();
    auto instance = s.kernel->mount(runtime).value();
    CHECK(wait(instance.ready())->hasValue());
    auto scope = s.kernel->reserveReplacement({runtime.id()}).value();
    scope.freeze().value();
    s.mode(name);
    auto result = eve::detail::InstanceStateAccess::capture(scope, instance);
    CHECK(!result.hasValue() && result.error().code == ErrorCode::AbiProtocol);
    s.faulted();
    const bool deleted = name == "capture_deleted";
    CHECK((s.object(0) != nullptr) == !deleted && s.count(5) == (deleted ? 1 : 0) &&
          s.records(ResourceKind::Instance) == 1);
    scope.release().value();
    CHECK(!wait(instance.stop())->value().quiescent);
    CHECK((s.object(0) != nullptr) == !deleted);
}
void guards(Scene& s, bool wrongThread)
{
    auto runtime = s.kernel->registerRuntime("state", s.runtime()).value();
    auto one = s.kernel->mount(runtime).value(), two = s.kernel->mount(runtime).value();
    CHECK(wait(one.ready())->hasValue() && wait(two.ready())->hasValue());
    auto scope = s.kernel->reserveReplacement({runtime.id()}).value();
    scope.freeze().value();
    if (wrongThread) {
        bool rejected = false;
        std::thread worker([&] {
            const auto result = eve::detail::InstanceStateAccess::capture(scope, one);
            rejected = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
        });
        worker.join();
        CHECK(rejected && s.count(3) == 0);
    } else {
        s.observe([&](int phase) {
            CHECK(phase == 3);
            const auto nested = eve::detail::InstanceStateAccess::capture(scope, two);
            CHECK(!nested.hasValue() && nested.error().code == ErrorCode::ConfigConflict && s.count(3) == 1);
        });
        CHECK(eve::detail::InstanceStateAccess::capture(scope, one).value()->json == "7");
        s.observe({});
    }
    scope.release().value();
    CHECK(wait(s.kernel->close())->value().quiescent && s.count(5) == 2 && s.count(6) == 2);
}
void lifecycle(Scene& s, const QString& name)
{
    const bool restoring = name.startsWith("restore");
    const bool quit = name.endsWith("quit");
    auto runtime = s.kernel->registerRuntime("state", s.runtime()).value();
    std::optional<Instance> instance;
    std::optional<Task<CleanupReport>> stopped;
    std::optional<ReplacementScope> scope;
    QPointer<QObject> object;
    s.observe([&](int phase) {
        if (restoring && phase == 1)
            return;
        CHECK(phase == (restoring ? 2 : 3));
        object = s.object(0);
        CHECK(object);
        if (quit)
            CHECK(QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::DirectConnection));
        else
            stopped = restoring ? instance->stop() : scope->stop();
        events(); // 故意违反插件禁止泵事件的契约，检验宿主删除保护。
        CHECK(object && s.count(5) == 0 && s.count(6) == 0);
        if (stopped)
            CHECK(!stopped->isReady());
    });
    if (restoring) {
        instance = mount(s, runtime, {{"counter", 1}, "57"});
        if (quit) {
            QElapsedTimer timer;
            timer.start();
            while (s.count(2) == 0) {
                CHECK(timer.elapsed() < 5000);
                events();
            }
        } else
            CHECK(!wait(instance->ready())->hasValue());
        CHECK(s.count(4) == 0);
    } else {
        instance = s.kernel->mount(runtime).value();
        CHECK(wait(instance->ready())->hasValue());
        scope.emplace(s.kernel->reserveReplacement({runtime.id()}).value());
        scope->freeze().value();
        const auto result = eve::detail::InstanceStateAccess::capture(*scope, *instance);
        CHECK(!result.hasValue() && result.error().code == ErrorCode::ContextClosing);
    }
    s.observe({});
    if (quit) {
        events();
        CHECK(!object && s.count(5) == 1 && s.count(6) == 1 && !s.scheduler->isOpen());
    } else {
        CHECK(stopped && wait(*stopped)->value().quiescent);
        CHECK(!object && s.count(5) == 1 && s.count(6) == 1);
        if (scope)
            scope->release().value();
    }
}
void objectGuard()
{
    auto scheduler = std::make_shared<qt::QtScheduler>();
    auto owned = std::make_unique<QObject>();
    QPointer<QObject> observed = owned.get();
    qt::QObjectResource resource(owned, scheduler);
    std::optional<qt::detail::ObjectCall> first{qt::detail::ObjectCall::enter(resource).value()};
    std::optional<qt::detail::ObjectCall> second{qt::detail::ObjectCall::enter(resource).value()};
    auto closed = resource.close();
    events();
    CHECK(observed && !closed.isReady() && !qt::detail::ObjectCall::enter(resource).hasValue());
    first.reset();
    events();
    CHECK(observed && !closed.isReady());
    second.reset();
    CHECK(wait(closed)->hasValue() && !observed && resource.snapshot().value().confirmed);
    CHECK(scheduler->close().hasValue());
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        const auto name = args[1];
        if (name == "object_guard") {
            objectGuard();
            return 0;
        }
        app.setProperty("eve.qt.stateMode", name);
        QTemporaryDir temporary(args[2] + "/run-XXXXXX");
        CHECK(temporary.isValid());
        temporary.setAutoRemove(false);
        const auto fixtures = args[3].split(';');
        CHECK(fixtures.size() == 2);
        Scene s(temporary.path(), fixtures[name == "legacy" ? 1 : 0]);
        if (name.startsWith("describe")) {
            s.inspection.emplace(eve::module::detail::findGeneration(s.generation)
                                     ->ledger.acquire(ResourceKind::Call, "test.qt.state.rejectedInspection")
                                     .value());
            auto rejected = qtRuntime(s.generation, {"org.eve.qt.page", {}, {}}, s.scheduler);
            CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::AbiProtocol);
            s.mode({});
            CHECK(!qtRuntime(s.generation, {"org.eve.qt.page", {}, {}}, s.scheduler).hasValue());
            CHECK(s.count(0) == 1 && s.count(7) == 0 && !s.manager.snapshot().value()[0].businessAccepted);
            s.inspection.reset();
            CHECK(s.records(ResourceKind::Call) == 0);
        } else if (name == "metadata") {
            auto first = s.runtime(), second = s.runtime(), stateless = s.runtime("org.eve.qt.settings");
            CHECK(first.stateSchema()->id == "counter" && second.stateSchema()->version == 2 &&
                  !stateless.stateSchema());
            CHECK(s.count(0) == 2 && s.count(7) == 0 && s.count(1) == 0);
        } else if (name == "roundtrip")
            roundtrip(s);
        else if (name == "origin_lifetime")
            origin(s);
        else if (name == "reentry" || name == "wrong_thread")
            guards(s, name == "wrong_thread");
        else if (name == "legacy" || name == "stateless") {
            auto runtime = s.runtime();
            CHECK(!runtime.stateSchema());
            auto instance = Instance::mount(runtime, s.scheduler);
            CHECK(wait(instance.ready())->hasValue() && wait(instance.stop())->value().quiescent);
        } else if (name.startsWith("capture") && !name.endsWith("close") && !name.endsWith("quit"))
            capture(s, name);
        else if (name.endsWith("close") || name.endsWith("quit"))
            lifecycle(s, name);
        else
            failure(s, name);
        if (hookFailure)
            std::rethrow_exception(hookFailure);
        if (s.scheduler->isOpen())
            CHECK(s.scheduler->close().hasValue());
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
