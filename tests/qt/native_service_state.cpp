/** @file native_service_state.cpp
 * @brief 实际 A/B 业务 DLL 经原 Native Runtime/Core 状态桥导入、冻结捕获及消费。
 */
#include "../../src/core/instance_state_p.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <algorithm>
#include <eve/kernel.hpp>
#include <eve/module/manager.hpp>
#include <eve/module/runtime.hpp>
#include <eve/qt/scheduler.hpp>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

bool nativeServiceStateAbiCase(const QString&, const QString&);
namespace {
using namespace eve;
using namespace eve::module;
#define CHECK(value)                                                                                                   \
    do {                                                                                                               \
        if (!(value))                                                                                                  \
            throw std::runtime_error(std::string(#value) + " at " + std::to_string(__LINE__));                         \
    } while (false)
template <class T> T take(Result<T> value)
{
    if (!value.hasValue())
        throw std::runtime_error(value.error().phase + ": " + value.error().message);
    return std::move(value).value();
}
void take(Result<void> value)
{
    if (!value.hasValue())
        throw std::runtime_error(value.error().message);
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
NativeUnloadWhitelist qualification(const QString& root)
{
    NativeUnloadWhitelist allowed;
    for (const auto* name : {"counter", "reporter"})
        for (const auto* variant : {"a", "b"}) {
            const auto manifest = std::string("native-upgrade/") + name + "-" + variant + "/module.json";
            const auto artifact = take(preflightModule(root.toStdString(), manifest));
            const auto id = std::string("org.eve.example.") + name;
            CHECK(artifact.manifest().moduleId == id && artifact.manifest().loader == ModuleLoader::CAbi);
            CHECK(artifact.manifest().moduleVersion == (std::string(variant) == "a" ? "1.1.0" : "1.2.0"));
            CHECK(artifact.manifest().plugins == std::vector<std::string>{std::string("example.") + name});
            CHECK(artifact.manifest().unloadPolicy == ModuleUnloadPolicy::Explicit);
            // 仅这四个编译目标的既定身份/实际摘要；不接收用户所选的任意 manifest。
            allowed.push_back({id, artifact.manifest().sha256});
        }
    return allowed;
}
struct Scene {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ModuleManager manager;
    GenerationId counterId, reporterId;
    std::optional<NativeModule> counter, reporter;
    std::optional<Runtime> counterRuntime, reporterRuntime;
    std::vector<Runtime> parents;
    Scene(const QString& root, const std::string& variant)
    {
        const auto allowed = qualification(root);
        const auto path = [&](const char* name) {
            return std::string("native-upgrade/") + name + "-" + variant + "/module.json";
        };
        const auto denied = manager.load(root.toStdString(), path("counter"));
        CHECK(!denied.hasValue() && denied.error().code == ErrorCode::ModulePinned);
        CHECK(take(manager.snapshot()).empty());
        counterId = take(manager.load(root.toStdString(), path("counter"), {}, allowed)).generation;
        reporterId = take(manager.load(root.toStdString(), path("reporter"), {}, allowed)).generation;
        counter.emplace(take(NativeModule::open(manager, counterId, scheduler, kernel->services())));
        reporter.emplace(take(NativeModule::open(manager, reporterId, scheduler, kernel->services())));
        counterRuntime.emplace(take(kernel->registerRuntime(
            "counter",
            take(counter->runtime({"example.counter", {}, {{{"example.counter", 1}, true, {1, {"next"}}}}})))));
        reporterRuntime.emplace(take(kernel->registerRuntime(
            "reporter", take(reporter->runtime({"example.reporter",
                                                {{{"example.counter", 1}, 1, {"next"}}},
                                                {{{"example.report", 1}, true, {1, {"read"}}}}})))));
        CHECK(counterRuntime->stateSchema() && counterRuntime->stateSchema()->id == "org.eve.example.counter" &&
              counterRuntime->stateSchema()->version == 1);
        CHECK(!reporterRuntime->stateSchema());
        for (const auto& state : take(manager.snapshot()))
            CHECK(state.businessAccepted && state.descriptorAccepted && !state.businessFailure && !state.nativePinned &&
                  state.loadPolicy == ModuleLoadPolicy::ApprovedNative);
    }
    Instance imported(StateSnapshot state, const std::shared_ptr<std::optional<Instance>>& child, RealmId realm = {})
    {
        const auto number = parents.size();
        auto parent = take(kernel->registerPlugin(
            "importer." + std::to_string(number),
            [runtime = *counterRuntime, child, state = std::move(state), realm](Context context) {
                const auto source =
                    realm.valid() ? context.isolate("example.counter", realm) : Result<Context>::success(context);
                if (!source.hasValue())
                    return failedTask(source.error());
                auto mounted = source.value().inject(runtime, "null", {}, state);
                if (!mounted.hasValue())
                    return failedTask(mounted.error());
                child->emplace(std::move(mounted).value());
                return (*child)->ready();
            }));
        parents.push_back(parent);
        return take(kernel->mount(parent, "null"));
    }
    std::string capture(const Instance& instance)
    {
        auto scope = take(kernel->reserveReplacement({counterRuntime->id()}));
        take(scope.freeze());
        const auto value = take(eve::detail::InstanceStateAccess::capture(scope, instance));
        CHECK(value && value->schema.id == "org.eve.example.counter" && value->schema.version == 1);
        take(scope.release());
        barrier(scheduler);
        return value->json;
    }
    void close()
    {
        CHECK(wait(kernel->close(), scheduler)->value().quiescent);
        CHECK(wait(counter->close(), scheduler)->value().quiescent);
        CHECK(wait(reporter->close(), scheduler)->value().quiescent);
        CHECK(!take(counter->snapshot()).faulted && !take(reporter->snapshot()).faulted);
        parents.clear();
        counterRuntime.reset();
        reporterRuntime.reset();
        kernel.reset();
        barrier(scheduler);
        take(manager.drain(counterId));
        take(manager.drain(reporterId));
        for (const auto& state : take(manager.snapshot()))
            CHECK(state.resources.drained());
        CHECK(take(scheduler->snapshot()).callbackErrors == 0);
        take(scheduler->close());
        // 本段只核状态/资源契约，不把 drain 或进程退出称为物理卸载。
    }
};
void runtime(const QString& root, const std::string& variant)
{
    Scene scene(root, variant);
    auto child = std::make_shared<std::optional<Instance>>();
    const auto parent = scene.imported({{"org.eve.example.counter", 1}, "9007199254740993"}, child);
    CHECK(wait(parent.ready(), scene.scheduler)->hasValue() && *child);
    CHECK(scene.capture(**child) == "9007199254740993");
    const auto consumer = take(scene.kernel->mount(*scene.reporterRuntime, "null"));
    CHECK(wait(consumer.ready(), scene.scheduler)->hasValue());
    const auto rows = take(scene.reporter->snapshot()).instances;
    CHECK(rows.size() == 1 && rows[0].logs.size() == 1 &&
          rows[0].logs[0].text == "start result {\"value\":9007199254740994}");
    CHECK(scene.capture(**child) == "9007199254740994");
    CHECK(wait(consumer.stop(), scene.scheduler)->value().quiescent);
    CHECK(scene.capture(**child) == "9007199254740995");
    const auto restarted = take(scene.kernel->mount(*scene.reporterRuntime, "null"));
    CHECK(wait(restarted.ready(), scene.scheduler)->hasValue());
    const auto restartedId = take(restarted.snapshot()).instance;
    const auto fresh = take(scene.reporter->snapshot()).instances;
    const auto found =
        std::find_if(fresh.begin(), fresh.end(), [&](const auto& row) { return row.instance == restartedId; });
    CHECK(found != fresh.end() && found->logs.size() == 1 &&
          found->logs[0].text == "start result {\"value\":9007199254740996}");
    CHECK(scene.capture(**child) == "9007199254740996");
    scene.close();
}
void boundaries(const QString& root)
{
    Scene scene(root, "b");
    const auto other = take(scene.kernel->services().createRealm());
    auto zero = std::make_shared<std::optional<Instance>>(), maximum = std::make_shared<std::optional<Instance>>();
    const auto first = scene.imported({{"org.eve.example.counter", 1}, "0"}, zero);
    const auto second = scene.imported({{"org.eve.example.counter", 1}, "18446744073709551615"}, maximum, other);
    CHECK(wait(first.ready(), scene.scheduler)->hasValue() && wait(second.ready(), scene.scheduler)->hasValue());
    CHECK(scene.capture(**zero) == "0" && scene.capture(**maximum) == "18446744073709551615");
    CHECK(take(scene.counter->snapshot()).instances.size() == 2);
    scene.close();
}
void rejections(const QString& root)
{
    Scene scene(root, "a");
    for (const auto& state : std::vector<StateSnapshot>{{{"org.eve.example.counter", 1}, "01"},
                                                        {{"org.eve.example.counter", 1}, "18446744073709551616"},
                                                        {{"org.eve.example.counter", 1}, "1e0"},
                                                        {{"org.eve.example.counter", 2}, "1"},
                                                        {{"other.counter", 1}, "1"}}) {
        auto child = std::make_shared<std::optional<Instance>>();
        const auto parent = scene.imported(state, child);
        CHECK(!wait(parent.ready(), scene.scheduler)->hasValue());
        CHECK(wait(parent.stop(), scene.scheduler)->value().quiescent);
        CHECK(take(scene.counter->snapshot()).instances.empty() && !take(scene.counter->snapshot()).faulted);
    }
    scene.close();
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        CHECK(argc == 3);
        const auto root = QDir(QString::fromLocal8Bit(argv[1])).absolutePath();
        const auto name = QString::fromLocal8Bit(argv[2]);
        if (nativeServiceStateAbiCase(name, root))
            return 0;
        if (name == "runtime_a" || name == "runtime_b")
            runtime(root, name == "runtime_a" ? "a" : "b");
        else if (name == "runtime_boundaries")
            boundaries(root);
        else if (name == "runtime_rejections")
            rejections(root);
        else
            throw std::runtime_error("unknown Native business state case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
