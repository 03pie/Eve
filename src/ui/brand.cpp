/** @file brand.cpp
 * @brief 为静态 UI 库显式链接 Eve 资源，并保留设计合集提供的全部图标尺寸。
 */
#include "brand_p.hpp"
#include <QResource>
#include <QSize>
#include <QString>
#include <initializer_list>

/** @brief Q_INIT_RESOURCE 位于全局命名空间，确保静态链接时带入资源注册单元。 */
static void initializeEveBrandResources()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE(eve_brand);
        return true;
    }();
    (void)initialized;
}

namespace cordis::ui::detail {
QIcon eveApplicationIcon()
{
    initializeEveBrandResources();
    QIcon result;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        result.addFile(QStringLiteral(":/cordis/brand/eve/") + QString::number(size) + QStringLiteral(".png"),
                       QSize(size, size));
    return result;
}
} // namespace cordis::ui::detail
