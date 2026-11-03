/** @file runtime_origin_p.hpp
 * @brief 原生与 Qt 工厂共用的来源租约；不保留插件可调用指针或第二套代次目录。
 */
#pragma once
#include <cordis/module/ledger.hpp>
#include <memory>
#include <string>

namespace cordis::module::detail {
struct Generation;
class RuntimeOriginControl {
public:
    RuntimeOriginControl(std::shared_ptr<Generation> generation, std::string plugin, ModuleLease lease);
    RuntimeOriginControl(const RuntimeOriginControl&) = delete;
    RuntimeOriginControl& operator=(const RuntimeOriginControl&) = delete;
    ~RuntimeOriginControl();
    /** @brief 工厂构造成功后、公开前登记真实身份；只调用一次，不调用插件。 */
    Result<void> bind(RuntimeId runtime);

private:
    std::shared_ptr<Generation> generation_;
    std::string plugin_;
    RuntimeId runtime_;
    ModuleLease lease_;
};
/** @brief 由既有 GenerationAdmission 保护创建期间；最后激活闭包销毁才解除登记。 */
Result<std::shared_ptr<RuntimeOriginControl>> retainRuntimeOrigin(GenerationId generation, std::string plugin);
} // namespace cordis::module::detail
