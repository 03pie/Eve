/** @file workspace_endurance_windows.cpp
 * @brief 一次准备隐藏的真实 HWND；只读采样当前 Win32 资源和可遍历 Qt 拥有树。
 */
#include "workspace_endurance_p.hpp"
#include <ElaWindow.h>
#include <QApplication>
#include <QGuiApplication>
#include <QJsonObject>
#include <QThread>
#include <QWidget>
#include <QWindow>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace workspace_endurance_test {
namespace {
QApplication* desktopApplication()
{
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    PLATFORM_CHECK(app && QThread::currentThread() == app->thread());
    PLATFORM_CHECK(QGuiApplication::platformName() == QStringLiteral("windows"));
    return app;
}

ElaWindow* existingShell()
{
    ElaWindow* result = nullptr;
    const auto windows = QApplication::topLevelWidgets();
    for (auto* widget : windows)
        if (auto* window = qobject_cast<ElaWindow*>(widget)) {
            PLATFORM_CHECK(!result);
            result = window;
        }
    return result;
}

/** @brief internalWinId 只读取已创建的窗口；关闭后没有 Shell 是合法采样状态。 */
QJsonObject nativeShellProof(ElaWindow* window)
{
    if (!window)
        return {{"present", false}};
    PLATFORM_CHECK(window->testAttribute(Qt::WA_DontShowOnScreen) &&
                   window->testAttribute(Qt::WA_ShowWithoutActivating));
    const auto id = window->internalWinId();
    PLATFORM_CHECK(id != 0);
    const auto handle = reinterpret_cast<HWND>(id);
    PLATFORM_CHECK(::IsWindow(handle));
    DWORD process = 0;
    const auto thread = ::GetWindowThreadProcessId(handle, &process);
    PLATFORM_CHECK(thread != 0 && process == ::GetCurrentProcessId() && thread == ::GetCurrentThreadId());
    PLATFORM_CHECK(!::IsWindowVisible(handle));
    return {{"present", true},
            {"hwnd", QString::number(static_cast<qulonglong>(id), 16)},
            {"process", QString::number(process)},
            {"thread", QString::number(thread)},
            {"visible", false}};
}

std::uint32_t guiResources(DWORD kind, const char* label)
{
    ::SetLastError(ERROR_SUCCESS);
    // GetCurrentProcess 是伪句柄；不创建必须排除或关闭的临时进程句柄。
    const auto count = ::GetGuiResources(::GetCurrentProcess(), kind);
    const auto error = ::GetLastError();
    if (count == 0 && error != ERROR_SUCCESS)
        throw std::runtime_error(std::string(label) + " failed, Windows error " + std::to_string(error));
    return static_cast<std::uint32_t>(count);
}

using Histogram = std::map<QString, std::uint64_t>;
QJsonObject histogram(const Histogram& values)
{
    QJsonObject result;
    for (const auto& entry : values)
        result.insert(entry.first, QString::number(static_cast<qulonglong>(entry.second)));
    return result;
}

QJsonObject ownedTrees(QApplication* app, QJsonObject native)
{
    std::vector<const QObject*> pending{app};
    const auto widgets = QApplication::allWidgets();
    for (const auto* widget : widgets)
        pending.push_back(widget);
    const auto windows = QGuiApplication::allWindows();
    for (const auto* window : windows)
        pending.push_back(window);

    std::set<const QObject*> visited;
    Histogram classes;
    std::map<QString, Histogram> names;
    while (!pending.empty()) {
        const auto* object = pending.back();
        pending.pop_back();
        if (!visited.insert(object).second)
            continue;
        const auto type = QString::fromLatin1(object->metaObject()->className());
        ++classes[type];
        ++names[type][object->objectName()];
        for (const auto* child : object->children())
            pending.push_back(child);
    }
    QJsonObject objectNames;
    for (const auto& entry : names)
        objectNames.insert(entry.first, histogram(entry.second));
    return {{"scope",
             QStringLiteral("deduplicated QObject owning trees reachable from QApplication/allWidgets/allWindows")},
            {"stable", QJsonObject{{"total", QString::number(static_cast<qulonglong>(visited.size()))},
                                   {"classes", histogram(classes)}}},
            {"detail", QJsonObject{{"objectNames", objectNames}, {"nativeShell", std::move(native)}}}};
}
} // namespace

void prepareDesktop(Scene& scene)
{
    (void)desktopApplication();
    PLATFORM_CHECK(scene.state && scene.scheduler && scene.scheduler->isCurrentThread());
    auto* window = existingShell();
    PLATFORM_CHECK(window && !window->isVisible() && !window->testAttribute(Qt::WA_DontShowOnScreen));
    // 原外壳尚未 show；所有原生创建和 show 副作用只属于本次准备，不进入 collector。
    window->setAttribute(Qt::WA_DontShowOnScreen);
    window->setAttribute(Qt::WA_ShowWithoutActivating);
    const auto id = window->winId();
    PLATFORM_CHECK(id != 0);
    (void)nativeShellProof(window);
    notebook::take(scene.state->shell.show());
    PLATFORM_CHECK(window->internalWinId() == id);
    (void)nativeShellProof(window);
}

DesktopSample sampleDesktop()
{
    auto* app = desktopApplication();
    auto native = nativeShellProof(existingShell());
    DWORD handles = 0;
    if (!::GetProcessHandleCount(::GetCurrentProcess(), &handles))
        throw std::runtime_error("GetProcessHandleCount failed, Windows error " + std::to_string(::GetLastError()));
    DesktopSample result;
    result.handles = static_cast<std::uint32_t>(handles);
    result.gdi = guiResources(GR_GDIOBJECTS, "GetGuiResources(GDI)");
    result.user = guiResources(GR_USEROBJECTS, "GetGuiResources(USER)");
    result.ownedObjectTrees = ownedTrees(app, std::move(native));
    return result;
}
} // namespace workspace_endurance_test
