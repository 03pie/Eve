/** @file configuration.cpp
 * @brief Ela 配置编辑、差异、期望启停及保存页面；所有运行操作通过宿主会话。
 */
#include "desktop_text.hpp"
#include "appearance_p.hpp"
#include "status_p.hpp"
#include "theme_palette_p.hpp"
#include <ElaComboBox.h>
#include <ElaFlowLayout.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToggleSwitch.h>
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMetaObject>
#include <QPointer>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <eve/loader/qt_watcher.hpp>
#include <eve/ui/configuration.hpp>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace eve::ui {
namespace {
using namespace loader;
/** @brief 错误只按纯文本显示，不执行配置里的富文本或链接。 */
QString describe(const ErrorInfo& error, unsigned depth = 0)
{
    auto text = QString::fromStdString(error.message);
    if (error.entryPath)
        text += " [" + QString::fromStdString(*error.entryPath) + "]";
    const auto summary = text;
    if (depth < 4)
        for (std::size_t i = 0; i < error.causes.size() && i < 8; ++i) {
            const auto cause = describe(error.causes[i], depth + 1);
            if (cause != summary)
                text += "\n  " + cause;
        }
    return text;
}
/** @brief 显示实际状态，等待父代由单独字段区分。 */
QString runtimeState(const RunningEntry& entry)
{
    if (entry.waitingForParent)
        return eve::desktopText("configuration.waitingForParent");
    switch (entry.state) {
    case InstanceState::Pending:
        return eve::desktopText("configuration.waitingForDependencies");
    case InstanceState::Loading:
        return eve::desktopText("configuration.starting");
    case InstanceState::Active:
        return eve::desktopText("configuration.running");
    case InstanceState::Unloading:
        return eve::desktopText("configuration.cleaningUp");
    case InstanceState::Failed:
        return eve::desktopText("configuration.failed");
    case InstanceState::Disposed:
        return eve::desktopText("configuration.stopped");
    }
    return {};
}
/** @brief 目标差异用操作名称呈现，不要求用户解释枚举编号。 */
QString changeName(ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::NoOp:
        return eve::desktopText("configuration.unchanged");
    case ChangeKind::Add:
        return eve::desktopText("configuration.add");
    case ChangeKind::Remove:
        return eve::desktopText("configuration.remove");
    case ChangeKind::Disable:
        return eve::desktopText("configuration.disable");
    case ChangeKind::Restart:
        return eve::desktopText("configuration.restart");
    case ChangeKind::Recreate:
        return eve::desktopText("configuration.recreate");
    }
    return {};
}
/** @brief 页面只弱持会话，宿主生命周期独立于页签是否打开。 */
class ConfigurationPanel final : public QWidget {
public:
    /** @brief 创建 Ela 组件及有界刷新，不在构造时接受目标或读写文件。 */
    ConfigurationPanel(std::shared_ptr<ConfigurationSession> session, std::string text,
                       std::shared_ptr<QtConfigurationWatcher> watcher)
        : session_(session), watcher_(watcher)
    {
        setObjectName("EveConfigurationPage");
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 20, 24, 16);
        layout->setSpacing(8);
        auto title = new ElaText(eve::desktopText("configuration.configuration"), 24, this);
        title->setIsBold(true);
        layout->addWidget(title);
        auto help = new ElaText(eve::desktopText("configuration.editReviewChangesApplySaveToDisk"), this);
        help->setWordWrap(true);
        help->setTextPixelSize(13);
        help->setTextFormat(Qt::PlainText);
        help->setToolTip(
            eve::desktopText("configuration.savingNormalizesYAMLAndRemovesCommentsClosing"));
        detail::secondaryText(help);
        layout->addWidget(help);
        summary_ = new ElaText(this);
        summary_->setObjectName("EveConfigSummary");
        summary_->setWordWrap(true);
        summary_->setTextPixelSize(13);
        summary_->setTextFormat(Qt::PlainText);
        auto states = new QHBoxLayout;
        states->setSpacing(16);
        editStatus_ = new ElaText(this);
        editStatus_->setObjectName("EveConfigEditStatus");
        saveStatus_ = new ElaText(this);
        saveStatus_->setObjectName("EveConfigSaveStatus");
        for (auto statusLabel : {editStatus_, summary_, saveStatus_}) {
            statusLabel->setTextPixelSize(13);
            statusLabel->setTextFormat(Qt::PlainText);
            statusLabel->setWordWrap(true);
            states->addWidget(statusLabel, 1);
        }
        layout->addLayout(states);
        auto buttons = new ElaFlowLayout(0, 8, 4);
        buttons->setIsAnimation(false);
        preview_ = new detail::ActionButton(eve::desktopText("configuration.reviewChanges"), this);
        preview_->setObjectName("EveConfigPreview");
        apply_ = new detail::ActionButton(eve::desktopText("configuration.apply"), this);
        apply_->setObjectName("EveConfigApply");
        detail::primaryButton(apply_);
        apply_->setToolTip(eve::desktopText("configuration.applyTheReviewedConfigurationToTheWorkbench"));
        save_ = new detail::ActionButton(eve::desktopText("configuration.save"), this);
        save_->setObjectName("EveConfigSave");
        reset_ = new detail::ActionButton(eve::desktopText("configuration.discardEdits"), this);
        reset_->setObjectName("EveConfigReset");
        reset_->setToolTip(eve::desktopText("configuration.restoreTheAppliedConfigurationAndDiscardUnapplied"));
        rebase_ = new detail::ActionButton(eve::desktopText("configuration.useDiskBaseline"), this);
        rebase_->setObjectName("EveConfigRebase");
        rebase_->setToolTip(eve::desktopText("configuration.keepTheCurrentTargetAndUseThe"));
        for (auto button : {preview_, apply_, save_}) {
            button->setMinimumHeight(32);
            buttons->addWidget(button);
        }
        viewLog_ = new detail::ActionButton(eve::desktopText("configuration.viewRelatedLogs"), this);
        viewLog_->setObjectName("EveConfigOpenLog");
        viewLog_->hide();
        buttons->addWidget(viewLog_);
        QObject::connect(viewLog_, &QPushButton::clicked, this, [this] {
            Frame frame(*this);
            if (locate_ && eventId_)
                locate_(eventId_, viewLog_);
        });
        for (auto button : {reset_, rebase_}) {
            button->setMinimumHeight(32);
            buttons->addWidget(button);
        }
        layout->addLayout(buttons);
        diskRow_ = new QWidget(this);
        auto diskLayout = new QHBoxLayout(diskRow_);
        diskLayout->setContentsMargins(0, 0, 0, 0);
        diskStatus_ = new ElaText(diskRow_);
        diskStatus_->setTextPixelSize(12);
        diskStatus_->setTextFormat(Qt::PlainText);
        diskStatus_->setWordWrap(true);
        diskStatus_->setObjectName("EveConfigDiskStatus");
        detail::secondaryText(diskStatus_);
        diskLoad_ = new detail::ActionButton(eve::desktopText("configuration.loadDiskVersion"), diskRow_);
        diskLoad_->setObjectName("EveConfigDiskLoad");
        diskLoad_->setToolTip(eve::desktopText("configuration.loadTheDiskCopyIntoTheEditor"));
        diskLayout->addWidget(diskStatus_, 1);
        diskLayout->addWidget(diskLoad_);
        diskRow_->setVisible(static_cast<bool>(watcher));
        layout->addWidget(diskRow_);
        QObject::connect(diskLoad_, &QPushButton::clicked, this, [this] {
            action([&](ConfigurationSession& session) {
                auto source = watcher_.lock();
                if (!source || session.snapshot().value().closing)
                    return;
                auto disk = source->snapshot();
                if (!disk.hasValue() || !disk.value().external ||
                    disk.value().changeSequence != displayedDiskSequence_ || disk.value().closing) {
                    message_ = eve::desktopText("configuration.theDiskChangeNotificationHasBeenUpdated");
                    return;
                }
                auto acknowledged = source->acknowledge(displayedDiskSequence_);
                if (!acknowledged.hasValue()) {
                    message_ = describe(acknowledged.error());
                    return;
                }
                editor_->setPlainText(QString::fromStdString(disk.value().external->text));
                draft_.reset();
                message_ = eve::desktopText("configuration.theDiskCopyIsNowInThe");
            });
        });
        region_ = new ElaComboBox(this);
        region_->setProperty("ElaTranslucentFill", true);
        region_->setObjectName("EveConfigRegion");
        region_->setAccessibleName(eve::desktopText("configuration.configurationArea"));
        region_->addItems(
            {eve::desktopText("configuration.configurationSource"), eve::desktopText("configuration.desiredAndRuntimeState"), eve::desktopText("configuration.changesAndResults")});
        layout->addWidget(region_);
        auto split = new QSplitter(Qt::Horizontal, this);
        split->setObjectName("EveConfigSplitter");
        split_ = split;
        split->setChildrenCollapsible(false);
        split->setHandleWidth(8);
        const auto applyTheme = [this, split](ElaThemeType::ThemeMode mode) {
            applySurfacePalette(mode);
            // Keep the full hit target transparent over the backdrop and draw only its dividing edge.
            split->setStyleSheet(QStringLiteral("QSplitter#EveConfigSplitter::handle:horizontal {"
                                                "background: transparent; border-left: 1px solid %1; }")
                                     .arg(eTheme->getThemeColor(mode, ElaThemeType::BasicBorder).name(QColor::HexArgb)));
        };
        applyTheme(eTheme->getThemeMode());
        QObject::connect(eTheme, &ElaTheme::themeModeChanged, this, applyTheme);
        layout->addWidget(split, 3);
        auto source = new QWidget(split);
        auto sourceLayout = new QVBoxLayout(source);
        sourceLayout->setContentsMargins(0, 0, 0, 0);
        sourceLayout->setSpacing(8);
        auto sourceTitle = new ElaText(eve::desktopText("configuration.configurationSource"), 14, source);
        sourceTitle->setIsBold(true);
        sourceLayout->addWidget(sourceTitle);
        filePath_ = new ElaText(source);
        filePath_->setObjectName("EveConfigFilePath");
        filePath_->setTextPixelSize(12);
        filePath_->setTextFormat(Qt::PlainText);
        filePath_->setWordWrap(false);
        filePath_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        detail::secondaryText(filePath_);
        sourceLayout->addWidget(filePath_);
        editor_ = new ElaPlainTextEdit(source);
        editor_->setObjectName("EveConfigEditor");
        detail::bindWorkbenchContentPalette(editor_);
        auto textFont = QApplication::font();
        textFont.setPixelSize(14);
        editor_->setFont(textFont);
        editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
        editor_->setTabStopDistance(QFontMetrics(textFont).horizontalAdvance(QLatin1Char(' ')) * 4);
        editor_->setMinimumSize(180, 120);
        sourceLayout->addWidget(editor_, 1);
        auto side = new QWidget(split);
        auto sideLayout = new QVBoxLayout(side);
        sideLayout->setContentsMargins(0, 0, 0, 0);
        sideLayout->setSpacing(8);
        auto runningTitle = new ElaText(eve::desktopText("configuration.desiredAndRuntimeState"), 14, side);
        runningTitle->setIsBold(true);
        sideLayout->addWidget(runningTitle);
        auto runningHelp = new ElaText(eve::desktopText("configuration.entriesAppearAfterYouApplyTheConfiguration"), 12, side);
        detail::secondaryText(runningHelp);
        sideLayout->addWidget(runningHelp);
        table_ = new ElaTableView(side);
        table_->setObjectName("EveConfigEntries");
        detail::bindWorkbenchContentPalette(table_);
        model_ = new QStandardItemModel(table_);
        model_->setHorizontalHeaderLabels({eve::desktopText("configuration.entry"), eve::desktopText("configuration.desired"),
                                           eve::desktopText("configuration.runtime"), eve::desktopText("configuration.instance")});
        table_->setModel(model_);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->verticalHeader()->hide();
        table_->verticalHeader()->setDefaultSectionSize(34);
        table_->setShowGrid(false);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        table_->horizontalHeader()->setMinimumSectionSize(80);
        table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        table_->setColumnWidth(0, 200);
        table_->setColumnWidth(1, 90);
        table_->setColumnWidth(2, 100);
        table_->setColumnWidth(3, 80);
        table_->horizontalHeader()->setStretchLastSection(true);
        sideLayout->addWidget(table_, 1);
        auto selected = new QHBoxLayout;
        selected_ = new ElaText(eve::desktopText("configuration.selectAnEntryToChangeItsLocal"), side);
        selected_->setWordWrap(true);
        selected_->setTextPixelSize(14);
        selected_->setTextFormat(Qt::PlainText);
        enabled_ = new detail::KeyboardToggleSwitch(side);
        enabled_->setObjectName("EveConfigEnabled");
        selected->addWidget(selected_, 1);
        selected->addWidget(enabled_);
        sideLayout->addLayout(selected);
        split->setStretchFactor(0, 2);
        split->setStretchFactor(1, 3);
        auto resultTitle = new ElaText(eve::desktopText("configuration.changesAndResults"), 14, this);
        resultTitle_ = resultTitle;
        resultTitle->setIsBold(true);
        layout->addWidget(resultTitle);
        details_ = new ElaPlainTextEdit(this);
        details_->setObjectName("EveConfigDetails");
        detail::bindWorkbenchContentPalette(details_);
        details_->setFont(textFont);
        details_->setReadOnly(true);
        details_->setPlaceholderText(
            eve::desktopText("configuration.reviewedChangesApplicationResultsSavesAndDisk"));
        details_->setMinimumHeight(90);
        layout->addWidget(details_, 1);
        QObject::connect(region_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                         [this] { arrangeRegions(); });
        arrangeRegions();
        auto snapshot = session->snapshot().value();
        if (text.empty() && snapshot.coordinator.desired) {
            text = snapshot.coordinator.desired->source().normalizedYaml();
            editorRevision_ = snapshot.coordinator.desiredRevision;
            matches_ = true;
        }
        editor_->setPlainText(QString::fromStdString(text));
        QObject::connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
            Frame frame(*this);
            draft_.reset();
            matches_ = false;
            refresh();
        });
        QObject::connect(preview_, &QPushButton::clicked, this, [this] {
            action([&](ConfigurationSession& session) {
                const auto text = editor_->toPlainText();
                auto result = session.preview(text.toUtf8().toStdString());
                if (editor_->toPlainText() != text) {
                    draft_.reset();
                    message_ = eve::desktopText("configuration.theEditorChangedDuringReviewReviewThe");
                    return;
                }
                if (!result.hasValue()) {
                    draft_.reset();
                    message_ = describe(result.error());
                    record(eve::desktopText("configuration.configurationReviewFailed"), displayedRevision_, "CONFIG_PREVIEW_FAILED", &result.error());
                    return;
                }
                draft_.emplace(std::move(result).value());
                record(eve::desktopText("configuration.configurationReviewed"), displayedRevision_, "CONFIG_PREVIEW_OK");
                message_ = eve::desktopText("configuration.reviewTheChangesBelowBeforeApplyingApply");
            });
        });
        QObject::connect(apply_, &QPushButton::clicked, this, [this] {
            action([&](ConfigurationSession& session) {
                if (!draft_)
                    return;
                const auto draft = *draft_;
                const auto text = editor_->toPlainText();
                auto result = session.apply(draft);
                if (!result.hasValue()) {
                    message_ = describe(result.error());
                    record(eve::desktopText("configuration.couldNotApplyConfiguration"), displayedRevision_, "CONFIG_APPLY_FAILED", &result.error());
                    return;
                }
                editorRevision_ = result.value().revision;
                record(eve::desktopText("configuration.configurationTargetApplied"), editorRevision_, "CONFIG_APPLY_ACCEPTED");
                matches_ = editor_->toPlainText() == text;
                draft_.reset();
                message_ = matches_ ? eve::desktopText("configuration.targetAcceptedCheckTheRuntimeAndDisk")
                                    : eve::desktopText("configuration.targetAcceptedNewerEditsAreStillIn");
            });
        });
        QObject::connect(save_, &QPushButton::clicked, this, [this] {
            action([&](ConfigurationSession& session) {
                const auto revision = displayedRevision_;
                auto result = session.save(revision);
                if (!result.hasValue()) {
                    message_ = describe(result.error());
                    recordSave(revision, SaveOutcome::Failed, &result.error());
                    return;
                }
                saving_ = PendingSave{result.value(), revision};
                message_ = eve::desktopText("configuration.saveQueued");
            });
        });
        QObject::connect(reset_, &QPushButton::clicked, this,
                         [this] { action([&](ConfigurationSession& session) { useDesired(session); }); });
        QObject::connect(rebase_, &QPushButton::clicked, this, [this] {
            action([&](ConfigurationSession& session) {
                const auto revision = displayedRevision_;
                auto result = session.rebaseExternal(revision);
                const auto* error = result.hasValue() ? nullptr : &result.error();
                const auto operation = recoveryOperation(revision, error);
                recovery_ = SaveRecovery{revision, operation};
                record(result.hasValue() ? eve::desktopText("configuration.diskBaselineAccepted") : eve::desktopText("configuration.couldNotAcceptTheDiskBaseline"),
                       revision, result.hasValue() ? "CONFIG_REBASE_ACCEPTED" : "CONFIG_REBASE_FAILED",
                       error, operation);
                message_ =
                    result.hasValue() ? eve::desktopText("configuration.theBaselineIsUpToDateSave") : describe(result.error());
            });
        });
        QObject::connect(
            table_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
                Frame frame(*this);
                path_ = index.isValid() ? model_->index(index.row(), 0).data(Qt::UserRole).toString().toStdString()
                                        : std::string{};
                refresh();
            });
        QObject::connect(enabled_, &ElaToggleSwitch::toggled, this, [this](bool enabled) {
            Frame frame(*this);
            if (togglePending_)
                return;
            const auto path = path_;
            const auto revision = displayedRevision_;
            togglePending_ = true;
            enabled_->setEnabled(false);
            QMetaObject::invokeMethod(
                this,
                [this, path, revision, enabled] {
                    togglePending_ = false;
                    action([&](ConfigurationSession& session) {
                        if (!matches_ || editorRevision_ != revision) {
                            message_ = eve::desktopText("configuration.theEditorOrTargetChangedApplyThe");
                            return;
                        }
                        const auto text = editor_->toPlainText();
                        auto result = session.setEnabled(path, enabled, revision);
                        if (!result.hasValue()) {
                            message_ = describe(result.error());
                            return;
                        }
                        if (editor_->toPlainText() == text) {
                            useDesired(session);
                            message_ = eve::desktopText("configuration.theDesiredEnabledStateChangedSaveTo");
                        } else {
                            matches_ = false;
                            message_ = eve::desktopText("configuration.theDesiredEnabledStateChangedNewerEdits");
                        }
                    });
                },
                Qt::QueuedConnection);
        });
        timer_ = new QTimer(this);
        timer_->setInterval(100);
        QObject::connect(timer_, &QTimer::timeout, this, [this] { action([](ConfigurationSession&) {}); });
        timer_->start();
        QObject::connect(eTheme, &ElaTheme::themeModeChanged, this, [this] {
            fingerprint_.clear();
            action([](ConfigurationSession&) {});
        });
        refresh();
    }

    void bindFeedback(std::function<std::uint64_t(std::string, OutputLevel, OutputDiagnostic)> publish,
                      std::function<void(std::uint64_t, QWidget*)> locate)
    {
        publish_ = std::move(publish);
        locate_ = std::move(locate);
        viewLog_->setVisible(eventId_ && locate_);
    }

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        if (details_)
            arrangeRegions();
    }
    /** @brief 资源清理可嵌套泵送事件，当前 UI 写入栈退出后才允许 DeferredDelete。 */
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::DeferredDelete && frames_) {
            deferred_ = true;
            return true;
        }
        if (event->type() == QEvent::ParentChange || event->type() == QEvent::Show)
            applySurfacePalette(eTheme->getThemeMode());
        return QWidget::event(event);
    }

private:
    void applySurfacePalette(ElaThemeType::ThemeMode mode)
    {
        // A standalone page owns its surface. Embedded pages use their window's backdrop.
        if (isWindow()) {
            setPalette(detail::workbenchPalette(palette(), mode));
            setAutoFillBackground(true);
        } else {
            detail::applyWorkbenchContentPalette(this, mode);
        }
    }
    void arrangeRegions()
    {
        const bool narrow = width() < 760;
        const int section = region_->currentIndex();
        region_->setVisible(narrow);
        split_->setVisible(!narrow || section != 2);
        for (int i = 0; i < split_->count(); ++i)
            split_->widget(i)->setVisible(!narrow || section == i);
        resultTitle_->setVisible(!narrow || section == 2);
        details_->setVisible(!narrow || section == 2);
    }
    void record(const QString& summary, std::uint64_t revision, const char* code, const ErrorInfo* error = nullptr,
                std::optional<OperationId> operation = {}, OutputLevel level = OutputLevel::Info)
    {
        if (!publish_)
            return;
        auto text = summary + eve::desktopText("configuration.revision").arg(revision);
        if (error) {
            text += eve::desktopText("configuration.errorCode").arg(static_cast<int>(error->code));
            if (!operation && error->operation.valid())
                operation = error->operation;
        }
        eventId_ = publish_(text.toStdString(), error ? OutputLevel::Error : level, {operation, code});
        viewLog_->setVisible(eventId_ && locate_);
    }
    OperationId recoveryOperation(std::uint64_t revision, const ErrorInfo* error) const
    {
        if (recovery_ && recovery_->revision == revision)
            return recovery_->operation;
        if (error && error->operation.valid())
            return error->operation;
        return allocateOperationId();
    }
    void recordSave(std::uint64_t revision, SaveOutcome outcome, const ErrorInfo* error)
    {
        const auto operation = recoveryOperation(revision, error);
        const auto succeeded = outcome == SaveOutcome::Written || outcome == SaveOutcome::Unchanged;
        if (succeeded)
            recovery_.reset();
        else
            recovery_ = SaveRecovery{revision, operation};
        const char* code = "CONFIG_SAVE_FAILED";
        switch (outcome) {
        case SaveOutcome::Written:
            code = "CONFIG_SAVE_WRITTEN";
            break;
        case SaveOutcome::Unchanged:
            code = "CONFIG_SAVE_UNCHANGED";
            break;
        case SaveOutcome::Conflict:
            code = "CONFIG_SAVE_CONFLICT";
            break;
        case SaveOutcome::Skipped:
            code = "CONFIG_SAVE_SKIPPED";
            break;
        case SaveOutcome::Failed:
            break;
        }
        record(succeeded ? eve::desktopText("configuration.configurationSaved") : eve::desktopText("configuration.couldNotSaveConfiguration"), revision, code, error,
               operation, succeeded ? OutputLevel::Info : OutputLevel::Error);
    }
    /** @brief 模型信号或 EnabledChange 重入关闭时不销毁当前页面栈。 */
    struct Frame {
        ConfigurationPanel& panel;
        /** @brief 固定一次 UI 访问。 */
        explicit Frame(ConfigurationPanel& value) : panel(value) { ++panel.frames_; }
        /** @brief 已接收的 DeferredDelete 不会再次 deleteLater，改在应用队列中等待访问栈归零。 */
        ~Frame()
        {
            if (--panel.frames_ != 0 || !panel.deferred_)
                return;
            panel.deferred_ = false;
            QPointer<ConfigurationPanel> guarded(&panel);
            QTimer::singleShot(0, qApp, [guarded] {
                if (!guarded)
                    return;
                if (guarded->frames_)
                    guarded->deferred_ = true;
                else
                    delete guarded.data();
            });
        }
    };
    /** @brief 捕获 Qt 信号边界异常，错误留在页面值状态而不穿越事件循环。 */
    template <class F> void action(F callback)
    {
        Frame frame(*this);
        try {
            auto session = session_.lock();
            if (!session) {
                message_ = eve::desktopText("configuration.theConfigurationSessionHasClosed");
                refresh();
                return;
            }
            callback(*session);
        } catch (const std::exception& error) {
            message_ = QString::fromUtf8(error.what());
        } catch (...) {
            message_ = eve::desktopText("configuration.theConfigurationOperationFailedUnexpectedly");
        }
        refresh();
    }
    /** @brief 显式替换编辑内容；定时刷新不会覆盖尚未应用的编辑。 */
    void useDesired(ConfigurationSession& session)
    {
        auto state = session.snapshot().value();
        if (!state.coordinator.desired)
            return;
        QSignalBlocker blocker(editor_);
        editor_->setPlainText(QString::fromStdString(state.coordinator.desired->source().normalizedYaml()));
        editorRevision_ = state.coordinator.desiredRevision;
        matches_ = true;
        draft_.reset();
    }
    /** @brief 控件重用且程序同步屏蔽业务信号，期望值和实际状态分别显示。 */
    void refresh()
    {
        Frame frame(*this);
        if (refreshing_)
            return;
        refreshing_ = true;
        struct Reset {
            bool& value;
            /** @brief 所有返回路径恢复刷新门禁。 */
            ~Reset() { value = false; }
        } reset{refreshing_};
        auto session = session_.lock();
        if (!session) {
            for (auto button : {preview_, apply_, save_, reset_, rebase_, diskLoad_})
                button->setEnabled(false);
            enabled_->setEnabled(false);
            editor_->setReadOnly(true);
            summary_->setText(eve::desktopText("configuration.runtimeSessionUnavailable"));
            saveStatus_->setText(eve::desktopText("configuration.diskSaveUnavailable"));
            return;
        }
        auto result = session->snapshot();
        if (!result.hasValue()) {
            message_ = describe(result.error());
            return;
        }
        auto state = result.value();
        displayedRevision_ = state.coordinator.desiredRevision;
        if (editorRevision_ != displayedRevision_)
            matches_ = false;
        if (saving_)
            if (auto saved = saving_->task.tryResult()) {
                const bool written = saved->hasValue() && (saved->value().outcome == SaveOutcome::Written ||
                                                           saved->value().outcome == SaveOutcome::Unchanged);
                const auto error = saved->hasValue() ? saved->value().error : std::optional<ErrorInfo>(saved->error());
                message_ = written ? eve::desktopText("configuration.savedRevision").arg(saved->value().revision)
                           : state.store.external
                               ? eve::desktopText("configuration.theFileChangedOnDiskReviewThe")
                           : saved->hasValue() && saved->value().error ? describe(*saved->value().error)
                           : saved->hasValue()                         ? eve::desktopText("configuration.theSaveDidNotFinish")
                                                                       : describe(saved->error());
                const auto revision = saved->hasValue() ? saved->value().revision : saving_->requestedRevision;
                saving_.reset();
                recordSave(revision, saved->hasValue() ? saved->value().outcome : SaveOutcome::Failed,
                           error ? &*error : nullptr);
            }
        editStatus_->setText(matches_ && state.coordinator.desired ? eve::desktopText("configuration.editorApplied")
                             : draft_ && draft_->baseRevision() == displayedRevision_
                                 ? eve::desktopText("configuration.editorChangesReviewed")
                                 : eve::desktopText("configuration.editorNotApplied"));
        auto runtime = eve::desktopText("configuration.inProgress");
        if (!state.coordinator.desired)
            runtime = eve::desktopText("configuration.notApplied");
        else if (state.coordinator.runningError)
            runtime = eve::desktopText("configuration.stateUnavailable");
        else if (state.latestPlan && state.latestPlan->revision == displayedRevision_) {
            switch (state.latestPlan->outcome) {
            case PlanOutcome::Applied:
                runtime = eve::desktopText("configuration.applied");
                break;
            case PlanOutcome::Pending:
                runtime = eve::desktopText("configuration.waitingForDependenciesOrParent");
                break;
            case PlanOutcome::Failed:
                runtime = eve::desktopText("configuration.executionFailed");
                break;
            case PlanOutcome::Superseded:
                runtime = eve::desktopText("configuration.superseded");
                break;
            case PlanOutcome::Cancelled:
                runtime = eve::desktopText("configuration.canceled");
                break;
            }
        }
        summary_->setText(eve::desktopText("configuration.runtimeR").arg(runtime).arg(displayedRevision_));
        auto persisted = state.store.external    ? eve::desktopText("configuration.saveConflict")
                         : state.store.writing   ? eve::desktopText("configuration.saving")
                         : state.store.lastError ? eve::desktopText("configuration.saveFailed")
                         : state.store.dirty     ? eve::desktopText("configuration.unsaved")
                         : state.store.persistedRevision
                             ? eve::desktopText("configuration.savedR").arg(*state.store.persistedRevision)
                             : eve::desktopText("configuration.notYetSaved");
        saveStatus_->setText(eve::desktopText("configuration.disk").arg(persisted));
        saveStatus_->setToolTip(
            state.store.external
                ? eve::desktopText("configuration.reviewTheDiskCopyUseItAs")
                : eve::desktopText("configuration.saveWritesTheAcceptedTargetToDisk"));
        const auto file = state.store.baseline ? QString::fromStdString(state.store.baseline->version.path) : QString{};
        filePath_->setText(filePath_->fontMetrics().elidedText(file, Qt::ElideMiddle, filePath_->width()));
        filePath_->setToolTip(file);
        filePath_->setAccessibleName(file);
        std::map<std::string, RunningEntry> running;
        for (const auto& row : state.coordinator.running)
            running.emplace(row.path, row);
        QString fingerprint = QString::number(displayedRevision_);
        for (const auto& row : state.coordinator.running)
            fingerprint += "|" + QString::fromStdString(row.path) + "|" + runtimeState(row) + "|" +
                           QString::number(row.instance.value()) + "|" + QString::number(row.epoch) + ";";
        if (fingerprint != fingerprint_) {
            fingerprint_ = fingerprint;
            QSignalBlocker blocker(table_->selectionModel());
            auto desired = state.coordinator.desired;
            model_->setRowCount(desired ? static_cast<int>(desired->source().structure().entries().size()) : 0);
            int index = 0, chosen = -1;
            if (desired)
                for (const auto& entry : desired->source().structure().entries()) {
                    auto actual = running.find(entry.first);
                    QStringList columns{QString::fromStdString(entry.first),
                                        entry.second.disabled            ? eve::desktopText("configuration.disable")
                                        : entry.second.effectiveDisabled ? eve::desktopText("configuration.parentDisabled")
                                                                         : eve::desktopText("configuration.enable"),
                                        actual == running.end() ? eve::desktopText("configuration.stopped")
                                                                : runtimeState(actual->second),
                                        actual == running.end() || !actual->second.instance.valid()
                                            ? QString("—")
                                            : QString::number(actual->second.instance.value())};
                    for (int column = 0; column < columns.size(); ++column) {
                        auto item = new QStandardItem(columns[column]);
                        if (column == 2 && actual != running.end())
                            detail::decorateStatus(item,
                                                   detail::instanceStatus(actual->second.state,
                                                                          static_cast<bool>(actual->second.failure)));
                        if (!column)
                            item->setData(columns[0], Qt::UserRole);
                        item->setToolTip(columns[column]);
                        model_->setItem(index, column, item);
                    }
                    if (entry.first == path_)
                        chosen = index;
                    ++index;
                }
            if (chosen >= 0)
                table_->selectRow(chosen);
            else {
                table_->clearSelection();
                table_->setCurrentIndex({});
                path_.clear();
            }
        }
        auto desired = state.coordinator.desired;
        const auto entry = desired ? desired->source().structure().entries().find(path_)
                                   : std::map<std::string, EntryValue>::const_iterator{};
        const bool selected = desired && entry != desired->source().structure().entries().end();
        selected_->setText(selected ? QString::fromStdString(path_) +
                                          (matches_ ? QString{} : eve::desktopText("configuration.applyEditsOrRestoreTheAppliedConfiguration"))
                                    : eve::desktopText("configuration.selectAnEntryToChangeItsLocal"));
        {
            QSignalBlocker blocker(enabled_);
            enabled_->setIsToggled(selected && !entry->second.disabled);
        }
        QString detail = message_;
        if (draft_) {
            detail += eve::desktopText("configuration.candidateBasedOnRevisionTargetChanges").arg(draft_->baseRevision());
            for (const auto& row : draft_->changes().entries)
                detail += QString::fromStdString(row.path) + " : " + changeName(row.kind) + "\n";
        }
        if (state.latestPlan) {
            detail += eve::desktopText("configuration.lastExecutionRevision")
                          .arg(state.latestPlan->revision)
                          .arg(state.latestPlan->outcome == PlanOutcome::Applied   ? eve::desktopText("configuration.applied")
                               : state.latestPlan->outcome == PlanOutcome::Pending ? eve::desktopText("configuration.waitingForDependenciesOrParent")
                               : state.latestPlan->outcome == PlanOutcome::Failed  ? eve::desktopText("configuration.failed")
                                                                                   : eve::desktopText("configuration.supersededOrCanceled"));
            for (const auto& error : state.latestPlan->errors)
                detail += describe(error) + "\n";
        }
        if (state.coordinator.runningError)
            detail += "\n" + describe(*state.coordinator.runningError);
        if (auto found = running.find(path_); found != running.end() && found->second.failure)
            detail += "\n" + describe(*found->second.failure);
        if (state.store.lastError) {
            const auto error = describe(*state.store.lastError);
            if (error != message_)
                detail += "\n" + error;
        }
        if (state.store.external)
            detail += eve::desktopText("configuration.conflictingDiskCopyReviewBeforeAccepting") +
                      QString::fromStdString(state.store.external->text);
        diskLoad_->setEnabled(false);
        if (auto source = watcher_.lock()) {
            auto disk = source->snapshot();
            if (!disk.hasValue())
                diskStatus_->setText(describe(disk.error()));
            else {
                displayedDiskSequence_ = disk.value().changeSequence;
                auto text = disk.value().closed || disk.value().closing
                                ? eve::desktopText("configuration.fileMonitoringIsOff")
                                : eve::desktopText("configuration.fileMonitoringIsOnReviewDiskChanges");
                if (disk.value().external) {
                    const auto& external = *disk.value().external;
                    text = external.version.exists ? eve::desktopText("configuration.theFileChangedOnDiskYouCan")
                                                   : eve::desktopText("configuration.theFileWasDeletedLoadingTheDisk");
                    detail +=
                        eve::desktopText("configuration.detectedDiskCopyPendingReview") + QString::fromStdString(external.text);
                    diskLoad_->setEnabled(!state.closing && !disk.value().closing && !disk.value().closed &&
                                          !disk.value().deliveryBlocked);
                }
                if (disk.value().error)
                    text += "\n" + describe(*disk.value().error);
                diskStatus_->setText(text);
            }
        } else
            diskStatus_->setText(eve::desktopText("configuration.fileMonitoringIsUnavailable"));
        if (detail.isEmpty())
            detail = eve::desktopText("configuration.reviewChangesBeforeApplyingThemTheRuntime");
        if (details_->toPlainText() != detail)
            details_->setPlainText(detail);
        auto current = session->snapshot();
        const bool open = current.hasValue() && !current.value().closing && !current.value().deliveryBlocked &&
                          !state.store.deliveryBlocked && !state.coordinator.deliveryBlocked;
        editor_->setReadOnly(!open);
        preview_->setEnabled(open);
        apply_->setEnabled(open && draft_ && draft_->baseRevision() == displayedRevision_);
        save_->setEnabled(open && desired && !saving_);
        reset_->setEnabled(open && desired);
        rebase_->setEnabled(open && state.store.external && !state.store.writing && !state.store.queuedWrites);
        enabled_->setEnabled(open && selected && matches_ && !togglePending_);
        enabled_->setAccessibleName(eve::desktopText("configuration.enableTheSelectedConfigurationEntry"));
        enabled_->setToolTip(!selected   ? eve::desktopText("configuration.selectAConfigurationEntry")
                             : !matches_ ? eve::desktopText("configuration.applyEditsOrRestoreTheAppliedConfiguration2")
                             : !open || togglePending_
                                 ? eve::desktopText("configuration.waitForTheCurrentOperationToFinish")
                                 : eve::desktopText("configuration.changeTheDesiredEnabledStateOfThe"));
        diskLoad_->setEnabled(open && diskLoad_->isEnabled());
    }
    std::weak_ptr<ConfigurationSession> session_;
    std::weak_ptr<QtConfigurationWatcher> watcher_;
    QWidget* diskRow_;
    ElaText* diskStatus_;
    ElaPushButton* diskLoad_;
    std::uint64_t displayedDiskSequence_ = 0;
    ElaText *summary_, *selected_, *filePath_, *editStatus_, *saveStatus_;
    ElaPushButton *preview_, *apply_, *save_, *reset_, *rebase_;
    ElaPushButton* viewLog_;
    ElaComboBox* region_;
    ElaText* resultTitle_;
    QSplitter* split_;
    std::uint64_t eventId_ = 0;
    std::function<std::uint64_t(std::string, OutputLevel, OutputDiagnostic)> publish_;
    std::function<void(std::uint64_t, QWidget*)> locate_;
    ElaPlainTextEdit *editor_, *details_ = nullptr;
    ElaTableView* table_;
    QStandardItemModel* model_;
    ElaToggleSwitch* enabled_;
    QTimer* timer_;
    std::optional<ConfigurationDraft> draft_;
    struct PendingSave {
        Task<SaveReport> task;
        std::uint64_t requestedRevision;
    };
    struct SaveRecovery {
        std::uint64_t revision;
        OperationId operation;
    };
    std::optional<PendingSave> saving_;
    std::optional<SaveRecovery> recovery_;
    std::string path_;
    QString message_, fingerprint_;
    std::uint64_t displayedRevision_ = 0, editorRevision_ = 0;
    unsigned frames_ = 0;
    bool deferred_ = false, refreshing_ = false, matches_ = false, togglePending_ = false;
};
} // namespace
void bindConfigurationFeedback(QWidget* page, std::function<std::uint64_t(std::string, OutputLevel)> publish,
                               std::function<void(std::uint64_t, QWidget*)> locate)
{
    std::function<std::uint64_t(std::string, OutputLevel, OutputDiagnostic)> adapted;
    if (publish)
        adapted = [publish = std::move(publish)](std::string text, OutputLevel level, OutputDiagnostic) {
            return publish(std::move(text), level);
        };
    bindConfigurationFeedback(page, std::move(adapted), std::move(locate));
}
void bindConfigurationFeedback(QWidget* page,
                               std::function<std::uint64_t(std::string, OutputLevel, OutputDiagnostic)> publish,
                               std::function<void(std::uint64_t, QWidget*)> locate)
{
    if (auto panel = dynamic_cast<ConfigurationPanel*>(page))
        panel->bindFeedback(std::move(publish), std::move(locate));
}
/** @brief 工厂本身不接受目标，返回页面交由已有页面 effect 的完整销毁屏障托管。 */
std::unique_ptr<QWidget> createConfigurationPage(const std::shared_ptr<loader::ConfigurationSession>& session,
                                                 std::string text,
                                                 std::shared_ptr<loader::QtConfigurationWatcher> watcher)
{
    if (!qApp || QThread::currentThread() != qApp->thread() || !session || !session->snapshot().hasValue())
        throw std::invalid_argument("configuration page requires application thread and owner session");
    return std::make_unique<ConfigurationPanel>(session, std::move(text), std::move(watcher));
}
} // namespace eve::ui
