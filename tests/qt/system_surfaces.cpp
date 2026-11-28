/** @file system_surfaces.cpp
 * @brief 在同一个 Windows 工作台观察外部系统外观变化，只读桌面配置。
 */
#include <ElaApplication.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaWindow.h>
#include "preferences_p.hpp"
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTest>
#include <QTemporaryDir>
#include <QUuid>
#include <qt_windows.h>
#include <dwmapi.h>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
void require(bool value, const char* reason)
{
    if (!value) throw std::runtime_error(reason);
}

struct SurfaceState {
    bool highContrast;
    bool transparency;
    int backdrop;
};

constexpr auto personalizeKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

SurfaceState nativeState(HWND window, HKEY account = HKEY_CURRENT_USER)
{
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0), "SPI_GETHIGHCONTRAST");
    DWORD transparency = 1, size = sizeof(transparency);
    const auto result = RegGetValueW(account, personalizeKey,
                                     L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &size);
    require(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND, "read EnableTransparency");
    int backdrop = -1;
    // DWMWA_SYSTEMBACKDROP_TYPE, available starting in Windows 11 22H2.
    require(SUCCEEDED(DwmGetWindowAttribute(window, 38, &backdrop, sizeof(backdrop))), "query DWM backdrop");
    return {(contrast.dwFlags & HCF_HIGHCONTRASTON) != 0,
            result == ERROR_FILE_NOT_FOUND || transparency != 0, backdrop};
}
QByteArray pixelsDigest(const QPixmap& pixmap)
{
    require(!pixmap.isNull(), "missing selected pixmap");
    const auto image = pixmap.toImage().convertToFormat(QImage::Format_RGB32);
    QCryptographicHash digest(QCryptographicHash::Sha256);
    for (int y = 0; y < image.height(); ++y)
        digest.addData(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 4);
    return digest.result().toHex();
}

// Only this disposable test process gets a different predefined HKCU. Every write
// uses a key descended directly from the private hive handle.
class PrivateTransparencyHive {
public:
    void open()
    {
        require(files_.isValid(), "private hive directory");
        const auto path = QDir::toNativeSeparators(files_.filePath("settings.hiv")).toStdWString();
        require(RegOpenCurrentUser(KEY_READ, &realAccount_) == ERROR_SUCCESS, "open actual account read-only");
        require(RegLoadAppKeyW(path.c_str(), &root_, KEY_ALL_ACCESS, REG_PROCESS_APPKEY, 0) == ERROR_SUCCESS,
                "load private app hive");
        require(RegCreateKeyExW(root_, personalizeKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &personalize_, nullptr)
                == ERROR_SUCCESS, "create private Personalize key");
        const DWORD value = 1;
        require(RegSetValueExW(root_, marker_.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                               sizeof(value)) == ERROR_SUCCESS, "mark private hive");
        setTransparency(true);
        require(RegOverridePredefKey(HKEY_CURRENT_USER, root_) == ERROR_SUCCESS, "redirect test process HKCU");
        redirected_ = true;
        require(markerStatus() == ERROR_SUCCESS, "private hive marker must be readable through process HKCU");
    }
    void setTransparency(bool enabled)
    {
        const DWORD value = enabled ? 1 : 0;
        require(RegSetValueExW(personalize_, L"EnableTransparency", 0, REG_DWORD,
                               reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS,
                "set private transparency value");
    }
    HKEY actualAccount() const { return realAccount_; }
    void finish()
    {
        require(RegOverridePredefKey(HKEY_CURRENT_USER, nullptr) == ERROR_SUCCESS, "restore process HKCU");
        redirected_ = false;
        require(markerStatus() == ERROR_FILE_NOT_FOUND, "private marker still visible through restored process HKCU");
        closeHandles();
        require(files_.remove(), "remove temporary application hive");
    }
    ~PrivateTransparencyHive()
    {
        if (redirected_) RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        closeHandles();
    }
private:
    LSTATUS markerStatus() const
    {
        DWORD value = 0, size = sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER, nullptr, marker_.c_str(), RRF_RT_REG_DWORD, nullptr, &value, &size);
    }
    void closeHandles()
    {
        for (HKEY* key : {&personalize_, &root_, &realAccount_}) {
            if (*key) RegCloseKey(*key);
            *key = nullptr;
        }
    }
    QTemporaryDir files_;
    const std::wstring marker_ = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdWString();
    HKEY realAccount_ = nullptr, root_ = nullptr, personalize_ = nullptr;
    bool redirected_ = false;
};
} // namespace

bool acrylicAllowed(ElaWindow& window)
{
    const auto state = nativeState(reinterpret_cast<HWND>(window.winId()));
    return !state.highContrast && state.transparency;
}

void verifyAcrylicSurface(ElaWindow& window)
{
    require(eApp->getWindowDisplayMode() == ElaApplicationType::Acrylic, "Acrylic must be the effective Ela mode");
    constexpr int acrylicBackdrop = 3; // DWMSBT_TRANSIENTWINDOW
    require(nativeState(reinterpret_cast<HWND>(window.winId())).backdrop == acrylicBackdrop,
            "DWM must apply desktop Acrylic");
    require(window.palette().brush(QPalette::Window) == QBrush(Qt::transparent),
            "Qt window background must expose desktop Acrylic");
}

void observeSystemSurfaces(ElaWindow& window, const QString& settingsPath, const QString& imagePath,
                           const QString& isolatedRestriction)
{
    const bool privateHive = isolatedRestriction == "private-hive";
    const bool isolated = !isolatedRestriction.isEmpty() && !privateHive;
    const bool automated = privateHive || isolated;
    const auto kind = privateHive ? QStringLiteral("transparency") :
                      isolated ? isolatedRestriction : qEnvironmentVariable("EVE_SURFACE_KIND");
    require(kind == "contrast" || kind == "transparency", "set EVE_SURFACE_KIND to contrast or transparency");
    QTemporaryDir temporary;
    const auto destination = qEnvironmentVariable("EVE_SURFACE_EVIDENCE", automated ? temporary.path() : QString());
    require(!destination.isEmpty() && QDir().mkpath(destination), "set EVE_SURFACE_EVIDENCE to a new directory");
    const QDir directory(destination);
    require(directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(), "evidence directory must be empty");
    const bool baselineOnly = !automated && qEnvironmentVariable("EVE_SURFACE_BASELINE_ONLY") == "1";
    const auto material = window.findChild<QComboBox*>("EveSettingsMaterial");
    const auto hint = window.findChild<ElaText*>("EveSettingsMaterialHint");
    const auto layer = window.findChild<QWidget*>("EveWorkbenchWallpaper", Qt::FindDirectChildrenOnly);
    require(material && hint && layer, "expected settings material, hint and wallpaper layer");
    const auto hwnd = reinterpret_cast<HWND>(window.winId());
    const auto digest = pixelsDigest(window.getWindowPixmap(ElaThemeType::Light));
    const auto initial = nativeState(hwnd);
    eve::ui::detail::WorkbenchPreferences* preferences = nullptr;
    // The restored phase requires DWM to accept Mica under the real desktop policy.
    if (automated && (initial.highContrast || !initial.transparency)) {
        std::cout << "SKIP: desktop currently requires solid surfaces" << std::endl;
        return;
    }
    if (isolated) {
        for (auto child : window.children())
            if (auto value = dynamic_cast<eve::ui::detail::WorkbenchPreferences*>(child)) preferences = value;
        require(preferences, "expected workbench-owned preferences");
    }
    std::optional<PrivateTransparencyHive> hive;
    if (privateHive) hive.emplace().open();
    const auto actualAccount = hive ? hive->actualAccount() : HKEY_CURRENT_USER;
    auto capture = [&](const QString& phase, bool restricted) {
        const eve::ui::detail::SystemSurfaceSettings policy{restricted && kind == "contrast",
                                                               !(restricted && kind == "transparency")};
        if (isolated) preferences->applySystemSurfaceSettings(policy);
        if (hive) {
            // Flush the prior phase's queued refresh before altering its input.
            QTest::qWait(30);
            hive->setTransparency(!restricted);
            require(nativeState(hwnd).transparency == !restricted, "redirected account input");
            if (phase != "baseline") {
                QTest::qWait(30);
                require(window.property("EveWallpaperVisible").toBool() == restricted,
                        "window state must still reflect previous phase before WM_SETTINGCHANGE");
            }
            DWORD_PTR response = 0;
            require(SendMessageTimeoutW(hwnd, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"),
                                        SMTO_ABORTIFHUNG, 2000, &response) != 0, "notify own workbench HWND");
        }
        QElapsedTimer timer;
        timer.start();
        auto state = nativeState(hwnd, actualAccount);
        const auto matches = [&] {
            state = nativeState(hwnd, actualAccount);
            const bool osMatches = restricted
                ? (kind == "contrast" ? state.highContrast : !state.highContrast && !state.transparency)
                : !state.highContrast && state.transparency;
            // Ela releases Mica by resetting DWM to AUTO. NONE is also an explicit solid fallback.
            const bool effective = restricted
                ? eApp->getWindowDisplayMode() == ElaApplicationType::Normal && (state.backdrop == 0 || state.backdrop == 1)
                : eApp->getWindowDisplayMode() == ElaApplicationType::Mica && state.backdrop == 2;
            const bool osUnchanged = state.highContrast == initial.highContrast && state.transparency == initial.transparency;
            return (automated ? osUnchanged : osMatches) && effective && window.property("EveWallpaperVisible").toBool() == !restricted &&
                   layer->isVisible() == !restricted && window.property("EveRequestedMaterialAccepted").toBool() == !restricted;
        };
        while (!matches() && timer.elapsed() < (automated || phase == "baseline" ? 5000 : 180000)) QTest::qWait(50);
        const bool observed = matches();
        const auto restrictionText = kind == "contrast" ? QStringLiteral("contrast theme") : QStringLiteral("transparency is off");
        const auto waitForHint = [&] {
            QSize previous;
            QElapsedTimer stable;
            return QTest::qWaitFor([&] {
                const bool correct = restricted ? hint->text().contains(restrictionText)
                    : !hint->text().contains(QStringLiteral("Using a solid background"));
                if (hint->size() != previous) {
                    previous = hint->size();
                    stable.start();
                    return false;
                }
                return correct && stable.isValid() && stable.elapsed() >= 40 && hint->width() > 0 &&
                       hint->height() >= hint->heightForWidth(hint->width());
            }, 1500);
        };
        const bool correctHint = waitForHint();
        // Capture only this widget tree, even when Windows Settings covers the HWND.
        const auto screenshot = (observed ? phase : phase + "-timeout") + ".png";
        require(window.grab().save(directory.filePath(screenshot)), "save settings window screenshot");
        QFile file(settingsPath);
        require(file.open(QIODevice::ReadOnly), "read isolated Eve preferences");
        const auto values = QJsonDocument::fromJson(file.readAll()).object();
        const auto currentDigest = pixelsDigest(window.getWindowPixmap(ElaThemeType::Light));
        const bool choices = material->currentIndex() == ElaApplicationType::Mica &&
                             values.value("workbench.windowMaterial").toString() == "mica" &&
                             values.value("workbench.wallpaper").toString() == imagePath && currentDigest == digest;
        QJsonObject result{
            {"observed", observed && choices && correctHint}, {"phase", phase}, {"restriction", kind},
            {"observationMode", privateHive ? "private-app-hive-native-message" :
                                isolated ? "isolated-application-policy" : "external-windows-settings"},
            {"baselineOnly", baselineOnly}, {"timestampUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"hwnd", QString::number(reinterpret_cast<quintptr>(hwnd))},
            {"highContrast", state.highContrast}, {"transparency", state.transparency}, {"dwmBackdrop", state.backdrop},
            {"selectedMaterial", material->currentIndex()}, {"effectiveElaMaterial", static_cast<int>(eApp->getWindowDisplayMode())},
            {"requestedMaterialAccepted", window.property("EveRequestedMaterialAccepted").toBool()},
            {"wallpaperLayerVisible", layer->isVisible()}, {"storedWallpaperSha256", QString::fromLatin1(currentDigest)},
            {"wallpaperPreferenceMatches", values.value("workbench.wallpaper").toString() == imagePath},
            {"savedMaterial", values.value("workbench.windowMaterial")}, {"settingsHint", hint->text()},
            {"hintWidth", hint->width()}, {"hintHeight", hint->height()},
            {"hintHeightForWidth", hint->heightForWidth(hint->width())},
            {"screenshot", screenshot}, {"screenshotSource", "Qt window grab, DWM attribute read separately"}};
        if (isolated) {
            result.insert("policyHighContrast", policy.highContrast);
            result.insert("policyTransparencyEnabled", policy.transparencyEnabled);
            if (restricted && observed && correctHint) {
                const auto wide = window.size();
                window.resize(700, 620);
                require(waitForHint(), "narrow workbench clipped Windows appearance reason");
                result.insert("narrowHintHeight", hint->height());
                result.insert("narrowHintHeightForWidth", hint->heightForWidth(hint->width()));
                require(window.grab().save(directory.filePath("restricted-narrow.png")), "capture narrow material explanation");
                window.resize(wide);
            }
        }
        if (hive) result.insert("redirectedAccountTransparency", nativeState(hwnd).transparency);
        const auto name = observed && choices && correctHint ? phase : phase + "-failed";
        const auto bytes = QJsonDocument(result).toJson();
        QSaveFile output(directory.filePath(name + ".json"));
        require(output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit(),
                "save phase measurements");
        std::cout << name.toStdString() << std::endl;
        require(observed && choices && correctHint, "surface phase did not match, see phase measurements");
    };
    capture("baseline", false);
    if (baselineOnly) return;
    capture("restricted", true);
    capture("restored", false);
    if (hive) {
        hive->finish();
        const auto finalState = nativeState(hwnd);
        require(finalState.transparency == initial.transparency && finalState.highContrast == initial.highContrast,
                "actual account must match the original settings after remapping");
        std::cout << "process HKCU restored, private marker absent and hive files removed" << std::endl;
    }
}
