/** @file connection_retirement.cpp
 * @brief 确定性验证释放栈仍占信号预算，覆盖丢弃及入口门禁失败两条路径。
 */
// 私有控制块测试直接编译实现，以注入释放观察而不扩展公开信号类型白名单或增加生产测试钩子。
#include "../../src/qt/connection.cpp"
#include <QCoreApplication>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

/** @brief 释放观察是 host 内部测试数据，不通过 observeValues 注册自定义参数类型。 */
void retirementBudget(bool guardFailure)
{
    using namespace cordis;
    using namespace cordis::qt;
    auto scheduler = std::make_shared<QtScheduler>();
    auto state = std::make_shared<cordis::qt::detail::ConnectionControl>();
    state->scheduler = scheduler;
    state->armed = true;
    state->maximumPending = 1;
    state->guard = [] { return Result<void>::failure(makeError(ErrorCode::ContextClosing, "test guard")); };
    cordis::qt::detail::SignalBridge bridge(state);
    auto ticket = bridge.reserve();
    if (!ticket)
        throw std::runtime_error("initial ticket rejected");
    bool observed = false;
    std::shared_ptr<cordis::qt::detail::ConnectionTicket> extra;
    auto payload = std::shared_ptr<const void>(new int(42), [&](const void* value) {
        // 与并发发射相同，重入 reserve 使用真实锁和预算判断；此时旧存储尚未释放。
        observed = true;
        extra = bridge.reserve();
        delete static_cast<const int*>(value);
    });
    bridge.deliver(ticket, std::move(payload));
    if (guardFailure)
        state->enter(ticket->id);
    else {
        state->drop(ticket->id, false);
        state->drain();
    }
    const bool premature = static_cast<bool>(extra);
    if (extra)
        bridge.payloadFailed(extra, false);
    state->drain();
    auto available = bridge.reserve();
    if (available)
        bridge.payloadFailed(available, false);
    state->drain();
    scheduler->close().value();
    if (!observed || premature || !available)
        throw std::runtime_error(guardFailure ? "guard failure released budget before payload" :
                                               "drain released budget before payload");
}

/** @brief 两条真实释放路径都必须在实际数据释放后才恢复预算。 */
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool passed = true;
    for (const bool guardFailure : {false, true}) {
        try {
            retirementBudget(guardFailure);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            passed = false;
        }
    }
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
