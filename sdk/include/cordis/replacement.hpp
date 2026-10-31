/** @file replacement.hpp
 * @brief 受控替换的纯值闭包；不授予停止、创建或模块切换能力。
 */
#pragma once
#include <cordis/identity.hpp>
#include <cordis/state.hpp>
#include <vector>

namespace cordis {
struct ReplacementMember {
    InstanceId instance;
    RuntimeId runtime;
    InstanceId parent;
    Epoch epoch;
    ConfigRevision revision;
    InstanceState state;
};
/** @brief 从实际所有权/committed/声明依赖计算的一次快照；后续事务必须重新核对版本。
 * members 与 stopOrder 按消费者/子实例优先排序，startOrder 为反向的提供者/父实例顺序。
 * 不是已锁定计划、运行执行器或当前代切换回执。
 */
struct ReplacementClosure {
    std::vector<RuntimeId> runtimes;
    std::vector<ReplacementMember> members;
    std::vector<InstanceId> stopOrder;
    std::vector<InstanceId> startOrder;
};
} // namespace cordis
