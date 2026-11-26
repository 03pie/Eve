#include "reader_page.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <cordis/module/qt_plugin.hpp>
#include <cordis/ui/commands.hpp>
#include <cordis/ui/output.hpp>
#include <cordis/ui/pages.hpp>
#include <optional>
#include <stdexcept>

using namespace cordis;

class LiteratureReader final : public QObject, public module::IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(cordis::module::IQtInstanceV1)
public:
    Task<void> activate(Context context) override
    {
        const auto config = QJsonDocument::fromJson(QByteArray::fromStdString(context.config()));
        if (!config.isObject() || !config.object().value(QStringLiteral("dataRoot")).isString())
            return failedTask(makeError(ErrorCode::InvalidArgument, "literature.reader requires dataRoot"));
        const auto dataRoot = config.object().value(QStringLiteral("dataRoot")).toString();
        if (!QDir::isAbsolutePath(dataRoot))
            return failedTask(makeError(ErrorCode::InvalidArgument, "literature.reader dataRoot must be absolute"));
        const auto title = config.object().value(QStringLiteral("title")).toString(QStringLiteral("文献阅读"));
        auto pages = context.consume<ui::UiPagesService>({"ui.pages"});
        auto commands = context.consume<ui::CommandService>({"ui.commands"});
        auto output = context.consume<ui::OutputService>({"ui.output"});
        auto scheduling = context.consume<std::shared_ptr<qt::QtScheduler>>({"qt.scheduler"});
        if (!pages.hasValue()) return failedTask(pages.error());
        if (!commands.hasValue()) return failedTask(commands.error());
        if (!output.hasValue()) return failedTask(output.error());
        if (!scheduling.hasValue()) return failedTask(scheduling.error());
        std::shared_ptr<qt::QtScheduler> scheduler;
        auto schedulerAccess = scheduling.value().withService([&](std::shared_ptr<qt::QtScheduler>& value) { scheduler = value; });
        if (!schedulerAccess.hasValue()) return failedTask(schedulerAccess.error());
        if (!scheduler || !context.usesScheduler(scheduler))
            return failedTask(makeError(ErrorCode::InvalidArgument, "literature.reader scheduler identity differs"));
        auto library = std::make_shared<literature::ReaderLibrary>();
        QString error;
        if (!library->open(dataRoot, &error))
            return failedTask(makeError(ErrorCode::InvalidArgument, error.toStdString()));
        auto cancelled = std::make_shared<std::atomic_bool>(false);
        auto cleanup = context.onCleanup([cancelled](CleanupContext) {
            cancelled->store(true);
            return completedTask();
        }, "literature.cancel");
        if (!cleanup.hasValue()) return failedTask(cleanup.error());
        std::optional<Result<ui::PageHandle>> page;
        auto pageAccess = pages.value().withService([&](ui::UiPagesService& service) {
            page.emplace(service.add(context, {"literature-reader", title.toStdString(), "literature",
                [this, library, context, scheduler, cancelled, title] {
                    auto widget = std::make_unique<literature::ReaderPage>(library, context, scheduler, cancelled, title);
                    reader_ = widget.get();
                    return widget;
                }}));
        });
        if (!pageAccess.hasValue()) return failedTask(pageAccess.error());
        if (!page->hasValue()) return failedTask(page->error());
        page_ = page->value();
        std::optional<ErrorInfo> commandFailure;
        auto commandAccess = commands.value().withService([&](ui::CommandService& service) {
            auto open = service.add(context, {"literature.reader.open", "打开文献阅读",
                [page = *page_] { auto result = page.navigate(); return result.hasValue() ? completedTask() : failedTask(result.error()); }, {}});
            if (!open.hasValue()) { commandFailure = open.error(); return; }
            auto save = service.add(context, {"literature.reader.save", "保存文献书库",
                [this] {
                    return reader_ && reader_->saveLibrary() ? completedTask()
                        : failedTask(makeError(ErrorCode::CallbackFailed, "literature library save failed"));
                }, [this] { return reader_ && !reader_->property("readerBusy").toBool(); }});
            if (!save.hasValue()) commandFailure = save.error();
        });
        if (!commandAccess.hasValue()) return failedTask(commandAccess.error());
        if (commandFailure) return failedTask(*commandFailure);
        auto write = output.value().withService([&](ui::OutputService& service) {
            (void)service.write(context, "literature-reader", "Literature reader 1.1.0: library opened.");
        });
        if (!write.hasValue()) return failedTask(write.error());
        return page_->ready();
    }

private:
    QPointer<literature::ReaderPage> reader_;
    std::optional<ui::PageHandle> page_;
};

class LiteratureFactory final : public QObject, public module::IQtPluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.cordis.IQtPluginV1/1.0" FILE "cordis_plugin_metadata.json")
    Q_INTERFACES(cordis::module::IQtPluginV1)
public:
    QStringList pluginIds() const override { return {QStringLiteral("literature.reader")}; }
    QObject* create(const QString& id) override
    {
        return id == QStringLiteral("literature.reader") ? new LiteratureReader : nullptr;
    }
};

#include "plugin.moc"
