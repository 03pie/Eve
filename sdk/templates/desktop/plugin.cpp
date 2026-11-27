/** @file plugin.cpp
 * @brief An external desktop owns the window, toolbar and layout around the shared workbench.
 */
#include <eve/desktop/layout.hpp>
#include <eve/module/qt_plugin.hpp>
#include <QAction>
#include <QMainWindow>
#include <QToolBar>
#include <QVBoxLayout>

class Workshop final : public QObject, public eve::module::IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(eve::module::IQtInstanceV1)
public:
    eve::Task<void> activate(eve::Context context) override
    {
        using namespace eve::desktop;
        auto service = context.consume<LayoutServiceV1>({layoutService});
        if (!service.hasValue()) return eve::failedTask(service.error());
        auto root = std::make_unique<QMainWindow>();
        root->setObjectName("WorkshopDesktop");
        root->setWindowTitle("Workshop Desktop");
        root->resize(1120, 760);
        auto toolbar = root->addToolBar("Workbench");
        toolbar->setMovable(false);
        const auto navigation = [&](const QString& title, Action route) {
            auto item = toolbar->addAction(title);
            connect(item, &QAction::triggered, root.get(), [context, ref = service.value(), route] {
                (void)ref.withService([&](LayoutServiceV1& layout) { (void)layout.invoke(context, route); });
            });
        };
        navigation(tr("Home"), Action::Home);
        navigation(tr("Extensions"), Action::Extensions);
        navigation(tr("Output"), Action::Output);
        navigation(tr("Commands"), Action::Commands);
        navigation(tr("Notifications"), Action::Notifications);
        navigation(tr("Settings"), Action::Settings);
        auto workspace = new QWidget(root.get());
        auto contents = new QVBoxLayout(workspace);
        contents->setContentsMargins(0, 0, 0, 0);
        root->setCentralWidget(workspace);
        std::optional<eve::Result<void>> attached;
        auto used = service.value().withService([&](LayoutServiceV1& layout) {
            attached.emplace(layout.attach(context, {std::move(root), workspace}));
        });
        if (!used.hasValue()) return eve::failedTask(used.error());
        return attached->hasValue() ? eve::completedTask() : eve::failedTask(attached->error());
    }
};

class Factory final : public QObject, public eve::module::IQtPluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.eve.IQtPluginV1/1.0" FILE "eve_plugin_metadata.json")
    Q_INTERFACES(eve::module::IQtPluginV1)
public:
    QStringList pluginIds() const override { return {QStringLiteral(EVE_QT_UI_PLUGIN_ID)}; }
    QObject* create(const QString& id) override
    { return id == QStringLiteral(EVE_QT_UI_PLUGIN_ID) ? new Workshop : nullptr; }
};
#include "plugin.moc"
