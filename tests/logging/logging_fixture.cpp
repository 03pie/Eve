/** @brief A resident Qt fixture that emits a single diagnosed event during activation. */
#include <eve/module/qt_plugin.hpp>
#include <eve/ui/output.hpp>

using namespace eve;

class LoggingInstance final : public QObject, public module::IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(eve::module::IQtInstanceV1)
public:
    Task<void> activate(Context context) override
    {
        const auto output = context.consume<ui::OutputService>({"ui.output"});
        if (!output.hasValue())
            return failedTask(output.error());
        std::optional<ErrorInfo> failure;
        const auto accessed = output.value().withService([&](ui::OutputService& service) {
            const auto event = service.write(context, "fixture", "TEST_OPERATION_REJECTED: Test operation declined.",
                                             ui::OutputLevel::Error);
            if (!event.hasValue())
                failure = event.error();
        });
        if (!accessed.hasValue())
            return failedTask(accessed.error());
        return failure ? failedTask(*failure) : completedTask();
    }
};

class LoggingFactory final : public QObject, public module::IQtPluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.eve.IQtPluginV1/1.0" FILE "logging_metadata.json")
    Q_INTERFACES(eve::module::IQtPluginV1)
public:
    QStringList pluginIds() const override { return {"test.logging"}; }
    QObject* create(const QString& id) override { return id == "test.logging" ? new LoggingInstance : nullptr; }
};

#include "logging_fixture.moc"
