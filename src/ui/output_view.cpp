/** @file output_view.cpp
 * @brief 输出运行与来源选择、分页、增量绘制、焦点和筛选结果。
 */
#include "output_state_p.hpp"
#include <ElaLineEdit.h>
#include <ElaMenu.h>
#include <ElaText.h>
#include <ElaPlainTextEdit.h>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSaveFile>
#include <QShortcut>
#include <QUrl>
#include <QWidgetAction>
#include <QToolTip>
#include <QLabel>
#include <QResizeEvent>
#include <QToolButton>
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
#include <limits>
#include <utility>

namespace cordis::ui::detail
{
namespace
{
// Keep the output editor as the actual tab so existing panel navigation and its splitter remain stable.
class OutputEditor final : public ElaPlainTextEdit
{
  public:
    explicit OutputEditor(QWidget* parent) : ElaPlainTextEdit(parent) {}
    QWidget* toolbar = nullptr;
    QWidget* run = nullptr;
    QWidget* search = nullptr;
    QWidget* level = nullptr;
    QWidget* instance = nullptr;
    void fitTools()
    {
        if (!toolbar)
            return;
        const bool wide = width() >= 1080, medium = width() >= 760;
        auto focus = QApplication::focusWidget();
        const bool moveFocus =
            (!wide && (focus == search || focus == instance)) || (!medium && (focus == run || focus == level));
        run->setVisible(medium);
        search->setVisible(wide);
        level->setVisible(medium);
        instance->setVisible(wide);
        for(const char* id:{"CordisCopyPanelOutput","CordisClearPanelOutput"})
            if(auto button=window()->findChild<QToolButton*>(id)) button->setVisible(wide && isVisible());
        const int h = toolbar->sizeHint().height();
        setViewportMargins(0, h, 0, 0);
        toolbar->setGeometry(0, 0, width(), h);
        if (moveFocus)
            if (auto more = toolbar->findChild<QToolButton*>("CordisOutputMore"))
                more->setFocus(Qt::OtherFocusReason);
    }

  protected:
    void showEvent(QShowEvent* event) override
    {
        ElaPlainTextEdit::showEvent(event);
        fitTools();
    }
    void resizeEvent(QResizeEvent* event) override
    {
        ElaPlainTextEdit::resizeEvent(event);
        fitTools();
    }
};

bool exportWouldModifyArchive(const QString& path, const QString& logs)
{
    const QFileInfo file(path), root(logs);
#ifdef Q_OS_WIN
    const auto sensitivity = Qt::CaseInsensitive;
#else
    const auto sensitivity = Qt::CaseSensitive;
#endif
    const auto isWithin = [&](const QString& candidate, const QString& directory) {
        if (candidate.isEmpty() || directory.isEmpty())
            return false;
        const auto absolute = QDir::cleanPath(candidate);
        const auto rootPath = QDir::cleanPath(directory);
        return absolute.compare(rootPath, sensitivity) == 0 || absolute.startsWith(rootPath + '/', sensitivity);
    };
    const auto canonicalParent = file.absoluteDir().canonicalPath();
    return isWithin(file.absoluteFilePath(), root.absoluteFilePath()) ||
           isWithin(file.canonicalFilePath(), root.canonicalFilePath()) ||
           (!canonicalParent.isEmpty() && isWithin(canonicalParent + '/' + file.fileName(), root.canonicalFilePath()));
}
} // namespace
QPlainTextEdit* createOutputEditor(QWidget* parent) { return new OutputEditor(parent); }

void OutputControl::readHistory(bool older)
{
    if (!older) {
        history.clear();
        olderCursor.reset();
        notice.clear();
        browsingOlder = false;
    }
    if (!archive)
        return;
    if (live() && !older &&
        std::any_of(entries.begin(), entries.end(), [&](const auto& row) { return outputMatches(row, filter); }))
        return;
    quint64 before = 0;
    if (live() && !olderCursor && history.empty() && !entries.empty()) {
        before = std::numeric_limits<quint64>::max();
        for (const auto& row : entries)
            if (row.eventId)
                before = std::min(before, row.eventId);
    }
    const LogQuery query(*archive);
    const auto page = query.page(olderCursor ? *olderCursor : query.begin(run, filter, before));
    olderCursor = page.older;
    notice = !page.warning.isEmpty() ? page.warning
             : !page.expired.empty() ? QString::fromUtf8("所选范围的一部分日志已清理。")
             : !page.missing.empty() || page.missingIncomplete ? QString::fromUtf8("所选范围有缺失记录。")
              : QString{};
    history.insert(history.begin(), page.rows.begin(), page.rows.end());
    if (older && !page.rows.empty()) {
        browsingOlder = true;
        if (history.size() > OutputService::historyLimit)
            history.resize(OutputService::historyLimit);
    }
}
QString OutputControl::lineFor(const LogRecord& row, bool memory)
{
    if (!memory)
        return formatLogText(row);
    auto [it, added] = liveLines.try_emplace(row.entry.sequence);
    if (added)
        it->second = formatLogText(row);
    return it->second;
}
QString OutputControl::text(std::vector<VisibleRecord>* positions)
{
    QStringList lines;
    int position = 0;
    auto append = [&](const LogRecord& row, bool memory) {
        if (!outputMatches(row, filter))
            return;
        if (live() && cleared.count(row.entry.sequence))
            return;
        const QString line = lineFor(row, memory);
        if (positions)
            positions->push_back({position, position + line.size(), row.entry.sequence, row.eventId, line});
        position += line.size() + 1;
        lines.push_back(line);
    };
    for (const auto& row : history)
        append(row, false);
    if (live() && !browsingOlder)
        for (const auto& row : entries)
            append(row, true);
    return lines.join('\n');
}
void OutputControl::renderText(bool preserve, bool rebuild, const std::vector<LogRecord>& added)
{
    if (!open || !editor)
        return;
    const QString warning = !notice.isEmpty() ? notice : archive ? archive->error() : QString{};
    if (feedback) {
        const auto message = warning.isEmpty() && unread ? QString::fromUtf8("%1 条新记录").arg(unread) : warning;
        feedback->setText(message);
        feedback->setToolTip(message);
        feedback->setVisible(!message.isEmpty());
    }
    editor->setPlaceholderText(warning.isEmpty() ? QString::fromUtf8("没有匹配的记录。可更换运行或清除筛选。")
                                                 : warning);
    if (!rebuild) {
        QStringList lines;
        int blocks = editor->document()->blockCount();
        for (const auto& row : added) {
            lines.push_back(lineFor(row, true));
            blocks += 1 + lines.back().count('\n');
        }
        if (blocks <= 5000) {
            const bool hasRows = !visibleRecords.empty();
            int position = hasRows ? visibleRecords.back().end + 1 : 0;
            for (int i = 0; i < lines.size(); ++i) {
                visibleRecords.push_back({position, position + lines[i].size(), added[std::size_t(i)].entry.sequence,
                                          added[std::size_t(i)].eventId, lines[i]});
                position += lines[i].size() + 1;
            }
            const auto old = editor->verticalScrollBar()->value();
            const bool scroll = (!follow || follow->isChecked()) && !editor->textCursor().hasSelection();
            const int anchor = editor->textCursor().anchor(), cursorPosition = editor->textCursor().position();
            QTextCursor append(editor->document());
            append.movePosition(QTextCursor::End);
            append.insertText((hasRows ? QStringLiteral("\n") : QString{}) + lines.join('\n'));
            if (open && editor && !scroll) {
                auto cursor = editor->textCursor();
                cursor.setPosition(anchor);
                cursor.setPosition(cursorPosition, QTextCursor::KeepAnchor);
                editor->setTextCursor(cursor);
            }
            if (open && editor)
                editor->verticalScrollBar()->setValue(scroll ? editor->verticalScrollBar()->maximum() : old);
            return;
        }
    }
    const auto cursorLocation = [&](int position) {
        for (const auto& row : visibleRecords)
            if (row.start <= position && position <= row.end)
                return std::make_pair(row.sequence, position - row.start);
        return std::make_pair(std::uint64_t{0}, 0);
    };
    const auto oldAnchor = cursorLocation(editor->textCursor().anchor());
    const auto oldPosition = cursorLocation(editor->textCursor().position());
    visibleRecords.clear();
    auto visible = text(&visibleRecords);
    if (visible.count('\n') >= 5000) {
        const auto length = visible.size();
        visible = visible.section('\n', -5000);
        const auto offset = length - visible.size();
        visibleRecords.erase(std::remove_if(visibleRecords.begin(), visibleRecords.end(),
                                            [&](const auto& record) { return record.end < offset; }),
                             visibleRecords.end());
        for (auto& record : visibleRecords) {
            record.start = std::max(0, record.start - offset);
            record.end -= offset;
        }
    }
    const auto old = editor->toPlainText();
    if (old == visible)
        return;
    const auto prior = editor->verticalScrollBar()->value();
    auto cur = editor->textCursor();
    const bool scroll = (!follow || follow->isChecked()) && !cur.hasSelection();
    if (preserve && !old.isEmpty() && visible.startsWith(old + "\n")) {
        QTextCursor append(editor->document());
        append.movePosition(QTextCursor::End);
        append.insertText(visible.mid(old.size()));
    } else {
        editor->setPlainText(visible);
    }
    if (open && editor && preserve && !scroll) {
        const auto locate = [&](const auto& loc) {
            for (const auto& row : visibleRecords)
                if (row.sequence == loc.first)
                    return std::min(row.end, row.start + loc.second);
            return 0;
        };
        cur = editor->textCursor();
        cur.setPosition(locate(oldAnchor));
        cur.setPosition(locate(oldPosition), QTextCursor::KeepAnchor);
        editor->setTextCursor(cur);
    }
    if (open && editor)
        editor->verticalScrollBar()->setValue(scroll ? editor->verticalScrollBar()->maximum() : prior);
}
namespace
{
using Choices = std::vector<std::pair<QString, QString>>; // key, display label
void syncCombo(QComboBox* combo, const Choices& choices, const QString& selected)
{
    if (!combo)
        return;
    QSignalBlocker block(combo);
    for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
        const auto& choice = choices[static_cast<std::size_t>(i)];
        if (i >= combo->count())
            combo->addItem(choice.second, choice.first);
        else {
            if (combo->itemData(i).toString() != choice.first)
                combo->setItemData(i, choice.first);
            if (combo->itemText(i) != choice.second)
                combo->setItemText(i, choice.second);
        }
    }
    while (combo->count() > static_cast<int>(choices.size()))
        combo->removeItem(combo->count() - 1);
    combo->setCurrentIndex(combo->findData(selected));
}
QString instanceKey(const LogFilter& filter)
{
    if (!filter.instanceKey.isEmpty())
        return "config/" + filter.instanceKey;
    if (!filter.instance)
        return {};
    auto key = QString::number(filter.instance->value());
    if (filter.epoch)
        key += "/" + QString::number(*filter.epoch);
    return key;
}
void rebuildFilter(const std::shared_ptr<OutputControl>& state, bool updateSources = true)
{
    state->unread = 0;
    state->readHistory(false);
    state->channelsDirty |= updateSources || !state->live();
    state->textDirty = true;
    state->preserveText = false;
    refreshOutput(state);
}
void syncRuns(OutputControl& state)
{
    Choices runs;
    if (state.archive)
        for (const auto& run : state.archive->runs())
            runs.emplace_back(run.id, run.id == state.archive->currentRun()
                                          ? QString::fromUtf8("本次运行 · ") + run.label
                                          : run.label);
    if (runs.empty())
        runs.emplace_back(state.run, QString::fromUtf8("本次运行"));
    syncCombo(state.runs, runs, state.run);
}
void clearFilterControls(OutputControl& state)
{
    state.filter = {};
    QSignalBlocker search(state.search), level(state.levels);
    state.search->clear();
    state.levels->setCurrentIndex(0);
}
void returnFromOutput(OutputControl& state, bool returnToPanelPage)
{
    auto page = returnToPanelPage ? state.originPage : QPointer<QWidget>{};
    state.originPage.clear();
    if (page)
        state.panel->setCurrentWidget(page);
    else
        state.panel->hide();
    state.origin.restore(page ? page->findChild<QWidget*>("CordisProblemsTree") : nullptr);
}
} // namespace
void OutputControl::syncSources()
{
    Choices sources{{QString{}, QString::fromUtf8("全部来源")}};
    const auto catalog = archive ? archive->sourceCatalog(run) : liveSources;
    std::map<QString, QString> owners;
    for (auto it = catalog.cbegin(); it != catalog.cend(); ++it) {
        sources.emplace_back(it.key(), it->name);
        if (!filter.sourceId.isEmpty() && it.key() != filter.sourceId)
            continue;
        for (const auto& [instance, epoch] : it->identities) {
            const auto id = QString::number(instance.value());
            owners[id] = QString::fromUtf8("实例 #") + id;
            owners[id + "/" + QString::number(epoch)] = QString::fromUtf8("#%1 · 代 %2").arg(id).arg(epoch);
        }
        for (const auto& key : it->instanceKeys)
            owners["config/" + key] = QString::fromUtf8("配置实例 ") + key;
    }
    if (!filter.sourceId.isEmpty() && !catalog.contains(filter.sourceId))
        sources.emplace_back(filter.sourceId, filter.sourceId.section('/', 1));
    syncCombo(targets, sources, filter.sourceId);
    Choices identities{{QString{}, QString::fromUtf8("全部实例")}};
    const auto key = instanceKey(filter);
    if (!key.isEmpty() && !owners.count(key))
        owners[key] = filter.instanceKey.isEmpty() ? "#" + key : QString::fromUtf8("配置实例 ") + filter.instanceKey;
    identities.insert(identities.end(), owners.begin(), owners.end());
    syncCombo(instances, identities, key);
}
void setOutputSourceResolver(const std::shared_ptr<OutputControl>& state, std::function<QString(InstanceId)> resolve)
{
    state->resolve = std::move(resolve);
}
void setOutputArchive(const std::shared_ptr<OutputControl>& state, std::shared_ptr<RuntimeLogArchive> archive)
{
    if (!state || !state->open || state->archive || !archive)
        return;
    state->archive = std::move(archive);
    state->run = state->archive->currentRun();
    for (auto& row : state->entries)
        row.eventId = state->archive->append(row);
    state->liveSources.clear();
    syncRuns(*state);
    rebuildFilter(state);
}
void installOutputTools(const std::shared_ptr<OutputControl>& self, QTabWidget* panel)
{
    self->panel = panel;
    auto editor = static_cast<OutputEditor*>(self->editor.data());
    auto tools = new QWidget(editor);
    editor->toolbar = tools;
    auto layout = new QHBoxLayout(tools);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(4);
    const auto combo = [&](const char* id, const QString& name, int maximum) {
        auto box = new ElaComboBox(tools);
        box->setObjectName(id);
        box->setAccessibleName(name);
        box->setToolTip(name);
        box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        box->setMinimumWidth(62);
        box->setMaximumWidth(maximum);
        box->setFixedHeight(28);
        layout->addWidget(box, 1);
        return box;
    };
    self->runs = combo("CordisOutputRun", QString::fromUtf8("运行记录"), 210);
    editor->run = self->runs;
    self->targets = combo("CordisOutputTarget", QString::fromUtf8("输出来源"), 190);
    self->instances = combo("CordisOutputInstance", QString::fromUtf8("实例或代次"), 135);
    editor->instance = self->instances;
    self->levels = combo("CordisOutputLevel", QString::fromUtf8("最低显示级别"), 108);
    editor->level = self->levels;
    self->levels->addItem(QString::fromUtf8("所有级别"));
    self->levels->addItem(QString::fromUtf8("调试及以上"), static_cast<int>(LogSeverity::Debug));
    self->levels->addItem(QString::fromUtf8("信息及以上"), static_cast<int>(LogSeverity::Info));
    self->levels->addItem(QString::fromUtf8("警告及以上"), static_cast<int>(LogSeverity::Warning));
    self->levels->addItem(QString::fromUtf8("仅错误"), static_cast<int>(LogSeverity::Error));
    self->search = new ElaLineEdit(tools);
    editor->search = self->search;
    self->search->setObjectName("CordisOutputSearch");
    self->search->setPlaceholderText(QString::fromUtf8("筛选日志正文"));
    self->search->setAccessibleName(self->search->placeholderText());
    self->search->setClearButtonEnabled(true);
    self->search->setMinimumWidth(50);
    self->search->setMaximumWidth(250);
    layout->addWidget(self->search, 1);
    self->follow = new ElaToolButton(tools);
    self->follow->setObjectName("CordisOutputFollow");
    self->follow->setElaIcon(ElaIconType::ArrowDown);
    self->follow->setCheckable(true);
    self->follow->setChecked(true);
    self->follow->setAccessibleName(QString::fromUtf8("跟随最新记录"));
    self->follow->setToolTip(self->follow->accessibleName());
    layout->addWidget(self->follow);
    auto feedback = new ElaText(QString{}, 12, tools);
    feedback->setWordWrap(false);
    self->feedback = feedback;
    self->feedback->setTextFormat(Qt::PlainText);
    self->feedback->setMaximumWidth(160);
    self->feedback->setObjectName("CordisOutputFeedback");
    self->feedback->hide();
    layout->addWidget(self->feedback);
    auto more = new ElaToolButton(tools);
    more->setObjectName("CordisOutputMore");
    more->setText(QString::fromUtf8("筛选与更多"));
    more->setAccessibleName(more->text());
    more->setPopupMode(QToolButton::InstantPopup);
    layout->addWidget(more);
    auto menu = new ElaMenu(more);
    more->setMenu(menu);
    layout->addStretch();
    std::weak_ptr<OutputControl> weak = self;
    QObject::connect(self->runs, QOverload<int>::of(&QComboBox::currentIndexChanged), tools, [weak](int) {
        if (auto s = weak.lock(); s && s->open) {
            s->run = s->runs->currentData().toString();
            rebuildFilter(s);
        }
    });
    QObject::connect(self->targets, QOverload<int>::of(&QComboBox::currentIndexChanged), tools, [weak](int) {
        if (auto s = weak.lock(); s && s->open) {
            s->filter.sourceId = s->targets->currentData().toString();
            s->filter.instance.reset();
            s->filter.epoch.reset();
            s->filter.instanceKey.clear();
            s->editor->window()->setProperty("CordisOutputSource", s->filter.sourceId);
            rebuildFilter(s);
        }
    });
    QObject::connect(self->instances, QOverload<int>::of(&QComboBox::currentIndexChanged), tools, [weak](int) {
        if (auto s = weak.lock(); s && s->open) {
            const auto id = s->instances->currentData().toString();
            s->filter.instance.reset();
            s->filter.epoch.reset();
            s->filter.instanceKey.clear();
            if (id.startsWith("config/")) {
                s->filter.instanceKey = id.mid(7);
            } else if (!id.isEmpty()) {
                s->filter.instance = InstanceId{id.section('/', 0, 0).toULongLong()};
                if (id.contains('/'))
                    s->filter.epoch = id.section('/', 1, 1).toULongLong();
            }
            rebuildFilter(s);
        }
    });
    QObject::connect(self->levels, QOverload<int>::of(&QComboBox::currentIndexChanged), tools, [weak](int index) {
        if (auto s = weak.lock(); s && s->open) {
            const auto selected = s->levels->itemData(index);
            s->filter.minimumSeverity = selected.isValid()
                ? std::optional<LogSeverity>{static_cast<LogSeverity>(selected.toInt())} : std::nullopt;
            rebuildFilter(s, false);
        }
    });
    QObject::connect(self->search, &QLineEdit::textChanged, tools, [weak](const QString& text) {
        if (auto s = weak.lock(); s && s->open) {
            s->filter.text = text;
            rebuildFilter(s, false);
        }
    });
    QObject::connect(self->follow, &QToolButton::toggled, tools, [weak](bool on) {
        if (auto s = weak.lock(); on && s && s->open) {
            if (s->browsingOlder)
                s->readHistory(false);
            s->unread = 0;
            s->textDirty = true;
            refreshOutput(s);
            s->editor->verticalScrollBar()->setValue(s->editor->verticalScrollBar()->maximum());
        }
    });
    // Menus let the compact toolbar edit the exact same controls and selections as the wide view.
    QObject::connect(menu, &QMenu::aboutToShow, menu, [weak, menu] {
        const auto s = weak.lock();
        if (!s || !s->open)
            return;
        menu->clear();
        for (QComboBox* box : {s->runs.data(), s->instances.data(), s->levels.data()}) {
            auto choices = menu->addMenu(box->accessibleName());
            for (int i = 0; i < box->count(); ++i) {
                auto a = choices->addAction(box->itemText(i));
                a->setCheckable(true);
                a->setChecked(i == box->currentIndex());
                const auto data = box->itemData(i);
                const auto label = box->itemText(i);
                QObject::connect(a, &QAction::triggered, box, [box, data, label] {
                    box->setCurrentIndex(data.isValid() ? box->findData(data) : box->findText(label));
                });
            }
        }
        auto edit = new ElaLineEdit(menu);
        edit->setPlaceholderText(s->search->placeholderText());
        edit->setAccessibleName(edit->placeholderText());
        edit->setText(s->filter.text);
        auto field = new QWidgetAction(menu);
        field->setDefaultWidget(edit);
        menu->addAction(field);
        QObject::connect(edit, &QLineEdit::textChanged, s->search, &QLineEdit::setText);
        menu->addAction(QString::fromUtf8("清除筛选"), [s] {
            clearFilterControls(*s);
            s->editor->window()->setProperty("CordisOutputSource", QString{});
            rebuildFilter(s);
        });
        menu->addAction(QString::fromUtf8("查看上次运行"), [s] { showPreviousOutput(s, s->origin.target()); });
        auto older = menu->addAction(QString::fromUtf8("读取前一页"), [s] {
            s->readHistory(true);
            if (s->browsingOlder)
                s->follow->setChecked(false);
            s->channelsDirty = s->textDirty = true;
            refreshOutput(s);
            s->editor->verticalScrollBar()->setValue(s->editor->verticalScrollBar()->minimum());
        });
        older->setEnabled(s->archive && (s->olderCursor || (s->live() && s->history.empty() && !s->entries.empty())));
        auto newest = menu->addAction(QString::fromUtf8("返回最新一页"), [s] { rebuildFilter(s); });
        newest->setEnabled(s->browsingOlder);
        menu->addSeparator();
        for(const char* id:{"CordisCopyOutput","CordisClearOutput"})
            if(auto action=s->editor->window()->findChild<QAction*>(id)) menu->addAction(action);
        menu->addAction(QString::fromUtf8("复制本条"), [s] {
            const auto position = s->editor->textCursor().position();
            for (const auto& row : s->visibleRecords)
                if (position >= row.start && position <= row.end) {
                    QApplication::clipboard()->setText(QString("[%1 / event %2] ")
                                                           .arg(s->run.isEmpty() ? QStringLiteral("session") : s->run)
                                                           .arg(row.eventId) +
                                                       row.line);
                    break;
                }
        });
        menu->addAction(QString::fromUtf8("导出已筛选记录…"), [s] {
            const QString run = s->run;
            const LogFilter filter = s->filter;
            const auto archive = s->archive;
            const auto cursor = archive ? std::optional<LogCursor>(LogQuery(*archive).begin(run, filter)) : std::nullopt;
            const auto memory = archive ? QString{} : s->text();
            const auto path =
                QFileDialog::getSaveFileName(s->editor, QString::fromUtf8("导出已筛选记录"),
                                             QStringLiteral("cordis-output.txt"), QStringLiteral("Text (*.txt)"));
            if (path.isEmpty() || !s->open)
                return;
            if (archive && exportWouldModifyArchive(path, archive->directory())) {
                s->notice = QString::fromUtf8("请选择归档目录之外的导出位置。");
                QToolTip::showText(s->editor->mapToGlobal(QPoint(8, 4)), s->notice, s->editor);
                return;
            }
            QSaveFile file(path);
            bool ok = file.open(QIODevice::WriteOnly);
            QString exportError;
            if (ok && cursor)
                ok = LogQuery(*archive).exportText(*cursor, file, exportError);
            else if (ok) {
                const auto bytes = memory.toUtf8();
                ok = file.write(bytes) == bytes.size();
            }
            if (ok)
                ok = file.commit();
            if (!ok) {
                file.cancelWriting();
                s->notice = QString::fromUtf8("日志导出失败：") +
                            (exportError.isEmpty() ? file.errorString() : exportError);
            }
            QToolTip::showText(s->editor->mapToGlobal(QPoint(8, 4)),
                               ok ? QString::fromUtf8("已导出筛选结果到 ") + path : s->notice, s->editor);
        });
    });
    self->filter.sourceId = editor->window()->property("CordisOutputSource").toString();
    syncRuns(*self);
    self->syncSources();
    editor->fitTools();
    auto escape = new QShortcut(QKeySequence(Qt::Key_Escape), panel);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(escape, &QShortcut::activated, panel, [weak] {
        if (auto s = weak.lock(); s && s->open)
            returnFromOutput(*s, true);
    });
}
void showOutput(const std::shared_ptr<OutputControl>& s, std::optional<std::uint64_t> sequence, InstanceId instance,
                QWidget* returnFocus, const QString& runId, const QString& configurationPath, quint64 eventId)
{
    if (!s || !s->open || !s->panel)
        return;
    auto mainWindow = s->panel->window();
    const bool external = returnFocus && returnFocus->window() != mainWindow;
    if (returnFocus && (external || !s->origin.hasSuspendedWindows()) &&
        (!s->panel->isAncestorOf(returnFocus) || s->panel->currentWidget() != s->editor)) {
        s->originPage = s->panel->isAncestorOf(returnFocus) ? s->panel->currentWidget() : nullptr;
        s->origin.capture(returnFocus, mainWindow);
    }
    const bool locating = sequence || eventId;
    if (locating || instance.valid() || !runId.isEmpty() || !configurationPath.isEmpty()) {
        s->run = runId.isEmpty() && s->archive ? s->archive->currentRun() : runId;
        if (locating || instance.valid() || !configurationPath.isEmpty())
            clearFilterControls(*s);
        std::optional<LogRecord> target;
        std::optional<LogLookup> exact;
        bool inMemory = false;
        if (locating) {
            if (s->live())
                for (const auto& row : s->entries)
                    if (eventId ? row.eventId == eventId : row.entry.sequence == *sequence) {
                        target = row;
                        inMemory = true;
                        break;
                    }
            if (!target && s->archive) {
                if (eventId) {
                    exact = LogQuery(*s->archive).locate(s->run, eventId);
                    target = exact->record;
                } else {
                    target = s->archive->locate(s->run, *sequence);
                }
            }
        }
        if (target)
            s->filter.sourceId = target->sourceId;
        if (instance.valid())
            s->filter.instance = instance;
        else if (!configurationPath.isEmpty() && !target)
            s->filter.instanceKey = configurationPath;
        syncRuns(*s);
        rebuildFilter(s);
        if (!instance.valid() && !configurationPath.isEmpty() && !target && s->notice.isEmpty() && s->text().isEmpty())
            s->notice = QString::fromUtf8("配置实例 %1 尚无已发布的运行日志。").arg(configurationPath);
        if (locating && !target) {
            if (exact && exact->status == LogLookupStatus::Expired)
                s->notice = QString::fromUtf8("该日志事件已按保留策略清理。");
            else if (exact && exact->status == LogLookupStatus::Unavailable)
                s->notice = exact->warning;
            else
                s->notice = QString::fromUtf8("当前记录中找不到该日志事件。");
        }
        if (target) {
            if (!inMemory && s->archive) {
                const LogQuery query(*s->archive);
                const auto located = exact ? *exact : query.locate(s->run, target->eventId);
                if (located.neighborhood) {
                    const auto page = query.page(*located.neighborhood);
                    s->history = page.rows;
                    s->olderCursor = page.older;
                    s->notice = page.warning;
                }
                s->browsingOlder = true;
            }
            s->cleared.erase(target->entry.sequence);
        }
        s->textDirty = true;
        refreshOutput(s);
    }
    s->panel->show();
    s->panel->setCurrentWidget(s->editor);
    mainWindow->raise();
    mainWindow->activateWindow();
    s->targets->setFocus();
    if (locating) {
        for (const auto& row : s->visibleRecords)
            if (eventId ? row.eventId == eventId : row.sequence == *sequence) {
                auto cursor = s->editor->textCursor();
                cursor.setPosition(row.start);
                cursor.setPosition(row.end, QTextCursor::KeepAnchor);
                s->editor->setTextCursor(cursor);
                s->editor->ensureCursorVisible();
                break;
            }
    }
}
void showNotificationOutput(const std::shared_ptr<OutputControl>& s, std::uint64_t sequence,
                            const QString& runId, WorkbenchReturnPoint origin, quint64 eventId)
{
    if (!s || !s->open || !s->panel) {
        origin.restore();
        return;
    }
    auto target = origin.target();
    if (!s->origin.hasSuspendedWindows() || origin.hasSuspendedWindows())
        s->originPage = target && s->panel->isAncestorOf(target) && s->panel->currentWidget() != s->editor
                            ? s->panel->currentWidget() : nullptr;
    s->origin.take(std::move(origin));
    showOutput(s, sequence, {}, nullptr, runId, {}, eventId);
}
QString currentOutputText(const std::shared_ptr<OutputControl>& s)
{
    if (!s || !s->open || !s->scheduler->isCurrentThread())
        return {};
    settleOutput(s, false);
    return s->text();
}
void toggleOutputPanel(const std::shared_ptr<OutputControl>& s)
{
    if (!s || !s->open || !s->panel) return;
    if (!s->panel->isHidden()) {
        returnFromOutput(*s, false);
        return;
    }
    const auto page=s->panel->currentWidget();
    showOutput(s,{}, {},QApplication::focusWidget());
    if(page && page!=s->editor) {
        s->panel->setCurrentWidget(page);
        if (const auto filter=page->findChild<QLineEdit*>("CordisProblemsFilter")) filter->setFocus();
    }
}
void showPreviousOutput(const std::shared_ptr<OutputControl>& s, QWidget* origin)
{
    if (!s || !s->open || !s->panel)
        return;
    if (s->archive)
        for (const auto& run : s->archive->runs())
            if (run.id != s->archive->currentRun()) {
                showOutput(s, {}, {}, origin, run.id);
                return;
            }
    showOutput(s, {}, {}, origin);
    QToolTip::showText(s->editor->mapToGlobal(QPoint(8, 4)), QString::fromUtf8("尚无可查看的上次运行。"), s->editor);
}
void openOutputDirectory(const std::shared_ptr<OutputControl>& s)
{
    if (!s || !s->open || !s->editor)
        return;
    if (!s->archive || !QDesktopServices::openUrl(QUrl::fromLocalFile(s->archive->directory())))
        QToolTip::showText(s->editor->window()->mapToGlobal(QPoint(30, 50)),
                           QString::fromUtf8("当前宿主没有可打开的日志目录。"), s->editor->window());
}
void clearOutput(const std::shared_ptr<OutputControl>& s)
{
    if (!s || !s->open || !s->scheduler->isCurrentThread())
        return;
    settleOutput(s, false);
    if (s->live()) {
        for (const auto& row : s->entries)
            if (outputMatches(row, s->filter))
                s->cleared.insert(row.entry.sequence);
        for (const auto& row : s->history)
            if (outputMatches(row, s->filter))
                s->cleared.insert(row.entry.sequence);
    }
    s->history.clear();
    s->textDirty = true;
    s->preserveText = false;
    refreshOutput(s);
}
} // namespace cordis::ui::detail
