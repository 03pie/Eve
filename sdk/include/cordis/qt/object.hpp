/** @file object.hpp
 * @brief 声明独占 QObject 的延迟删除及完整析构确认屏障。
 */
#pragma once
#include <cordis/qt/scheduler.hpp>
#include <cordis/task.hpp>
#include <memory>

namespace cordis::qt {
namespace detail { struct ObjectControl; }
/** @brief 对象销毁信号与整个删除调用完成分开记账。 */
struct ObjectSnapshot {
    bool closing;
    bool alive;
    bool destroyedObserved;
    bool confirmed;
};
/** @brief 可共享的独占资源句柄；Qt 守卫持有资源直到显式关闭，释放句柄不跨线程删除对象。 */
class QObjectResource {
public:
    /** @brief owner 接管无 parent 对象；成功后清空 object，构造失败不转移所有权。
     * @param object 不得外部 delete/deleteLater、启用 WA_DeleteOnClose、迁移、重设 parent 或拦截 DeferredDelete；子对象遵循同样的亲和约束。
     * @throws std::invalid_argument 对象、线程或调度器不满足前置条件。
     */
    QObjectResource(std::unique_ptr<QObject>& object, std::shared_ptr<QtScheduler> scheduler);
    /** @brief owner 请求 deleteLater；整个删除调用返回后才成功，重复关闭返回同一屏障。 */
    Task<void> close() const;
    /** @brief owner 查询；意外外部销毁只报告失败，不伪造完整析构确认。 */
    Result<ObjectSnapshot> snapshot() const;
private:
    std::shared_ptr<detail::ObjectControl> control_;
};
} // namespace cordis::qt
