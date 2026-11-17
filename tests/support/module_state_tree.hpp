/** @file module_state_tree.hpp
 * @brief 预约入口只依赖非模板分派，状态树模板由独立翻译单元实例化。
 */
#pragma once
#include <memory>

class QString;
struct Scene;
namespace cordis {
class Runtime;
class IScheduler;
class Kernel;
} // namespace cordis
bool operationStateTreeCase(Scene&, const cordis::Runtime& old, const cordis::Runtime& next,
                            const std::shared_ptr<cordis::IScheduler>& scheduler,
                            const std::shared_ptr<cordis::Kernel>& kernel, const QString& mode);
