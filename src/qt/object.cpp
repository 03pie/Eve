/** @file object.cpp
 * @brief 在 Qt 批准的 DeferredDelete 时机完成删除，并在析构返回后发布资源确认。
 */
#include <cordis/qt/object.hpp>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cordis::qt::detail {
/** @brief 全部可变状态只在应用线程访问，Promise 允许任意线程观察。 */
struct ObjectControl {
    const std::thread::id owner = std::this_thread::get_id();
    QPointer<QObject> object;
    QObject* guard = nullptr;
    bool closing = false;
    bool deleting = false;
    bool observed = false;
    bool confirmed = false;
    Promise<void> closed;
    /** @brief 不依赖 scheduler 继续接收任务，但需要 Qt 事件循环处理延迟删除。 */
    void requestClose()
    {
        if (closing || closed.task().isReady()) return;
        closing = true;
        if (object && guard) object->deleteLater();
        else closed.reject(makeError(ErrorCode::CleanupFailed, "QObject deletion guard unavailable"));
    }
};
/** @brief 守卫不以目标为 parent，确保目标的全部子对象析构后仍能报告确认。 */
class ObjectGuard final : public QObject {
public:
    /** @brief 应用持有守卫，句柄丢弃不丢失资源账目。 */
    ObjectGuard(QCoreApplication* app, std::shared_ptr<ObjectControl> control)
        : QObject(app), control_(std::move(control)) {}
    /** @brief 应用停止而资源未确认时保守报告失败，不在 GUI 已退出后强制析构对象。 */
    ~ObjectGuard() override
    {
        control_->guard = nullptr;
        if (!control_->closed.task().isReady())
            control_->closed.reject(makeError(ErrorCode::CleanupFailed, "application ended before QObject deletion confirmed"));
    }
    /** @brief destroyed 早于子对象析构；只有受控删除路径可在稍后发布成功。 */
    void observed()
    {
        auto control = control_;
        control->observed = true;
        if (!control->deleting) {
            deleteLater();
            control->closed.reject(makeError(ErrorCode::CleanupFailed, "QObject destroyed outside resource deletion barrier"));
        }
    }
protected:
    /** @brief Qt 已批准延迟删除时接管 delete；删除 watched 后必须返回 true。 */
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto control = control_;
        if (event->type() != QEvent::DeferredDelete || watched != control->object.data() || !control->closing)
            return false;
        control->deleting = true;
        delete watched;
        control->deleting = false;
        control->confirmed = control->observed;
        deleteLater();
        if (control->confirmed) control->closed.resolve();
        else control->closed.reject(makeError(ErrorCode::CleanupFailed, "QObject destroyed notification missing"));
        return true;
    }
private:
    std::shared_ptr<ObjectControl> control_;
};
} // namespace cordis::qt::detail

namespace cordis::qt {
/** @brief 先验证所有权与线程，再安装守卫；最后一步才转移调用方的 unique_ptr。 */
QObjectResource::QObjectResource(std::unique_ptr<QObject>& object, std::shared_ptr<QtScheduler> scheduler)
{
    auto app = QCoreApplication::instance();
    if (!app || QCoreApplication::closingDown() || !scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen() ||
        QThread::currentThread() != app->thread() || !object || object->thread() != app->thread() || object->parent())
        throw std::invalid_argument("QObjectResource requires an unparented application-thread object and open scheduler");
    control_ = std::make_shared<detail::ObjectControl>();
    control_->object = object.get();
    auto guard = std::make_unique<detail::ObjectGuard>(app, control_);
    control_->guard = guard.get();
    QObject::connect(object.get(), &QObject::destroyed, guard.get(), [target = guard.get()] { target->observed(); }, Qt::DirectConnection);
    QObject::connect(app, &QCoreApplication::aboutToQuit, guard.get(), [control = control_] { control->requestClose(); }, Qt::DirectConnection);
    object->installEventFilter(guard.get());
    object.release();
    guard.release();
}
/** @brief 不把错误线程请求混入共享关闭结果；关闭调用自身保留控制块以容忍重入释放句柄。 */
Task<void> QObjectResource::close() const
{
    auto control = control_;
    if (std::this_thread::get_id() != control->owner)
        return failedTask(makeError(ErrorCode::WrongThread, "QObjectResource close requires owner thread"));
    control->requestClose();
    return control->closed.task();
}
/** @brief 快照只观察确认状态，不推动或伪造资源销毁。 */
Result<ObjectSnapshot> QObjectResource::snapshot() const
{
    if (std::this_thread::get_id() != control_->owner)
        return Result<ObjectSnapshot>::failure(makeError(ErrorCode::WrongThread, "QObjectResource snapshot requires owner thread"));
    return Result<ObjectSnapshot>::success({control_->closing, !control_->object.isNull(), control_->observed, control_->confirmed});
}
} // namespace cordis::qt
