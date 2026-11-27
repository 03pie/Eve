/** @file layout.hpp
 * @brief Versioned contract for a selected desktop extension on the QApplication thread.
 */
#pragma once
#include <eve/instance.hpp>
#include <QWidget>
#include <memory>

namespace eve::desktop {
inline constexpr char layoutService[] = "eve.desktop.layout";
enum class Action { Home, Extensions, Settings, Commands, Output, Notifications };

/** The extension designs the top-level window and an empty layout for the shared workbench. */
struct Window {
    std::unique_ptr<QWidget> root;
    QWidget* workspace = nullptr;
};

class LayoutServiceV1 {
public:
    virtual ~LayoutServiceV1() = default;
    /** Transfer the unparented root once during activation. Eve releases it before the instance retires. */
    virtual Result<void> attach(const Context& owner, Window window) = 0;
    /** Dispatch a platform action from this desktop's own navigation. */
    virtual Result<void> invoke(const Context& owner, Action action) = 0;
};
}
