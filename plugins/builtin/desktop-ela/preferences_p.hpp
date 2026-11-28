/** @file preferences_p.hpp
 * @brief 宿主界面偏好独立保存，不接触插件配置、实例身份或页面工厂。
 */
#pragma once
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <functional>
class ElaWindow;
class QImage;
class QTabWidget;
class QSplitter;
namespace eve::ui::detail
{
/** @brief 设置控件、偏好文件和首次启动共同使用的逻辑像素范围。 */
struct PreferenceDimension {
    int minimum, initial, maximum;
};
inline constexpr PreferenceDimension navigationWidthPixels{180, 240, 360};
inline constexpr PreferenceDimension panelHeightPixels{120, 160, 480};

struct SystemSurfaceSettings {
    bool highContrast = false;
    bool transparencyEnabled = true;
};
/** @brief 仅在 owner 工作；空路径为会话模式，窗口销毁自动释放连接和定时器。 */
class WorkbenchPreferences final : public QObject
{
  public:
    /** Load and monitor the user file owned by this window. */
    WorkbenchPreferences(ElaWindow *window, QTabWidget *panel, QString file);
    /** @brief 绑定设置页提示，立即回放当前状态，不保存插件回调。 */
    void setReporter(std::function<void(const QString &)> reporter);
    /** @brief 显式修改布局；高度按窗口可用空间限制，底部展开状态不持久化。 */
    void setPanelHeight(int height);
    /** @brief 当前首选高度，不随底部隐藏而清零。 */
    int panelHeight() const { return panelHeight_; }
    int material() const { return material_; }
    int transition() const { return transition_; }
    bool darkTheme() const { return darkTheme_; }
    bool userCardVisible() const { return userCard_; }
    const QString &backgroundPath() const { return backgroundPath_; }
    const QPixmap &background() const { return background_; }
    const QString &appearanceHint() const { return appearanceHint_; }
    const QString &filePath() const { return file_; }
    void observeAppearance(std::function<void()> observer);
    void setFileGuard(std::function<void(const std::function<void()> &)> guard);
    void setDarkTheme(bool dark);
    void setMaterial(int mode);
    void setTransition(int mode);
    void setUserCardVisible(bool visible);
    bool setBackground(const QString &path);
    /** Apply an observed system value to this workbench's current material and wallpaper. */
    void applySystemSurfaceSettings(SystemSurfaceSettings system);
    /** Stop incoming file events and save pending edits before the window closes. */
    bool close();
    void openFile();
    /** @brief 恢复默认主题/布局，不应用业务配置或打开底部。 */
    void reset();

  private:
    void load();
    bool flush(bool updateView = true);
    void watchFiles();
    void apply(const QJsonObject &value);
    void applyBackground(const QString &path, const QImage &image);
    QJsonObject snapshot() const;
    void notifyControls();
    /** @brief 合并快速设置变化，写入窗口属于宿主定时器。 */
    void changed();
    /** @brief 面板显示时应用高度；隐藏时保留值，窗口缩小时重新限幅。 */
    void applyHeight();
    void applyAppearance();
    void applyMotion();
    /** @brief 提示通过宿主值文本回报，不穿越异常或保留 UI 工厂。 */
    void report(const QString &text);
    /** @brief 观察显隐/尺寸，不拦截原生事件。 */
    bool eventFilter(QObject *watched, QEvent *event) override;
    QPointer<ElaWindow> window_;
    QPointer<QTabWidget> panel_;
    QPointer<QSplitter> splitter_;
    QPointer<QWidget> wallpaper_;
    QPixmap background_;
    QString backgroundPath_, appearanceHint_;
    int material_ = 0, transition_ = 0;
    bool darkTheme_ = false, userCard_ = true;
    QString file_, message_;
    QJsonObject baseline_;
    QByteArray observed_;
    bool observedWithFile_ = false;
    bool hasObservation_ = false;
    QFileSystemWatcher watcher_;
    QTimer timer_, reloadTimer_;
    std::function<void(const std::function<void()> &)> fileGuard_;
    std::function<void(const QString &)> reporter_;
    std::function<void()> appearanceObserver_;
    int panelHeight_ = panelHeightPixels.initial;
    bool restoring_ = true, applyingHeight_ = false, dirty_ = false;
};
} // namespace eve::ui::detail
