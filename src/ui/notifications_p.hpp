/** @file notifications_p.hpp
 * @brief 通知的有界宿主值后端；原命令服务仅持有控制块及委派入口。
 */
#pragma once
#include "output_p.hpp"
#include "pending_values_p.hpp"
#include <cordis/ui/notifications.hpp>

class QLabel;
namespace cordis::ui::detail {
struct NotificationControl;
void installNotificationCenter(const std::shared_ptr<NotificationControl>& control, QWidget* window,
                               std::function<QString(InstanceId)> sourceName);
Result<void> toggleNotificationCenter(const std::shared_ptr<NotificationControl>& control, QWidget* origin);
void muteNotifications(const std::shared_ptr<NotificationControl>& control, bool muted);
void watchNotificationArchive(const std::shared_ptr<NotificationControl>& control,
                              std::shared_ptr<RuntimeLogArchive> archive);
/** @brief 状态栏控件由外壳拥有，通知历史和候选值由此后端拥有。 */
std::shared_ptr<NotificationControl> createNotifications(std::shared_ptr<qt::QtScheduler> scheduler,
                                                         QLabel* notification);
/** @brief 接入唯一输出内核和原窗口访问屏障。 */
void configureNotifications(const std::shared_ptr<NotificationControl>& control, std::shared_ptr<OutputControl> output,
                            WindowAccess access);
/** @brief 创建观察前先纯值结算并校验，拒绝的文本不登记无用观察。 */
Result<void> prepareNotification(const std::shared_ptr<NotificationControl>& control, const Notification& notification);
/** @brief 同一候选 owner/epoch 复用原来源；新来源由 Context 私有桥填充。 */
std::shared_ptr<PendingSource> notificationSource(const std::shared_ptr<NotificationControl>& control,
                                                  ContextIdentity owner);
/** @brief 插件值已由真实 Context 校验，来源只用于接纳或暂存复制值。 */
Result<std::uint64_t> submitNotification(const std::shared_ptr<NotificationControl>& control, ContextIdentity owner,
                                         Notification notification, std::shared_ptr<PendingSource> source);
/** @brief 已公开的命令结果沿同一校验/接纳内核即时提交宿主值。 */
Result<std::uint64_t> appendNotification(const std::shared_ptr<NotificationControl>& control, ContextIdentity owner,
                                         Notification notification);
/** @brief 整批结算及镜像完成后才可呈现；只读/关闭路径传 false。 */
void settleNotifications(const std::shared_ptr<NotificationControl>& control, bool render = true);
/** @brief 纯值快照先结算已有终态，关闭后仍可读取已接纳历史。 */
Result<std::vector<NotificationSnapshot>> notificationSnapshot(const std::shared_ptr<NotificationControl>& control);
/** @brief 关门丢弃尚未确认值并释放来源；不删除纯历史。 */
void closeNotifications(const std::shared_ptr<NotificationControl>& control);
} // namespace cordis::ui::detail
