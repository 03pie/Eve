/** @file diagnostics.cpp
 * @brief 原模块账目和配置错误的纯文本投影，不持目录/工厂或控件。
 */
#include "modules_panel_p.hpp"

namespace notebook::detail {
QString moduleErrorText(const eve::ErrorInfo& error, unsigned depth)
{
    auto text = QString::fromStdString(error.phase + ": " + error.message);
    if (depth < 4)
        for (std::size_t i = 0; i < error.causes.size() && i < 8; ++i)
            text += '\n' + QString(static_cast<int>(depth + 1) * 2, ' ') + moduleErrorText(error.causes[i], depth + 1);
    return text;
}
namespace {
const char* stateName(eve::module::ModuleState state)
{
    using eve::module::ModuleState;
    switch (state) {
    case ModuleState::Serving:
        return "业务入口开放";
    case ModuleState::Loaded:
        return "已装载";
    case ModuleState::Failed:
        return "失败";
    case ModuleState::Validated:
        return "已校验";
    case ModuleState::Draining:
        return "排空中";
    case ModuleState::Unloading:
        return "卸载中";
    case ModuleState::Unloaded:
        return "已卸载";
    case ModuleState::Resident:
        return "进程驻留";
    }
    return "未知";
}
} // namespace
QString moduleDiagnosticsText(const WorkspaceModulesSnapshot& snapshot)
{
    QString text;
    for (const auto& row : snapshot.generations) {
        const auto role = row.role == eve::module::GenerationRole::Current     ? "当前"
                          : row.role == eve::module::GenerationRole::Candidate ? "候选"
                                                                                  : "已退役";
        text += QString::fromStdString(row.manifest.moduleId) + QString("  #%1  ").arg(row.generation.value()) +
                QString::fromUtf8(role) + " · " + QString::fromUtf8(stateName(row.state));
        text += QString::fromUtf8("\n版本：%1；执行引用：%2；业务工厂：%3；映像引用：%4\n")
                    .arg(QString::fromStdString(row.manifest.moduleVersion))
                    .arg(row.resources.records.size())
                    .arg(row.businessAccepted ? QString::fromUtf8("已接受") : QString::fromUtf8("未接受"))
                    .arg(!row.nativeMapped  ? QString::fromUtf8("目录引用已释放")
                         : row.nativePinned ? QString::fromUtf8("目录引用驻留")
                                            : QString::fromUtf8("目录引用仍在"));
        text += QString::fromStdString(row.libraryPath) + '\n';
        for (const auto& record : row.resources.records)
            text += "  " + QString::fromStdString(record.label) + '\n';
        for (const auto& reason : row.resources.pinReasons)
            text += QString::fromUtf8("  驻留原因：") + QString::fromStdString(reason) + '\n';
        for (const auto* failure : {&row.failure, &row.businessFailure, &row.unloadFailure})
            if (*failure)
                text += QString::fromUtf8("  错误：") + moduleErrorText(**failure) + '\n';
        text += '\n';
    }
    for (const auto& error : snapshot.configurationErrors)
        text += moduleErrorText(error) + '\n';
    return text;
}
} // namespace notebook::detail
