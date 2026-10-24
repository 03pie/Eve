/** @file commands.hpp
 * @brief 声明按实例代次托管、等待异步调用排空的命令贡献。
 */
#pragma once
#include <cordis/instance.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cordis::ui {
namespace detail { struct CommandControl; struct ServicesControl; }
class ElaShell;
/** @brief id 限定在实例/epoch；execute 的 Task 完成代表本次工作结束，不得等待自身关闭。
 * enabled 可省略；谓词和执行回调都在 owner 运行，不得长时间阻塞。
 */
struct CommandSpec {
    std::string id;
    std::string title;
    std::function<Task<void>()> execute;
    std::function<bool()> enabled;
};
/** @brief 快照不调用插件谓词；activeCalls 包含排队、执行、等待源结果和谓词求值。
 * deliveryBlocked 表示已接受调用的推进被调度器阻断，资源会保守保留。
 */
struct CommandSnapshot {
    ContextIdentity owner;
    std::string id;
    std::string title;
    bool retiring;
    std::size_t activeCalls;
    bool deliveryBlocked;
};
/** @brief 可共享的本代命令能力，撤销立即关门，清理等待回调及捕获释放。 */
class CommandHandle {
public:
    /** @brief 观察命令 effect 初始化结果。 */
    Task<void> ready() const;
    /** @brief owner 同步查询谓词，异常转为 CallbackFailed，求值后重查业务门禁。 */
    Result<bool> enabled() const;
    /** @brief owner 接受调用后排队，进入谓词/回调前重查门禁；禁用返回失败 Task。 */
    Task<void> execute() const;
    /** @brief owner 幂等撤销，等待所有已接受调用及 effect 完整关闭。 */
    Task<CleanupReport> close() const;
    /** @brief owner 读取历史诊断，不求值 enabled，也不授予重新执行能力。 */
    Result<CommandSnapshot> snapshot() const;
private:
    friend class CommandService;
    /** @brief 仅成功登记后创建命令句柄。 */
    explicit CommandHandle(std::shared_ptr<detail::CommandControl> control) : control_(std::move(control)) {}
    std::shared_ptr<detail::CommandControl> control_;
};
/** @brief ui.commands/v1；普通插件不获得 QListWidget、QAction 或 ElaWindow。 */
class CommandService {
public:
    /** @brief owner 创建本代 effect；重复 id 拒绝，停止实例自动撤销。 */
    Result<CommandHandle> add(const Context& owner, CommandSpec spec) const;
    /** @brief owner 读取所有保留的命令，包括排空中或投递阻断的记录。 */
    Result<std::vector<CommandSnapshot>> snapshot() const;
private:
    friend class ElaShell;
    /** @brief 服务借用宿主控制块，生命周期由外壳与 effect 管理。 */
    explicit CommandService(std::weak_ptr<detail::ServicesControl> control) : control_(std::move(control)) {}
    std::weak_ptr<detail::ServicesControl> control_;
};
} // namespace cordis::ui
