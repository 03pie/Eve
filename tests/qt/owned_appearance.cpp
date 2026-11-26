/** @brief Inspect notification and managed-window surfaces in one real plugin workbench. */
#include "platform_workspace_p.hpp"
#include <ElaApplication.h>
#include <ElaTheme.h>
#include <ElaWindow.h>
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListView>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSplitter>
#include <QTest>
#include <QToolButton>
#include <QWindow>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace modules_view_test {
using namespace platform_workspace_test;
void finishModules(Scene&);

namespace {
template <class T> T* required(QWidget* owner, const char* name)
{
    auto value = owner->findChild<T*>(name);
    PLATFORM_CHECK(value);
    return value;
}
struct Evidence {
    QDir folder;
    QJsonArray captures;
    void record(QString name, QWidget* surface, QWidget* field, bool dark, int mode)
    {
        PLATFORM_CHECK(surface->isVisible() && field->isVisible());
        auto top = surface->window();
        // Capture the application composition while the widgets and their native window are still alive.
        QTest::qWait(35);
        const auto capture = surface->grab();
        const QPoint point = field->mapTo(surface, QPoint(field->width() - 15, field->height() - 12));
        const QImage image = capture.toImage();
        const qreal dpr = capture.devicePixelRatio();
        const QColor sample = image.pixelColor(point * dpr);
        const QString file = QString("%1-%2-%3").arg(name, dark ? "dark" : "light").arg(mode);
        PLATFORM_CHECK(capture.save(folder.filePath(file + ".png")));
        QJsonObject row{{"surface", name}, {"dark", dark}, {"material", mode}, {"target", field->objectName()},
                        {"window", QString::number(top->winId())}, {"pixel", sample.name(QColor::HexArgb)},
                        {"sampleX", point.x()}, {"sampleY", point.y()}, {"width", surface->width()},
                        {"height", surface->height()}, {"dpr", capture.devicePixelRatio()},
                        {"screenshot", file + ".png"}, {"windowsMaterialAccepted", top->property("ElaWindowMaterialApplied").toBool()}};
        if (name == "configuration") {
            const auto handle = required<QSplitter>(surface, "CordisConfigSplitter")->handle(1);
            const auto divider = image.pixelColor(handle->mapTo(surface, handle->rect().center()) * dpr);
            row["divider"] = divider.name(QColor::HexArgb);
            PLATFORM_CHECK(divider.alpha() < 200 || (dark ? divider.lightness() < 130 : divider.lightness() > 150));
        }
        if (name.startsWith("notice")) {
            QRect textArea = field->rect();
            if (auto list = qobject_cast<QListView*>(field->parentWidget()))
                textArea = list->visualRect(list->model()->index(0, 0)).adjusted(12, 2, -12, -2);
            textArea.moveTopLeft(field->mapTo(surface, textArea.topLeft()));
            const QRect pixels(textArea.topLeft() * dpr, textArea.size() * dpr);
            int ink = 0;
            for (int y = pixels.top(); y <= pixels.bottom(); ++y)
                for (int x = pixels.left(); x <= pixels.right(); ++x) {
                    const auto color = image.pixelColor(x, y);
                    if (color.alpha() > 240 && color.saturation() < 25 &&
                        (dark ? color.lightness() - sample.lightness() : sample.lightness() - color.lightness()) > 65)
                        ++ink;
                }
            row["contrastingTextPixels"] = ink;
            PLATFORM_CHECK(ink >= 15);
        }
#ifdef Q_OS_WIN
        if (QApplication::platformName() == "windows" && mode == ElaApplicationType::Acrylic) {
            top->raise();
            top->activateWindow();
            const auto hwnd = reinterpret_cast<HWND>(top->winId());
            const UINT zOrderOnly = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
            PLATFORM_CHECK(SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, zOrderOnly));
            struct RestoreWindowOrder {
                HWND window;
                UINT flags;
                ~RestoreWindowOrder() { SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, flags); }
            } restore{hwnd, zOrderOnly};
            auto screen = top->windowHandle()->screen();
            const QRect monitor = screen->geometry();
            if (top->width() <= monitor.width() && top->height() <= monitor.height() &&
                !monitor.contains(QRect(top->mapToGlobal({0, 0}), top->size())))
                top->move(monitor.center() - top->rect().center());
            QTest::qWait(140);
            RECT client{};
            PLATFORM_CHECK(GetClientRect(hwnd, &client));
            // Capture after desktop composition. Capturing the HWND alone loses its system backdrop.
            for (int x : {16, static_cast<int>(client.right / 2), static_cast<int>(client.right) - 16}) {
                for (int y : {16, static_cast<int>(client.bottom / 2), static_cast<int>(client.bottom) - 16}) {
                    POINT corner{x, y};
                    PLATFORM_CHECK(ClientToScreen(hwnd, &corner));
                    PLATFORM_CHECK(GetAncestor(WindowFromPoint(corner), GA_ROOT) == hwnd);
                }
            }
            const QPoint start = top->mapToGlobal({0, 0}) - monitor.topLeft();
            const auto native = screen->grabWindow(0, start.x(), start.y(), top->width(), top->height());
            PLATFORM_CHECK(!native.isNull() && native.save(folder.filePath(file + "-window.png")));
            row["nativeScreenshot"] = file + "-window.png";
        }
#endif
        captures.append(row);
        QFile report(folder.filePath("captures.json"));
        PLATFORM_CHECK(report.open(QIODevice::WriteOnly) && report.write(QJsonDocument(captures).toJson()) > 0);
        // Material windows may delegate the background to DWM. Opaque dark viewports must not paint white.
        if (name.startsWith("notice") || mode == ElaApplicationType::Normal || (dark && sample.alpha() > 200))
            if (dark ? sample.lightness() >= 130 : sample.lightness() <= 150)
                throw std::runtime_error((file + " sampled " + sample.name(QColor::HexArgb)).toStdString());
    }
};
} // namespace

void ownedAppearance(const QString& sourceBin)
{
    const auto path = qEnvironmentVariable("CORDIS_OWNED_APPEARANCE_EVIDENCE");
    PLATFORM_CHECK(!path.isEmpty() && QDir().mkpath(path));
    Evidence evidence{QDir(path), {}};
    PLATFORM_CHECK(evidence.folder.entryList(QDir::Files).isEmpty());
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    take(scene.state->shell.show());
    ElaWindow* window = nullptr;
    for (auto top : QApplication::topLevelWidgets())
        if (auto main = qobject_cast<ElaWindow*>(top)) window = main;
    PLATFORM_CHECK(window);
    required<QAction>(window, "CordisOpenSettings")->trigger();
    const auto theme = required<QComboBox>(window, "CordisSettingsTheme");
    const auto material = required<QComboBox>(window, "CordisSettingsMaterial");
    const auto center = required<QWidget>(window, "CordisNotificationCenter");
    const auto list = required<QListView>(center, "CordisNotificationList");
    const auto empty = required<QLabel>(center, "CordisNotificationEmpty");
    const auto bell = required<QToolButton>(window, "CordisShowNotifications");
    const auto cycle = [&](const std::function<void(bool, int)>& observe) {
        for (bool dark : {false, true}) {
            theme->setCurrentIndex(dark);
            for (int mode = 0; mode < 6; ++mode) {
                material->setCurrentIndex(mode);
                PLATFORM_CHECK(eTheme->getThemeMode() == (dark ? ElaThemeType::Dark : ElaThemeType::Light));
                if (QApplication::platformName() == "windows")
                    PLATFORM_CHECK(static_cast<int>(eApp->getWindowDisplayMode()) == mode);
                observe(dark, mode);
            }
        }
    };
    bell->click();
    PLATFORM_CHECK(empty->isVisible() && !list->isVisible());
    cycle([&](bool dark, int mode) { evidence.record("notice-empty", center, empty, dark, mode); });
    bell->click();
    auto registration = take(scene.state->kernel->registerPlugin("appearance.notifications", [&](Context owner) {
        take(scene.state->shell.notifications()->post(owner, {NotificationLevel::Info, "日志导出完成。", {}}));
        take(scene.state->shell.notifications()->post(owner, {NotificationLevel::Info, "已载入工作区配置。", {}}));
        return completedTask();
    }));
    auto instance = take(scene.state->kernel->mount(registration));
    PLATFORM_CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    bell->click();
    PLATFORM_CHECK(list->isVisible() && !empty->isVisible() && list->model()->rowCount() == 2);
    cycle([&](bool dark, int mode) { evidence.record("notice-populated", center, list->viewport(), dark, mode); });
    bell->click();
    const auto checkDialog = [&](const char* name, const char* fieldName, const QString& label) {
        const auto content = required<QWidget>(window, name);
        auto dialog = content->window();
        auto field = required<QAbstractScrollArea>(dialog, fieldName);
        PLATFORM_CHECK(dialog != window && field->isVisible());
        cycle([&](bool dark, int mode) { evidence.record(label, dialog, field->viewport(), dark, mode); });
        dialog->close();
        until([&] { return !dialog->isVisible(); });
    };
    take(scene.state->shell.showManagement());
    checkDialog("CordisManagement", "CordisManagementDetails", "management");
    required<QAction>(window, "CordisConfigurationFile")->trigger();
    checkDialog("CordisConfigurationPage", "CordisConfigEditor", "configuration");
    required<QAction>(window, "CordisAddPlugin")->trigger();
    checkDialog("CordisAddPluginDialog", "CordisAddPluginConfiguration", "add-instance");
    required<QAction>(window, "CordisOpenModules")->trigger();
    checkDialog("CordisModuleDiagnostics", "CordisModuleTable", "modules");
    PLATFORM_CHECK(evidence.captures.size() == 72);
    finishModules(scene);
}
} // namespace modules_view_test
