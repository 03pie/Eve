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
/** @brief 可随平台输出归档的诊断信息。operation 关联一次操作，code 用于识别同类诊断。 */
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
/** @brief ui.output/v1 的实时输出视图。宿主复制已发布的记录，并在已连接归档时提交给后台写入器。
 * historyLimit 与 channelLimit 限制实时历史。textLimit 是单条插件文本的 UTF-8 字节上限。
 * pending 系列常量限制等待资源组发布的记录条数、总字节数和目标数。
 */
class OutputService {
public:
    static constexpr std::size_t historyLimit = 1000, channelLimit = 64, textLimit = 4096;
    static constexpr std::size_t pendingCountLimit = 1000, pendingByteLimit = 1024 * 1024, pendingTargetLimit = 64;
    /** @brief 在 owner 调度线程按有效 epoch 写入 UTF-8 纯文本，未发布的记录有界等待原组终态。
     * @param owner 拥有本条输出的实例上下文，宿主使用实际代次标记来源。
     * @param channel 非空 UTF-8 通道名，最多 64 字节，禁止 ASCII 控制字符。
     * @param text 非空 UTF-8 文本，超过 textLimit 时返回 InvalidArgument。
     * @param level 该条记录的严重程度，默认为 Info。
     * @return 成功时返回本服务的序号。待发布记录被放弃时可留下空洞，持久化确认由宿主归档层另行报告。
     */
    Result<std::uint64_t> write(const Context& owner, std::string channel, std::string text,
                                OutputLevel level = OutputLevel::Info) const;
    /** @brief 在 owner 线程结算已有终态并复制当前有界历史。开放期间发布故障返回错误。
     * 宿主关闭后仍可读取其保留的历史，输出控制对象销毁后返回空快照。
     */
    Result<std::vector<OutputEntry>> snapshot() const;
private:
    friend class ElaShell;
    explicit OutputService(std::weak_ptr<detail::OutputControl> control) : control_(std::move(control)) {}
    std::weak_ptr<detail::OutputControl> control_;
};
} // namespace cordis::ui
