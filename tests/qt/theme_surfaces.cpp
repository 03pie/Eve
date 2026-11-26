/** Verify that assigning a workbench automation id preserves native Ela rendering. */
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaListView.h>
#include <ElaPlainTextEdit.h>
#include <ElaScrollArea.h>
#include <ElaSpinBox.h>
#include <ElaTableView.h>
#include <ElaTheme.h>
#include <ElaWindow.h>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QScreen>
#include <QTest>
#include <QVBoxLayout>
#include <QWindow>
#include <array>
#include <stdexcept>

namespace {
void require(bool condition, const char* text)
{
    if (!condition) throw std::runtime_error(text);
}
QImage paint(QWidget& surface)
{
    return surface.grab().toImage().convertToFormat(QImage::Format_ARGB32);
}
void save(const QImage& image, const QString& name)
{
    const auto destination = qEnvironmentVariable("CORDIS_THEME_EVIDENCE");
    if (destination.isEmpty()) return;
    QDir directory(destination);
    require(directory.mkpath(".") && image.save(directory.filePath(name + ".png")), "capture Ela surface");
}
void saveNative(QWidget& widget, const QString& name)
{
    if (qEnvironmentVariableIsEmpty("CORDIS_THEME_EVIDENCE") || QGuiApplication::platformName() != "windows") return;
    auto window = widget.windowHandle();
    require(window && window->screen(), "native surface window");
    const auto image = window->screen()->grabWindow(widget.winId()).toImage();
    require(!image.isNull(), "native surface capture");
    save(image, name + "-window");
}
void controls(QWidget* parent, bool renamed)
{
    auto layout = new QVBoxLayout(parent);
    layout->setSpacing(4);
    auto list = new ElaListView(parent);
    list->setFixedHeight(64);
    auto scroll = new ElaScrollArea(parent);
    scroll->setFixedHeight(30);
    auto plain = new ElaPlainTextEdit(parent);
    plain->setFixedHeight(52);
    plain->setPlainText(QStringLiteral("日志"));
    auto line = new ElaLineEdit(parent);
    line->setText(QStringLiteral("设置搜索"));
    auto combo = new ElaComboBox(parent);
    combo->addItem(QStringLiteral("标准材质"));
    auto spin = new ElaSpinBox(parent);
    spin->setValue(42);
    auto table = new ElaTableView(parent);
    table->setFixedHeight(40);
    const std::array<QWidget*, 7> widgets{list, scroll, plain, line, combo, spin, table};
    for (auto widget : widgets) {
        if (renamed) widget->setObjectName("CordisTest" + widget->objectName());
        widget->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(widget);
    }
}

void settingsSurfaces(ElaWindow& window)
{
    auto theme = window.findChild<QComboBox*>("CordisSettingsTheme");
    auto material = window.findChild<QComboBox*>("CordisSettingsMaterial");
    auto nav = window.findChild<ElaListView*>("CordisSettingsNavigation");
    auto hint = window.findChild<QLabel*>("CordisSettingsMaterialHint");
    require(theme && material && nav && hint, "settings appearance controls");
    for (const bool dark : {false, true}) {
        theme->setCurrentIndex(dark);
        for (int mode = 0; mode < material->count(); ++mode) {
            material->setCurrentIndex(mode);
            const auto tag = QString("%1-%2").arg(dark ? "dark" : "light").arg(mode);
            QTest::qWait(40);
            require(hint->height() >= hint->heightForWidth(hint->width()), "clipped material explanation");
            const auto page = paint(window);
            const auto viewport = nav->viewport();
            const auto point = viewport->mapTo(&window, QPoint(viewport->width() / 2, viewport->height() - 6));
            const QColor pixel = page.pixelColor(point * page.devicePixelRatio());
            // The backing store may delegate this area to DWM; opaque navigation must use the page's theme.
            if (pixel.alpha() > 230 && (dark ? pixel.lightness() > 142 : pixel.lightness() < 135))
                throw std::runtime_error(("settings navigation " + tag + " " + pixel.name()).toStdString());
            save(page, "settings-" + tag);
            if (mode != 0 && mode != 4) continue;
            saveNative(window, "settings-" + tag);
            material->showPopup();
            QTest::qWait(470); // The Ela list finishes its expansion in 400 ms.
            auto popup = material->view()->window();
            require(popup != &window && popup->isVisible(), "opened material list");
            const auto frame = paint(*popup);
            save(frame, "settings-material-menu-" + tag);
            const QColor surface = frame.pixelColor(frame.width() * 4 / 5, frame.height() / 2);
            if (surface.alpha() < 230 || (dark ? surface.lightness() > 135 : surface.lightness() < 155))
                throw std::runtime_error(("settings material menu " + tag + " " + surface.name(QColor::HexArgb)).toStdString());
            const QColor shadow = frame.pixelColor(qRound(2 * frame.devicePixelRatio()), frame.height() / 2);
            if (shadow.alpha() > 190)
                throw std::runtime_error(("settings material menu shadow " + tag + " " + shadow.name(QColor::HexArgb)).toStdString());
            material->hidePopup();
            require(QTest::qWaitFor([popup] { return !popup->isVisible(); }, 1000), "closed material list");
        }
    }
}
}
void auditElaThemeSurfaces(ElaWindow& workbench)
{
    QWidget fixture;
    fixture.setAttribute(Qt::WA_DontShowOnScreen);
    fixture.resize(620, 320);
    auto layout = new QHBoxLayout(&fixture);
    auto left = new QWidget(&fixture);
    auto right = new QWidget(&fixture);
    left->setFixedWidth(290);
    right->setFixedWidth(290);
    layout->addWidget(left);
    layout->addWidget(right);
    controls(left, false);
    controls(right, true);
    fixture.show();
    QStringList mismatches;
    for (auto theme : {ElaThemeType::Dark, ElaThemeType::Light, ElaThemeType::Dark}) {
        eTheme->setThemeMode(theme);
        auto palette = fixture.palette();
        palette.setColor(QPalette::Window, ElaThemeColor(theme, WindowBase));
        fixture.setPalette(palette);
        fixture.setAutoFillBackground(true);
        QTest::qWait(70);
        const auto tag = theme == ElaThemeType::Dark ? "dark" : "light";
        const auto frame = paint(fixture);
        save(frame, QString("controls-%1").arg(tag));
        const auto ratio = frame.devicePixelRatio();
        for (auto base : left->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
            const auto peer = right->findChild<QWidget*>("CordisTest" + base->objectName(), Qt::FindDirectChildrenOnly);
            require(peer && peer->size() == base->size(), "mismatched Ela control geometry");
            const auto portion = [&](QWidget* control) {
                const QPoint origin = control->mapTo(&fixture, {});
                return frame.copy(QRect(origin * ratio, control->size() * ratio));
            };
            const auto expected = portion(base);
            const auto sample = expected.pixelColor(expected.width() * 2 / 3, expected.height() / 2);
            require(theme == ElaThemeType::Dark ? sample.lightness() < 145 : sample.lightness() > 140,
                    "baseline control surface did not follow the theme");
            if (expected != portion(peer)) {
                const auto label = QString("%1-%2").arg(tag, base->objectName());
                if (!mismatches.contains(label)) mismatches.append(label);
            }
        }
    }
    if (!mismatches.isEmpty()) throw std::runtime_error(mismatches.join(", ").toStdString());
    settingsSurfaces(workbench);
}
