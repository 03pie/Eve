/** @file notifications.cpp
 * @brief 有界通知候选值、纯历史与状态栏呈现，不持有插件对象。
 */
#include "notifications_p.hpp"
#include <QLabel>
#include <QPointer>
#include <algorithm>
#include <deque>
#include <limits>

namespace cordis::ui::detail {
struct NotificationControl : std::enable_shared_from_this<NotificationControl> {
    std::shared_ptr<qt::QtScheduler> scheduler;
    QPointer<QLabel> notification;
    std::shared_ptr<OutputControl> logs;
    WindowAccess access;
    PendingValues<NotificationSnapshot> pending{NotificationService::pendingCountLimit,
                                                NotificationService::pendingByteLimit,
                                                NotificationService::pendingTargetLimit};
    std::deque<NotificationSnapshot> history;
    std::optional<ErrorInfo> deliveryError;
    std::uint64_t sequence = 0;
    bool open = true;
    bool dirty = false;
    bool rendering = false;

    Result<void> thread() const
    {
        if (!scheduler->isCurrentThread())
            return Result<void>::failure(makeError(ErrorCode::WrongThread, "notifications require owner thread"));
        return Result<void>::success();
    }
    Result<void> business() const
    {
        const auto checked = thread();
        if (!checked.hasValue())
            return checked;
        if (!scheduler->isOpen())
            return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "notification scheduler closed"));
        if (!open || !notification)
            return Result<void>::failure(makeError(ErrorCode::ContextClosing, "notifications closed"));
        return Result<void>::success();
    }
    /** @brief 所有提交共用一次校验，UTF-8 与输出镜像一致。 */
    Result<void> validate(const Notification& value) const
    {
        if (value.text.empty() || value.text.size() > NotificationService::textLimit ||
            (value.level != NotificationLevel::Info && value.level != NotificationLevel::Warning &&
             value.level != NotificationLevel::Error) ||
            sequence == std::numeric_limits<std::uint64_t>::max())
            return Result<void>::failure(
                makeError(ErrorCode::InvalidArgument, "invalid notification or exhausted sequence"));
        if (QString::fromUtf8(value.text.data(), static_cast<int>(value.text.size())).toUtf8().toStdString() !=
            value.text)
            return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "notification requires UTF-8 text"));
        return Result<void>::success();
    }
    /** @brief 先修改纯值历史并镜像；调用此入口不触发 Qt 信号。 */
    void admit(NotificationSnapshot value)
    {
        const auto owner = value.owner;
        const auto logText = value.notification.text;
        const auto level = value.notification.level;
        const auto position =
            std::lower_bound(history.begin(), history.end(), value.sequence,
                             [](const auto& entry, std::uint64_t sequence) { return entry.sequence < sequence; });
        history.insert(position, std::move(value));
        while (history.size() > NotificationService::historyLimit)
            history.pop_front();
        dirty = true;
        if (logs) {
            auto mirrored = appendOutput(logs, owner, "通知", logText,
                                         level == NotificationLevel::Error     ? OutputLevel::Error
                                         : level == NotificationLevel::Warning ? OutputLevel::Warning
                                                                               : OutputLevel::Info,
                                         false);
            if (!mirrored.hasValue() && !deliveryError)
                deliveryError = mirrored.error();
        }
    }
    /** @brief 复用外壳访问屏障，嵌套通知只使原投影再次失效。 */
    void render()
    {
        if (!open || !dirty || !notification || !access || rendering)
            return;
        rendering = true;
        while (open && dirty && notification && access) {
            dirty = false;
            if (history.empty())
                break;
            const auto latest = history.back().notification;
            const auto prefix = latest.level == NotificationLevel::Error     ? QString::fromUtf8("错误：")
                                : latest.level == NotificationLevel::Warning ? QString::fromUtf8("提示：")
                                                                             : QString::fromUtf8("消息：");
            const auto text = prefix + QString::fromStdString(latest.text);
            auto held = shared_from_this();
            auto guard = access;
            guard([held, text] {
                if (!held->open || !held->notification)
                    return;
                held->notification->setText(text);
                if (held->open && held->notification)
                    held->notification->setToolTip(text);
            });
        }
        rendering = false;
    }
};

std::shared_ptr<NotificationControl> createNotifications(std::shared_ptr<qt::QtScheduler> scheduler,
                                                         QLabel* notification)
{
    auto result = std::make_shared<NotificationControl>();
    result->scheduler = std::move(scheduler);
    result->notification = notification;
    notification->setTextFormat(Qt::PlainText);
    return result;
}
void configureNotifications(const std::shared_ptr<NotificationControl>& self, std::shared_ptr<OutputControl> output,
                            WindowAccess access)
{
    self->logs = std::move(output);
    self->access = std::move(access);
}
std::shared_ptr<PendingSource> notificationSource(const std::shared_ptr<NotificationControl>& self,
                                                  ContextIdentity owner)
{
    auto source = self->pending.source(owner);
    return source ? source : std::make_shared<PendingSource>(owner);
}
void settleNotifications(const std::shared_ptr<NotificationControl>& self, bool render)
{
    if (!self->scheduler->isCurrentThread() || !self->open)
        return;
    auto ready = self->pending.takeReady();
    for (auto& value : ready)
        self->admit(std::move(value));
    if (render) {
        self->render();
        if (self->logs)
            refreshOutput(self->logs);
    }
}
/** @brief 开放期间报告观察/镜像错误，关闭后只保留纯历史。 */
static Result<void> notificationStatus(const std::shared_ptr<NotificationControl>& self)
{
    const auto checked = self->thread();
    if (!checked.hasValue())
        return checked;
    if (!self->open)
        return Result<void>::success();
    const auto pending = self->pending.status();
    if (!pending.hasValue())
        return pending;
    if (self->deliveryError)
        return Result<void>::failure(*self->deliveryError);
    return Result<void>::success();
}
Result<void> prepareNotification(const std::shared_ptr<NotificationControl>& self, const Notification& value)
{
    const auto checked = self->business();
    if (!checked.hasValue())
        return checked;
    settleNotifications(self, false);
    const auto status = notificationStatus(self);
    return status.hasValue() ? self->validate(value) : status;
}
/** @brief 普通和候选提交保留同一序号，失败接纳不占号。 */
static Result<std::uint64_t> submit(const std::shared_ptr<NotificationControl>& self, ContextIdentity owner,
                                    Notification value, const std::shared_ptr<PendingSource>& source)
{
    using Output = Result<std::uint64_t>;
    const auto checked = prepareNotification(self, value);
    if (!checked.hasValue())
        return Output::failure(checked.error());
    auto decision = source ? source->decision() : Result<std::optional<bool>>::success(true);
    if (!decision.hasValue())
        return Output::failure(decision.error());
    if (decision.value().has_value() && !*decision.value())
        return Output::failure(makeError(ErrorCode::ContextClosing, "notification publication aborted"));
    const auto sequence = self->sequence + 1;
    NotificationSnapshot entry{sequence, owner, std::move(value)};
    if (!decision.value().has_value()) {
        const auto bytes = entry.notification.text.size();
        auto queued =
            self->pending.enqueue(std::move(entry), source,
                                  std::to_string(owner.instance.value()) + "/" + std::to_string(owner.epoch), bytes);
        if (!queued.hasValue())
            return Output::failure(queued.error());
        self->sequence = sequence;
    } else {
        self->sequence = sequence;
        self->admit(std::move(entry));
    }
    self->render();
    if (self->logs)
        refreshOutput(self->logs);
    return Output::success(sequence);
}
Result<std::uint64_t> submitNotification(const std::shared_ptr<NotificationControl>& self, ContextIdentity owner,
                                         Notification value, std::shared_ptr<PendingSource> source)
{
    return submit(self, owner, std::move(value), source);
}
Result<std::uint64_t> appendNotification(const std::shared_ptr<NotificationControl>& self, ContextIdentity owner,
                                         Notification value)
{
    return submit(self, owner, std::move(value), {});
}
Result<std::vector<NotificationSnapshot>> notificationSnapshot(const std::shared_ptr<NotificationControl>& self)
{
    const auto checked = self->thread();
    if (!checked.hasValue())
        return Result<std::vector<NotificationSnapshot>>::failure(checked.error());
    settleNotifications(self, false);
    const auto status = notificationStatus(self);
    if (!status.hasValue())
        return Result<std::vector<NotificationSnapshot>>::failure(status.error());
    return Result<std::vector<NotificationSnapshot>>::success({self->history.begin(), self->history.end()});
}
void closeNotifications(const std::shared_ptr<NotificationControl>& self)
{
    self->open = false;
    self->pending.clear();
    self->access = {};
}
} // namespace cordis::ui::detail
