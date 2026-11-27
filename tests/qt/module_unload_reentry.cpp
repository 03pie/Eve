/** @file module_unload_reentry.cpp
 * @brief 真实 Native 创建的调度器查询重入关闭及卸载，返回后不能再读取已退役表。
 */
#include "module_unload_p.hpp"

namespace unload_test {
namespace {
class ReentryScheduler final : public IScheduler {
public:
    std::shared_ptr<qt::QtScheduler> target = std::make_shared<qt::QtScheduler>();
    mutable std::function<void()> onOpen;
    bool post(ScheduledJob job) override { return target->post(std::move(job)); }
    bool isCurrentThread() const noexcept override { return target->isCurrentThread(); }
    bool isOpen() const override
    {
        auto action = std::move(onOpen);
        onOpen = {};
        if (action)
            action();
        return target->isOpen();
    }
};
} // namespace
bool createReentry(Scene& scene, const QString& name)
{
    if (name != "create_reentry")
        return false;
    auto artifact = scene.load();
    auto scheduler = std::make_shared<ReentryScheduler>();
    auto module = NativeModule::open(scene.manager, artifact.generation, scheduler).value();
    bool entered = false;
    scheduler->onOpen = [&] {
        CHECK(!entered);
        entered = true;
        CHECK(scene.call<unsigned(EVE_CALL*)()>(artifact, "eve_business_fixture_creates") == 0);
        const auto closed = module.close().tryResult();
        CHECK(closed && closed->hasValue() && closed->value().quiescent);
        CHECK(scene.manager.tryUnload(artifact.generation).hasValue());
        scene.gone(artifact);
        CHECK(scheduler->target->isOpen()); // 查询仍返回 true，必须由返回后的实际 root 门禁拒绝。
    };
    const auto created = module.create("org.eve.plugin", "null", InstanceId{1}, 1);
    CHECK(entered && !created.hasValue() && created.error().code == ErrorCode::ContextClosing);
    const auto closed = module.snapshot().value();
    CHECK(closed.closed && !closed.faulted && closed.instances.empty());
    CHECK(!scene.state(artifact.generation).businessFailure);
    CHECK(scheduler->target->close().hasValue());
    CHECK(wait(module.close())->value().quiescent);
    return true;
}
} // namespace unload_test
