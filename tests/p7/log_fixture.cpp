/** @brief Two independently mounted, command-only Qt plugins with the same output channel. */
#include <cordis/module/qt_plugin.hpp>
#include <cordis/ui/commands.hpp>
#include <cordis/ui/output.hpp>

using namespace cordis;
using namespace cordis::ui;

class LogInstance final : public QObject, public module::IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(cordis::module::IQtInstanceV1)
public:
    explicit LogInstance(std::string marker) : marker_(std::move(marker)) {}
    Task<void> activate(Context context) override
    {
        auto commands = context.consume<CommandService>({"ui.commands"});
        auto output = context.consume<OutputService>({"ui.output"});
        if (!commands.hasValue()) return failedTask(commands.error());
        if (!output.hasValue()) return failedTask(output.error());
        std::optional<Task<void>> ready;
        const auto registered = commands.value().withService([&](CommandService& service) {
            auto command = service.add(context, {"write", "U7 publish " + marker_, [this, context, output = output.value()] {
                std::optional<ErrorInfo> error;
                auto accessed = output.withService([&](OutputService& log) {
                    auto event = log.write(context, "shared", "U7 event " + marker_ + std::to_string(count_ + 1));
                    if (event.hasValue()) ++count_;
                    else error = event.error();
                });
                if (!accessed.hasValue()) return failedTask(accessed.error());
                return error ? failedTask(*error) : completedTask();
            }, {}});
            ready = command.hasValue() ? command.value().ready() : failedTask(command.error());
        });
        return registered.hasValue() ? *ready : failedTask(registered.error());
    }
private:
    std::string marker_;
    int count_ = 0;
};

class LogFactory final : public QObject, public module::IQtPluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.cordis.IQtPluginV1/1.0" FILE "u7_metadata.json")
    Q_INTERFACES(cordis::module::IQtPluginV1)
public:
    QStringList pluginIds() const override { return {"u7.alpha", "u7.bravo"}; }
    QObject* create(const QString& id) override
    {
        if (id == "u7.alpha") return new LogInstance("A");
        if (id == "u7.bravo") return new LogInstance("B");
        return nullptr;
    }
};
#include "log_fixture.moc"
