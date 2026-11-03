/** @file module_usage.cpp
 * @brief 原生/Qt 两事务共享真实消费者模块；停机空窗仍保留模块使用意图。
 */
#include "../../src/module/replacement_commit_p.hpp"
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QThread>
#include <cordis/kernel.hpp>
#include <cordis/module/manager.hpp>
#include <cordis/module/qt_module.hpp>
#include <cordis/module/runtime.hpp>
#include <cordis/qt/scheduler.hpp>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cordis;
using namespace cordis::module;
namespace files = cordis::testing::modules;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
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
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        const bool failure = args[1] == "failure";
        const auto dlls = args[3].split(';');
        CHECK(dlls.size() == 6);
        ModuleManager manager;
        auto install = [&](int index, const char* folder, const char* id, bool qt, const char* version) {
            return files::install(dlls[index], args[2] + '/' + folder, id,
                                  qt ? QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"}
                                     : QJsonArray{"org.cordis.plugin"},
                                  qt ? "qt-ui" : "c-abi", version)
                .toStdString();
        };
        const auto aPath = install(0, "a", "org.cordis.business", false, "1.0.0");
        const auto aNextPath = install(1, "a-next", "org.cordis.business", false, "2.0.0");
        const auto cPath = install(2, "c", "org.cordis.qt.example", true, "1.0.0");
        const auto cNextPath = install(3, "c-next", "org.cordis.qt.example", true, "2.0.0");
        const auto bPath = install(4, "b", "org.cordis.usage", false, "1.0.0");
        const auto bNextPath = install(5, "b-next", "org.cordis.usage", false, "2.0.0");
        const auto root = args[2].toStdString();
        const auto a = manager.load(root, aPath).value().generation;
        const auto aNext = manager.stage(root, aNextPath, a).value().generation;
        const auto c = loadQtModule(root, cPath).value().generation;
        const auto cNext = stageQtModule(root, cNextPath, c).value().generation;
        const auto b = manager.load(root, bPath).value().generation;
        const auto bNext = manager.stage(root, bNextPath, b).value().generation;
        auto scheduler = std::make_shared<qt::QtScheduler>();
        Kernel kernel(scheduler), other(scheduler);
        auto moduleA = NativeModule::open(manager, a, scheduler, kernel.services()).value();
        auto moduleB = NativeModule::open(manager, b, scheduler, kernel.services()).value();
        auto factoryA = moduleA.runtime({"org.cordis.plugin", {}, {}}).value();
        auto factoryB = moduleB.runtime({"org.cordis.plugin", {}, {}}).value();
        auto factoryC = qtRuntime(c, {"org.cordis.qt.page", {}, {}}, scheduler).value();
        kernel.registerRuntime("a", factoryA).value();
        kernel.registerRuntime("b", factoryB).value();
        kernel.registerRuntime("c", factoryC).value();
        other.registerRuntime("b", factoryB).value();
        std::vector<Instance> children;
        auto owner = [&](const char* name, Runtime provider, const char* config) {
            return kernel
                .registerPlugin(name,
                                [provider, consumer = factoryB, config, &children](Context context) {
                                    children.push_back(context.inject(provider, config).value());
                                    children.push_back(context.inject(consumer, "\"sync\"").value());
                                    return completedTask();
                                })
                .value();
        };
        auto ownerA = owner("owner-a", factoryA, "\"sync\"");
        auto ownerC = owner("owner-c", factoryC, "null");
        auto parentA = kernel.mount(ownerA).value(), parentC = kernel.mount(ownerC).value();
        CHECK(wait(parentA.ready())->hasValue() && wait(parentC.ready())->hasValue());
        CHECK(children.size() == 4);
        for (const auto& child : children)
            CHECK(wait(child.ready())->hasValue());
        auto scopeA = kernel.reserveReplacement({factoryA.id()}).value();
        auto scopeC = kernel.reserveReplacement({factoryC.id()}).value();
        auto first = manager.reserveReplacement(a, aNext, scopeA).value();
        auto second = manager.reserveReplacement(c, cNext, scopeC).value();
        CHECK(wait(scopeA.stop())->value().quiescent && wait(scopeC.stop())->value().quiescent);
        CHECK(moduleB.snapshot().value().instances.empty());
        // 另一 Kernel 的作用域看不到旧消费者，但进程目录仍须保护它们的重建意图。
        auto scopeB = other.reserveReplacement({factoryB.id()}).value();
        auto blocked = manager.reserveReplacement(b, bNext, scopeB);
        CHECK(!blocked.hasValue() && blocked.error().code == ErrorCode::ConfigConflict);
        CHECK(moduleB.close().tryResult()->error().code == ErrorCode::ConfigConflict &&
              !moduleB.snapshot().value().closing);
        CHECK(manager.drain(b).error().code == ErrorCode::ConfigConflict);
        CHECK(manager.stage(root, bNextPath, b).value().reused);
        auto ordinary =
            Instance::mount(factoryB, scheduler, "\"sync\"", std::make_shared<ServiceRegistry>(kernel.services()));
        CHECK(wait(ordinary.ready())->hasValue() && wait(ordinary.stop())->value().quiescent);
        std::thread release([guard = std::move(first)]() mutable { guard.reset(); });
        release.join();
        scopeA.release().value();
        CHECK(manager.reserveReplacement(b, bNext, scopeB).error().code == ErrorCode::ConfigConflict);
        second.reset();
        scopeC.release().value();
        auto available = manager.reserveReplacement(b, bNext, scopeB).value();
        // 反方向同样原子：消费者已有写占用时，目标模块不能留下半组预约。
        auto retryParent = kernel.mount(ownerA).value();
        CHECK(wait(retryParent.ready())->hasValue() && children.size() == 6);
        CHECK(wait(children[4].ready())->hasValue());
        CHECK(wait(children[5].ready())->error().code == ErrorCode::ConfigConflict);
        CHECK(wait(children[5].operation().value().target)->value().outcome == StableOutcome::Failed);
        auto requested = kernel.reserveReplacement({factoryA.id()});
        if (!requested.hasValue())
            throw std::runtime_error("retry scope: " + requested.error().message);
        auto retryScope = std::move(requested).value();
        auto denied = manager.reserveReplacement(a, aNext, retryScope);
        CHECK(!denied.hasValue() && denied.error().code == ErrorCode::ConfigConflict);
        CHECK(moduleA.runtime({"org.cordis.plugin", {}, {}}).hasValue());
        available.reset();
        auto rebound = manager.reserveReplacement(a, aNext, retryScope);
        if (!rebound.hasValue())
            throw std::runtime_error("retry module: " + rebound.error().message);
        auto retry = std::move(rebound).value();
        CHECK(manager.reserveReplacement(b, bNext, scopeB).error().code == ErrorCode::ConfigConflict);
        retry.reset();
        retryScope.release().value();
        scopeB.release().value();
        CHECK(wait(retryParent.stop())->value().quiescent);
        // 原闭包没有 B/C；候选实际新增两种后端的业务，丢弃后仍要保留使用意图直到操作结束。
        auto candidateModuleA = NativeModule::open(manager, aNext, scheduler, kernel.services()).value();
        auto candidateFactoryA = candidateModuleA.runtime({"org.cordis.plugin", {}, {}}).value();
        auto dynamic = kernel.reserveReplacement({factoryA.id()}).value();
        CHECK(dynamic.inputs().value().empty());
        dynamic.prepareDefinitions({{factoryA.id(), candidateFactoryA}}).value();
        auto dynamicGuard = manager.reserveReplacement(a, aNext, dynamic).value();
        CHECK(wait(dynamic.stop())->value().quiescent);
        auto extraA = dynamic.stage(candidateFactoryA, "\"sync\"").value();
        auto extraB = dynamic.stage(factoryB, "\"sync\"").value();
        auto extraC = dynamic.stage(factoryC, "null").value();
        CHECK(wait(extraA.ready())->hasValue() && wait(extraB.ready())->hasValue() && wait(extraC.ready())->hasValue());
        dynamic.prepareCommit().value();
        if (failure) {
            auto bad = moduleB.create("org.cordis.plugin", "\"create-null\"", InstanceId{900002}, 1);
            CHECK(!bad.hasValue() && bad.error().code == ErrorCode::AbiProtocol);
            auto rejected = module::detail::ReplacementAccess::prepare(dynamicGuard, dynamic);
            CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::AbiProtocol);
            CHECK(dynamic.snapshot().value().state == ReplacementScopeState::Prepared);
        } else {
            auto sealed = module::detail::ReplacementAccess::prepare(dynamicGuard, dynamic).value();
            CHECK(!sealed.committed());
        }
        CHECK(wait(dynamic.discard())->value().quiescent);
        if (failure) {
            dynamicGuard.authorizeRecovery(dynamic).value();
            dynamic.prepareCommit(ReplacementCommitMode::Recovery).value();
            {
                auto preparedRecovery = module::detail::ReplacementAccess::prepare(dynamicGuard, dynamic);
                if (!preparedRecovery.hasValue())
                    throw std::runtime_error("candidate-only module blocked old recovery: " +
                                             preparedRecovery.error().message);
                auto recovered = std::move(preparedRecovery).value();
                recovered.publish().value();
            }
            dynamicGuard.reset();
            dynamic.release().value();
            CHECK(wait(kernel.close())->value().quiescent && wait(other.close())->value().quiescent);
            CHECK(wait(moduleA.close())->value().quiescent && wait(candidateModuleA.close())->value().quiescent);
            CHECK(!wait(moduleB.close())->value().quiescent && moduleB.snapshot().value().faulted);
            CHECK(!manager.drain(b).hasValue());
            scheduler->close().value();
            return 0;
        }
        auto pendingB = other.reserveReplacement({factoryB.id()}).value();
        other.registerRuntime("c", factoryC).value();
        auto pendingC = other.reserveReplacement({factoryC.id()}).value();
        auto dynamicB = manager.reserveReplacement(b, bNext, pendingB);
        auto dynamicC = manager.reserveReplacement(c, cNext, pendingC);
        CHECK(!dynamicB.hasValue() && dynamicB.error().code == ErrorCode::ConfigConflict);
        CHECK(!dynamicC.hasValue() && dynamicC.error().code == ErrorCode::ConfigConflict);
        dynamicGuard.reset();
        dynamic.release().value();
        auto releasedB = manager.reserveReplacement(b, bNext, pendingB).value();
        auto releasedC = manager.reserveReplacement(c, cNext, pendingC).value();
        releasedB.reset();
        releasedC.reset();
        pendingB.release().value();
        pendingC.release().value();
        CHECK(wait(kernel.close())->value().quiescent && wait(other.close())->value().quiescent);
        CHECK(wait(moduleA.close())->value().quiescent && wait(moduleB.close())->value().quiescent &&
              wait(candidateModuleA.close())->value().quiescent);
        QElapsedTimer timer;
        timer.start();
        while (scheduler->snapshot().value().queued) {
            CHECK(timer.elapsed() < 5000);
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        scheduler->close().value();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
