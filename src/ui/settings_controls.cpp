#include "appearance_p.hpp"
#include "preferences_p.hpp"
#include "settings_page_p.hpp"
#include <ElaComboBox.h>
#include <ElaSpinBox.h>
#include <ElaToolButton.h>
#include <ElaWindow.h>
#include <QAction>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPointer>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

namespace eve::ui::detail
{
namespace
{
/** Keep the setting synchronized with the panel while the workbench is hidden or resized. */
class PanelVisibility final : public QObject
{
  public:
    PanelVisibility(QWidget *panel, ElaToggleSwitch *toggle) : QObject(toggle), panel_(panel), toggle_(toggle)
    {
        panel->installEventFilter(this);
    }
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
            QSignalBlocker block(toggle_);
            toggle_->setIsToggled(!panel_->isHidden());
        }
        return false;
    }

  private:
    QWidget *panel_;
    ElaToggleSwitch *toggle_;
};
} // namespace

SettingsPage *createSettingsPage(ElaWindow *window, QTabWidget *panel, WorkbenchPreferences *prefs,
                                 SettingsPage::Guard guard)
{
    auto page = new SettingsPage(window, guard);
    auto combo = [page, guard](const char *id, const QString &title, const QStringList &labels, int index, auto apply) {
        auto control = new ElaComboBox(page);
        control->setObjectName(id);
        control->setAccessibleName(title);
        control->addItems(labels);
        control->setCurrentIndex(index);
        QObject::connect(control, QOverload<int>::of(&QComboBox::currentIndexChanged), page,
                         [guard, apply](int value) { guard([&] { apply(value); }); });
        return control;
    };
    auto toggle = [page, guard](const char *id, const QString &title, bool checked, const QString &keywords,
                                auto apply) {
        auto control = new KeyboardToggleSwitch(page);
        control->setObjectName(id);
        control->setAccessibleName(title);
        control->setIsToggled(checked);
        QObject::connect(control, &ElaToggleSwitch::toggled, page,
                         [guard, apply](bool value) { guard([&] { apply(value); }); });
        page->addRow(title, control, keywords);
        return control;
    };
    auto spin = [page, guard](const char *id, const QString &title, int low, int high, int value,
                              const QString &keywords, auto apply) {
        auto control = new ElaSpinBox(page);
        control->setObjectName(id);
        control->setAccessibleName(title);
        control->setRange(low, high);
        control->setSuffix(" px");
        control->setValue(value);
        // Ela's inline arrows occupy two button heights. Leave the editor space
        // for the full value and suffix even when a larger UI font is in use.
        const auto valueWidth = control->fontMetrics().horizontalAdvance(QString::number(high) + control->suffix());
        control->setFixedWidth(2 * control->height() + valueWidth + 18);
        QObject::connect(control, QOverload<int>::of(&QSpinBox::valueChanged), page,
                         [guard, apply](int number) { guard([&] { apply(number); }); });
        page->addRow(title, control, keywords);
        return control;
    };
    page->addGroup("EveSettingsAppearanceSection", QObject::tr("外观"), "appearance");
    const auto themeTitle = QObject::tr("工作台主题");
    auto theme = combo("EveSettingsTheme", themeTitle, {QObject::tr("浅色"), QObject::tr("深色")},
                       eTheme->getThemeMode() == ElaThemeType::Dark,
                       [](int index) { eTheme->setThemeMode(index ? ElaThemeType::Dark : ElaThemeType::Light); });
    page->addRow(themeTitle, theme, QObject::tr("浅色 深色 theme light dark"));
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, theme, [theme](ElaThemeType::ThemeMode mode) {
        QSignalBlocker block(theme);
        theme->setCurrentIndex(mode == ElaThemeType::Dark);
    });
    auto materialBox = new QWidget(page);
    auto stack = new QVBoxLayout(materialBox);
    stack->setContentsMargins(0, 0, 0, 0);
    auto controls = new QHBoxLayout;
    stack->addLayout(controls);
    auto picker = new ElaToolButton(materialBox);
    picker->setObjectName("EveBackgroundPicker");
    picker->setAccessibleName(QObject::tr("选择背景图片"));
    picker->setToolTip(QObject::tr("选择本地背景图片"));
    picker->setFixedSize(66, 34);
    picker->setIconSize(QSize(56, 28));
    controls->addWidget(picker);
    const auto materialTitle = QObject::tr("窗口材质");
    auto material = combo("EveSettingsMaterial", materialTitle,
                          {QObject::tr("标准"), QObject::tr("Ela 云母"), QObject::tr("Windows 云母"),
                           QObject::tr("Mica Alt"), QObject::tr("亚克力毛玻璃"), QObject::tr("DWM 模糊")},
                          prefs->material(), [prefs](int value) { prefs->setMaterial(value); });
    controls->addWidget(material);
    auto hint = new ElaText(materialBox);
    hint->setObjectName("EveSettingsMaterialHint");
    hint->setTextPixelSize(12);
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    secondaryText(hint);
    stack->addWidget(hint);
    auto restore = new ActionButton(QObject::tr("恢复默认背景"), materialBox);
    restore->setObjectName("EveResetBackground");
    stack->addWidget(restore, 0, Qt::AlignRight);
    const auto refreshImage = [prefs, picker, hint, restore] {
        const bool custom = !prefs->background().isNull();
        const auto pixmap = custom ? prefs->background() : QPixmap(":/include/Image/MicaBase.png");
        const auto preview =
            pixmap.scaled(picker->iconSize(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QRect crop(QPoint(), picker->iconSize());
        crop.moveCenter(preview.rect().center());
        picker->setIcon(QIcon(preview.copy(crop)));
        restore->setVisible(custom);
        const auto effect = prefs->appearanceHint();
        hint->setText(
            (custom ? QFileInfo(prefs->backgroundPath()).fileName() : QObject::tr("点击缩略图选择背景图片。")) +
            (effect.isEmpty() ? QString{} : "\n" + effect));
    };
    QObject::connect(picker, &QToolButton::clicked, page, [=] {
        auto dialog = new QFileDialog(page, QObject::tr("选择背景图片"), prefs->backgroundPath(),
                                      QObject::tr("图片 (*.png *.jpg *.jpeg *.webp)"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setFileMode(QFileDialog::ExistingFile);
        QObject::connect(dialog, &QFileDialog::fileSelected, page, [=](const QString &path) {
            guard([&] {
                if (!prefs->setBackground(path))
                    hint->setText(QObject::tr("无法读取这张图片，请重新选择。"));
            });
        });
        dialog->open();
    });
    QObject::connect(restore, &QPushButton::clicked, page, [=] {
        guard([&] {
            prefs->setBackground({});
            picker->setFocus();
        });
    });
    prefs->observeAppearance([alive = QPointer<QWidget>(page), refreshImage] { if (alive) refreshImage(); });
    page->addRow(materialTitle, materialBox,
                 QObject::tr("背景 图片 壁纸 窗口效果 云母 毛玻璃 acrylic mica dwm blur wallpaper background"),
                 material);
    const auto transitionTitle = QObject::tr("页面切换效果");
    auto transition = combo(
        "EveSettingsTransition", transitionTitle,
        {QObject::tr("无动效"), QObject::tr("上浮"), QObject::tr("缩放"), QObject::tr("翻转"), QObject::tr("模糊")},
        prefs->transition(), [prefs](int value) { prefs->setTransition(value); });
    auto motionControls = new QWidget(page);
    auto motionLayout = new QHBoxLayout(motionControls);
    motionLayout->setContentsMargins(0, 0, 0, 0);
    motionLayout->addWidget(transition);
    auto preview = new ActionButton(QObject::tr("预览"), motionControls);
    preview->setObjectName("EveSettingsTransitionPreview");
    preview->setAccessibleName(QObject::tr("预览页面切换效果"));
    motionLayout->addWidget(preview);
    const auto refreshMotion = [=] { preview->setEnabled(window->getStackSwitchMode() != ElaWindowType::None); };
    QObject::connect(window, &ElaWindow::pStackSwitchModeChanged, preview, refreshMotion);
    QObject::connect(preview, &QPushButton::clicked, page, [=] { guard([=] { window->previewStackSwitch(); }); });
    refreshMotion();
    page->addRow(transitionTitle, motionControls, QObject::tr("动画 过渡 动效 transition"), transition);
    auto user = toggle("EveSettingsUserCard", QObject::tr("显示用户卡片"), prefs->userCardVisible(),
                       QObject::tr("账户 登录 头像"), [prefs](bool visible) { prefs->setUserCardVisible(visible); });
    page->addGroup("EveSettingsLayoutSection", QObject::tr("工作区布局"), "layout");
    const auto navTitle = QObject::tr("插件导航");
    auto nav =
        combo("EveSettingsNavigationMode", navTitle, {QObject::tr("紧凑图标"), QObject::tr("展开名称")},
              window->getNavigationBarDisplayMode() == ElaNavigationType::Maximal, [window](int index) {
                  window->setNavigationBarDisplayMode(index ? ElaNavigationType::Maximal : ElaNavigationType::Compact);
              });
    page->addRow(navTitle, nav, "navigation");
    QObject::connect(window, &ElaWindow::pNavigationBarDisplayModeChanged, nav, [=] {
        QSignalBlocker block(nav);
        nav->setCurrentIndex(window->getNavigationBarDisplayMode() == ElaNavigationType::Maximal);
    });
    auto navWidth =
        spin("EveSettingsNavigationWidth", QObject::tr("展开导航宽度"), navigationWidthPixels.minimum,
             navigationWidthPixels.maximum, window->getNavigationBarWidth(),
             QObject::tr("导航宽度 width"), [window](int width) { window->setNavigationBarWidth(width); });
    QObject::connect(window, &ElaWindow::pNavigationBarWidthChanged, navWidth, [=] {
        QSignalBlocker block(navWidth);
        navWidth->setValue(window->getNavigationBarWidth());
    });
    auto showPanel = toggle("EveSettingsPanel", QObject::tr("显示底部工作区"), !panel->isHidden(),
                            QObject::tr("底部面板 panel"), [panel](bool visible) { panel->setVisible(visible); });
    new PanelVisibility(panel, showPanel);
    auto height = spin("EveSettingsPanelHeight", QObject::tr("底部工作区高度"), panelHeightPixels.minimum,
                       panelHeightPixels.maximum, prefs->panelHeight(),
                       QObject::tr("面板高度 panel height"), [prefs](int value) { prefs->setPanelHeight(value); });
    if (auto splitter = window->findChild<QSplitter *>("ElaWorkbenchPanelSplitter"))
        QObject::connect(splitter, &QSplitter::splitterMoved, height, [=] {
            QSignalBlocker block(height);
            height->setValue(prefs->panelHeight());
        });
    auto reset = new ActionButton(QObject::tr("恢复默认"), page);
    reset->setObjectName("EveSettingsReset");
    page->addRow(QObject::tr("默认外观与布局"), reset, QObject::tr("重置 reset"));
    QObject::connect(reset, &QPushButton::clicked, page, [=] {
        guard([&] {
            prefs->reset();
            const QSignalBlocker a(material), b(transition), c(user), d(height);
            material->setCurrentIndex(prefs->material());
            transition->setCurrentIndex(prefs->transition());
            user->setIsToggled(prefs->userCardVisible());
            height->setValue(prefs->panelHeight());
            refreshImage();
        });
    });
    page->addGroup("EveSettingsNotificationsSection", QObject::tr("通知"), "notifications");
    toggle("EveSettingsDoNotDisturb", QObject::tr("免打扰"), window->property("EveDoNotDisturb").toBool(),
           QObject::tr("静音 通知"), [window](bool muted) { window->setProperty("EveDoNotDisturb", muted); });
    page->addGroup("EveSettingsDeveloperSection", QObject::tr("开发者"), "developer");
    auto output = new ActionButton(QObject::tr("打开输出"), page);
    output->setObjectName("EveSettingsOutput");
    page->addRow(QObject::tr("运行日志"), output, QObject::tr("日志 通道 log output"));
    QObject::connect(output, &QPushButton::clicked, page, [=] {
        guard([&] {
            if (auto action = window->findChild<QAction *>("EveOpenOutput"))
                action->trigger();
        });
    });
    page->addGroup("EveSettingsShortcutsSection", QObject::tr("快捷键"), "keyboard shortcuts");
    auto keys = new ElaText(
        QObject::tr("搜索页面　Ctrl+P\n搜索命令　Ctrl+Shift+P\n展开导航　Ctrl+B\n底部工作区　Ctrl+J\n搜索设置　Ctrl+F"),
        14, page);
    keys->setWordWrap(true);
    keys->setFocusPolicy(Qt::StrongFocus);
    page->addRow(QObject::tr("工作台快捷键"), keys, QObject::tr("搜索 命令 页签 键盘 前进 后退"));
    prefs->setReporter([label = QPointer<ElaText>(page->status())](const QString &message) {
        if (label)
            label->setText(message);
    });
    page->finish();
    return page;
}
} // namespace eve::ui::detail
