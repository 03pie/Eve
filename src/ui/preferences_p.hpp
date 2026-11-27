/** @file preferences_p.hpp
 * @brief 宿主界面偏好独立保存，不接触插件配置、实例身份或页面工厂。
 */
#pragma once
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <functional>
class ElaWindow;
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
    /** @brief 载入有界版本化偏好；无效数据保留默认布局并提供错误说明。 */
    WorkbenchPreferences(ElaWindow *window, QTabWidget *panel, QString file);
    /** @brief 绑定设置页提示，立即回放当前状态，不保存插件回调。 */
    void setReporter(std::function<void(const QString &)> reporter);
    /** @brief 显式修改布局；高度按窗口可用空间限制，底部展开状态不持久化。 */
    void setPanelHeight(int height);
    /** @brief 当前首选高度，不随底部隐藏而清零。 */
    int panelHeight() const { return panelHeight_; }
    int material() const { return material_; }
    int transition() const { return transition_; }
    bool userCardVisible() const { return userCard_; }
    const QString &backgroundPath() const { return backgroundPath_; }
    const QPixmap &background() const { return background_; }
    const QString &appearanceHint() const { return appearanceHint_; }
    void observeAppearance(std::function<void()> observer);
    void setMaterial(int mode);
    void setTransition(int mode);
    void setUserCardVisible(bool visible);
    bool setBackground(const QString &path);
    /** Apply an observed system value to this workbench's current material and wallpaper. */
    void applySystemSurfaceSettings(SystemSurfaceSettings system);
    /** @brief 原子写入已改变的偏好，失败保留旧文件且工作台继续可用。 */
    bool flush();
    /** @brief 恢复默认主题/布局，不应用业务配置或打开底部。 */
    void reset();

  private:
    /** @brief 初始恢复不触发保存，未知版本也不在无用户操作时覆盖。 */
    void load();
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
    bool userCard_ = true;
    QString file_, message_;
    QTimer timer_;
    std::function<void(const QString &)> reporter_;
    std::function<void()> appearanceObserver_;
    int panelHeight_ = panelHeightPixels.initial;
    bool restoring_ = true, applyingHeight_ = false, dirty_ = false;
};
} // namespace eve::ui::detail
