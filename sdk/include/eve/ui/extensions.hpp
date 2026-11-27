/** @file extensions.hpp
 * @brief Host data and operations consumed by the desktop extension center.
 */
#pragma once
#include <eve/task.hpp>
#include <QByteArray>
#include <QString>
#include <functional>
#include <vector>

namespace eve::ui {
enum class ExtensionKind { Business, Desktop };
enum class ExtensionAction { Install, Uninstall, UseDesktop, Enable, AddInstance, Configure, Disable, ShowLogs, Restart };

struct ExtensionItem {
    QString id, name, description, publisher, version, pluginId, state;
    ExtensionKind kind = ExtensionKind::Business;
    bool builtin = false, current = false, pending = false, missing = false;
    int instances = 0;
    std::vector<ExtensionAction> actions;
    QString packageHash;
    QByteArray icon;
    bool legacyReference = false;
};

struct ExtensionPreview {
    ExtensionItem item;
    QString path, sha256;
};

/** The host validates all mutations against current inventory and the reviewed archive digest. */
struct ExtensionControls {
    std::function<Result<std::vector<ExtensionItem>>()> list;
    std::function<Task<ExtensionPreview>(QString path)> preview;
    std::function<Task<void>(ExtensionAction action, QString id, QString path, QString sha256)> apply;
};
} // namespace eve::ui
