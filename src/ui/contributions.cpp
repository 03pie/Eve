/** @file contributions.cpp
 * @brief Stable plugin-facing symbols. A retained handle always dispatches to its own desktop epoch.
 */
#include "contribution_backend_p.hpp"

namespace eve::ui {
Task<void> PageHandle::ready() const
{
    auto record = control_;
    return record->readyPage();
}

Result<void> PageHandle::navigate() const
{
    auto record = control_;
    return record->navigatePage();
}

Task<CleanupReport> PageHandle::close() const
{
    auto record = control_;
    return record->closePage();
}

Result<PageSnapshot> PageHandle::snapshot() const
{
    auto record = control_;
    return record->inspectPage();
}

Result<PageHandle> UiPagesService::add(const Context& owner, PageSpec spec) const
{
    if (auto host = control_.lock())
        return host->addPage(owner, std::move(spec));
    return Result<PageHandle>::failure(makeError(ErrorCode::ContextClosing, "UI shell expired"));
}

Result<std::vector<PageSnapshot>> UiPagesService::snapshot() const
{
    if (auto host = control_.lock())
        return host->pageSnapshots();
    return Result<std::vector<PageSnapshot>>::success({});
}

Task<void> CommandHandle::ready() const
{
    auto record = control_;
    return record->readyCommand();
}

Result<bool> CommandHandle::enabled() const
{
    auto record = control_;
    return record->enabled();
}

Task<void> CommandHandle::execute() const
{
    auto record = control_;
    return record->execute();
}

Task<CleanupReport> CommandHandle::close() const
{
    auto record = control_;
    return record->closeCommand();
}

Result<CommandSnapshot> CommandHandle::snapshot() const
{
    auto record = control_;
    return record->inspectCommand();
}

Result<CommandHandle> CommandService::add(const Context& owner, CommandSpec spec) const
{
    if (auto host = control_.lock())
        return host->addCommand(owner, std::move(spec));
    return Result<CommandHandle>::failure(makeError(ErrorCode::ContextClosing, "command host expired"));
}

Result<std::vector<CommandSnapshot>> CommandService::snapshot() const
{
    if (auto host = control_.lock())
        return host->commandSnapshots();
    return Result<std::vector<CommandSnapshot>>::success({});
}

Result<std::uint64_t> NotificationService::post(const Context& owner, Notification notification) const
{
    if (auto host = control_.lock())
        return host->postNotification(owner, std::move(notification));
    return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "notification host expired"));
}

Result<std::vector<NotificationSnapshot>> NotificationService::snapshot() const
{
    if (auto host = control_.lock())
        return host->notificationSnapshots();
    return Result<std::vector<NotificationSnapshot>>::success({});
}

Result<std::uint64_t> OutputService::write(const Context& owner, std::string channel, std::string text, OutputLevel level) const
{
    if (auto host = control_.lock())
        return host->writeOutput(owner, std::move(channel), std::move(text), level);
    return Result<std::uint64_t>::failure(makeError(ErrorCode::ContextClosing, "output host expired"));
}

Result<std::vector<OutputEntry>> OutputService::snapshot() const
{
    if (auto host = control_.lock())
        return host->outputSnapshot();
    return Result<std::vector<OutputEntry>>::success({});
}

Result<DiagnosticsSnapshot> DiagnosticsService::snapshot() const
{
    if (auto host = control_.lock())
        return host->capture();
    return Result<DiagnosticsSnapshot>::failure(makeError(ErrorCode::ContextClosing, "management expired"));
}
} // namespace eve::ui
