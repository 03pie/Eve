/** @file management.cpp
 * @brief 以宿主值快照驱动管理页，排队校验停止/重试，不把管理能力交给普通插件。
 */
#include "appearance_p.hpp"
#include "fixed_tabs_p.hpp"
#include "management_p.hpp"
#include "status_p.hpp"
#include <ElaComboBox.h>
#include <ElaDialog.h>
#include <ElaLineEdit.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cordis::ui::detail {
/** @brief 将运行状态显示为面向管理操作的名称。 */
QString stateName(InstanceState state)
{
    switch (state) {
    case InstanceState::Pending: return QString::fromUtf8("等待依赖");
    case InstanceState::Loading: return QString::fromUtf8("启动中");
    case InstanceState::Active: return QString::fromUtf8("运行中");
    case InstanceState::Unloading: return QString::fromUtf8("清理中");
    case InstanceState::Failed: return QString::fromUtf8("失败");
    case InstanceState::Disposed: return QString::fromUtf8("已终结");
    }
    return QString::fromUtf8("未知");
}
/** @brief 错误及有限深度原因链按纯文本呈现，不解释插件输入为富文本。 */
QString errorText(const ErrorInfo& error, unsigned depth = 0)
{
    QString text = QString::fromStdString(error.message.empty() ? "未提供错误说明" : error.message);
    if (!error.phase.empty()) text += " [" + QString::fromStdString(error.phase) + "]";
    if (error.operation.valid()) text += QString::fromUtf8(" 操作 %1").arg(error.operation.value());
    if (depth < 4) for (std::size_t i = 0; i < error.causes.size() && i < 8; ++i)
        text += "\n" + QString(static_cast<int>(depth + 1) * 2, QLatin1Char(' ')) + errorText(error.causes[i], depth + 1);
    return text;
}
/** @brief 完整服务键包含主版本及精确 realm，避免把不同隔离域混为缺失。 */
QString keyText(const ServiceKey& key)
{
    return QString::fromStdString(key.name) + QString("/v%1 @%2").arg(key.interfaceMajor).arg(key.realm.value());
}
/** @brief 服务生命周期展示业务名称，避免要求使用者记忆枚举编号。 */
QString bindingName(BindingState state)
{
    switch (state) {
    case BindingState::Staged: return QString::fromUtf8("待发布");
    case BindingState::Published: return QString::fromUtf8("已发布");
    case BindingState::Retiring: return QString::fromUtf8("退役中");
    case BindingState::Released: return QString::fromUtf8("已释放");
    }
    return QString::fromUtf8("未知");
}
/** @brief 操作完成不等于激活成功，显示真实稳定结果供用户判断。 */
QString outcomeName(StableOutcome outcome)
{
    switch (outcome) {
    case StableOutcome::Active: return QString::fromUtf8("已启动");
    case StableOutcome::PendingMissingDependency: return QString::fromUtf8("等待依赖");
    case StableOutcome::Disabled: return QString::fromUtf8("已停用");
    case StableOutcome::Failed: return QString::fromUtf8("失败");
    case StableOutcome::Disposed: return QString::fromUtf8("已终结");
    case StableOutcome::Superseded: return QString::fromUtf8("已被新操作替代");
    }
    return QString::fromUtf8("未知");
}
/** @brief 操作记录使用纯文本；未派发前不把无效零值编号显示成内核操作。 */
QString operationText(const ManagementOperation& operation)
{
    QString text = QString("#%1 ").arg(operation.request) +
                   (operation.action == ManagementAction::Stop         ? QString::fromUtf8("停止")
                    : operation.action == ManagementAction::Load       ? QString::fromUtf8("加载")
                    : operation.action == ManagementAction::SetEnabled ? QString::fromUtf8("配置启停")
                    : operation.action == ManagementAction::Reconcile  ? QString::fromUtf8("重新协调")
                                                                       : QString::fromUtf8("重试"));
    if (!operation.configurationPath.empty()) {
        text += QString::fromStdString(operation.configurationPath) +
                QString::fromUtf8(" 基准修订=%1 ").arg(operation.expectedDesiredRevision);
        if (operation.pending)
            text += QString::fromUtf8("等待完成");
        else if (operation.error)
            text += errorText(*operation.error);
        else if (operation.configurationResult) {
            text += QString::fromUtf8("目标修订=%1 ").arg(operation.configurationResult->revision);
            switch (operation.configurationResult->outcome) {
            case ConfigurationOutcome::Applied:
                text += QString::fromUtf8("已应用");
                break;
            case ConfigurationOutcome::Pending:
                text += QString::fromUtf8("等待依赖/父项");
                break;
            case ConfigurationOutcome::Failed:
                text += QString::fromUtf8("失败");
                break;
            case ConfigurationOutcome::Superseded:
                text += QString::fromUtf8("已被新目标替代");
                break;
            case ConfigurationOutcome::Cancelled:
                text += QString::fromUtf8("已取消");
                break;
            }
        }
        return text + "\n";
    }
    text += QString::fromUtf8(" 实例=%1 ").arg(operation.instance.value());
    text += operation.pending ? QString::fromUtf8("等待完成") : operation.stable ? outcomeName(operation.stable->outcome) : operation.error ? QString::fromUtf8("失败") : QString::fromUtf8("已完成");
    if (operation.deliveryBlocked) text += QString::fromUtf8("（投递受阻）");
    text += operation.operation.valid() ? QString::fromUtf8(" 操作号=%1\n").arg(operation.operation.value()) : QString::fromUtf8(" 尚未进入内核\n");
    if (operation.error) text += errorText(*operation.error) + "\n";
    return text;
}
/** @brief 仅 worker 投递错误可写 blocked；其他字段和界面都在 owner 修改。 */
struct ManagementRecord {
    ManagementOperation data;
    std::atomic_bool blocked{false};
};
/** @brief 后端弱持 Kernel；Qt 控件由外壳拥有，诊断服务和控制器只借用本控制块。 */
struct ManagementControl : std::enable_shared_from_this<ManagementControl> {
    std::shared_ptr<qt::QtScheduler> scheduler;
    std::weak_ptr<Kernel> kernel;
    bool attached = false;
    bool open = true;
    bool refreshing = false;
    bool initialSelection = true;
    std::size_t viewAccesses = 0;
    Promise<void> uiClosed;
    std::shared_ptr<UiPagesService> pages;
    std::shared_ptr<CommandService> commands;
    std::shared_ptr<NotificationService> notifications;
    QPointer<QWidget> widget;
    QPointer<ElaTableView> table;
    QStandardItemModel* model = nullptr;
    QPointer<QPlainTextEdit> details;
    QPointer<QPlainTextEdit> services;
    QPointer<QPlainTextEdit> notices;
    QPointer<QPlainTextEdit> operationsView;
    QPointer<QLabel> summary;
    QPointer<ElaText> heading;
    QPointer<QLabel> status;
    QPointer<QPushButton> stopButton;
    QPointer<QPushButton> retryButton;
    QPointer<QPushButton> refreshButton;
    QPointer<QTimer> timer;
    std::optional<DiagnosticsSnapshot> displayed;
    std::uint64_t sequence = 0;
    std::deque<std::shared_ptr<ManagementRecord>> operations;
    std::map<InstanceId, std::uint64_t> busy;
    std::map<InstanceId, Instance> stopped;
    std::weak_ptr<IConfigurationManagement> configuration;
    bool configurationAttached = false, readingConfiguration = false;
    std::set<RuntimeId> protectedRuntimes;
    std::optional<ConfigurationManagementSnapshot> lastConfiguration;
    std::map<std::string, std::uint64_t> configurationBusy;
    QPointer<QWidget> configurationTab;
    QPointer<FixedDiagnosticsTabs> tabs;
    bool bindingConfiguration = false;
    QPointer<ElaTableView> configurationTable;
    QStandardItemModel* configurationModel = nullptr;
    QPointer<QLabel> configurationSummary;
    QPointer<QPushButton> configurationEnable, configurationDisable, configurationReconcile;
    std::string configurationSelected;
    QString focusedCard;
    InstanceId focusedInstance;
    QPointer<ElaPlainTextEdit> configurationEditor;
    QPointer<QPushButton> configurationApply, configurationSave, configurationReload;
    std::string editorPath;
    std::uint64_t editorRevision = 0;
    bool editorDirty = false, formPending = false;
    std::atomic_bool formDeliveryBlocked{false};
    QPointer<ElaDialog> addDialog;
    QPointer<ElaLineEdit> addId;
    QPointer<ElaComboBox> addType;
    QPointer<ElaPlainTextEdit> addConfiguration;
    QPointer<QPushButton> addSubmit;
    QPointer<ElaText> addStatus;
    std::uint64_t addRevision = 0;
    /** @brief 仅绑定配置入口时创建第五个标签，未绑定的手动管理界面保持原结构。 */
    void createConfigurationTab()
    {
        ViewAccess access(*this);
        auto panel = new QWidget(tabs);
        configurationTab = panel;
        panel->setObjectName("CordisManagementConfiguration");
        auto layout = new QVBoxLayout(panel);
        auto configSummary = new ElaText(panel);
        configSummary->setTextPixelSize(14);
        configSummary->setTextFormat(Qt::PlainText);
        configSummary->setWordWrap(true);
        configSummary->setObjectName("CordisManagementConfigurationSummary");
        configurationSummary = configSummary;
        layout->addWidget(configSummary);
        auto configTable = new ElaTableView(panel);
        configurationTable = configTable;
        configTable->setObjectName("CordisManagementConfigurationTable");
        configurationModel = new QStandardItemModel(0, 5, configTable);
        configTable->setModel(configurationModel);
        configurationModel->setHorizontalHeaderLabels({QString::fromUtf8("配置路径"), QString::fromUtf8("插件"),
                                                       QString::fromUtf8("期望"), QString::fromUtf8("实际状态"),
                                                       QString::fromUtf8("实例")});
        configTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        configTable->setSelectionMode(QAbstractItemView::SingleSelection);
        configTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        configTable->verticalHeader()->hide();
        configTable->verticalHeader()->setDefaultSectionSize(34);
        configTable->setShowGrid(false);
        configTable->horizontalHeader()->setMinimumSectionSize(80);
        configTable->horizontalHeader()->setStretchLastSection(true);
        configTable->setColumnWidth(0, 220);
        configTable->setColumnWidth(1, 160);
        configTable->setColumnWidth(2, 100);
        configTable->setColumnWidth(3, 130);
        layout->addWidget(configTable, 1);
        auto actions = new QHBoxLayout;
        auto enable = new ActionButton(QString::fromUtf8("启用条目"), panel);
        configurationEnable = enable;
        enable->setObjectName("CordisManagementConfigurationEnable");
        auto disable = new ActionButton(QString::fromUtf8("停用条目"), panel);
        configurationDisable = disable;
        disable->setObjectName("CordisManagementConfigurationDisable");
        auto retry = new ActionButton(QString::fromUtf8("重新协调目标"), panel);
        configurationReconcile = retry;
        retry->setObjectName("CordisManagementConfigurationReconcile");
        for (auto button : {enable, disable, retry}) {
            button->setEnabled(false);
            actions->addWidget(button);
        }
        actions->addStretch();
        layout->addLayout(actions);
        auto editor = new ElaPlainTextEdit(panel);
        editor->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}");
        configurationEditor = editor;
        editor->setObjectName("CordisPluginConfigurationEditor");
        editor->setPlaceholderText(QString::fromUtf8("当前插件的 YAML / JSON 配置；应用后仍需显式保存。"));
        editor->setMinimumHeight(120);
        editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        layout->addWidget(editor, 1);
        auto editActions = new QHBoxLayout;
        configurationApply = new ActionButton(QString::fromUtf8("应用配置"), panel);
        configurationSave = new ActionButton(QString::fromUtf8("保存目标到磁盘"), panel);
        configurationReload = new ActionButton(QString::fromUtf8("重新载入当前配置"), panel);
        configurationApply->setObjectName("CordisPluginConfigurationApply");
        configurationSave->setObjectName("CordisPluginConfigurationSave");
        configurationReload->setObjectName("CordisPluginConfigurationReload");
        for (auto button : {configurationApply, configurationSave, configurationReload})
            editActions->addWidget(button);
        layout->addLayout(editActions);
        std::weak_ptr<ManagementControl> weak = shared_from_this();
        QObject::connect(editor, &QPlainTextEdit::textChanged, panel, [weak] {
            if (auto self = weak.lock(); self && self->open) {
                ViewAccess access(*self);
                self->editorDirty = true;
                self->renderEditor();
            }
        });
        QObject::connect(configurationApply, &QPushButton::clicked, panel, [weak] {
            if (auto self = weak.lock())
                self->editConfiguration(false);
        });
        QObject::connect(configurationSave, &QPushButton::clicked, panel, [weak] {
            if (auto self = weak.lock())
                self->saveConfiguration();
        });
        QObject::connect(configurationReload, &QPushButton::clicked, panel, [weak] {
            if (auto self = weak.lock(); self && self->open && !self->formPending) {
                ViewAccess access(*self);
                self->editorDirty = false;
                self->editorPath.clear();
                self->refreshFromSignal();
            }
        });
        QObject::connect(
            configTable->selectionModel(), &QItemSelectionModel::currentChanged, panel,
            [weak](const QModelIndex& index) {
                if (auto self = weak.lock()) {
                    self->configurationSelected =
                        index.isValid()
                            ? self->configurationModel->index(index.row(), 0).data().toString().toStdString()
                            : std::string{};
                    self->refreshFromSignal();
                }
            });
        auto request = [weak](std::optional<bool> enabled) {
            if (auto self = weak.lock()) {
                ViewAccess access(*self);
                if (!self->open || !self->displayed || !self->displayed->configuration)
                    return;
                const auto path = self->configurationSelected;
                const auto revision = self->displayed->configuration->revision;
                auto result = self->requestConfiguration(path, enabled, revision);
                if (!result.hasValue() && self->open)
                    self->status->setText(errorText(result.error()));
            }
        };
        QObject::connect(enable, &QPushButton::clicked, panel, [request] { request(true); });
        QObject::connect(disable, &QPushButton::clicked, panel, [request] { request(false); });
        QObject::connect(retry, &QPushButton::clicked, panel, [request] { request({}); });
        tabs->addTab(panel, QString::fromUtf8("配置条目"));
        if (open)
            tabs->setCurrentWidget(panel);
    }
    /** @brief 只在 owner 读取非原子账目，关闭后可读取诊断但不可发起操作。 */
    Result<void> thread() const
    {
        if (!scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "management requires owner thread"));
        return Result<void>::success();
    }
    /** @brief 当前 UI 调用返回之前不允许外壳销毁控件，保护 Qt 模型信号中的关闭重入。 */
    struct ViewAccess {
        ManagementControl& owner;
        /** @brief 方法调用方须持有控制块直到本 guard 析构。 */
        explicit ViewAccess(ManagementControl& control) : owner(control) { ++owner.viewAccesses; }
        /** @brief 最后一个界面写入栈返回后，才能发布 UI 静止。 */
        ~ViewAccess() { if (--owner.viewAccesses == 0 && !owner.open) owner.uiClosed.resolve(); }
    };
    /** @brief 编辑草稿不会被定时刷新覆盖；修订变化交给 CAS 或显式重载处理。 */
    void renderEditor()
    {
        if (!open || !configurationEditor)
            return;
        const auto source = lastConfiguration;
        std::optional<ManagedConfigurationEntry> entry;
        if (source)
            for (const auto& row : source->entries)
                if (row.path == configurationSelected)
                    entry = row;
        if (entry && source && !editorDirty && (editorPath != entry->path || editorRevision != source->revision)) {
            editorPath = entry->path;
            editorRevision = source->revision;
            const QSignalBlocker blocker(configurationEditor);
            configurationEditor->setPlainText(QString::fromStdString(entry->configuration));
        }
        if (!open)
            return;
        const bool usable =
            source && source->open && source->canEdit && entry && !formPending && !formDeliveryBlocked.load();
        configurationEditor->setReadOnly(!usable);
        configurationApply->setEnabled(usable && editorDirty && editorPath == configurationSelected);
        configurationSave->setEnabled(usable && source->dirty && !source->writing && !editorDirty);
        configurationReload->setEnabled(entry && !formPending);
        configurationApply->setToolTip(
            QString::fromUtf8("校验并应用此条目的配置；其他条目和子树保持。修订变化时请重新载入。"));
        configurationSave->setToolTip(QString::fromUtf8("保存整个当前配置目标；尚未应用的编辑不会写入磁盘。"));
    }
    /** @brief 回执只在 owner 写入 UI，投递失败通过原子标志留在下一次刷新诊断。 */
    template <class T, class Handler> void observeForm(const Task<T>& task, Handler handler)
    {
        std::weak_ptr<ManagementControl> weak = shared_from_this();
        auto receipt = task.thenOn(scheduler, [weak, handler](const Result<T>& result) {
            if (auto self = weak.lock(); self && self->open) {
                ViewAccess access(*self);
                self->formPending = false;
                handler(*self, result);
                if (!self->open)
                    return;
                if (self->addSubmit)
                    self->addSubmit->setEnabled(true);
                self->refreshFromSignal();
            }
        });
        cordis::detail::TaskAccess::observe(receipt, [weak](Task<void>::Snapshot result) {
            if (!result->hasValue())
                if (auto self = weak.lock())
                    self->formDeliveryBlocked.store(true);
        });
    }
    /** @brief 固定草稿与 CAS 修订；配置变更和保存始终是两项明确操作。 */
    void editConfiguration(bool adding)
    {
        if (!open || formPending)
            return;
        ViewAccess access(*this);
        auto provider = configuration.lock();
        auto target = kernel.lock();
        if (!provider || !target || target->snapshot().value().closing)
            return;
        const auto path = configurationSelected;
        const auto text = (adding ? addConfiguration : configurationEditor)->toPlainText().toStdString();
        const auto revision = adding ? addRevision : editorRevision;
        const auto id = adding ? addId->text().toStdString() : std::string{};
        const auto plugin = adding ? addType->currentText().toStdString() : std::string{};
        formPending = true;
        if (addSubmit)
            addSubmit->setEnabled(false);
        renderEditor();
        if (!open)
            return;
        auto accepted = [&]() -> Result<Task<ConfigurationActionReport>> {
            try {
                return adding ? provider->addInstance(id, plugin, text, revision)
                              : provider->updateConfiguration(path, text, revision);
            } catch (...) {
                return Result<Task<ConfigurationActionReport>>::failure(
                    makeError(ErrorCode::CallbackFailed, "configuration callback failed"));
            }
        }();
        if (!open)
            return;
        if (!accepted.hasValue()) {
            formPending = false;
            auto label = adding ? addStatus : QPointer<ElaText>(qobject_cast<ElaText*>(status.data()));
            if (label)
                label->setText(errorText(accepted.error()));
            if (open && addSubmit)
                addSubmit->setEnabled(true);
            if (open)
                renderEditor();
            return;
        }
        observeForm(
            accepted.value(), [adding, path](ManagementControl& self, const Result<ConfigurationActionReport>& result) {
                const bool accepted = result.hasValue();
                auto label = adding ? self.addStatus : QPointer<ElaText>(qobject_cast<ElaText*>(self.status.data()));
                QString message = !accepted ? errorText(result.error())
                                            : QString::fromUtf8("配置目标已接受，运行状态见插件卡片；尚未保存。");
                if (accepted) {
                    if (result.value().outcome == ConfigurationOutcome::Failed)
                        message = QString::fromUtf8("目标已接受，但运行失败；请从卡片查看原因或重试。");
                    else if (result.value().outcome == ConfigurationOutcome::Superseded)
                        message = QString::fromUtf8("本次协调已被更新的配置目标替代，请查看当前卡片状态。");
                    else if (result.value().outcome == ConfigurationOutcome::Cancelled)
                        message = QString::fromUtf8("当前协调已取消，请查看最新配置目标和运行状态。");
                    if (result.value().error)
                        message += "\n" + errorText(*result.value().error);
                }
                if (label)
                    label->setText(message);
                if (!self.open)
                    return;
                if (accepted && !adding && self.configurationSelected == path) {
                    self.editorDirty = false;
                    self.editorRevision = result.value().revision;
                }
                if (accepted && adding) {
                    self.addRevision = result.value().revision;
                    if (!result.value().error && (result.value().outcome == ConfigurationOutcome::Applied ||
                                                  result.value().outcome == ConfigurationOutcome::Pending))
                        self.addDialog->hide();
                }
            });
    }
    /** @brief 保存失败原样反馈，窗口继续保留草稿及冲突恢复入口。 */
    void saveConfiguration()
    {
        if (!open || formPending || editorDirty || !lastConfiguration)
            return;
        ViewAccess access(*this);
        auto provider = configuration.lock();
        if (!provider)
            return;
        const auto revision = lastConfiguration->revision;
        auto target = kernel.lock();
        if (!target || target->snapshot().value().closing)
            return;
        auto saving = [&]() -> Result<Task<void>> {
            try {
                return provider->saveConfiguration(revision);
            } catch (...) {
                return Result<Task<void>>::failure(
                    makeError(ErrorCode::CallbackFailed, "configuration save callback failed"));
            }
        }();
        if (!open)
            return;
        if (!saving.hasValue()) {
            status->setText(errorText(saving.error()));
            return;
        }
        formPending = true;
        renderEditor();
        observeForm(saving.value(), [](ManagementControl& self, const Result<void>& result) {
            self.status->setText(result.hasValue() ? QString::fromUtf8("当前配置目标已保存。")
                                                   : errorText(result.error()));
        });
    }
    /** @brief 复制 core/UI 值快照，不获取服务对象或运行命令谓词。 */
    Result<DiagnosticsSnapshot> capture()
    {
        auto checked = thread();
        if (!checked.hasValue()) return Result<DiagnosticsSnapshot>::failure(checked.error());
        auto target = kernel.lock();
        if (!target) return Result<DiagnosticsSnapshot>::failure(makeError(ErrorCode::ContextClosing, "management kernel unavailable"));
        auto core = target->snapshot();
        if (!core.hasValue()) return Result<DiagnosticsSnapshot>::failure(core.error());
        auto bindings = target->services().snapshot();
        auto pageRows = pages->snapshot();
        auto commandRows = commands->snapshot();
        auto history = notifications->snapshot();
        if (!bindings.hasValue()) return Result<DiagnosticsSnapshot>::failure(bindings.error());
        if (!pageRows.hasValue()) return Result<DiagnosticsSnapshot>::failure(pageRows.error());
        if (!commandRows.hasValue()) return Result<DiagnosticsSnapshot>::failure(commandRows.error());
        if (!history.hasValue()) return Result<DiagnosticsSnapshot>::failure(history.error());
        DiagnosticsSnapshot output{open, core.value(), {}, bindings.value(), pageRows.value(), commandRows.value(), history.value(), {}};
        for (const auto& snapshot : core.value().instances) {
            if (protectedRuntimes.count(snapshot.runtime))
                continue;
            auto instance = target->findInstance(snapshot.instance);
            if (!instance.hasValue()) return Result<DiagnosticsSnapshot>::failure(instance.error());
            auto dependencies = instance.value().dependencies();
            auto operation = instance.value().operation();
            if (!dependencies.hasValue()) return Result<DiagnosticsSnapshot>::failure(dependencies.error());
            if (!operation.hasValue()) return Result<DiagnosticsSnapshot>::failure(operation.error());
            std::string name = "未命名插件";
            for (const auto& runtime : core.value().names) if (runtime.id == snapshot.runtime) { name = runtime.name; break; }
            const bool pending = busy.count(snapshot.instance) != 0;
            const bool actionable = open && scheduler->isOpen() && !core.value().closing && !pending;
            ManagedInstanceSnapshot row{snapshot, std::move(name), dependencies.value(), operation.value().id, {}, pending,
                actionable && snapshot.state != InstanceState::Unloading && snapshot.state != InstanceState::Disposed,
                actionable && snapshot.state == InstanceState::Failed && snapshot.resources.effects.empty() && snapshot.children == 0 && snapshot.committedLeases == 0};
            if (auto result = operation.value().target.tryResult(); result && result->hasValue()) row.stable = result->value();
            output.instances.push_back(std::move(row));
        }
        for (const auto& operation : operations) {
            auto value = operation->data;
            value.deliveryBlocked = operation->blocked.load();
            output.operations.push_back(std::move(value));
        }
        // 仅保留控制器亲自停止的实例；终结对象不再携带激活闭包，缓存有界。
        for (const auto& entry : stopped) {
            auto snapshot = entry.second.snapshot();
            if (!snapshot.hasValue() || snapshot.value().state != InstanceState::Disposed)
                continue;
            if (protectedRuntimes.count(snapshot.value().runtime))
                continue;
            std::string name;
            for (const auto& runtime : core.value().names)
                if (runtime.id == snapshot.value().runtime)
                    name = runtime.name;
            auto operation = entry.second.operation().value();
            ManagedInstanceSnapshot row{
                snapshot.value(), std::move(name), {}, operation.id, {}, busy.count(entry.first) != 0, false, false};
            if (auto result = operation.target.tryResult(); result && result->hasValue())
                row.stable = result->value();
            row.canLoad = open && scheduler->isOpen() && !core.value().closing && !row.busy &&
                          !snapshot.value().deliveryBlocked && !snapshot.value().resources.deliveryBlocked;
            output.instances.push_back(std::move(row));
        }
        if (configurationAttached) {
            auto source = readConfiguration();
            if (source.hasValue())
                lastConfiguration = source.value();
            else
                output.configurationError = source.error();
            output.configuration = lastConfiguration.value_or(ConfigurationManagementSnapshot{});
            auto& configured = *output.configuration;
            configured.open =
                configured.open && open && scheduler->isOpen() && !core.value().closing && source.hasValue();
            for (auto& row : configured.entries) {
                row.busy = row.busy || configurationBusy.count(row.path);
                row.canSetEnabled = row.canSetEnabled && configured.open && !row.busy;
                row.canReconcile = row.canReconcile && configured.open && !row.busy;
            }
        }
        output.managementOpen = open;
        if (!open)
            for (auto& row : output.instances)
                row.canStop = row.canRetry = row.canLoad = false;
        return Result<DiagnosticsSnapshot>::success(std::move(output));
    }
    /** @brief 外部宿主适配器可关闭/抛错，读取失败保留值缓存但不允许手动降级。 */
    Result<ConfigurationManagementSnapshot> readConfiguration()
    {
        if (readingConfiguration)
            return Result<ConfigurationManagementSnapshot>::failure(
                makeError(ErrorCode::ConfigConflict, "configuration snapshot reentered"));
        auto provider = configuration.lock();
        if (!provider)
            return Result<ConfigurationManagementSnapshot>::failure(
                makeError(ErrorCode::ContextClosing, "configuration management expired"));
        readingConfiguration = true;
        struct Reset {
            bool& value; /** @brief 异常路径恢复读取门禁。 */
            ~Reset() { value = false; }
        } reset{readingConfiguration};
        try {
            return provider->snapshot();
        } catch (const std::exception& error) {
            return Result<ConfigurationManagementSnapshot>::failure(makeError(ErrorCode::CallbackFailed, error.what()));
        } catch (...) {
            return Result<ConfigurationManagementSnapshot>::failure(
                makeError(ErrorCode::CallbackFailed, "configuration snapshot threw"));
        }
    }
    /** @brief 管理页和 Home 使用同一配置快照与权限，不根据是否有实例临时切换管理模式。 */
    void renderConfiguration(const DiagnosticsSnapshot& data)
    {
        if (!open || !configurationTab)
            return;
        configurationEnable->setEnabled(false);
        configurationDisable->setEnabled(false);
        configurationReconcile->setEnabled(false);
        for (auto button : {configurationEnable, configurationDisable, configurationReconcile})
            button->setToolTip(QString::fromUtf8("请选择可管理的配置条目；状态不可用或会话关闭时不能操作。"));
        if (!data.configuration)
            return;
        const auto& source = *data.configuration;
        auto text =
            QString::fromUtf8("配置目标修订 %1 · 已保存 %2 · %3")
                .arg(source.revision)
                .arg(source.persistedRevision ? QString::number(*source.persistedRevision) : QString::fromUtf8("无"))
                .arg(source.dirty ? QString::fromUtf8("未保存") : QString::fromUtf8("已同步"));
        if (source.writing)
            text += QString::fromUtf8(" · 保存中");
        if (data.configurationError)
            text += "\n" + errorText(*data.configurationError);
        else if (source.error)
            text += "\n" + errorText(*source.error);
        configurationSummary->setText(text);
        QSignalBlocker blocker(configurationTable->selectionModel());
        configurationModel->setRowCount(static_cast<int>(source.entries.size()));
        if (!open)
            return;
        int selected = -1;
        for (std::size_t i = 0; i < source.entries.size(); ++i) {
            const auto& row = source.entries[i];
            if (row.path == configurationSelected)
                selected = static_cast<int>(i);
            QString actual = data.configurationError || (!row.hasActual && row.error) ? QString::fromUtf8("状态不可用")
                             : row.waitingForParent                                   ? QString::fromUtf8("等待父项")
                             : row.hasActual                                          ? stateName(row.state)
                                                                                      : QString::fromUtf8("未运行");
            QStringList columns{QString::fromStdString(row.path), QString::fromStdString(row.plugin),
                                !row.enabled            ? QString::fromUtf8("停用")
                                : !row.effectiveEnabled ? QString::fromUtf8("父项停用")
                                                        : QString::fromUtf8("启用"),
                                actual, row.instance.valid() ? QString::number(row.instance.value()) : QString("—")};
            for (int j = 0; j < columns.size(); ++j) {
                auto item = configurationModel->item(static_cast<int>(i), j);
                if (!item) {
                    item = new QStandardItem(columns[j]);
                    item->setToolTip(columns[j]);
                    configurationModel->setItem(static_cast<int>(i), j, item);
                } else if (item->text() != columns[j]) {
                    configurationModel->setData(configurationModel->index(static_cast<int>(i), j), columns[j]);
                    if (!open)
                        return;
                    configurationModel->setData(configurationModel->index(static_cast<int>(i), j), columns[j],
                                                Qt::ToolTipRole);
                }
                if (!open)
                    return;
                if (j == 3) {
                    auto presentation = row.hasActual ? instanceStatus(row.state, static_cast<bool>(row.error))
                                                      : StatusPresentation{actual, ElaIconType::Hourglass};
                    if (!row.effectiveEnabled)
                        presentation.icon = ElaIconType::CircleStop;
                    if (data.configurationError || (!row.hasActual && row.error))
                        presentation.icon = ElaIconType::TriangleExclamation;
                    configurationModel->setData(configurationModel->index(static_cast<int>(i), j),
                                                statusIcon(presentation), Qt::DecorationRole);
                    if (!open)
                        return;
                    configurationModel->setData(configurationModel->index(static_cast<int>(i), j), actual,
                                                Qt::AccessibleDescriptionRole);
                    if (!open)
                        return;
                }
            }
        }
        if (selected >= 0) {
            configurationTable->setCurrentIndex(configurationModel->index(selected, 0));
            configurationTable->selectRow(selected);
            const auto row = source.entries[static_cast<std::size_t>(selected)];
            if (row.error)
                configurationSummary->setText(text + "\n" + errorText(*row.error));
            if (!open)
                return;
            configurationEnable->setEnabled(row.canSetEnabled && !row.enabled);
            configurationDisable->setEnabled(row.canSetEnabled && row.enabled);
            configurationReconcile->setEnabled(row.canReconcile);
            const auto unavailable =
                row.busy ? QString::fromUtf8("配置操作正在进行，请等待当前目标稳定。")
                         : QString::fromUtf8("当前条目不允许修改，请检查会话、运行和清理状态。初始化后可再次刷新。");
            configurationEnable->setToolTip(row.enabled ? QString::fromUtf8("此条目已经启用。")
                                            : row.canSetEnabled
                                                ? QString::fromUtf8("启用所选条目，保存到磁盘需另行操作。")
                                                : unavailable);
            configurationDisable->setToolTip(!row.enabled ? QString::fromUtf8("此条目已经停用。")
                                             : row.canSetEnabled
                                                 ? QString::fromUtf8("停用所选条目，保存到磁盘需另行操作。")
                                                 : unavailable);
            configurationReconcile->setToolTip(
                row.canReconcile ? QString::fromUtf8("重新协调当前完整配置目标，重试失败条目。")
                                 : QString::fromUtf8("仅对当前可重试的失败条目开放，不绕过配置目标操作实例。"));
        } else {
            configurationSelected.clear();
            configurationTable->clearSelection();
            configurationTable->setCurrentIndex({});
        }
    }
    /** @brief 选择绑定的是不可变实例号，刷新/排序不把操作转移给另一行。 */
    InstanceId selected() const
    {
        if (!focusedCard.isEmpty())
            return focusedInstance;
        if (!table || !table->selectionModel()) return {};
        const auto rows = table->selectionModel()->selectedRows(0);
        if (rows.size() != 1) return {};
        auto item = model->item(rows.front().row(), 0);
        return item ? InstanceId(item->data(Qt::UserRole).toULongLong()) : InstanceId{};
    }
    /** @brief 只渲染已复制详情；所有文字控件均使用纯文本。 */
    void renderSelection()
    {
        if (!open || !widget) return;
        ViewAccess access(*this);
        // setEnabled 等 Qt 调用可重入刷新；详情使用独立值副本，不借用 displayed 的迭代器。
        const auto local = displayed;
        stopButton->setEnabled(false);
        retryButton->setEnabled(false);
        stopButton->setToolTip(QString::fromUtf8("请选择可停止的实例；操作中或清理受阻时不能重复提交。"));
        retryButton->setToolTip(QString::fromUtf8("请选择已失败且可重试的实例；配置条目请通过配置目标管理。"));
        if (!local) return;
        const auto id = selected();
        const auto& rows = local->instances;
        auto found = std::find_if(rows.begin(), rows.end(), [id](const ManagedInstanceSnapshot& row) { return row.instance.instance == id; });
        if (found == rows.end()) {
            QString text;
            if (!focusedCard.isEmpty() && lastConfiguration)
                for (const auto& row : lastConfiguration->entries)
                    if (row.path == configurationSelected) {
                        text = QString::fromStdString(row.plugin) +
                               QString::fromUtf8("\n配置路径：%1\n实际状态：%2\n实例：%3  "
                                                 "代次：%4\n由配置目标管理，使用配置页操作启停和重试。")
                                   .arg(QString::fromStdString(row.path))
                                   .arg(row.hasActual ? stateName(row.state) : QString::fromUtf8("尚无实例"))
                                   .arg(row.instance.value())
                                   .arg(row.epoch);
                        if (row.error)
                            text += "\n" + errorText(*row.error);
                    }
            details->setPlainText(
                text.isEmpty() ? QString::fromUtf8("此实例已离开当前运行目录，请从 Home 打开最新卡片。") : text);
            return;
        }
        stopButton->setEnabled(found->canStop);
        retryButton->setEnabled(found->canRetry);
        if (found->canStop)
            stopButton->setToolTip(QString::fromUtf8("停止所选实例 %1，并等待其资源完整清理。").arg(id.value()));
        if (found->canRetry)
            retryButton->setToolTip(QString::fromUtf8("重试所选失败实例 %1。").arg(id.value()));
        const auto& snapshot = found->instance;
        QString text = QString::fromStdString(found->name) + QString::fromUtf8("\n状态：%1  实例：%2  操作：%3\n配置修订：%4  代次：%5\n")
            .arg(stateName(snapshot.state)).arg(id.value()).arg(found->operation.value()).arg(snapshot.revision).arg(snapshot.epoch);
        if (snapshot.deliveryBlocked || snapshot.resources.deliveryBlocked) text += QString::fromUtf8("调度投递受阻，资源尚未静止。\n");
        if (snapshot.failure) text += QString::fromUtf8("错误：") + errorText(*snapshot.failure) + "\n";
        text += QString::fromUtf8("\n依赖\n");
        if (found->dependencies.empty()) text += QString::fromUtf8("无声明强依赖\n");
        for (const auto& dependency : found->dependencies) {
            text += keyText(dependency.key) + QString(" minor >= %1").arg(dependency.minimumMinor);
            for (const auto& capability : dependency.capabilities) text += " [" + QString::fromStdString(capability) + "]";
            text += dependency.error ? " : " + errorText(*dependency.error) + "\n" : QString::fromUtf8(" : 可用\n");
        }
        text += QString::fromUtf8("\n资源：%1  已提交租约：%2  子实例：%3\n").arg(snapshot.resources.effects.size()).arg(snapshot.committedLeases).arg(snapshot.children);
        for (const auto& effect : snapshot.resources.effects) {
            text += QString::fromStdString(effect.label) + QString::fromUtf8("  初始化等待=%1 清理等待=%2 保留清理=%3\n")
                .arg(effect.setupPending).arg(effect.cleanupPending).arg(effect.retainedCleanups);
            for (const auto& error : effect.errors) text += errorText(error) + "\n";
        }
        for (const auto& error : snapshot.resources.errors) text += errorText(error) + "\n";
        text += QString::fromUtf8("\n管理操作\n");
        for (const auto& operation : local->operations) if (operation.instance == id) text += operationText(operation);
        details->setPlainText(text);
    }
    /** @brief 通知来自独立的宿主服务，无 Kernel 快照也能查看已保留历史。 */
    void renderNotifications()
    {
        if (!open || !notices)
            return;
        const auto values = notifications->snapshot();
        QString text;
        if (!values.hasValue())
            text = errorText(values.error());
        else
            for (const auto& entry : values.value()) {
                const auto level = entry.notification.level == NotificationLevel::Error     ? QString::fromUtf8("错误")
                                   : entry.notification.level == NotificationLevel::Warning ? QString::fromUtf8("提示")
                                                                                            : QString::fromUtf8("消息");
                text += QString("#%1 ").arg(entry.sequence) + level + " · " +
                        QString::fromStdString(entry.notification.text) + "\n";
            }
        if (text.isEmpty())
            text = QString::fromUtf8("暂无通知。插件消息会保留在这里，收到通知不会自动展开底部工作区。");
        if (notices->toPlainText() != text)
            notices->setPlainText(text);
    }
    /** @brief 刷新持有 UI 访问屏障，避免模型信号内重入关闭时删除当前调用栈上的控件。 */
    Result<void> refresh()
    {
        auto checked = thread();
        if (!checked.hasValue()) return checked;
        if (!open || !widget) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed"));
        if (refreshing) return Result<void>::success();
        ViewAccess access(*this);
        refreshing = true;
        /** @brief 异常路径也恢复刷新门禁。 */
        struct RefreshGuard { bool& flag; /** @brief 退出当前刷新。 */ ~RefreshGuard() { flag = false; } } guard{refreshing};
        auto data = capture();
        if (!data.hasValue()) {
            displayed.reset();
            model->setRowCount(0);
            stopButton->setEnabled(false); retryButton->setEnabled(false);
            details->clear();
            services->clear();
            operationsView->clear();
            renderNotifications();
            summary->setText(QString::fromUtf8("管理页未连接可用内核"));
            status->setText(errorText(data.error()));
            if (configurationTab) {
                configurationEnable->setEnabled(false);
                configurationDisable->setEnabled(false);
                configurationReconcile->setEnabled(false);
                configurationSummary->setText(errorText(data.error()));
            }
            return Result<void>::failure(data.error());
        }
        if (!open) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed during refresh"));
        const auto previous = selected();
        displayed.emplace(std::move(data.value()));
        const auto& view = *displayed;
        {
            QSignalBlocker blocker(table->selectionModel());
            model->setRowCount(static_cast<int>(view.instances.size()));
            int chosen = -1;
            for (std::size_t i = 0; i < view.instances.size(); ++i) {
                const auto& row = view.instances[i];
                const auto id = row.instance.instance;
                if (id == previous) chosen = static_cast<int>(i);
                QStringList columns{QString::number(id.value()), QString::fromStdString(row.name), stateName(row.instance.state),
                    QString::number(row.instance.revision), QString::number(row.instance.epoch)};
                for (int column = 0; column < columns.size(); ++column) {
                    auto item = new QStandardItem(columns[column]);
                    item->setToolTip(columns[column]);
                    if (column == 2)
                        decorateStatus(item,
                                       instanceStatus(row.instance.state, static_cast<bool>(row.instance.failure)));
                    if (column == 0) item->setData(QVariant::fromValue<qulonglong>(id.value()), Qt::UserRole);
                    model->setItem(static_cast<int>(i), column, item);
                    if (!open) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed during render"));
                }
            }
            if (chosen < 0 && !previous.valid() && initialSelection && !view.instances.empty()) { chosen = 0; initialSelection = false; }
            if (chosen >= 0) table->selectRow(chosen);
            else { table->clearSelection(); table->setCurrentIndex({}); }
        }
        const bool manualVisible = focusedCard.isEmpty() && (!configurationAttached || !view.instances.empty());
        table->setVisible(manualVisible);
        const bool manualActions =
            manualVisible || (!focusedCard.isEmpty() && !focusedCard.startsWith("configuration:"));
        stopButton->setVisible(manualActions);
        retryButton->setVisible(manualActions);
        if (!open)
            return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed during layout"));
        summary->setText((configurationAttached ? QString::fromUtf8("手动实例 %1  服务绑定 %2  页面 %3  命令 %4%5")
                                                : QString::fromUtf8("顶层实例 %1  服务绑定 %2  页面 %3  命令 %4%5"))
                             .arg(view.instances.size())
                             .arg(view.bindings.size())
                             .arg(view.pages.size())
                             .arg(view.commands.size())
                             .arg(view.kernel.deliveryBlocked ? QString::fromUtf8("  内核投递受阻")
                                  : view.kernel.closing       ? QString::fromUtf8("  内核正在关闭")
                                                              : QString{}));
        QString serviceText;
        for (const auto& binding : view.bindings) serviceText += keyText(binding.key) + QString::fromUtf8("  状态=%1 调用=%2 租约=%3 投递受阻=%4\n")
            .arg(bindingName(binding.state)).arg(binding.activeCalls).arg(binding.committedLeases).arg(binding.deliveryBlocked);
        serviceText += QString::fromUtf8("\n页面贡献\n");
        for (const auto& page : view.pages) serviceText += QString::fromStdString(page.id) + QString::fromUtf8("  实例=%1 代次=%2 撤销=%3 对象存活=%4\n")
            .arg(page.owner.instance.value()).arg(page.owner.epoch).arg(page.retiring).arg(page.widgetAlive);
        serviceText += QString::fromUtf8("\n命令贡献\n");
        for (const auto& command : view.commands) serviceText += QString::fromStdString(command.id) + QString::fromUtf8("  实例=%1 代次=%2 调用保留=%3 撤销=%4 投递受阻=%5\n")
            .arg(command.owner.instance.value()).arg(command.owner.epoch).arg(command.activeCalls).arg(command.retiring).arg(command.deliveryBlocked);
        services->setPlainText(serviceText);
        renderNotifications();
        QString operationHistory;
        for (const auto& operation : view.operations) {
            if (!focusedCard.isEmpty() && operation.instance != focusedInstance &&
                operation.configurationPath != configurationSelected)
                continue;
            operationHistory += operationText(operation) + "\n";
        }
        operationsView->setPlainText(operationHistory);
        renderSelection();
        renderConfiguration(view);
        renderEditor();
        if (open && configurationTable)
            configurationTable->setVisible(focusedCard.isEmpty());
        if (open && heading) {
            QString title = QString::fromUtf8("插件管理");
            QString description;
            if (!focusedCard.isEmpty()) {
                title = QString::fromUtf8("插件对象已失效");
                description = QString::fromUtf8("此身份已离开当前目录，请从 Home 打开最新卡片。");
                if (focusedCard.startsWith("configuration:") && view.configuration) {
                    for (const auto& entry : view.configuration->entries)
                        if (QString::fromStdString(entry.path) == focusedCard.mid(14)) {
                            title = QString::fromStdString(entry.plugin);
                            description =
                                QString::fromUtf8("配置实例 · %1 · %2")
                                    .arg(QString::fromStdString(entry.path),
                                         entry.hasActual ? stateName(entry.state) : QString::fromUtf8("尚无运行实例"));
                        }
                } else
                    for (const auto& entry : view.instances)
                        if (entry.instance.instance == focusedInstance) {
                            title = QString::fromStdString(entry.name);
                            description = QString::fromUtf8("手动实例 #%1 · %2")
                                              .arg(focusedInstance.value())
                                              .arg(stateName(entry.instance.state));
                        }
            }
            heading->setText(title);
            if (open && !focusedCard.isEmpty())
                summary->setText(description);
        }
        if (open && formDeliveryBlocked.load())
            status->setText(QString::fromUtf8("配置回执投递受阻，请完成退出或恢复调度器后重试。"));
        return Result<void>::success();
    }
    /** @brief 原生信号边界隔离刷新异常；错误不得穿越 Qt 事件循环。 */
    void refreshFromSignal()
    {
        try { (void)refresh(); }
        catch (const std::exception&) { if (open && status) status->setText(QString::fromUtf8("无法刷新管理快照")); }
        catch (...) { if (open && status) status->setText(QString::fromUtf8("管理快照刷新异常")); }
    }
    /** @brief 推进失败只写原子诊断，不从 worker 改 busy、历史或 Qt 控件。 */
    void watch(const Task<void>& receipt, const std::shared_ptr<ManagementRecord>& record)
    {
        std::weak_ptr<ManagementRecord> weak = record;
        cordis::detail::TaskAccess::observe(receipt, [weak](Task<void>::Snapshot result) {
            if (!result->hasValue()) if (auto held = weak.lock()) held->blocked.store(true);
        });
    }
    /** @brief 完成只释放对应请求的 busy 锁，晚到结果不会操作关闭后的窗口。 */
    void finish(const std::shared_ptr<ManagementRecord>& record)
    {
        if (record->data.stable && record->data.stable->outcome == StableOutcome::Failed && !record->data.error) {
            auto error = makeError(ErrorCode::ActivationFailed, "activation failed");
            error.instance = record->data.instance; error.operation = record->data.operation;
            if (auto target = kernel.lock()) {
                auto instance = target->findInstance(record->data.instance);
                if (instance.hasValue()) {
                    auto snapshot = instance.value().snapshot();
                    if (snapshot.hasValue() && snapshot.value().failure) error = *snapshot.value().failure;
                }
            }
            record->data.error = std::move(error);
        }
        record->data.pending = false;
        if (!record->data.configurationPath.empty()) {
            auto found = configurationBusy.find(record->data.configurationPath);
            if (found != configurationBusy.end() && found->second == record->data.request)
                configurationBusy.erase(found);
        } else {
            auto found = busy.find(record->data.instance);
            if (found != busy.end() && found->second == record->data.request)
                busy.erase(found);
        }
        if (open && status) {
            status->setText(record->data.error ? errorText(*record->data.error) : QString::fromUtf8("管理操作已完成"));
            refreshFromSignal();
        }
    }
    /** @brief 真正执行前重新读取内核关闭和实例状态，不依赖界面缓存的按钮状态。 */
    void run(const std::shared_ptr<ManagementRecord>& record)
    {
        auto target = kernel.lock();
        if (!open || !target || !scheduler->isOpen()) {
            record->data.error = makeError(ErrorCode::ContextClosing, "management closed before dispatch"); finish(record); return;
        }
        auto snapshot = target->snapshot();
        if (!snapshot.hasValue() || snapshot.value().closing) {
            record->data.error = snapshot.hasValue() ? makeError(ErrorCode::ContextClosing, "kernel closing") : snapshot.error(); finish(record); return;
        }
        auto instance = target->findInstance(record->data.instance);
        if (record->data.action == ManagementAction::Load) {
            auto found = stopped.find(record->data.instance);
            if (found == stopped.end()) {
                record->data.error = makeError(ErrorCode::InstanceDisposed, "stopped instance unavailable");
                finish(record);
                return;
            }
            if (protectedRuntimes.count(found->second.snapshot().value().runtime)) {
                record->data.error =
                    makeError(ErrorCode::ContextClosing, "configuration runtime cannot be loaded manually");
                finish(record);
                return;
            }
            auto mounted = target->remount(found->second);
            if (!mounted.hasValue()) {
                record->data.error = mounted.error();
                finish(record);
                return;
            }
            const auto previous = record->data.instance;
            record->data.previousInstance = previous;
            record->data.instance = mounted.value().snapshot().value().instance;
            busy.erase(previous);
            busy.emplace(record->data.instance, record->data.request);
            stopped.erase(found);
            auto operation = mounted.value().operation().value();
            record->data.operation = operation.id;
            std::weak_ptr<ManagementControl> weak = shared_from_this();
            watch(operation.target.thenOn(scheduler,
                                          [weak, record](const Result<StableReport>& result) {
                                              if (result.hasValue())
                                                  record->data.stable = result.value();
                                              else
                                                  record->data.error = result.error();
                                              if (auto owner = weak.lock())
                                                  owner->finish(record);
                                          }),
                  record);
            return;
        }
        if (!instance.hasValue()) { record->data.error = instance.error(); finish(record); return; }
        if (protectedRuntimes.count(instance.value().snapshot().value().runtime)) {
            record->data.error =
                makeError(ErrorCode::ContextClosing, "configuration runtime requires desired-state management");
            finish(record);
            return;
        }
        std::weak_ptr<ManagementControl> weak = shared_from_this();
        if (record->data.action == ManagementAction::Stop) {
            if (stopped.count(record->data.instance) == 0 && stopped.size() == ManagementController::historyLimit) {
                auto old = std::find_if(stopped.begin(), stopped.end(), [&](const auto& entry) {
                    return busy.count(entry.first) == 0 &&
                           entry.second.snapshot().value().state == InstanceState::Disposed;
                });
                if (old == stopped.end()) {
                    record->data.error = makeError(ErrorCode::ContextClosing, "stopped instance cache full");
                    finish(record);
                    return;
                }
                stopped.erase(old);
            }
            stopped.emplace(record->data.instance, instance.value());
            auto operation = instance.value().operation();
            if (operation.hasValue()) record->data.operation = operation.value().id;
            watch(instance.value().stop().thenOn(scheduler, [weak, record](const Result<CleanupReport>& result) {
                if (result.hasValue()) {
                    record->data.cleanup = result.value();
                    if (!result.value().quiescent) {
                        auto error = makeError(ErrorCode::CleanupFailed, "instance retained resources");
                        error.causes = result.value().errors; record->data.error = std::move(error);
                    }
                } else record->data.error = result.error();
                if (auto owner = weak.lock()) owner->finish(record);
            }), record);
        } else {
            auto operation = instance.value().retry();
            if (!operation.hasValue()) { record->data.error = operation.error(); finish(record); return; }
            record->data.operation = operation.value().id;
            watch(instance.value().awaitTarget(operation.value()).thenOn(scheduler, [weak, record](const Result<StableReport>& result) {
                if (result.hasValue()) record->data.stable = result.value();
                else record->data.error = result.error();
                if (auto owner = weak.lock()) owner->finish(record);
            }), record);
        }
    }
    /** @brief 手动及配置动作共用有界历史，全部在途时不丢弃旧回执。 */
    Result<std::shared_ptr<ManagementRecord>> allocateRecord()
    {
        using Output = Result<std::shared_ptr<ManagementRecord>>;
        if (sequence == std::numeric_limits<std::uint64_t>::max())
            return Output::failure(makeError(ErrorCode::InvalidArgument, "management request ids exhausted"));
        if (operations.size() == ManagementController::historyLimit) {
            auto old =
                std::find_if(operations.begin(), operations.end(), [](const auto& row) { return !row->data.pending; });
            if (old == operations.end())
                return Output::failure(
                    makeError(ErrorCode::ContextClosing, "management history contains only pending operations"));
            operations.erase(old);
        }
        auto record = std::make_shared<ManagementRecord>();
        record->data.request = ++sequence;
        operations.push_back(record);
        return Output::success(record);
    }
    /** @brief 排队后仍检查来源、路径和修订，外部编辑不能使旧开关作用到新目标。 */
    void runConfiguration(const std::shared_ptr<ManagementRecord>& record)
    {
        auto fail = [&](ErrorInfo error) {
            record->data.error = std::move(error);
            finish(record);
        };
        auto target = kernel.lock();
        if (!open || !scheduler->isOpen() || !target || target->snapshot().value().closing) {
            fail(makeError(ErrorCode::ContextClosing, "management closing before configuration dispatch"));
            return;
        }
        auto source = readConfiguration();
        if (!source.hasValue()) {
            fail(source.error());
            return;
        }
        if (source.value().revision != record->data.expectedDesiredRevision) {
            fail(makeError(ErrorCode::ConfigConflict, "configuration changed before dispatch"));
            return;
        }
        auto found = std::find_if(source.value().entries.begin(), source.value().entries.end(),
                                  [&](const auto& row) { return row.path == record->data.configurationPath; });
        if (!open || !source.value().open || found == source.value().entries.end() || found->busy ||
            (record->data.desiredEnabled ? !found->canSetEnabled : !found->canReconcile)) {
            fail(makeError(ErrorCode::ContextClosing, "configuration action unavailable"));
            return;
        }
        auto provider = configuration.lock();
        if (!provider || target->snapshot().value().closing) {
            fail(makeError(ErrorCode::ContextClosing, "configuration management unavailable"));
            return;
        }
        auto task = [&]() -> Result<Task<ConfigurationActionReport>> {
            try {
                if (record->data.desiredEnabled)
                    return provider->setEnabled(record->data.configurationPath, *record->data.desiredEnabled,
                                                record->data.expectedDesiredRevision);
                return provider->reconcile(record->data.configurationPath, record->data.expectedDesiredRevision);
            } catch (const std::exception& error) {
                return Result<Task<ConfigurationActionReport>>::failure(
                    makeError(ErrorCode::CallbackFailed, error.what()));
            } catch (...) {
                return Result<Task<ConfigurationActionReport>>::failure(
                    makeError(ErrorCode::CallbackFailed, "configuration action threw"));
            }
        }();
        if (!task.hasValue()) {
            fail(task.error());
            return;
        }
        std::weak_ptr<ManagementControl> weak = shared_from_this();
        watch(task.value().thenOn(
                  scheduler,
                  [weak, record](const Result<ConfigurationActionReport>& result) {
                      if (!result.hasValue())
                          record->data.error = result.error();
                      else {
                          const auto& report = result.value();
                          const bool valid = report.revision > record->data.expectedDesiredRevision &&
                                             (report.outcome == ConfigurationOutcome::Applied ||
                                              report.outcome == ConfigurationOutcome::Pending ||
                                              report.outcome == ConfigurationOutcome::Failed ||
                                              report.outcome == ConfigurationOutcome::Superseded ||
                                              report.outcome == ConfigurationOutcome::Cancelled);
                          record->data.configurationResult = report;
                          record->data.error = report.error;
                          if (!valid)
                              record->data.error =
                                  makeError(ErrorCode::CallbackFailed, "invalid configuration management receipt");
                          if (result.value().outcome == ConfigurationOutcome::Failed && !record->data.error)
                              record->data.error =
                                  makeError(ErrorCode::ActivationFailed, "configuration execution failed");
                      }
                      if (auto self = weak.lock())
                          self->finish(record);
                  }),
              record);
    }
    /** @brief 控制请求固定路径及当前 CAS，不把未挂载/禁用路径转换成虚构实例。 */
    Result<std::uint64_t> requestConfiguration(const std::string& path, std::optional<bool> enabled,
                                               std::uint64_t expected)
    {
        auto checked = thread();
        if (!checked.hasValue())
            return Result<std::uint64_t>::failure(checked.error());
        if (path.empty())
            return Result<std::uint64_t>::failure(makeError(ErrorCode::InvalidArgument, "configuration path is empty"));
        if (!open || !scheduler->isOpen())
            return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "management closing"));
        auto data = capture();
        if (!data.hasValue())
            return Result<std::uint64_t>::failure(data.error());
        if (!open || data.value().kernel.closing || !data.value().configuration || data.value().configurationError)
            return Result<std::uint64_t>::failure(data.value().configurationError.value_or(
                makeError(ErrorCode::ContextClosing, "configuration management unavailable")));
        const auto& source = *data.value().configuration;
        if (source.revision != expected)
            return Result<std::uint64_t>::failure(
                makeError(ErrorCode::ConfigConflict, "configuration revision changed"));
        auto found = std::find_if(source.entries.begin(), source.entries.end(),
                                  [&](const auto& row) { return row.path == path; });
        if (!source.open || found == source.entries.end() || found->busy ||
            (enabled ? !found->canSetEnabled : !found->canReconcile))
            return Result<std::uint64_t>::failure(
                makeError(ErrorCode::ContextClosing, "configuration action unavailable"));
        auto allocated = allocateRecord();
        if (!allocated.hasValue())
            return Result<std::uint64_t>::failure(allocated.error());
        auto record = allocated.value();
        record->data.configurationPath = path;
        record->data.expectedDesiredRevision = expected;
        record->data.desiredEnabled = enabled;
        record->data.instance = found->instance;
        record->data.action = enabled ? ManagementAction::SetEnabled : ManagementAction::Reconcile;
        configurationBusy.emplace(path, record->data.request);
        std::weak_ptr<ManagementControl> weak = shared_from_this();
        bool accepted = false;
        try {
            accepted = scheduler->post({[weak, record] {
                                            if (auto self = weak.lock())
                                                self->runConfiguration(record);
                                        },
                                        [record] { record->blocked.store(true); }});
        } catch (...) {
        }
        if (!accepted) {
            record->data.error = makeError(ErrorCode::SchedulerClosed, "configuration dispatch rejected");
            finish(record);
        } else if (open)
            refreshFromSignal();
        return Result<std::uint64_t>::success(record->data.request);
    }
    /** @brief 先验证真实快照再锁定实例；历史满且全在途时拒绝，不丢失等待中的诊断。 */
    Result<std::uint64_t> request(InstanceId id, ManagementAction action)
    {
        auto checked = thread();
        if (!checked.hasValue()) return Result<std::uint64_t>::failure(checked.error());
        if (bindingConfiguration)
            return Result<std::uint64_t>::failure(
                makeError(ErrorCode::ConfigConflict, "configuration binding in progress"));
        if (!open || !scheduler->isOpen()) return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "management closing"));
        auto data = capture();
        if (!data.hasValue()) return Result<std::uint64_t>::failure(data.error());
        auto found = std::find_if(data.value().instances.begin(), data.value().instances.end(), [id](const ManagedInstanceSnapshot& row) { return row.instance.instance == id; });
        if (found == data.value().instances.end()) return Result<std::uint64_t>::failure(makeError(ErrorCode::InstanceDisposed, "unknown or disposed instance"));
        if ((action == ManagementAction::Stop && !found->canStop) ||
            (action == ManagementAction::Retry && !found->canRetry) ||
            (action == ManagementAction::Load && !found->canLoad))
            return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "instance busy or action unavailable"));
        auto allocated = allocateRecord();
        if (!allocated.hasValue())
            return Result<std::uint64_t>::failure(allocated.error());
        auto record = allocated.value();
        record->data.instance = id;
        record->data.action = action;
        busy.emplace(id, record->data.request);
        std::weak_ptr<ManagementControl> weak = shared_from_this();
        bool accepted = false;
        try { accepted = scheduler->post({[weak, record] { if (auto owner = weak.lock()) owner->run(record); }, [record] { record->blocked.store(true); }}); }
        catch (...) {}
        if (!accepted) { record->data.error = makeError(ErrorCode::SchedulerClosed, "management dispatch rejected"); finish(record); }
        else if (open) refreshFromSignal();
        return Result<std::uint64_t>::success(record->data.request);
    }
};
/** @brief 接入永久宿主页；控件和刷新计时器均是宿主资源，不存插件回调。 */
std::shared_ptr<ManagementControl> createManagement(QWidget* parent, std::shared_ptr<qt::QtScheduler> scheduler,
    std::shared_ptr<UiPagesService> pages, std::shared_ptr<CommandService> commands, std::shared_ptr<NotificationService> notifications)
{
    auto state = std::make_shared<ManagementControl>();
    state->scheduler = std::move(scheduler); state->pages = std::move(pages); state->commands = std::move(commands); state->notifications = std::move(notifications);
    auto root = new ElaDialog(parent);
    root->setIsStayTop(false);
    root->setWindowTitle(QString::fromUtf8("插件管理"));
    root->resize(1000, 740);
    root->setObjectName("CordisManagement");
    state->widget = root;
    auto layout = new QVBoxLayout(root);
    layout->setContentsMargins(24, 48, 24, 16);
    layout->setSpacing(12);
    auto title = new ElaText(QString::fromUtf8("插件管理"), root);
    title->setTextPixelSize(22);
    title->setIsBold(true);
    layout->addWidget(title);
    state->heading = title;
    auto summary = new ElaText(QString::fromUtf8("管理页未连接内核"), root); summary->setTextPixelSize(14); summary->setObjectName("CordisManagementSummary"); summary->setTextFormat(Qt::PlainText); layout->addWidget(summary); state->summary = summary;
    auto table = new ElaTableView(root);
    state->model = new QStandardItemModel(0, 5, table);
    table->setModel(state->model); table->setObjectName("CordisInstanceTable");
    state->model->setHorizontalHeaderLabels({QString::fromUtf8("实例"), QString::fromUtf8("插件"), QString::fromUtf8("状态"), QString::fromUtf8("配置修订"), QString::fromUtf8("代次")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(34);
    table->setShowGrid(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setMinimumSectionSize(64);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->setColumnWidth(0, 64); table->setColumnWidth(1, 180);
    table->setColumnWidth(2, 110); table->setColumnWidth(3, 110);
    table->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table, 2); state->table = table;
    auto actions = new QHBoxLayout;
    auto refresh = new ActionButton(QString::fromUtf8("刷新"), root);
    refresh->setObjectName("CordisManagementRefresh");
    actions->addWidget(refresh);
    state->refreshButton = refresh;
    auto stop = new ActionButton(QString::fromUtf8("停止实例"), root);
    stop->setObjectName("CordisManagementStop");
    stop->setEnabled(false);
    actions->addWidget(stop);
    state->stopButton = stop;
    auto retry = new ActionButton(QString::fromUtf8("重试失败实例"), root);
    retry->setObjectName("CordisManagementRetry");
    retry->setEnabled(false);
    actions->addWidget(retry);
    state->retryButton = retry;
    actions->addStretch(); layout->addLayout(actions);
    auto tabs = new FixedDiagnosticsTabs(root);
    state->tabs = tabs;
    tabs->setObjectName("CordisManagementTabs");
    layout->addWidget(tabs, 3);
    auto details = new ElaPlainTextEdit(tabs); details->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}"); details->setObjectName("CordisManagementDetails"); details->setReadOnly(true); tabs->addTab(details, QString::fromUtf8("实例详情")); state->details = details;
    auto services = new ElaPlainTextEdit(tabs); services->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}"); services->setObjectName("CordisManagementServices"); services->setReadOnly(true); tabs->addTab(services, QString::fromUtf8("服务与界面")); state->services = services;
    auto notices = new ElaPlainTextEdit(tabs); notices->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}"); notices->setObjectName("CordisManagementNotices"); notices->setReadOnly(true); tabs->addTab(notices, QString::fromUtf8("通知")); state->notices = notices;
    auto operations = new ElaPlainTextEdit(tabs); operations->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}"); operations->setObjectName("CordisManagementOperations"); operations->setReadOnly(true); tabs->addTab(operations, QString::fromUtf8("操作记录")); state->operationsView = operations;
    auto status = new ElaText(root); status->setTextPixelSize(14); status->setObjectName("CordisManagementStatus"); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); layout->addWidget(status); state->status = status;
    auto timer = new QTimer(root); timer->setInterval(250); state->timer = timer;
    std::weak_ptr<ManagementControl> weak = state;
    QObject::connect(timer, &QTimer::timeout, root, [weak] { if (auto owner = weak.lock()) owner->refreshFromSignal(); });
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, root, [weak] {
        if (auto owner = weak.lock(); owner && owner->open)
            owner->refreshFromSignal();
    });
    QObject::connect(refresh, &QPushButton::clicked, root, [weak] { if (auto owner = weak.lock()) owner->refreshFromSignal(); });
    QObject::connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, root, [weak] { if (auto owner = weak.lock()) owner->renderSelection(); });
    QObject::connect(stop, &QPushButton::clicked, root, [weak] { if (auto owner = weak.lock()) {
        auto result = owner->request(owner->selected(), ManagementAction::Stop);
        if (!result.hasValue() && owner->open) owner->status->setText(errorText(result.error()));
    }});
    QObject::connect(retry, &QPushButton::clicked, root, [weak] { if (auto owner = weak.lock()) {
        auto result = owner->request(owner->selected(), ManagementAction::Retry);
        if (!result.hasValue() && owner->open) owner->status->setText(errorText(result.error()));
    }});
    return state;
}
/** @brief 页面所有权始终属于外壳的 Qt 对象树。 */
QWidget* managementWidget(const std::shared_ptr<ManagementControl>& control) { return control->widget; }
/** @brief 卡片身份先检查真实快照；无中央页签、副本内核或独立生命周期。 */
Result<void> showManagementDialog(const std::shared_ptr<ManagementControl>& control, const QString& id,
                                  bool configuration)
{
    auto checked = control->thread();
    if (!checked.hasValue())
        return checked;
    if (!control->open || !control->widget)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed"));
    ManagementControl::ViewAccess access(*control);
    if (control->focusedCard != id && (control->editorDirty || control->formPending)) {
        control->widget->show();
        return Result<void>::failure(
            makeError(ErrorCode::ConfigConflict, "请先应用或重新载入当前配置草稿，再切换管理对象。"));
    }
    auto data = control->capture();
    if (!data.hasValue() && !id.isEmpty())
        return Result<void>::failure(data.error());
    QString title = QString::fromUtf8("插件管理");
    InstanceId instance;
    std::string path;
    if (id.startsWith("configuration:")) {
        path = id.mid(14).toStdString();
        if (!data.value().configuration)
            return Result<void>::failure(makeError(ErrorCode::StaleContext, "配置条目已不可用"));
        const auto& entries = data.value().configuration->entries;
        const auto row =
            std::find_if(entries.begin(), entries.end(), [&](const auto& row) { return row.path == path; });
        if (row == entries.end())
            return Result<void>::failure(makeError(ErrorCode::StaleContext, "配置条目已移除，请从最新卡片打开"));
        instance = row->instance;
        title = QString::fromStdString(row->plugin) + " · " + QString::fromStdString(path);
    } else if (!id.isEmpty()) {
        bool valid = false;
        instance = InstanceId(id.toULongLong(&valid));
        const auto& entries = data.value().instances;
        const auto row = std::find_if(entries.begin(), entries.end(),
                                      [&](const auto& row) { return row.instance.instance == instance; });
        if (!valid || row == entries.end())
            return Result<void>::failure(makeError(ErrorCode::StaleContext, "插件实例已变化，请从最新卡片打开"));
        title = QString::fromStdString(row->name) + QString::fromUtf8(" · 实例 #%1").arg(instance.value());
    }
    control->focusedCard = id;
    control->focusedInstance = instance;
    if (!path.empty())
        control->configurationSelected = path;
    control->initialSelection = false;
    control->refreshFromSignal();
    if (!control->open)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed during refresh"));
    control->widget->setWindowTitle(title);
    for (int index = 0; index < control->tabs->count(); ++index) {
        auto tab = control->tabs->widget(index);
        control->tabs->setTabVisible(index, id.isEmpty() || tab == control->details || tab == control->operationsView ||
                                                (!path.empty() && tab == control->configurationTab));
    }
    control->tabs->setCurrentWidget(configuration && control->configurationTab ? control->configurationTab.data()
                                                                               : control->details.data());
    if (!control->open)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed during tab selection"));
    control->widget->show();
    if (control->open) {
        control->widget->raise();
        control->widget->activateWindow();
    }
    return control->open
               ? Result<void>::success()
               : Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed while showing window"));
}
/** @brief 表单由宿主窗口持有，关闭后保留输入；提交再次检查 schema 和目标修订。 */
Result<void> showAddPluginDialog(const std::shared_ptr<ManagementControl>& control)
{
    auto checked = control->thread();
    if (!checked.hasValue())
        return checked;
    if (!control->open || !control->widget)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed"));
    ManagementControl::ViewAccess access(*control);
    auto data = control->capture();
    if (!data.hasValue())
        return Result<void>::failure(data.error());
    if (!data.value().configuration || !data.value().configuration->open || !data.value().configuration->canEdit ||
        data.value().configuration->availablePlugins.empty())
        return Result<void>::failure(
            makeError(ErrorCode::InvalidArgument, "当前入口未接入可编辑配置会话；请使用配置工作台添加已注册插件。"));
    if (control->formPending)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "配置操作仍在处理中"));
    control->addRevision = data.value().configuration->revision;
    if (!control->addDialog) {
        auto dialog = new ElaDialog(control->widget->parentWidget());
        control->addDialog = dialog;
        dialog->setObjectName("CordisAddPluginDialog");
        dialog->setWindowTitle(QString::fromUtf8("添加插件配置"));
        dialog->setIsStayTop(false);
        dialog->resize(620, 550);
        auto layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(24, 48, 24, 20);
        auto hint = new ElaText(
            QString::fromUtf8("选择已注册插件并创建独立配置实例。添加后应用到运行目标；保存到磁盘需另行操作。"), 14,
            dialog);
        hint->setWordWrap(true);
        hint->setTextFormat(Qt::PlainText);
        layout->addWidget(hint);
        layout->addWidget(new ElaText(QString::fromUtf8("插件类型"), 14, dialog));
        control->addType = new ElaComboBox(dialog);
        control->addType->setObjectName("CordisAddPluginType");
        control->addType->setAccessibleName(QString::fromUtf8("已注册插件类型"));
        for (const auto& name : data.value().configuration->availablePlugins)
            control->addType->addItem(QString::fromStdString(name));
        layout->addWidget(control->addType);
        layout->addWidget(new ElaText(QString::fromUtf8("实例标识（当前配置中唯一）"), 14, dialog));
        control->addId = new ElaLineEdit(dialog);
        control->addId->setObjectName("CordisAddPluginId");
        control->addId->setAccessibleName(QString::fromUtf8("插件配置实例标识"));
        layout->addWidget(control->addId);
        layout->addWidget(new ElaText(QString::fromUtf8("插件配置 · YAML / JSON"), 14, dialog));
        control->addConfiguration = new ElaPlainTextEdit(dialog);
        control->addConfiguration->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}");
        control->addConfiguration->setObjectName("CordisAddPluginConfiguration");
        control->addConfiguration->setPlainText("{}");
        layout->addWidget(control->addConfiguration, 1);
        control->addStatus = new ElaText(dialog);
        control->addStatus->setTextPixelSize(13);
        control->addStatus->setTextFormat(Qt::PlainText);
        control->addStatus->setWordWrap(true);
        layout->addWidget(control->addStatus);
        auto reload = new ActionButton(QString::fromUtf8("更新目标修订后重试"), dialog);
        reload->setObjectName("CordisAddPluginReload");
        layout->addWidget(reload);
        auto submit = new ActionButton(QString::fromUtf8("添加并应用"), dialog);
        primaryButton(submit);
        control->addSubmit = submit;
        control->addSubmit->setObjectName("CordisAddPluginSubmit");
        layout->addWidget(control->addSubmit);
        std::weak_ptr<ManagementControl> weak = control;
        QObject::connect(control->addSubmit, &QPushButton::clicked, dialog, [weak] {
            if (auto self = weak.lock())
                self->editConfiguration(true);
        });
        QObject::connect(reload, &QPushButton::clicked, dialog, [weak] {
            if (auto self = weak.lock()) {
                auto result = showAddPluginDialog(self);
                if (!result.hasValue() && self->open && self->addStatus)
                    self->addStatus->setText(errorText(result.error()));
            }
        });
    }
    control->addStatus->setText(QString::fromUtf8("基于配置目标修订 %1").arg(control->addRevision));
    if (!control->open)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed during form creation"));
    control->addDialog->show();
    if (control->open) {
        control->addDialog->raise();
        control->addDialog->activateWindow();
    }
    return Result<void>::success();
}
/** @brief 保留调用控制块，管理视图访问屏障处理信号重入。 */
Result<void> refreshManagement(const std::shared_ptr<ManagementControl>& control) { return control->refresh(); }
/** @brief 根窗口访问由调用方保留，管理页同时保留自己的视图写入栈。 */
Result<void> showManagementNotifications(const std::shared_ptr<ManagementControl>& control)
{
    if (!control->open || !control->widget)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "notification view closed"));
    ManagementControl::ViewAccess access(*control);
    control->renderNotifications();
    if (!control->open || !control->tabs || !control->notices)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "notification view closed during render"));
    control->tabs->setTabVisible(control->tabs->indexOf(control->notices), true);
    control->tabs->setCurrentWidget(control->notices);
    if (!control->open)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "notification view closed during selection"));
    control->widget->setWindowTitle(QString::fromUtf8("通知历史"));
    control->widget->show();
    if (control->open) {
        control->widget->raise();
        control->widget->activateWindow();
    }
    return control->open
               ? Result<void>::success()
               : Result<void>::failure(makeError(ErrorCode::ContextClosing, "notification view closed while showing"));
}
/** @brief 单图标入口仍经过管理请求的线程、排队、实例忙及关闭门禁。 */
Result<void> setManagementEnabled(const std::shared_ptr<ManagementControl>& control, InstanceId instance, bool enabled)
{
    auto snapshot = control->capture();
    if (!snapshot.hasValue())
        return Result<void>::failure(snapshot.error());
    if (!control->open || !control->scheduler->isOpen())
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closing"));
    const auto& rows = snapshot.value().instances;
    auto found = std::find_if(rows.begin(), rows.end(),
                              [instance](const auto& row) { return row.instance.instance == instance; });
    if (found == rows.end())
        return Result<void>::failure(makeError(ErrorCode::InstanceDisposed, "plugin instance unavailable"));
    if (!found->busy && snapshot.value().managementOpen && !snapshot.value().kernel.closing &&
        ((enabled &&
          (found->instance.state == InstanceState::Active || found->instance.state == InstanceState::Pending ||
           found->instance.state == InstanceState::Loading)) ||
         (!enabled && found->instance.state == InstanceState::Disposed)))
        return Result<void>::success();
    if ((enabled && !found->canRetry && !found->canLoad) || (!enabled && !found->canStop))
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "plugin action unavailable"));
    auto result = control->request(instance, !enabled          ? ManagementAction::Stop
                                             : found->canRetry ? ManagementAction::Retry
                                                               : ManagementAction::Load);
    return result.hasValue() ? Result<void>::success() : Result<void>::failure(result.error());
}
/** @brief 配置路径动作与手动实例动作共享管理历史和关闭门禁。 */
Result<void> setManagementConfigurationEnabled(const std::shared_ptr<ManagementControl>& control,
                                               const std::string& path, bool enabled, std::uint64_t revision)
{
    auto result = control->requestConfiguration(path, enabled, revision);
    return result.hasValue() ? Result<void>::success() : Result<void>::failure(result.error());
}
/** @brief 失效配置源不能通过贡献兜底重新获得手动操作。 */
bool hideManagementContribution(const std::shared_ptr<ManagementControl>& control, InstanceId instance)
{
    if (!control->configurationAttached)
        return false;
    if (control->lastConfiguration)
        for (const auto& row : control->lastConfiguration->entries)
            if (row.instance == instance)
                return true;
    auto kernel = control->kernel.lock();
    if (!kernel)
        return true;
    auto found = kernel->findInstance(instance);
    if (found.hasValue())
        return control->protectedRuntimes.count(found.value().snapshot().value().runtime) != 0;
    return control->configuration.expired();
}
/** @brief 管理操作结果只持宿主记录；关闭等待当前 UI 写入栈返回，不取消已进入的内核操作。 */
Task<void> closeManagement(const std::shared_ptr<ManagementControl>& control)
{
    if (!control->open) return control->uiClosed.task();
    ManagementControl::ViewAccess access(*control);
    control->open = false;
    if (control->timer) control->timer->stop();
    if (control->widget) control->widget->setEnabled(false);
    if (control->addDialog) {
        control->addDialog->setEnabled(false);
        control->addDialog->hide();
    }
    return control->uiClosed.task();
}
} // namespace cordis::ui::detail

namespace cordis::ui {
/** @brief 只读服务不授予停止/重试能力，Kernel 生命周期独立于服务。 */
Result<DiagnosticsSnapshot> DiagnosticsService::snapshot() const
{
    auto control = control_.lock();
    if (!control) return Result<DiagnosticsSnapshot>::failure(makeError(ErrorCode::ContextClosing, "management expired"));
    return control->capture();
}
/** @brief 宿主只绑定一次，禁止重定向尚有排队请求的管理控制器。 */
Result<void> ManagementController::attach(const std::shared_ptr<Kernel>& kernel) const
{
    auto control = control_.lock();
    if (!control) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management expired"));
    auto checked = control->thread();
    if (!checked.hasValue()) return checked;
    if (!control->open || !control->scheduler->isOpen()) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closing"));
    if (!kernel || !kernel->usesScheduler(control->scheduler)) return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "management requires kernel on same scheduler"));
    if (control->attached) return control->kernel.lock() == kernel ? Result<void>::success() : Result<void>::failure(makeError(ErrorCode::ServiceConflict, "management already attached"));
    control->kernel = kernel; control->attached = true;
    control->timer->start();
    return control->refresh();
}
/** @brief 配置绑定不可替换，定义在真实 Kernel 中验证且不能接管在途手动请求。 */
Result<void> ManagementController::attachConfiguration(const std::shared_ptr<IConfigurationManagement>& provider,
                                                       std::vector<RuntimeId> runtimes) const
{
    auto state = control_.lock();
    if (!state)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management expired"));
    auto checked = state->thread();
    if (!checked.hasValue())
        return checked;
    auto kernel = state->kernel.lock();
    if (!state->open || !state->scheduler->isOpen() || !kernel || state->bindingConfiguration)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "configuration binding unavailable"));
    if (!provider || runtimes.empty())
        return Result<void>::failure(
            makeError(ErrorCode::InvalidArgument, "configuration binding requires provider and protected runtimes"));
    std::set<RuntimeId> protectedIds(runtimes.begin(), runtimes.end());
    if (protectedIds.size() != runtimes.size())
        return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "duplicate protected runtime"));
    if (state->configurationAttached)
        return state->configuration.lock() == provider && state->protectedRuntimes == protectedIds
                   ? Result<void>::success()
                   : Result<void>::failure(
                         makeError(ErrorCode::ServiceConflict, "configuration management already attached"));
    auto core = kernel->snapshot().value();
    if (core.closing)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "kernel closing"));
    for (auto id : protectedIds)
        if (!id.valid() ||
            std::none_of(core.names.begin(), core.names.end(), [&](const auto& row) { return row.id == id; }))
            return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "foreign protected runtime"));
    for (const auto& row : state->operations)
        if (row->data.pending) {
            auto found = kernel->findInstance(row->data.instance);
            auto old = state->stopped.find(row->data.instance);
            if ((found.hasValue() && protectedIds.count(found.value().snapshot().value().runtime)) ||
                (old != state->stopped.end() && protectedIds.count(old->second.snapshot().value().runtime)))
                return Result<void>::failure(
                    makeError(ErrorCode::ConfigConflict, "manual request still pending for protected runtime"));
        }
    state->bindingConfiguration = true;
    struct Reset {
        bool& flag; /** @brief 所有返回路径恢复绑定门禁。 */
        ~Reset() { flag = false; }
    } reset{state->bindingConfiguration};
    auto initial = [&]() -> Result<ConfigurationManagementSnapshot> {
        try {
            return provider->snapshot();
        } catch (const std::exception& error) {
            return Result<ConfigurationManagementSnapshot>::failure(makeError(ErrorCode::CallbackFailed, error.what()));
        } catch (...) {
            return Result<ConfigurationManagementSnapshot>::failure(
                makeError(ErrorCode::CallbackFailed, "configuration binding snapshot threw"));
        }
    }();
    if (!initial.hasValue())
        return Result<void>::failure(initial.error());
    if (!state->open || kernel->snapshot().value().closing)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed during binding"));
    detail::ManagementControl::ViewAccess access(*state);
    state->protectedRuntimes = std::move(protectedIds);
    state->configuration = provider;
    state->configurationAttached = true;
    state->lastConfiguration = initial.value();
    state->createConfigurationTab();
    return state->refresh();
}
/** @brief 配置启停不依赖是否存在有效 InstanceId。 */
Result<std::uint64_t> ManagementController::setConfigurationEnabled(const std::string& path, bool enabled,
                                                                    std::uint64_t revision) const
{
    auto state = control_.lock();
    return state ? state->requestConfiguration(path, enabled, revision)
                 : Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "management expired"));
}
/** @brief 失败配置重试通过完整目标收敛，不使用手动实例重试。 */
Result<std::uint64_t> ManagementController::reconcileConfiguration(const std::string& path,
                                                                   std::uint64_t revision) const
{
    auto state = control_.lock();
    return state ? state->requestConfiguration(path, {}, revision)
                 : Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "management expired"));
}
/** @brief 本地保留宿主控制块，Qt 信号重入释放 facade 也不会访问悬空对象。 */
Result<void> ManagementController::refresh() const
{
    auto control = control_.lock();
    if (!control) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management expired"));
    return control->refresh();
}
/** @brief 实例号在接受时固定，执行前仍会检查内核和目标状态。 */
Result<std::uint64_t> ManagementController::stop(InstanceId instance) const
{
    auto control = control_.lock();
    if (!control) return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "management expired"));
    return control->request(instance, ManagementAction::Stop);
}
/** @brief 重试使用内核真实操作号观察结果，未完成时重复请求会被 busy 拒绝。 */
Result<std::uint64_t> ManagementController::retry(InstanceId instance) const
{
    auto control = control_.lock();
    if (!control) return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "management expired"));
    return control->request(instance, ManagementAction::Retry);
}
/** @brief 加载沿用相同的 owner、排队、去重和关闭门禁。 */
Result<std::uint64_t> ManagementController::load(InstanceId instance) const
{
    auto control = control_.lock();
    if (!control)
        return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "management expired"));
    return control->request(instance, ManagementAction::Load);
}
} // namespace cordis::ui
