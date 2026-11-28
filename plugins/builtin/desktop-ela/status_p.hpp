/** @file status_p.hpp
 * @brief 由真实生命周期值生成 Ela 状态图标与文字，不从显示文案反推状态。
 */
#pragma once
#include "desktop_text.hpp"
#include <ElaIcon.h>
#include <ElaTheme.h>
#include <QIcon>
#include <QStandardItem>
#include <QString>
#include <eve/instance.hpp>

namespace eve::ui::detail {
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
        return {eve::desktopText("configuration.running"), ElaIconType::CircleCheck};
    case InstanceState::Pending:
        return {eve::desktopText("configuration.waitingForDependencies"), ElaIconType::Hourglass};
    case InstanceState::Loading:
        return {eve::desktopText("configuration.starting"), ElaIconType::ArrowsRotate};
    case InstanceState::Unloading:
        return cleanupBlocked ? StatusPresentation{eve::desktopText("workbench.cleanupBlocked"), ElaIconType::TriangleExclamation}
                              : StatusPresentation{eve::desktopText("configuration.cleaningUp"), ElaIconType::ArrowsRotate};
    case InstanceState::Failed:
        return {eve::desktopText("configuration.failed"), ElaIconType::CircleXmark};
    case InstanceState::Disposed:
        return {eve::desktopText("configuration.stopped"), ElaIconType::CircleStop};
    }
    return {eve::desktopText("workbench.statusUnavailable"), ElaIconType::CircleQuestion};
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
} // namespace eve::ui::detail
