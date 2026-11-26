/** @file output.hpp
 * @brief 按实例代次隔离的纯文本输出；记录不保留插件对象或回调。
 */
#pragma once
#include <cordis/instance.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cordis::ui {
namespace detail { struct OutputControl; }
class ElaShell;
enum class OutputLevel { Info, Warning, Error };
struct OutputDiagnostic {
    std::optional<OperationId> operation;
    std::string code;
};
/** @brief 全部为宿主复制的值，空 owner 表示平台通道。 */
struct OutputEntry {
    std::uint64_t sequence;
    std::optional<ContextIdentity> owner;
    std::string channel, title, text;
    OutputLevel level;
};
/** @brief ui.output/v1；owner 写入，最多保留 1000 条、64 个目标，每条 4096 字节。 */
class OutputService {
public:
    static constexpr std::size_t historyLimit = 1000, channelLimit = 64, textLimit = 4096;
    static constexpr std::size_t pendingCountLimit = 1000, pendingByteLimit = 1024 * 1024, pendingTargetLimit = 64;
    /** @brief 同调度器/epoch 校验后复制；未发布值有界等待原组终态，不改变可见通道或历史。
     * 成功即保留本服务序号，放弃可留下空洞；发布后按保留序号接纳，拒绝不占号。
     */
    Result<std::uint64_t> write(const Context& owner, std::string channel, std::string text,
                                OutputLevel level = OutputLevel::Info) const;
    /** @brief owner 先纯值结算已有终态再复制历史；开放期间发布故障返回错误，关闭后历史仍可读。 */
    Result<std::vector<OutputEntry>> snapshot() const;
private:
    friend class ElaShell;
    explicit OutputService(std::weak_ptr<detail::OutputControl> control) : control_(std::move(control)) {}
    std::weak_ptr<detail::OutputControl> control_;
};
} // namespace cordis::ui
