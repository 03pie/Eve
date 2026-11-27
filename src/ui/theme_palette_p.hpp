#pragma once
#include <ElaTheme.h>
#include <QPalette>

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
    return palette;
}
}
