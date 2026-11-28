#pragma once
#include <eve/ui/extensions.hpp>
#include <QTranslator>
#include <memory>

namespace eve::ui::detail {
struct DesktopLanguageState {
    QString locale = QStringLiteral("en");
    QString packageHash;
};

/** Acquire the process's desktop translator on the application thread. */
std::shared_ptr<QTranslator> acquireDesktopTranslator(const QString& settingsFile, DesktopLanguageState& active);
std::shared_ptr<ExtensionControls> withLanguageExtensions(std::shared_ptr<ExtensionControls> host,
    const QString& settingsFile, DesktopLanguageState active, std::function<bool()> available);
}
