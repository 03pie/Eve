/** @file retirement_text.cpp
 * @brief 原代次策略和回收回执的纯文本投影，不把目录引用当作物理成功。
 */
#include "modules_panel_p.hpp"
#include <algorithm>

namespace notebook::detail {
namespace {
QString attemptText(const char* title, const std::optional<eve::Result<void>>& result)
{
    const auto name = QString::fromUtf8(title);
    if (!result)
        return name + QString::fromUtf8("：未尝试\n");
    return name + (result->hasValue() ? QString::fromUtf8("：成功\n")
                                      : QString::fromUtf8("：拒绝或失败\n") + moduleErrorText(result->error()) + '\n');
}
} // namespace

QString retirementStatusText(const WorkspaceModulesSnapshot& modules, eve::GenerationId selected,
                             const eve::Result<eve::module::ModuleGenerationSnapshot>& target,
                             eve::GenerationId attempted, const eve::Result<WorkspaceRetirementReport>* result)
{
    QString text = selected.valid() ? QString::fromUtf8("所选代次 #%1\n").arg(selected.value())
                                    : QString::fromUtf8("请选择需要检查的代次。\n");
    const auto row = std::find_if(modules.generations.begin(), modules.generations.end(),
                                  [&](const auto& value) { return value.generation == selected; });
    if (row != modules.generations.end()) {
        text += QString::fromStdString(row->manifest.moduleId) + " · " +
                QString::fromStdString(row->manifest.moduleVersion) + '\n';
        text += row->loadPolicy == eve::module::ModuleLoadPolicy::ApprovedNative
                    ? QString::fromUtf8("首次装载策略：ApprovedNative；实际回收仍须通过原资源检查。\n")
                    : QString::fromUtf8("首次装载策略：Resident，代码进程驻留。\n");
        text += row->nativeMapped ? QString::fromUtf8("目录映像引用仍在。\n")
                                  : QString::fromUtf8("目录映像引用已释放；物理状态以实际卸载回执为准。\n");
        for (const auto& reason : row->resources.pinReasons)
            text += QString::fromUtf8("PIN：") + QString::fromStdString(reason) + '\n';
        if (row->unloadFailure)
            text += QString::fromUtf8("原物理终结诊断：") + moduleErrorText(*row->unloadFailure) + '\n';
    } else if (selected.valid())
        text += QString::fromUtf8("所选代次已不在当前目录；不会自动选择其他代次。\n");
    if (selected.valid() && !target.hasValue())
        text += QString::fromUtf8("范围检查：") + moduleErrorText(target.error()) + '\n';
    if (result) {
        text += QString::fromUtf8("\n最后一次尝试：GenerationId #%1\n").arg(attempted.value());
        if (!result->hasValue())
            text += QString::fromUtf8("请求未接纳：") + moduleErrorText(result->error()) + '\n';
        else {
            text += attemptText("排空", result->value().drain);
            text += attemptText("物理卸载", result->value().unload);
        }
    }
    return text;
}
} // namespace notebook::detail
