/** @file plugin.cpp
 * @brief 通过安装的 SDK 注册一个跟随实例激活代存活的页面。
 */
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <cordis/module/qt_plugin.hpp>
#include <cordis/ui/pages.hpp>
#include <optional>

/** @brief 每个工厂实例独立登记页面贡献。 */
class ExternalPage final : public QObject, public cordis::module::IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(cordis::module::IQtInstanceV1)
public:
    /** @brief 在本代上下文登记页面，并等待宿主完成注册。 */
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

/** @brief 由 QPluginLoader 持有的工厂，使用同一目标的清单身份。 */
class ExternalFactory final : public QObject, public cordis::module::IQtPluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.cordis.IQtPluginV1/1.0" FILE "cordis_plugin_metadata.json")
    Q_INTERFACES(cordis::module::IQtPluginV1)
public:
    /** @brief 返回由 CMake 的 PLUGIN_ID 传入的公开标识。 */
    QStringList pluginIds() const override { return {QStringLiteral(CORDIS_QT_UI_PLUGIN_ID)}; }
    /** @brief 仅为受支持的标识创建无 parent 的新对象，由宿主接管。 */
    QObject* create(const QString& id) override
    {
        return id == QStringLiteral(CORDIS_QT_UI_PLUGIN_ID) ? new ExternalPage : nullptr;
    }
};

#include "plugin.moc"
