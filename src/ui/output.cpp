/** @file output.cpp
 * @brief 对外输出服务、发布接纳与有界实时记录。
 */
#include "output_state_p.hpp"
#include <ElaComboBox.h>
#include <ElaToolButton.h>
#include <QDateTime>
#include <QPlainTextEdit>
#include <QTimer>
#include <algorithm>
#include <limits>
#include <utility>
namespace cordis::ui::detail
{
namespace
{
/** @brief 平台截断和插件严格长度共用一次值校验；不登记目标或消耗序号。 */
Result<OutputEntry> prepareOutput(std::optional<ContextIdentity> owner, std::string channel, std::string text,
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
    const auto title = owner ? QString::fromUtf8("%1 · 实例 #%2 / 代 %3")
                                   .arg(QString::fromStdString(channel))
                                   .arg(owner->instance.value())
                                   .arg(owner->epoch)
                             : QString::fromUtf8("平台");
    return Output::success({0, owner, std::move(id), title.toStdString(), std::move(text), level});
}
} // namespace
bool outputMatches(const LogRecord& row, const LogFilter& filter)
{
    const auto& e = row.entry;
    return (filter.sourceId.isEmpty() || filter.sourceId == row.sourceId) && e.level >= filter.minimumLevel &&
           (!filter.instance || (e.owner && e.owner->instance == *filter.instance)) &&
           (!filter.epoch || (e.owner && e.owner->epoch == *filter.epoch)) &&
           (filter.text.isEmpty() || QString::fromStdString(e.text).contains(filter.text, Qt::CaseInsensitive));
}
void OutputControl::admit(OutputEntry entry, std::optional<OperationId> notification)
{
    QString name;
    if (entry.owner) {
        const auto known = publishedOwners.find(entry.owner->instance);
        if (known != publishedOwners.end())
            name = known->second;
        else if (resolve) {
            name = resolve(entry.owner->instance);
            if (!name.isEmpty())
                publishedOwners[entry.owner->instance] = name;
        }
    }
    const QString source = !entry.owner     ? QStringLiteral("platform")
                           : name.isEmpty() ? QString("instance/%1").arg(entry.owner->instance.value())
                                            : "plugin/" + name;
    if (name.isEmpty())
        name = entry.owner ? QString::fromStdString(entry.channel).section('/', 3) : QString::fromUtf8("平台");
    LogRecord row{std::move(entry), source, name, QDateTime::currentMSecsSinceEpoch(), 0, notification};
    row.eventId = archive ? archive->append(row.entry, source, name, row.utcMs,notification) : row.entry.sequence;
    if (!retainedChannels.count(row.entry.channel))
        channelsDirty = true;
    if (!archive)
        collectLogSource(liveSources, row);
    if (live() && !browsingOlder && outputMatches(row, filter)) {
        if (!textDirty && appended.size() < OutputService::historyLimit &&
            (entries.empty() || row.entry.sequence > entries.back().entry.sequence))
            appended.push_back(row);
        else
            textDirty = true;
        if (follow && !follow->isChecked())
            ++unread;
    }
    ++retainedChannels[row.entry.channel];
    const auto at = std::lower_bound(entries.begin(), entries.end(), row.entry.sequence,
                                     [](const auto& item, auto seq) { return item.entry.sequence < seq; });
    entries.insert(at, std::move(row));
    while (entries.size() > OutputService::historyLimit || retainedChannels.size() > OutputService::channelLimit) {
        const auto& oldest = entries.front();
        liveLines.erase(oldest.entry.sequence);
        if (live() && !browsingOlder && outputMatches(oldest, filter))
            textDirty = true;
        if (!--retainedChannels[oldest.entry.channel]) {
            retainedChannels.erase(oldest.entry.channel);
            channelsDirty = true;
        }
        entries.pop_front();
    }
}
std::shared_ptr<OutputControl> createOutput(std::shared_ptr<qt::QtScheduler> scheduler, QPlainTextEdit* editor,
                                            WindowAccess access)
{
    auto result = std::make_shared<OutputControl>();
    result->scheduler = std::move(scheduler);
    result->editor = editor;
    result->access = std::move(access);
    editor->setPlaceholderText(QString::fromUtf8("当前目标暂无输出。运行命令或插件操作后会在这里显示真实记录。"));
    return result;
}
void refreshOutput(const std::shared_ptr<OutputControl>& self)
{
    if (!self || !self->scheduler->isCurrentThread() || !self->open || !self->access || self->presenting)
        return;
    self->presenting = true;
    while (self->open && (self->channelsDirty || self->textDirty || !self->appended.empty())) {
        const bool updateChannels = std::exchange(self->channelsDirty, false);
        const bool updateText = std::exchange(self->textDirty, false);
        const bool preserve = std::exchange(self->preserveText, true);
        auto added = std::exchange(self->appended, {});
        // 模型/text 信号可重入接纳；只跨信号保留本次复制的投影。
        auto access = self->access;
        access([self, updateChannels, updateText, preserve, &added] {
            if (!self->open)
                return;
            if (updateChannels && self->targets)
                self->syncSources();
            if (self->open && (updateText || !added.empty()))
                self->renderText(preserve, updateText, added);
        });
    }
    self->presenting = false;
}
void settleOutput(const std::shared_ptr<OutputControl>& self, bool render)
{
    if (!self || !self->scheduler->isCurrentThread())
        return;
    auto ready = self->pending.takeReady();
    for (auto& entry : ready)
        self->admit(std::move(entry));
    if (render)
        refreshOutput(self);
}
namespace
{
void presentIncoming(const std::shared_ptr<OutputControl>& state)
{
    if (!state->open || !state->editor)
        return;
    // Command feedback stays synchronous. A burst only paints one further bounded projection at the event-loop
    // boundary.
    if (!state->rendersThisTurn) {
        std::weak_ptr<OutputControl> weak = state;
        QTimer::singleShot(0, state->editor, [weak] {
            if (const auto s = weak.lock(); s && s->open) {
                s->rendersThisTurn = 0;
                refreshOutput(s);
            }
        });
    }
    if (++state->rendersThisTurn <= 8)
        refreshOutput(state);
}
} // namespace
Result<std::uint64_t> appendOutput(const std::shared_ptr<OutputControl>& self, std::optional<ContextIdentity> owner,
                                   std::string channel, std::string text, OutputLevel level, bool render,
                                   std::optional<OperationId> notification)
{
    using Output = Result<std::uint64_t>;
    if (!self->scheduler->isCurrentThread())
        return Output::failure(makeError(ErrorCode::WrongThread, "output requires owner thread"));
    if (!self->open || !self->scheduler->isOpen())
        return Output::failure(makeError(ErrorCode::ContextClosing, "output closed"));
    settleOutput(self, false);
    auto prepared = prepareOutput(owner, std::move(channel), std::move(text), level, false);
    if (!prepared.hasValue())
        return Output::failure(prepared.error());
    if (self->sequence == std::numeric_limits<std::uint64_t>::max())
        return Output::failure(makeError(ErrorCode::InvalidArgument, "output sequence exhausted"));
    const auto sequence = self->sequence + 1;
    prepared.value().sequence = sequence;
    self->admit(std::move(prepared).value(),notification);
    self->sequence = sequence;
    if (render)
        presentIncoming(self);
    return Output::success(sequence);
}
void closeOutput(const std::shared_ptr<OutputControl>& self)
{
    if (!self)
        return;
    settleOutput(self, false);
    self->pending.clear();
    self->open = false;
    self->access = {};
}
} // namespace cordis::ui::detail

namespace cordis::ui
{
Result<std::uint64_t> OutputService::write(const Context& owner, std::string channel, std::string text,
                                           OutputLevel level) const
{
    using Output = Result<std::uint64_t>;
    auto state = control_.lock();
    if (!state)
        return Output::failure(makeError(ErrorCode::ContextClosing, "output host expired"));
    const auto guard = owner.eventGuard(state->scheduler);
    if (!guard.hasValue())
        return Output::failure(guard.error());
    if (!state->open)
        return Output::failure(makeError(ErrorCode::ContextClosing, "output closed"));
    detail::settleOutput(state, false);
    const auto available = state->pending.status();
    if (!available.hasValue())
        return Output::failure(available.error());
    const auto identity = owner.identity();
    if (!identity.hasValue())
        return Output::failure(identity.error());
    auto prepared = detail::prepareOutput(identity.value(), std::move(channel), std::move(text), level, true);
    if (!prepared.hasValue())
        return Output::failure(prepared.error());
    if (state->sequence == std::numeric_limits<std::uint64_t>::max())
        return Output::failure(makeError(ErrorCode::InvalidArgument, "output sequence exhausted"));
    auto source = state->pending.source(identity.value());
    if (!source) {
        source = std::make_shared<detail::PendingSource>(identity.value());
        const std::weak_ptr<detail::OutputControl> weak = state;
        auto presentation = owner.presentation(state->scheduler, source->observe([weak] {
            if (auto host = weak.lock())
                detail::settleOutput(host);
        }));
        if (!presentation.hasValue())
            return Output::failure(presentation.error());
        source->publication = std::move(presentation.value().publication);
        source->observation = std::move(presentation.value().observation);
    }
    if (!state->open)
        return Output::failure(makeError(ErrorCode::ContextClosing, "output closed during registration"));
    const auto decision = source->decision();
    if (!decision.hasValue())
        return Output::failure(decision.error());
    const auto sequence = state->sequence + 1;
    prepared.value().sequence = sequence;
    if (!decision.value().has_value()) {
        const auto target = prepared.value().channel;
        const auto bytes = target.size() + prepared.value().channel.size() + prepared.value().title.size() +
                           prepared.value().text.size();
        const auto queued = state->pending.enqueue(std::move(prepared).value(), std::move(source), target, bytes);
        if (!queued.hasValue())
            return Output::failure(queued.error());
    } else if (*decision.value())
        state->admit(std::move(prepared).value());
    else
        return Output::failure(makeError(ErrorCode::ContextClosing, "output publication aborted"));
    state->sequence = sequence;
    detail::presentIncoming(state);
    return Output::success(sequence);
}
Result<std::vector<OutputEntry>> OutputService::snapshot() const
{
    using Output = Result<std::vector<OutputEntry>>;
    auto state = control_.lock();
    if (!state)
        return Output::success({});
    if (!state->scheduler->isCurrentThread())
        return Output::failure(makeError(ErrorCode::WrongThread, "output snapshot requires owner"));
    detail::settleOutput(state, false);
    if (state->open) {
        const auto available = state->pending.status();
        if (!available.hasValue())
            return Output::failure(available.error());
    }
    std::vector<OutputEntry> snapshot;
    for (const auto& row : state->entries)
        snapshot.push_back(row.entry);
    return Output::success(std::move(snapshot));
}
} // namespace cordis::ui
