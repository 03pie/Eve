/** @file pages.hpp
 * @brief 声明不暴露 ElaWindow 的页面贡献服务及本代 effect 句柄。
 */
#pragma once
#include <cordis/instance.hpp>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class QWidget;
namespace cordis::ui {
namespace detail { struct PageControl; struct ShellControl; }
class ElaShell;
/** @brief 工厂须在应用线程返回无 parent、未启用自动删除的 QWidget；只调用一次。 */
using PageFactory = std::function<std::unique_ptr<QWidget>()>;
/** @brief id/category 限定在实例与 epoch 内；同 category 聚合为插件入口，入口标题取首个页面，空 category 放在根导航。 */
struct PageSpec {
    std::string id;
    std::string title;
    std::string category;
    PageFactory factory;
};
/** @brief 不含裸页面或 Ela key 的宿主贡献诊断。 */
struct PageSnapshot {
    ContextIdentity owner;
    std::string id;
    bool navigationPresent;
    bool widgetAlive;
    bool current;
    bool retiring;
};
/** @brief 共享页面的 effect 生命周期；句柄析构不伪造异步撤销完成。 */
class PageHandle {
public:
    /** @brief 观察页面工厂及导航登记是否成功，始终通过 Task 排队观察。 */
    Task<void> ready() const;
    /** @brief owner 打开/恢复页签前重查实例/epoch 和贡献门禁；旧代及退役页不可重新进入。 */
    Result<void> navigate() const;
    /** @brief owner 幂等撤销，立即关闭输入/导航门禁，等待完整页面析构与工厂闭包释放。 */
    Task<CleanupReport> close() const;
    /** @brief owner 读取保留的诊断，贡献释放后仍可查询。 */
    Result<PageSnapshot> snapshot() const;
private:
    friend class UiPagesService;
    /** @brief 仅由成功登记的服务创建句柄。 */
    explicit PageHandle(std::shared_ptr<detail::PageControl> control) : control_(std::move(control)) {}
    std::shared_ptr<detail::PageControl> control_;
};
/** @brief 普通插件只通过此服务贡献页面，不接触宿主 ElaWindow 或导航内部对象。 */
class UiPagesService {
public:
    /** @brief owner 登记本代页面 effect；重复 id 拒绝且不调用工厂，停用自动撤销。 */
    Result<PageHandle> add(const Context& owner, PageSpec spec) const;
    /** @brief owner 读取仍保留的贡献，包括清理失败或等待销毁的页面。 */
    Result<std::vector<PageSnapshot>> snapshot() const;
private:
    friend class ElaShell;
    /** @brief 服务借用 shell 控制块，避免服务绑定与窗口互相强持。 */
    explicit UiPagesService(std::weak_ptr<detail::ShellControl> control) : control_(std::move(control)) {}
    std::weak_ptr<detail::ShellControl> control_;
};
} // namespace cordis::ui
