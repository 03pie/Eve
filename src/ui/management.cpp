/** @file management.cpp
 * @brief 以宿主值快照驱动管理页，排队校验停止/重试，不把管理能力交给普通插件。
 */
#include "management_p.hpp"
#include "fixed_tabs_p.hpp"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <ElaTableView.h>
#include <ElaText.h>
#include <ElaPushButton.h>
#include <ElaPlainTextEdit.h>
#include <QStandardItemModel>
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
    QString text = QString("#%1 ").arg(operation.request) + (operation.action == ManagementAction::Stop ? QString::fromUtf8("停止") : QString::fromUtf8("重试"));
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
    QPointer<QLabel> status;
    QPointer<QPushButton> stopButton;
    QPointer<QPushButton> retryButton;
    QPointer<QPushButton> refreshButton;
    QPointer<QTimer> timer;
    std::optional<DiagnosticsSnapshot> displayed;
    std::uint64_t sequence = 0;
    std::deque<std::shared_ptr<ManagementRecord>> operations;
    std::map<InstanceId, std::uint64_t> busy;
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
        return Result<DiagnosticsSnapshot>::success(std::move(output));
    }
    /** @brief 选择绑定的是不可变实例号，刷新/排序不把操作转移给另一行。 */
    InstanceId selected() const
    {
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
        if (!local) return;
        const auto id = selected();
        const auto& rows = local->instances;
        auto found = std::find_if(rows.begin(), rows.end(), [id](const ManagedInstanceSnapshot& row) { return row.instance.instance == id; });
        if (found == rows.end()) { details->clear(); return; }
        stopButton->setEnabled(found->canStop);
        retryButton->setEnabled(found->canRetry);
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
            details->clear(); services->clear(); notices->clear(); operationsView->clear();
            summary->setText(QString::fromUtf8("管理页未连接可用内核"));
            status->setText(errorText(data.error()));
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
                    if (column == 0) item->setData(QVariant::fromValue<qulonglong>(id.value()), Qt::UserRole);
                    model->setItem(static_cast<int>(i), column, item);
                    if (!open) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "management closed during render"));
                }
            }
            if (chosen < 0 && !previous.valid() && initialSelection && !view.instances.empty()) { chosen = 0; initialSelection = false; }
            if (chosen >= 0) table->selectRow(chosen);
            else { table->clearSelection(); table->setCurrentIndex({}); }
        }
        summary->setText(QString::fromUtf8("顶层实例 %1  服务绑定 %2  页面 %3  命令 %4%5")
            .arg(view.instances.size()).arg(view.bindings.size()).arg(view.pages.size()).arg(view.commands.size())
            .arg(view.kernel.deliveryBlocked ? QString::fromUtf8("  内核投递受阻") : view.kernel.closing ? QString::fromUtf8("  内核正在关闭") : QString{}));
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
        QString noticeText;
        for (const auto& notice : view.notifications) noticeText += QString("#%1 ").arg(notice.sequence) + QString::fromStdString(notice.notification.text) + "\n";
        notices->setPlainText(noticeText);
        QString operationHistory;
        for (const auto& operation : view.operations) operationHistory += operationText(operation) + "\n";
        operationsView->setPlainText(operationHistory);
        renderSelection();
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
            auto error = makeError(ErrorCode::ActivationFailed, "retry failed");
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
        auto found = busy.find(record->data.instance);
        if (found != busy.end() && found->second == record->data.request) busy.erase(found);
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
        if (!instance.hasValue()) { record->data.error = instance.error(); finish(record); return; }
        std::weak_ptr<ManagementControl> weak = shared_from_this();
        if (record->data.action == ManagementAction::Stop) {
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
    /** @brief 先验证真实快照再锁定实例；历史满且全在途时拒绝，不丢失等待中的诊断。 */
    Result<std::uint64_t> request(InstanceId id, ManagementAction action)
    {
        auto checked = thread();
        if (!checked.hasValue()) return Result<std::uint64_t>::failure(checked.error());
        if (!open || !scheduler->isOpen()) return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "management closing"));
        auto data = capture();
        if (!data.hasValue()) return Result<std::uint64_t>::failure(data.error());
        auto found = std::find_if(data.value().instances.begin(), data.value().instances.end(), [id](const ManagedInstanceSnapshot& row) { return row.instance.instance == id; });
        if (found == data.value().instances.end()) return Result<std::uint64_t>::failure(makeError(ErrorCode::InstanceDisposed, "unknown or disposed instance"));
        if ((action == ManagementAction::Stop && !found->canStop) || (action == ManagementAction::Retry && !found->canRetry))
            return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "instance busy or action unavailable"));
        if (sequence == std::numeric_limits<std::uint64_t>::max()) return Result<std::uint64_t>::failure(makeError(ErrorCode::InvalidArgument, "management request ids exhausted"));
        if (operations.size() == ManagementController::historyLimit) {
            auto old = std::find_if(operations.begin(), operations.end(), [](const auto& record) { return !record->data.pending; });
            if (old == operations.end()) return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "management history contains only pending operations"));
            operations.erase(old);
        }
        auto record = std::make_shared<ManagementRecord>();
        record->data.request = ++sequence; record->data.instance = id; record->data.action = action;
        operations.push_back(record);
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
    auto root = new QWidget(parent); root->setObjectName("CordisManagement"); state->widget = root;
    auto layout = new QVBoxLayout(root);
    auto title = new ElaText(QString::fromUtf8("插件管理"), root); title->setTextPixelSize(22); title->setIsBold(true); layout->addWidget(title);
    auto summary = new ElaText(QString::fromUtf8("管理页未连接内核"), root); summary->setTextPixelSize(14); summary->setObjectName("CordisManagementSummary"); summary->setTextFormat(Qt::PlainText); layout->addWidget(summary); state->summary = summary;
    auto table = new ElaTableView(root);
    state->model = new QStandardItemModel(0, 5, table);
    table->setModel(state->model); table->setObjectName("CordisInstanceTable");
    state->model->setHorizontalHeaderLabels({QString::fromUtf8("实例"), QString::fromUtf8("插件"), QString::fromUtf8("状态"), QString::fromUtf8("配置修订"), QString::fromUtf8("代次")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setMinimumSectionSize(64);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->setColumnWidth(0, 64); table->setColumnWidth(1, 180);
    table->setColumnWidth(2, 110); table->setColumnWidth(3, 110);
    table->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table, 2); state->table = table;
    auto actions = new QHBoxLayout;
    auto refresh = new ElaPushButton(QString::fromUtf8("刷新"), root); refresh->setObjectName("CordisManagementRefresh"); actions->addWidget(refresh); state->refreshButton = refresh;
    auto stop = new ElaPushButton(QString::fromUtf8("停止实例"), root); stop->setObjectName("CordisManagementStop"); stop->setEnabled(false); actions->addWidget(stop); state->stopButton = stop;
    auto retry = new ElaPushButton(QString::fromUtf8("重试失败实例"), root); retry->setObjectName("CordisManagementRetry"); retry->setEnabled(false); actions->addWidget(retry); state->retryButton = retry;
    actions->addStretch(); layout->addLayout(actions);
    auto tabs = new FixedDiagnosticsTabs(root); tabs->setObjectName("CordisManagementTabs"); layout->addWidget(tabs, 3);
    auto details = new ElaPlainTextEdit(tabs); details->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}"); details->setObjectName("CordisManagementDetails"); details->setReadOnly(true); tabs->addTab(details, QString::fromUtf8("实例详情")); state->details = details;
    auto services = new ElaPlainTextEdit(tabs); services->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}"); services->setObjectName("CordisManagementServices"); services->setReadOnly(true); tabs->addTab(services, QString::fromUtf8("服务与界面")); state->services = services;
    auto notices = new ElaPlainTextEdit(tabs); notices->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}"); notices->setObjectName("CordisManagementNotices"); notices->setReadOnly(true); tabs->addTab(notices, QString::fromUtf8("通知")); state->notices = notices;
    auto operations = new ElaPlainTextEdit(tabs); operations->setStyleSheet("ElaPlainTextEdit{background-color:transparent;}"); operations->setObjectName("CordisManagementOperations"); operations->setReadOnly(true); tabs->addTab(operations, QString::fromUtf8("操作记录")); state->operationsView = operations;
    auto status = new ElaText(root); status->setTextPixelSize(14); status->setObjectName("CordisManagementStatus"); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); layout->addWidget(status); state->status = status;
    auto timer = new QTimer(root); timer->setInterval(250); state->timer = timer;
    std::weak_ptr<ManagementControl> weak = state;
    QObject::connect(timer, &QTimer::timeout, root, [weak] { if (auto owner = weak.lock()) owner->refreshFromSignal(); });
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
/** @brief 保留调用控制块，管理视图访问屏障处理信号重入。 */
Result<void> refreshManagement(const std::shared_ptr<ManagementControl>& control) { return control->refresh(); }
/** @brief 管理操作结果只持宿主记录；关闭等待当前 UI 写入栈返回，不取消已进入的内核操作。 */
Task<void> closeManagement(const std::shared_ptr<ManagementControl>& control)
{
    if (!control->open) return control->uiClosed.task();
    ManagementControl::ViewAccess access(*control);
    control->open = false;
    if (control->timer) control->timer->stop();
    if (control->widget) control->widget->setEnabled(false);
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
} // namespace cordis::ui
