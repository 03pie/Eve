/** @file module_unload_reader.cpp
 * @brief 实际工厂借用结束前，即使执行引用已空，也不能物理卸载读代次。
 */
#include "../../src/module/replacement_commit_p.hpp"
#include "module_unload_p.hpp"

namespace unload_test {
namespace {
template <class T> T take(Result<T> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().phase + ": " + result.error().message);
    return std::move(result).value();
}
} // namespace
bool borrowedReader(Scene& scene, const QString& name)
{
    if (name != "reader")
        return false;
    const auto current = scene.load();
    const auto nextPath = scene.prepare(6, "candidate");
    const auto next = take(scene.manager.stage(scene.root.toStdString(), nextPath.toStdString(), current.generation, {},
                                               scene.approval(nextPath)));
    const auto candidate = scene.describe(next.generation, nextPath);
    const auto consumer = scene.load(1, "consumer");
    auto scheduler = std::make_shared<qt::QtScheduler>();
    {
        auto currentModule = take(NativeModule::open(scene.manager, current.generation, scheduler));
        auto consumerModule = take(NativeModule::open(scene.manager, consumer.generation, scheduler));
        const auto original = take(currentModule.runtime({"org.eve.plugin", {}, {}}));
        std::optional<Runtime> borrowed{take(consumerModule.runtime({"org.eve.plugin", {}, {}}))};
        const auto borrowedId = borrowed->id();
        Kernel kernel(scheduler);
        CHECK(kernel.registerRuntime("target", original).hasValue());
        auto scope = take(kernel.reserveReplacement({original.id()}));
        auto reservation = take(scene.manager.reserveReplacement(current.generation, candidate.generation, scope));
        // 真实读占用也保护 host.close；先关闭会话，保留实际 Runtime 来源供 borrow 固定。
        CHECK(take(*wait(consumerModule.close())).quiescent);
        CHECK(native::ReplacementAccess::borrow(reservation, scope, {*borrowed}, {*borrowed}).hasValue());
        borrowed.reset();
        CHECK(!scene.manager.runtimeOrigin(borrowedId).value());
        CHECK(scene.state(consumer.generation).resources.records.empty());
        const auto held = native::findGeneration(consumer.generation);
        CHECK(held && held->runtimeOrigins.empty() && held->instanceOrigins.empty());
        const auto refused = scene.manager.tryUnload(consumer.generation);
        CHECK(!refused.hasValue() && refused.error().code == ErrorCode::ConfigConflict);
        CHECK(consumer.witness->read() == 0 && scene.state(consumer.generation).resources.accepting);
        scene.protectedFile(consumer);
        reservation.reset();
        CHECK(scope.release().hasValue());
        CHECK(scene.manager.tryUnload(consumer.generation).hasValue());
        scene.gone(consumer);
        CHECK(take(*wait(currentModule.close())).quiescent);
        CHECK(take(*wait(kernel.close())).quiescent);
    }
    CHECK(scene.manager.tryUnload(candidate.generation).hasValue());
    scene.gone(candidate);
    CHECK(scene.manager.tryUnload(current.generation).hasValue());
    scene.gone(current);
    CHECK(scheduler->close().hasValue());
    return true;
}
} // namespace unload_test
