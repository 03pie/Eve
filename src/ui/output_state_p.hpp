/** @file output_state_p.hpp
 * @brief 输出服务和工作台共享的宿主状态。候选值、已接纳值与可重建的显示位置分别持有。
 */
#pragma once
#include "output_p.hpp"
#include "pending_values_p.hpp"
#include "runtime_log_query_p.hpp"
#include "workbench_focus_p.hpp"
#include <QPointer>
#include <deque>
#include <map>
#include <set>
class ElaComboBox;
class ElaLineEdit;
class ElaToolButton;
class QLabel;
namespace eve::ui::detail
{
struct QtOutputOrigin {
    GenerationId generation;
    QString moduleId, moduleVersion, pluginId;
};
struct OutputControl {
    std::shared_ptr<qt::QtScheduler> scheduler;
    QPointer<QPlainTextEdit> editor;
    QPointer<ElaComboBox> targets, runs, levels, instances;
    QPointer<ElaLineEdit> search;
    QPointer<ElaToolButton> follow;
    QPointer<QLabel> feedback;
    QPointer<QTabWidget> panel;
    WorkbenchReturnPoint origin;
    QPointer<QWidget> originPage;
    WindowAccess access;
    std::function<QString(InstanceId)> resolve;
    std::function<QString(ContextIdentity)> resolveConfiguration;
    std::map<RuntimeId, QtOutputOrigin> qtOrigins;
    std::shared_ptr<RuntimeLogArchive> archive;
    std::deque<LogRecord> entries;
    std::map<std::string, int> retainedChannels;
    std::vector<LogRecord> history;
    LogFilter filter;
    QString run, notice;
    std::set<quint64> cleared;
    LogSourceCatalog liveSources;
    std::map<InstanceId, QString> publishedOwners;
    std::map<std::uint64_t, QString> liveLines;
    struct VisibleRecord {
        int start, end;
        std::uint64_t sequence;
        quint64 eventId;
        QString line;
    };
    std::vector<VisibleRecord> visibleRecords;
    std::vector<LogRecord> appended;
    unsigned unread = 0;
    unsigned rendersThisTurn = 0;
    std::optional<LogCursor> olderCursor;
    PendingValues<LogRecord> pending{OutputService::pendingCountLimit, OutputService::pendingByteLimit,
                                     OutputService::pendingTargetLimit};
    std::uint64_t sequence = 0;
    bool open = true, presenting = false, channelsDirty = true, textDirty = false, preserveText = true;
    bool browsingOlder = false;
    bool live() const { return !archive || run == archive->currentRun(); }

    void admit(OutputEntry entry, std::optional<OperationId> notification = {}, OutputDiagnostic diagnostic = {});
    void admit(LogRecord row);
    LogRecord captureQt(OutputEntry entry, const ContextSource& source) const;
    void readHistory(bool older);
    QString lineFor(const LogRecord& row, bool memory);
    QString text(std::vector<VisibleRecord>* positions = nullptr);
    void renderText(bool preserve, bool rebuild, const std::vector<LogRecord>& added);
    void syncSources();
};
bool outputMatches(const LogRecord& row, const LogFilter& filter);
} // namespace eve::ui::detail
