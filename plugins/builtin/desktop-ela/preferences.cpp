/** @file preferences.cpp
 * @brief 在独立 JSON 中保存 Ela 主题及工作台布局，失败不影响插件运行与关闭屏障。
 */
#include "preferences_p.hpp"
#include "desktop_text.hpp"
#include "theme_palette_p.hpp"
#include "workbench_dialog_p.hpp"
#include <ElaApplication.h>
#include <ElaDialog.h>
#include <ElaTheme.h>
#include <ElaWindow.h>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QImageReader>
#include <QLockFile>
#include <QPainter>
#include <QSettings>
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <QAbstractNativeEventFilter>
#endif
#include <QSaveFile>
#include <QSplitter>
#include <QTabWidget>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::ui::detail {
namespace {
constexpr int minimumMainPaneHeight = 240;
constexpr int maximumSettingsBytes = 16384;
const QStringList materialNames{"normal", "elaMica", "mica", "micaAlt", "acrylic", "dwmBlur"};
const QStringList transitionNames{"none", "popup", "scale", "flip", "blur"};

struct SettingsDocument {
    bool exists = false;
    QByteArray bytes;
    QJsonObject value;
    QString problem;
};

bool validSettings(const QJsonObject& value)
{
    const auto integer = [&](const char* key, int min, int max) {
        const auto entry = value.value(QLatin1String(key));
        if (entry.isUndefined()) return true;
        const auto number = entry.toDouble();
        return entry.isDouble() && std::isfinite(number) && std::floor(number) == number && number >= min && number <= max;
    };
    const auto choice = [&](const char* key, const QStringList& options) {
        const auto entry = value.value(QLatin1String(key));
        return entry.isUndefined() || options.contains(entry.toString());
    };
    for (const auto* key : {"workbench.showUserCard", "notifications.doNotDisturb"}) {
        const auto entry = value.value(QLatin1String(key));
        if (!entry.isUndefined() && !entry.isBool()) return false;
    }
    for (const auto* key : {"workbench.wallpaper", "output.source"}) {
        const auto entry = value.value(QLatin1String(key));
        if (!entry.isUndefined() && !entry.isString()) return false;
    }
    const auto wallpaper = value.value("workbench.wallpaper").toString();
    return (wallpaper.isEmpty() || QDir::isAbsolutePath(wallpaper)) &&
           choice("workbench.theme", {"light", "dark"}) && choice("workbench.navigationMode", {"compact", "expanded"}) &&
           integer("workbench.navigationWidth", navigationWidthPixels.minimum, navigationWidthPixels.maximum) &&
           integer("workbench.panelHeight", panelHeightPixels.minimum, panelHeightPixels.maximum) &&
           choice("workbench.windowMaterial", materialNames) && choice("workbench.pageTransition", transitionNames);
}

SettingsDocument readSettings(const QString& path)
{
    SettingsDocument result;
    QFile file(path);
    if (path.isEmpty() || !file.exists()) return result;
    result.exists = true;
    if (!file.open(QIODevice::ReadOnly)) {
        result.problem = desktopText("settings.couldNotReadTheUserSettingsFile");
        return result;
    }
    result.bytes = file.read(maximumSettingsBytes + 1);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(result.bytes, &error);
    if (file.error() != QFile::NoError || result.bytes.size() > maximumSettingsBytes ||
        error.error != QJsonParseError::NoError || !document.isObject() || !validSettings(document.object())) {
        result.problem = desktopText("settings.invalidUserSettingsJSONOrValuesFix");
        return result;
    }
    result.value = document.object();
    return result;
}

bool writeSettings(const QString& path, const QJsonObject& value, const SettingsDocument& expected)
{
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
    if (bytes.size() > maximumSettingsBytes || !QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
    const auto current = readSettings(path);
    if (!current.problem.isEmpty() || current.exists != expected.exists || current.bytes != expected.bytes)
        return false;
    return file.commit();
}

QString settingsReady()
{
    return desktopText("settings.changesAreSavedToTheUserSettings");
}

#ifdef Q_OS_WIN
class SystemAppearanceObserver final : public QObject, public QAbstractNativeEventFilter {
public:
    SystemAppearanceObserver(QWidget* window, QObject* owner, std::function<void()> refresh)
        : QObject(owner), window_(window), refresh_(std::move(refresh))
    {
        qApp->installNativeEventFilter(this);
    }
    ~SystemAppearanceObserver() override { qApp->removeNativeEventFilter(this); }
    bool nativeEventFilter(const QByteArray&, void* message, long*) override
    {
        const auto* event = static_cast<MSG*>(message);
        if (event->message == WM_SETTINGCHANGE && window_ &&
            event->hwnd == reinterpret_cast<HWND>(window_->effectiveWinId()))
            QTimer::singleShot(0, this, refresh_);
        return false;
    }
private:
    QPointer<QWidget> window_;
    std::function<void()> refresh_;
};
#endif
void paintWallpaper(QWidget* surface, ElaWindow* owner, qreal opacity)
{
    if (!owner || !owner->property("EveWallpaperVisible").toBool() || surface->size().isEmpty()) return;
    const auto& picture = owner->getWindowPixmap(eTheme->getThemeMode());
    if (picture.isNull()) return;
    auto crop = surface->size();
    crop.scale(picture.size(), Qt::KeepAspectRatio);
    QRect source(QPoint(),crop);
    source.moveCenter(picture.rect().center());
    QPainter painter(surface);
    painter.setOpacity(opacity);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(surface->rect(), picture, source);
}
class WorkbenchDialog final : public ElaDialog {
public:
    explicit WorkbenchDialog(QWidget* parent) : ElaDialog(parent)
    {
        bindWorkbenchWindowPalette(this, ElaThemeType::DialogBase);
        for (auto host = parent; host && !owner_; host = host->parentWidget())
            owner_ = qobject_cast<ElaWindow*>(host);
    }
protected:
    void paintEvent(QPaintEvent* event) override
    {
        ElaDialog::paintEvent(event);
        const auto display = eApp->getWindowDisplayMode();
        if (display != ElaApplicationType::Normal && display != ElaApplicationType::ElaMica &&
            !property("ElaWindowMaterialApplied").toBool()) {
            QPainter painter(this);
            painter.fillRect(rect(), palette().color(QPalette::Base));
        }
        paintWallpaper(this, owner_, .75);
    }
private:
    QPointer<ElaWindow> owner_;
};
class WallpaperLayer final : public QWidget {
public:
    explicit WallpaperLayer(ElaWindow* owner) : QWidget(owner), owner_(owner)
    {
        setObjectName("EveWorkbenchWallpaper");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        hide();
        lower();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        paintWallpaper(this, owner_, .70);
    }
private:
    ElaWindow* owner_;
};
SystemSurfaceSettings readSystemSurfaces()
{
    SystemSurfaceSettings result;
#ifdef Q_OS_WIN
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0))
        result.highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    const QSettings theme(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), QSettings::NativeFormat);
    result.transparencyEnabled = theme.value("EnableTransparency", 1).toInt() != 0;
#endif
    return result;
}
bool animationsEnabled()
{
#ifdef Q_OS_WIN
    BOOL enabled = TRUE;
    return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) || enabled;
#else
    return true;
#endif
}

QImage readBackground(const QString& path)
{
    if (path.isEmpty()) return {};
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto format = reader.format().toLower();
    if (format != "png" && format != "jpeg" && format != "webp") return {};
    const auto size = reader.size();
    if (!size.isValid()) return {};
    if (std::max(size.width(), size.height()) > 4096)
        reader.setScaledSize(size.scaled(4096, 4096, Qt::KeepAspectRatio));
    return reader.read();
}

}
WorkbenchPreferences::WorkbenchPreferences(ElaWindow* window, QTabWidget* panel, QString file)
    : QObject(window), window_(window), panel_(panel),
      splitter_(window->findChild<QSplitter*>("ElaWorkbenchPanelSplitter")),
      file_(std::move(file))
{
    timer_.setSingleShot(true);
    timer_.setInterval(250);
    reloadTimer_.setSingleShot(true);
    reloadTimer_.setInterval(100);
    const auto queueReload = [this] {
        if (!reloadTimer_.isActive()) reloadTimer_.start();
    };
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, queueReload);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, queueReload);
    connect(&reloadTimer_, &QTimer::timeout, this, [this] {
        if (fileGuard_) fileGuard_([this] { load(); });
    });
    wallpaper_ = new WallpaperLayer(window);
#ifdef Q_OS_WIN
    new SystemAppearanceObserver(window, this, [this] { applyAppearance(); });
#endif
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (fileGuard_) fileGuard_([this] { flush(); });
    });
    applyMotion();
    if (file_.isEmpty()) {
        darkTheme_ = eTheme->getThemeMode() == ElaThemeType::Dark;
        report(desktopText("settings.settingsForThisWindowAreKeptFor"));
    } else {
        baseline_ = snapshot();
        load();
    }
    panel->hide();
    panel->installEventFilter(this);
    window->installEventFilter(this);
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this](ElaThemeType::ThemeMode mode) {
        wallpaper_->update();
        if (file_.isEmpty()) {
            darkTheme_ = mode == ElaThemeType::Dark;
            notifyControls();
        }
    });
    connect(window, &ElaWindow::pNavigationBarWidthChanged, this, [this] { changed(); });
    connect(window, &ElaWindow::pNavigationBarDisplayModeChanged, this, [this] { changed(); });
    if (splitter_)
        connect(splitter_, &QSplitter::splitterMoved, this, [this] {
            if (!applyingHeight_ && panel_ && panel_->isVisible()) {
                panelHeight_ = std::clamp(panel_->height(), panelHeightPixels.minimum, panelHeightPixels.maximum);
                changed();
            }
        });
    restoring_ = false;
}
ElaDialog* createWorkbenchDialog(QWidget* parent)
{
    return new WorkbenchDialog(parent);
}
void WorkbenchPreferences::setReporter(std::function<void(const QString&)> reporter)
{
    reporter_ = std::move(reporter);
    report(message_);
}
void WorkbenchPreferences::setFileGuard(std::function<void(const std::function<void()>&)> guard)
{
    fileGuard_ = std::move(guard);
}
void WorkbenchPreferences::report(const QString& text)
{
    message_ = text;
    auto callback = reporter_;
    if (callback)
        callback(text);
}
void WorkbenchPreferences::load()
{
    watchFiles();
    const auto document = readSettings(file_);
    if (hasObservation_ && observedWithFile_ == document.exists && observed_ == document.bytes &&
        document.problem.isEmpty()) return;
    if (!document.problem.isEmpty()) {
        hasObservation_ = false;
        report(document.problem);
        return;
    }
    if (dirty_) {
        flush();
        return;
    }
    observed_ = document.bytes;
    observedWithFile_ = document.exists;
    hasObservation_ = true;
    apply(document.value);
}

void WorkbenchPreferences::watchFiles()
{
    if (file_.isEmpty()) return;
    const QFileInfo info(file_);
    const auto file = info.isFile() ? info.absoluteFilePath() : QString{};
    auto directory = info.absoluteDir();
    while (!directory.exists() && !directory.isRoot())
        directory.setPath(QDir::cleanPath(directory.absoluteFilePath(QStringLiteral(".."))));
    const auto parent = directory.exists() ? directory.absolutePath() : QString{};
    for (const auto& path : watcher_.files() + watcher_.directories()) {
        if (path != file && path != parent) watcher_.removePath(path);
    }
    for (const auto& path : {file, parent}) {
        if (path.isEmpty()) continue;
        if (!watcher_.files().contains(path) && !watcher_.directories().contains(path)) watcher_.addPath(path);
    }
}

void WorkbenchPreferences::apply(const QJsonObject& value)
{
    restoring_ = true;
    setDarkTheme(value.value("workbench.theme").toString() == "dark");
    window_->setNavigationBarWidth(value.value("workbench.navigationWidth").toInt(navigationWidthPixels.initial));
    window_->setNavigationBarDisplayMode(
        value.value("workbench.navigationMode").toString() == "expanded" ? ElaNavigationType::Maximal : ElaNavigationType::Compact);
    const auto previousHeight = panelHeight_;
    panelHeight_ = value.value("workbench.panelHeight").toInt(panelHeightPixels.initial);
    material_ = std::max(0, materialNames.indexOf(value.value("workbench.windowMaterial").toString()));
    transition_ = std::max(0, transitionNames.indexOf(value.value("workbench.pageTransition").toString()));
    setUserCardVisible(value.value("workbench.showUserCard").toBool(true));
    window_->setProperty("EveDoNotDisturb", value.value("notifications.doNotDisturb").toBool());
    window_->setProperty("EveOutputSource", value.value("output.source").toString());
    const auto path = value.value("workbench.wallpaper").toString();
    if (path != backgroundPath_ || (!path.isEmpty() && background_.isNull()))
        applyBackground(path, readBackground(path));
    applyAppearance();
    if (panelHeight_ != previousHeight) applyHeight();
    baseline_ = snapshot();
    restoring_ = false;
    notifyControls();
    report(path.isEmpty() || !background_.isNull() ? settingsReady()
                                                   : desktopText("settings.couldNotLoadTheBackgroundImageThe"));
}
void WorkbenchPreferences::setDarkTheme(bool dark)
{
    if (darkTheme_ != dark) {
        darkTheme_ = dark;
        changed();
    }
    eTheme->setThemeMode(dark ? ElaThemeType::Dark : ElaThemeType::Light);
    notifyControls();
}
void WorkbenchPreferences::setMaterial(int mode)
{
    material_ = std::clamp(mode, 0, 5);
    applyAppearance();
    changed();
}
void WorkbenchPreferences::observeAppearance(std::function<void()> observer)
{
    appearanceObserver_ = std::move(observer);
    notifyControls();
}

void WorkbenchPreferences::notifyControls()
{
    if (!restoring_ && appearanceObserver_) appearanceObserver_();
}
void WorkbenchPreferences::setTransition(int mode)
{
    transition_ = std::clamp(mode, 0, 4);
    applyMotion();
    changed();
}
void WorkbenchPreferences::applyMotion()
{
    const bool animate = animationsEnabled();
    window_->setProperty("EveReducedMotion", !animate);
    window_->setStackSwitchMode(static_cast<ElaWindowType::StackSwitchMode>(animate ? transition_ : 0));
}
void WorkbenchPreferences::setUserCardVisible(bool visible)
{
    userCard_ = visible;
    window_->setUserInfoCardVisible(visible);
    changed();
}
bool WorkbenchPreferences::setBackground(const QString& path)
{
    const auto image = readBackground(path);
    if (!path.isEmpty() && image.isNull()) return false;
    applyBackground(path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath(), image);
    if (!restoring_) applyAppearance();
    changed();
    return true;
}

void WorkbenchPreferences::applyBackground(const QString& path, const QImage& image)
{
    window_->setWindowPaintMode(ElaWindowType::Normal);
    background_ = QPixmap::fromImage(image);
    backgroundPath_ = path;
    for (auto theme : {ElaThemeType::Light, ElaThemeType::Dark}) {
        auto rendered = background_;
        if (!rendered.isNull()) {
            QPainter painter(&rendered);
            auto tint = eTheme->getThemeColor(theme, ElaThemeType::WindowBase);
            tint.setAlpha(155);
            painter.fillRect(rendered.rect(), tint);
        }
        window_->setWindowPixmap(theme, rendered);
    }
}
void WorkbenchPreferences::applyAppearance()
{
    applySystemSurfaceSettings(readSystemSurfaces());
    applyMotion();
}
void WorkbenchPreferences::applySystemSurfaceSettings(SystemSurfaceSettings system)
{
    const bool solid = system.highContrast || !system.transparencyEnabled;
    const int requested = solid ? 0 : material_;
    eApp->setWindowDisplayMode(static_cast<ElaApplicationType::WindowDisplayMode>(requested));
    const bool supported = requested <= 1 || window_->property("ElaWindowMaterialApplied").toBool();
    window_->setProperty("EveRequestedMaterialAccepted", supported && requested == material_);
    if (!supported) eApp->setWindowDisplayMode(ElaApplicationType::Normal);
    if (system.highContrast) appearanceHint_ = desktopText("settings.aWindowsContrastThemeIsActiveUsing");
    else if (!system.transparencyEnabled) appearanceHint_ = desktopText("settings.windowsTransparencyIsOffUsingASolid");
    else if (!supported) appearanceHint_ = desktopText("settings.windowsDidNotApplyTheSelectedMaterial");
    else appearanceHint_.clear();
    const bool image = !solid && !background_.isNull();
    const bool normal = eApp->getWindowDisplayMode() == ElaApplicationType::Normal;
    window_->setProperty("EveWallpaperVisible", image);
    window_->setWindowPaintMode(image && normal ? ElaWindowType::Pixmap : ElaWindowType::Normal);
    wallpaper_->setGeometry(window_->rect());
    wallpaper_->setVisible(image && !normal);
    wallpaper_->lower();
    wallpaper_->update();
    for (auto tool : window_->findChildren<ElaDialog*>()) {
        if (dynamic_cast<WorkbenchDialog*>(tool)) tool->update();
    }
    notifyControls();
}
void WorkbenchPreferences::changed()
{
    if (restoring_ || file_.isEmpty())
        return;
    dirty_ = true;
    timer_.start();
}
void WorkbenchPreferences::setPanelHeight(int height)
{
    panelHeight_ = std::clamp(height, panelHeightPixels.minimum, panelHeightPixels.maximum);
    applyHeight();
    changed();
}
void WorkbenchPreferences::applyHeight()
{
    if (!splitter_ || !panel_ || panel_->isHidden() || applyingHeight_)
        return;
    applyingHeight_ = true;
    const auto total = splitter_->height();
    const auto desired = std::min(panelHeight_, std::max(panelHeightPixels.minimum, total - minimumMainPaneHeight));
    splitter_->setSizes({std::max(minimumMainPaneHeight, total - desired), desired});
    applyingHeight_ = false;
}
bool WorkbenchPreferences::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window_) {
        if (event->type() == QEvent::Resize) wallpaper_->setGeometry(window_->rect());
        if (event->type() == QEvent::ThemeChange) applyAppearance();
        if (event->type() == QEvent::DynamicPropertyChange) {
            const auto key = static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName();
            if (key == "EveDoNotDisturb" || key == "EveOutputSource") changed();
        }
    }
    if ((watched == panel_ && event->type() == QEvent::Show) || (watched == window_ && event->type() == QEvent::Resize))
        applyHeight();
    return QObject::eventFilter(watched, event);
}
QJsonObject WorkbenchPreferences::snapshot() const
{
    QJsonObject value;
    if (darkTheme_) value.insert("workbench.theme", "dark");
    if (window_->getNavigationBarDisplayMode() == ElaNavigationType::Maximal)
        value.insert("workbench.navigationMode", "expanded");
    const int width = std::clamp(window_->getNavigationBarWidth(), navigationWidthPixels.minimum,
                                 navigationWidthPixels.maximum);
    if (width != navigationWidthPixels.initial) value.insert("workbench.navigationWidth", width);
    if (panelHeight_ != panelHeightPixels.initial) value.insert("workbench.panelHeight", panelHeight_);
    if (material_) value.insert("workbench.windowMaterial", materialNames.at(material_));
    if (transition_) value.insert("workbench.pageTransition", transitionNames.at(transition_));
    if (!userCard_) value.insert("workbench.showUserCard", false);
    if (!backgroundPath_.isEmpty()) value.insert("workbench.wallpaper", backgroundPath_);
    if (window_->property("EveDoNotDisturb").toBool()) value.insert("notifications.doNotDisturb", true);
    const auto source = window_->property("EveOutputSource").toString();
    if (!source.isEmpty()) value.insert("output.source", source);
    return value;
}

bool WorkbenchPreferences::flush(bool updateView)
{
    timer_.stop();
    if (!dirty_ || file_.isEmpty() || !window_) return true;
    const auto disk = readSettings(file_);
    if (!disk.problem.isEmpty()) {
        hasObservation_ = false;
        report(disk.problem);
        return false;
    }
    const bool externalChange = !hasObservation_ || observedWithFile_ != disk.exists || observed_ != disk.bytes;
    const auto current = snapshot();
    auto keys = baseline_.keys() + current.keys();
    keys.removeDuplicates();
    auto combined = disk.value;
    for (const auto& key : keys) {
        const auto value = current.value(key);
        if (value != baseline_.value(key)) {
            if (value.isUndefined()) combined.remove(key);
            else combined.insert(key, value);
        }
    }
    const bool changedFile = combined != disk.value;
    if (changedFile && !writeSettings(file_, combined, disk)) {
        report(desktopText("settings.couldNotSaveUserSettingsTheFile"));
        return false;
    }
    dirty_ = false;
    if (updateView) {
        hasObservation_ = true;
        observed_ = changedFile ? QJsonDocument(combined).toJson(QJsonDocument::Indented) : disk.bytes;
        observedWithFile_ = changedFile || disk.exists;
        if (changedFile) watchFiles();
        if (externalChange) {
            apply(combined);
        } else {
            baseline_ = current;
            report(settingsReady());
        }
    }
    return true;
}

bool WorkbenchPreferences::close()
{
    watcher_.blockSignals(true);
    reloadTimer_.stop();
    return flush(false);
}

void WorkbenchPreferences::openFile()
{
    if (file_.isEmpty()) return;
    flush();
    const auto disk = readSettings(file_);
    if (!disk.exists && !writeSettings(file_, {}, disk)) {
        report(desktopText("settings.couldNotCreateTheUserSettingsFile").arg(QDir::toNativeSeparators(file_)));
        return;
    }
    watchFiles();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file_).absoluteFilePath()))) {
        report(desktopText("settings.openTheUserSettingsFileInAn").arg(QDir::toNativeSeparators(file_)));
    }
}
void WorkbenchPreferences::reset()
{
    if (!window_)
        return;
    setDarkTheme(false);
    material_ = transition_ = 0;
    setUserCardVisible(true);
    setBackground({});
    window_->setNavigationBarWidth(navigationWidthPixels.initial);
    window_->setNavigationBarDisplayMode(ElaNavigationType::Compact);
    setPanelHeight(panelHeightPixels.initial);
    notifyControls();
    changed();
}
} // namespace eve::ui::detail
