/** @file output.cpp
 * @brief 输出准备、原发布组候选值、纯历史接纳与可重入呈现各自承担单一职责。
 */
#include "output_p.hpp"
#include "pending_values_p.hpp"
#include <ElaComboBox.h>
#include <ElaToolButton.h>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabWidget>
#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <utility>

namespace cordis::ui::detail {
namespace {
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
struct OutputControl {
    std::shared_ptr<qt::QtScheduler> scheduler;
    QPointer<QPlainTextEdit> editor;
    QPointer<ElaComboBox> targets;
    QPointer<ElaToolButton> follow;
    WindowAccess access;
    std::deque<OutputEntry> entries;
    std::map<std::string, QString> channels{{"platform", QString::fromUtf8("平台")}};
    PendingValues<OutputEntry> pending{OutputService::pendingCountLimit, OutputService::pendingByteLimit,
                                       OutputService::pendingTargetLimit};
    std::string selected = "platform";
    std::uint64_t sequence = 0;
    bool open = true, presenting = false, channelsDirty = false, textDirty = false, preserveText = true;
    /** @brief 所有来源共用纯历史内核；晚到值保留原号，淘汰不进入控件或业务回调。 */
    void admit(OutputEntry entry)
    {
        const auto id = entry.channel;
        const auto title = QString::fromStdString(entry.title);
        const auto at =
            std::lower_bound(entries.begin(), entries.end(), entry.sequence,
                             [](const auto& row, std::uint64_t sequence) { return row.sequence < sequence; });
        entries.insert(at, std::move(entry));
        if (!channels.count(id)) {
            channels.emplace(id, title);
            channelsDirty = true;
        }
        if (selected == id)
            textDirty = true;
        while (channels.size() > OutputService::channelLimit) {
            const auto oldest =
                std::find_if(entries.begin(), entries.end(), [](const auto& row) { return row.channel != "platform"; });
            const auto fallback =
                std::find_if(channels.begin(), channels.end(), [](const auto& row) { return row.first != "platform"; });
            const auto victim = oldest == entries.end() ? fallback->first : oldest->channel;
            channels.erase(victim);
            entries.erase(
                std::remove_if(entries.begin(), entries.end(), [&](const auto& row) { return row.channel == victim; }),
                entries.end());
            if (selected == victim) {
                selected = "platform";
                textDirty = true;
            }
            channelsDirty = true;
        }
        while (entries.size() > OutputService::historyLimit) {
            if (entries.front().channel == selected)
                textDirty = true;
            entries.pop_front();
        }
    }
    /** @brief 完整文本用于复制，记录由 historyLimit/textLimit 限定总大小。 */
    QString text() const
    {
        QStringList lines;
        for (const auto& entry : entries) if (entry.channel == selected) {
            const auto level = entry.level == OutputLevel::Error ? "ERROR" : entry.level == OutputLevel::Warning ? "WARN" : "INFO";
            lines.push_back(QString("[%1] [%2] %3").arg(entry.sequence).arg(QString::fromLatin1(level), QString::fromStdString(entry.text)));
        }
        return lines.join('\n');
    }
    /** @brief 仅由刷新窗口访问栈调用；不跨同步信号借用历史容器元素。 */
    void renderText(bool preserve)
    {
        if (!open || !editor) return;
        auto visible = text();
        int offset = visible.size();
        for (int line = 0; line < 5000; ++line) {
            offset = visible.lastIndexOf('\n', offset - 1);
            if (offset < 0) break;
        }
        if (offset >= 0) visible = visible.mid(offset + 1);
        const auto previous = editor->verticalScrollBar()->value();
        const auto position = editor->textCursor().position(), anchor = editor->textCursor().anchor();
        const bool scroll = !follow || follow->isChecked();
        editor->setPlainText(visible);
        if (open && editor && preserve && !scroll) {
            auto cursor = editor->textCursor();
            cursor.setPosition(std::min(anchor, visible.size()));
            cursor.setPosition(std::min(position, visible.size()), QTextCursor::KeepAnchor);
            editor->setTextCursor(cursor);
        }
        if (open && editor)
            editor->verticalScrollBar()->setValue(scroll ? editor->verticalScrollBar()->maximum() : previous);
    }
};
std::shared_ptr<OutputControl> createOutput(std::shared_ptr<qt::QtScheduler> scheduler,
                                           QPlainTextEdit* editor, WindowAccess access)
{
    auto result = std::make_shared<OutputControl>();
    result->scheduler = std::move(scheduler); result->editor = editor; result->access = std::move(access);
    editor->setPlaceholderText(QString::fromUtf8("当前目标暂无输出。运行命令或插件操作后会在这里显示真实记录。"));
    return result;
}
void refreshOutput(const std::shared_ptr<OutputControl>& self)
{
    if (!self || !self->scheduler->isCurrentThread() || !self->open || !self->access || self->presenting)
        return;
    self->presenting = true;
    while (self->open && (self->channelsDirty || self->textDirty)) {
        const bool updateChannels = std::exchange(self->channelsDirty, false);
        const bool updateText = std::exchange(self->textDirty, false);
        const bool preserve = std::exchange(self->preserveText, true);
        // 模型/text 信号可重入接纳；只跨信号保留本次复制的投影。
        const auto channels = updateChannels ? self->channels : std::map<std::string, QString>{};
        const auto selected = self->selected;
        auto access = self->access;
        access([self, updateChannels, updateText, preserve, channels, selected] {
            if (!self->open)
                return;
            if (updateChannels && self->targets) {
                QSignalBlocker blocker(self->targets);
                self->targets->clear();
                if (!self->open || !self->targets)
                    return;
                self->targets->addItem(QString::fromUtf8("平台"), "platform");
                for (const auto& channel : channels) {
                    if (!self->open || !self->targets)
                        return;
                    if (channel.first != "platform")
                        self->targets->addItem(channel.second, QString::fromStdString(channel.first));
                }
                if (self->open && self->targets)
                    self->targets->setCurrentIndex(self->targets->findData(QString::fromStdString(selected)));
            }
            if (self->open && updateText)
                self->renderText(preserve);
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
Result<std::uint64_t> appendOutput(const std::shared_ptr<OutputControl>& self, std::optional<ContextIdentity> owner,
                                   std::string channel, std::string text, OutputLevel level, bool render)
{
    using Output = Result<std::uint64_t>;
    if (!self->scheduler->isCurrentThread()) return Output::failure(makeError(ErrorCode::WrongThread, "output requires owner thread"));
    if (!self->open || !self->scheduler->isOpen()) return Output::failure(makeError(ErrorCode::ContextClosing, "output closed"));
    settleOutput(self, false);
    auto prepared = prepareOutput(owner, std::move(channel), std::move(text), level, false);
    if (!prepared.hasValue())
        return Output::failure(prepared.error());
    if (self->sequence == std::numeric_limits<std::uint64_t>::max())
        return Output::failure(makeError(ErrorCode::InvalidArgument, "output sequence exhausted"));
    const auto sequence = self->sequence + 1;
    prepared.value().sequence = sequence;
    self->admit(std::move(prepared).value());
    self->sequence = sequence;
    if (render)
        refreshOutput(self);
    return Output::success(sequence);
}
void installOutputTools(const std::shared_ptr<OutputControl>& self, QTabWidget* panel)
{
    auto tools = panel->findChild<QWidget*>("CordisPanelTools");
    auto layout = tools ? qobject_cast<QHBoxLayout*>(tools->layout()) : nullptr;
    if (!layout) return;
    auto target = new ElaComboBox(tools); self->targets = target;
    target->setObjectName("CordisOutputTarget"); target->setAccessibleName(QString::fromUtf8("输出目标"));
    target->setFixedWidth(220); target->setFixedHeight(28); target->addItem(QString::fromUtf8("平台"), "platform");
    layout->insertWidget(0, target);
    auto follow = new ElaToolButton(tools); self->follow = follow;
    follow->setObjectName("CordisOutputFollow"); follow->setElaIcon(ElaIconType::ArrowDown);
    follow->setCheckable(true); follow->setChecked(true); follow->setFixedSize(28, 28);
    follow->setToolTip(QString::fromUtf8("自动滚动到最新输出")); follow->setAccessibleName(follow->toolTip());
    layout->insertWidget(1, follow);
    std::weak_ptr<OutputControl> weak = self;
    QObject::connect(target, QOverload<int>::of(&QComboBox::currentIndexChanged), target, [weak](int) {
        if (auto state = weak.lock(); state && state->open && state->targets) {
            const auto requested = state->targets->currentData().toString().toStdString();
            settleOutput(state, false);
            state->selected = state->channels.count(requested) ? requested : "platform";
            state->textDirty = true;
            state->preserveText = false;
            refreshOutput(state);
        }
    });
    QObject::connect(follow, &QToolButton::toggled, follow, [weak](bool on) {
        if (auto state = weak.lock(); on && state) {
            settleOutput(state, false);
            state->textDirty = true;
            refreshOutput(state);
        }
    });
    const auto visible = [weak, panel](int) {
        if (auto state = weak.lock(); state && state->open) {
            const bool show = panel->currentWidget() == state->editor;
            if (state->targets) state->targets->setVisible(show);
            if (state->follow) state->follow->setVisible(show);
        }
    };
    QObject::connect(panel, &QTabWidget::currentChanged, target, visible); visible(panel->currentIndex());
}
QString currentOutputText(const std::shared_ptr<OutputControl>& self)
{
    if (!self || !self->scheduler->isCurrentThread() || !self->open) return {};
    settleOutput(self, false);
    return self->text();
}
void clearOutput(const std::shared_ptr<OutputControl>& self)
{
    if (!self || !self->scheduler->isCurrentThread() || !self->open) return;
    const auto selected = self->selected;
    settleOutput(self, false);
    self->entries.erase(std::remove_if(self->entries.begin(), self->entries.end(), [&](const auto& entry) { return entry.channel == selected; }), self->entries.end());
    self->textDirty = true;
    self->preserveText = false;
    refreshOutput(self);
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

namespace cordis::ui {
Result<std::uint64_t> OutputService::write(const Context& owner, std::string channel, std::string text, OutputLevel level) const
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
    detail::refreshOutput(state);
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
    return Output::success({state->entries.begin(), state->entries.end()});
}
} // namespace cordis::ui
