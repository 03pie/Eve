/** @file home.cpp
 * @brief 首页卡片展示扩展与工作区实例，打开详情仍留在当前页面。
 */
#include "appearance_p.hpp"
#include "desktop_text.hpp"
#include "brand_p.hpp"
#include "home_p.hpp"
#include <ElaFlowLayout.h>
#include <ElaIcon.h>
#include <ElaImageCard.h>
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QLayout>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QMetaObject>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QScrollArea>
#include <QResizeEvent>
#include <QSet>
#include <QSignalBlocker>
#include <QString>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace eve::ui::detail {
namespace {
class PluginCard final : public ElaScrollPageArea {
public:
    explicit PluginCard(QWidget* parent) : ElaScrollPageArea(parent) {}
    void setPrimary(QPushButton* button) { primary_ = button; }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        pressed_ = primary_ && primary_->isEnabled() && event->button() == Qt::LeftButton && !childAt(event->pos());
        if (pressed_) { event->accept(); setFocus(Qt::MouseFocusReason); }
        else ElaScrollPageArea::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (!std::exchange(pressed_, false)) { ElaScrollPageArea::mouseReleaseEvent(event); return; }
        event->accept();
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && !childAt(event->pos()) && primary_)
            primary_->click();
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (hasFocus() && primary_ && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)) {
            event->accept();
            if (!event->isAutoRepeat()) primary_->click();
            return;
        }
        ElaScrollPageArea::keyPressEvent(event);
    }
    void paintEvent(QPaintEvent* event) override
    {
        ElaScrollPageArea::paintEvent(event);
        if (!hasFocus()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::PrimaryNormal), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect().adjusted(2, 2, -3, -3), getBorderRadius(), getBorderRadius());
    }

private:
    QPointer<QPushButton> primary_;
    bool pressed_ = false;
};

struct HomeCard {
    HomePlugin plugin;
    QString extensionId{};
    QByteArray icon{};
    bool builtIn = false;
    bool instance = true;
    QString instancePath{};
    QString packageName{};
    QString publisher{};
    QStringList categories{};

    QString searchText() const
    {
        return (QStringList{plugin.title, packageName, publisher, extensionId, plugin.pluginId, instancePath, plugin.id} + categories)
            .join(QLatin1Char('\n'));
    }
};

std::vector<HomeCard> homeCards(const std::vector<HomePlugin>& instances, const std::vector<ExtensionItem>& extensions)
{
    QHash<QString, const ExtensionItem*> byPlugin;
    for (const auto& extension : extensions)
        if (extension.kind == ExtensionKind::Business && !extension.pluginId.isEmpty())
            byPlugin.insert(extension.pluginId, &extension);
    std::vector<HomeCard> cards;
    QSet<QString> represented;
    for (const auto& instance : instances) {
        HomeCard card{instance};
        if (instance.configuration)
            card.instancePath = instance.id.mid(QStringLiteral("configuration:").size());
        if (const auto* extension = byPlugin.value(instance.pluginId)) {
            represented.insert(extension->id);
            card.extensionId = extension->id;
            card.icon = extension->icon;
            card.packageName = extension->name;
            card.publisher = extension->publisher;
            card.categories = extension->categories;
            if (extension->missing || extension->pending) {
                card.plugin.subtitle = extension->state;
                card.plugin.needsAttention = true;
                card.plugin.status.icon = ElaIconType::TriangleExclamation;
            }
        }
        cards.push_back(std::move(card));
    }
    for (const auto& extension : extensions) {
        if (represented.contains(extension.id)) continue;
        HomePlugin plugin{QStringLiteral("extension:") + extension.id, extension.name, extension.state};
        plugin.pluginId = extension.pluginId;
        QStringList identity;
        if (extension.builtin) identity << eve::desktopText("home.builtInDesktop");
        else if (extension.kind == ExtensionKind::Desktop) identity << eve::desktopText("home.desktopExtension");
        else if (extension.kind == ExtensionKind::Language) identity << eve::desktopText("home.languageExtension");
        if (!extension.publisher.isEmpty()) identity << extension.publisher;
        if (!extension.version.isEmpty()) identity << extension.version;
        plugin.identity = identity.isEmpty() ? extension.id : identity.join(QStringLiteral(" · "));
        plugin.needsAttention = extension.missing || extension.pending;
        plugin.status.icon = plugin.needsAttention ? ElaIconType::TriangleExclamation :
                             extension.current ? ElaIconType::CircleCheck :
                             extension.kind == ExtensionKind::Language ? ElaIconType::Language : ElaIconType::PuzzlePiece;
        cards.push_back({std::move(plugin), extension.id, extension.icon, extension.builtin, false, {}, {}, extension.publisher,
                         extension.categories});
    }
    return cards;
}

/** @brief 品牌背景使用原生绘制，缩放时保持清晰，不依赖演示仓库的大体积图片。 */
QImage banner()
{
    QImage image(1600, 380, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0, 0, 1600, 380);
    gradient.setColorAt(0, QColor("#102644"));
    gradient.setColorAt(.48, QColor("#234c82"));
    gradient.setColorAt(1, QColor("#6652a3"));
    painter.fillRect(image.rect(), gradient);
    for (int i = 0; i < 7; ++i) {
        painter.setPen(QPen(QColor(190, 221, 255, 35), 2));
        painter.drawEllipse(QPointF(1330, 130), 100 + i * 60, 100 + i * 60);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(99, 223, 246, 70));
    painter.drawEllipse(QPointF(1150, 190), 100, 100);
    painter.setBrush(QColor(211, 190, 255, 65));
    painter.drawRoundedRect(QRectF(1330, 195, 160, 160), 32, 32);
    return image;
}
/** @brief 实例标识决定卡片配色，使用 Ela 图标，不借用插件提供的图像对象。 */
QPixmap pluginIcon(const QString& id)
{
    const QColor colors[]{QColor("#3476cb"), QColor("#7c5cc4"), QColor("#168c96"), QColor("#c17b38")};
    auto color = colors[qHash(id) % 4];
    QPixmap pixmap(96, 96);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(pixmap.rect(), 20, 20);
    painter.drawPixmap(24, 24,
                       ElaIcon::getInstance()->getElaIcon(ElaIconType::PuzzlePiece, 42, Qt::white).pixmap(48, 48));
    return pixmap;
}
} // namespace

HomePage::HomePage()
{
    setObjectName("EveWelcomePage");
    setWindowTitle(eve::desktopText("home.evePluginWorkbench"));
    setTitleVisible(false);
    setContentsMargins(16, 12, 16, 12);
    auto content = new QWidget;
    content->setWindowTitle(windowTitle());
    auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 20);
    layout->setSpacing(16);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    auto hero = new ElaImageCard(content);
    hero->setObjectName("EveHomeBanner");
    hero->setCardImage(banner());
    hero->setBorderRadius(14);
    hero->setMinimumHeight(116);
    hero->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    bannerRow_ = new QBoxLayout(QBoxLayout::LeftToRight, hero);
    bannerRow_->setContentsMargins(24, 16, 24, 16);
    bannerRow_->setSpacing(12);
    auto summary = new QVBoxLayout;
    summary->setSpacing(4);
    auto title = new ElaText(windowTitle(), 24, hero);
    title->setIsBold(true);
    count_ = new ElaText(hero);
    count_->setTextPixelSize(14);
    count_->setObjectName("EveInstanceSummary");
    for (auto text : {title, count_.data()}) {
        text->setStyleSheet("color:white;background:transparent;");
        summary->addWidget(text);
    }
    bannerRow_->addLayout(summary, 1);
    add_ = new ActionButton(eve::desktopText("home.addPluginInstance"), hero);
    add_->setObjectName("EveWelcomePlugins");
    primaryButton(add_);
    bannerRow_->addWidget(add_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    install_ = new ActionButton(eve::desktopText("home.installExtension"), hero);
    install_->setObjectName("EveHomeInstallExtension");
    primaryButton(install_);
    bannerRow_->addWidget(install_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    install_->hide();
    layout->addWidget(hero);
    auto label = new ElaText(eve::desktopText("home.plugins"), 20, content);
    label->setIsBold(true);
    auto heading = new QHBoxLayout;
    heading->addWidget(label);
    heading->addStretch();
    search_ = new ElaLineEdit(content);
    search_->setObjectName("EveHomeExtensionSearch");
    search_->setAccessibleName(eve::desktopText("home.searchPlugins"));
    search_->setPlaceholderText(eve::desktopText("home.searchByNamePublisherOrID"));
    search_->setMaximumWidth(420);
    search_->setClearButtonEnabled(true);
    search_->hide();
    heading->addWidget(search_, 1);
    layout->addLayout(heading);
    error_ = new ElaText(content);
    error_->setObjectName("EveHomeError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    error_->hide();
    layout->addWidget(error_);
    cards_ = new QWidget(content);
    cards_->setObjectName("EvePluginCards");
    flow_ = new ElaFlowLayout(cards_, 0, 12, 12);
    flow_->setIsAnimation(false);
    layout->addWidget(cards_);
    empty_ = new QWidget(content);
    empty_->setObjectName("EveEmptyInstances");
    emptyRow_ = new QBoxLayout(QBoxLayout::LeftToRight, empty_);
    emptyRow_->setContentsMargins(0, 0, 0, 0);
    auto emptyText = new ElaText(eve::desktopText("home.noPluginInstancesYet"), 14, empty_);
    emptyText->setObjectName("EveHomeEmpty");
    secondaryText(emptyText);
    emptyRow_->addWidget(emptyText);
    emptyRow_->addStretch();
    layout->addWidget(empty_);
    noMatches_ = new ElaText(eve::desktopText("home.noMatchingPluginsFound"), 14, content);
    noMatches_->setObjectName("EveHomeNoMatchingExtensions");
    secondaryText(noMatches_);
    noMatches_->hide();
    layout->addWidget(noMatches_);
    layout->addStretch();
    connect(search_, &QLineEdit::textChanged, this, [this] { applyExtensionSearch(false); });
    addCentralWidget(content);
}

void HomePage::setExtensions(std::vector<ExtensionItem> inventory,
                             std::function<void(const QString&, const QString&)> showDetails)
{
    extensions_ = std::move(inventory);
    showDetails_ = std::move(showDetails);
    install_->setVisible(bool(showDetails_));
    add_->setVisible(!showDetails_);
    search_->setVisible(bool(showDetails_));
}

void HomePage::focusExtensions(const QString& category)
{
    if (!category.isEmpty()) search_->setText(QStringLiteral("category:") + category);
    if (search_->isVisible()) {
        if (auto scroll = findChild<QScrollArea*>()) scroll->ensureWidgetVisible(search_);
        search_->setFocus(Qt::ShortcutFocusReason);
        search_->selectAll();
        return;
    }
    if (install_->isVisible()) install_->setFocus(Qt::ShortcutFocusReason);
}

void HomePage::focusInstance(const QString& key)
{
    if (auto card = items_.value(key)) {
        if (card->isHidden()) search_->clear();
        if (auto manage = card->findChild<QPushButton*>("EvePluginManage")) {
            if (auto scroll = findChild<QScrollArea*>())
                scroll->ensureWidgetVisible(manage);
            manage->setFocus(Qt::OtherFocusReason);
        }
    }
}

void HomePage::resizeEvent(QResizeEvent* event)
{
    ElaScrollPage::resizeEvent(event);
    const auto direction = width() < 560 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    bannerRow_->setDirection(direction);
    emptyRow_->setDirection(direction);
}

void HomePage::applyExtensionSearch(bool layoutChanged)
{
    const auto query = showDetails_ ? search_->text().trimmed() : QString{};
    const auto terms = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const bool filterCategory = query.startsWith(QStringLiteral("category:"), Qt::CaseInsensitive);
    const auto category = query.mid(9).trimmed();
    bool hasMatch = false;
    for (auto card : items_) {
        if (!card) continue;
        const auto text = card->property("EveExtensionSearchText").toString();
        const bool matches = filterCategory ? card->property("EveExtensionCategories").toStringList().contains(category, Qt::CaseInsensitive) :
            std::all_of(terms.begin(), terms.end(), [&](const QString& term) {
            return text.contains(term, Qt::CaseInsensitive);
        });
        hasMatch |= matches;
        layoutChanged |= card->isHidden() == matches;
        // Set visibility explicitly for newly added cards before the layout's deferred show.
        card->setVisible(matches);
    }
    cards_->setVisible(hasMatch);
    empty_->setVisible(items_.isEmpty() && terms.isEmpty());
    noMatches_->setVisible(!hasMatch && !terms.isEmpty());
    if (layoutChanged) {
        flow_->invalidate();
        cards_->updateGeometry();
    }
}

void HomePage::setPlugins(const std::vector<HomePlugin>& plugins,
                          std::function<Result<void>(const QString&, bool, std::uint64_t)> power,
                          std::function<void(const std::function<void()>&)> access,
                          std::function<Result<void>(const QString&, bool)> manage)
{
    power_ = std::move(power);
    access_ = std::move(access);
    manage_ = std::move(manage);
    bool layoutChanged = false;
    QSet<QString> retained;
    const auto cards = homeCards(plugins, extensions_);
    for (const auto& card : cards) {
        const auto& plugin = card.plugin;
        retained.insert(plugin.id);
        auto item = items_.value(plugin.id);
        if (!item) {
            layoutChanged = true;
            auto area = new PluginCard(cards_);
            item = area;
            item->setObjectName("EvePluginCard");
            item->setProperty("EvePluginId", plugin.id);
            item->setFixedWidth(280);
            item->setMinimumHeight(144);
            item->setMaximumHeight(QWIDGETSIZE_MAX);
            auto body = new QVBoxLayout(item);
            body->setContentsMargins(14, 14, 14, 14);
            body->setSpacing(10);
            body->setSizeConstraint(QLayout::SetNoConstraint);
            auto header = new QHBoxLayout;
            header->setSpacing(12);
            auto icon = new ElaText(item);
            icon->setObjectName("EvePluginIcon");
            icon->setFixedSize(48, 48);
            header->addWidget(icon);
            auto text = new QVBoxLayout;
            text->setSpacing(4);
            auto title = new ElaText(item);
            title->setObjectName("EvePluginTitle");
            title->setTextFormat(Qt::PlainText);
            title->setTextPixelSize(16);
            title->setIsBold(true);
            auto status = new ElaText(item);
            status->setObjectName("EvePluginStatus");
            status->setTextFormat(Qt::PlainText);
            status->setTextPixelSize(13);
            text->addWidget(title);
            auto statusRow = new QHBoxLayout;
            statusRow->setSpacing(6);
            auto stateIcon = new ElaText(item);
            stateIcon->setObjectName("EvePluginStateIcon");
            stateIcon->setFixedSize(16, 16);
            statusRow->addWidget(stateIcon);
            statusRow->addWidget(status, 1);
            text->addLayout(statusRow);
            header->addLayout(text, 1);
            body->addLayout(header);
            auto footer = new QHBoxLayout;
            auto identity = new ElaText(eve::desktopText("home.instance").arg(plugin.id), 12, item);
            identity->setObjectName("EvePluginIdentity");
            identity->setTextFormat(Qt::PlainText);
            secondaryText(identity);
            footer->addWidget(identity, 1);
            auto button = new KeyboardToggleSwitch(item);
            button->setObjectName("EvePluginPower");
            button->setAttribute(Qt::WA_NoMousePropagation);
            // Align the identity row with adjacent instances even when a package has no power switch.
            identity->setMinimumHeight(button->minimumHeight());
            footer->addWidget(button, 0, Qt::AlignVCenter);
            body->addLayout(footer);
            auto tools = new QHBoxLayout;
            for (const auto& entry :
                 {std::make_pair("EvePluginManage", eve::desktopText("home.manage")),
                  std::make_pair("EvePluginConfigure", eve::desktopText("home.configure"))}) {
                auto action = new ActionButton(entry.second, item);
                action->setObjectName(QString::fromLatin1(entry.first));
                action->setAttribute(Qt::WA_NoMousePropagation);
                action->setMinimumHeight(30);
                action->setAccessibleName(plugin.title + " · " + action->text());
                const bool configuration = action->objectName() == "EvePluginConfigure";
                if (!configuration) area->setPrimary(action);
                action->setEnabled(!configuration || plugin.configuration);
                if (configuration)
                    action->setToolTip(plugin.configuration
                                           ? eve::desktopText("home.editAndSaveTheConfigurationForThis")
                                           : eve::desktopText("home.theHostCreatedThisInstanceOutsideThe"));
                tools->addWidget(action);
                connect(action, &QPushButton::clicked, this, [this, item, id = plugin.id, configuration] {
                    if (!item || !item->isEnabled() || !access_)
                        return;
                    const auto extension = item->property("EveExtensionId").toString();
                    if (!configuration && !extension.isEmpty() && showDetails_) {
                        auto details = showDetails_;
                        auto access = access_;
                        const auto path = item->property("EveInstancePath").toString();
                        access([details, extension, path] { details(extension, path); });
                        return;
                    }
                    if (!manage_) return;
                    auto manage = manage_;
                    auto access = access_;
                    access([this, id, configuration, manage] {
                        QPointer<HomePage> alive(this);
                        auto result = manage(id, configuration);
                        if (!alive)
                            return;
                        if (!result.hasValue())
                            error_->setText(QString::fromStdString(result.error().message));
                        if (alive)
                            error_->setVisible(!result.hasValue());
                    });
                });
            }
            body->addLayout(tools);
            for (auto label : item->findChildren<ElaText*>()) label->setAttribute(Qt::WA_TransparentForMouseEvents);
            flow_->addWidget(item);
            items_.insert(plugin.id, item);
            // 发出时捕获修订，排队执行；刷新后的新修订不能覆盖旧点击的 CAS。
            connect(button, &ElaToggleSwitch::toggled, this,
                    [this, button = QPointer<ElaToggleSwitch>(button), id = plugin.id](bool enabled) {
                        if (!button || !button->isEnabled() || !power_)
                            return;
                        auto invoke = power_;
                        auto access = access_;
                        const auto revision = button->property("EveConfigurationRevision").toULongLong();
                        auto execute = [this, button, id, revision, enabled, invoke] {
                            if (!button || !button->isEnabled())
                                return;
                            QPointer<HomePage> alive(this);
                            button->setEnabled(false);
                            if (!alive)
                                return;
                            auto result = invoke(id, enabled, revision);
                            if (!alive)
                                return;
                            QPointer<ElaText> message(error_);
                            if (!result.hasValue())
                                message->setText(QString::fromStdString(result.error().message));
                            if (message)
                                message->setVisible(!result.hasValue());
                        };
                        QMetaObject::invokeMethod(
                            this,
                            [execute, access] {
                                if (access)
                                    access(execute);
                                else
                                    execute();
                            },
                            Qt::QueuedConnection);
                    });
        }
        auto title = item->findChild<ElaText*>("EvePluginTitle");
        auto status = item->findChild<ElaText*>("EvePluginStatus");
        auto button = item->findChild<ElaToggleSwitch*>("EvePluginPower");
        auto manageButton = item->findChild<QPushButton*>("EvePluginManage");
        auto configure = item->findChild<QPushButton*>("EvePluginConfigure");
        const bool canOpen = !card.extensionId.isEmpty() || plugin.canInspect;
        item->setProperty("EveExtensionId", card.extensionId);
        item->setProperty("EveInstancePath", card.instancePath);
        item->setProperty("EveExtensionSearchText", card.searchText());
        item->setProperty("EveExtensionCategories", card.categories);
        item->setFocusPolicy(canOpen ? Qt::StrongFocus : Qt::NoFocus);
        item->setCursor(canOpen ? Qt::PointingHandCursor : Qt::ArrowCursor);
        item->setAccessibleName(plugin.title);
        item->setAccessibleDescription(plugin.subtitle);
        manageButton->setEnabled(canOpen);
        manageButton->setAccessibleName(plugin.title + QStringLiteral(" · ") + manageButton->text());
        configure->setVisible(card.instance);
        configure->setAccessibleName(plugin.title + QStringLiteral(" · ") + configure->text());
        button->setVisible(card.instance);
        auto icon = item->findChild<ElaText*>("EvePluginIcon");
        if (!icon->property("EveIconSource").isValid() || icon->property("EveIconSource").toByteArray() != card.icon) {
            QPixmap artwork;
            if (!card.icon.isEmpty()) artwork.loadFromData(card.icon, "PNG");
            if (artwork.isNull()) artwork = card.builtIn ? eveApplicationIcon().pixmap(48) : pluginIcon(plugin.id);
            icon->setPixmap(artwork.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            icon->setProperty("EveIconSource", card.icon);
        }
        button->setProperty("EveConfigurationRevision", QVariant::fromValue<qulonglong>(plugin.revision));
        auto identity = item->findChild<ElaText*>("EvePluginIdentity");
        const auto identityText =
            plugin.identity.isEmpty() ? eve::desktopText("home.instance").arg(plugin.id) : plugin.identity;
        const auto summaryIdentity = card.packageName.isEmpty() ? identityText
                                    : card.packageName + QStringLiteral(" · ") + card.instancePath;
        identity->setText(identity->fontMetrics().elidedText(summaryIdentity, Qt::ElideMiddle, 185));
        identity->setAccessibleName(identityText);
        identity->setToolTip(QStringLiteral("<span>%1</span>").arg(identityText.toHtmlEscaped()));
        const auto name = title->fontMetrics().elidedText(plugin.title, Qt::ElideRight, 192);
        if (title->text() != name)
            title->setText(name);
        title->setAccessibleName(plugin.title);
        const auto state = plugin.busy ? eve::desktopText("home.working").arg(plugin.subtitle) : plugin.subtitle;
        if (status->text() != state)
            status->setText(state);
        status->setWordWrap(true);
        auto stateIcon = item->findChild<ElaText*>("EvePluginStateIcon");
        auto presentation = plugin.status;
        if (plugin.busy)
            presentation.icon = ElaIconType::ArrowsRotate;
        stateIcon->setPixmap(statusIcon(presentation).pixmap(16, 16));
        stateIcon->setAccessibleName(state);
        button->setEnabled(static_cast<bool>(power_) && !plugin.busy &&
                           (plugin.canStop || plugin.canLoad || plugin.canRetry));
        if (button->getIsToggled() != plugin.enabled) {
            const QSignalBlocker blocked(button);
            button->setIsToggled(plugin.enabled);
        }
        const auto action = plugin.configuration ? (plugin.enabled ? eve::desktopText("home.disableConfigurationEntry")
                                                                   : eve::desktopText("home.enableConfigurationEntry"))
                            : plugin.canStop     ? eve::desktopText("home.stopPlugin")
                            : plugin.canRetry    ? eve::desktopText("home.retryLoading")
                                                 : eve::desktopText("home.loadPlugin");
        const auto hint = !power_       ? eve::desktopText("home.manageThisInstanceFromItsPlugin")
                          : plugin.busy ? eve::desktopText("home.anOperationIsAlreadyInProgress")
                          : !(plugin.canStop || plugin.canLoad || plugin.canRetry)
                              ? eve::desktopText("home.changesAreUnavailableCheckThePluginManager")
                              : action;
        button->setToolTip(hint);
        button->setAccessibleDescription(hint);
        button->setAccessibleName(action);
        item->setToolTip(plugin.title.toHtmlEscaped() + "<br>" + plugin.subtitle.toHtmlEscaped());
    }
    for (auto it = items_.begin(); it != items_.end();) {
        if (retained.contains(it.key())) {
            ++it;
            continue;
        }
        auto item = it.value();
        it = items_.erase(it);
        layoutChanged = true;
        if (item) {
            item->setEnabled(false);
            item->hide();
            flow_->removeWidget(item);
            item->deleteLater();
        }
    }
    int running = 0, attention = 0;
    for (const auto& card : cards) {
        running += card.plugin.running;
        attention += card.plugin.needsAttention;
    }
    auto summary = eve::desktopText("home.instancesRunning").arg(plugins.size()).arg(running);
    if (showDetails_)
        summary.prepend(eve::desktopText("home.extensions").arg(extensions_.size()));
    if (attention)
        summary += eve::desktopText("home.needAttention").arg(attention);
    count_->setText(summary);
    if (!showDetails_ && cards.empty() && emptyRow_->indexOf(add_) < 0)
        emptyRow_->insertWidget(1, add_, 0, Qt::AlignLeft);
    else if (!cards.empty() && bannerRow_->indexOf(add_) < 0)
        bannerRow_->addWidget(add_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    applyExtensionSearch(layoutChanged);
}
} // namespace eve::ui::detail
