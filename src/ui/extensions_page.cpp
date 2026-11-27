#include "extensions_page_p.hpp"
#include "appearance_p.hpp"
#include "brand_p.hpp"
#include "theme_palette_p.hpp"
#include <ElaComboBox.h>
#include <ElaIcon.h>
#include <ElaLineEdit.h>
#include <ElaListView.h>
#include <ElaMenu.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaText.h>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <algorithm>

namespace eve::ui::detail {
namespace {
constexpr int sideBySideWidth = 720;
QIcon packageIcon(const ExtensionItem& item)
{
    QPixmap image;
    if (!item.icon.isEmpty() && image.loadFromData(item.icon, "PNG")) return QIcon(image);
    if (item.builtin && item.kind == ExtensionKind::Desktop) return eveApplicationIcon();
    return ElaIcon::getInstance()->getElaIcon(ElaIconType::PuzzlePiece, 36, eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::PrimaryNormal));
}
QString kindTitle(ExtensionKind kind) { return kind == ExtensionKind::Desktop ? QObject::tr("桌面外壳") : QObject::tr("业务扩展"); }
QString actionTitle(ExtensionAction action)
{
    switch (action) {
    case ExtensionAction::Install: return QObject::tr("从文件更新或重装…");
    case ExtensionAction::Uninstall: return QObject::tr("卸载…");
    case ExtensionAction::UseDesktop: return QObject::tr("设为下次启动的桌面");
    case ExtensionAction::Enable: return QObject::tr("启用");
    case ExtensionAction::AddInstance: return QObject::tr("添加实例");
    case ExtensionAction::Configure: return QObject::tr("配置实例");
    case ExtensionAction::Disable: return QObject::tr("停用");
    case ExtensionAction::ShowLogs: return QObject::tr("查看日志");
    case ExtensionAction::Restart: return QObject::tr("重启工作台");
    }
    return {};
}
ElaText* label(QWidget* parent, int size, const char* id)
{
    auto text = new ElaText(parent);
    text->setObjectName(id);
    text->setTextPixelSize(size);
    text->setWordWrap(true);
    text->setTextFormat(Qt::PlainText);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return text;
}
ElaPushButton* button(QWidget* parent, const QString& caption, const char* id)
{
    auto result = new ActionButton(caption, parent);
    result->setObjectName(id);
    result->setAccessibleName(caption);
    return result;
}
}

ExtensionsPage::ExtensionsPage(std::shared_ptr<qt::QtScheduler> scheduler, std::shared_ptr<ExtensionControls> controls,
                               Instances instances, Present present, QWidget* parent)
    : QWidget(parent), scheduler_(std::move(scheduler)), controls_(std::move(controls)),
      instances_(std::move(instances)), present_(std::move(present))
{
    setObjectName("EveExtensionsPage");
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 12, 18, 14);
    layout->setSpacing(12);
    auto header = new QHBoxLayout;
    auto heading = label(this, 24, "EveExtensionsHeading");
    heading->setText(tr("扩展"));
    header->addWidget(heading, 1);
    install_ = button(this, tr("从安装包安装…"), "EveExtensionsChooseFile");
    connect(install_, &QPushButton::clicked, this, [this] { choosePackage(); });
    header->addWidget(install_);
    layout->addLayout(header);

    auto panes = new QHBoxLayout;
    panes->setSpacing(18);
    layout->addLayout(panes, 1);
    browser_ = new QWidget(this);
    auto catalogue = new QVBoxLayout(browser_);
    catalogue->setContentsMargins(0, 0, 0, 0);
    search_ = new ElaLineEdit(browser_);
    search_->setObjectName("EveExtensionsSearch");
    search_->setPlaceholderText(tr("搜索名称、标识或发布者"));
    search_->setAccessibleName(tr("搜索扩展"));
    search_->setClearButtonEnabled(true);
    catalogue->addWidget(search_);
    filter_ = new ElaComboBox(browser_);
    filter_->setObjectName("EveExtensionsFilter");
    filter_->setAccessibleName(tr("筛选扩展"));
    filter_->addItems({tr("全部扩展"), tr("业务扩展"), tr("桌面外壳"), tr("内置"), tr("缺失"), tr("待重启")});
    catalogue->addWidget(filter_);
    list_ = new ElaListView(browser_);
    list_->setObjectName("EveExtensionsList");
    list_->setAccessibleName(tr("扩展列表"));
    list_->setItemHeight(list_->fontMetrics().lineSpacing() * 2 + 14);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setIconSize(QSize(30, 30));
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    model_ = new QStandardItemModel(list_);
    list_->setModel(model_);
    catalogue->addWidget(list_, 1);
    empty_ = label(browser_, 14, "EveExtensionsEmpty");
    catalogue->addWidget(empty_);
    panes->addWidget(browser_, 2);

    auto scroll = new ElaScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    details_ = scroll;
    auto content = new QWidget(scroll);
    auto detail = new QVBoxLayout(content);
    detail->setContentsMargins(6, 0, 6, 10);
    detail->setSpacing(10);
    back_ = button(content, tr("返回扩展列表"), "EveExtensionsBack");
    detail->addWidget(back_, 0, Qt::AlignLeft);
    title_ = label(content, 22, "EveExtensionTitle");
    meta_ = label(content, 14, "EveExtensionMetadata");
    body_ = label(content, 15, "EveExtensionDescription");
    auto identity = new QHBoxLayout;
    icon_ = label(content, 14, "EveExtensionIcon");
    icon_->setFixedSize(40, 40);
    identity->addWidget(icon_);
    identity->addWidget(title_, 1);
    detail->addLayout(identity);
    detail->addWidget(meta_);
    detail->addWidget(body_);
    instance_ = new ElaComboBox(content);
    instance_->setObjectName("EveExtensionInstance");
    instance_->setAccessibleName(tr("选择要配置的实例"));
    detail->addWidget(instance_, 0, Qt::AlignLeft);
    auto actions = new QHBoxLayout;
    primary_ = button(content, {}, "EveExtensionPrimary");
    primaryButton(primary_);
    more_ = button(content, tr("更多操作"), "EveExtensionMore");
    menu_ = new ElaMenu(more_);
    more_->setMenu(menu_);
    cancel_ = button(content, tr("取消"), "EveExtensionCancel");
    for (auto control : {primary_, more_, cancel_}) actions->addWidget(control);
    actions->addStretch();
    detail->addLayout(actions);
    status_ = label(content, 14, "EveExtensionStatus");
    detail->addWidget(status_);
    auto errors = new QHBoxLayout;
    retryButton_ = button(content, tr("重试"), "EveExtensionRetry");
    logButton_ = button(content, tr("查看日志"), "EveExtensionErrorLogs");
    errors->addWidget(retryButton_);
    errors->addWidget(logButton_);
    errors->addStretch();
    detail->addLayout(errors);
    detail->addStretch();
    scroll->setWidget(content);
    panes->addWidget(scroll, 3);

    connect(search_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(filter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { rebuild(); });
    connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { selectRow(false); });
    connect(list_, &QListView::clicked, this, [this] { selectRow(true); });
    connect(list_, &QListView::activated, this, [this] { selectRow(true); });
    connect(primary_, &QPushButton::clicked, this, [this] { auto act = primaryAction_; if (act) act(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        preview_.reset(); confirmRemoval_ = false; retry_ = {}; message_.clear(); render();
    });
    connect(back_, &QPushButton::clicked, this, [this] { narrowDetail_ = false; updatePanes(); list_->setFocus(); });
    connect(retryButton_, &QPushButton::clicked, this, [this] { auto retry = retry_; if (retry) retry(); });
    connect(logButton_, &QPushButton::clicked, this, [this] { present_(ExtensionAction::ShowLogs, {}, {}); });
    auto theme = [this](ElaThemeType::ThemeMode mode) { setPalette(workbenchPalette(palette(), mode)); };
    theme(eTheme->getThemeMode());
    connect(eTheme, &ElaTheme::themeModeChanged, this, theme);
    render();
}

const ExtensionItem* ExtensionsPage::selected() const
{
    auto it = std::find_if(inventory_.begin(), inventory_.end(), [this](const auto& row) { return row.id == selectedId_; });
    return it == inventory_.end() ? nullptr : &*it;
}
void ExtensionsPage::refresh()
{
    if (busy_) return;
    auto value = controls_->list();
    if (!value.hasValue()) { showFailure(value.error(), [this] { refresh(); }); return; }
    inventory_ = std::move(value).value();
    rebuild();
}
void ExtensionsPage::rebuild()
{
    const auto query = search_->text().trimmed();
    const auto filter = filter_->currentIndex();
    const auto previousScroll = list_->verticalScrollBar()->value();
    QSignalBlocker blocked(list_->selectionModel());
    model_->clear();
    int selectedRow = 0;
    for (const auto& item : inventory_) {
        if ((filter == 1 && item.kind != ExtensionKind::Business) || (filter == 2 && item.kind != ExtensionKind::Desktop)
            || (filter == 3 && !item.builtin) || (filter == 4 && !item.missing) || (filter == 5 && !item.pending)) continue;
        if (!(item.name + ' ' + item.id + ' ' + item.publisher).contains(query, Qt::CaseInsensitive)) continue;
        auto row = new QStandardItem(item.name + "\n" + item.state + QStringLiteral("  ·  ") + item.version);
        row->setIcon(packageIcon(item));
        row->setData(item.id, Qt::UserRole);
        row->setToolTip(item.id);
        if (item.id == selectedId_) selectedRow = model_->rowCount();
        model_->appendRow(row);
    }
    list_->setCurrentIndex(model_->index(selectedRow, 0));
    const auto visibleId = list_->currentIndex().data(Qt::UserRole).toString();
    if (visibleId != selectedId_ && !preview_) {
        confirmRemoval_ = false;
        retry_ = {};
        message_.clear();
    }
    selectedId_ = visibleId;
    list_->verticalScrollBar()->setValue(previousScroll);
    empty_->setText(inventory_.empty() ? tr("尚未安装扩展。") : tr("没有符合筛选条件的扩展。"));
    empty_->setVisible(model_->rowCount() == 0);
    render();
}
void ExtensionsPage::selectRow(bool openDetail)
{
    const auto id = list_->currentIndex().data(Qt::UserRole).toString();
    if (busy_ || id.isEmpty()) return;
    if (id != selectedId_ || preview_) { preview_.reset(); confirmRemoval_ = false; message_.clear(); retry_ = {}; }
    selectedId_ = id;
    if (openDetail) narrowDetail_ = true;
    render();
    if (openDetail && width() < sideBySideWidth) back_->setFocus();
}
void ExtensionsPage::render()
{
    const auto* item = preview_ ? &preview_->item : selected();
    icon_->setVisible(item != nullptr);
    if (item) icon_->setPixmap(packageIcon(*item).pixmap(icon_->size()));
    title_->setText(item ? item->name : tr("扩展详情"));
    QStringList info;
    if (item) {
        info << kindTitle(item->kind) << item->version << item->publisher;
        if (item->builtin) info << tr("内置");
        if (item->current) info << tr("当前桌面");
        if (item->pending) info << tr("待重启");
        info.removeAll(QString());
    }
    meta_->setText(item ? info.join(QStringLiteral("  ·  ")) + "\n" + item->id : QString());
    QString text;
    if (item) {
        text = item->description;
        if (preview_) {
            text += tr("\n\n本地文件：%1\nSHA-256：%2\n扩展可执行本机代码。请确认你信任此文件的来源。")
                        .arg(QFileInfo(preview_->path).fileName(), preview_->sha256);
        } else {
            text += "\n" + item->state;
            if (item->kind == ExtensionKind::Business) text += tr("\n当前工作区实例：%1").arg(item->instances);
            if (confirmRemoval_) text += tr("\n\n卸载后将保留工作区中的 %1 个实例配置。").arg(item->instances);
        }
    }
    body_->setText(text.trimmed());
    const auto previousInstance = instance_->currentText();
    instance_->clear();
    if (item && !preview_ && !confirmRemoval_ && item->kind == ExtensionKind::Business &&
        std::find(item->actions.begin(), item->actions.end(), ExtensionAction::Configure) != item->actions.end())
        instance_->addItems(instances_(item->pluginId));
    const auto index = instance_->findText(previousInstance);
    if (index >= 0) instance_->setCurrentIndex(index);
    instance_->setVisible(instance_->count() > 1);
    instance_->setEnabled(!busy_);
    primaryAction_ = {};
    menu_->clear();
    QString primaryTitle;
    if (preview_) {
        primaryTitle = tr("确认安装");
        primaryAction_ = [this] { submit(ExtensionAction::Install, preview_->item.id, preview_->path, preview_->sha256); };
    } else if (confirmRemoval_ && item) {
        primaryTitle = tr("确认卸载");
        primaryAction_ = [this] { submit(ExtensionAction::Uninstall, selectedId_, {}, removalHash_); };
    } else if (item) {
        for (auto action : item->actions) {
            if (primaryTitle.isEmpty()) {
                primaryTitle = actionTitle(action);
                primaryAction_ = [this, action] { runAction(action); };
            } else {
                auto entry = menu_->addAction(actionTitle(action));
                connect(entry, &QAction::triggered, this, [this, action] { runAction(action); });
            }
        }
    }
    primary_->setText(primaryTitle);
    primary_->setAccessibleName(primaryTitle);
    primary_->setVisible(!primaryTitle.isEmpty());
    primary_->setEnabled(!busy_);
    more_->setVisible(!menu_->actions().isEmpty());
    more_->setEnabled(!busy_);
    cancel_->setVisible(!busy_ && (preview_.has_value() || confirmRemoval_));
    status_->setText(message_);
    retryButton_->setVisible(!busy_ && static_cast<bool>(retry_));
    logButton_->setVisible(!busy_ && static_cast<bool>(retry_));
    browser_->setEnabled(!busy_);
    install_->setEnabled(!busy_);
    updatePanes();
}
void ExtensionsPage::updatePanes()
{
    const bool narrow = width() < sideBySideWidth;
    browser_->setVisible(!narrow || !narrowDetail_);
    details_->setVisible(!narrow || narrowDetail_);
    back_->setVisible(narrow);
    back_->setEnabled(!busy_);
}
void ExtensionsPage::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); updatePanes(); }
void ExtensionsPage::showEvent(QShowEvent* event) { QWidget::showEvent(event); refresh(); }
void ExtensionsPage::showDesktops()
{
    if (busy_) return;
    preview_.reset(); confirmRemoval_ = false; retry_ = {}; message_.clear(); narrowDetail_ = false;
    search_->clear();
    filter_->setCurrentIndex(2);
    refresh();
    list_->setFocus();
}
void ExtensionsPage::choosePackage(const QString& expectedId)
{
    if (busy_) return;
    QPointer<ExtensionsPage> guard(this);
    const auto path = QFileDialog::getOpenFileName(this, tr("选择 Eve 扩展安装包"), {}, tr("Eve 扩展安装包 (*.evepkg)"));
    if (guard && !path.isEmpty()) previewFile(path, expectedId);
}
void ExtensionsPage::previewFile(const QString& path, const QString& expectedId)
{
    if (busy_ || path.isEmpty()) return;
    preview_.reset(); confirmRemoval_ = false; retry_ = {}; narrowDetail_ = true;
    message_ = tr("正在读取安装包…"); busy_ = true; render();
    QPointer<ExtensionsPage> self(this);
    const auto* row = selected();
    const auto legacyPlugin = row && row->legacyReference && row->id == expectedId ? row->pluginId : QString{};
    controls_->preview(path).thenOn(scheduler_, [self, path, expectedId, legacyPlugin](const Result<ExtensionPreview>& result) {
        if (!self) return;
        self->busy_ = false;
        const auto retry = [self, path, expectedId] { if (self) self->previewFile(path, expectedId); };
        if (!result.hasValue()) { self->showFailure(result.error(), retry); return; }
        const bool legacyMatch = !legacyPlugin.isEmpty() && result.value().item.kind == ExtensionKind::Business &&
                                 result.value().item.pluginId == legacyPlugin;
        if (!expectedId.isEmpty() && result.value().item.id != expectedId && !legacyMatch) {
            self->showFailure(makeError(ErrorCode::InvalidArgument, "所选安装包与要更新的扩展标识不同。"),
                              [self, expectedId] { if (self) self->choosePackage(expectedId); });
            return;
        }
        self->preview_ = result.value();
        self->message_.clear();
        self->render();
        self->primary_->setFocus();
    });
}
void ExtensionsPage::showFailure(const ErrorInfo& error, std::function<void()> retry)
{
    message_ = QString::fromStdString(error.message);
    retry_ = std::move(retry);
    narrowDetail_ = true;
    render();
}
void ExtensionsPage::runAction(ExtensionAction action)
{
    const auto* item = selected();
    if (!item || busy_) return;
    retry_ = {}; message_.clear();
    switch (action) {
    case ExtensionAction::Install: choosePackage(item->id); return;
    case ExtensionAction::Uninstall:
        removalHash_ = item->packageHash;
        confirmRemoval_ = true;
        render();
        primary_->setFocus();
        return;
    case ExtensionAction::ShowLogs:
    case ExtensionAction::AddInstance:
    case ExtensionAction::Configure: {
        auto result = present_(action, item->pluginId, instance_->currentText());
        if (!result.hasValue()) showFailure(result.error(), [this, action] { runAction(action); });
        return;
    }
    default: submit(action, item->id); return;
    }
}
void ExtensionsPage::submit(ExtensionAction action, const QString& id, const QString& path, const QString& digest)
{
    if (busy_) return;
    const auto targetId = id;
    const auto archive = path;
    const auto sha256 = digest;
    busy_ = true; retry_ = {}; message_ = tr("正在应用更改…"); render();
    QPointer<ExtensionsPage> self(this);
    controls_->apply(action, targetId, archive, sha256).thenOn(scheduler_,
        [self, action, targetId, archive, sha256](const Result<void>& result) {
            if (!self) return;
            self->busy_ = false;
            if (!result.hasValue()) {
                self->showFailure(result.error(), [self, action, targetId, archive, sha256] {
                    if (self) self->submit(action, targetId, archive, sha256);
                });
                return;
            }
            self->preview_.reset(); self->confirmRemoval_ = false; self->selectedId_ = targetId;
            self->message_ = QObject::tr("更改已应用。");
            if (action == ExtensionAction::Install) {
                QSignalBlocker blockFilter(self->filter_);
                QSignalBlocker blockSearch(self->search_);
                self->filter_->setCurrentIndex(0);
                self->search_->clear();
            }
            self->refresh();
        });
}
}
