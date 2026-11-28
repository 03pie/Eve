#pragma once
#include "builtin_ids.hpp"
#include <eve/ui/output.hpp>

namespace eve::workbench {
inline constexpr char desktopLogServiceKey[] = "eve.desktop.runLog";
inline constexpr char desktopRunLogSinkKey[] = "eve.desktop.runLogSink";

/** Output-only capability for workbench plugins. */
class DesktopRunLogSink {
public:
    virtual ~DesktopRunLogSink() = default;
    virtual Result<std::uint64_t> write(std::string text, ui::OutputLevel level, ui::OutputDiagnostic diagnostic) = 0;
};

/** The launcher records the final cleanup receipt after the kernel finishes closing. */
class DesktopRunLogOwner : public DesktopRunLogSink {
public:
    virtual void finish(bool clean) = 0;
};

}
