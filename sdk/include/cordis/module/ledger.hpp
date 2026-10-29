/** @file ledger.hpp
 * @brief 模块代次的宿主资源账目；账目清空不等于获得物理卸载许可。
 */
#pragma once
#include <cordis/result.hpp>
#include <memory>
#include <string>
#include <vector>

namespace cordis::module {
namespace detail {
struct LedgerControl;
}

/** @brief 可执行引用分类；不将业务实例的 Active/Disposed 当作 DLL 驻留状态。 */
enum class ResourceKind {
    Instance,
    SetupTask,
    CleanupTask,
    Call,
    Worker,
    Callback,
    QueuePayload,
    ServiceObject,
    QObject,
    ExternalHandle
};

/** @brief 一条宿主值记录，不包含插件指针或可调用对象。 */
struct ResourceRecord {
    std::uint64_t id;
    ResourceKind kind;
    std::string label;
};

/** @brief 原子读取的账目副本；pin 不会随记录释放而自动解除。 */
struct LedgerSnapshot {
    GenerationId generation;
    bool accepting;
    std::vector<ResourceRecord> records;
    std::vector<std::string> pinReasons;
    /** @brief 只证明本账目已停止接收且无已登记引用，不证明允许卸载。 */
    bool drained() const noexcept { return !accepting && records.empty(); }
};

/** @brief 一次引用登记的唯一所有者；必须在插件资源及其捕获完整销毁后释放。 */
class ModuleLease {
public:
    /** @brief 创建不持有记录的空租约。 */
    ModuleLease() noexcept = default;
    /** @brief 租约不能复制，防止不清晰的资源释放责任。 */
    ModuleLease(const ModuleLease&) = delete;
    /** @brief 租约不能复制赋值。 */
    ModuleLease& operator=(const ModuleLease&) = delete;
    /** @brief 转移登记，原租约变为空。 */
    ModuleLease(ModuleLease&& other) noexcept;
    /** @brief 先释放当前登记，再转移新登记。 */
    ModuleLease& operator=(ModuleLease&& other) noexcept;
    /** @brief 只释放宿主记录，可在任意线程执行；不调用插件代码或卸载库。 */
    ~ModuleLease();
    /** @brief 幂等释放本条登记；调用前由资源适配器保证真实析构已经结束。 */
    void reset() noexcept;
    /** @brief 检查当前是否拥有一条登记。 */
    explicit operator bool() const noexcept { return id_ != 0; }

private:
    friend class GenerationLedger;
    ModuleLease(std::shared_ptr<detail::LedgerControl> control, std::uint64_t id) noexcept;
    std::shared_ptr<detail::LedgerControl> control_;
    std::uint64_t id_ = 0;
};

/** @brief 一个新代次的线程安全宿主账目；副本指向同一代次。 */
class GenerationLedger {
public:
    /** @brief 分配进程内唯一代次；耗尽时抛 overflow_error，不循环复用标识。 */
    GenerationLedger();
    /** @brief 句柄复制共享同一代次；右值复制同样保持原句柄有效。 */
    GenerationLedger(const GenerationLedger&) = default;
    /** @brief 改为共享另一账目，不影响旧账目尚存租约的安全退出。 */
    GenerationLedger& operator=(const GenerationLedger&) = default;
    /** @brief 在排空前登记引用，最多 4096 条；空/超长标签或未知类别被拒绝。 */
    Result<ModuleLease> acquire(ResourceKind kind, std::string label) const;
    /** @brief 永久拒绝新登记；已存在租约仍需等到真实资源退出。 */
    void beginDraining() const;
    /** @brief 增加不可自动解除的驻留原因；重复原因幂等，最多 64 项。 */
    Result<void> pin(std::string reason) const;
    /** @brief 复制当前记录及驻留原因，不提供计数清零或移除其他租约的能力。 */
    LedgerSnapshot snapshot() const;

private:
    std::shared_ptr<detail::LedgerControl> control_;
};
} // namespace cordis::module
