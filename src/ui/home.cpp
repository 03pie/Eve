/** @file home.cpp
 * @brief 以 Ela 背景卡和紧凑状态卡展示插件，卡片不参与页面导航。
 */
#include "appearance_p.hpp"
#include "home_p.hpp"
#include <ElaFlowLayout.h>
#include <ElaIcon.h>
#include <ElaImageCard.h>
#include <ElaPushButton.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QLayout>
#include <QLinearGradient>
#include <QMetaObject>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSet>
#include <QSignalBlocker>
#include <QString>
#include <QVBoxLayout>
#include <QVariant>
#include <functional>
#include <utility>
#include <vector>

namespace cordis::ui::detail {
namespace {
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
    setObjectName("CordisWelcomePage");
    setWindowTitle(QString::fromUtf8("Cordis 插件工作台"));
    setTitleVisible(false);
    setContentsMargins(16, 12, 16, 12);
    auto content = new QWidget;
    content->setWindowTitle(windowTitle());
    auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 20);
    layout->setSpacing(16);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    auto hero = new ElaImageCard(content);
    hero->setObjectName("CordisHomeBanner");
    hero->setCardImage(banner());
    hero->setBorderRadius(14);
    hero->setMinimumHeight(220);
    hero->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto body = new QVBoxLayout(hero);
    body->setContentsMargins(30, 24, 30, 24);
    body->setSpacing(8);
    auto eyebrow = new ElaText("CORDIS  /  PLUGIN WORKBENCH", 13, hero);
    auto title = new ElaText(QString::fromUtf8("把灵感，连接成工作台。"), 30, hero);
    title->setIsBold(true);
    auto subtitle = new ElaText(QString::fromUtf8("你的插件、工具与创作空间，从这里出发。"), 15, hero);
    for (auto text : {eyebrow, title, subtitle}) {
        text->setStyleSheet("color:white;background:transparent;");
        body->addWidget(text);
    }
    body->addStretch();
    auto actions = new QHBoxLayout;
    actions->setSpacing(10);
    for (const auto& entry :
         {std::make_pair("CordisWelcomePlugins", "添加插件"), std::make_pair("CordisWelcomeSettings", "个性化设置"),
          std::make_pair("CordisWelcomeCommands", "打开命令")}) {
        auto button = new ActionButton(QString::fromUtf8(entry.second), hero);
        button->setObjectName(QString::fromLatin1(entry.first));
        button->setMinimumHeight(34);
        if (button->objectName() == "CordisWelcomePlugins")
            primaryButton(button);
        actions->addWidget(button);
    }
    actions->addStretch();
    body->addLayout(actions);
    layout->addWidget(hero);
    auto heading = new QHBoxLayout;
    auto label = new ElaText(QString::fromUtf8("插件状态"), 20, content);
    label->setIsBold(true);
    heading->addWidget(label);
    heading->addStretch();
    count_ = new ElaText(QString::fromUtf8("0 个插件"), 13, content);
    secondaryText(count_);
    heading->addWidget(count_);
    layout->addLayout(heading);
    auto help = new ElaText(
        QString::fromUtf8("使用左右开关启停；从卡片打开管理与配置窗口。添加插件位于顶部“文件”菜单。"), 13, content);
    secondaryText(help);
    layout->addWidget(help);
    error_ = new ElaText(content);
    error_->setObjectName("CordisHomeError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    error_->hide();
    layout->addWidget(error_);
    cards_ = new QWidget(content);
    cards_->setObjectName("CordisPluginCards");
    flow_ = new ElaFlowLayout(cards_, 0, 12, 12);
    flow_->setIsAnimation(false);
    layout->addWidget(cards_);
    empty_ =
        new ElaText(QString::fromUtf8("暂无插件。通过“文件 → 添加插件配置…”添加已注册插件的配置实例。"), 14, content);
    empty_->setObjectName("CordisHomeEmpty");
    secondaryText(empty_);
    layout->addWidget(empty_);
    layout->addStretch();
    addCentralWidget(content);
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
    for (const auto& plugin : plugins) {
        retained.insert(plugin.id);
        auto item = items_.value(plugin.id);
        if (!item) {
            layoutChanged = true;
            item = new ElaScrollPageArea(cards_);
            item->setObjectName("CordisPluginCard");
            item->setProperty("CordisPluginId", plugin.id);
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
            icon->setPixmap(pluginIcon(plugin.id).scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            icon->setFixedSize(48, 48);
            header->addWidget(icon);
            auto text = new QVBoxLayout;
            text->setSpacing(4);
            auto title = new ElaText(item);
            title->setObjectName("CordisPluginTitle");
            title->setTextFormat(Qt::PlainText);
            title->setTextPixelSize(16);
            title->setIsBold(true);
            auto status = new ElaText(item);
            status->setObjectName("CordisPluginStatus");
            status->setTextFormat(Qt::PlainText);
            status->setTextPixelSize(13);
            text->addWidget(title);
            auto statusRow = new QHBoxLayout;
            statusRow->setSpacing(6);
            auto stateIcon = new ElaText(item);
            stateIcon->setObjectName("CordisPluginStateIcon");
            stateIcon->setFixedSize(16, 16);
            statusRow->addWidget(stateIcon);
            statusRow->addWidget(status, 1);
            text->addLayout(statusRow);
            header->addLayout(text, 1);
            body->addLayout(header);
            auto footer = new QHBoxLayout;
            auto identity = new ElaText(QString::fromUtf8("实例 #%1").arg(plugin.id), 12, item);
            identity->setObjectName("CordisPluginIdentity");
            secondaryText(identity);
            footer->addWidget(identity, 1);
            auto button = new KeyboardToggleSwitch(item);
            button->setObjectName("CordisPluginPower");
            footer->addWidget(button, 0, Qt::AlignVCenter);
            body->addLayout(footer);
            auto tools = new QHBoxLayout;
            for (const auto& entry :
                 {std::make_pair("CordisPluginManage", "管理"), std::make_pair("CordisPluginConfigure", "配置")}) {
                auto action = new ActionButton(QString::fromUtf8(entry.second), item);
                action->setObjectName(QString::fromLatin1(entry.first));
                action->setMinimumHeight(30);
                action->setAccessibleName(plugin.title + " · " + action->text());
                const bool configuration = action->objectName() == "CordisPluginConfigure";
                action->setEnabled(!configuration || plugin.configuration);
                if (configuration)
                    action->setToolTip(plugin.configuration
                                           ? QString::fromUtf8("编辑此条目的配置并显式保存")
                                           : QString::fromUtf8("此实例由宿主手动创建，未接入配置会话"));
                tools->addWidget(action);
                connect(action, &QPushButton::clicked, this, [this, id = plugin.id, configuration] {
                    if (!manage_ || !access_)
                        return;
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
            flow_->addWidget(item);
            items_.insert(plugin.id, item);
            // 发出时捕获修订，排队执行；刷新后的新修订不能覆盖旧点击的 CAS。
            connect(button, &ElaToggleSwitch::toggled, this,
                    [this, button = QPointer<ElaToggleSwitch>(button), id = plugin.id](bool enabled) {
                        if (!button || !button->isEnabled() || !power_)
                            return;
                        auto invoke = power_;
                        auto access = access_;
                        const auto revision = button->property("CordisConfigurationRevision").toULongLong();
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
        auto title = item->findChild<ElaText*>("CordisPluginTitle");
        auto status = item->findChild<ElaText*>("CordisPluginStatus");
        auto button = item->findChild<ElaToggleSwitch*>("CordisPluginPower");
        button->setProperty("CordisConfigurationRevision", QVariant::fromValue<qulonglong>(plugin.revision));
        auto identity = item->findChild<ElaText*>("CordisPluginIdentity");
        const auto identityText =
            plugin.identity.isEmpty() ? QString::fromUtf8("实例 #%1").arg(plugin.id) : plugin.identity;
        identity->setText(identity->fontMetrics().elidedText(identityText, Qt::ElideMiddle, 185));
        identity->setToolTip(identityText);
        const auto name = title->fontMetrics().elidedText(plugin.title, Qt::ElideRight, 192);
        if (title->text() != name)
            title->setText(name);
        title->setAccessibleName(plugin.title);
        const auto state = plugin.busy ? QString::fromUtf8("操作中 · %1").arg(plugin.subtitle) : plugin.subtitle;
        if (status->text() != state)
            status->setText(state);
        status->setWordWrap(true);
        auto stateIcon = item->findChild<ElaText*>("CordisPluginStateIcon");
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
        const auto action = plugin.configuration ? (plugin.enabled ? QString::fromUtf8("停用配置条目")
                                                                   : QString::fromUtf8("启用配置条目"))
                            : plugin.canStop     ? QString::fromUtf8("停止插件")
                            : plugin.canRetry    ? QString::fromUtf8("重试加载")
                                                 : QString::fromUtf8("加载插件");
        const auto hint = !power_       ? QString::fromUtf8("此入口仅展示状态；请使用所属插件的管理入口。")
                          : plugin.busy ? QString::fromUtf8("正在处理，请稍候，不会重复提交操作。")
                          : !(plugin.canStop || plugin.canLoad || plugin.canRetry)
                              ? QString::fromUtf8("当前状态不允许修改；会话可能已关闭或资源仍在清理，请查看插件管理。")
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
    count_->setText(QString::fromUtf8("%1 个插件").arg(plugins.size()));
    empty_->setVisible(plugins.empty());
    if (layoutChanged) {
        flow_->invalidate();
        cards_->updateGeometry();
    }
}
} // namespace cordis::ui::detail
