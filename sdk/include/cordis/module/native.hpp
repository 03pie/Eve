/** @file native.hpp
 * @brief 同进程原生业务模块的显式创建、启动、关闭；不公开 ABI 函数或 DLL 句柄。
 */
#pragma once
#include <cordis/effect.hpp>
#include <cordis/module/ledger.hpp>
#include <cordis/service.hpp>
#include <optional>

namespace cordis::module {
class ModuleManager;
namespace detail {
struct NativeModuleControl;
struct NativeInstanceControl;
struct NativeServices;
} // namespace detail
enum class NativeInstanceState { Created, Starting, Started, Stopping, Destroyed, Failed };
struct NativeLogRecord {
    std::uint32_t level;
    std::string text;
};
struct NativeInstanceSnapshot {
    GenerationId generation;
    InstanceId instance;
    Epoch epoch;
    std::string pluginId;
    NativeInstanceState state;
    bool nativeCreated;
    bool closing;
    bool uncertain;
    bool deliveryBlocked;
    std::size_t pendingRequests;
    std::size_t activeHostCalls;
    std::optional<ErrorInfo> failure;
    std::vector<NativeLogRecord> logs;
    std::size_t pendingHostOperations;
    ScopeSnapshot resources;
    std::vector<BindingSnapshot> services;
    std::size_t committedServices;
};
/** @brief owner 线程使用的共享实例句柄，析构不代替显式 close。
 * 失败关闭保留原生实例与模块引用；成功关闭才执行并确认 destroy。
 */
class NativeInstance {
public:
    Task<void> start() const;
    Task<CleanupReport> close() const;
    /** @brief 只观察关闭/不确定资源报告，不发起关闭，可跨线程复制观察句柄。 */
    Task<CleanupReport> closed() const;
    Result<NativeInstanceSnapshot> snapshot() const;

private:
    friend class NativeModule;
    friend struct detail::NativeServices;
    explicit NativeInstance(std::shared_ptr<detail::NativeInstanceControl> control) : control_(std::move(control)) {}
    std::shared_ptr<detail::NativeInstanceControl> control_;
};
struct NativeModuleSnapshot {
    GenerationId generation;
    bool closing;
    bool closed;
    bool faulted;
    std::vector<NativeInstanceSnapshot> instances;
};
/** @brief 每代模块至多一个活动宿主会话；业务表协商结果进程缓存，HostTable 寿命稳定。
 * 关闭会话前不能 drain 模块；默认驻留，不提供物理卸载。
 */
class NativeModule {
public:
    /** @brief 显式索引支持跨模块服务；未传入时首次服务操作创建本代私有索引。
     * 共享索引须使用同一 scheduler，调用方保证其中所有活动实例的 InstanceId 唯一。
     * 活动模块一旦使用索引就不能切换；默认索引不提供与其他模块的隐式连接。
     */
    static Result<NativeModule> open(ModuleManager& manager, GenerationId generation,
                                     std::shared_ptr<IScheduler> scheduler, std::optional<ServiceRegistry> services = {});
    Result<NativeInstance> create(std::string pluginId, std::string configJson, InstanceId instance, Epoch epoch) const;
    Task<CleanupReport> close() const;
    Result<NativeModuleSnapshot> snapshot() const;

private:
    explicit NativeModule(std::shared_ptr<detail::NativeModuleControl> control) : control_(std::move(control)) {}
    std::shared_ptr<detail::NativeModuleControl> control_;
};
} // namespace cordis::module
