/** @file store.hpp
 * @brief 将已接受目标与历史保存状态分开，以有界单写队列维护磁盘基线和退出刷新。
 */
#pragma once
#include <cordis/loader/file.hpp>
#include <cordis/loader/yaml.hpp>
#include <cordis/task.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace cordis::loader {
/** @brief owner 调用的事务写后端；任务终结表示该次 I/O 已结束，不得遗留独立后台工作。 */
class IConfigurationWriter {
public:
    /** @brief Store 排空后才释放最后一个后端所有者。 */
    virtual ~IConfigurationWriter() = default;
    /** @brief 比较完整磁盘版本；Unchanged 不得改变基线，可同步重入，异步期间自行保留入参。 */
    virtual Task<ConfigurationFileCommit> commit(std::shared_ptr<const ValidatedCandidate> target,
        ConfigurationFileVersion expected) = 0;
};
/** @brief 包括在途写入的普通保存上限；close 的最终刷新在队列排空后使用同一槽位。 */
struct StoreLimits {
    std::size_t maximumWrites = 64;
};
/** @brief 业务结果与回执投递失败分离；失败后未开始的写入为 Skipped。 */
enum class SaveOutcome { Written, Unchanged, Conflict, Failed, Skipped };
/** @brief 历史结果只含宿主值；冲突原文不随未来 rebase 改写。 */
struct SaveReport {
    std::uint64_t revision = 0;
    SaveOutcome outcome = SaveOutcome::Failed;
    std::optional<ErrorInfo> error;
    std::shared_ptr<const ConfigurationFileSnapshot> external;
};
/** @brief persisted 是最后获确认的历史保存目标，外部冲突不会伪装成新的保存成功。 */
struct StoreSnapshot {
    std::uint64_t desiredRevision = 0;
    std::shared_ptr<const ValidatedCandidate> desired;
    std::optional<std::uint64_t> persistedRevision;
    std::shared_ptr<const ValidatedCandidate> persisted;
    std::shared_ptr<const ConfigurationFileSnapshot> baseline;
    std::shared_ptr<const ConfigurationFileSnapshot> external;
    std::optional<ErrorInfo> lastError;
    std::size_t queuedWrites = 0;
    bool writing = false;
    bool dirty = false;
    bool closing = false;
    bool closed = false;
    bool deliveryBlocked = false;
};
namespace detail { struct StoreControl; }
/** @brief owner 单写 Store；初始基线由宿主读取，目标修订由协调器接受流程分配。 */
class ConfigurationStore {
public:
    /** @brief 构造不执行 I/O；基线原文不代表已验证或已经跟踪的目标。 */
    ConfigurationStore(std::shared_ptr<IScheduler> scheduler, std::shared_ptr<IConfigurationWriter> writer,
        ConfigurationFileSnapshot baseline, StoreLimits limits = {});
    /** @brief 已接受保存会自保留到排空，尚未请求保存的最新目标须由 close 刷新。 */
    ~ConfigurationStore();
    /** @brief 禁止复制执行队列所有者。 */
    ConfigurationStore(const ConfigurationStore&) = delete;
    /** @brief 禁止覆盖已有保存账目。 */
    ConfigurationStore& operator=(const ConfigurationStore&) = delete;
    /** @brief 跟踪协调器已经接受的不可变目标；修订须递增，同对象同修订可重复跟踪。 */
    Result<void> track(std::shared_ptr<const ValidatedCandidate> target, std::uint64_t acceptedRevision);
    /** @brief 以当前目标修订校验后排队；重复请求同一个尚未完成的修订共享任务。 */
    Result<Task<SaveReport>> save(std::uint64_t expectedDesiredRevision);
    /** @brief 宿主明确选择新的磁盘基线；仅允许队列空闲且目标修订匹配，不表示目标已保存。 */
    Result<void> rebase(ConfigurationFileSnapshot disk, std::uint64_t expectedDesiredRevision);
    /** @brief owner 读取期望、历史保存、基线及队列，不触发 I/O。 */
    Result<StoreSnapshot> snapshot() const;
    /** @brief 同步拒绝新业务，排空已接受保存并刷新最新 dirty 目标；未保存时终结为失败。 */
    Task<void> close();
private:
    std::shared_ptr<detail::StoreControl> control_;
};
} // namespace cordis::loader
