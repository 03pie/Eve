#include "appearance_p.hpp"
#include "desktop_text.hpp"
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
        control->setProperty("ElaTranslucentFill", true);
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
        control->setProperty("ElaTranslucentFill", true);
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
    page->addGroup("EveSettingsAppearanceSection", desktopText("settings.appearance"), "appearance");
    auto openJson = new ActionButton(desktopText("settings.openJson"), page);
    openJson->setObjectName("EveSettingsOpenJson");
    openJson->setToolTip(prefs->filePath());
    openJson->setEnabled(!prefs->filePath().isEmpty());
    page->addRow(desktopText("settings.userSettingsFile"), openJson, QStringLiteral("settings.json file raw edit"));
    QObject::connect(openJson, &QPushButton::clicked, page, [=] { guard([=] { prefs->openFile(); }); });
    const auto themeTitle = desktopText("settings.colorTheme");
    auto theme = combo("EveSettingsTheme", themeTitle, {desktopText("settings.light"), desktopText("settings.dark")},
                       prefs->darkTheme(), [prefs](int index) { prefs->setDarkTheme(index != 0); });
    page->addRow(themeTitle, theme, desktopText("settings.workbenchThemeLightDark"));
    auto materialBox = new QWidget(page);
    auto stack = new QVBoxLayout(materialBox);
    stack->setContentsMargins(0, 0, 0, 0);
    auto controls = new QHBoxLayout;
    stack->addLayout(controls);
    auto picker = new ElaToolButton(materialBox);
    picker->setObjectName("EveBackgroundPicker");
    picker->setAccessibleName(desktopText("settings.chooseBackgroundImage"));
    picker->setToolTip(desktopText("settings.chooseALocalBackgroundImage"));
    picker->setFixedSize(66, 34);
    picker->setIconSize(QSize(56, 28));
    controls->addWidget(picker);
    const auto materialTitle = desktopText("settings.windowMaterial");
    auto material = combo("EveSettingsMaterial", materialTitle,
                          {desktopText("settings.standard"), desktopText("settings.elaMica"), desktopText("settings.windowsMica"),
                           desktopText("settings.micaAlt"), desktopText("settings.acrylic"), desktopText("settings.dwmBlur")},
                          prefs->material(), [prefs](int value) { prefs->setMaterial(value); });
    controls->addWidget(material);
    auto hint = new ElaText(materialBox);
    hint->setObjectName("EveSettingsMaterialHint");
    hint->setTextPixelSize(12);
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    secondaryText(hint);
    stack->addWidget(hint);
    auto restore = new ActionButton(desktopText("settings.resetBackground"), materialBox);
    restore->setObjectName("EveResetBackground");
    stack->addWidget(restore, 0, Qt::AlignRight);
    const auto refreshImage = [prefs, picker, hint, restore] {
        const auto& path = prefs->backgroundPath();
        const bool custom = !prefs->background().isNull();
        const auto pixmap = custom ? prefs->background() : QPixmap(":/include/Image/MicaBase.png");
        const auto preview =
            pixmap.scaled(picker->iconSize(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QRect crop(QPoint(), picker->iconSize());
        crop.moveCenter(preview.rect().center());
        picker->setIcon(QIcon(preview.copy(crop)));
        restore->setVisible(!path.isEmpty());
        auto text = path.isEmpty() ? desktopText("settings.selectTheThumbnailToChooseABackground")
                                   : QFileInfo(path).fileName();
        if (!path.isEmpty() && !custom)
            text += "\n" + desktopText("settings.couldNotLoadTheBackgroundImageThe");
        const auto effect = prefs->appearanceHint();
        if (!effect.isEmpty()) text += "\n" + effect;
        hint->setText(text);
    };
    QObject::connect(picker, &QToolButton::clicked, page, [=] {
        auto dialog = new QFileDialog(page, desktopText("settings.chooseBackgroundImage"), prefs->backgroundPath(),
                                      desktopText("settings.imagesPngJpgJpegWebp"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setFileMode(QFileDialog::ExistingFile);
        QObject::connect(dialog, &QFileDialog::fileSelected, page, [=](const QString &path) {
            guard([&] {
                if (!prefs->setBackground(path))
                    hint->setText(desktopText("settings.couldNotReadThisImageChooseAnother"));
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
    page->addRow(materialTitle, materialBox,
                 desktopText("settings.workbenchWindowMaterialWorkbenchWallpaperAcrylicMicaDwm"),
                 material);
    const auto transitionTitle = desktopText("settings.pageTransition");
    auto transition = combo(
        "EveSettingsTransition", transitionTitle,
        {desktopText("management.none"), desktopText("settings.popup"), desktopText("settings.scale"), desktopText("settings.flip"), desktopText("settings.blur")},
        prefs->transition(), [prefs](int value) { prefs->setTransition(value); });
    auto motionControls = new QWidget(page);
    auto motionLayout = new QHBoxLayout(motionControls);
    motionLayout->setContentsMargins(0, 0, 0, 0);
    motionLayout->addWidget(transition);
    auto preview = new ActionButton(desktopText("settings.preview"), motionControls);
    preview->setObjectName("EveSettingsTransitionPreview");
    preview->setAccessibleName(desktopText("settings.previewPageTransition"));
    motionLayout->addWidget(preview);
    const auto refreshMotion = [=] { preview->setEnabled(window->getStackSwitchMode() != ElaWindowType::None); };
    QObject::connect(window, &ElaWindow::pStackSwitchModeChanged, preview, refreshMotion);
    QObject::connect(preview, &QPushButton::clicked, page, [=] { guard([=] { window->previewStackSwitch(); }); });
    refreshMotion();
    page->addRow(transitionTitle, motionControls, desktopText("settings.workbenchPageTransitionAnimationMotionTransition"), transition);
    auto user = toggle("EveSettingsUserCard", desktopText("settings.showUserCard"), prefs->userCardVisible(),
                       desktopText("settings.workbenchShowUserCardAccountSignInAvatar"), [prefs](bool visible) { prefs->setUserCardVisible(visible); });
    page->addGroup("EveSettingsLayoutSection", desktopText("settings.workbenchLayout"), "layout");
    const auto navTitle = desktopText("settings.extensionNavigation");
    auto nav =
        combo("EveSettingsNavigationMode", navTitle, {desktopText("settings.compactIcons"), desktopText("settings.expandedLabels")},
              window->getNavigationBarDisplayMode() == ElaNavigationType::Maximal, [window](int index) {
                  window->setNavigationBarDisplayMode(index ? ElaNavigationType::Maximal : ElaNavigationType::Compact);
              });
    page->addRow(navTitle, nav, "navigation");
    QObject::connect(window, &ElaWindow::pNavigationBarDisplayModeChanged, nav, [=] {
        QSignalBlocker block(nav);
        nav->setCurrentIndex(window->getNavigationBarDisplayMode() == ElaNavigationType::Maximal);
    });
    auto navWidth =
        spin("EveSettingsNavigationWidth", desktopText("settings.expandedNavigationWidth"), navigationWidthPixels.minimum,
             navigationWidthPixels.maximum, window->getNavigationBarWidth(),
             desktopText("settings.workbenchNavigationWidthNavigationWidth"), [window](int width) { window->setNavigationBarWidth(width); });
    QObject::connect(window, &ElaWindow::pNavigationBarWidthChanged, navWidth, [=] {
        QSignalBlocker block(navWidth);
        navWidth->setValue(window->getNavigationBarWidth());
    });
    auto showPanel = toggle("EveSettingsPanel", desktopText("settings.showBottomPanel"), !panel->isHidden(),
                            desktopText("settings.bottomPanel"), [panel](bool visible) { panel->setVisible(visible); });
    new PanelVisibility(panel, showPanel);
    auto height = spin("EveSettingsPanelHeight", desktopText("settings.bottomPanelHeight"), panelHeightPixels.minimum,
                       panelHeightPixels.maximum, prefs->panelHeight(),
                       desktopText("settings.workbenchPanelHeightPanelHeight"), [prefs](int value) { prefs->setPanelHeight(value); });
    if (auto splitter = window->findChild<QSplitter *>("ElaWorkbenchPanelSplitter"))
        QObject::connect(splitter, &QSplitter::splitterMoved, height, [=] {
            QSignalBlocker block(height);
            height->setValue(prefs->panelHeight());
        });
    auto reset = new ActionButton(desktopText("settings.resetToDefaults"), page);
    reset->setObjectName("EveSettingsReset");
    page->addRow(desktopText("settings.defaultAppearanceAndLayout"), reset, desktopText("settings.reset"));
    QObject::connect(reset, &QPushButton::clicked, page, [=] {
        guard([=] { prefs->reset(); });
    });
    page->addGroup("EveSettingsNotificationsSection", desktopText("settings.notifications"), "notifications");
    auto muted = toggle("EveSettingsDoNotDisturb", desktopText("settings.doNotDisturb"), window->property("EveDoNotDisturb").toBool(),
           desktopText("settings.notificationsDoNotDisturbMuteNotifications"), [window](bool muted) { window->setProperty("EveDoNotDisturb", muted); });
    page->addGroup("EveSettingsDeveloperSection", desktopText("settings.developer"), "developer");
    auto output = new ActionButton(desktopText("settings.openOutput"), page);
    output->setObjectName("EveSettingsOutput");
    page->addRow(desktopText("settings.runtimeLog"), output, desktopText("settings.logsChannelsOutput"));
    QObject::connect(output, &QPushButton::clicked, page, [=] {
        guard([&] {
            if (auto action = window->findChild<QAction *>("EveOpenOutput"))
                action->trigger();
        });
    });
    page->addGroup("EveSettingsShortcutsSection", desktopText("settings.keyboardShortcuts"), "keyboard shortcuts");
    auto keys = new ElaText(
        desktopText("settings.quickOpenCtrlPCommandPaletteCtrl"),
        14, page);
    keys->setWordWrap(true);
    keys->setFocusPolicy(Qt::StrongFocus);
    page->addRow(desktopText("settings.workbenchShortcuts"), keys, desktopText("settings.searchCommandsTabsKeyboardForwardBack"));
    prefs->setReporter([label = QPointer<ElaText>(page->status())](const QString &message) {
        if (label)
            label->setText(message);
    });
    prefs->observeAppearance([=, alive = QPointer<QWidget>(page)] {
        if (!alive) return;
        const QSignalBlocker blockTheme(theme), a(material), b(transition), c(user), d(height), e(muted);
        theme->setCurrentIndex(prefs->darkTheme());
        material->setCurrentIndex(prefs->material());
        transition->setCurrentIndex(prefs->transition());
        user->setIsToggled(prefs->userCardVisible());
        height->setValue(prefs->panelHeight());
        muted->setIsToggled(window->property("EveDoNotDisturb").toBool());
        refreshImage();
    });
    page->finish();
    return page;
}
} // namespace eve::ui::detail
