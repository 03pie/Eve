#include "extension_facts_p.hpp"
#include "extension_links_p.hpp"
#include "appearance_p.hpp"

#include <eve/ui/extensions.hpp>
#include <ElaFlowLayout.h>
#include <QDesktopServices>
#include <QGridLayout>
#include <QLocale>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace eve::ui::detail {
namespace {
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

QVBoxLayout* section(QVBoxLayout* body, const QString& title, const char* id)
{
    auto panel = new QWidget(body->parentWidget());
    panel->setObjectName(id);
    auto section = new QVBoxLayout(panel);
    section->setContentsMargins(0, 4, 0, 0);
    section->setSpacing(7);
    auto heading = label(panel, 17, "EveExtensionSectionHeading");
    heading->setText(title);
    heading->setIsBold(true);
    section->addWidget(heading);
    body->addWidget(panel);
    return section;
}

QGridLayout* factsGrid(QVBoxLayout* section)
{
    auto grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(7);
    section->addLayout(grid);
    return grid;
}

ActionButton* sidebarButton(QWidget* parent, const QString& text, const char* id)
{
    auto button = new ActionButton(text, parent);
    button->setObjectName(id);
    button->setAutoDefault(false);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(25);
    auto font = button->font();
    font.setPixelSize(13);
    button->setFont(font);
    return button;
}

}

ExtensionFacts::FactRow::FactRow(QGridLayout* grid, const QString& caption, const char* id)
{
    name = label(grid->parentWidget(), 13, "EveExtensionFactLabel");
    name->setText(caption);
    name->setWordWrap(false);
    grid->setColumnMinimumWidth(0, std::max(grid->columnMinimumWidth(0), name->sizeHint().width()));
    secondaryText(name);
    value = label(grid->parentWidget(), 13, id);
    const int row = grid->rowCount();
    grid->addWidget(name, row, 0, Qt::AlignTop);
    grid->addWidget(value, row, 1);
}

void ExtensionFacts::FactRow::setValue(const QString& text) const
{
    value->setText(text);
    name->setVisible(!text.isEmpty());
    value->setVisible(!text.isEmpty());
}

ExtensionFacts::ExtensionFacts(QWidget* parent, CategorySelected categorySelected)
    : ElaScrollArea(parent), categorySelected_(std::move(categorySelected))
{
    setObjectName("EveExtensionInstallInfo");
    setAccessibleName(tr("扩展信息"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto page = new QWidget(this);
    auto body = new QVBoxLayout(page);
    body->setContentsMargins(10, 3, 12, 10);
    body->setSpacing(12);

    auto installGrid = factsGrid(section(body, tr("安装信息"), "EveExtensionInstallation"));
    identity_ = {installGrid, tr("模块标识"), "EveExtensionIdentity"};
    plugin_ = {installGrid, tr("插件标识"), "EveExtensionPluginId"};
    version_ = {installGrid, tr("版本"), "EveExtensionVersion"};
    source_ = {installGrid, tr("来源"), "EveExtensionSource"};
    state_ = {installGrid, tr("状态"), "EveExtensionState"};
    installedAt_ = {installGrid, tr("安装时间"), "EveExtensionInstalledAt"};
    packageBytes_ = {installGrid, tr("安装包大小"), "EveExtensionPackageBytes"};

    auto workspaceLayout = section(body, tr("当前工作区"), "EveExtensionWorkspaceInfo");
    workspace_ = workspaceLayout->parentWidget();
    auto workspaceGrid = factsGrid(workspaceLayout);
    workspaceGrid->setColumnMinimumWidth(0, installGrid->columnMinimumWidth(0));
    instances_ = {workspaceGrid, tr("实例数"), "EveExtensionInstanceCount"};

    auto categoryLayout = section(body, tr("分类"), "EveExtensionCategories");
    categories_ = categoryLayout->parentWidget();
    tags_ = new ElaFlowLayout(0, 6, 5);
    tags_->setIsAnimation(false);
    categoryLayout->addLayout(tags_);

    auto resourceLayout = section(body, tr("资源"), "EveExtensionResources");
    resources_ = resourceLayout->parentWidget();
    const std::pair<QString, const char*> resources[] = {
        {tr("主页"), "EveExtensionHomepage"},
        {tr("源代码"), "EveExtensionRepository"},
        {tr("问题反馈"), "EveExtensionIssues"},
        {tr("许可证"), "EveExtensionLicenseLink"},
    };
    for (std::size_t index = 0; index < links_.size(); ++index) {
        const auto& [caption, id] = resources[index];
        auto button = sidebarButton(resources_, QStringLiteral("↗ %1").arg(caption), id);
        button->setAccessibleName(caption);
        button->setLightDefaultColor(Qt::transparent);
        button->setDarkDefaultColor(Qt::transparent);
        links_[index].button = button;
        resourceLayout->addWidget(button, 0, Qt::AlignLeft);
        connect(button, &QPushButton::clicked, this, [this, index] {
            const auto target = links_[index].target;
            if (!target.isEmpty()) QDesktopServices::openUrl(target);
        });
    }
    license_ = label(resources_, 13, "EveExtensionLicense");
    resourceLayout->addWidget(license_);
    body->addStretch();

    setWidget(page);
    // The sidebar inherits the Ela dialog surface.
    page->setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
}

void ExtensionFacts::setCategories(const QStringList& categories, bool navigationEnabled)
{
    for (int index = 0; index < categories.size(); ++index) {
        if (index == categoryButtons_.size()) {
            auto button = sidebarButton(categories_, {}, "EveExtensionCategory");
            categoryButtons_.push_back(button);
            tags_->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, button] {
                const auto selected = categorySelected_;
                const auto category = button->accessibleName();
                if (selected) selected(category);
            });
        }
        auto button = categoryButtons_[index];
        const auto& category = categories.at(index);
        const auto fit = button->fontMetrics().elidedText(category, Qt::ElideRight, 165);
        button->setText(fit);
        button->setAccessibleName(category);
        button->setToolTip(QStringLiteral("<span>%1</span>").arg(category.toHtmlEscaped()));
        button->setFixedWidth(button->fontMetrics().horizontalAdvance(fit) + 18);
        button->setEnabled(navigationEnabled);
        button->show();
    }
    for (int index = categories.size(); index < categoryButtons_.size(); ++index)
        categoryButtons_[index]->hide();
    categories_->setVisible(!categories.isEmpty());
}

void ExtensionFacts::setResources(const ExtensionItem& item)
{
    const QString urls[] = {item.homepage, item.repository, item.issues, item.license};
    bool hasLinks = false;
    for (std::size_t index = 0; index < links_.size(); ++index) {
        auto& resource = links_[index];
        const QUrl url(urls[index], QUrl::StrictMode);
        resource.target = isExtensionWebLink(url) ? url : QUrl{};
        const bool visible = !resource.target.isEmpty();
        resource.button->setVisible(visible);
        if (visible)
            resource.button->setToolTip(QStringLiteral("<span>%1</span>").arg(resource.target.toDisplayString().toHtmlEscaped()));
        hasLinks = hasLinks || visible;
    }
    const bool plainLicense = !item.license.isEmpty() && links_.back().target.isEmpty();
    license_->setVisible(plainLicense);
    license_->setText(plainLicense ? tr("许可证：%1").arg(item.license) : QString{});
    resources_->setVisible(hasLinks || plainLicense);
}

void ExtensionFacts::setItem(const ExtensionItem* item, bool preview, bool categoryNavigationEnabled)
{
    if (!item) return;
    identity_.setValue(item->legacyReference ? QString{} : item->id);
    plugin_.setValue(item->pluginId);
    version_.setValue(item->version);
    state_.setValue(item->state);
    source_.setValue(item->builtin ? tr("内置") :
                     preview || !item->packageHash.isEmpty() ? tr("本地安装包") :
                     item->missing ? tr("工作区引用") : tr("开发模块"));
    const QLocale locale;
    installedAt_.setValue(!preview && item->installedAt.isValid()
        ? locale.toString(item->installedAt.toLocalTime(), QLocale::ShortFormat) : QString{});
    packageBytes_.setValue(item->packageBytes > 0
        ? locale.formattedDataSize(item->packageBytes, 1) : QString{});
    workspace_->setVisible(!preview && item->kind == ExtensionKind::Business);
    instances_.setValue(QString::number(item->instances));
    setCategories(item->categories, categoryNavigationEnabled && !preview);
    setResources(*item);
}
}
