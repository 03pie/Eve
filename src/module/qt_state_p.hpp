/** @file qt_state_p.hpp
 * @brief Qt 可选状态接线；原资源句柄与对象指针始终来自同一个 QtBusiness。
 */
#pragma once
#include "qt_module_p.hpp"
#include <eve/qt/object.hpp>

namespace eve::module::detail {
Result<void> negotiateQtState(const std::shared_ptr<QtResidency>&, const std::vector<std::string>&);
Result<std::string> validateQtState(const std::shared_ptr<Generation>&, const std::shared_ptr<QtResidency>&,
                                    const std::shared_ptr<qt::QtScheduler>&, const std::string&, const StateSnapshot&);
Result<StateParticipant> qtStateParticipant(const std::shared_ptr<Generation>&, const std::shared_ptr<QtResidency>&,
                                            const std::shared_ptr<qt::QtScheduler>&, qt::QObjectResource, QObject*,
                                            const std::string&, std::function<void(ErrorInfo)>);
} // namespace eve::module::detail
