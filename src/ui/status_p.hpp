/** @file status_p.hpp
 * @brief 由真实生命周期值生成 Ela 状态图标与文字，不从显示文案反推状态。
 */
#pragma once
#include <ElaIcon.h>
#include <ElaTheme.h>
#include <QIcon>
#include <QStandardItem>
#include <QString>
#include <cordis/instance.hpp>

namespace cordis::ui::detail {
/** @brief 呈现值不持有实例或回调，卡片和表格共享同一映射。 */
struct StatusPresentation {
    QString text;
    ElaIconType::IconName icon = ElaIconType::Circle;
};
/** @brief 清理失败仍是未静止状态，不能画成已停止。 */
inline StatusPresentation instanceStatus(InstanceState state, bool cleanupBlocked = false)
{
    switch (state) {
    case InstanceState::Active:
        return {QString::fromUtf8("运行中"), ElaIconType::CircleCheck};
    case InstanceState::Pending:
        return {QString::fromUtf8("等待依赖"), ElaIconType::Hourglass};
    case InstanceState::Loading:
        return {QString::fromUtf8("启动中"), ElaIconType::ArrowsRotate};
    case InstanceState::Unloading:
        return cleanupBlocked ? StatusPresentation{QString::fromUtf8("清理受阻"), ElaIconType::TriangleExclamation}
                              : StatusPresentation{QString::fromUtf8("清理中"), ElaIconType::ArrowsRotate};
    case InstanceState::Failed:
        return {QString::fromUtf8("失败"), ElaIconType::CircleXmark};
    case InstanceState::Disposed:
        return {QString::fromUtf8("已停止"), ElaIconType::CircleStop};
    }
    return {QString::fromUtf8("状态不可用"), ElaIconType::CircleQuestion};
}
/** @brief 图标从 ElaAwesome 获取，使用当前主题正文色保证明暗可读。 */
inline QIcon statusIcon(const StatusPresentation& value)
{
    return ElaIcon::getInstance()->getElaIcon(value.icon, 14,
                                              eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::BasicText));
}
/** @brief 表格保留可读文字及辅助描述，图标不是唯一的状态信息。 */
inline void decorateStatus(QStandardItem* item, const StatusPresentation& value)
{
    item->setIcon(statusIcon(value));
    item->setData(value.text, Qt::AccessibleDescriptionRole);
}
} // namespace cordis::ui::detail
