/** @file workspace_endurance_p.hpp
 * @brief 原平台连续替换的测试私有纯值采样；不保留代次、运行能力或回调。
 */
#pragma once
#include "platform_upgrade_p.hpp"
#include <QJsonObject>
#include <cstdint>

namespace workspace_endurance_test {
using platform_upgrade_test::Scene;
struct CatalogSample {
    QJsonObject stable;
    QJsonObject detail;
    std::uint64_t dependencyEvaluations = 0;
};
struct DesktopSample {
    QJsonObject ownedObjectTrees;
    std::uint32_t handles = 0, gdi = 0, user = 0;
};
CatalogSample sampleCatalog(const Scene& scene);
/** @brief 初始采样前完成 Qt 互斥锁争用和原生窗口准备；不激活或显示桌面窗口。 */
void prepareDesktop(Scene& scene);
DesktopSample sampleDesktop();
} // namespace workspace_endurance_test
