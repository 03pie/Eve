/** @file modules_panel.cpp
 * @brief 同一托管模块窗口；所有更新沿 Workspace 具体视图屏障，不拥有业务能力。
 */
#include "modules_panel_p.hpp"
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <QHeaderView>
#include <QHideEvent>
#include <QShowEvent>
#include <QStandardItemModel>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>

namespace notebook {
namespace {
class ModulesPanel final : public QWidget {
public:
    explicit ModulesPanel(std::weak_ptr<Workspace> workspace) : workspace_(std::move(workspace))
    {
        setObjectName("CordisModuleDiagnostics");
        setMinimumSize(700, 470);
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 20, 20, 20);
        auto heading = new ElaText(QString::fromUtf8("模块管理"), 24, this);
        layout->addWidget(heading);
        auto description =
            new ElaText(QString::fromUtf8("模块代码由多个配置实例共享。这里显示当前版本及实际资源状态。"), 14, this);
        description->setWordWrap(true);
        description->setTextFormat(Qt::PlainText);
        layout->addWidget(description);
        table_ = new ElaTableView(this);
        table_->setObjectName("CordisModuleTable");
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        model_ = new QStandardItemModel(0, 4, table_);
        model_->setHorizontalHeaderLabels({QString::fromUtf8("模块"), QString::fromUtf8("当前版本"),
                                           QString::fromUtf8("映像策略"), QString::fromUtf8("配置实例")});
        table_->setModel(model_);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setStretchLastSection(true);
        table_->verticalHeader()->setVisible(false);
        layout->addWidget(table_, 1);
        status_ = new ElaText(this);
        status_->setTextPixelSize(13);
        status_->setWordWrap(true);
        status_->setTextFormat(Qt::PlainText);
        status_->setObjectName("CordisModuleStatus");
        layout->addWidget(status_);
        auto detailsTitle = new ElaText(QString::fromUtf8("资源与诊断"), 16, this);
        layout->addWidget(detailsTitle);
        details_ = new ElaPlainTextEdit(this);
        details_->setObjectName("CordisModuleDiagnosticsText");
        details_->setReadOnly(true);
        layout->addWidget(details_, 2);
        auto refresh = new ElaPushButton(QString::fromUtf8("刷新"), this);
        refresh->setObjectName("CordisModuleDiagnosticsRefresh");
        layout->addWidget(refresh);
        QObject::connect(refresh, &QPushButton::clicked, this, [this] { refreshView(); });
        timer_ = new QTimer(this);
        timer_->setObjectName("CordisModulesRefreshTimer");
        timer_->setInterval(250);
        QObject::connect(timer_, &QTimer::timeout, this, [this] { refreshView(); });
        refreshView();
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        timer_->start();
        refreshView();
    }
    void hideEvent(QHideEvent* event) override
    {
        timer_->stop();
        QWidget::hideEvent(event);
    }

private:
    std::weak_ptr<Workspace> workspace_;
    ElaTableView* table_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    ElaText* status_ = nullptr;
    ElaPlainTextEdit* details_ = nullptr;
    QTimer* timer_ = nullptr;

    void render(const WorkspaceModulesSnapshot& snapshot)
    {
        model_->setRowCount(static_cast<int>(snapshot.current.size()));
        for (std::size_t index = 0; index < snapshot.current.size(); ++index) {
            const auto& entry = snapshot.current[index];
            const auto generation = std::find_if(snapshot.generations.begin(), snapshot.generations.end(),
                                                 [&](const auto& row) { return row.generation == entry.generation; });
            QStringList paths;
            for (const auto& path : entry.paths)
                paths.push_back(QString::fromStdString(path));
            QString version, policy;
            if (generation != snapshot.generations.end()) {
                version = QString::fromStdString(generation->manifest.moduleVersion);
                policy = generation->loadPolicy == cordis::module::ModuleLoadPolicy::ApprovedNative
                             ? QString::fromUtf8("支持按需回收")
                             : QString::fromUtf8("进程驻留");
            }
            const QStringList columns{QString::fromStdString(entry.plugin), version, policy, paths.join(", ")};
            for (int column = 0; column < columns.size(); ++column) {
                const auto row = static_cast<int>(index);
                if (!model_->item(row, column))
                    model_->setItem(row, column, new QStandardItem);
                auto item = model_->item(row, column);
                item->setText(columns[column]);
                item->setToolTip(columns[column]);
                if (column == 0) {
                    item->setData(QString::fromStdString(entry.alias), Qt::UserRole);
                    item->setData(QVariant::fromValue<qulonglong>(entry.generation.value()), Qt::UserRole + 1);
                }
            }
        }
        const auto text = detail::moduleDiagnosticsText(snapshot);
        if (details_->toPlainText() != text)
            details_->setPlainText(text);
        status_->setText(snapshot.open ? QString::fromUtf8("%1 个模块").arg(snapshot.current.size())
                                       : QString::fromUtf8("工作台正在关闭或停止接纳操作；这里保留已知状态。"));
    }
    void refreshView()
    {
        const auto workspace = workspace_.lock();
        if (!workspace)
            return;
        // 当前同步写栈结束前，Workspace 的破坏性关闭不得删除本页。
        (void)workspace->withModuleView([this, workspace] {
            const auto snapshot = workspace->modulesSnapshot();
            if (snapshot.hasValue())
                render(snapshot.value());
            else
                status_->setText(QString::fromStdString(snapshot.error().message));
        });
    }
};
} // namespace
std::unique_ptr<QWidget> modulesPanel(std::weak_ptr<Workspace> workspace)
{
    return std::make_unique<ModulesPanel>(std::move(workspace));
}
} // namespace notebook
