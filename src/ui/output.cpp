/** @file output.cpp
 * @brief 有界输出记录、精确实例目标及当前通道复制/清空/滚动，不使用通知冒充日志。
 */
#include "output_p.hpp"
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

namespace cordis::ui::detail {
struct OutputControl {
    std::shared_ptr<qt::QtScheduler> scheduler;
    QPointer<QPlainTextEdit> editor;
    QPointer<ElaComboBox> targets;
    QPointer<ElaToolButton> follow;
    WindowAccess access;
    std::deque<OutputEntry> entries;
    std::map<std::string, QString> channels{{"platform", QString::fromUtf8("平台")}};
    std::string selected = "platform";
    std::uint64_t sequence = 0;
    bool open = true, rendering = false, dirty = false;
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
    /** @brief 控件信号允许重入，显示最多 5000 行以限制 QTextDocument 块数。 */
    void render(bool preserve = true)
    {
        if (!open || !editor) return;
        if (rendering) { dirty = true; return; }
        dirty = false;
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
        rendering = true;
        auto guard = access;
        guard([this, visible, previous, scroll, preserve, position, anchor] {
            if (!open || !editor) return;
            editor->setPlainText(visible);
            if (open && editor && preserve && !scroll) {
                auto cursor = editor->textCursor(); cursor.setPosition(std::min(anchor, visible.size()));
                cursor.setPosition(std::min(position, visible.size()), QTextCursor::KeepAnchor); editor->setTextCursor(cursor);
            }
            if (open && editor) editor->verticalScrollBar()->setValue(scroll ? editor->verticalScrollBar()->maximum() : previous);
        });
        rendering = false;
        if (dirty && open) render();
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
Result<std::uint64_t> appendOutput(const std::shared_ptr<OutputControl>& self,
    std::optional<ContextIdentity> owner, std::string channel, std::string text, OutputLevel level)
{
    using Output = Result<std::uint64_t>;
    if (!self->scheduler->isCurrentThread()) return Output::failure(makeError(ErrorCode::WrongThread, "output requires owner thread"));
    if (!self->open || !self->scheduler->isOpen()) return Output::failure(makeError(ErrorCode::ContextClosing, "output closed"));
    if (channel.empty() || channel.size() > 64 || text.empty() ||
        (level != OutputLevel::Info && level != OutputLevel::Warning && level != OutputLevel::Error) ||
        self->sequence == std::numeric_limits<std::uint64_t>::max())
        return Output::failure(makeError(ErrorCode::InvalidArgument, "invalid output record"));
    if (text.size() > OutputService::textLimit) {
        auto end = OutputService::textLimit - 3;
        while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
        text.resize(end); text += "…";
    }
    if (QString::fromUtf8(channel.data(), static_cast<int>(channel.size())).toUtf8().toStdString() != channel ||
        QString::fromUtf8(text.data(), static_cast<int>(text.size())).toUtf8().toStdString() != text ||
        std::any_of(channel.begin(), channel.end(), [](unsigned char byte) { return byte < 0x20 || byte == 0x7f; }))
        return Output::failure(makeError(ErrorCode::InvalidArgument, "output requires UTF-8 text and printable channel"));
    auto id = owner ? "plugin/" + std::to_string(owner->instance.value()) + "/" + std::to_string(owner->epoch) + "/" + channel : "platform";
    const auto title = owner ? QString::fromUtf8("%1 · 实例 #%2 / 代 %3").arg(QString::fromStdString(channel)).arg(owner->instance.value()).arg(owner->epoch) : QString::fromUtf8("平台");
    bool channelsChanged = false, renderNeeded = false;
    if (!self->channels.count(id)) {
        channelsChanged = true;
        if (self->channels.size() >= OutputService::channelLimit) {
            // 淘汰最早写入的非平台目标；不让旧代历史使新代永久无法输出。
            auto oldest = std::find_if(self->entries.begin(), self->entries.end(), [](const auto& entry) { return entry.channel != "platform"; });
            auto victim = oldest == self->entries.end() ? self->channels.begin()->first : oldest->channel;
            if (victim == "platform") {
                auto other = self->channels.begin(); while (other != self->channels.end() && other->first == "platform") ++other;
                victim = other->first;
            }
            self->channels.erase(victim);
            self->entries.erase(std::remove_if(self->entries.begin(), self->entries.end(), [&](const auto& entry) { return entry.channel == victim; }), self->entries.end());
            if (self->selected == victim) { self->selected = "platform"; renderNeeded = true; }
        }
        self->channels.emplace(id, title);
    }
    const auto sequence = ++self->sequence;
    self->entries.push_back({sequence, owner, id, title.toStdString(), std::move(text), level});
    while (self->entries.size() > OutputService::historyLimit) {
        if (self->entries.front().channel == self->selected) renderNeeded = true;
        self->entries.pop_front();
    }
    auto access = self->access;
    if (channelsChanged) access([self] {
        if (!self->open || !self->targets) return;
        QSignalBlocker blocker(self->targets);
        self->targets->clear();
        self->targets->addItem(QString::fromUtf8("平台"), "platform");
        for (const auto& channel : self->channels) if (channel.first != "platform")
            self->targets->addItem(channel.second, QString::fromStdString(channel.first));
        self->targets->setCurrentIndex(self->targets->findData(QString::fromStdString(self->selected)));
    });
    if (renderNeeded || self->selected == id) self->render();
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
            state->selected = state->targets->currentData().toString().toStdString(); state->render(false);
        }
    });
    QObject::connect(follow, &QToolButton::toggled, follow, [weak](bool on) { if (auto state = weak.lock(); on && state) state->render(); });
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
    return self->text();
}
void clearOutput(const std::shared_ptr<OutputControl>& self)
{
    if (!self || !self->scheduler->isCurrentThread() || !self->open) return;
    const auto selected = self->selected;
    self->entries.erase(std::remove_if(self->entries.begin(), self->entries.end(), [&](const auto& entry) { return entry.channel == selected; }), self->entries.end());
    self->render(false);
}
void closeOutput(const std::shared_ptr<OutputControl>& self) { if (self) { self->open = false; self->access = {}; } }
} // namespace cordis::ui::detail

namespace cordis::ui {
Result<std::uint64_t> OutputService::write(const Context& owner, std::string channel, std::string text, OutputLevel level) const
{
    auto state = control_.lock();
    if (!state) return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "output host expired"));
    auto guard = owner.eventGuard(state->scheduler);
    if (!guard.hasValue()) return Result<std::uint64_t>::failure(guard.error());
    if (text.size() > textLimit) return Result<std::uint64_t>::failure(makeError(ErrorCode::InvalidArgument, "output record exceeds text limit"));
    auto identity = owner.identity();
    if (!identity.hasValue()) return Result<std::uint64_t>::failure(identity.error());
    return detail::appendOutput(state, identity.value(), std::move(channel), std::move(text), level);
}
Result<std::vector<OutputEntry>> OutputService::snapshot() const
{
    auto state = control_.lock();
    if (!state) return Result<std::vector<OutputEntry>>::success({});
    if (!state->scheduler->isCurrentThread()) return Result<std::vector<OutputEntry>>::failure(makeError(ErrorCode::WrongThread, "output snapshot requires owner"));
    return Result<std::vector<OutputEntry>>::success({state->entries.begin(), state->entries.end()});
}
} // namespace cordis::ui
