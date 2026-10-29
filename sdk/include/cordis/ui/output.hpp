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
    /** @brief 校验同调度器及当前 epoch 后复制；通道名最多 64 字节，旧记录按有界历史淘汰。 */
    Result<std::uint64_t> write(const Context& owner, std::string channel, std::string text,
                                OutputLevel level = OutputLevel::Info) const;
    /** @brief owner 复制保留的历史；不会执行插件代码。 */
    Result<std::vector<OutputEntry>> snapshot() const;
private:
    friend class ElaShell;
    explicit OutputService(std::weak_ptr<detail::OutputControl> control) : control_(std::move(control)) {}
    std::weak_ptr<detail::OutputControl> control_;
};
} // namespace cordis::ui
