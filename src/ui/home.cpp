/** @file home.cpp
 * @brief 以 Ela 背景卡和紧凑状态卡展示插件，卡片不参与页面导航。
 */
#include "home_p.hpp"
#include <ElaFlowLayout.h>
#include <ElaIcon.h>
#include <ElaImageCard.h>
#include <ElaPushButton.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>
#include <QHBoxLayout>
#include <QImage>
#include <QFontMetrics>
#include <QLayout>
#include <QLinearGradient>
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
    setContentsMargins(12, 8, 12, 8);
    auto content = new QWidget;
    content->setWindowTitle(windowTitle());
    auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 20);
    layout->setSpacing(18);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    auto hero = new ElaImageCard(content);
    hero->setObjectName("CordisHomeBanner");
    hero->setCardImage(banner());
    hero->setBorderRadius(14);
    hero->setFixedHeight(260);
    auto body = new QVBoxLayout(hero);
    body->setContentsMargins(30, 24, 30, 24);
    body->setSpacing(12);
    auto eyebrow = new ElaText("CORDIS  /  PLUGIN WORKBENCH", 13, hero);
    auto title = new ElaText(QString::fromUtf8("把灵感，连接成工作台。"), 32, hero);
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
         {std::make_pair("CordisWelcomePlugins", "管理插件"), std::make_pair("CordisWelcomeSettings", "个性化设置"),
          std::make_pair("CordisWelcomeCommands", "打开命令")}) {
        auto button = new ElaPushButton(QString::fromUtf8(entry.second), hero);
        button->setObjectName(QString::fromLatin1(entry.first));
        button->setMinimumHeight(34);
        actions->addWidget(button);
    }
    actions->addStretch();
    body->addLayout(actions);
    layout->addWidget(hero);
    auto heading = new QHBoxLayout;
    auto label = new ElaText(QString::fromUtf8("插件状态"), 22, content);
    label->setIsBold(true);
    heading->addWidget(label);
    heading->addStretch();
    count_ = new ElaText(QString::fromUtf8("0 个插件"), 13, content);
    heading->addWidget(count_);
    layout->addLayout(heading);
    auto help = new ElaText(QString::fromUtf8("查看插件当前状态，使用左右开关加载或停止插件。"), 13, content);
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
    empty_ = new ElaText(QString::fromUtf8("插件就绪后会显示在这里，也可以先到插件管理中查看。"), 14, content);
    layout->addWidget(empty_);
    layout->addStretch();
    addCentralWidget(content);
}

void HomePage::setPlugins(const std::vector<HomePlugin>& plugins,
                          std::function<Result<void>(const QString&, bool)> power)
{
    power_ = std::move(power);
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
            text->addWidget(status);
            header->addLayout(text, 1);
            body->addLayout(header);
            auto footer = new QHBoxLayout;
            auto identity = new ElaText(QString::fromUtf8("实例 #%1").arg(plugin.id), 12, item);
            footer->addWidget(identity, 1);
            auto button = new ElaToggleSwitch(item);
            button->setObjectName("CordisPluginPower");
            footer->addWidget(button, 0, Qt::AlignVCenter);
            body->addLayout(footer);
            flow_->addWidget(item);
            items_.insert(plugin.id, item);
            // 排队执行，让原生开关的鼠标/动画处理先返回；控件撤销后不再使用旧指针。
            connect(
                button, &ElaToggleSwitch::toggled, this,
                [this, button = QPointer<ElaToggleSwitch>(button), id = plugin.id](bool enabled) {
                    if (!button || !button->isEnabled() || !power_)
                        return;
                    QPointer<HomePage> alive(this);
                    auto invoke = power_;
                    const auto key = id;
                    button->setEnabled(false);
                    if (!alive)
                        return;
                    auto result = invoke(key, enabled);
                    if (!alive)
                        return;
                    QPointer<ElaText> message(error_);
                    if (!result.hasValue())
                        message->setText(QString::fromStdString(result.error().message));
                    if (message)
                        message->setVisible(!result.hasValue());
                },
                Qt::QueuedConnection);
        }
        auto title = item->findChild<ElaText*>("CordisPluginTitle");
        auto status = item->findChild<ElaText*>("CordisPluginStatus");
        auto button = item->findChild<ElaToggleSwitch*>("CordisPluginPower");
        const auto name = title->fontMetrics().elidedText(plugin.title, Qt::ElideRight, 192);
        if (title->text() != name)
            title->setText(name);
        title->setAccessibleName(plugin.title);
        const auto state = plugin.busy ? QString::fromUtf8("操作中 · %1").arg(plugin.subtitle) : plugin.subtitle;
        if (status->text() != state)
            status->setText(state);
        button->setEnabled(static_cast<bool>(power_) && !plugin.busy &&
                           (plugin.canStop || plugin.canLoad || plugin.canRetry));
        if (button->getIsToggled() != plugin.enabled) {
            const QSignalBlocker blocked(button);
            button->setIsToggled(plugin.enabled);
        }
        const auto action = plugin.canStop    ? QString::fromUtf8("停止插件")
                            : plugin.canRetry ? QString::fromUtf8("重试加载")
                                              : QString::fromUtf8("加载插件");
        button->setToolTip(plugin.busy ? QString::fromUtf8("正在处理，请稍候") : action);
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
