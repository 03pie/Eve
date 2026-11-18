/** @file upgrade_text.cpp
 * @brief 原升级/保存值的纯文本投影；不查询平台、不推断提交或物理成功。
 */
#include "modules_panel_p.hpp"
#include <algorithm>

namespace notebook::detail {
namespace {
using namespace cordis;
QString policy(const module::ModuleManifest& manifest)
{
    return manifest.loader == module::ModuleLoader::QtUi ? QString::fromUtf8("Qt / 进程驻留")
           : manifest.unloadPolicy == module::ModuleUnloadPolicy::Explicit
               ? QString::fromUtf8("Native / explicit 声明（仍由宿主精确批准）")
               : QString::fromUtf8("Native / 进程驻留");
}
bool recoveryFailed(const ErrorInfo& error, unsigned depth = 0)
{
    if (error.code == ErrorCode::RecoveryFailed)
        return true;
    if (depth < 4)
        for (std::size_t index = 0; index < error.causes.size() && index < 8; ++index)
            if (recoveryFailed(error.causes[index], depth + 1))
                return true;
    return false;
}
QString resultText(const std::optional<Result<void>>& result)
{
    if (!result)
        return QString::fromUtf8("未尝试");
    return result->hasValue() ? QString::fromUtf8("成功") : moduleErrorText(result->error());
}
QString reportText(const WorkspaceUpgradeReport& report, const WorkspaceModulesSnapshot& modules, bool terminal,
                   bool hostActive)
{
    QString text = QString::fromUtf8("模块 %1 · previous #%2 · candidate %3\n")
                       .arg(QString::fromStdString(report.alias))
                       .arg(report.previous.value())
                       .arg(report.candidate.valid() ? "#" + QString::number(report.candidate.value())
                                                     : QString::fromUtf8("未返回代次"));
    if (report.replacement) {
        const auto& actual = *report.replacement;
        text += QString("OperationId #%1\ncommitted=%2 · recovered=%3 · resourcesRetained=%4\n")
                    .arg(actual.operation.value())
                    .arg(actual.committed)
                    .arg(actual.recovered)
                    .arg(actual.resourcesRetained);
        if (actual.committed)
            text += QString::fromUtf8("业务已共同提交。");
        else if (actual.recovered)
            text += QString::fromUtf8("旧业务已完整恢复。");
        if (terminal)
            switch (actual.outcome) {
            case ReplacementOutcome::Applied:
                text += " Applied\n";
                break;
            case ReplacementOutcome::Restored:
                text += " Restored\n";
                break;
            case ReplacementOutcome::Cancelled:
                text += " Cancelled\n";
                break;
            case ReplacementOutcome::Failed:
                text += " Failed\n";
                break;
            }
        else
            text += QString::fromUtf8("\n原事务仍在推进，尚无业务终态回执。\n");
        for (const auto& error : actual.errors) {
            if (recoveryFailed(error))
                text += QString::fromUtf8("RecoveryFailed：旧业务恢复失败。\n");
            text += moduleErrorText(error) + '\n';
        }
    } else if (report.failure)
        text += QString::fromUtf8("本次整体替换尚未开始；未取得原事务 OperationId。\n");
    else
        text += QString::fromUtf8("尚无原事务 OperationId。\n");
    text += QString("retained=%1 · deliveryBlocked=%2\n").arg(report.retained).arg(report.deliveryBlocked);
    if (hostActive)
        text += report.retained ? QString::fromUtf8("宿主原执行因故障保留。\n")
                                : QString::fromUtf8("宿主原执行仍在途，等待收尾。\n");
    if (report.failure)
        text += moduleErrorText(*report.failure) + '\n';
    if (report.retired.valid())
        text += QString::fromUtf8("清理目标 #%1\n").arg(report.retired.value());
    if (report.nativeClose) {
        if (!report.nativeClose->hasValue())
            text += QString::fromUtf8("Native 会话退出：") + moduleErrorText(report.nativeClose->error()) + '\n';
        else {
            text += QString::fromUtf8("Native 会话 quiescent=%1\n").arg(report.nativeClose->value().quiescent);
            for (const auto& error : report.nativeClose->value().errors)
                text += moduleErrorText(error) + '\n';
        }
    }
    text += QString::fromUtf8("排空：") + resultText(report.drain) + '\n';
    const auto target = std::find_if(modules.generations.begin(), modules.generations.end(),
                                     [&](const auto& row) { return row.generation == report.retired; });
    if (target != modules.generations.end() && target->manifest.loader == module::ModuleLoader::QtUi)
        text += QString::fromUtf8("Qt 代码进程驻留；未提供物理卸载。\n");
    else
        text += QString::fromUtf8("物理卸载尝试：") + resultText(report.unload) + '\n';
    return text;
}
} // namespace

QString upgradePreviewText(const UpgradePreview& preview)
{
    const auto& old = preview.previousManifest;
    const auto& next = preview.artifact.manifest();
    QString text =
        QString::fromUtf8("%1\n旧版本 %2 · Current #%3\n旧 SHA256 %4\n候选版本 %5\n候选 SHA256 %6\n%7\n目标修订 %8\n")
            .arg(QString::fromStdString(next.moduleId), QString::fromStdString(old.moduleVersion))
            .arg(preview.previous.value())
            .arg(QString::fromStdString(old.sha256), QString::fromStdString(next.moduleVersion),
                 QString::fromStdString(next.sha256), policy(next))
            .arg(preview.desiredRevision);
    text += QString::fromUtf8("旧声明策略：") + policy(old) + '\n';
    text += QString::fromUtf8("影响配置路径：\n");
    for (const auto& path : preview.affectedPaths)
        text += "  " + QString::fromStdString(path) + '\n';
    text += QString::fromUtf8("实际闭包成员：\n");
    for (const auto& member : preview.impact.members)
        text += QString("  instance #%1 / epoch %2 / runtime #%3\n")
                    .arg(member.instance.value())
                    .arg(member.epoch)
                    .arg(member.runtime.value());
    text += QString::fromUtf8("文件与元数据预检通过；执行时仍须核对模块接口、状态及当前目标。\n");
    return text;
}

QString upgradePhaseText(const WorkspaceUpgradeSnapshot& upgrade)
{
    if (!upgrade.active)
        return upgrade.accepting ? QString::fromUtf8("可选择候选并检查影响。")
                                 : QString::fromUtf8("当前不接纳升级操作。");
    QString text = QString::fromUtf8("原执行在途");
    if (upgrade.phase) {
        using Phase = cordis::ReplacementPhase;
        switch (*upgrade.phase) {
        case Phase::Prepared:
            text = "Prepared · 已准备";
            break;
        case Phase::Capturing:
            text = "Capturing · 捕获状态";
            break;
        case Phase::Stopping:
            text = "Stopping · 停止旧业务";
            break;
        case Phase::Building:
            text = "Building · 构建候选";
            break;
        case Phase::Discarding:
            text = "Discarding · 清理候选";
            break;
        case Phase::Recovering:
            text = "Recovering · 恢复旧业务";
            break;
        case Phase::Committed:
            text = "Committed · 业务已提交，等待宿主收尾";
            break;
        case Phase::Restored:
            text = "Restored · 旧业务已恢复，等待宿主收尾";
            break;
        case Phase::Failed:
            text = "Failed · 原事务故障";
            break;
        case Phase::Cancelled:
            text = "Cancelled · 原事务取消，等待宿主收尾";
            break;
        }
    }
    if (upgrade.cancellationRequested)
        text += QString::fromUtf8(" · 已请求取消，等待原执行结算");
    if (upgrade.deliveryBlocked)
        text += QString::fromUtf8(" · deliveryBlocked，保留原执行");
    return text;
}

bool upgradePhaseTerminal(cordis::ReplacementPhase phase)
{
    using Phase = cordis::ReplacementPhase;
    return phase == Phase::Committed || phase == Phase::Restored || phase == Phase::Failed || phase == Phase::Cancelled;
}

QString upgradeReportText(const WorkspaceUpgradeSnapshot& upgrade, const WorkspaceModulesSnapshot& modules,
                          const std::string& alias)
{
    if (upgrade.active)
        return reportText(*upgrade.active, modules, upgrade.phase && upgradePhaseTerminal(*upgrade.phase), true);
    const auto found = std::find_if(upgrade.history.rbegin(), upgrade.history.rend(),
                                    [&](const auto& report) { return report.alias == alias; });
    return found == upgrade.history.rend() ? QString::fromUtf8("该模块暂无升级记录。")
                                           : reportText(*found, modules, true, false);
}

QString upgradeSaveText(const WorkspaceModulesSnapshot& modules,
                        const cordis::Task<cordis::loader::SaveReport>::Snapshot& result, bool pending)
{
    QString text =
        QString::fromUtf8("当前目标修订 %1 · 已保存 %2 · %3")
            .arg(modules.desiredRevision)
            .arg(modules.persistedRevision ? QString::number(*modules.persistedRevision) : QString::fromUtf8("无"))
            .arg(modules.dirty ? QString::fromUtf8("配置未保存") : QString::fromUtf8("配置已同步"));
    if (pending || modules.writing)
        text += QString::fromUtf8(" · 保存中");
    if (!result)
        return text;
    if (!result->hasValue())
        return text + '\n' + moduleErrorText(result->error());
    const auto& report = result->value();
    const char* outcome = "Failed";
    switch (report.outcome) {
    case cordis::loader::SaveOutcome::Written:
        outcome = "Written";
        break;
    case cordis::loader::SaveOutcome::Unchanged:
        outcome = "Unchanged";
        break;
    case cordis::loader::SaveOutcome::Conflict:
        outcome = "Conflict";
        break;
    case cordis::loader::SaveOutcome::Failed:
        outcome = "Failed";
        break;
    case cordis::loader::SaveOutcome::Skipped:
        outcome = "Skipped";
        break;
    }
    text += QString::fromUtf8("\n保存回执：修订 %1 · %2").arg(report.revision).arg(QString::fromLatin1(outcome));
    if (report.error)
        text += '\n' + moduleErrorText(*report.error);
    return text;
}
} // namespace notebook::detail
