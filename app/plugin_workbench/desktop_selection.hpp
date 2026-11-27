/** @file desktop_selection.hpp
 * @brief Persist the requested desktop and guard a launch until its first visible window.
 */
#pragma once
#include <eve/kernel.hpp>
#include <eve/qt/scheduler.hpp>
#include <QString>
#include <memory>

class QLockFile;
namespace eve::workbench {
class DesktopSelection final {
public:
    explicit DesktopSelection(QString directory);
    ~DesktopSelection();
    Result<void> request(QString moduleId);
    Result<QString> requested() const;
    Result<QString> begin(bool safe);
    Result<void> confirm();
    Result<void> recover();
    /** An orderly exit or unrelated workspace error leaves the requested desktop available to retry. */
    Result<void> defer();
    QString selected() const { return selected_; }
    bool attemptedExternal() const { return bool(trial_); }
private:
    Result<void> finish(bool succeeded);
    QString directory_;
    QString selected_;
    std::unique_ptr<QLockFile> trial_;
};
ErrorInfo selectedDesktopFailure(ErrorInfo error);
Result<Runtime> registerDesktopLayout(Kernel& kernel, std::shared_ptr<qt::QtScheduler> scheduler,
    QString manifestPath, QString moduleId, QString pluginId, QString digest);
}
