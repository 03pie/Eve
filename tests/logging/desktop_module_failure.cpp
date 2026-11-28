/** Start the real launcher from a disposable copy of its runtime and inspect the resulting archive. */
#include "runtime_log_archive_p.hpp"
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <qt_windows.h>
#include <iostream>

namespace {
using namespace eve::ui::detail;
namespace files = eve::testing::modules;

#define CHECK(test) do { if (!(test)) throw std::runtime_error(std::string(#test) + " at " + std::to_string(__LINE__)); } while (false)

struct WindowSearch {
    DWORD process;
    HWND window = nullptr;
};

BOOL CALLBACK findWorkbench(HWND window, LPARAM argument)
{
    auto& search = *reinterpret_cast<WindowSearch*>(argument);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != search.process || !IsWindowVisible(window))
        return TRUE;
    wchar_t title[256] = {};
    if (GetWindowTextW(window, title, 256) && QString::fromWCharArray(title).startsWith("Eve")) {
        search.window = window;
        return FALSE;
    }
    return TRUE;
}

void closeWorkbench(QProcess& app)
{
    WindowSearch search{static_cast<DWORD>(app.processId())};
    QElapsedTimer deadline;
    deadline.start();
    while (!search.window && deadline.elapsed() < 15000 && app.state() == QProcess::Running) {
        EnumWindows(findWorkbench, reinterpret_cast<LPARAM>(&search));
        if (!search.window)
            app.waitForFinished(50);
    }
    if (!search.window)
        throw std::runtime_error("no Eve window in healthy runtime, stderr: " + app.readAllStandardError().toStdString());
    CHECK(PostMessageW(search.window, WM_CLOSE, 0, 0));
}

void exercise(const QString& executable, const QString& workspace, const QString& diagnostic)
{
    QProcess app;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", diagnostic.isEmpty() ? "windows" : "offscreen");
    app.setProcessEnvironment(environment);
    app.setWorkingDirectory(QFileInfo(executable).absolutePath());
    app.start(executable, {"--workspace", workspace});
    CHECK(app.waitForStarted(5000));
    if (diagnostic.isEmpty())
        closeWorkbench(app);
    CHECK(app.waitForFinished(20000));
    CHECK(app.exitStatus() == QProcess::NormalExit && (app.exitCode() == 0) == diagnostic.isEmpty());
    const auto runs = readSavedLogRuns(workspace);
    CHECK(runs.size() == 1 && runs.front().state == LogRunState::Ended);
    bool failed = false, cleaned = false;
    CHECK(scanSavedDescending(workspace, runs.front().id, 0, [&](const LogRecord& row) {
        if (row.sourceId == "platform") {
            failed |= !diagnostic.isEmpty() && row.entry.level == eve::ui::OutputLevel::Error &&
                      row.diagnosticCode == diagnostic && !row.entry.text.empty();
            cleaned |= row.diagnosticCode == "WORKBENCH_CLEANUP_COMPLETE";
        }
        return true;
    }).isEmpty());
    if (!cleaned || failed != !diagnostic.isEmpty()) {
        std::cerr << "Expected '" << diagnostic.toStdString() << "' and a cleanup marker, stderr: "
                  << app.readAllStandardError().constData() << '\n';
        throw std::runtime_error("unexpected desktop startup archive");
    }
}

void copyDlls(const QDir& source, const QDir& destination)
{
    for (const auto& name : source.entryList({"*.dll"}, QDir::Files))
        CHECK(QFile::copy(source.filePath(name), destination.filePath(name)));
}

void verify(const QFileInfo& executable)
{
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    const QDir runtime(temporary.path());
    const QDir source = executable.absoluteDir();
    const auto program = runtime.filePath(executable.fileName());
    CHECK(QFile::copy(executable.absoluteFilePath(), program));
    copyDlls(source, runtime);
    if (source.exists("platforms")) {
        CHECK(runtime.mkdir("platforms"));
        copyDlls(QDir(source.filePath("platforms")), QDir(runtime.filePath("platforms")));
    }
    const QString system = QStringLiteral("system/desktop/");
    CHECK(runtime.mkpath(system));
    const auto manifest = runtime.filePath(system + "module.json");
    const auto original = files::read(source.filePath(system + "module.json"));
    const auto identity = QJsonDocument::fromJson(original).object();
    const auto library = identity.value("library").toString();
    CHECK(!library.isEmpty() && QFileInfo(library).fileName() == library);
    const auto image = runtime.filePath(system + library);
    exercise(program, runtime.filePath("no-manifest"), "WORKBENCH_DESKTOP_MANIFEST_MISSING");
    files::write(manifest, original);
    exercise(program, runtime.filePath("no-library"), "WORKBENCH_DESKTOP_LIBRARY_MISSING");
    CHECK(QFile::copy(source.filePath(system + library), image));
    auto modified = identity;
    modified["moduleId"] = "org.eve.test.substitute";
    files::write(manifest, QJsonDocument(modified).toJson());
    exercise(program, runtime.filePath("wrong-identity"), "WORKBENCH_DESKTOP_IDENTITY_INVALID");
    const auto alternate = QStringLiteral("alternate.dll");
    CHECK(QFile::copy(image, runtime.filePath(system + alternate)));
    modified = identity;
    modified["library"] = alternate;
    files::write(manifest, QJsonDocument(modified).toJson());
    exercise(program, runtime.filePath("wrong-library"), "WORKBENCH_DESKTOP_IDENTITY_INVALID");
    modified = identity;
    modified["abi"] = QJsonObject{{"major", 2}, {"minor", 0}};
    files::write(manifest, QJsonDocument(modified).toJson());
    exercise(program, runtime.filePath("wrong-abi"), "WORKBENCH_DESKTOP_INCOMPATIBLE");
    modified = identity;
    modified["moduleVersion"] = "99.0.0";
    files::write(manifest, QJsonDocument(modified).toJson());
    exercise(program, runtime.filePath("wrong-desktop-version"), "WORKBENCH_DESKTOP_INCOMPATIBLE");
    files::write(manifest, original);
    const auto messages = system + "messages.en.json";
    CHECK(QFile::copy(source.filePath(messages), runtime.filePath(messages)));
    exercise(program, runtime.filePath("healthy"), {});
    QFile damaged(image);
    CHECK(damaged.open(QIODevice::Append) && damaged.write("corrupt") == 7);
    damaged.close();
    exercise(program, runtime.filePath("damaged"), "WORKBENCH_DESKTOP_HASH_INVALID");
}
} // namespace

int main(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    try {
        CHECK(app.arguments().size() == 2);
        verify(QFileInfo(app.arguments().at(1)));
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
