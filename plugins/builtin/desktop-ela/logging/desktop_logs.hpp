#pragma once
#include "desktop_run_log.hpp"
#include <eve/kernel.hpp>
#include <QString>

namespace eve::workbench {
/** Mount before the desktop so that a failed UI module load has the same journal as a healthy run. */
Result<Runtime> registerDesktopLogs(Kernel& kernel, QString workspace, QString version,
    std::function<void(std::shared_ptr<DesktopRunLogOwner>)> onStarted);
}
