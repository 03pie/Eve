/** @file extensions.hpp
 * @brief Host catalog, package review and extension actions available to the desktop.
 */
#pragma once
#include <eve/task.hpp>
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
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
    QString readme; // UTF-8 package README decoded by the host; description is the plain-text summary.
    QStringList categories;
    QString homepage, repository, issues, license;
    qint64 packageBytes = -1;
    QDateTime installedAt;
    QString defaultConfig;
    QStringList desktopServices;
    QString changelog;
};

struct ExtensionPreview {
    ExtensionItem item;
    QString path, sha256;
    QString installedVersion, installedHash;
};

/** The host validates all mutations against current inventory and the reviewed archive digest. */
struct ExtensionControls {
    std::function<Result<std::vector<ExtensionItem>>()> list;
    std::function<Task<ExtensionPreview>(QString path)> preview;
    std::function<Task<void>(ExtensionAction action, QString id, QString path, QString sha256)> apply;
    /** A host that reports the installed version checks that selection again before committing. */
    std::function<Task<void>(const ExtensionPreview&)> installReviewed;
};
} // namespace eve::ui
