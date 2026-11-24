/** Minimal external plugin: the host owns the loader root and each context owns its page. */
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <cordis/module/qt_plugin.hpp>
#include <cordis/ui/pages.hpp>
#include <optional>

class ExternalPage final : public QObject, public cordis::module::IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(cordis::module::IQtInstanceV1)
public:
    cordis::Task<void> activate(cordis::Context context) override
    {
        auto pages = context.consume<cordis::ui::UiPagesService>({"ui.pages"});
        if (!pages.hasValue())
            return cordis::failedTask(pages.error());
        std::optional<cordis::Result<cordis::ui::PageHandle>> page;
        auto access = pages.value().withService([&](cordis::ui::UiPagesService& service) {
            page.emplace(service.add(context, {"external-page", "External SDK page", "external",
                [] {
                    auto widget = std::make_unique<QWidget>();
                    auto layout = new QVBoxLayout(widget.get());
                    layout->addWidget(new QLabel(QStringLiteral("Built outside the Cordis repository"), widget.get()));
                    return widget;
                }}));
        });
        if (!access.hasValue())
            return cordis::failedTask(access.error());
        if (!page->hasValue())
            return cordis::failedTask(page->error());
        return page->value().ready();
    }
};

class ExternalFactory final : public QObject, public cordis::module::IQtPluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.cordis.IQtPluginV1/1.0" FILE "cordis_plugin_metadata.json")
    Q_INTERFACES(cordis::module::IQtPluginV1)
public:
    QStringList pluginIds() const override { return {QStringLiteral("example.page")}; }
    QObject* create(const QString& id) override
    {
        return id == QStringLiteral("example.page") ? new ExternalPage : nullptr;
    }
};

#include "plugin.moc"
