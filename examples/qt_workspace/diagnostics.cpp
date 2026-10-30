/** @file diagnostics.cpp
 * @brief 宿主只读模块诊断窗口，显示真实代次/引用/驻留与错误；不提供卸载能力。
 */
#include "workspace.hpp"
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <QVBoxLayout>

namespace notebook {
namespace {
QString errorText(const cordis::ErrorInfo& error, unsigned depth = 0)
{
    auto text = QString::fromStdString(error.message);
    if (depth < 4)
        for (std::size_t i = 0; i < error.causes.size() && i < 8; ++i)
            text += '\n' + QString(static_cast<int>(depth + 1) * 2, ' ') + errorText(error.causes[i], depth + 1);
    return text;
}
} // namespace
std::unique_ptr<QWidget> moduleDiagnostics(const std::shared_ptr<cordis::module::ModuleManager>& manager,
                                           std::shared_ptr<cordis::ui::DiagnosticsService> diagnostics)
{
    auto widget = std::make_unique<QWidget>();
    widget->setObjectName("CordisModuleDiagnostics");
    auto layout = new QVBoxLayout(widget.get());
    layout->addWidget(new ElaText(QString::fromUtf8("模块诊断"), 24, widget.get()));
    auto detail = new ElaPlainTextEdit(widget.get());
    detail->setObjectName("CordisModuleDiagnosticsText");
    detail->setReadOnly(true);
    layout->addWidget(detail);
    auto refresh = new ElaPushButton(QString::fromUtf8("刷新"), widget.get());
    refresh->setObjectName("CordisModuleDiagnosticsRefresh");
    layout->addWidget(refresh);
    std::weak_ptr<cordis::module::ModuleManager> weak = manager;
    auto update = [weak, detail, diagnostics = std::move(diagnostics)] {
        auto source = weak.lock();
        if (!source) {
            detail->setPlainText(QString::fromUtf8("模块目录已关闭"));
            return;
        }
        auto result = source->snapshot();
        if (!result.hasValue()) {
            detail->setPlainText(QString::fromStdString(result.error().message));
            return;
        }
        QString text;
        for (const auto& row : result.value()) {
            using cordis::module::ModuleState;
            const char* state = row.state == ModuleState::Serving     ? "业务入口开放"
                                : row.state == ModuleState::Loaded    ? "已装载"
                                : row.state == ModuleState::Failed    ? "失败"
                                : row.state == ModuleState::Validated ? "已校验"
                                : row.state == ModuleState::Draining  ? "排空中"
                                                                      : "进程驻留";
            text += QString::fromStdString(row.manifest.moduleId) + QString("  #%1  ").arg(row.generation.value()) +
                    QString::fromUtf8(state);
            text += QString::fromUtf8("\n执行引用：%1；业务工厂：%2；物理映像：%3\n")
                        .arg(row.resources.records.size())
                        .arg(row.businessAccepted ? QString::fromUtf8("已接受") : QString::fromUtf8("未接受"))
                        .arg(row.nativePinned ? QString::fromUtf8("进程驻留") : QString::fromUtf8("未固定"));
            text += QString::fromStdString(row.libraryPath) + '\n';
            for (const auto& record : row.resources.records)
                text += "  " + QString::fromStdString(record.label) + '\n';
            for (const auto& reason : row.resources.pinReasons)
                text += QString::fromUtf8("  驻留原因：") + QString::fromStdString(reason) + '\n';
            for (const auto* failure : {&row.failure, &row.businessFailure})
                if (*failure)
                    text += QString::fromUtf8("  错误：") +
                            QString::fromStdString((*failure)->phase + ": " + (*failure)->message) + '\n';
            text += '\n';
        }
        if (diagnostics) {
            auto state = diagnostics->snapshot();
            if (!state.hasValue())
                text += QString::fromStdString(state.error().message);
            else if (state.value().configuration) {
                for (const auto& entry : state.value().configuration->entries)
                    if (entry.error)
                        text += QString::fromStdString(entry.path) + ": " + errorText(*entry.error) + '\n';
            }
        }
        detail->setPlainText(text);
    };
    QObject::connect(refresh, &QPushButton::clicked, widget.get(), update);
    update();
    return widget;
}
} // namespace notebook
