/** @file qt_state_plugin.hpp
 * @brief 同工具链 Qt 插件的可选同步状态接口；不改变既有工厂和实例 V1。
 */
#pragma once
#include <QtPlugin>
#include <eve_abi/state.h>

namespace eve::module {
/** @brief root 的独立可选接口；缺失表示无状态，describe 的 UNSUPPORTED 仅表示该 PluginId 无状态。
 * 所有输入仅在调用中借用，输出使用宿主固定缓冲区；遵循 state.h 的大小、状态码和纯值约束。
 * 方法同步完成，不得处理异步请求、删除 root 或重入同一工厂的状态调用。
 */
class IQtStatePluginV1 {
public:
    virtual ~IQtStatePluginV1() = default;
    virtual EveStatus describeState(EveBytesV1 plugin, EveStateSchemaV1* output) = 0;
    virtual EveStatus validateState(EveBytesV1 plugin, const EveStateSchemaV1* source, EveBytesV1 json,
                                       EveStateBufferV1* output) = 0;
};
/** @brief 声明有状态的工厂创建的每个业务对象必须实现此接口。
 * restore 在 activate 前同步执行，capture 只用于已激活并被冻结的真实实例。
 * 状态方法不得删除/转移对象或调用 activate；对象继续由原 QObjectResource 独占管理。
 */
class IQtStateInstanceV1 {
public:
    virtual ~IQtStateInstanceV1() = default;
    virtual EveStatus captureState(EveStateBufferV1* output) = 0;
    virtual EveStatus restoreState(const EveStateSchemaV1* source, EveBytesV1 json) = 0;
};
} // namespace eve::module
Q_DECLARE_INTERFACE(eve::module::IQtStatePluginV1, "org.eve.IQtStatePluginV1/1.0")
Q_DECLARE_INTERFACE(eve::module::IQtStateInstanceV1, "org.eve.IQtStateInstanceV1/1.0")
