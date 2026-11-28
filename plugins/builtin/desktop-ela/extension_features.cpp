#include "extension_features_p.hpp"
#include "appearance_p.hpp"
#include "theme_palette_p.hpp"
#include <ElaPlainTextEdit.h>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVBoxLayout>
#include <algorithm>
#include <eve/desktop/layout.hpp>
#include <eve/ui/extensions.hpp>

namespace eve::ui::detail {
namespace {
QVBoxLayout* group(QVBoxLayout* body, const QString& title)
{
    auto section = new QWidget(body->parentWidget());
    auto layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);
    auto heading = new ElaText(title, 17, section);
    heading->setIsBold(true);
    layout->addWidget(heading);
    body->addWidget(section);
    return layout;
}
ElaText* contributionList(QVBoxLayout* section, const char* name)
{
    auto text = new ElaText(section->parentWidget());
    text->setObjectName(name);
    text->setTextPixelSize(14);
    text->setTextFormat(Qt::PlainText);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    text->setWordWrap(true);
    text->setMinimumWidth(1);
    section->addWidget(text);
    return text;
}
QString contributionText(const std::vector<ExtensionContribution>& entries)
{
    QStringList lines;
    for (const auto& entry : entries) {
        QStringList info;
        if (entry.window) info << QObject::tr("窗口");
        info << entry.id << entry.instance;
        lines << entry.title + QLatin1Char('\n') + info.join(QStringLiteral(" · "));
    }
    return lines.join(QStringLiteral("\n\n"));
}
void setList(QWidget* section, ElaText* label, const std::vector<ExtensionContribution>& entries)
{
    const auto text = contributionText(entries);
    if (text != label->text()) label->setText(text);
    section->setVisible(!entries.empty());
}
}

ExtensionFeaturesView::ExtensionFeaturesView(QWidget* parent) : ElaScrollArea(parent)
{
    setObjectName("EveExtensionFeatures");
    setAccessibleName(tr("扩展功能"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto content = new QWidget(this);
    auto body = new QVBoxLayout(content);
    body->setContentsMargins(10, 14, 18, 10);
    body->setSpacing(18);

    auto desktop = group(body, tr("桌面接口"));
    desktop_ = desktop->parentWidget();
    desktopText_ = contributionList(desktop, "EveExtensionDesktopInterface");

    auto config = group(body, tr("默认实例配置"));
    configuration_ = config->parentWidget();
    defaults_ = new ElaPlainTextEdit(configuration_);
    defaults_->setObjectName("EveExtensionDefaultConfig");
    defaults_->setAccessibleName(tr("默认实例配置"));
    defaults_->setReadOnly(true);
    defaults_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    defaults_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    config->addWidget(defaults_);

    runtimeVersion_ = new ElaText(tr("以下贡献来自当前运行版本。已安装的版本将在重启后生效。"), 14, content);
    runtimeVersion_->setObjectName("EveExtensionContributionVersion");
    runtimeVersion_->setWordWrap(true);
    secondaryText(runtimeVersion_);
    body->addWidget(runtimeVersion_);

    auto pages = group(body, tr("当前工作区的页面与窗口"));
    pages_ = pages->parentWidget();
    pageList_ = contributionList(pages, "EveExtensionFeaturePages");
    auto commands = group(body, tr("当前工作区的命令"));
    commands_ = commands->parentWidget();
    commandList_ = contributionList(commands, "EveExtensionFeatureCommands");
    body->addStretch();
    setWidget(content);
    content->setAutoFillBackground(false);
    bindWorkbenchContentPalette(this);
    bindWorkbenchContentPalette(defaults_);
}

bool ExtensionFeaturesView::setContent(const ExtensionItem* item, const ExtensionWorkspace& workspace)
{
    const bool desktop = item && item->kind == ExtensionKind::Desktop && !item->missing;
    desktop_->setVisible(desktop);
    if (desktop) {
        const auto text = item->builtin
            ? tr("托管 Eve 工作区并向平台和业务插件提供服务。\n\n%1").arg(item->desktopServices.join(QLatin1Char('\n')))
            : tr("通过 LayoutServiceV1（%1）提供顶层窗口和工作区容器，并接入 Eve 平台操作。")
                .arg(QString::fromLatin1(eve::desktop::layoutService));
        if (text != desktopText_->text()) desktopText_->setText(text);
    }

    const auto config = item && item->kind == ExtensionKind::Business ? item->defaultConfig : QString{};
    if (config != configSource_) {
        configSource_ = config;
        const auto document = QJsonDocument::fromJson(config.toUtf8());
        hasDefaults_ = document.isObject() && !document.object().isEmpty();
        const auto text = hasDefaults_ ? QString::fromUtf8(document.toJson()).trimmed() : QString{};
        defaults_->setPlainText(text);
        const auto lines = std::clamp(defaults_->blockCount() + 1, 5, 14);
        defaults_->setFixedHeight(lines * defaults_->fontMetrics().lineSpacing() + 10);
    }
    configuration_->setVisible(hasDefaults_);
    setList(pages_, pageList_, workspace.pages);
    setList(commands_, commandList_, workspace.commands);
    const bool contributions = !workspace.pages.empty() || !workspace.commands.empty();
    runtimeVersion_->setVisible(item && item->pending && contributions);
    return desktop || hasDefaults_ || contributions;
}
}
