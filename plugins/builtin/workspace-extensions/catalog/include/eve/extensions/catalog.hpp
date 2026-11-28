/** @file catalog.hpp
 * @brief 用户级扩展内容及其下次装配选择。只发布文件和元数据，启动实例由工作台协调。
 */
#pragma once
#include <eve/result.hpp>
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace eve::extensions {
enum class ExtensionSource { InstalledPackage, DevelopmentManifest };
/** Product-owned identities cannot be claimed by workspace packages or configuration entries. */
bool isReservedIdentity(const QString& moduleId, const QString& pluginId,
                        ExtensionSource source = ExtensionSource::InstalledPackage);
/** These plugin IDs could appear in valid catalogs before the built-in services were introduced. */
bool isLegacyPluginConflict(const QString& pluginId);

struct Package {
    QString moduleId, pluginId, version, name, publisher, description, kind;
    QString sha256, packageHash, defaultConfig, manifestPath;
    QByteArray icon;
    QString readme;
    QStringList categories;
    QString homepage, repository, issues, license;
    qint64 packageBytes = -1;
    QDateTime installedAt; // UTC time when this archive was selected in the local catalog.
    QString changelog;
};

class Catalog {
public:
    static QString defaultRoot();
    explicit Catalog(QString root = defaultRoot());
    /** 只读预检。Qt 插件 metadata 由应用线程检查，预览不执行工厂代码。 */
    static Result<Package> preview(const QString& file);
    Result<QVector<Package>> installed() const;
    /** 仅接受与用户确认时完全一致的包字节。返回不可覆盖的模块清单路径。 */
    Result<Package> install(const QString& file, const QString& expectedPackageHash, const QString& expectedModuleId = {},
                            std::optional<QString> expectedInstalledHash = std::nullopt);
    Result<void> remove(const QString& moduleId, const QString& expectedPackageHash);
private:
    QString root_;
};
}
