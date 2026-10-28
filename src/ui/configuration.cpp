/** @file configuration.cpp
 * @brief Ela 配置编辑、差异、期望启停及保存页面；所有运行操作通过宿主会话。
 */
#include "appearance_p.hpp"
#include "status_p.hpp"
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
#include <QPalette>
#include <QPointer>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <cordis/loader/qt_watcher.hpp>
#include <cordis/ui/configuration.hpp>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cordis::ui {
namespace {
using namespace loader;
/** @brief 错误只按纯文本显示，不执行配置里的富文本或链接。 */
QString describe(const ErrorInfo& error, unsigned depth = 0)
{
    auto text = QString::fromStdString(error.message);
    if (error.entryPath)
        text += " [" + QString::fromStdString(*error.entryPath) + "]";
    if (depth < 4)
        for (std::size_t i = 0; i < error.causes.size() && i < 8; ++i)
            text += "\n  " + describe(error.causes[i], depth + 1);
    return text;
}
/** @brief 显示实际状态，等待父代由单独字段区分。 */
QString runtimeState(const RunningEntry& entry)
{
    if (entry.waitingForParent)
        return QString::fromUtf8("等待父项");
    switch (entry.state) {
    case InstanceState::Pending:
        return QString::fromUtf8("等待依赖");
    case InstanceState::Loading:
        return QString::fromUtf8("启动中");
    case InstanceState::Active:
        return QString::fromUtf8("运行中");
    case InstanceState::Unloading:
        return QString::fromUtf8("清理中");
    case InstanceState::Failed:
        return QString::fromUtf8("失败");
    case InstanceState::Disposed:
        return QString::fromUtf8("已停止");
    }
    return {};
}
/** @brief 目标差异用操作名称呈现，不要求用户解释枚举编号。 */
QString changeName(ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::NoOp:
        return QString::fromUtf8("不变");
    case ChangeKind::Add:
        return QString::fromUtf8("新增");
    case ChangeKind::Remove:
        return QString::fromUtf8("移除");
    case ChangeKind::Disable:
        return QString::fromUtf8("停用");
    case ChangeKind::Restart:
        return QString::fromUtf8("重启");
    case ChangeKind::Recreate:
        return QString::fromUtf8("重建");
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
        setObjectName("CordisConfigurationPage");
        setAutoFillBackground(true);
        const auto applyTheme = [this](ElaThemeType::ThemeMode mode) {
            Frame frame(*this);
            auto colors = palette();
            colors.setColor(QPalette::Window, eTheme->getThemeColor(mode, ElaThemeType::WindowBase));
            setPalette(colors);
        };
        applyTheme(eTheme->getThemeMode());
        QObject::connect(eTheme, &ElaTheme::themeModeChanged, this, applyTheme);
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 20, 24, 16);
        layout->setSpacing(8);
        auto title = new ElaText(QString::fromUtf8("配置管理"), 24, this);
        title->setIsBold(true);
        layout->addWidget(title);
        auto help = new ElaText(QString::fromUtf8("编辑配置 → 检查差异 → 应用到工作台 → 保存到磁盘"), this);
        help->setWordWrap(true);
        help->setTextPixelSize(13);
        help->setTextFormat(Qt::PlainText);
        help->setToolTip(
            QString::fromUtf8("保存会规范化 YAML，不保留注释；退出会刷新已应用的修改，未应用编辑不会写盘。"));
        detail::secondaryText(help);
        layout->addWidget(help);
        summary_ = new ElaText(this);
        summary_->setObjectName("CordisConfigSummary");
        summary_->setWordWrap(true);
        summary_->setTextPixelSize(13);
        summary_->setTextFormat(Qt::PlainText);
        auto states = new QHBoxLayout;
        states->setSpacing(16);
        editStatus_ = new ElaText(this);
        editStatus_->setObjectName("CordisConfigEditStatus");
        saveStatus_ = new ElaText(this);
        saveStatus_->setObjectName("CordisConfigSaveStatus");
        for (auto statusLabel : {editStatus_, summary_, saveStatus_}) {
            statusLabel->setTextPixelSize(13);
            statusLabel->setTextFormat(Qt::PlainText);
            statusLabel->setWordWrap(true);
            states->addWidget(statusLabel, 1);
        }
        layout->addLayout(states);
        auto buttons = new QHBoxLayout;
        buttons->setSpacing(8);
        preview_ = new detail::ActionButton(QString::fromUtf8("检查差异"), this);
        preview_->setObjectName("CordisConfigPreview");
        apply_ = new detail::ActionButton(QString::fromUtf8("应用"), this);
        apply_->setObjectName("CordisConfigApply");
        detail::primaryButton(apply_);
        apply_->setToolTip(QString::fromUtf8("将已检查的配置应用到工作台；保存到磁盘需另行操作。"));
        save_ = new detail::ActionButton(QString::fromUtf8("保存"), this);
        save_->setObjectName("CordisConfigSave");
        reset_ = new detail::ActionButton(QString::fromUtf8("还原编辑"), this);
        reset_->setObjectName("CordisConfigReset");
        reset_->setToolTip(QString::fromUtf8("使用已应用目标替换编辑区，放弃尚未应用的编辑。"));
        rebase_ = new detail::ActionButton(QString::fromUtf8("采用冲突基线"), this);
        rebase_->setObjectName("CordisConfigRebase");
        rebase_->setToolTip(QString::fromUtf8("保留当前目标，只采用下方展示的磁盘版本为新基线；之后仍需点击保存。"));
        for (auto button : {preview_, apply_, save_}) {
            button->setMinimumHeight(32);
            buttons->addWidget(button);
        }
        buttons->addStretch();
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
        diskStatus_->setObjectName("CordisConfigDiskStatus");
        detail::secondaryText(diskStatus_);
        diskLoad_ = new detail::ActionButton(QString::fromUtf8("载入磁盘版本"), diskRow_);
        diskLoad_->setObjectName("CordisConfigDiskLoad");
        diskLoad_->setToolTip(QString::fromUtf8("将通知中的磁盘原文载入编辑区；不会自动应用或改变保存基线。"));
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
                    message_ = QString::fromUtf8("磁盘通知已变化，请重新查看。");
                    return;
                }
                auto acknowledged = source->acknowledge(displayedDiskSequence_);
                if (!acknowledged.hasValue()) {
                    message_ = describe(acknowledged.error());
                    return;
                }
                editor_->setPlainText(QString::fromStdString(disk.value().external->text));
                draft_.reset();
                message_ = QString::fromUtf8("磁盘原文已载入编辑区；尚未预览或应用，保存基线未改变。");
            });
        });
        auto split = new QSplitter(Qt::Horizontal, this);
        split->setChildrenCollapsible(false);
        split->setHandleWidth(8);
        layout->addWidget(split, 3);
        auto source = new QWidget(split);
        auto sourceLayout = new QVBoxLayout(source);
        sourceLayout->setContentsMargins(0, 0, 0, 0);
        sourceLayout->setSpacing(8);
        auto sourceTitle = new ElaText(QString::fromUtf8("配置源文件"), 14, source);
        sourceTitle->setIsBold(true);
        sourceLayout->addWidget(sourceTitle);
        filePath_ = new ElaText(source);
        filePath_->setObjectName("CordisConfigFilePath");
        filePath_->setTextPixelSize(12);
        filePath_->setTextFormat(Qt::PlainText);
        filePath_->setWordWrap(false);
        filePath_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        detail::secondaryText(filePath_);
        sourceLayout->addWidget(filePath_);
        editor_ = new ElaPlainTextEdit(source);
        editor_->setObjectName("CordisConfigEditor");
        editor_->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}");
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
        auto runningTitle = new ElaText(QString::fromUtf8("期望与运行状态"), 14, side);
        runningTitle->setIsBold(true);
        sideLayout->addWidget(runningTitle);
        auto runningHelp = new ElaText(QString::fromUtf8("应用后显示条目；选择条目可调整启用状态。"), 12, side);
        detail::secondaryText(runningHelp);
        sideLayout->addWidget(runningHelp);
        table_ = new ElaTableView(side);
        table_->setObjectName("CordisConfigEntries");
        model_ = new QStandardItemModel(table_);
        model_->setHorizontalHeaderLabels({QString::fromUtf8("条目"), QString::fromUtf8("期望"),
                                           QString::fromUtf8("运行"), QString::fromUtf8("实例")});
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
        selected_ = new ElaText(QString::fromUtf8("选择条目调整本地启用状态"), side);
        selected_->setWordWrap(true);
        selected_->setTextPixelSize(14);
        selected_->setTextFormat(Qt::PlainText);
        enabled_ = new detail::KeyboardToggleSwitch(side);
        enabled_->setObjectName("CordisConfigEnabled");
        selected->addWidget(selected_, 1);
        selected->addWidget(enabled_);
        sideLayout->addLayout(selected);
        split->setStretchFactor(0, 2);
        split->setStretchFactor(1, 3);
        auto resultTitle = new ElaText(QString::fromUtf8("差异与执行结果"), 14, this);
        resultTitle->setIsBold(true);
        layout->addWidget(resultTitle);
        details_ = new ElaPlainTextEdit(this);
        details_->setObjectName("CordisConfigDetails");
        details_->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}");
        details_->setFont(textFont);
        details_->setReadOnly(true);
        details_->setPlaceholderText(
            QString::fromUtf8("检查差异后在此查看变更；应用、保存及磁盘冲突的结果也会显示在这里。"));
        details_->setMinimumHeight(90);
        layout->addWidget(details_, 1);
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
                    message_ = QString::fromUtf8("编辑内容在检查期间发生变化，请重新检查差异。");
                    return;
                }
                if (!result.hasValue()) {
                    draft_.reset();
                    message_ = describe(result.error());
                    return;
                }
                draft_.emplace(std::move(result).value());
                message_ = QString::fromUtf8("检查通过；可审阅下方差异后应用。无配置变化时，应用仍可重试失败项。");
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
                    return;
                }
                editorRevision_ = result.value().revision;
                matches_ = editor_->toPlainText() == text;
                draft_.reset();
                message_ = matches_ ? QString::fromUtf8("目标已接受；运行结果和保存状态分别显示。")
                                    : QString::fromUtf8("目标已接受；期间新增的编辑已保留，尚未应用。");
            });
        });
        QObject::connect(save_, &QPushButton::clicked, this, [this] {
            action([&](ConfigurationSession& session) {
                auto result = session.save(displayedRevision_);
                if (!result.hasValue()) {
                    message_ = describe(result.error());
                    return;
                }
                saving_.emplace(result.value());
                message_ = QString::fromUtf8("保存已排队。");
            });
        });
        QObject::connect(reset_, &QPushButton::clicked, this,
                         [this] { action([&](ConfigurationSession& session) { useDesired(session); }); });
        QObject::connect(rebase_, &QPushButton::clicked, this, [this] {
            action([&](ConfigurationSession& session) {
                auto result = session.rebaseExternal(displayedRevision_);
                message_ =
                    result.hasValue() ? QString::fromUtf8("基线已更新；当前目标尚未保存。") : describe(result.error());
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
                            message_ = QString::fromUtf8("编辑内容或目标已改变，请先应用或使用已应用目标。");
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
                            message_ = QString::fromUtf8("启停目标已更新，尚未保存。");
                        } else {
                            matches_ = false;
                            message_ = QString::fromUtf8("启停目标已更新；期间新增的编辑已保留，尚未应用。");
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

protected:
    /** @brief 资源清理可嵌套泵送事件，当前 UI 写入栈退出后才允许 DeferredDelete。 */
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::DeferredDelete && frames_) {
            deferred_ = true;
            return true;
        }
        return QWidget::event(event);
    }

private:
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
                message_ = QString::fromUtf8("配置会话已释放。");
                refresh();
                return;
            }
            callback(*session);
        } catch (const std::exception& error) {
            message_ = QString::fromUtf8(error.what());
        } catch (...) {
            message_ = QString::fromUtf8("配置操作异常。");
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
            summary_->setText(QString::fromUtf8("运行：会话不可用"));
            saveStatus_->setText(QString::fromUtf8("磁盘：无法保存"));
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
            if (auto saved = saving_->tryResult()) {
                message_ = saved->hasValue() && (saved->value().outcome == SaveOutcome::Written ||
                                                 saved->value().outcome == SaveOutcome::Unchanged)
                               ? QString::fromUtf8("已保存修订 %1。").arg(saved->value().revision)
                           : saved->hasValue() && saved->value().error ? describe(*saved->value().error)
                           : saved->hasValue()                         ? QString::fromUtf8("保存未完成。")
                                                                       : describe(saved->error());
                saving_.reset();
            }
        editStatus_->setText(matches_ && state.coordinator.desired ? QString::fromUtf8("编辑：已应用")
                             : draft_ && draft_->baseRevision() == displayedRevision_
                                 ? QString::fromUtf8("编辑：差异已检查")
                                 : QString::fromUtf8("编辑：尚未应用"));
        auto runtime = QString::fromUtf8("执行中");
        if (!state.coordinator.desired)
            runtime = QString::fromUtf8("尚未应用");
        else if (state.coordinator.runningError)
            runtime = QString::fromUtf8("状态不可用");
        else if (state.latestPlan && state.latestPlan->revision == displayedRevision_) {
            switch (state.latestPlan->outcome) {
            case PlanOutcome::Applied:
                runtime = QString::fromUtf8("已应用");
                break;
            case PlanOutcome::Pending:
                runtime = QString::fromUtf8("等待依赖/父项");
                break;
            case PlanOutcome::Failed:
                runtime = QString::fromUtf8("执行失败");
                break;
            case PlanOutcome::Superseded:
                runtime = QString::fromUtf8("已替换");
                break;
            case PlanOutcome::Cancelled:
                runtime = QString::fromUtf8("已取消");
                break;
            }
        }
        summary_->setText(QString::fromUtf8("运行：%1 · R%2").arg(runtime).arg(displayedRevision_));
        auto persisted = state.store.external    ? QString::fromUtf8("保存冲突")
                         : state.store.writing   ? QString::fromUtf8("保存中")
                         : state.store.lastError ? QString::fromUtf8("保存失败")
                         : state.store.dirty     ? QString::fromUtf8("未保存")
                         : state.store.persistedRevision
                             ? QString::fromUtf8("已保存 R%1").arg(*state.store.persistedRevision)
                             : QString::fromUtf8("尚未保存");
        saveStatus_->setText(QString::fromUtf8("磁盘：%1").arg(persisted));
        saveStatus_->setToolTip(
            state.store.external
                ? QString::fromUtf8("审阅磁盘原文后可采用冲突基线，再显式保存；当前目标和编辑仍被保留。")
                : QString::fromUtf8("保存仅写入已接受的配置目标；尚未应用的编辑不会写盘。"));
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
                                        entry.second.disabled            ? QString::fromUtf8("停用")
                                        : entry.second.effectiveDisabled ? QString::fromUtf8("父项停用")
                                                                         : QString::fromUtf8("启用"),
                                        actual == running.end() ? QString::fromUtf8("未运行")
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
                                          (matches_ ? QString{} : QString::fromUtf8(" · 请先应用或使用已应用目标"))
                                    : QString::fromUtf8("选择条目调整本地启用状态"));
        {
            QSignalBlocker blocker(enabled_);
            enabled_->setIsToggled(selected && !entry->second.disabled);
        }
        QString detail = message_;
        if (draft_) {
            detail += QString::fromUtf8("\n候选基于修订 %1（目标差异）\n").arg(draft_->baseRevision());
            for (const auto& row : draft_->changes().entries)
                detail += QString::fromStdString(row.path) + " : " + changeName(row.kind) + "\n";
        }
        if (state.latestPlan) {
            detail += QString::fromUtf8("\n最近执行：修订 %1 · %2\n")
                          .arg(state.latestPlan->revision)
                          .arg(state.latestPlan->outcome == PlanOutcome::Applied   ? QString::fromUtf8("已应用")
                               : state.latestPlan->outcome == PlanOutcome::Pending ? QString::fromUtf8("等待依赖/父项")
                               : state.latestPlan->outcome == PlanOutcome::Failed  ? QString::fromUtf8("失败")
                                                                                   : QString::fromUtf8("已替代或取消"));
            for (const auto& error : state.latestPlan->errors)
                detail += describe(error) + "\n";
        }
        if (state.coordinator.runningError)
            detail += "\n" + describe(*state.coordinator.runningError);
        if (auto found = running.find(path_); found != running.end() && found->second.failure)
            detail += "\n" + describe(*found->second.failure);
        if (state.store.lastError)
            detail += "\n" + describe(*state.store.lastError);
        if (state.store.external)
            detail += QString::fromUtf8("\n磁盘冲突原文（保留当前目标，不自动采用）：\n") +
                      QString::fromStdString(state.store.external->text);
        diskLoad_->setEnabled(false);
        if (auto source = watcher_.lock()) {
            auto disk = source->snapshot();
            if (!disk.hasValue())
                diskStatus_->setText(describe(disk.error()));
            else {
                displayedDiskSequence_ = disk.value().changeSequence;
                auto text = disk.value().closed || disk.value().closing
                                ? QString::fromUtf8("文件监听已关闭。")
                                : QString::fromUtf8("监听磁盘变化；不会自动覆盖编辑或应用目标。");
                if (disk.value().external) {
                    const auto& external = *disk.value().external;
                    text = external.version.exists ? QString::fromUtf8("检测到磁盘修改，可显式载入编辑区。")
                                                   : QString::fromUtf8("磁盘文件已删除；载入会清空编辑区。");
                    detail +=
                        QString::fromUtf8("\n监听到的磁盘原文（尚未采用）：\n") + QString::fromStdString(external.text);
                    diskLoad_->setEnabled(!state.closing && !disk.value().closing && !disk.value().closed &&
                                          !disk.value().deliveryBlocked);
                }
                if (disk.value().error)
                    text += "\n" + describe(*disk.value().error);
                diskStatus_->setText(text);
            }
        } else
            diskStatus_->setText(QString::fromUtf8("文件监听不可用。"));
        if (detail.isEmpty())
            detail = QString::fromUtf8("先检查配置差异，再应用到工作台；运行结果和磁盘保存分别反馈。");
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
        enabled_->setAccessibleName(QString::fromUtf8("启用所选配置条目"));
        enabled_->setToolTip(!selected   ? QString::fromUtf8("请选择一个配置条目。")
                             : !matches_ ? QString::fromUtf8("请先应用编辑，或还原到已应用目标，再调整启用状态。")
                             : !open || togglePending_
                                 ? QString::fromUtf8("当前操作在途或会话已关闭，请等待状态稳定。")
                                 : QString::fromUtf8("修改所选条目的期望启用状态，保存需另行操作。"));
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
    ElaPlainTextEdit *editor_, *details_;
    ElaTableView* table_;
    QStandardItemModel* model_;
    ElaToggleSwitch* enabled_;
    QTimer* timer_;
    std::optional<ConfigurationDraft> draft_;
    std::optional<Task<SaveReport>> saving_;
    std::string path_;
    QString message_, fingerprint_;
    std::uint64_t displayedRevision_ = 0, editorRevision_ = 0;
    unsigned frames_ = 0;
    bool deferred_ = false, refreshing_ = false, matches_ = false, togglePending_ = false;
};
} // namespace
/** @brief 工厂本身不接受目标，返回页面交由已有页面 effect 的完整销毁屏障托管。 */
std::unique_ptr<QWidget> createConfigurationPage(const std::shared_ptr<loader::ConfigurationSession>& session,
                                                 std::string text,
                                                 std::shared_ptr<loader::QtConfigurationWatcher> watcher)
{
    if (!qApp || QThread::currentThread() != qApp->thread() || !session || !session->snapshot().hasValue())
        throw std::invalid_argument("configuration page requires application thread and owner session");
    return std::make_unique<ConfigurationPanel>(session, std::move(text), std::move(watcher));
}
} // namespace cordis::ui
