#include "extension_dialog_p.hpp"
#include "appearance_p.hpp"
#include "brand_p.hpp"
#include "fixed_tabs_p.hpp"
#include "workbench_dialog_p.hpp"
#include <ElaComboBox.h>
#include <ElaDialog.h>
#include <ElaIcon.h>
#include <ElaMenu.h>
#include <ElaScrollArea.h>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPointer>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace eve::ui::detail {
namespace {
using Action = ExtensionAction;
QString actionTitle(Action action)
{
    switch (action) {
    case Action::Install: return QObject::tr("从文件更新或重装…");
    case Action::Uninstall: return QObject::tr("卸载…");
    case Action::UseDesktop: return QObject::tr("设为下次启动的桌面");
    case Action::Enable: return QObject::tr("启用");
    case Action::Disable: return QObject::tr("停用");
    case Action::Restart: return QObject::tr("重启工作台");
    case Action::AddInstance: return QObject::tr("添加实例");
    case Action::Configure: return QObject::tr("配置实例");
    case Action::ShowLogs: return QObject::tr("查看日志");
    }
    return {};
}
QIcon packageIcon(const ExtensionItem& item)
{
    QPixmap image;
    if (!item.icon.isEmpty() && image.loadFromData(item.icon, "PNG")) return QIcon(image);
    if (item.builtin && item.kind == ExtensionKind::Desktop) return eveApplicationIcon();
    return ElaIcon::getInstance()->getElaIcon(ElaIconType::PuzzlePiece, 48,
        eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::PrimaryNormal));
}
ElaText* label(QWidget* parent, int size, const char* id)
{
    auto text = new ElaText(parent);
    text->setObjectName(id);
    text->setTextPixelSize(size);
    text->setTextFormat(Qt::PlainText);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text->setWordWrap(true);
    text->setMinimumWidth(1);
    return text;
}
ElaPushButton* button(QWidget* parent, const QString& caption, const char* id)
{
    auto result = new ActionButton(caption, parent);
    result->setObjectName(id);
    result->setAccessibleName(caption);
    return result;
}
QVBoxLayout* addTab(QTabWidget* tabs, const QString& title)
{
    auto scroll = new ElaScrollArea(tabs);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto page = new QWidget(scroll);
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 18, 8, 8);
    layout->setSpacing(12);
    scroll->setWidget(page);
    // QScrollArea::setWidget enables native background filling; keep the Ela dialog surface visible.
    page->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    tabs->addTab(scroll, title);
    return layout;
}
}

ExtensionDialog::ExtensionDialog(std::shared_ptr<qt::QtScheduler> scheduler, std::shared_ptr<ExtensionControls> controls,
                                 Instances instances, Present present, std::function<void()> changed, QWidget* owner)
    : QObject(createWorkbenchDialog(owner)), scheduler_(std::move(scheduler)), controls_(std::move(controls)),
      instances_(std::move(instances)), present_(std::move(present)), changed_(std::move(changed)),
      window_(static_cast<ElaDialog*>(parent()))
{
    window_->setObjectName("EveExtensionDialog");
    window_->setWindowTitle(tr("插件详情"));
    window_->setIsStayTop(false);
    window_->resize(QSize(680, 460).boundedTo(owner->screen()->availableGeometry().size()));
    auto layout = new QVBoxLayout(window_);
    layout->setContentsMargins(24, 48, 24, 20);
    layout->setSpacing(14);
    auto header = new QHBoxLayout;
    header->setSpacing(16);
    icon_ = label(window_, 14, "EveExtensionIcon");
    icon_->setFixedSize(64, 64);
    icon_->setAlignment(Qt::AlignCenter);
    header->addWidget(icon_, 0, Qt::AlignTop);
    auto heading = new QVBoxLayout;
    heading->setSpacing(5);
    title_ = label(window_, 24, "EveExtensionTitle");
    title_->setIsBold(true);
    heading->addWidget(title_);
    metadata_ = label(window_, 14, "EveExtensionMetadata");
    secondaryText(metadata_);
    heading->addWidget(metadata_);
    auto actions = new QHBoxLayout;
    primary_ = button(window_, {}, "EveExtensionPrimary");
    primaryButton(primary_);
    more_ = button(window_, tr("更多操作"), "EveExtensionMore");
    menu_ = new ElaMenu(more_);
    more_->setMenu(menu_);
    cancel_ = button(window_, tr("取消"), "EveExtensionCancel");
    actions->addWidget(primary_);
    actions->addWidget(more_);
    actions->addWidget(cancel_);
    actions->addStretch();
    heading->addLayout(actions);
    header->addLayout(heading, 1);
    layout->addLayout(header);

    tabs_ = new FixedDiagnosticsTabs(window_);
    tabs_->setObjectName("EveExtensionTabs");
    auto intro = addTab(tabs_, tr("介绍"));
    body_ = label(intro->parentWidget(), 16, "EveExtensionDescription");
    intro->addWidget(body_);
    state_ = label(intro->parentWidget(), 14, "EveExtensionState");
    intro->addWidget(state_);
    identity_ = label(intro->parentWidget(), 14, "EveExtensionIdentity");
    secondaryText(identity_);
    intro->addWidget(identity_);
    review_ = label(intro->parentWidget(), 14, "EveExtensionPackageReview");
    intro->addWidget(review_);
    intro->addStretch();

    auto manage = addTab(tabs_, tr("管理"));
    auto pane = manage->parentWidget();
    scope_ = label(pane, 16, "EveExtensionWorkspace");
    scope_->setIsBold(true);
    manage->addWidget(scope_);
    instance_ = new ElaComboBox(pane);
    instance_->setObjectName("EveExtensionInstance");
    instance_->setAccessibleName(tr("工作区实例"));
    instance_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    manage->addWidget(instance_);
    auto operations = new QHBoxLayout;
    configure_ = button(pane, actionTitle(Action::Configure), "EveExtensionConfigure");
    add_ = button(pane, actionTitle(Action::AddInstance), "EveExtensionAddInstance");
    logs_ = button(pane, actionTitle(Action::ShowLogs), "EveExtensionShowLogs");
    operations->addWidget(configure_);
    operations->addWidget(add_);
    operations->addWidget(logs_);
    operations->addStretch();
    manage->addLayout(operations);
    manage->addStretch();
    layout->addWidget(tabs_, 1);

    status_ = label(window_, 14, "EveExtensionStatus");
    layout->addWidget(status_);
    auto errors = new QHBoxLayout;
    retryButton_ = button(window_, tr("重试"), "EveExtensionRetry");
    logButton_ = button(window_, tr("查看日志"), "EveExtensionErrorLogs");
    errors->addWidget(retryButton_);
    errors->addWidget(logButton_);
    errors->addStretch();
    layout->addLayout(errors);
    connect(primary_, &QPushButton::clicked, this, [this] { auto act = primaryAction_; if (act) act(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (!item_) { window_->reject(); return; }
        preview_.reset();
        removalHash_.reset();
        retry_ = {};
        message_.clear();
        render();
    });
    connect(instance_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        instancePath_ = instance_->currentText();
        configure_->setEnabled(!applying_ && index >= 0);
    });
    connect(configure_, &QPushButton::clicked, this, [this] { dispatch(Action::Configure); });
    connect(add_, &QPushButton::clicked, this, [this] { dispatch(Action::AddInstance); });
    connect(logs_, &QPushButton::clicked, this, [this] { dispatch(Action::ShowLogs); });
    connect(retryButton_, &QPushButton::clicked, this, [this] { auto act = retry_; if (act) act(); });
    connect(logButton_, &QPushButton::clicked, this, [this] { presentWorkspaceAction(Action::ShowLogs, {}, {}); });
    connect(window_, &QDialog::finished, this, [this] { resetPresentation(); });
    auto theme = [this](ElaThemeType::ThemeMode mode) {
        window_->setPalette(workbenchPalette(window_->palette(), mode));
        const auto* item = preview_ ? &preview_->item : item_ ? &*item_ : nullptr;
        if (item) icon_->setPixmap(packageIcon(*item).pixmap(icon_->size()));
    };
    theme(eTheme->getThemeMode());
    connect(eTheme, &ElaTheme::themeModeChanged, this, theme);
}

void ExtensionDialog::resetPresentation()
{
    ++presentation_;
    item_.reset();
    preview_.reset();
    removalHash_.reset();
    retry_ = {};
    primaryAction_ = {};
    reading_ = false;
    instancePath_.clear();
    message_.clear();
    tabs_->setCurrentIndex(0);
}
void ExtensionDialog::showWindow()
{
    if (window_->isMinimized()) window_->showNormal();
    else window_->show();
    window_->raise();
    window_->activateWindow();
}
void ExtensionDialog::reloadItem(const QString& id)
{
    const auto ticket = presentation_;
    const auto host = controls_;
    QPointer<ExtensionDialog> self(this);
    auto inventory = host->list();
    if (!self || ticket != presentation_) return;
    if (!inventory.hasValue()) {
        item_.reset();
        showFailure(inventory.error(), [this, id] {
            message_.clear();
            reloadItem(id);
        });
        return;
    }
    const auto& items = inventory.value();
    const auto found = std::find_if(items.begin(), items.end(), [&](const auto& entry) { return entry.id == id; });
    if (found == items.end()) {
        item_.reset();
        if (message_.isEmpty()) message_ = tr("该扩展已从当前目录移除。");
    } else {
        item_ = *found;
    }
    retry_ = {};
    render();
}
void ExtensionDialog::openItem(const QString& moduleId, const QString& instancePath)
{
    resetPresentation();
    QPointer<ExtensionDialog> self(this);
    const auto ticket = presentation_;
    instancePath_ = instancePath;
    reloadItem(moduleId);
    if (self && ticket == presentation_) showWindow();
}
bool ExtensionDialog::offers(Action action) const
{
    return item_ && std::find(item_->actions.begin(), item_->actions.end(), action) != item_->actions.end();
}
void ExtensionDialog::render()
{
    const auto* row = preview_ ? &preview_->item : item_ ? &*item_ : nullptr;
    const bool actionable = !reading_ && !applying_;
    title_->setText(row ? row->name : reading_ ? tr("安装扩展") : tr("插件详情"));
    icon_->setVisible(row != nullptr);
    if (row) icon_->setPixmap(packageIcon(*row).pixmap(icon_->size()));
    QStringList metadata;
    if (row) {
        if (!row->publisher.isEmpty()) metadata << row->publisher;
        if (!row->version.isEmpty()) metadata << tr("版本 %1").arg(row->version);
        metadata << (row->kind == ExtensionKind::Desktop ? tr("桌面外壳") : tr("业务扩展"));
        if (row->builtin) metadata << tr("内置");
    }
    metadata_->setText(metadata.join(QStringLiteral("   ·   ")));
    metadata_->setVisible(!metadata.isEmpty());
    body_->setText(row && !row->description.isEmpty() ? row->description : tr("暂无扩展介绍。"));
    QStringList identity;
    if (row) {
        if (!row->legacyReference) identity << tr("模块标识   %1").arg(row->id);
        if (!row->pluginId.isEmpty()) identity << tr("插件标识   %1").arg(row->pluginId);
    }
    identity_->setText(identity.join('\n'));
    state_->setText(row ? row->state : QString{});
    state_->setVisible(!state_->text().isEmpty());
    review_->setVisible(preview_.has_value());
    if (preview_)
        review_->setText(tr("文件   %1\nSHA-256   %2\n\n此安装包将运行本机代码。请确认你信任它的来源。")
                         .arg(QFileInfo(preview_->path).fileName(), preview_->sha256));

    const bool management = item_ && !preview_ &&
        (offers(Action::Configure) || offers(Action::AddInstance) || offers(Action::ShowLogs));
    if (!management || removalHash_) tabs_->setCurrentIndex(0);
    tabs_->setTabVisible(1, management);
    tabs_->setVisible(row != nullptr);
    tabs_->setTabEnabled(1, actionable && !removalHash_);
    if (management) {
        const auto ticket = presentation_;
        QPointer<ExtensionDialog> self(this);
        const auto list = instances_;
        const auto plugin = item_->pluginId;
        const auto paths = offers(Action::Configure) ? list(plugin) : QStringList{};
        if (!self || ticket != presentation_) return;
        scope_->setText(tr("当前工作区实例（%1）").arg(item_->instances));
        QSignalBlocker blocked(instance_);
        instance_->clear();
        instance_->addItems(paths);
        const int current = instancePath_.isEmpty() && instance_->count() ? 0 : instance_->findText(instancePath_);
        instance_->setCurrentIndex(current);
        instance_->setVisible(instance_->count() > 0);
        instance_->setEnabled(actionable);
        configure_->setVisible(offers(Action::Configure));
        configure_->setEnabled(actionable && current >= 0);
        add_->setVisible(offers(Action::AddInstance));
        add_->setEnabled(actionable);
        logs_->setVisible(offers(Action::ShowLogs));
        logs_->setEnabled(actionable);
    }
    renderActions();
    const auto message = removalHash_ && message_.isEmpty() && item_
        ? tr("确认卸载 %1？当前工作区的 %2 个实例配置将保留。").arg(item_->name).arg(item_->instances)
        : message_;
    status_->setText(message);
    status_->setVisible(!message.isEmpty());
    retryButton_->setVisible(actionable && bool(retry_));
    logButton_->setVisible(actionable && bool(retry_));
}
void ExtensionDialog::renderActions()
{
    primaryAction_ = {};
    menu_->clear();
    QString title;
    if (preview_) {
        title = tr("确认安装");
        primaryAction_ = [this] { submit(Action::Install, preview_->item.id, preview_->path, preview_->sha256); };
    } else if (removalHash_ && item_) {
        title = tr("确认卸载");
        primaryAction_ = [this] { submit(Action::Uninstall, item_->id, {}, *removalHash_); };
    } else if (item_) {
        for (const auto act : {Action::Restart, Action::UseDesktop, Action::Enable, Action::Disable, Action::Install, Action::Uninstall}) {
            if (!offers(act)) continue;
            const auto caption = act == Action::Install && item_->missing ? tr("重新安装…") : actionTitle(act);
            if (title.isEmpty() && (act != Action::Install || item_->missing) && act != Action::Uninstall) {
                title = caption;
                primaryAction_ = [this, act] { dispatch(act); };
            } else {
                connect(menu_->addAction(caption), &QAction::triggered, this, [this, act] { dispatch(act); });
            }
        }
    }
    const bool ready = !reading_ && !applying_;
    primary_->setText(title);
    primary_->setAccessibleName(title);
    primary_->setVisible(!title.isEmpty());
    primary_->setEnabled(ready);
    more_->setVisible(!menu_->isEmpty());
    more_->setEnabled(ready);
    cancel_->setVisible(ready && (preview_ || removalHash_));
}
void ExtensionDialog::choosePackage(const QString& expectedId)
{
    // Callers may pass item_->id. A native chooser can re-enter the event loop and change the selection.
    const auto target = expectedId;
    const auto legacyPlugin = item_ && item_->legacyReference && item_->id == target ? item_->pluginId : QString{};
    const auto ticket = presentation_;
    QPointer<ExtensionDialog> self(this);
    const auto path = QFileDialog::getOpenFileName(window_->isVisible() ? window_ : window_->parentWidget(),
        tr("选择 Eve 扩展安装包"), {}, tr("Eve 扩展安装包 (*.evepkg)"));
    if (self && ticket == presentation_ && !path.isEmpty()) previewFile(path, target, legacyPlugin);
}
void ExtensionDialog::previewFile(const QString& path, const QString& expectedId, const QString& legacyPlugin)
{
    if (expectedId.isEmpty()) item_.reset();
    const auto ticket = ++presentation_;
    preview_.reset();
    removalHash_.reset();
    retry_ = {};
    tabs_->setCurrentIndex(0);
    reading_ = true;
    message_ = tr("正在读取安装包…");
    QPointer<ExtensionDialog> self(this);
    const auto host = controls_;
    const auto scheduler = scheduler_;
    render();
    if (!self || ticket != presentation_) return;
    showWindow();
    if (!self || ticket != presentation_) return;
    host->preview(path).thenOn(scheduler, [self, ticket, path, expectedId, legacyPlugin](const Result<ExtensionPreview>& result) {
        if (!self || self->presentation_ != ticket) return;
        self->reading_ = false;
        if (!result.hasValue()) {
            self->showFailure(result.error(), [self, path, expectedId, legacyPlugin] {
                if (self) self->previewFile(path, expectedId, legacyPlugin);
            });
            return;
        }
        const auto& preview = result.value();
        const bool legacyMatch = !legacyPlugin.isEmpty() && preview.item.kind == ExtensionKind::Business &&
                                 preview.item.pluginId == legacyPlugin;
        if (!expectedId.isEmpty() && preview.item.id != expectedId && !legacyMatch) {
            self->showFailure(makeError(ErrorCode::InvalidArgument, "所选安装包与要更新的扩展标识不同。"),
                              [self, expectedId] { if (self) self->choosePackage(expectedId); });
            return;
        }
        self->preview_ = preview;
        self->message_.clear();
        self->render();
        if (self && ticket == self->presentation_) self->primary_->setFocus();
    });
}
void ExtensionDialog::showFailure(const ErrorInfo& error, std::function<void()> retry)
{
    message_ = QString::fromStdString(error.message);
    retry_ = std::move(retry);
    render();
}
void ExtensionDialog::presentWorkspaceAction(Action action, const QString& plugin, const QString& path)
{
    auto target = plugin;
    auto instance = path;
    const auto ticket = presentation_;
    QPointer<ExtensionDialog> self(this);
    const auto present = present_;
    window_->hide();
    auto result = present(action, target, instance);
    if (!self || ticket != presentation_) return;
    if (result.hasValue()) resetPresentation();
    else {
        showFailure(result.error(), [this, action, target, instance] { presentWorkspaceAction(action, target, instance); });
        if (self && ticket == presentation_) showWindow();
    }
}
void ExtensionDialog::dispatch(Action action)
{
    if (reading_ || applying_ || !offers(action)) return;
    retry_ = {};
    message_.clear();
    switch (action) {
    case Action::Install: choosePackage(item_->id); return;
    case Action::Uninstall:
        removalHash_ = item_->packageHash;
        render();
        primary_->setFocus();
        return;
    case Action::Configure:
    case Action::AddInstance:
    case Action::ShowLogs:
        presentWorkspaceAction(action, item_->pluginId, instance_->currentText());
        return;
    default: submit(action, item_->id, {}, {}); return;
    }
}
void ExtensionDialog::submit(Action action, const QString& id, const QString& path, const QString& digest)
{
    if (applying_ || reading_) return;
    const auto target = id, archive = path, reviewed = digest;
    const auto ticket = presentation_;
    retry_ = {};
    applying_ = true;
    message_ = tr("正在应用更改…");
    QPointer<ExtensionDialog> self(this);
    const auto host = controls_;
    const auto scheduler = scheduler_;
    render();
    if (!self) return;
    host->apply(action, target, archive, reviewed).thenOn(scheduler,
        [self, ticket, action, target, archive, reviewed](const Result<void>& result) {
            if (!self) return;
            self->applying_ = false;
            // Even a failed activation may have installed the package. Always reload the home inventory.
            const auto changed = self->changed_;
            changed();
            if (!self) return;
            if (ticket != self->presentation_) {
                if (!self->window_->isVisible()) return;
                if (!self->reading_ && !self->preview_ && !self->removalHash_ && self->item_ && self->item_->id == target)
                    self->reloadItem(target);
                else self->render();
                return;
            }
            if (!result.hasValue()) {
                self->showFailure(result.error(), [self, action, target, archive, reviewed] {
                    if (self) self->submit(action, target, archive, reviewed);
                });
                return;
            }
            self->preview_.reset();
            self->removalHash_.reset();
            self->message_ = action == Action::Uninstall ? tr("已卸载扩展。") : tr("更改已应用。");
            self->reloadItem(target);
        });
}
}
