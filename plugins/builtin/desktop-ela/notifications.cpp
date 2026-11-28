/** @file notifications.cpp
 * @brief 有界通知候选值、纯历史与状态栏呈现，不持有插件对象。
 */
#include "notifications_p.hpp"
#include "desktop_archive.hpp"
#include "appearance_p.hpp"
#include "theme_palette_p.hpp"
#include "workbench_focus_p.hpp"
#include <ElaListView.h>
#include <ElaMessageBar.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QShortcut>
#include <QSettings>
#include <QMap>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTextDocument>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <deque>
#include <limits>
#include <future>

namespace eve::ui::detail {
namespace
{
struct AcceptedNotice {
    NotificationSnapshot value;
    QDateTime received;
    QString source;
    std::uint64_t event = 0;
    bool read = false;
    bool announce = false;
    QString run;
    quint64 archivedEvent = 0;
    bool sameGroup(const NotificationSnapshot& other) const
    {
        return value.owner.instance == other.owner.instance && value.owner.epoch == other.owner.epoch &&
               value.notification.operation == other.notification.operation &&
               value.notification.level == other.notification.level &&
               value.notification.text == other.notification.text;
    }
};
QString levelName(NotificationLevel level)
{
    if (level == NotificationLevel::Error)
        return QStringLiteral("错误");
    if (level == NotificationLevel::Warning)
        return QStringLiteral("警告");
    return QStringLiteral("消息");
}
// This is a child of the workbench, so opening it never creates another top-level window.
class NoticePanel final : public QWidget
{
public:
    explicit NoticePanel(QWidget* window) : QWidget(window)
    {
        setObjectName("EveNotificationCenter");
        setAccessibleName(QStringLiteral("通知中心"));
        bindWorkbenchPalette(this, ElaThemeType::PopupBase);
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        auto header = new QHBoxLayout;
        header->addWidget(new ElaText(QStringLiteral("通知中心"), 18, this), 1);
        closeButton = new ActionButton(QStringLiteral("关闭"), this);
        closeButton->setObjectName("EveNotificationClose");
        closeButton->setAccessibleName(QStringLiteral("关闭通知中心"));
        header->addWidget(closeButton);
        layout->addLayout(header);
        status = new ElaText(this);
        status->setObjectName("EveNotificationArchiveStatus");
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        status->hide();
        layout->addWidget(status);
        list = new ElaListView(this);
        list->setObjectName("EveNotificationList");
        list->setAccessibleName(QStringLiteral("通知列表"));
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        list->setWordWrap(true);
        list->setItemHeight(60);
        list->setUniformItemSizes(false);
        model = new QStandardItemModel(list);
        list->setModel(model);
        layout->addWidget(list, 1);
        empty = new ElaText(QStringLiteral("暂无通知"), 14, this);
        empty->setObjectName("EveNotificationEmpty");
        empty->setAlignment(Qt::AlignCenter);
        secondaryText(empty);
        layout->addWidget(empty, 1);
        auto tools = new QHBoxLayout;
        markRead = new ActionButton(QStringLiteral("全部标为已读"), this);
        markRead->setObjectName("EveNotificationMarkRead");
        openLog = new ActionButton(QStringLiteral("查看相关日志"), this);
        openLog->setObjectName("EveNotificationOpenLog");
        tools->addWidget(markRead);
        tools->addStretch();
        tools->addWidget(openLog);
        layout->addLayout(tools);
        window->installEventFilter(this);
        position();
        hide();
    }
    void position()
    {
        auto window = parentWidget();
        const int w = std::min(420, std::max(1, window->width() - 16));
        const int h = std::min(370, std::max(1, window->height() - 86));
        setGeometry(std::max(0, window->width() - w - 8), std::max(0, window->height() - h - 34), w, h);
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        drawWorkbenchPopup(painter, rect());
    }
    bool eventFilter(QObject* watched, QEvent* e) override
    {
        if (watched == parentWidget() && e->type() == QEvent::Resize)
            position();
        if (watched == parentWidget() && e->type() == QEvent::DynamicPropertyChange &&
            static_cast<QDynamicPropertyChangeEvent*>(e)->propertyName() == "EveReducedMotion" &&
            parentWidget()->property("EveReducedMotion").toBool()) {
            const auto bars = parentWidget()->findChildren<ElaMessageBar*>(QString(), Qt::FindDirectChildrenOnly);
            for (auto bar : bars) {
                bar->hide();
                bar->deleteLater();
            }
        }
        return QWidget::eventFilter(watched, e);
    }
    ElaListView* list;
    ElaText *status, *empty;
    QStandardItemModel* model;
    ActionButton *closeButton, *openLog, *markRead;
};
}
struct NotificationControl : std::enable_shared_from_this<NotificationControl> {
    std::shared_ptr<qt::QtScheduler> scheduler;
    QPointer<QLabel> notification;
    std::shared_ptr<OutputControl> logs;
    WindowAccess access;
    PendingValues<NotificationSnapshot> pending{NotificationService::pendingCountLimit,
                                                NotificationService::pendingByteLimit,
                                                NotificationService::pendingTargetLimit};
    std::deque<AcceptedNotice> history;
    QPointer<NoticePanel> panel;
    WorkbenchReturnPoint origin;
    QPointer<QToolButton> bell;
    std::function<QString(InstanceId)> sourceName;
    bool muted = false;
    std::shared_ptr<DesktopArchive> archive;
    QString archiveWarning, readWarning;
    bool faultUnread = false, announceFault = false;
    std::deque<AcceptedNotice> historical;
    std::future<std::deque<AcceptedNotice>> loading;
    std::optional<ErrorInfo> deliveryError;
    std::uint64_t sequence = 0;
    bool open = true;
    bool dirty = false;
    bool rendering = false;

    Result<void> thread() const
    {
        if (!scheduler->isCurrentThread())
            return Result<void>::failure(makeError(ErrorCode::WrongThread, "notifications require owner thread"));
        return Result<void>::success();
    }
    Result<void> business() const
    {
        const auto checked = thread();
        if (!checked.hasValue())
            return checked;
        if (!scheduler->isOpen())
            return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "notification scheduler closed"));
        if (!open || !notification)
            return Result<void>::failure(makeError(ErrorCode::ContextClosing, "notifications closed"));
        return Result<void>::success();
    }
    /** @brief 所有提交共用一次校验，UTF-8 与输出镜像一致。 */
    Result<void> validate(const Notification& value) const
    {
        if (value.text.empty() || value.text.size() > NotificationService::textLimit ||
            (value.level != NotificationLevel::Info && value.level != NotificationLevel::Warning &&
             value.level != NotificationLevel::Error) ||
            sequence == std::numeric_limits<std::uint64_t>::max())
            return Result<void>::failure(
                makeError(ErrorCode::InvalidArgument, "invalid notification or exhausted sequence"));
        if (QString::fromUtf8(value.text.data(), static_cast<int>(value.text.size())).toUtf8().toStdString() !=
            value.text)
            return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "notification requires UTF-8 text"));
        return Result<void>::success();
    }
    /** @brief 先修改纯值历史并镜像；调用此入口不触发 Qt 信号。 */
    void admit(NotificationSnapshot value)
    {
        const auto owner = value.owner;
        const auto logText = value.notification.text;
        const auto level = value.notification.level;
        std::uint64_t event = 0;
        if (logs) {
            auto mirrored = appendOutput(logs, owner, "通知", logText,
                                         level == NotificationLevel::Error     ? OutputLevel::Error
                                         : level == NotificationLevel::Warning ? OutputLevel::Warning
                                                                               : OutputLevel::Info,
                                         false, value.notification.operation);
            if (!mirrored.hasValue() && !deliveryError)
                deliveryError = mirrored.error();
            if (mirrored.hasValue())
                event = mirrored.value();
        }
        const bool announce =
            level != NotificationLevel::Info && !muted &&
            std::none_of(history.begin(), history.end(), [&](const auto& row) { return row.sameGroup(value); });
        const auto position =
            std::lower_bound(history.begin(), history.end(), value.sequence,
                             [](const auto& entry, std::uint64_t seq) { return entry.value.sequence < seq; });
        const auto name = owner.instance.valid() && sourceName ? sourceName(owner.instance) : QString{};
        history.insert(position, {std::move(value), QDateTime::currentDateTime(), name, event, false, announce,
                                  archive ? archive->currentRun() : QString{}});
        while (history.size() > NotificationService::historyLimit)
            history.pop_front();
        dirty = true;
    }
    std::vector<AcceptedNotice*> visibleEntries()
    {
        std::vector<AcceptedNotice*> rows;
        const auto append = [&](auto& entries) {
            for (auto it = entries.rbegin(); it != entries.rend() && rows.size() < NotificationService::historyLimit;
                 ++it)
                rows.push_back(&*it);
        };
        append(history);
        append(historical);
        return rows;
    }
    void markRead(const AcceptedNotice* selected = nullptr)
    {
        QMap<QString, QStringList> changedRuns;
        for (auto row : visibleEntries()) {
            if (row->read || (selected && (row->run != selected->run || !row->sameGroup(selected->value))))
                continue;
            row->read = true;
            if (archive && row->event && !row->run.isEmpty())
                changedRuns[row->run].append(QString::number(row->event));
        }
        for (auto it = changedRuns.cbegin(); it != changedRuns.cend(); ++it) {
            const auto directory = archive->directoryFor(it.key());
            const auto folder = QDir(directory).filePath(it.key());
            if (directory.isEmpty() || !QFileInfo(folder).isDir() || QFileInfo(folder).isSymLink()) {
                readWarning = QStringLiteral("通知归档已不可用，无法保存已读状态。");
                continue;
            }
            QSettings receipts(QDir(folder).filePath("notice-read.ini"), QSettings::IniFormat);
            for (const auto& key : it.value())
                receipts.setValue(key, true);
            auto ids = receipts.childKeys();
            std::sort(ids.begin(), ids.end(),
                      [](const QString& a, const QString& b) { return a.toULongLong() > b.toULongLong(); });
            for (int i = static_cast<int>(NotificationService::historyLimit); i < ids.size(); ++i)
                receipts.remove(ids[i]);
            receipts.sync();
            if (receipts.status() != QSettings::NoError)
                readWarning = QStringLiteral("未能保存通知已读状态。");
        }
        if (!selected)
            faultUnread = false;
        dirty = true;
        render();
    }
    void renderCenter()
    {
        if (!panel)
            return;
        const auto rows = visibleEntries();
        const auto unread =
            std::count_if(rows.begin(), rows.end(), [](const auto* entry) { return !entry->read; }) + faultUnread;
        if (bell) {
            const auto label = QStringLiteral("通知中心，%1 条未读").arg(unread);
            bell->setText(unread ? QString::number(unread) : QString());
            bell->setToolButtonStyle(unread ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
            const int iconWidth = bell->iconSize().width() + 12;
            bell->setMinimumWidth(iconWidth + (unread ? bell->fontMetrics().horizontalAdvance(bell->text()) + 6 : 0));
            bell->setToolTip(label);
            bell->setAccessibleName(label);
        }
        std::optional<Notification> toast;
        for (auto& entry : history) {
            if (entry.announce)
                toast = entry.value.notification;
            entry.announce = false;
        }
        if (!open || !panel)
            return;
        QStringList messages{archiveWarning, readWarning};
        messages.removeAll(QString{});
        const auto problem = messages.join("\n");
        panel->status->setText(problem);
        panel->status->setVisible(!problem.isEmpty());
        const bool showFault = announceFault;
        announceFault = false;
        if (!panel->isVisible()) {
            auto window = panel->parentWidget();
            const bool showBar = !muted && window->isVisible() && !window->property("EveReducedMotion").toBool();
            if (showFault && showBar)
                ElaMessageBar::warning(ElaMessageBarType::BottomRight, QStringLiteral("日志归档"),
                                       QStringLiteral("日志归档受阻，请在通知中心查看。"), 3600, window);
            if (toast && showBar) {
                const auto text = QString::fromStdString(toast->text).simplified();
                const auto summary = panel->fontMetrics().elidedText(
                    text, Qt::ElideRight, std::max(80, std::min(320, window->width() - 140)));
                if (toast->level == NotificationLevel::Error)
                    ElaMessageBar::error(ElaMessageBarType::BottomRight, QStringLiteral("错误"), summary, 3600, window);
                else
                    ElaMessageBar::warning(ElaMessageBarType::BottomRight, QStringLiteral("警告"), summary, 3600,
                                           window);
            }
            return;
        }
        struct Group {
            AcceptedNotice latest;
            std::uint64_t first;
            int count;
            bool unread;
        };
        std::vector<Group> groups;
        for (auto it : rows) {
            auto match = std::find_if(groups.begin(), groups.end(), [&](const auto& g) {
                return it->run == g.latest.run && it->sameGroup(g.latest.value);
            });
            if (match == groups.end())
                groups.push_back({*it, it->value.sequence, 1, !it->read});
            else {
                ++match->count;
                match->first = it->value.sequence;
                match->unread |= !it->read;
            }
        }
        const auto selected = panel->list->currentIndex().data(Qt::UserRole + 1);
        const QSignalBlocker block(panel->list->selectionModel());
        panel->model->clear();
        int row = 0;
        for (const auto& group : groups) {
            const auto& note = group.latest;
            const auto source = note.value.owner.instance.valid() ? QStringLiteral("实例 #%1 · 代 %2")
                                                                        .arg(note.value.owner.instance.value())
                                                                        .arg(note.value.owner.epoch)
                                                                  : QStringLiteral("平台");
            const bool prior = archive && note.run != archive->currentRun();
            QString text = note.received.toString(prior ? "yyyy-MM-dd HH:mm:ss" : "HH:mm:ss") + "  " +
                           (note.source.isEmpty() ? source : note.source + " · " + source) + " · " +
                           levelName(note.value.notification.level);
            if (group.unread)
                text += QStringLiteral(" · 未读");
            if (group.count > 1)
                text += QStringLiteral(" ×%1").arg(group.count);
            text += "\n" + QString::fromStdString(note.value.notification.text);
            auto item = new QStandardItem(text);
            item->setToolTip(Qt::convertFromPlainText(text));
            item->setData(QVariant::fromValue<qulonglong>(note.event), Qt::UserRole);
            item->setData(note.run + "/" + QString::number(group.first), Qt::UserRole + 1);
            item->setData(QVariant::fromValue<qulonglong>(note.value.sequence), Qt::UserRole + 2);
            item->setData(note.run, Qt::UserRole + 3);
            if (selected.isValid() && selected == item->data(Qt::UserRole + 1))
                row = panel->model->rowCount();
            panel->model->appendRow(item);
        }
        panel->empty->setVisible(groups.empty());
        panel->list->setVisible(!groups.empty());
        panel->list->setCurrentIndex(panel->model->index(row, 0));
        panel->markRead->setEnabled(unread > 0);
        panel->openLog->setEnabled(logs && panel->list->currentIndex().data(Qt::UserRole).toULongLong());
    }
    /** @brief 复用外壳访问屏障，嵌套通知只使原投影再次失效。 */
    void render()
    {
        if (!open || !dirty || !notification || !access || rendering)
            return;
        rendering = true;
        while (open && dirty && notification && access) {
            dirty = false;
            if (history.empty()) {
                auto guard = access;
                guard([held = shared_from_this()] {
                    if (held->open)
                        held->renderCenter();
                });
                continue;
            }
            const auto latest = history.back().value.notification;
            const auto prefix = latest.level == NotificationLevel::Error     ? QString::fromUtf8("错误：")
                                : latest.level == NotificationLevel::Warning ? QString::fromUtf8("提示：")
                                                                             : QString::fromUtf8("消息：");
            const auto text = prefix + QString::fromStdString(latest.text);
            auto held = shared_from_this();
            auto guard = access;
            guard([held, text] {
                if (!held->open || !held->notification)
                    return;
                held->notification->setText(text);
                if (held->open && held->notification)
                    held->notification->setToolTip(text);
                if (held->open)
                    held->renderCenter();
            });
        }
        rendering = false;
    }
};

std::shared_ptr<NotificationControl> createNotifications(std::shared_ptr<qt::QtScheduler> scheduler,
                                                         QLabel* notification)
{
    auto result = std::make_shared<NotificationControl>();
    result->scheduler = std::move(scheduler);
    result->notification = notification;
    notification->setTextFormat(Qt::PlainText);
    return result;
}
void configureNotifications(const std::shared_ptr<NotificationControl>& self, std::shared_ptr<OutputControl> output,
                            WindowAccess access)
{
    self->logs = std::move(output);
    self->access = std::move(access);
}
void installNotificationCenter(const std::shared_ptr<NotificationControl>& self, QWidget* window,
                               std::function<QString(InstanceId)> sourceName)
{
    self->sourceName = std::move(sourceName);
    self->panel = new NoticePanel(window);
    self->bell = window->findChild<QToolButton*>("EveShowNotifications");
    if (self->bell)
        self->bell->setFocusPolicy(Qt::TabFocus);
    auto panel = self->panel.data();
    std::weak_ptr<NotificationControl> weak = self;
    const auto close = [weak] {
        if (auto host = weak.lock(); host && host->open && host->panel) {
            host->panel->hide();
            host->origin.restore(host->bell.data());
        }
    };
    QObject::connect(panel->closeButton, &QPushButton::clicked, panel, close);
    auto escape = new QShortcut(QKeySequence(Qt::Key_Escape), panel);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(escape, &QShortcut::activated, panel, close);
    QObject::connect(panel->markRead, &QPushButton::clicked, panel, [weak] {
        if (auto host = weak.lock(); host && host->open) {
            host->markRead();
        }
    });
    const auto openLog = [weak] {
        auto host = weak.lock();
        if (!host || !host->open || !host->logs || !host->panel)
            return;
        const auto index = host->panel->list->currentIndex();
        const auto event = index.data(Qt::UserRole).toULongLong();
        if (!event)
            return;
        const auto seq = index.data(Qt::UserRole + 2).toULongLong();
        const auto run = index.data(Qt::UserRole + 3).toString();
        auto rows = host->visibleEntries();
        const auto found = std::find_if(rows.begin(), rows.end(),
                                        [&](const auto* n) { return n->run == run && n->value.sequence == seq; });
        if (found == rows.end())
            return;
        const auto archivedEvent = (*found)->archivedEvent;
        host->markRead(*found);
        host->panel->hide();
        if (host->open)
            showNotificationOutput(host->logs, event, run, std::exchange(host->origin, {}), archivedEvent);
    };
    QObject::connect(panel->openLog, &QPushButton::clicked, panel, openLog);
    QObject::connect(panel->list, &QListView::activated, panel, [openLog](const QModelIndex&) { openLog(); });
    QObject::connect(panel->list->selectionModel(), &QItemSelectionModel::currentChanged, panel,
                     [weak](const QModelIndex& index) {
                         if (auto host = weak.lock(); host && host->open && host->panel)
                             host->panel->openLog->setEnabled(host->logs && index.data(Qt::UserRole).toULongLong());
                     });
}
void watchNotificationArchive(const std::shared_ptr<NotificationControl>& self,
                              std::shared_ptr<DesktopArchive> archive)
{
    if (!archive || self->archive || !self->panel)
        return;
    self->archive = archive;
    for (auto& entry : self->history)
        entry.run = archive->currentRun();
    self->loading = std::async(std::launch::async, [archive] {
        QString lastRun;
        std::unique_ptr<QSettings> reads;
        std::deque<AcceptedNotice> notices;
        for (const auto& item : archive->recentNotifications(NotificationService::historyLimit)) {
            const auto& run = item.first;
            const auto& record = item.second;
            if (run != lastRun) {
                const auto directory = archive->directoryFor(run);
                if (directory.isEmpty())
                    continue;
                reads = std::make_unique<QSettings>(QDir(directory).filePath(run + "/notice-read.ini"),
                                                    QSettings::IniFormat);
                lastRun = run;
            }
            Notification value{static_cast<NotificationLevel>(record.entry.level), record.entry.text,
                               *record.notification};
            const auto seq = record.entry.sequence;
            notices.push_back({{seq, record.entry.owner.value_or(ContextIdentity{}), std::move(value)},
                               QDateTime::fromMSecsSinceEpoch(record.utcMs),
                               record.sourceName,
                               seq,
                               reads->value(QString::number(seq)).toBool(),
                               false,
                               run,
                               record.eventId});
        }
        return notices;
    });
    auto poll = new QTimer(self->panel);
    poll->setInterval(250);
    std::weak_ptr<NotificationControl> weak = self;
    QObject::connect(poll, &QTimer::timeout, self->panel, [weak] {
        auto host = weak.lock();
        if (!host || !host->open)
            return;
        const auto warning = host->archive->error();
        if (host->archiveWarning != warning) {
            const bool first = host->archiveWarning.isEmpty() && !warning.isEmpty();
            host->archiveWarning = warning;
            host->announceFault = first;
            if (first)
                host->faultUnread = true;
            if (warning.isEmpty())
                host->faultUnread = false;
            host->dirty = true;
        }
        if (host->loading.valid() && host->loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                host->historical = host->loading.get();
            } catch (const std::exception&) {
                host->readWarning = QStringLiteral("无法读取历史通知。");
            }
            host->dirty = true;
        }
        host->render();
    });
    poll->start();
}
Result<void> toggleNotificationCenter(const std::shared_ptr<NotificationControl>& self, QWidget* origin)
{
    const auto available = self->business();
    if (!available.hasValue())
        return available;
    if (!self->panel)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "notification center unavailable"));
    settleNotifications(self, false);
    auto guard = self->access;
    if (!guard)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "notification access closed"));
    guard([self, trigger = QPointer<QWidget>(origin)] {
        if (!self->open || !self->panel)
            return;
        if (self->panel->isVisible() && !trigger) {
            self->panel->closeButton->click();
            return;
        }
        auto mainWindow = self->panel->window();
        auto target = trigger ? trigger.data() : QApplication::focusWidget();
        self->origin.capture(target ? target : self->bell.data(), mainWindow);
        if (!self->open || !self->panel)
            return;
        self->panel->position();
        self->panel->show();
        self->panel->raise();
        self->renderCenter();
        mainWindow->raise();
        mainWindow->activateWindow();
        if (self->panel->list->isVisible())
            self->panel->list->setFocus(Qt::OtherFocusReason);
        else
            self->panel->closeButton->setFocus(Qt::OtherFocusReason);
    });
    return Result<void>::success();
}
void muteNotifications(const std::shared_ptr<NotificationControl>& self, bool muted)
{
    self->muted = muted;
    if (muted)
        for (auto& n : self->history)
            n.announce = false;
}
std::shared_ptr<PendingSource> notificationSource(const std::shared_ptr<NotificationControl>& self,
                                                  ContextIdentity owner)
{
    auto source = self->pending.source(owner);
    return source ? source : std::make_shared<PendingSource>(owner);
}
void settleNotifications(const std::shared_ptr<NotificationControl>& self, bool render)
{
    if (!self->scheduler->isCurrentThread() || !self->open)
        return;
    auto ready = self->pending.takeReady();
    for (auto& value : ready)
        self->admit(std::move(value));
    if (render) {
        self->render();
        if (self->logs)
            refreshOutput(self->logs);
    }
}
/** @brief 开放期间报告观察/镜像错误，关闭后只保留纯历史。 */
static Result<void> notificationStatus(const std::shared_ptr<NotificationControl>& self)
{
    const auto checked = self->thread();
    if (!checked.hasValue())
        return checked;
    if (!self->open)
        return Result<void>::success();
    const auto pending = self->pending.status();
    if (!pending.hasValue())
        return pending;
    if (self->deliveryError)
        return Result<void>::failure(*self->deliveryError);
    return Result<void>::success();
}
Result<void> prepareNotification(const std::shared_ptr<NotificationControl>& self, const Notification& value)
{
    const auto checked = self->business();
    if (!checked.hasValue())
        return checked;
    settleNotifications(self, false);
    const auto status = notificationStatus(self);
    return status.hasValue() ? self->validate(value) : status;
}
/** @brief 普通和候选提交保留同一序号，失败接纳不占号。 */
static Result<std::uint64_t> submit(const std::shared_ptr<NotificationControl>& self, ContextIdentity owner,
                                    Notification value, const std::shared_ptr<PendingSource>& source)
{
    using Output = Result<std::uint64_t>;
    const auto checked = prepareNotification(self, value);
    if (!checked.hasValue())
        return Output::failure(checked.error());
    auto decision = source ? source->decision() : Result<std::optional<bool>>::success(true);
    if (!decision.hasValue())
        return Output::failure(decision.error());
    if (decision.value().has_value() && !*decision.value())
        return Output::failure(makeError(ErrorCode::ContextClosing, "notification publication aborted"));
    const auto sequence = self->sequence + 1;
    NotificationSnapshot entry{sequence, owner, std::move(value)};
    if (!decision.value().has_value()) {
        const auto bytes = entry.notification.text.size();
        auto queued =
            self->pending.enqueue(std::move(entry), source,
                                  std::to_string(owner.instance.value()) + "/" + std::to_string(owner.epoch), bytes);
        if (!queued.hasValue())
            return Output::failure(queued.error());
        self->sequence = sequence;
    } else {
        self->sequence = sequence;
        self->admit(std::move(entry));
    }
    self->render();
    if (self->logs)
        refreshOutput(self->logs);
    return Output::success(sequence);
}
Result<std::uint64_t> submitNotification(const std::shared_ptr<NotificationControl>& self, ContextIdentity owner,
                                         Notification value, std::shared_ptr<PendingSource> source)
{
    return submit(self, owner, std::move(value), source);
}
Result<std::uint64_t> appendNotification(const std::shared_ptr<NotificationControl>& self, ContextIdentity owner,
                                         Notification value)
{
    return submit(self, owner, std::move(value), {});
}
Result<std::vector<NotificationSnapshot>> notificationSnapshot(const std::shared_ptr<NotificationControl>& self)
{
    const auto checked = self->thread();
    if (!checked.hasValue())
        return Result<std::vector<NotificationSnapshot>>::failure(checked.error());
    settleNotifications(self, false);
    const auto status = notificationStatus(self);
    if (!status.hasValue())
        return Result<std::vector<NotificationSnapshot>>::failure(status.error());
    std::vector<NotificationSnapshot> values;
    for (const auto& entry : self->history)
        values.push_back(entry.value);
    return Result<std::vector<NotificationSnapshot>>::success(std::move(values));
}
void closeNotifications(const std::shared_ptr<NotificationControl>& self)
{
    self->open = false;
    if (self->panel)
        self->panel->hide();
    self->sourceName = {};
    self->pending.clear();
    self->access = {};
}
} // namespace eve::ui::detail
