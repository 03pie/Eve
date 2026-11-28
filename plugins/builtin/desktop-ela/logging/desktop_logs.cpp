#include "desktop_logs.hpp"
#include "runtime_log_archive_p.hpp"

namespace eve::workbench {
namespace {
class RunSink final : public DesktopRunLogOwner {
public:
    explicit RunSink(std::shared_ptr<ui::detail::RuntimeLogArchive> archive) : archive_(std::move(archive)) {}
    Result<std::uint64_t> write(std::string text, ui::OutputLevel level, ui::OutputDiagnostic diagnostic) override
    { return archive_->writePlatform(std::move(text), level, std::move(diagnostic)); }
    void finish(bool clean) override { archive_->finish(clean); }
private:
    std::shared_ptr<ui::detail::RuntimeLogArchive> archive_;
};
}

Result<Runtime> registerDesktopLogs(Kernel& kernel, QString workspace, QString version,
    std::function<void(std::shared_ptr<DesktopRunLogOwner>)> onStarted)
{
    return kernel.registerPlugin(desktopLogPluginId,
        [workspace = std::move(workspace), version = std::move(version), onStarted = std::move(onStarted)](Context ctx) {
            auto run = std::make_shared<ui::detail::RuntimeLogArchive>(workspace, version);
            auto added = ctx.provide({desktopLogServiceKey}, std::make_shared<ui::detail::DesktopArchiveHandle>(run));
            if (!added.hasValue()) return failedTask(added.error());
            auto sink = std::make_shared<RunSink>(std::move(run));
            auto output = ctx.provide<DesktopRunLogSink>({desktopRunLogSinkKey}, sink);
            if (!output.hasValue()) return failedTask(output.error());
            onStarted(std::move(sink));
            return completedTask();
        }, {}, {ServiceOutput::of<ui::detail::DesktopArchiveHandle>({desktopLogServiceKey}),
                ServiceOutput::of<DesktopRunLogSink>({desktopRunLogSinkKey})});
}
}
