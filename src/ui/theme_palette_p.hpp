#pragma once
#include <ElaApplication.h>
#include <ElaTheme.h>
#include <QAbstractScrollArea>
#include <QPalette>
#include <QWidget>

namespace eve::ui::detail {
// Native Qt widgets read different roles for lists, labels, headers and
// inactive selections. Bind the containing surface and let children inherit.
inline QPalette workbenchPalette(QPalette palette, ElaThemeType::ThemeMode mode,
                                 ElaThemeType::ThemeColor base = ElaThemeType::WindowBase)
{
    const auto color = [mode](ElaThemeType::ThemeColor token) { return eTheme->getThemeColor(mode, token); };
    palette.setColor(QPalette::Window, color(base));
    palette.setColor(QPalette::Base, color(base));
    palette.setColor(QPalette::AlternateBase, color(ElaThemeType::BasicAlternating));
    palette.setColor(QPalette::Button, color(ElaThemeType::BasicBase));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(role, color(ElaThemeType::BasicText));
        palette.setColor(QPalette::Disabled, role, color(ElaThemeType::BasicTextDisable));
    }
    palette.setColor(QPalette::PlaceholderText, color(ElaThemeType::BasicTextCategory));
    palette.setColor(QPalette::Highlight, color(ElaThemeType::PrimaryNormal));
    palette.setColor(QPalette::HighlightedText, color(ElaThemeType::BasicTextInvert));
    palette.setColor(QPalette::Link, color(ElaThemeType::PrimaryNormal));
    palette.setColor(QPalette::ToolTipBase, color(ElaThemeType::PopupBase));
    palette.setColor(QPalette::ToolTipText, color(ElaThemeType::BasicText));
    return palette;
}

inline QPalette workbenchContentPalette(QPalette palette, ElaThemeType::ThemeMode mode)
{
    auto colors = workbenchPalette(palette, mode);
    colors.setColor(QPalette::Window, Qt::transparent);
    colors.setColor(QPalette::Base, Qt::transparent);
    return colors;
}

inline void applyWorkbenchContentPalette(QWidget* surface, ElaThemeType::ThemeMode mode)
{
    surface->setPalette(workbenchContentPalette(surface->palette(), mode));
    surface->setAutoFillBackground(false);
    if (auto area = qobject_cast<QAbstractScrollArea*>(surface)) {
        auto viewport = area->viewport();
        viewport->setPalette(workbenchContentPalette(viewport->palette(), mode));
        viewport->setAutoFillBackground(false);
    }
}

inline void bindWorkbenchContentPalette(QWidget* surface)
{
    const auto apply = [surface](ElaThemeType::ThemeMode mode) { applyWorkbenchContentPalette(surface, mode); };
    apply(eTheme->getThemeMode());
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, surface, apply);
}

inline void bindWorkbenchPalette(QWidget* surface, ElaThemeType::ThemeColor base = ElaThemeType::WindowBase)
{
    const auto apply = [surface, base](ElaThemeType::ThemeMode mode) {
        surface->setPalette(workbenchPalette(surface->palette(), mode, base));
    };
    apply(eTheme->getThemeMode());
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, surface, apply);
}

inline void bindWorkbenchWindowPalette(QWidget* window, ElaThemeType::ThemeColor base = ElaThemeType::WindowBase)
{
    const auto apply = [window, base](ElaThemeType::ThemeMode mode) {
        const auto existing = window->palette();
        auto colors = workbenchPalette(existing, mode, base);
        // Ela Mica owns this brush while native materials need to see through it.
        const auto backdrop = eApp->getWindowDisplayMode() == ElaApplicationType::ElaMica
            ? existing.brush(QPalette::Window) : QBrush(Qt::transparent);
        colors.setBrush(QPalette::Window, backdrop);
        window->setPalette(colors);
    };
    apply(eTheme->getThemeMode());
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, window, apply);
}
}
