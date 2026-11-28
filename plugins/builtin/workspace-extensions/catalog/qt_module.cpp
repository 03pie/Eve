#include <eve/extensions/catalog.hpp>
#include <eve/extensions/qt_module.hpp>
#include "qt_artifact_load_p.hpp"
#include <QFileInfo>

namespace eve::extensions {
Result<module::ModuleLoadReport> loadQtExtension(const QString& manifestPath, const QString& moduleId,
    const QString& pluginId, const QString& digest, ExtensionSource source)
{
    using Output = Result<module::ModuleLoadReport>;
    if (isReservedIdentity(moduleId, pluginId, source))
        return Output::failure(makeError(ErrorCode::AbiMismatch, "Workspace extension uses a reserved identity."));
    const QFileInfo manifest(manifestPath);
    if (manifest.fileName() != QStringLiteral("module.json"))
        return Output::failure(makeError(ErrorCode::ConfigInvalid, "Invalid workspace extension manifest path."));
    return module::detail::loadInstalledQtModule(manifest.absolutePath().toStdString(), moduleId.toStdString(),
        pluginId.toStdString(), digest.toStdString());
}
}
