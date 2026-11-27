#include "desktop_selection.hpp"
#include "desktop_contract.hpp"
#include "qt_system_p.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>

namespace eve::workbench {
namespace {
struct Preference {
    QString module = QString::fromLatin1(desktopModuleId);
    bool starting = false;
};
bool validId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z][A-Za-z0-9_-]*(\\.[A-Za-z0-9_-]+)*\\z"));
    return id.size() <= 128 && pattern.match(id).hasMatch();
}
Result<Preference> readPreference(const QString& directory)
{
    QFile file(QDir(directory).filePath("desktop-selection.json"));
    if (!file.exists()) return Result<Preference>::success({});
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096)
        return Result<Preference>::failure(makeError(ErrorCode::ConfigIo, "Cannot read desktop selection."));
    auto obj = QJsonDocument::fromJson(file.readAll()).object();
    if (obj.value("version").toInt() != 1 || !validId(obj.value("moduleId").toString()) ||
        !obj.value("starting").isBool())
        return Result<Preference>::failure(makeError(ErrorCode::ConfigInvalid, "Invalid desktop selection."));
    return Result<Preference>::success({obj.value("moduleId").toString(), obj.value("starting").toBool()});
}
Result<void> writePreference(const QString& directory, const Preference& state)
{
    QSaveFile file(QDir(directory).filePath("desktop-selection.json"));
    auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"moduleId", state.module},
        {"starting", state.starting}}).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return Result<void>::failure(makeError(ErrorCode::ConfigIo, "Cannot persist desktop selection."));
    return Result<void>::success();
}
std::unique_ptr<QLockFile> lock(const QString& directory, const char* file)
{
    auto guard = std::make_unique<QLockFile>(QDir(directory).filePath(QString::fromLatin1(file)));
    guard->setStaleLockTime(0); // A live slow launch must never be mistaken for a crashed owner.
    guard->tryLock(0);
    return guard;
}
ErrorInfo busy()
{
    return makeError(ErrorCode::ConfigConflict, "Desktop selection is busy or its lock cannot be created.");
}
}
DesktopSelection::DesktopSelection(QString directory)
    : directory_(std::move(directory)), selected_(QString::fromLatin1(desktopModuleId)) {}
DesktopSelection::~DesktopSelection() = default;

Result<QString> DesktopSelection::requested() const
{
    auto preference = readPreference(directory_);
    return preference.hasValue() ? Result<QString>::success(preference.value().module)
                                 : Result<QString>::failure(preference.error());
}

Result<void> DesktopSelection::request(QString id)
{
    if (!validId(id)) return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "Invalid desktop module id."));
    if (!QDir().mkpath(directory_)) return Result<void>::failure(makeError(ErrorCode::ConfigIo, "Cannot create desktop settings directory."));
    auto guard = lock(directory_, "desktop-selection.lock");
    if (!guard->isLocked()) return Result<void>::failure(busy());
    auto trial = lock(directory_, "desktop-startup.lock");
    if (!trial->isLocked()) return Result<void>::failure(busy());
    return writePreference(directory_, {std::move(id), false});
}

Result<QString> DesktopSelection::begin(bool safe)
{
    using Out = Result<QString>;
    if (trial_) return Out::failure(busy());
    selected_ = QString::fromLatin1(desktopModuleId);
    if (safe || !QFileInfo::exists(QDir(directory_).filePath("desktop-selection.json")))
        return Out::success(selected_);
    auto guard = lock(directory_, "desktop-selection.lock");
    if (!guard->isLocked()) return Out::success(selected_);
    auto read = readPreference(directory_);
    if (!read.hasValue()) {
        // A corrupt preference must not lock its user out of the extension manager.
        return Out::success(selected_);
    }
    auto state = read.value();
    if (state.module == selected_) return Out::success(selected_);
    auto trial = lock(directory_, "desktop-startup.lock");
    if (!trial->isLocked()) return Out::success(selected_);
    if (state.starting) {
        // The owner of the previous attempt exited before acknowledging its visible window.
        auto reset = writePreference(directory_, {});
        return reset.hasValue() ? Out::success(selected_) : Out::failure(reset.error());
    }
    state.starting = true;
    auto saved = writePreference(directory_, state);
    if (!saved.hasValue()) return Out::failure(saved.error());
    selected_ = state.module;
    trial_ = std::move(trial);
    return Out::success(selected_);
}
Result<void> DesktopSelection::finish(bool succeeded)
{
    if (!trial_) return Result<void>::success();
    // While trial_ is held, competing readers choose Ela and writers cannot mutate the preference.
    auto saved = writePreference(directory_, succeeded ? Preference{selected_, false} : Preference{});
    if (saved.hasValue()) trial_.reset();
    return saved;
}
Result<void> DesktopSelection::confirm() { return finish(true); }
Result<void> DesktopSelection::recover() { return finish(false); }
Result<void> DesktopSelection::defer() { return finish(true); }

ErrorInfo selectedDesktopFailure(ErrorInfo error)
{
    error.phase = "WORKBENCH_SELECTED_DESKTOP_FAILED";
    return error;
}

Result<Runtime> registerDesktopLayout(Kernel& kernel, std::shared_ptr<qt::QtScheduler> scheduler,
    QString manifestPath, QString moduleId, QString pluginId, QString digest)
{
    const auto reject = [](ErrorInfo error) { return Result<Runtime>::failure(selectedDesktopFailure(std::move(error))); };
    QFileInfo path(manifestPath);
    if (path.fileName() != QStringLiteral("module.json"))
        return reject(makeError(ErrorCode::ConfigInvalid, "Invalid desktop module manifest path."));
    auto root = path.absolutePath().toUtf8().toStdString();
    auto loaded = module::detail::loadInstalledQtModule(root, moduleId.toStdString(), pluginId.toStdString(), digest.toStdString());
    if (!loaded.hasValue()) return reject(loaded.error());
    auto negotiated = module::qtRuntime(loaded.value().generation,
        {pluginId.toStdString(), {ServiceRequirement::of<desktop::LayoutServiceV1>({desktop::layoutService})}, {}}, scheduler);
    if (!negotiated.hasValue()) return reject(negotiated.error());
    auto registered = kernel.registerRuntime(pluginId.toStdString(), negotiated.value());
    return registered.hasValue() ? registered : reject(registered.error());
}
}
