/** @file panel.cpp
 * @brief 可检索命令与当前运行问题；错误有来源、详情及稳定身份定位。
 */
#include "panel_p.hpp"
#include <ElaLineEdit.h>
#include <ElaListView.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSplitter>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <set>
#include <map>

namespace cordis::ui::detail {
QWidget* createCommandPanel(QWidget* parent, QListView*& list)
{
    auto page = new QWidget(parent); page->setObjectName("CordisCommandPanel");
    auto layout = new QVBoxLayout(page); layout->setContentsMargins(10, 6, 10, 6); layout->setSpacing(4);
    auto toolbar = new QHBoxLayout;
    auto search = new ElaLineEdit(page); search->setObjectName("CordisCommandFilter");
    search->setPlaceholderText(QString::fromUtf8("搜索平台和插件命令（名称或 ID）"));
    search->setClearButtonEnabled(true); search->setAccessibleName(QString::fromUtf8("搜索命令"));
    auto run = new ElaPushButton(QString::fromUtf8("执行"), page); run->setObjectName("CordisRunCommand");
    run->setFixedWidth(72); toolbar->addWidget(search, 1); toolbar->addWidget(run); layout->addLayout(toolbar);
    auto view = new ElaListView(page); list = view; view->setObjectName("CordisCommandList");
    view->setEditTriggers(QAbstractItemView::NoEditTriggers); view->setItemHeight(32);
    view->setStyleSheet("ElaListView{background:transparent;border:0;}"); layout->addWidget(view, 1);
    auto result = new ElaText(QString::fromUtf8("选择命令后按回车、双击或点击执行。"), 12, page);
    result->setTextFormat(Qt::PlainText); result->setObjectName("CordisCommandResult"); result->setWordWrap(true);
    layout->addWidget(result); page->setFocusProxy(search);
    return page;
}
namespace {
struct Problem { QString id, source, message, detail; InstanceId instance; bool warning = false; QString path{}; };
QString details(const ErrorInfo& error, unsigned depth = 0)
{
    auto text = QString::fromStdString(error.message);
    if (!error.phase.empty()) text += " [" + QString::fromStdString(error.phase) + "]";
    if (depth < 4) for (std::size_t i = 0; i < error.causes.size() && i < 8; ++i)
        text += "\n" + QString((depth + 1) * 2, ' ') + details(error.causes[i], depth + 1);
    return text;
}
}
void installProblemsPanel(QTabWidget* panel, std::shared_ptr<DiagnosticsService> diagnostics,
    std::shared_ptr<OutputControl> output, WindowAccess access, std::function<Result<void>(InstanceId, const QString&)> locate)
{
    auto page = new QWidget(panel); page->setObjectName("CordisProblemsPanel");
    auto layout = new QVBoxLayout(page); layout->setContentsMargins(10, 6, 10, 6); layout->setSpacing(4);
    auto filter = new ElaLineEdit(page); filter->setObjectName("CordisProblemsFilter");
    filter->setPlaceholderText(QString::fromUtf8("筛选问题：插件、路径或错误信息")); filter->setClearButtonEnabled(true);
    layout->addWidget(filter);
    auto split = new QSplitter(Qt::Horizontal, page);
    auto tree = new QTreeWidget(split); tree->setObjectName("CordisProblemsTree");
    tree->setHeaderLabels({QString::fromUtf8("来源 / 问题"), QString::fromUtf8("说明")});
    tree->setRootIsDecorated(true); tree->setUniformRowHeights(true); tree->setColumnWidth(0, 260);
    tree->header()->setStretchLastSection(true);
    auto detail = new QPlainTextEdit(split); detail->setReadOnly(true); detail->setObjectName("CordisProblemDetails");
    detail->setPlaceholderText(QString::fromUtf8("选择问题查看原因；双击定位到插件管理。"));
    split->addWidget(tree); split->addWidget(detail); split->setStretchFactor(0, 2); split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);
    auto status = new ElaText(QString::fromUtf8("正在读取运行诊断…"), 12, page); status->setTextFormat(Qt::PlainText); status->setObjectName("CordisProblemsStatus");
    layout->addWidget(status);
    panel->addTab(page, QString::fromUtf8("问题"));
    auto previous = std::make_shared<QString>();
    auto seen = std::make_shared<std::set<QString>>();
    auto refresh = [panel, page, tree, detail, status, filter, diagnostics, output, access, previous, seen] {
        auto result = diagnostics->snapshot();
        if (!result.hasValue()) {
            access([&] { status->setText(QString::fromUtf8("运行诊断暂不可用，保留上次结果：") + QString::fromStdString(result.error().message)); });
            return;
        }
        std::vector<Problem> problems;
        if (result.hasValue()) {
            std::set<QString> unique;
            const auto add = [&](QString id, QString source, const ErrorInfo& error, InstanceId instance = {}, bool warning = false, QString path = {}) {
                const auto key = source + "/" + QString::number(static_cast<int>(error.code)) + "/" + QString::fromStdString(error.message);
                if (!unique.insert(key).second) return;
                problems.push_back({std::move(id), std::move(source), QString::fromStdString(error.message), details(error), instance, warning, std::move(path)});
            };
            const auto& snapshot = result.value();
            for (const auto& row : snapshot.instances) {
                const auto prefix = QString::number(row.instance.instance.value()) + "/" + QString::number(row.instance.epoch);
                const auto source = QString::fromStdString(row.name) + QString(" #%1").arg(row.instance.instance.value());
                if (row.instance.failure) add(prefix + "/failure", source, *row.instance.failure, row.instance.instance);
                for (const auto& error : row.instance.resources.errors) add(prefix + "/resource/" + QString::fromStdString(error.message), source, error, row.instance.instance);
                if (row.instance.state == InstanceState::Pending) for (const auto& dependency : row.dependencies)
                    if (dependency.error) add(prefix + "/dependency/" + QString::fromStdString(dependency.key.name), source, *dependency.error, row.instance.instance, true);
            }
            if (snapshot.configurationError) add("configuration/source", QString::fromUtf8("配置"), *snapshot.configurationError);
            if (snapshot.configuration) {
                if (snapshot.configuration->error) add("configuration/error", QString::fromUtf8("配置"), *snapshot.configuration->error);
                for (const auto& row : snapshot.configuration->entries) if (row.error)
                    add("configuration/" + QString::fromStdString(row.path), QString::fromStdString(row.path), *row.error, row.instance, false, QString::fromStdString(row.path));
            }
            for (const auto& command : snapshot.commands) if (command.deliveryBlocked)
                add("command/" + QString::number(command.owner.instance.value()) + "/" + QString::fromStdString(command.id), QString::fromStdString(command.title), makeError(ErrorCode::SchedulerClosed, "command delivery blocked"), command.owner.instance);
        }
        QString fingerprint;
        std::set<QString> active;
        for (const auto& problem : problems) {
            const auto key = problem.id + problem.detail;
            active.insert(key); fingerprint += key;
            if (!seen->count(key)) (void)appendOutput(output, {}, "platform", (problem.source + ": " + problem.detail).left(3500).toStdString(), problem.warning ? OutputLevel::Warning : OutputLevel::Error);
        }
        *seen = std::move(active);
        const auto query = filter->text(); fingerprint += query;
        access([&] {
            status->setText(!result.hasValue() ? QString::fromUtf8("运行诊断暂不可用：") + QString::fromStdString(result.error().message)
                                              : problems.empty() ? QString::fromUtf8("当前没有问题") : QString::fromUtf8("%1 个运行问题；双击可定位，恢复后自动移除。").arg(problems.size()));
            const auto tab = panel->indexOf(page); if (tab >= 0) panel->setTabText(tab, problems.empty() ? QString::fromUtf8("问题") : QString::fromUtf8("问题 %1").arg(problems.size()));
            if (fingerprint == *previous) return;
            *previous = fingerprint;
            const auto selected = tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toString() : QString{};
            QSignalBlocker blocker(tree); tree->clear();
            std::map<QString, QTreeWidgetItem*> groups;
            QTreeWidgetItem* restore = nullptr;
            for (const auto& problem : problems) {
                if (!query.isEmpty() && !(problem.source + problem.detail).contains(query, Qt::CaseInsensitive)) continue;
                auto& group = groups[problem.source];
                if (!group) { group = new QTreeWidgetItem(tree, {problem.source}); group->setExpanded(true); }
                auto item = new QTreeWidgetItem(group, {problem.warning ? QString::fromUtf8("警告") : QString::fromUtf8("错误"), problem.message});
                item->setData(0, Qt::UserRole, problem.id); item->setData(0, Qt::UserRole + 1, QVariant::fromValue<qulonglong>(problem.instance.value()));
                item->setData(0, Qt::UserRole + 2, problem.detail); item->setToolTip(1, problem.detail);
                item->setData(0, Qt::UserRole + 3, problem.path);
                if (problem.id == selected) restore = item;
            }
            if (restore) {
                tree->setCurrentItem(restore);
                detail->setPlainText(restore->data(0, Qt::UserRole + 2).toString());
            }
            else detail->clear();
        });
    };
    QObject::connect(tree, &QTreeWidget::currentItemChanged, detail, [detail, access](QTreeWidgetItem* item, QTreeWidgetItem*) {
        const auto text = item ? item->data(0, Qt::UserRole + 2).toString() : QString{};
        access([detail, text] { detail->setPlainText(text); });
    });
    QObject::connect(tree, &QTreeWidget::itemActivated, tree, [locate, status, access](QTreeWidgetItem* item, int) {
        if (item->data(0, Qt::UserRole).toString().isEmpty()) return;
        auto id = item->data(0, Qt::UserRole + 1).toULongLong();
        auto result = locate(InstanceId{id}, item->data(0, Qt::UserRole + 3).toString());
        if (!result.hasValue()) access([&] { status->setText(QString::fromStdString(result.error().message)); });
    });
    const auto theme = [tree, detail, access](ElaThemeType::ThemeMode mode) {
        access([tree, detail, mode] {
        auto colors = tree->palette(); colors.setColor(QPalette::Base, eTheme->getThemeColor(mode, ElaThemeType::WindowBase));
        colors.setColor(QPalette::Text, eTheme->getThemeColor(mode, ElaThemeType::BasicText));
        tree->setPalette(colors); detail->setPalette(colors);
        tree->setStyleSheet("QTreeWidget{border:0;background:transparent;}QHeaderView::section{background:transparent;border:0;}");
        detail->setStyleSheet("QPlainTextEdit{border:0;background:transparent;}");
        });
    };
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, page, theme); theme(eTheme->getThemeMode());
    auto timer = new QTimer(page); timer->setInterval(500);
    QObject::connect(timer, &QTimer::timeout, page, refresh);
    QObject::connect(filter, &QLineEdit::textChanged, page, [refresh](const QString&) { refresh(); }); timer->start();
}
} // namespace cordis::ui::detail
