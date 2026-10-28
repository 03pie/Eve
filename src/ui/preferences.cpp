/** @file preferences.cpp
 * @brief 在独立 JSON 中保存 Ela 主题及工作台布局，失败不影响插件运行与关闭屏障。
 */
#include "preferences_p.hpp"
#include <ElaTheme.h>
#include <ElaWindow.h>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSplitter>
#include <QTabWidget>
#include <algorithm>
#include <cmath>
#include <utility>

namespace cordis::ui::detail {
WorkbenchPreferences::WorkbenchPreferences(ElaWindow* window, QTabWidget* panel, QString file)
    : QObject(window), window_(window), panel_(panel),
      splitter_(window->findChild<QSplitter*>("ElaWorkbenchPanelSplitter")), file_(std::move(file))
{
    timer_.setSingleShot(true);
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, [this] { flush(); });
    load();
    panel->hide();
    panel->installEventFilter(this);
    window->installEventFilter(this);
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { changed(); });
    connect(window, &ElaWindow::pNavigationBarWidthChanged, this, [this] { changed(); });
    connect(window, &ElaWindow::pNavigationBarDisplayModeChanged, this, [this] { changed(); });
    if (splitter_)
        connect(splitter_, &QSplitter::splitterMoved, this, [this] {
            if (!applyingHeight_ && panel_ && panel_->isVisible()) {
                panelHeight_ = std::clamp(panel_->height(), 120, 480);
                changed();
            }
        });
    restoring_ = false;
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
        !integer("theme", 0, 1) || !integer("navigationMode", 2, 3) || !integer("navigationWidth", 180, 360) ||
        !integer("panelHeight", 120, 480)) {
        report(QString::fromUtf8("界面偏好格式或版本无效，已保留默认布局。修改设置后可重新保存。"));
        return;
    }
    eTheme->setThemeMode(value.value("theme").toInt() ? ElaThemeType::Dark : ElaThemeType::Light);
    window_->setNavigationBarWidth(value.value("navigationWidth").toInt());
    window_->setNavigationBarDisplayMode(
        static_cast<ElaNavigationType::NavigationDisplayMode>(value.value("navigationMode").toInt()));
    panelHeight_ = value.value("panelHeight").toInt();
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
    panelHeight_ = std::clamp(height, 120, 480);
    applyHeight();
    changed();
}
void WorkbenchPreferences::applyHeight()
{
    if (!splitter_ || !panel_ || panel_->isHidden() || applyingHeight_)
        return;
    applyingHeight_ = true;
    const auto total = splitter_->height();
    const auto desired = std::min(panelHeight_, std::max(120, total - 240));
    splitter_->setSizes({std::max(240, total - desired), desired});
    applyingHeight_ = false;
}
bool WorkbenchPreferences::eventFilter(QObject* watched, QEvent* event)
{
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
    const QJsonObject value{{"version", 1},
                            {"theme", eTheme->getThemeMode() == ElaThemeType::Dark ? 1 : 0},
                            {"navigationMode", static_cast<int>(mode)},
                            {"navigationWidth", std::clamp(window_->getNavigationBarWidth(), 180, 360)},
                            {"panelHeight", panelHeight_}};
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
    window_->setNavigationBarWidth(240);
    window_->setNavigationBarDisplayMode(ElaNavigationType::Compact);
    setPanelHeight(160);
    changed();
}
} // namespace cordis::ui::detail
