#include "extension_dialog_p.hpp"
#include "appearance_p.hpp"
#include "brand_p.hpp"
#include "extension_facts_p.hpp"
#include "extension_features_p.hpp"
#include "extension_readme_p.hpp"
#include "fixed_tabs_p.hpp"
#include "workbench_dialog_p.hpp"
#include <ElaComboBox.h>
#include <ElaDialog.h>
#include <ElaFlowLayout.h>
#include <ElaIcon.h>
#include <ElaMenu.h>
#include <ElaScrollArea.h>
#include <ElaToolButton.h>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPointer>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace eve::ui::detail {
namespace {
using Action = ExtensionAction;
QString actionTitle(Action action)
{
    switch (action) {
    case Action::Install: return QObject::tr("从文件安装其他版本或重装…");
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
QString installCaption(const ExtensionPreview& preview)
{
    if (preview.installedVersion.isEmpty()) return QObject::tr("确认安装");
    // The manifest uses decimal components that can exceed a native integer.
    const auto current = preview.installedVersion.split('.');
    const auto next = preview.item.version.split('.');
    for (int i = 0; i < std::max(current.size(), next.size()); ++i) {
        const auto left = current.value(i, QStringLiteral("0"));
        const auto right = next.value(i, QStringLiteral("0"));
        if (left == right) continue;
        const bool newer = right.size() == left.size() ? right > left : right.size() > left.size();
        return newer ? QObject::tr("确认更新") : QObject::tr("确认安装旧版");
    }
    return QObject::tr("确认重装");
}
QPixmap packageIcon(const ExtensionItem& item, const QSize& size, qreal scale)
{
    const auto pixels = size * scale;
    QPixmap image;
    if (!item.icon.isEmpty() && image.loadFromData(item.icon, "PNG")) {
        image = image.scaled(pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else if (item.builtin && item.kind == ExtensionKind::Desktop) {
        image = eveApplicationIcon().pixmap(pixels);
    } else {
        const auto tint = eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::PrimaryNormal);
        image = ElaIcon::getInstance()->getElaIcon(ElaIconType::PuzzlePiece, pixels.width(), tint).pixmap(pixels);
    }
    image.setDevicePixelRatio(scale);
    return image;
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
ElaScrollArea* scrollArea(QWidget* parent)
{
    auto scroll = new ElaScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}
void setScrollPage(QScrollArea* scroll, QWidget* page)
{
    scroll->setWidget(page);
    // QScrollArea::setWidget enables native background filling; keep the Ela dialog surface visible.
    page->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
}
QVBoxLayout* addTab(QTabWidget* tabs, const QString& title, int& index)
{
    auto scroll = scrollArea(tabs);
    auto page = new QWidget(scroll);
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 18, 8, 8);
    layout->setSpacing(12);
    setScrollPage(scroll, page);
    index = tabs->addTab(scroll, title);
    return layout;
}
QWidget* actionGroup(QWidget* parent, ElaPushButton* action, ElaPushButton* options)
{
    auto group = new QWidget(parent);
    auto layout = new QHBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(action);
    layout->addWidget(options);
    return group;
}
ElaPushButton* menuButton(QWidget* parent, const QString& caption, const char* id)
{
    auto result = button(parent, QString(QChar(ElaIconType::ChevronDown)), id);
    // ElaPushButton draws its own text rather than a QPushButton QIcon.
    auto font = result->font();
    font.setFamily(QStringLiteral("ElaAwesome"));
    font.setPixelSize(13);
    result->setFont(font);
    result->setFixedWidth(30);
    result->setAccessibleName(caption);
    result->setToolTip(caption);
    return result;
}

class DetailColumns final : public QWidget {
public:
    DetailColumns(QWidget* body, QWidget* facts, QWidget* parent)
        : QWidget(parent), facts_(facts), columns_(new QHBoxLayout(this))
    {
        columns_->setContentsMargins(2, 12, 0, 0);
        columns_->setSpacing(16);
        columns_->addWidget(body, 1);
        columns_->addWidget(facts_);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        const bool narrow = width() < 600;
        columns_->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        facts_->setMinimumWidth(narrow ? 0 : 225);
        facts_->setMaximumWidth(narrow ? QWIDGETSIZE_MAX : 290);
        facts_->setMaximumHeight(narrow ? 178 : QWIDGETSIZE_MAX);
    }

private:
    QWidget* facts_;
    QBoxLayout* columns_;
};

}

ExtensionDialog::ExtensionDialog(std::shared_ptr<qt::QtScheduler> scheduler, std::shared_ptr<ExtensionControls> controls,
                                 Workspace workspace, Present present, std::function<void()> changed, QWidget* owner)
    : QObject(createWorkbenchDialog(owner)), scheduler_(std::move(scheduler)), controls_(std::move(controls)),
      workspace_(std::move(workspace)), present_(std::move(present)), changed_(std::move(changed)),
      window_(static_cast<ElaDialog*>(parent()))
{
    window_->setObjectName("EveExtensionDialog");
    window_->setWindowTitle(tr("插件详情"));
    window_->setIsStayTop(false);
    auto layout = new QVBoxLayout(window_);
    // ElaAppBar already reserves its height in the window's contents margins.
    layout->setContentsMargins(24, 14, 24, 20);
    layout->setSpacing(14);
    auto header = new QHBoxLayout;
    header->setSpacing(20);
    icon_ = label(window_, 14, "EveExtensionIcon");
    icon_->setFixedSize(96, 96);
    icon_->setAlignment(Qt::AlignCenter);
    header->addWidget(icon_, 0, Qt::AlignVCenter);
    auto headerContent = new QWidget(window_);
    auto heading = new QVBoxLayout(headerContent);
    heading->setContentsMargins(0, 0, 0, 0);
    heading->setSpacing(5);
    title_ = label(window_, 27, "EveExtensionTitle");
    title_->setIsBold(true);
    heading->addWidget(title_);
    metadata_ = label(window_, 14, "EveExtensionMetadata");
    secondaryText(metadata_);
    heading->addWidget(metadata_);
    summary_ = new StatusBarText({}, window_);
    summary_->setObjectName("EveExtensionSummary");
    heading->addWidget(summary_);

    auto actions = new ElaFlowLayout(0, 8, 5);
    actions->setIsAnimation(false);
    primary_ = button(window_, {}, "EveExtensionPrimary");
    primaryButton(primary_);
    workspaceOptions_ = menuButton(window_, tr("其他工作区操作"), "EveExtensionWorkspaceOptions");
    primaryButton(workspaceOptions_);
    workspaceMenu_ = new ElaMenu(workspaceOptions_);
    workspaceOptions_->setMenu(workspaceMenu_);
    primaryGroup_ = actionGroup(window_, primary_, workspaceOptions_);

    uninstall_ = button(window_, actionTitle(Action::Uninstall), "EveExtensionUninstall");
    packageOptions_ = menuButton(window_, tr("其他安装包操作"), "EveExtensionPackageOptions");
    packageMenu_ = new ElaMenu(packageOptions_);
    packageOptions_->setMenu(packageMenu_);
    packageGroup_ = actionGroup(window_, uninstall_, packageOptions_);

    more_ = new ElaToolButton(window_);
    more_->setObjectName("EveExtensionMore");
    more_->setAccessibleName(tr("管理扩展"));
    more_->setToolTip(tr("管理扩展"));
    more_->setFocusPolicy(Qt::StrongFocus);
    more_->setFixedSize(40, 30);
    more_->setIsTransparent(true);
    more_->setElaIcon(ElaIconType::Gear);
    menu_ = new ElaMenu(more_);
    more_->setMenu(menu_);

    cancel_ = button(window_, tr("取消"), "EveExtensionCancel");
    actions->addWidget(primaryGroup_);
    actions->addWidget(packageGroup_);
    actions->addWidget(more_);
    actions->addWidget(cancel_);
    heading->addLayout(actions);
    header->addWidget(headerContent, 1, Qt::AlignVCenter);
    layout->addLayout(header);

    tabs_ = new FixedDiagnosticsTabs(window_);
    tabs_->setObjectName("EveExtensionTabs");
    auto introduction = new QWidget(tabs_);
    auto intro = new QVBoxLayout(introduction);
    intro->setContentsMargins(0, 0, 0, 0);
    intro->setSpacing(8);
    review_ = label(introduction, 14, "EveExtensionPackageReview");
    intro->addWidget(review_);
    body_ = new ExtensionReadmeView(introduction);
    body_->setObjectName("EveExtensionDescription");
    body_->setAccessibleName(tr("扩展文档"));
    intro->addWidget(body_, 1);
    facts_ = new ExtensionFacts(tabs_, [self = QPointer<ExtensionDialog>(this)](const QString& category) {
        if (!self || !self->categorySelected || !self->item_ || self->preview_ || self->removalHash_ ||
            self->reading_ || self->applying_) return;
        const auto selected = self->categorySelected;
        self->window_->reject();
        if (self) selected(category);
    });
    tabs_->addTab(new DetailColumns(introduction, facts_, tabs_), tr("详情"));

    features_ = new ExtensionFeaturesView(tabs_);
    featuresTab_ = tabs_->addTab(features_, tr("功能"));
    auto manage = addTab(tabs_, tr("工作区实例"), managementTab_);
    auto pane = manage->parentWidget();
    scope_ = label(pane, 16, "EveExtensionWorkspace");
    scope_->setIsBold(true);
    manage->addWidget(scope_);
    instance_ = new ElaComboBox(pane);
    instance_->setObjectName("EveExtensionInstance");
    instance_->setAccessibleName(tr("工作区实例"));
    instance_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    manage->addWidget(instance_);
    auto operations = new ElaFlowLayout(0, 8, 5);
    operations->setIsAnimation(false);
    configure_ = button(pane, actionTitle(Action::Configure), "EveExtensionConfigure");
    add_ = button(pane, actionTitle(Action::AddInstance), "EveExtensionAddInstance");
    logs_ = button(pane, actionTitle(Action::ShowLogs), "EveExtensionShowLogs");
    operations->addWidget(configure_);
    operations->addWidget(add_);
    operations->addWidget(logs_);
    manage->addLayout(operations);
    manage->addStretch();
    changelog_ = new ExtensionReadmeView(tabs_);
    changelog_->setObjectName("EveExtensionChangelog");
    changelog_->setAccessibleName(tr("变更记录"));
    tabs_->addTab(changelog_, tr("变更记录"));
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
    connect(uninstall_, &QPushButton::clicked, this, [this] { dispatch(Action::Uninstall); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (!item_) { window_->reject(); return; }
        preview_.reset();
        removalHash_.reset();
        retry_ = {};
        message_.clear();
        tabs_->setCurrentIndex(0);
        reloadItem(item_->id);
    });
    connect(instance_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        instancePath_ = instance_->currentText();
        configure_->setEnabled(!applying_ && index >= 0);
        renderActions();
    });
    connect(configure_, &QPushButton::clicked, this, [this] { dispatch(Action::Configure); });
    connect(add_, &QPushButton::clicked, this, [this] { dispatch(Action::AddInstance); });
    connect(logs_, &QPushButton::clicked, this, [this] { dispatch(Action::ShowLogs); });
    connect(retryButton_, &QPushButton::clicked, this, [this] { auto act = retry_; if (act) act(); });
    connect(logButton_, &QPushButton::clicked, this, [this] { presentWorkspaceAction(Action::ShowLogs, {}, {}); });
    connect(window_, &QDialog::finished, this, [this] { resetPresentation(); });
    auto theme = [this](ElaThemeType::ThemeMode mode) {
        window_->setPalette(workbenchPalette(window_->palette(), mode));
        updateIcon();
    };
    theme(eTheme->getThemeMode());
    connect(eTheme, &ElaTheme::themeModeChanged, this, theme);
    window_->installEventFilter(this);
}

bool ExtensionDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window_ && event->type() == QEvent::Resize) {
        const auto width = static_cast<QResizeEvent*>(event)->size().width();
        const int size = width >= 900 ? 128 : width >= 600 ? 96 : 72;
        if (icon_->width() != size) {
            icon_->setFixedSize(size, size);
            updateIcon();
        }
    }
    return QObject::eventFilter(watched, event);
}
void ExtensionDialog::updateIcon()
{
    const auto* item = preview_ ? &preview_->item : item_ ? &*item_ : nullptr;
    if (item) icon_->setPixmap(packageIcon(*item, icon_->size(), icon_->devicePixelRatioF()));
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
    body_->verticalScrollBar()->setValue(0);
    facts_->verticalScrollBar()->setValue(0);
    features_->verticalScrollBar()->setValue(0);
    changelog_->verticalScrollBar()->setValue(0);
}
void ExtensionDialog::placeWindow()
{
    window_->layout()->activate();
    const auto* owner = window_->parentWidget()->window();
    const auto screen = owner->screen()->availableGeometry();
    const auto frame = owner->frameGeometry();
    const auto ownerSize = frame.size().boundedTo(screen.size());
    const auto preferred = ownerSize == ownerSize_ ? window_->size() :
        QSize(qRound(ownerSize.width() * 0.80), qRound(ownerSize.height() * 0.77));
    const auto minimum = window_->minimumSizeHint().expandedTo(window_->minimumSize());
    const auto size = preferred.expandedTo(minimum).boundedTo(screen.size());
    QRect bounds(QPoint{}, size);
    bounds.moveCenter(frame.center());
    bounds.moveLeft(std::clamp(bounds.left(), screen.left(), screen.right() - size.width() + 1));
    bounds.moveTop(std::clamp(bounds.top(), screen.top(), screen.bottom() - size.height() + 1));
    window_->setGeometry(bounds);
    ownerSize_ = ownerSize;
}

void ExtensionDialog::showWindow()
{
    if (window_->isMinimized())
        window_->setWindowState(window_->windowState() & ~Qt::WindowMinimized);
    else if (!window_->isVisible() && !window_->isMaximized())
        placeWindow();
    window_->show();
    window_->raise();
    window_->activateWindow();
}
void ExtensionDialog::reloadItem(QString id)
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
    applyInventory(id, inventory.value());
}
void ExtensionDialog::syncInventory(const std::vector<ExtensionItem>& inventory)
{
    if (!window_->isVisible() || !item_ || preview_ || removalHash_ || reading_ || applying_ || retry_)
        return;
    const auto id = item_->id;
    applyInventory(id, inventory);
}
void ExtensionDialog::applyInventory(const QString& id, const std::vector<ExtensionItem>& inventory)
{
    const auto found = std::find_if(inventory.begin(), inventory.end(), [&](const auto& entry) { return entry.id == id; });
    if (found == inventory.end()) {
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
QString ExtensionDialog::featurePlugin() const
{
    return item_ && !preview_ && !reading_ && !item_->missing && item_->kind == ExtensionKind::Business
        ? item_->pluginId : QString{};
}
QString ExtensionDialog::visibleFeaturePlugin() const
{
    return window_->isVisible() && !removalHash_ ? featurePlugin() : QString{};
}
void ExtensionDialog::refreshFeatures(const QString& plugin, const ExtensionWorkspace& workspace)
{
    if (plugin.isEmpty() || plugin != visibleFeaturePlugin()) return;
    renderFeatures(&*item_, workspace);
}
void ExtensionDialog::renderFeatures(const ExtensionItem* item, const ExtensionWorkspace& workspace)
{
    const bool available = features_->setContent(item, workspace);
    tabs_->setTabVisible(featuresTab_, available);
    tabs_->setTabEnabled(featuresTab_, !reading_ && !applying_ && !removalHash_);
}
void ExtensionDialog::render()
{
    const auto* row = preview_ ? &preview_->item : item_ ? &*item_ : nullptr;
    const bool actionable = !reading_ && !applying_;
    title_->setText(row ? row->name : reading_ ? tr("安装扩展") : tr("插件详情"));
    icon_->setVisible(row != nullptr);
    updateIcon();
    QStringList metadata;
    if (row) {
        if (!row->publisher.isEmpty()) metadata << row->publisher;
        if (!row->version.isEmpty()) metadata << tr("版本 %1").arg(row->version);
        metadata << (row->kind == ExtensionKind::Desktop ? tr("桌面外壳") : tr("业务扩展"));
        if (row->builtin) metadata << tr("内置");
    }
    metadata_->setText(metadata.join(QStringLiteral("   ·   ")));
    metadata_->setVisible(!metadata.isEmpty());
    const auto summary = row ? row->description.simplified() : QString{};
    summary_->setText(summary);
    summary_->setVisible(!summary.isEmpty());
    summary_->setToolTip(QStringLiteral("<span>%1</span>").arg(summary.toHtmlEscaped()));
    body_->setContent(row ? row->readme : QString{},
        row && !row->description.isEmpty() ? row->description : tr("暂无扩展介绍。"));
    const bool hasChangelog = row && !row->changelog.isEmpty();
    tabs_->setTabVisible(tabs_->indexOf(changelog_), hasChangelog);
    changelog_->setContent(hasChangelog ? row->changelog : QString{}, {});
    facts_->setItem(row, preview_.has_value(), actionable && !removalHash_ && bool(categorySelected));
    review_->setVisible(preview_.has_value());
    if (preview_) {
        const auto versions = preview_->installedVersion.isEmpty() ? QString{} :
            tr("已安装版本   %1    →    所选版本   %2\n").arg(preview_->installedVersion, preview_->item.version);
        review_->setText(versions + tr("文件   %1\nSHA-256   %2\n\n此安装包将运行本机代码。请确认你信任它的来源。")
                         .arg(QFileInfo(preview_->path).fileName(), preview_->sha256));
    }

    const bool management = item_ && !preview_ &&
        (offers(Action::Configure) || offers(Action::AddInstance) || offers(Action::ShowLogs));
    if (removalHash_) tabs_->setCurrentIndex(0);
    tabs_->setTabVisible(managementTab_, management);
    tabs_->setVisible(row != nullptr);
    tabs_->setTabEnabled(managementTab_, actionable && !removalHash_);

    ExtensionWorkspace workspace;
    const auto plugin = featurePlugin();
    const auto readPlugin = !plugin.isEmpty() ? plugin :
        management && offers(Action::Configure) ? item_->pluginId : QString{};
    if (!readPlugin.isEmpty()) {
        const auto ticket = presentation_;
        QPointer<ExtensionDialog> self(this);
        const auto read = workspace_;
        workspace = read(readPlugin);
        if (!self || ticket != presentation_) return;
    }
    const auto* featureItem = reading_ ? nullptr : preview_ ? &preview_->item : item_ ? &*item_ : nullptr;
    renderFeatures(featureItem, plugin.isEmpty() ? ExtensionWorkspace{} : workspace);
    if (management) {
        const auto paths = offers(Action::Configure) ? workspace.paths : QStringList{};
        scope_->setText(tr("当前工作区实例（%1）").arg(item_->instances));
        QSignalBlocker blocked(instance_);
        bool pathsChanged = paths.size() != instance_->count();
        for (int i = 0; !pathsChanged && i < paths.size(); ++i)
            pathsChanged = paths.at(i) != instance_->itemText(i);
        if (pathsChanged) {
            instance_->clear();
            instance_->addItems(paths);
        }
        const int current = instancePath_.isEmpty() && instance_->count() ? 0 : instance_->findText(instancePath_);
        instance_->setCurrentIndex(current);
        if (current >= 0) instancePath_ = instance_->currentText();
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
    const auto retryCaption = preview_ ? tr("重新预览") : tr("重试");
    retryButton_->setText(retryCaption);
    retryButton_->setAccessibleName(retryCaption);
    retryButton_->setVisible(actionable && bool(retry_));
    logButton_->setVisible(actionable && bool(retry_));
}
void ExtensionDialog::renderActions()
{
    primaryAction_ = {};
    workspaceMenu_->clear();
    packageMenu_->clear();
    menu_->clear();
    QString title;
    bool canUninstall = false;
    if (preview_) {
        title = installCaption(*preview_);
        primaryAction_ = [this] { submit(Action::Install, preview_->item.id, preview_->path, preview_->sha256); };
    } else if (removalHash_ && item_) {
        title = tr("确认卸载");
        primaryAction_ = [this] { submit(Action::Uninstall, item_->id, {}, *removalHash_); };
    } else if (item_) {
        canUninstall = offers(Action::Uninstall);
        const auto caption = [this](Action action) {
            if (action == Action::Install && item_->missing) return tr("重新安装…");
            if (action == Action::Enable) return tr("启用工作区实例");
            if (action == Action::Disable) return tr("停用工作区实例");
            return actionTitle(action);
        };
        const auto addAction = [this, &caption](ElaMenu* menu, Action action) {
            if (!offers(action)) return;
            auto entry = menu->addAction(caption(action));
            if (action == Action::Configure) entry->setEnabled(instance_->currentIndex() >= 0);
            connect(entry, &QAction::triggered, this, [this, action] { dispatch(action); });
        };

        const auto workspaceActions = {Action::Restart, Action::UseDesktop, Action::Enable, Action::Disable, Action::AddInstance};
        std::optional<Action> leading;
        for (const auto action : workspaceActions) {
            if (!offers(action)) continue;
            leading = action;
            break;
        }
        const bool repair = item_->missing && !offers(Action::Restart);
        if (offers(Action::Install) && (repair || (!leading && !canUninstall)))
            leading = Action::Install;
        if (leading) {
            title = actionTitle(*leading);
            if (*leading == Action::Install) title = caption(*leading);
            primaryAction_ = [this, action = *leading] { dispatch(action); };
            for (const auto action : workspaceActions)
                if (action != *leading) addAction(workspaceMenu_, action);
            addAction(workspaceMenu_, Action::Configure);
        }
        if (leading != Action::Install)
            addAction(canUninstall ? packageMenu_ : menu_, Action::Install);

        for (const auto action : {Action::Configure, Action::AddInstance, Action::ShowLogs})
            addAction(menu_, action);
        const auto copyId = item_->legacyReference ? item_->pluginId : item_->id;
        if (!copyId.isEmpty()) {
            if (!menu_->isEmpty()) menu_->addSeparator();
            auto copy = menu_->addAction(item_->legacyReference ? tr("复制插件标识") : tr("复制模块标识"));
            connect(copy, &QAction::triggered, this, [copyId] { QApplication::clipboard()->setText(copyId); });
        }
    }
    const bool ready = !reading_ && !applying_;
    primary_->setText(title);
    primary_->setAccessibleName(title);
    primaryGroup_->setVisible(!title.isEmpty());
    primaryGroup_->setEnabled(ready && !(preview_ && retry_));
    workspaceOptions_->setVisible(!workspaceMenu_->isEmpty());
    packageGroup_->setVisible(canUninstall);
    packageGroup_->setEnabled(ready);
    packageOptions_->setVisible(!packageMenu_->isEmpty());
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
    body_->verticalScrollBar()->setValue(0);
    facts_->verticalScrollBar()->setValue(0);
    features_->verticalScrollBar()->setValue(0);
    changelog_->verticalScrollBar()->setValue(0);
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
    if (reading_ || applying_ || preview_ || removalHash_ || !offers(action)) return;
    if (action == Action::Configure && instance_->currentIndex() < 0) return;
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
    const auto install = action == Action::Install ? preview_ : std::nullopt;
    const auto ticket = presentation_;
    retry_ = {};
    applying_ = true;
    message_ = tr("正在应用更改…");
    QPointer<ExtensionDialog> self(this);
    const auto host = controls_;
    const auto scheduler = scheduler_;
    render();
    if (!self) return;
    const auto task = install && host->installReviewed ? host->installReviewed(*install) :
                                                        host->apply(action, target, archive, reviewed);
    task.thenOn(scheduler,
        [self, ticket, action, target, archive, reviewed](const Result<void>& result) {
            if (!self) return;
            // Even a failed activation may have installed the package. Always reload the home inventory.
            const auto changed = self->changed_;
            changed();
            if (!self) return;
            self->applying_ = false;
            if (ticket != self->presentation_) {
                if (!self->window_->isVisible()) return;
                if (!self->reading_ && !self->preview_ && !self->removalHash_ && self->item_)
                    self->reloadItem(self->item_->id);
                else self->render();
                return;
            }
            if (!result.hasValue()) {
                if (action == Action::Uninstall && result.error().code == ErrorCode::ConfigConflict) {
                    self->removalHash_.reset();
                    self->message_ = QString::fromStdString(result.error().message);
                    self->reloadItem(target);
                    return;
                }
                if (action == Action::Install) {
                    self->showFailure(result.error(), [self, target, archive] {
                        if (self) self->previewFile(archive, target, {});
                    });
                    return;
                }
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
