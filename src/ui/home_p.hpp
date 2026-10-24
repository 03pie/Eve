/** @file home_p.hpp
 * @brief Example 风格首页和动态插件卡片，所有插件动作仍由宿主重新校验。
 */
#pragma once
#include "workbench_p.hpp"
#include <ElaScrollPage.h>
#include <QMap>
#include <QPointer>
#include <functional>
#include <vector>
class ElaFlowLayout;
class ElaPopularCard;
class ElaText;
namespace cordis::ui::detail {
/** @brief 首页只保存插件值快照及稳定路由键，不持有插件工厂或上下文。 */
class HomePage final : public ElaScrollPage {
public:
    /** @brief 复用 Example 的 ImageCard、FlowLayout 和 PopularCard 创建品牌首页。 */
    HomePage();
    /** @brief 增量更新卡片，未变化的实例保持悬停、布局和焦点。 */
    void setPlugins(const std::vector<HomePlugin>& plugins, std::function<void(const QString&)> open);

private:
    QPointer<QWidget> cards_;
    QPointer<ElaText> count_;
    QPointer<ElaText> empty_;
    ElaFlowLayout* flow_ = nullptr;
    QMap<QString, QPointer<ElaPopularCard>> items_;
    std::function<void(const QString&)> open_;
};
} // namespace cordis::ui::detail
