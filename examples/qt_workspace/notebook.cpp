/** @file notebook.cpp
 * @brief 实际笔记 UI DLL，只消费共享页面、命令和输出服务，不链接模块目录或配置执行器。
 */
#include "contract.hpp"
#include <ElaPushButton.h>
#include <ElaText.h>
#include <QPlainTextEdit>
#include <QPointer>
#include <QVariant>
#include <QVBoxLayout>
#include <cordis/module/qt_plugin.hpp>
#include <cordis/ui/commands.hpp>
#include <cordis/ui/output.hpp>
#include <cordis/ui/pages.hpp>
#include <optional>

using namespace cordis;
using namespace cordis::ui;
class Notebook final : public QObject, public module::IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(cordis::module::IQtInstanceV1)
public:
    Task<void> activate(Context context) override
    {
        auto parsed = notebook::configuration(context.config());
        if (!parsed.hasValue())
            return failedTask(parsed.error());
        auto pages = context.consume<UiPagesService>({"ui.pages"});
        auto commands = context.consume<CommandService>({"ui.commands"});
        auto output = context.consume<OutputService>({"ui.output"});
        if (!pages.hasValue())
            return failedTask(pages.error());
        if (!commands.hasValue())
            return failedTask(commands.error());
        if (!output.hasValue())
            return failedTask(output.error());
        const auto value = parsed.value();
        auto registered = context.afterCleanup(
            [fail = value.failCleanup] {
                return fail ? failedTask(makeError(ErrorCode::CleanupFailed, "notebook cleanup failure requested"))
                            : completedTask();
            },
            "notebook.owner");
        if (!registered.hasValue())
            return failedTask(registered.error());
        const auto identity = context.identity().value();
        auto command = commands.value().withService([&](CommandService& service) {
            command_ = service
                           .add(context, {"notebook.publish", value.title.toStdString() + "：输出笔记",
                                          [this, context, output = output.value()] { return write(context, output); },
                                          [this] { return !editor_.isNull(); }})
                           .value();
        });
        if (!command.hasValue())
            return failedTask(command.error());
        auto page = pages.value().withService([&](UiPagesService& service) {
            page_ = service
                        .add(context,
                             {"notebook", value.title.toStdString(), "notebook",
                              [this, identity, value] {
                                  auto widget = std::make_unique<QWidget>();
                                  widget->setObjectName("CordisNotebookPage");
                                  widget->setProperty("cordis.instance",
                                                      QVariant::fromValue<qulonglong>(identity.instance.value()));
                                  widget->setProperty("cordis.epoch", QVariant::fromValue<qulonglong>(identity.epoch));
                                  auto layout = new QVBoxLayout(widget.get());
                                  auto title = new ElaText(value.title, 24, widget.get());
                                  title->setTextFormat(Qt::PlainText);
                                  layout->addWidget(title);
                                  editor_ = new QPlainTextEdit(widget.get());
                                  editor_->setObjectName("CordisNotebookEditor");
                                  editor_->setPlainText(value.text);
                                  layout->addWidget(editor_);
                                  auto publish = new ElaPushButton(QString::fromUtf8("输出当前笔记"), widget.get());
                                  publish->setObjectName("CordisNotebookPublish");
                                  layout->addWidget(publish);
                                  feedback_ = new ElaText(widget.get());
                                  feedback_->setObjectName("CordisNotebookFeedback");
                                  feedback_->setTextFormat(Qt::PlainText);
                                  layout->addWidget(feedback_);
                                  QObject::connect(publish, &QPushButton::clicked, widget.get(),
                                                   [command = *command_] { (void)command.execute(); });
                                  return widget;
                              }})
                        .value();
        });
        if (!page.hasValue())
            return failedTask(page.error());
        auto written = writeText(context, output.value(), "笔记插件已启动：" + value.title.toStdString());
        if (!written.hasValue())
            return failedTask(written.error());
        if (value.failStart)
            return failedTask(makeError(ErrorCode::ActivationFailed, "notebook start failure requested"));
        return page_->ready();
    }

private:
    static Result<void> writeText(const Context& context, const ServiceRef<OutputService>& output, std::string text)
    {
        std::optional<ErrorInfo> failure;
        auto accessed = output.withService([&](OutputService& service) {
            auto result = service.write(context, "notebook", std::move(text));
            if (!result.hasValue())
                failure = result.error();
        });
        if (!accessed.hasValue())
            return accessed;
        return failure ? Result<void>::failure(*failure) : Result<void>::success();
    }
    Task<void> write(const Context& context, const ServiceRef<OutputService>& output)
    {
        if (!editor_)
            return failedTask(makeError(ErrorCode::ContextClosing, "notebook page unavailable"));
        auto result = writeText(context, output, editor_->toPlainText().toStdString());
        if (feedback_)
            feedback_->setText(result.hasValue()
                                   ? QString::fromUtf8("已写入当前实例的输出通道。")
                                   : QString::fromUtf8("输出失败：") + QString::fromStdString(result.error().message));
        return result.hasValue() ? completedTask() : failedTask(result.error());
    }
    QPointer<QPlainTextEdit> editor_;
    QPointer<ElaText> feedback_;
    std::optional<PageHandle> page_;
    std::optional<CommandHandle> command_;
};
class NotebookFactory final : public QObject, public module::IQtPluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.cordis.IQtPluginV1/1.0" FILE "notebook_metadata.json")
    Q_INTERFACES(cordis::module::IQtPluginV1)
public:
    QStringList pluginIds() const override { return {"example.notebook"}; }
    QObject* create(const QString& id) override { return id == "example.notebook" ? new Notebook : nullptr; }
};
#include "notebook.moc"
