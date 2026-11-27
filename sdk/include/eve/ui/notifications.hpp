/** @file notifications.hpp
 * @brief 声明只保存宿主值数据的有界通知服务。
 */
#pragma once
#include <eve/instance.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eve::ui {
namespace detail { struct ServicesControl; }
class ElaShell;
/** @brief 通知级别只控制宿主展示，不允许嵌入任意 QWidget 或回调。 */
enum class NotificationLevel { Info, Warning, Error };
/** @brief 文本按纯文本显示；operation 可为无效零值，不保存插件对象。 */
struct Notification {
    NotificationLevel level = NotificationLevel::Info;
    std::string text;
    OperationId operation;
};
/** @brief 复制归属、序号和内容，实例结束后仍可保留历史，不延长插件生命周期。 */
struct NotificationSnapshot {
    std::uint64_t sequence;
    ContextIdentity owner;
    Notification notification;
};
/** @brief ui.notifications/v1；仅 owner 可提交，服务保留最多 100 条、每条最多 4096 字节。 */
class NotificationService {
public:
    static constexpr std::size_t historyLimit = 100;
    static constexpr std::size_t textLimit = 4096;
    static constexpr std::size_t pendingCountLimit = 100, pendingByteLimit = 256 * 1024, pendingTargetLimit = 100;
    /** @brief 验证 Context/epoch/同 scheduler 后接受 UTF-8 文本并返回单调序号；接受不代表已呈现。
     * Held 值独立有界保存，完整发布后进入历史及输出镜像，Abort 可留序号空洞；拒绝不占号。
     * 镜像在通知公开时才获独立输出序号，之前的输出清空不影响未来镜像；投递错误由后续操作报告。
     */
    Result<std::uint64_t> post(const Context& owner, Notification notification) const;
    /** @brief owner 读取由旧到新排列的值快照；关闭后仍可读取已保留历史。 */
    Result<std::vector<NotificationSnapshot>> snapshot() const;
private:
    friend class ElaShell;
    /** @brief 服务本身不强持窗口，也不保存调用方 Context。 */
    explicit NotificationService(std::weak_ptr<detail::ServicesControl> control) : control_(std::move(control)) {}
    std::weak_ptr<detail::ServicesControl> control_;
};
} // namespace eve::ui
