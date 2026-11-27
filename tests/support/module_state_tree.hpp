/** @file module_state_tree.hpp
 * @brief 预约入口只依赖非模板分派，状态树模板由独立翻译单元实例化。
 */
#pragma once
#include <memory>

class QString;
struct Scene;
namespace eve {
class Runtime;
class IScheduler;
class Kernel;
} // namespace eve
bool operationStateTreeCase(Scene&, const eve::Runtime& old, const eve::Runtime& next,
                            const std::shared_ptr<eve::IScheduler>& scheduler,
                            const std::shared_ptr<eve::Kernel>& kernel, const QString& mode);
