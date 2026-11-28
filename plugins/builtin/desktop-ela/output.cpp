/** @file output.cpp
 * @brief 对外输出服务、发布接纳与有界实时记录。
 */
#include "output_state_p.hpp"
#include "desktop_text.hpp"
#include "output_record_p.hpp"
#include <ElaComboBox.h>
#include <ElaToolButton.h>
#include <QDateTime>
#include <QPlainTextEdit>
#include <QTimer>
#include <eve/module/native.hpp>
#include <eve_abi/plugin.h>
#include <algorithm>
#include <limits>
#include <utility>
namespace eve::ui::detail
{
bool outputMatches(const LogRecord& row, const LogFilter& filter)
{
    return logMatches(row, filter);
}
LogRecord OutputControl::captureQt(OutputEntry entry, const ContextSource& source) const
{
    LogRecord row{std::move(entry), {}, {}, QDateTime::currentMSecsSinceEpoch()};
    row.operation = source.replacement;
    if (const auto registered = qtOrigins.find(source.runtime); registered != qtOrigins.end()) {
        row.sourceName = registered->second.pluginId;
        row.moduleId = registered->second.moduleId;
        row.moduleVersion = registered->second.moduleVersion;
        row.generation = registered->second.generation.value();
    }
    return row;
}
void OutputControl::admit(OutputEntry entry, std::optional<OperationId> notification, OutputDiagnostic diagnostic)
{
    LogRecord row{std::move(entry), {}, {}, QDateTime::currentMSecsSinceEpoch(), 0, notification};
    row.operation = diagnostic.operation;
    row.diagnosticCode = QString::fromStdString(diagnostic.code);
    admit(std::move(row));
}
void OutputControl::admit(LogRecord row)
{
    if (!row.entry.owner) {
        row.sourceId = QStringLiteral("platform");
        row.sourceName = QStringLiteral("Platform");
    } else {
        const auto owner = *row.entry.owner;
        if (row.sourceId.isEmpty()) {
            if (row.sourceName.isEmpty()) {
                const auto known = publishedOwners.find(owner.instance);
                if (known != publishedOwners.end())
                    row.sourceName = known->second;
                else if (resolve) {
                    row.sourceName = resolve(owner.instance);
                    if (!row.sourceName.isEmpty())
                        publishedOwners[owner.instance] = row.sourceName;
                }
            }
            if (row.sourceName.isEmpty()) {
                row.sourceId = QString("instance/%1").arg(owner.instance.value());
                row.sourceName = QString::fromStdString(row.entry.channel).section('/', 3);
            } else {
                row.sourceId = "plugin/" + row.sourceName;
            }
        }
        if (row.instanceKey.isEmpty() && resolveConfiguration)
            row.instanceKey = resolveConfiguration(owner);
    }
    if (!row.eventId)
        row.eventId = archive ? archive->append(row) : row.entry.sequence;
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
void setOutputConfigurationResolver(const std::shared_ptr<OutputControl>& self,
                                    std::function<QString(ContextIdentity)> resolve)
{
    self->resolveConfiguration = std::move(resolve);
}
Result<void> registerQtOutputRuntime(const std::shared_ptr<OutputControl>& self, RuntimeId runtime,
                                     GenerationId generation, const std::string& moduleId,
                                     const std::string& moduleVersion, const std::string& pluginId)
{
    if (!self->scheduler->isCurrentThread())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "output origin requires owner thread"));
    if (!self->open || !self->scheduler->isOpen())
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "output closed"));
    const auto validAtom = [](const std::string& value, std::size_t limit) {
        return !value.empty() && value.size() <= limit && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '.' || character == '_' || character == '-';
        });
    };
    if (!runtime.valid() || !generation.valid() || !validAtom(moduleId, 128) || !validAtom(moduleVersion, 64) ||
        !validAtom(pluginId, 128))
        return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "invalid Qt output origin"));
    const QtOutputOrigin value{generation, QString::fromStdString(moduleId), QString::fromStdString(moduleVersion),
                               QString::fromStdString(pluginId)};
    const auto [entry, inserted] = self->qtOrigins.emplace(runtime, value);
    const auto& existing = entry->second;
    if (!inserted && (existing.generation != value.generation || existing.moduleId != value.moduleId ||
                      existing.moduleVersion != value.moduleVersion || existing.pluginId != value.pluginId))
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "Qt output origin already differs"));
    return Result<void>::success();
}
std::shared_ptr<OutputControl> createOutput(std::shared_ptr<qt::QtScheduler> scheduler, QPlainTextEdit* editor,
                                            WindowAccess access)
{
    auto result = std::make_shared<OutputControl>();
    result->scheduler = std::move(scheduler);
    result->editor = editor;
    result->access = std::move(access);
    editor->setPlaceholderText(eve::desktopText("output.noOutputForThisTargetYetRun"));
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
                                   std::optional<OperationId> notification, OutputDiagnostic diagnostic)
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
    self->admit(std::move(prepared).value(), notification, std::move(diagnostic));
    self->sequence = sequence;
    if (render)
        presentIncoming(self);
    return Output::success(sequence);
}
Result<std::uint64_t> appendNativeOutput(const std::shared_ptr<OutputControl>& self, const module::NativeLogEvent& event)
{
    using Output = Result<std::uint64_t>;
    if (!self->scheduler->isCurrentThread())
        return Output::failure(makeError(ErrorCode::WrongThread, "output requires owner thread"));
    if (!self->open || !self->scheduler->isOpen())
        return Output::failure(makeError(ErrorCode::ContextClosing, "output closed"));
    if (event.record.level > EVE_LOG_ERROR || !event.sourceSequence || event.moduleId.empty() || event.pluginId.empty())
        return Output::failure(makeError(ErrorCode::InvalidArgument, "invalid native output identity or level"));
    settleOutput(self, false);
    if (self->sequence == std::numeric_limits<std::uint64_t>::max())
        return Output::failure(makeError(ErrorCode::InvalidArgument, "output sequence exhausted"));
    const auto severity = static_cast<int>(event.record.level) - static_cast<int>(EVE_LOG_INFO);
    auto sequence = ++self->sequence;
    LogRecord row{};
    row.moduleId = QString::fromStdString(event.moduleId);
    row.moduleVersion = QString::fromStdString(event.moduleVersion);
    row.sourceName = QString::fromStdString(event.pluginId);
    row.sourceId = "native/" + row.moduleId + "/" + row.sourceName;
    row.generation = event.generation.value();
    row.sourceSequence = event.sourceSequence;
    row.operation = event.operation;
    row.utcMs = event.utcMs;
    row.severity = static_cast<LogSeverity>(severity);
    const auto channel = "native/" + std::to_string(event.instance.value()) + "/" + std::to_string(event.epoch) + "/log";
    const auto title = eve::desktopText("output.nativeInstanceEpoch").arg(event.instance.value()).arg(event.epoch);
    row.entry = {sequence, ContextIdentity{event.instance, event.epoch}, channel, title.toStdString(),
                 event.record.text, static_cast<OutputLevel>(std::max(severity, 0))};
    self->admit(std::move(row));
    presentIncoming(self);
    return Output::success(sequence);
}
void closeOutput(const std::shared_ptr<OutputControl>& self)
{
    if (!self)
        return;
    settleOutput(self, false);
    self->pending.clear();
    if (self->archive)
        self->archive->detachOutput(self->sequence);
    self->open = false;
    self->access = {};
}
std::weak_ptr<OutputEndpoint> outputEndpoint(const std::shared_ptr<OutputControl>& control)
{
    return control;
}
} // namespace eve::ui::detail

namespace eve::ui
{
Result<std::uint64_t> detail::OutputControl::writeOutput(const Context& owner, std::string channel, std::string text, OutputLevel level)
{
    using Output = Result<std::uint64_t>;
    auto state = shared_from_this();
    const auto guard = owner.eventGuard(state->scheduler);
    if (!guard.hasValue())
        return Output::failure(guard.error());
    if (!state->open)
        return Output::failure(makeError(ErrorCode::ContextClosing, "output closed"));
    detail::settleOutput(state, false);
    const auto available = state->pending.status();
    if (!available.hasValue())
        return Output::failure(available.error());
    const auto contextSource = owner.source();
    if (!contextSource.hasValue())
        return Output::failure(contextSource.error());
    const ContextIdentity identity{contextSource.value().instance, contextSource.value().epoch};
    auto prepared = detail::prepareOutput(identity, std::move(channel), std::move(text), level, true);
    if (!prepared.hasValue())
        return Output::failure(prepared.error());
    if (state->sequence == std::numeric_limits<std::uint64_t>::max())
        return Output::failure(makeError(ErrorCode::InvalidArgument, "output sequence exhausted"));
    auto row = state->captureQt(std::move(prepared).value(), contextSource.value());
    auto source = state->pending.source(identity);
    if (!source) {
        source = std::make_shared<detail::PendingSource>(identity);
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
    const auto nextSequence = state->sequence + 1;
    row.entry.sequence = nextSequence;
    row.sourceSequence = nextSequence;
    if (!decision.value().has_value()) {
        const auto target = row.entry.channel;
        const auto metadataBytes = static_cast<std::size_t>(row.sourceName.size() + row.moduleId.size() +
                                                            row.moduleVersion.size()) * sizeof(QChar);
        const auto bytes = target.size() + row.entry.channel.size() + row.entry.title.size() + row.entry.text.size() +
                           metadataBytes;
        const auto queued = state->pending.enqueue(std::move(row), std::move(source), target, bytes);
        if (!queued.hasValue())
            return Output::failure(queued.error());
    } else if (*decision.value())
        state->admit(std::move(row));
    else
        return Output::failure(makeError(ErrorCode::ContextClosing, "output publication aborted"));
    state->sequence = nextSequence;
    detail::presentIncoming(state);
    return Output::success(nextSequence);
}
Result<std::vector<OutputEntry>> detail::OutputControl::outputSnapshot()
{
    using Output = Result<std::vector<OutputEntry>>;
    auto state = shared_from_this();
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
} // namespace eve::ui
