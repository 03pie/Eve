/** @file definition_driver.hpp
 * @brief 配置会话与实际驱动共用定义目录，不另外维护 schema 副本。
 */
#pragma once
#include <cordis/loader/coordinator.hpp>
#include <cordis/loader/definitions.hpp>

namespace cordis::loader {
class IConfiguredReconcileDriver : public IReconcileDriver {
public:
    /** @brief owner 读取当前定义身份；运行时专用旧驱动返回空，不支持定义换代协议。 */
    virtual std::shared_ptr<const ConfigurationDefinitions> configurationDefinitions() const = 0;
};
} // namespace cordis::loader
