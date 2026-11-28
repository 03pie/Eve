/** @brief Shared value validation for desktop and windowless platform logging. */
#pragma once
#include "desktop_text.hpp"
#include <eve/ui/output.hpp>
#include <QString>
#include <algorithm>

namespace eve::ui::detail
{
/** @brief 平台截断和插件严格长度共用一次值校验；不登记目标或消耗序号。 */
inline Result<OutputEntry> prepareOutput(std::optional<ContextIdentity> owner, std::string channel, std::string text,
                                         OutputLevel level, bool strict)
{
    using Output = Result<OutputEntry>;
    if (channel.empty() || channel.size() > 64 || text.empty() ||
        (level != OutputLevel::Info && level != OutputLevel::Warning && level != OutputLevel::Error))
        return Output::failure(makeError(ErrorCode::InvalidArgument, "invalid output record"));
    if (text.size() > OutputService::textLimit) {
        if (strict)
            return Output::failure(makeError(ErrorCode::InvalidArgument, "output record exceeds text limit"));
        auto end = OutputService::textLimit - 3;
        while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
            --end;
        text.resize(end);
        text += "…";
    }
    if (QString::fromUtf8(channel.data(), static_cast<int>(channel.size())).toUtf8().toStdString() != channel ||
        QString::fromUtf8(text.data(), static_cast<int>(text.size())).toUtf8().toStdString() != text ||
        std::any_of(channel.begin(), channel.end(), [](unsigned char byte) { return byte < 0x20 || byte == 0x7f; }))
        return Output::failure(
            makeError(ErrorCode::InvalidArgument, "output requires UTF-8 text and printable channel"));
    auto id =
        owner ? "plugin/" + std::to_string(owner->instance.value()) + "/" + std::to_string(owner->epoch) + "/" + channel
              : "platform";
    const auto title = owner ? eve::desktopText("logs.instanceEpoch")
                                   .arg(QString::fromStdString(channel))
                                   .arg(owner->instance.value())
                                   .arg(owner->epoch)
                             : QStringLiteral("Platform");
    return Output::success({0, owner, std::move(id), title.toStdString(), std::move(text), level});
}
} // namespace eve::ui::detail
