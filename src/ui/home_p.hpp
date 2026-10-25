/** @file home_p.hpp
 * @brief Example 风格首页和状态卡片，电源操作由宿主重新校验。
 */
#pragma once
#include "workbench_p.hpp"
#include <ElaScrollPage.h>
#include <QMap>
#include <QPointer>
#include <functional>
#include <vector>
class ElaFlowLayout;
class ElaScrollPageArea;
class ElaText;
namespace cordis::ui::detail {
/** @brief 首页只保存插件值快照及宿主操作，不持有插件工厂或上下文。 */
class HomePage final : public ElaScrollPage {
public:
    /** @brief 复用 Ela 背景卡、流布局和区域组件创建首页。 */
    HomePage();
    /** @brief 增量更新卡片，未变化的实例保持悬停、布局和焦点。 */
    void setPlugins(const std::vector<HomePlugin>& plugins,
                    std::function<Result<void>(const QString&, bool, std::uint64_t)> power,
                    std::function<void(const std::function<void()>&)> access);

private:
    QPointer<QWidget> cards_;
    QPointer<ElaText> count_;
    QPointer<ElaText> empty_;
    QPointer<ElaText> error_;
    ElaFlowLayout* flow_ = nullptr;
    QMap<QString, QPointer<ElaScrollPageArea>> items_;
    std::function<Result<void>(const QString&, bool, std::uint64_t)> power_;
    std::function<void(const std::function<void()>&)> access_;
};
} // namespace cordis::ui::detail
