/** @file contribution_backend_p.hpp
 * @brief Per-instance implementation of the shared SDK facades. The desktop owns the
 * service endpoints; contribution handles keep their individual records alive through retirement.
 */
#pragma once
#include <eve/ui/management.hpp>
#include <eve/ui/output.hpp>

namespace eve::ui::detail {
struct PageEndpoint {
    virtual ~PageEndpoint() = default;
    virtual Task<void> readyPage() = 0;
    virtual Result<void> navigatePage() = 0;
    virtual Task<CleanupReport> closePage() = 0;
    virtual Result<PageSnapshot> inspectPage() = 0;
};
struct PagesEndpoint {
    virtual ~PagesEndpoint() = default;
    virtual Result<PageHandle> addPage(const Context& owner, PageSpec spec) = 0;
    virtual Result<std::vector<PageSnapshot>> pageSnapshots() = 0;
};
struct CommandEndpoint {
    virtual ~CommandEndpoint() = default;
    virtual Task<void> readyCommand() = 0;
    virtual Result<bool> enabled() = 0;
    virtual Task<void> execute() = 0;
    virtual Task<CleanupReport> closeCommand() = 0;
    virtual Result<CommandSnapshot> inspectCommand() = 0;
};
struct CommandsEndpoint {
    virtual ~CommandsEndpoint() = default;
    virtual Result<CommandHandle> addCommand(const Context& owner, CommandSpec spec) = 0;
    virtual Result<std::vector<CommandSnapshot>> commandSnapshots() = 0;
};
struct NotificationsEndpoint {
    virtual ~NotificationsEndpoint() = default;
    virtual Result<std::uint64_t> postNotification(const Context& owner, Notification notification) = 0;
    virtual Result<std::vector<NotificationSnapshot>> notificationSnapshots() = 0;
};
struct OutputEndpoint {
    virtual ~OutputEndpoint() = default;
    virtual Result<std::uint64_t> writeOutput(const Context& owner, std::string channel, std::string text, OutputLevel level) = 0;
    virtual Result<std::vector<OutputEntry>> outputSnapshot() = 0;
};
struct DiagnosticsEndpoint {
    virtual ~DiagnosticsEndpoint() = default;
    virtual Result<DiagnosticsSnapshot> capture() = 0;
};
} // namespace eve::ui::detail
