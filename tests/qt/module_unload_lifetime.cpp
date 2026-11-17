/** @file module_unload_lifetime.cpp
 * @brief 实际 Native 资源与 DLL 捕获完整退出后才允许物理卸载。
 */
#include "module_unload_p.hpp"
#include <QObject>
#include <cordis/qt/object.hpp>
namespace unload_test {
template <class F> void until(F condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        CHECK(timer.elapsed() < 8000);
        events();
        QThread::msleep(1);
    }
}
void finish(Scene& scene, const Artifact& artifact, NativeModule& module)
{
    CHECK(wait(module.close())->value().quiescent);
    CHECK(module.snapshot().value().instances.empty() && scene.state(artifact.generation).resources.records.empty());
    CHECK(scene.manager.tryUnload(artifact.generation).hasValue());
    scene.gone(artifact);
}
void session(Scene& scene, const QString& name)
{
    const bool state = name == "state";
    auto artifact = scene.load(state ? 3U : 0U);
    auto scheduler = std::make_shared<qt::QtScheduler>();
    auto module = NativeModule::open(scene.manager, artifact.generation, scheduler).value();
    if (name == "runtime" || state) {
        std::optional<Runtime> factory{module.runtime({"org.cordis.plugin", {}, {}}).value()};
        CHECK(factory->stateSchema().has_value() == state);
        const auto id = factory->id();
        CHECK(wait(module.close())->value().quiescent);
        CHECK(scene.manager.runtimeOrigin(id).value() &&
              scene.records(artifact.generation, ResourceKind::ExternalHandle) > 0);
        scene.blocked(artifact);
        factory.reset();
        CHECK(!scene.manager.runtimeOrigin(id).value());
        CHECK(scene.manager.tryUnload(artifact.generation).hasValue());
        scene.gone(artifact);
    } else {
        CHECK(scene.records(artifact.generation, ResourceKind::ExternalHandle) > 0);
        scene.blocked(artifact);
        finish(scene, artifact, module);
    }
    CHECK(wait(module.close())->value().quiescent); // 已关闭 facade 的幂等入口不再读取已清表。
    CHECK(!module.runtime({"org.cordis.plugin", {}, {}}).hasValue());
    CHECK(!module.create("org.cordis.plugin", "null", InstanceId{9}, 1).hasValue());
    CHECK(scheduler->close().hasValue());
}
void instance(Scene& scene, const QString& name)
{
    const bool setup = name == "setup", cleanup = name == "cleanup";
    auto artifact = scene.load(cleanup ? 1U : 0U);
    auto scheduler = std::make_shared<qt::QtScheduler>();
    auto module = NativeModule::open(scene.manager, artifact.generation, scheduler).value();
    auto actor = module.create("org.cordis.plugin",
                               cleanup ? "\"stop\""
                               : setup ? "{}"
                                       : "\"sync\"",
                               InstanceId{1}, 1)
                     .value();
    const auto started = actor.start();
    if (setup) {
        CHECK(!started.isReady() && scene.records(artifact.generation, ResourceKind::SetupTask) > 0);
        scene.blocked(artifact);
        CHECK(scene.call<unsigned(CORDIS_CALL*)()>(artifact, "cordis_business_fixture_finish_all") == 1);
    }
    CHECK(wait(started)->hasValue() && scene.records(artifact.generation, ResourceKind::Instance) == 1);
    scene.blocked(artifact);
    const auto closed = actor.close();
    if (cleanup) {
        CHECK(!closed.isReady() && scene.records(artifact.generation, ResourceKind::CleanupTask) > 0);
        scene.blocked(artifact);
        CHECK(scene.call<unsigned(CORDIS_CALL*)()>(artifact, "cordis_business_fixture_finish_all") == 1);
    }
    CHECK(wait(closed)->value().quiescent && actor.snapshot().value().state == NativeInstanceState::Destroyed);
    finish(scene, artifact, module);
    if (name == "closed_facade") {
        CHECK(!wait(actor.start())->hasValue());
        CHECK(wait(actor.close())->value().quiescent && wait(module.close())->value().quiescent);
        CHECK(module.snapshot().value().closed &&
              !module.create("org.cordis.plugin", "null", InstanceId{2}, 1).hasValue());
        const auto generation =
            scene.manager
                .load(scene.root.toStdString(), artifact.manifest.toStdString(), {}, scene.approval(artifact.manifest))
                .value()
                .generation;
        auto again = scene.describe(generation, artifact.manifest);
        CHECK(generation != artifact.generation);
        auto reopened = NativeModule::open(scene.manager, generation, scheduler).value();
        CHECK(scene.call<unsigned(CORDIS_CALL*)()>(again, "cordis_business_fixture_queries") == 1);
        CHECK(scene.call<unsigned(CORDIS_CALL*)()>(again, "cordis_business_fixture_creates") == 0);
        auto fresh = reopened.create("org.cordis.plugin", "\"sync\"", InstanceId{2}, 1).value();
        CHECK(wait(fresh.start())->hasValue() && fresh.snapshot().value().nativeCreated);
        CHECK(scene.call<unsigned(CORDIS_CALL*)()>(again, "cordis_business_fixture_creates") == 1);
        CHECK(!module.create("org.cordis.plugin", "null", InstanceId{3}, 1).hasValue());
        finish(scene, again, reopened);
    }
    CHECK(scheduler->close().hasValue());
}
struct Gate final : IScheduler {
    std::shared_ptr<qt::QtScheduler> target = std::make_shared<qt::QtScheduler>();
    bool capture = false;
    std::optional<ScheduledJob> retained;
    bool post(ScheduledJob job) override
    {
        if (capture) {
            capture = false;
            retained = job;
        }
        return target->post(std::move(job));
    }
    bool isCurrentThread() const noexcept override { return target->isCurrentThread(); }
    bool isOpen() const override { return target->isOpen(); }
};
void capture(Scene& scene, const QString& name)
{
    const bool failed = name == "cleanup_failure", stale = name != "capture";
    auto artifact = scene.load(1);
    auto scheduler = std::make_shared<Gate>();
    auto module = NativeModule::open(scene.manager, artifact.generation, scheduler).value();
    auto actor =
        module.create("org.cordis.plugin", failed ? "\"capture-fail\"" : "\"capture\"", InstanceId{1}, 1).value();
    CHECK(wait(actor.start())->hasValue());
    scheduler->capture = true;
    CHECK(scene.call<CordisStatus(CORDIS_CALL*)()>(artifact, "cordis_lifecycle_queue") == CORDIS_PENDING &&
          scheduler->retained);
    const auto stat = [&](unsigned index) {
        return scene.call<std::uint64_t(CORDIS_CALL*)(unsigned)>(artifact, "cordis_lifecycle_stat", index);
    };
    if (!stale)
        until([&] { return stat(2) == 1; });
    const auto stopped = actor.close();
    until([&] { return stat(8) == 1; });
    CHECK(stat(0) == (stale ? 0U : 1U) && stat(1) == (failed ? 0U : 1U) && stat(9) == (failed ? 1U : 0U));
    CHECK(scene.records(artifact.generation, ResourceKind::Callback) > 0 &&
          scene.records(artifact.generation, ResourceKind::QueuePayload) > 0);
    scene.blocked(artifact);
    std::thread releaser([&] { scheduler->retained.reset(); });
    releaser.join();
    CHECK(wait(stopped)->value().quiescent == !failed);
    if (failed) {
        CHECK(!wait(module.close())->value().quiescent && stat(1) == 0 && stat(9) == 1);
        scene.blocked(artifact);
        CHECK(!scene.state(artifact.generation).resources.pinReasons.empty());
    } else {
        CHECK(stat(1) == 1 && stat(7) == 1);
        finish(scene, artifact, module);
    }
    CHECK(scheduler->target->close().hasValue());
}
void service(Scene& scene)
{
    auto artifact = scene.load(2);
    auto scheduler = std::make_shared<qt::QtScheduler>();
    auto module = NativeModule::open(scene.manager, artifact.generation, scheduler).value();
    auto actor = module.create("org.cordis.plugin", "{}", InstanceId{1}, 1).value();
    CHECK(wait(actor.start())->hasValue());
    CordisHandle effect = 0;
    CHECK(scene.call<CordisStatus(CORDIS_CALL*)(unsigned, CordisHandle, CordisHandle*)>(
              artifact, "cordis_resource_effect", 0U, CordisHandle{11}, &effect) == CORDIS_OK);
    CHECK(scene.records(artifact.generation, ResourceKind::SetupTask) > 0);
    scene.blocked(artifact);
    CHECK(scene.call<CordisStatus(CORDIS_CALL*)(unsigned, CordisHandle, CordisStatus)>(
              artifact, "cordis_resource_ready", 0U, effect, CORDIS_OK) == CORDIS_OK);
    std::string label = "unload.service", caps = "[]";
    const CordisBytesV1 keyBytes{reinterpret_cast<const std::uint8_t*>(label.data()), label.size()},
        capBytes{reinterpret_cast<const std::uint8_t*>(caps.data()), caps.size()};
    const CordisServiceKeyV1 key{keyBytes, 1, 0, 1};
    const CordisServiceMetadataV1 metadata{sizeof(CordisServiceMetadataV1), 0, capBytes};
    CordisHandle binding = 0;
    CHECK(scene.call<CordisStatus(CORDIS_CALL*)(unsigned, CordisHandle, const CordisServiceKeyV1*,
                                                const CordisServiceMetadataV1*, CordisHandle, CordisHandle*)>(
              artifact, "cordis_service_register", 0U, effect, &key, &metadata, CordisHandle{8}, &binding) ==
              CORDIS_OK &&
          binding);
    until([&] { return scene.records(artifact.generation, ResourceKind::ServiceObject) > 0; });
    scene.blocked(artifact);
    finish(scene, artifact, module);
    CHECK(scheduler->close().hasValue());
}
void worker(Scene& scene)
{
    auto artifact = scene.load();
    const auto ledger = native::findGeneration(artifact.generation)->ledger;
    auto threadLease = ledger.acquire(ResourceKind::Worker, "real native worker return path").value();
    Event entered, release, returned, exit;
    std::exception_ptr failure;
    std::thread body([&] {
        try {
            inDll(scene, artifact, [&] {
                entered.set();
                release.wait();
            });
            returned.set();
            exit.wait();
        } catch (...) {
            failure = std::current_exception();
            entered.set();
            returned.set();
        }
    });
    try {
        entered.wait();
        CHECK(scene.records(artifact.generation, ResourceKind::Worker) == 1 &&
              scene.records(artifact.generation, ResourceKind::Call) == 1);
        scene.blocked(artifact);
        release.set();
        returned.wait();
        CHECK(scene.records(artifact.generation, ResourceKind::Call) == 0);
        scene.blocked(artifact); // DLL 已返回，原线程与捕获尚未完整退出。
    } catch (...) {
        release.set();
        exit.set();
        body.join();
        throw;
    }
    exit.set();
    body.join();
    if (failure)
        std::rethrow_exception(failure);
    threadLease.reset();
    CHECK(artifact.witness->read() == 0);
    CHECK(scene.manager.tryUnload(artifact.generation).hasValue());
    scene.gone(artifact);
}
void qobject(Scene& scene)
{
    auto artifact = scene.load();
    auto scheduler = std::make_shared<qt::QtScheduler>();
    const auto ledger = native::findGeneration(artifact.generation)->ledger;
    auto lease = ledger.acquire(ResourceKind::QObject, "QObject with DLL callback dependency").value();
    bool childDestroyed = false, notified = false;
    struct Child final : QObject {
        bool& seen;
        Child(QObject* parent, bool& value) : QObject(parent), seen(value) {}
        ~Child() override { seen = true; }
    };
    std::unique_ptr<QObject> object = std::make_unique<QObject>();
    new Child(object.get(), childDestroyed);
    std::exception_ptr failure;
    QObject::connect(
        object.get(), &QObject::destroyed, QCoreApplication::instance(),
        [&] {
            try {
                CHECK(!childDestroyed && scene.records(artifact.generation, ResourceKind::QObject) == 1);
                inDll(scene, artifact, [&] { scene.blocked(artifact); });
                notified = true;
            } catch (...) {
                failure = std::current_exception();
            }
        },
        Qt::DirectConnection);
    qt::QObjectResource resource(object, scheduler);
    scene.blocked(artifact);
    CHECK(wait(resource.close())->hasValue());
    if (failure)
        std::rethrow_exception(failure);
    CHECK(notified && childDestroyed && resource.snapshot().value().confirmed);
    lease.reset();
    CHECK(scene.manager.tryUnload(artifact.generation).hasValue());
    scene.gone(artifact);
    CHECK(scheduler->close().hasValue());
}
bool lifetime(Scene& scene, const QString& name)
{
    if (name == "session" || name == "runtime" || name == "state")
        session(scene, name);
    else if (name == "instance" || name == "setup" || name == "cleanup" || name == "closed_facade")
        instance(scene, name);
    else if (name == "capture" || name == "stale_capture" || name == "cleanup_failure")
        capture(scene, name);
    else if (name == "service")
        service(scene);
    else if (name == "worker")
        worker(scene);
    else if (name == "qobject")
        qobject(scene);
    else if (name == "call") {
        auto artifact = scene.load();
        inDll(scene, artifact, [&] {
            CHECK(scene.records(artifact.generation, ResourceKind::Call) == 1);
            scene.blocked(artifact);
        });
        CHECK(scene.manager.tryUnload(artifact.generation).hasValue());
        scene.gone(artifact);
    } else
        return false;
    return true;
}
} // namespace unload_test
