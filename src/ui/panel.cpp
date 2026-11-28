/** @file panel.cpp
 * @brief 可检索命令与当前运行问题；错误有来源、详情及稳定身份定位。
 */
#include "panel_p.hpp"
#include "appearance_p.hpp"
#include "theme_palette_p.hpp"
#include <ElaLineEdit.h>
#include <ElaListView.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <QApplication>
#include <QAbstractListModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScopeGuard>
#include <QSplitter>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <set>
#include <map>
#include <algorithm>
#include <utility>

namespace eve::ui::detail
{
namespace
{
class QuickPageModel final : public QAbstractListModel
{
  public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(pages_.size());
    }
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
        const auto& page = pages_[static_cast<std::size_t>(index.row())];
        if (role == Qt::UserRole) return page.key;
        if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole)
            return page.category.isEmpty() ? page.title : page.title + QStringLiteral("  ·  ") + page.category;
        return {};
    }
    void setPages(std::vector<QuickAccessPage> pages)
    {
        const auto matches = [](const auto& a, const auto& b) {
            return a.key == b.key && a.title == b.title && a.category == b.category;
        };
        if (std::equal(pages_.begin(), pages_.end(), pages.begin(), pages.end(), matches)) return;
        QPointer<QuickPageModel> self(this);
        beginResetModel();
        if (!self) return;
        pages_ = std::move(pages);
        endResetModel();
    }

  private:
    std::vector<QuickAccessPage> pages_;
};

class QuickAccess final : public QWidget
{
  public:
    using Pages = std::function<std::vector<QuickAccessPage>()>;
    using ActivatePage = std::function<void(const QString&)>;

    QuickAccess(QWidget* host, QListView*& commandList) : QWidget(host)
    {
        setObjectName("EveCommandPanel");
        hide();
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 6, 10, 6);
        layout->setSpacing(4);
        auto toolbar = new QHBoxLayout;
        search_ = new ElaLineEdit(this);
        search_->setObjectName("EveCommandFilter");
        search_->setClearButtonEnabled(true);
        toolbar->addWidget(search_, 1);
        run_ = new ActionButton(tr("执行"), this);
        run_->setObjectName("EveRunCommand");
        run_->setFixedWidth(72);
        toolbar->addWidget(run_);
        layout->addLayout(toolbar);

        auto makeList = [this, layout](const char* id) {
            auto view = new ElaListView(this);
            view->setObjectName(id);
            view->setEditTriggers(QAbstractItemView::NoEditTriggers);
            view->setItemHeight(32);
            view->setFrameShape(QFrame::NoFrame);
            view->setIsTransparent(true);
            layout->addWidget(view, 1);
            return view;
        };
        commands_ = makeList("EveCommandList");
        commandList = commands_;
        commands_->setAccessibleName(tr("命令结果"));
        pages_ = makeList("EveQuickPageList");
        pages_->setAccessibleName(tr("页面结果"));
        model_ = new QuickPageModel(pages_);
        pages_->setModel(model_);
        commandResult_ = new ElaText(tr("选择命令后按回车执行。"), 12, this);
        commandResult_->setObjectName("EveCommandResult");
        pageResult_ = new ElaText(tr("没有匹配的页面。"), 12, this);
        for (auto result : {commandResult_, pageResult_}) {
            result->setTextFormat(Qt::PlainText);
            result->setWordWrap(true);
            layout->addWidget(result);
        }
        setFocusProxy(search_);
        updateMode();

        bindWorkbenchPalette(this, ElaThemeType::PopupBase);
        connect(search_, &QLineEdit::textChanged, this, [this] {
            updateMode();
            refreshPages(false);
        });
        connect(search_, &QLineEdit::textEdited, this, [this](const QString& text) {
            // Real edits can remove the command prefix. Programmatic search updates in command
            // mode (including when the panel is hidden) continue to address the command model.
            if (!commandEntry_ || text.startsWith(QLatin1Char('>'))) return;
            commandEntry_ = false;
            updateMode();
            refreshPages(false);
        });
        connect(search_, &QLineEdit::returnPressed, this, [this] { activateCurrent(); });
        connect(run_, &QPushButton::clicked, this, [this] { activateCurrent(); });
        connect(pages_, &QListView::clicked, this, [this](const QModelIndex& index) { activatePage(index); });
        connect(pages_, &QListView::activated, this, [this](const QModelIndex& index) { activatePage(index); });
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* next) {
            if (isVisible() && next && !owns(next)) dismiss(false);
        });
        qApp->installEventFilter(this);
    }
    void configurePages(Pages source, ActivatePage activate)
    {
        source_ = std::move(source);
        activatePage_ = std::move(activate);
        refreshPages();
    }
    void present(QWidget* trigger, QuickAccessMode mode)
    {
        if (!isVisible()) {
            auto focus = QApplication::focusWidget();
            origin_ = focus && focus->window() == parentWidget()->window() ? focus : nullptr;
        }
        const auto ticket = ++generation_;
        anchor_ = trigger;
        commandEntry_ = mode == QuickAccessMode::Commands;
        QPointer<QuickAccess> self(this);
        search_->setText(commandEntry_ ? QStringLiteral(">") : QString{});
        if (!self || ticket != generation_) return;
        updateMode();
        place();
        show();
        if (!self || ticket != generation_ || !isVisible()) return;
        raise();
        search_->setFocus(Qt::ShortcutFocusReason);
        if (self && ticket == generation_) refreshPages(false);
    }
    void dismiss(bool restoreFocus = true)
    {
        if (!isVisible()) return;
        const auto ticket = ++generation_;
        QPointer<QuickAccess> self(this);
        const auto origin = origin_;
        origin_.clear();
        hide();
        if (self && ticket == generation_ && restoreFocus && origin && origin->isVisible() && origin->isEnabled())
            origin->setFocus(Qt::ShortcutFocusReason);
    }
    void refreshPages(bool restoreSelection = true)
    {
        if (!isVisible() || mode_ != QuickAccessMode::Pages) return;
        if (refreshingPages_) {
            pageRefreshPending_ = true;
            return;
        }
        const auto query = search_->text().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const auto selected = restoreSelection ? pages_->currentIndex().data(Qt::UserRole).toString() : QString{};
        const auto ticket = generation_;
        QPointer<QuickAccess> self(this);
        refreshingPages_ = true;
        const auto refreshed = qScopeGuard([self] {
            if (!self) return;
            self->refreshingPages_ = false;
            if (std::exchange(self->pageRefreshPending_, false))
                QTimer::singleShot(0, self.data(), [self] { if (self) self->refreshPages(); });
        });
        const auto source = source_;
        auto pages = source ? source() : std::vector<QuickAccessPage>{};
        if (!self || ticket != generation_ || !isVisible() || mode_ != QuickAccessMode::Pages) return;
        pages.erase(std::remove_if(pages.begin(), pages.end(), [&](const auto& page) {
            return !std::all_of(query.cbegin(), query.cend(), [&](const QString& term) {
                return page.title.contains(term, Qt::CaseInsensitive) || page.category.contains(term, Qt::CaseInsensitive);
            });
        }), pages.end());
        const auto found = std::find_if(pages.cbegin(), pages.cend(), [&](const auto& page) { return page.key == selected; });
        const auto index = found == pages.cend() ? 0 : static_cast<int>(found - pages.cbegin());
        model_->setPages(std::move(pages));
        if (!self || ticket != generation_ || !isVisible() || mode_ != QuickAccessMode::Pages) return;
        pages_->setCurrentIndex(model_->index(index, 0));
        pageResult_->setVisible(model_->rowCount() == 0);
        run_->setEnabled(model_->rowCount() != 0);
    }

  protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        drawWorkbenchPopup(painter, rect());
    }

    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (!isVisible())
            return false;
        auto widget = qobject_cast<QWidget*>(object);
        if ((object == parentWidget() || object == anchor_.data()) &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Move))
            queuePlacement();
        if (object == parentWidget()->window() && event->type() == QEvent::WindowDeactivate) {
            const auto ticket = generation_;
            QTimer::singleShot(0, this, [this, ticket] {
                auto popup = QApplication::activePopupWidget();
                if (ticket == generation_ && isVisible() && !parentWidget()->window()->isActiveWindow() &&
                    (!popup || !owns(popup)))
                    dismiss(false);
            });
        }
        if (event->type() == QEvent::MouseButtonPress &&
            static_cast<QMouseEvent*>(event)->button() != Qt::NoButton && widget && !owns(widget)) {
            dismiss(false);
            return false;
        }
        if (widget && owns(widget) && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
            const auto key = static_cast<QKeyEvent*>(event)->key();
            const bool changeSelection = widget == search_ && (key == Qt::Key_Down || key == Qt::Key_Up);
            if (key == Qt::Key_Escape || changeSelection) {
                event->accept();
                if (event->type() == QEvent::KeyPress) {
                    if (changeSelection) moveSelection(key == Qt::Key_Down ? 1 : -1);
                    else dismiss();
                }
                return true;
            }
        }
        return false;
    }

  private:
    ElaLineEdit* search_;
    QListView *commands_, *pages_;
    QuickPageModel* model_;
    ElaPushButton* run_;
    ElaText *commandResult_, *pageResult_;
    QPointer<QWidget> origin_, anchor_;
    Pages source_;
    ActivatePage activatePage_;
    QuickAccessMode mode_ = QuickAccessMode::Commands;
    bool commandEntry_ = true;
    bool placementQueued_ = false;
    bool refreshingPages_ = false, pageRefreshPending_ = false;
    quint64 generation_ = 0;

    bool owns(const QWidget* widget) const
    {
        for (auto candidate = widget; candidate; candidate = candidate->parentWidget())
            if (candidate == this) return true;
        return false;
    }
    void updateMode()
    {
        const bool commands = commandEntry_ || search_->text().startsWith(QLatin1Char('>'));
        mode_ = commands ? QuickAccessMode::Commands : QuickAccessMode::Pages;
        search_->setPlaceholderText(commands ? tr("搜索平台和插件命令（名称或 ID）") : tr("搜索页面，输入 > 运行命令"));
        search_->setAccessibleName(commands ? tr("搜索命令") : tr("搜索页面"));
        commands_->setVisible(commands);
        pages_->setVisible(!commands);
        commandResult_->setVisible(commands);
        pageResult_->setVisible(!commands && model_->rowCount() == 0);
        run_->setText(commands ? tr("执行") : tr("打开"));
        run_->setEnabled(commands || model_->rowCount() != 0);
    }
    QListView* activeList() const { return mode_ == QuickAccessMode::Commands ? commands_ : pages_; }
    void moveSelection(int direction)
    {
        auto list = activeList();
        const auto* model = list->model();
        if (!model) return;
        const int total = model->rowCount();
        const int current = list->currentIndex().isValid() ? list->currentIndex().row() : direction < 0 ? 0 : -1;
        for (int step = 1; step <= total; ++step) {
            const auto candidate = model->index((current + step * direction + total) % total, 0);
            if (!candidate.flags().testFlag(Qt::ItemIsEnabled)) continue;
            list->setCurrentIndex(candidate);
            return;
        }
    }
    void activateCurrent()
    {
        auto list = activeList();
        const auto ticket = generation_;
        QPointer<QuickAccess> self(this);
        if (!list->currentIndex().isValid()) moveSelection(1);
        if (!self || ticket != generation_ || list != activeList()) return;
        const auto index = list->currentIndex();
        if (!index.isValid()) return;
        if (mode_ == QuickAccessMode::Commands) Q_EMIT commands_->activated(index);
        else activatePage(index);
    }
    void activatePage(const QModelIndex& index)
    {
        if (!isVisible() || mode_ != QuickAccessMode::Pages || index.model() != model_) return;
        const auto key = index.data(Qt::UserRole).toString();
        const auto activate = activatePage_;
        if (key.isEmpty() || !activate) return;
        QPointer<QuickAccess> self(this);
        dismiss();
        if (self) activate(key);
    }
    void queuePlacement()
    {
        if (placementQueued_) return;
        placementQueued_ = true;
        QTimer::singleShot(0, this, [this] {
            placementQueued_ = false;
            if (isVisible()) place();
        });
    }
    void place()
    {
        auto host = parentWidget();
        const int width = std::min(520, host->width());
        const auto contents = host->contentsRect();
        const auto point = anchor_ && anchor_->isVisibleTo(host) && !anchor_->visibleRegion().isEmpty()
            ? anchor_->mapTo(host, QPoint(anchor_->width() / 2, anchor_->height()))
            : QPoint(contents.center().x(), contents.top());
        const auto top = std::clamp(point.y(), 0, host->height());
        setGeometry(std::clamp(point.x() - width / 2, 0, host->width() - width), top, width,
                    std::min(280, host->height() - top));
    }
};
} // namespace
void configureQuickAccessPages(QWidget* panel, std::function<std::vector<QuickAccessPage>()> pages,
                               std::function<void(const QString&)> activate)
{
    if (auto access = dynamic_cast<QuickAccess*>(panel)) access->configurePages(std::move(pages), std::move(activate));
}
void refreshQuickAccessPages(QWidget* panel)
{
    if (auto access = dynamic_cast<QuickAccess*>(panel)) access->refreshPages();
}
void showQuickAccess(QWidget* panel, QWidget* anchor, QuickAccessMode mode)
{
    if (auto access = dynamic_cast<QuickAccess*>(panel)) access->present(anchor, mode);
}
void dismissQuickAccess(QWidget* panel)
{
    if (auto access = dynamic_cast<QuickAccess*>(panel)) access->dismiss();
}
void dismissCommandPanel(QWidget* list)
{
    if (list) dismissQuickAccess(list->parentWidget());
}
QWidget* createCommandPanel(QWidget* parent, QListView*& list)
{
    return new QuickAccess(parent, list);
}
namespace
{
struct Problem {
    QString id, source, message, detail;
    InstanceId instance;
    bool warning = false;
    QString path{};
};
QString details(const ErrorInfo& error, unsigned depth = 0)
{
    auto text = QString::fromStdString(error.message);
    if (!error.phase.empty())
        text += " [" + QString::fromStdString(error.phase) + "]";
    if (depth < 4)
        for (std::size_t i = 0; i < error.causes.size() && i < 8; ++i)
            text += "\n" + QString((depth + 1) * 2, ' ') + details(error.causes[i], depth + 1);
    return text;
}
} // namespace
void installProblemsPanel(QTabWidget* panel, std::shared_ptr<DiagnosticsService> diagnostics,
                          std::shared_ptr<OutputControl> output, WindowAccess access,
                          std::function<Result<void>(InstanceId, const QString&)> locate)
{
    auto page = new QWidget(panel);
    page->setObjectName("EveProblemsPanel");
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(4);
    auto filter = new ElaLineEdit(page);
    filter->setObjectName("EveProblemsFilter");
    filter->setPlaceholderText(QString::fromUtf8("筛选问题：插件、路径或错误信息"));
    filter->setClearButtonEnabled(true);
    auto actions = new QHBoxLayout;
    actions->addWidget(filter, 1);
    auto related = new ActionButton(QString::fromUtf8("查看相关日志"), page);
    related->setObjectName("EveProblemLog");
    related->setEnabled(false);
    actions->addWidget(related);
    layout->addLayout(actions);
    auto split = new QSplitter(Qt::Horizontal, page);
    auto tree = new QTreeWidget(split);
    tree->setObjectName("EveProblemsTree");
    tree->setHeaderLabels({QString::fromUtf8("来源 / 问题"), QString::fromUtf8("说明")});
    tree->setRootIsDecorated(true);
    tree->setUniformRowHeights(true);
    tree->setFrameShape(QFrame::NoFrame);
    tree->setColumnWidth(0, 260);
    tree->header()->setStretchLastSection(true);
    auto detail = new QPlainTextEdit(split);
    detail->setReadOnly(true);
    detail->setFrameShape(QFrame::NoFrame);
    // QHeaderView and Qt's Windows style have their own section and selection
    // surfaces. Paint these leaf roles from the same Ela tokens as the page.
    const auto theme = [tree, detail, split](ElaThemeType::ThemeMode mode) {
        applyWorkbenchContentPalette(tree, mode);
        applyWorkbenchContentPalette(tree->header(), mode);
        applyWorkbenchContentPalette(detail, mode);
        const auto text = eTheme->getThemeColor(mode, ElaThemeType::BasicText).name();
        const auto selection = eTheme->getThemeColor(mode, ElaThemeType::PrimaryNormal).name();
        const auto selectedText = eTheme->getThemeColor(mode, ElaThemeType::BasicTextInvert).name();
        const QString editor = QStringLiteral(
            "background:transparent;color:%1;selection-background-color:%2;selection-color:%3;border:0;")
            .arg(text, selection, selectedText);
        tree->setStyleSheet(QStringLiteral(
            "QTreeWidget{%1}QHeaderView{background:transparent;color:%3;border:0;}"
            "QHeaderView::section{background:%2;color:%3;border:0;}"
            "QTreeWidget::item:selected{background:%4;color:%5;}")
            .arg(editor, eTheme->getThemeColor(mode, ElaThemeType::BasicBaseAlpha).name(QColor::HexArgb),
                 text, selection, selectedText));
        detail->setStyleSheet(QStringLiteral("QPlainTextEdit{%1}").arg(editor));
        split->setStyleSheet(QStringLiteral("QSplitter::handle{background:%1;}")
            .arg(eTheme->getThemeColor(mode, ElaThemeType::BasicBorder).name()));
    };
    theme(eTheme->getThemeMode());
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, page, theme);
    detail->setObjectName("EveProblemDetails");
    detail->setPlaceholderText(QString::fromUtf8("选择问题查看原因；双击定位到插件管理。"));
    split->addWidget(tree);
    split->addWidget(detail);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);
    auto status = new ElaText(QString::fromUtf8("正在读取运行诊断…"), 12, page);
    status->setTextFormat(Qt::PlainText);
    status->setObjectName("EveProblemsStatus");
    layout->addWidget(status);
    panel->addTab(page, QString::fromUtf8("问题"));
    auto previous = std::make_shared<QString>();
    using ProblemEvent = std::pair<std::uint64_t, QString>;
    auto seen = std::make_shared<std::map<QString, ProblemEvent>>();
    auto refresh = [panel, page, tree, detail, status, filter, related, diagnostics, output, access, previous, seen] {
        auto result = diagnostics->snapshot();
        if (!result.hasValue()) {
            access([&] {
                status->setText(QString::fromUtf8("运行诊断暂不可用，保留上次结果：") +
                                QString::fromStdString(result.error().message));
            });
            return;
        }
        std::vector<Problem> problems;
        if (result.hasValue()) {
            std::set<QString> unique;
            const auto add = [&](QString id, QString source, const ErrorInfo& error, InstanceId instance = {},
                                 bool warning = false, QString path = {}) {
                const auto key = source + "/" + QString::number(static_cast<int>(error.code)) + "/" +
                                 QString::fromStdString(error.message);
                if (!unique.insert(key).second)
                    return;
                problems.push_back({std::move(id), std::move(source), QString::fromStdString(error.message),
                                    details(error), instance, warning, std::move(path)});
            };
            const auto& snapshot = result.value();
            for (const auto& row : snapshot.instances) {
                const auto prefix =
                    QString::number(row.instance.instance.value()) + "/" + QString::number(row.instance.epoch);
                const auto source =
                    QString::fromStdString(row.name) + QString(" #%1").arg(row.instance.instance.value());
                if (row.instance.failure)
                    add(prefix + "/failure", source, *row.instance.failure, row.instance.instance);
                for (const auto& error : row.instance.resources.errors)
                    add(prefix + "/resource/" + QString::fromStdString(error.message), source, error,
                        row.instance.instance);
                if (row.instance.state == InstanceState::Pending)
                    for (const auto& dependency : row.dependencies)
                        if (dependency.error)
                            add(prefix + "/dependency/" + QString::fromStdString(dependency.key.name), source,
                                *dependency.error, row.instance.instance, true);
            }
            if (snapshot.configurationError)
                add("configuration/source", QString::fromUtf8("配置"), *snapshot.configurationError);
            if (snapshot.configuration) {
                if (snapshot.configuration->error)
                    add("configuration/error", QString::fromUtf8("配置"), *snapshot.configuration->error);
                for (const auto& row : snapshot.configuration->entries)
                    if (row.error)
                        add("configuration/" + QString::fromStdString(row.path), QString::fromStdString(row.path),
                            *row.error, row.instance, false, QString::fromStdString(row.path));
            }
            for (const auto& command : snapshot.commands)
                if (command.deliveryBlocked)
                    add("command/" + QString::number(command.owner.instance.value()) + "/" +
                            QString::fromStdString(command.id),
                        QString::fromStdString(command.title),
                        makeError(ErrorCode::SchedulerClosed, "command delivery blocked"), command.owner.instance);
        }
        QString fingerprint;
        std::map<QString, ProblemEvent> active;
        for (const auto& problem : problems) {
            const auto key = problem.id + problem.detail;
            fingerprint += key;
            const auto old = seen->find(key);
            if (old != seen->end())
                active.emplace(key, old->second);
            else {
                const auto entry = appendOutput(output, {}, "platform",
                                                (problem.source + ": " + problem.detail).left(3500).toStdString(),
                                                problem.warning ? OutputLevel::Warning : OutputLevel::Error);
                if (entry.hasValue())
                    active.emplace(key, ProblemEvent{entry.value(), problem.source});
            }
        }
        for (const auto& old : *seen)
            if (!active.count(old.first))
                (void)appendOutput(output, {}, "platform",
                                   QString::fromUtf8("%1：先前的问题已解除（记录 #%2）。")
                                       .arg(old.second.second)
                                       .arg(old.second.first)
                                       .toStdString());
        *seen = std::move(active);
        const auto query = filter->text();
        fingerprint += query;
        access([&] {
            status->setText(
                !result.hasValue()
                    ? QString::fromUtf8("运行诊断暂不可用：") + QString::fromStdString(result.error().message)
                : problems.empty()
                    ? QString::fromUtf8("当前没有问题")
                    : QString::fromUtf8("%1 个运行问题；双击可定位，恢复后自动移除。").arg(problems.size()));
            const auto tab = panel->indexOf(page);
            if (tab >= 0)
                panel->setTabText(tab, problems.empty() ? QString::fromUtf8("问题")
                                                        : QString::fromUtf8("问题 %1").arg(problems.size()));
            if (fingerprint == *previous)
                return;
            *previous = fingerprint;
            const auto selected =
                tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toString() : QString{};
            QSignalBlocker blocker(tree);
            tree->clear();
            std::map<QString, QTreeWidgetItem*> groups;
            QTreeWidgetItem* restore = nullptr;
            for (const auto& problem : problems) {
                if (!query.isEmpty() && !(problem.source + problem.detail).contains(query, Qt::CaseInsensitive))
                    continue;
                auto& group = groups[problem.source];
                if (!group) {
                    group = new QTreeWidgetItem(tree, {problem.source});
                    group->setExpanded(true);
                }
                auto item = new QTreeWidgetItem(
                    group, {problem.warning ? QString::fromUtf8("警告") : QString::fromUtf8("错误"), problem.message});
                item->setData(0, Qt::UserRole, problem.id);
                item->setData(0, Qt::UserRole + 1, QVariant::fromValue<qulonglong>(problem.instance.value()));
                item->setData(0, Qt::UserRole + 2, problem.detail);
                item->setToolTip(1, problem.detail);
                item->setData(0, Qt::UserRole + 3, problem.path);
                auto event = seen->find(problem.id + problem.detail);
                if (event != seen->end())
                    item->setData(0, Qt::UserRole + 4, QVariant::fromValue<qulonglong>(event->second.first));
                if (problem.id == selected)
                    restore = item;
            }
            if (restore) {
                tree->setCurrentItem(restore);
                detail->setPlainText(restore->data(0, Qt::UserRole + 2).toString());
            } else
                detail->clear();
            related->setEnabled(restore && restore->data(0, Qt::UserRole + 4).isValid());
        });
    };
    QObject::connect(related, &QPushButton::clicked, page, [tree, related, output] {
        if (const auto item = tree->currentItem()) {
            const auto event = item->data(0, Qt::UserRole + 4);
            if (event.isValid())
                showOutput(output, event.toULongLong(), {}, related);
        }
    });
    QObject::connect(tree, &QTreeWidget::currentItemChanged, detail,
                     [detail, related, access](QTreeWidgetItem* item, QTreeWidgetItem*) {
                         const auto text = item ? item->data(0, Qt::UserRole + 2).toString() : QString{};
                         const bool canShow = item && item->data(0, Qt::UserRole + 4).isValid();
                         access([detail, text, related, canShow] {
                             detail->setPlainText(text);
                             related->setEnabled(canShow);
                         });
                     });
    QObject::connect(tree, &QTreeWidget::itemActivated, tree, [locate, status, access](QTreeWidgetItem* item, int) {
        if (item->data(0, Qt::UserRole).toString().isEmpty())
            return;
        auto id = item->data(0, Qt::UserRole + 1).toULongLong();
        auto result = locate(InstanceId{id}, item->data(0, Qt::UserRole + 3).toString());
        if (!result.hasValue())
            access([&] { status->setText(QString::fromStdString(result.error().message)); });
    });
    auto timer = new QTimer(page);
    timer->setInterval(500);
    QObject::connect(timer, &QTimer::timeout, page, refresh);
    QObject::connect(filter, &QLineEdit::textChanged, page, [refresh](const QString&) { refresh(); });
    timer->start();
}
} // namespace eve::ui::detail
