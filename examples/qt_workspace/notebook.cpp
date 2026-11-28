/** @file notebook.cpp
 * @brief 实际笔记 UI DLL，只消费共享页面、命令和输出服务，不链接模块目录或配置执行器。
 */
#include "contract.hpp"
#include <ElaPushButton.h>
#include <ElaText.h>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVariant>
#include <eve/module/qt_plugin.hpp>
#include <eve/ui/commands.hpp>
#include <eve/ui/output.hpp>
#include <eve/ui/pages.hpp>
#include <optional>

#ifndef EVE_NOTEBOOK_STATE
#define EVE_NOTEBOOK_STATE 0
#endif
#ifndef EVE_EXAMPLE_NOTEBOOK_FAIL_START
#define EVE_EXAMPLE_NOTEBOOK_FAIL_START 0
#endif
#if EVE_NOTEBOOK_STATE
#include "draft_state.hpp"
#include <eve/module/qt_state_plugin.hpp>
#endif

using namespace eve;
using namespace eve::ui;
class Notebook final : public QObject,
#if EVE_NOTEBOOK_STATE
                       public module::IQtStateInstanceV1,
#endif
                       public module::IQtInstanceV1 {
    Q_OBJECT
#if EVE_NOTEBOOK_STATE
    Q_INTERFACES(eve::module::IQtInstanceV1 eve::module::IQtStateInstanceV1)
#else
    Q_INTERFACES(eve::module::IQtInstanceV1)
#endif
public:
    Task<void> activate(Context context) override
    {
        if (activateAttempted_)
            return failedTask(makeError(ErrorCode::ActivationFailed, "notebook activation already attempted"));
        activateAttempted_ = true;
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
        if (!restored_)
            draft_ = value.text;
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
                              [this, identity, context, title = value.title] {
                                  auto widget = std::make_unique<QWidget>();
                                  widget->setObjectName("EveNotebookPage");
                                  widget->setProperty("eve.instance",
                                                      QVariant::fromValue<qulonglong>(identity.instance.value()));
                                  widget->setProperty("eve.epoch", QVariant::fromValue<qulonglong>(identity.epoch));
                                  auto layout = new QVBoxLayout(widget.get());
                                  auto heading = new ElaText(title, 24, widget.get());
                                  heading->setTextFormat(Qt::PlainText);
                                  layout->addWidget(heading);
                                  editor_ = new QPlainTextEdit(widget.get());
                                  editor_->viewport()->setAutoFillBackground(false);
                                  editor_->setObjectName("EveNotebookEditor");
                                  editor_->setPlainText(draft_);
                                  QObject::connect(editor_.data(), &QPlainTextEdit::textChanged, widget.get(),
                                                   [this, context, editor = editor_] {
                                                       if (!editor)
                                                           return;
                                                       if (context.identity().hasValue())
                                                           draft_ = editor->toPlainText();
                                                       else {
                                                           const QSignalBlocker blocked(editor.data());
                                                           editor->setPlainText(draft_);
                                                       }
                                                   });
                                  layout->addWidget(editor_);
                                  auto publish = new ElaPushButton(QString::fromUtf8("输出当前笔记"), widget.get());
                                  publish->setObjectName("EveNotebookPublish");
                                  layout->addWidget(publish);
                                  feedback_ = new ElaText(widget.get());
                                  feedback_->setObjectName("EveNotebookFeedback");
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
        if (value.failStart || EVE_EXAMPLE_NOTEBOOK_FAIL_START)
            return failedTask(makeError(ErrorCode::ActivationFailed, "notebook start failure requested"));
        return page_->ready();
    }
#if EVE_NOTEBOOK_STATE
    EveStatus captureState(EveStateBufferV1* output) noexcept override
    {
        if (!activateAttempted_) {
            const auto status = notebook::draft_state::prepare(output);
            return status == EVE_OK ? EVE_E_CLOSED : status;
        }
        return notebook::draft_state::capture(draft_, output);
    }
    EveStatus restoreState(const EveStateSchemaV1* schema, EveBytesV1 json) noexcept override
    {
        if (activateAttempted_)
            return EVE_E_CLOSED;
        try {
            QString draft;
            QByteArray canonical;
            const auto status = notebook::draft_state::decode(schema, json, draft, canonical);
            if (status != EVE_OK)
                return status;
            draft_ = std::move(draft);
            restored_ = true;
            return EVE_OK;
        } catch (...) {
            return EVE_E_FAILED;
        }
    }
#endif

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
        auto result = writeText(context, output, draft_.toStdString());
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
    QString draft_;
    bool restored_ = false, activateAttempted_ = false;
};
class NotebookFactory final : public QObject,
#if EVE_NOTEBOOK_STATE
                              public module::IQtStatePluginV1,
#endif
                              public module::IQtPluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.eve.IQtPluginV1/1.0" FILE "notebook_metadata.json")
#if EVE_NOTEBOOK_STATE
    Q_INTERFACES(eve::module::IQtPluginV1 eve::module::IQtStatePluginV1)
#else
    Q_INTERFACES(eve::module::IQtPluginV1)
#endif
public:
    QStringList pluginIds() const override { return {"example.notebook"}; }
    QObject* create(const QString& id) override { return id == "example.notebook" ? new Notebook : nullptr; }
#if EVE_NOTEBOOK_STATE
    EveStatus describeState(EveBytesV1 plugin, EveStateSchemaV1* output) noexcept override
    {
        return notebook::draft_state::describe(plugin, output);
    }
    EveStatus validateState(EveBytesV1 plugin, const EveStateSchemaV1* schema, EveBytesV1 json,
                               EveStateBufferV1* output) noexcept override
    {
        return notebook::draft_state::validate(plugin, schema, json, output);
    }
#endif
};
#include "notebook.moc"
