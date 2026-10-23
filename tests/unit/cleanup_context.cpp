/** @file cleanup_context.cpp
 * @brief 验证独立清理上下文的旧代快照、权限边界和异步有效窗口。
 */
#include <cordis/instance.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>

using namespace cordis;
using cordis::testing::FakeScheduler;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

/** @brief 编译期检测清理上下文是否暴露业务 effect。 */
template<class T, class = void> struct HasEffect : std::false_type {};
/** @brief 存在业务注册入口时使边界检查失败。 */
template<class T> struct HasEffect<T, std::void_t<decltype(std::declval<T>().effect(EffectSetup{}))>> : std::true_type {};
static_assert(!HasEffect<CleanupContext>::value, "cleanup must not expose business registration");
static_assert(!std::is_constructible_v<Context, CleanupContext>, "cleanup must not convert to activation context");

/** @brief 旧代清理期间保留原配置，完成后撤销能力，新代仅在静止后启动。 */
void epochWindow()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> gate;
    std::optional<CleanupContext> old;
    int started = 0;
    bool refused = false;
    Runtime runtime([&](Context context) {
        ++started;
        auto cleanup = context.onCleanup([&, context](CleanupContext closing) {
            auto snapshot = closing.snapshot().value();
            CHECK(snapshot.config == context.config());
            if (snapshot.epoch == 1) {
                CHECK(snapshot.revision == 1 && snapshot.config == "old");
                old.emplace(closing);
                auto newWork = context.effect([](EffectSetupContext) { return completedTask(); });
                auto newCleanup = context.onCleanup([](CleanupContext) { return completedTask(); });
                refused = !newWork.hasValue() && !newCleanup.hasValue();
                return gate.task();
            }
            CHECK(snapshot.epoch == 2 && snapshot.config == "new" && snapshot.revision == 2);
            return completedTask();
        });
        return cleanup.hasValue() ? cleanup.value().ready() : failedTask(cleanup.error());
    });
    auto instance = Instance::mount(runtime, scheduler, "old");
    CHECK(scheduler->runUntilIdle().hasValue());
    auto changed = instance.update("new", 1).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(started == 1 && refused && !changed.target.isReady());
    CHECK(old->snapshot().value().instance == instance.snapshot().value().instance);
    bool wrongThread = false;
    std::thread worker([&] { wrongThread = old->snapshot().error().code == ErrorCode::WrongThread; });
    worker.join();
    CHECK(wrongThread);
    std::thread completed([gate] { gate.resolve(); });
    completed.join();
    CHECK(old->snapshot().error().code == ErrorCode::StaleContext);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(started == 2 && changed.target.tryResult()->value().outcome == StableOutcome::Active);
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(stopped.tryResult()->value().quiescent);
}

/** @brief 异常与失败结束访问窗口，并且错误仍经 Effect 清理报告汇总。 */
void failures()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    std::optional<CleanupContext> thrown;
    std::optional<CleanupContext> rejected;
    int cleaned = 0;
    Runtime runtime([&](Context context) {
        CHECK(!context.onCleanup({}).hasValue());
        CHECK(context.onCleanup([&](CleanupContext closing) -> Task<void> {
            thrown.emplace(closing);
            throw std::runtime_error("cleanup threw");
        }).hasValue());
        CHECK(context.onCleanup([&](CleanupContext closing) {
            rejected.emplace(closing);
            return failedTask(makeError(ErrorCode::CleanupFailed, "release failed"));
        }).hasValue());
        CHECK(context.onCleanup([&](CleanupContext closing) {
            CHECK(closing.snapshot().hasValue());
            ++cleaned;
            return completedTask();
        }).hasValue());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(cleaned == 1 && !stopped.tryResult()->value().quiescent);
    CHECK(stopped.tryResult()->value().errors.size() == 2);
    CHECK(thrown->snapshot().error().code == ErrorCode::StaleContext);
    CHECK(rejected->snapshot().error().code == ErrorCode::StaleContext);
}

/** @brief 独立 token 清理也拒绝被捕获的业务 Context，并等待异步真实完成。 */
void tokenBoundary()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> gate;
    std::optional<EffectToken> token;
    std::optional<Context> business;
    std::optional<CleanupContext> closing;
    Runtime runtime([&](Context context) {
        business.emplace(context);
        token.emplace(context.onCleanup([&, context](CleanupContext cleanup) {
            closing.emplace(cleanup);
            CHECK(!context.effect([](EffectSetupContext) { return completedTask(); }).hasValue());
            return gate.task();
        }).value());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    bool wrongThread = false;
    std::thread worker([&] {
        wrongThread = business->onCleanup([](CleanupContext) { return completedTask(); }).error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrongThread);
    auto first = token->close();
    auto second = token->close();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!first.isReady() && closing->snapshot().hasValue());
    CHECK(instance.snapshot().value().state == InstanceState::Active);
    gate.resolve();
    CHECK(!closing->snapshot().hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(first.tryResult() == second.tryResult() && first.tryResult()->value().quiescent);
    CHECK(business->onCleanup([](CleanupContext) { return completedTask(); }).hasValue());
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
}

/** @brief 根据场景名执行清理上下文测试。 */
int main(int argc, char** argv)
{
    if (argc != 2) return EXIT_FAILURE;
    try {
        const std::string name = argv[1];
        if (name == "epoch_window") epochWindow();
        else if (name == "failures") failures();
        else if (name == "token_boundary") tokenBoundary();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
