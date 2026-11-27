/** @file startup_result.hpp
 * @brief Preserves structured diagnostics across synchronous host assembly steps.
 */
#pragma once
#include <eve/result.hpp>
#include <stdexcept>
namespace eve::workbench {
class StartupError : public std::runtime_error {
public:
    explicit StartupError(ErrorInfo error) : std::runtime_error(error.message), detail(std::move(error)) {}
    ErrorInfo detail;
};
template<class T> T checked(Result<T> result)
{
    if (!result.hasValue()) throw StartupError(result.error());
    return std::move(result).value();
}
inline void checked(Result<void> result)
{
    if (!result.hasValue()) throw StartupError(result.error());
}
}
