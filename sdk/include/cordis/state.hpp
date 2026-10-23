/**
 * @file state.hpp
 * @brief 声明运行时对象、绑定及 effect 的诊断状态。
 */
#pragma once

namespace cordis {
/**
 * @brief 运行时对象的诊断状态，不是持久化格式或公开二进制 ABI。
 */
enum class InstanceState {
    Pending,
    Loading,
    Active,
    Unloading,
    Failed,
    Disposed
};

/** @brief 服务绑定的诊断生命周期状态。 */
enum class BindingState { Staged, Published, Retiring, Released };
/** @brief effect 的初始化与清理状态。 */
enum class EffectState { SettingUp, Live, Closing, Closed };
/** @brief 用于稳定查询的实例最终结果分类。 */
enum class StableOutcome {
    Active,
    PendingMissingDependency,
    Disabled,
    Failed,
    Disposed,
    Superseded
};
} // namespace cordis
