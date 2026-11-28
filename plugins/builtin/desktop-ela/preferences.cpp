/** @file preferences.cpp
 * @brief 在独立 JSON 中保存 Ela 主题及工作台布局，失败不影响插件运行与关闭屏障。
 */
#include "preferences_p.hpp"
#include "theme_palette_p.hpp"
#include "workbench_dialog_p.hpp"
#include <ElaApplication.h>
#include <ElaDialog.h>
#include <ElaTheme.h>
#include <ElaWindow.h>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QImageReader>
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
#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::ui::detail {
namespace {
constexpr int minimumMainPaneHeight = 240;
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

}
WorkbenchPreferences::WorkbenchPreferences(ElaWindow* window, QTabWidget* panel, QString file)
    : QObject(window), window_(window), panel_(panel),
      splitter_(window->findChild<QSplitter*>("ElaWorkbenchPanelSplitter")), file_(std::move(file))
{
    timer_.setSingleShot(true);
    timer_.setInterval(250);
    wallpaper_ = new WallpaperLayer(window);
#ifdef Q_OS_WIN
    new SystemAppearanceObserver(window, this, [this] { applyAppearance(); });
#endif
    connect(&timer_, &QTimer::timeout, this, [this] { flush(); });
    applyMotion();
    load();
    panel->hide();
    panel->installEventFilter(this);
    window->installEventFilter(this);
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { wallpaper_->update(); changed(); });
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
void WorkbenchPreferences::report(const QString& text)
{
    message_ = text;
    auto callback = reporter_;
    if (callback)
        callback(text);
}
void WorkbenchPreferences::load()
{
    if (file_.isEmpty()) {
        report(QString::fromUtf8("当前为会话模式，界面设置在关闭后不保留。"));
        return;
    }
    report(QString::fromUtf8("外观与布局自动保存在本机；底部工作区启动时保持收起。"));
    QFile file(file_);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) {
        report(QString::fromUtf8("无法读取界面偏好，已保留默认布局。修改设置后可重新保存。"));
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto value = document.object();
    const auto integer = [&value](const char* key, int low, int high) {
        const auto item = value.value(QString::fromLatin1(key));
        const auto number = item.toDouble(-1);
        return item.isDouble() && std::isfinite(number) && std::floor(number) == number && number >= low &&
               number <= high;
    };
    if (error.error != QJsonParseError::NoError || !document.isObject() || !integer("version", 1, 1) ||
        !integer("theme", 0, 1) || !integer("navigationMode", 2, 3) ||
        !integer("navigationWidth", navigationWidthPixels.minimum, navigationWidthPixels.maximum) ||
        !integer("panelHeight", panelHeightPixels.minimum, panelHeightPixels.maximum)) {
        report(QString::fromUtf8("界面偏好格式或版本无效，已保留默认布局。修改设置后可重新保存。"));
        return;
    }
    eTheme->setThemeMode(value.value("theme").toInt() ? ElaThemeType::Dark : ElaThemeType::Light);
    window_->setNavigationBarWidth(value.value("navigationWidth").toInt());
    window_->setNavigationBarDisplayMode(
        static_cast<ElaNavigationType::NavigationDisplayMode>(value.value("navigationMode").toInt()));
    panelHeight_ = value.value("panelHeight").toInt();
    material_ = integer("material", 0, 5) ? value.value("material").toInt() : 0;
    transition_ = integer("transition", 0, 4) ? value.value("transition").toInt() : 0;
    setUserCardVisible(value.value("userCardVisible").toBool(true));
    window_->setProperty("EveDoNotDisturb", value.value("doNotDisturb").toBool());
    window_->setProperty("EveOutputSource", value.value("outputSource").toString());
    const auto path = value.value("background").toString();
    if (!path.isEmpty() && (!QDir::isAbsolutePath(path) || !setBackground(path)))
        report(QString::fromUtf8("背景图片无法读取，已使用默认背景。"));
    applyAppearance();
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
    if (appearanceObserver_) appearanceObserver_();
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
    QImage image;
    if (!path.isEmpty()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const auto format = reader.format().toLower();
        if (format != "png" && format != "jpeg" && format != "webp") return false;
        const auto size = reader.size();
        if (!size.isValid()) return false;
        if (std::max(size.width(), size.height()) > 4096)
            reader.setScaledSize(size.scaled(4096, 4096, Qt::KeepAspectRatio));
        image = reader.read();
        if (image.isNull()) return false;
    }
    window_->setWindowPaintMode(ElaWindowType::Normal);
    background_ = QPixmap::fromImage(image);
    backgroundPath_ = path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
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
    applyAppearance();
    changed();
    return true;
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
    if (system.highContrast) appearanceHint_ = tr("Windows 已启用对比度主题，当前使用实色。");
    else if (!system.transparencyEnabled) appearanceHint_ = tr("Windows 已关闭透明效果，当前使用实色。");
    else if (!supported) appearanceHint_ = tr("系统未应用所选材质，当前使用标准外观。");
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
    if (appearanceObserver_) appearanceObserver_();
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
bool WorkbenchPreferences::flush()
{
    timer_.stop();
    if (!dirty_ || file_.isEmpty() || !window_)
        return true;
    const auto mode = window_->getNavigationBarDisplayMode() == ElaNavigationType::Maximal ? ElaNavigationType::Maximal
                                                                                           : ElaNavigationType::Compact;
    QJsonObject value{{"version", 1},
                            {"theme", eTheme->getThemeMode() == ElaThemeType::Dark ? 1 : 0},
                            {"navigationMode", static_cast<int>(mode)},
                            {"navigationWidth", std::clamp(window_->getNavigationBarWidth(),
                                                           navigationWidthPixels.minimum, navigationWidthPixels.maximum)},
                            {"panelHeight", panelHeight_}};
    if (material_) value.insert("material", material_);
    if (transition_) value.insert("transition", transition_);
    if (!userCard_) value.insert("userCardVisible", false);
    if (!backgroundPath_.isEmpty()) value.insert("background", backgroundPath_);
    if (window_->property("EveDoNotDisturb").toBool()) value.insert("doNotDisturb", true);
    const auto source = window_->property("EveOutputSource").toString();
    if (!source.isEmpty()) value.insert("outputSource", source);
    QSaveFile file(file_);
    file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
    if (!QDir().mkpath(QFileInfo(file_).absolutePath()) || !file.open(QIODevice::WriteOnly) ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        report(QString::fromUtf8("界面偏好保存失败；本次设置仍然有效，下次修改时将重试。"));
        return false;
    }
    dirty_ = false;
    report(QString::fromUtf8("界面偏好已保存；底部工作区启动时保持收起。"));
    return true;
}
void WorkbenchPreferences::reset()
{
    if (!window_)
        return;
    eTheme->setThemeMode(ElaThemeType::Light);
    material_ = transition_ = 0;
    setUserCardVisible(true);
    setBackground({});
    window_->setNavigationBarWidth(navigationWidthPixels.initial);
    window_->setNavigationBarDisplayMode(ElaNavigationType::Compact);
    setPanelHeight(panelHeightPixels.initial);
    changed();
}
} // namespace eve::ui::detail
