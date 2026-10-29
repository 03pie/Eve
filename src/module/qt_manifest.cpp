/** @file qt_manifest.cpp
 * @brief 读取固定产物的 Qt 元数据，严格核对当前构建契约；不创建 root。
 */
#include "artifact_p.hpp"
#include "qt_manifest_p.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPluginLoader>
#include <QThread>
#include <algorithm>
#include <cordis/module/qt_manifest.hpp>

namespace cordis::module {
QtToolchain qtToolchain()
{
    QtToolchain value{CORDIS_PROJECT_VERSION, QT_VERSION_STR, "MSVC", CORDIS_QT_COMPILER_VERSION, ""};
#if defined(_DLL)
#if defined(_DEBUG)
    value.runtime = "MDd";
#else
    value.runtime = "MD";
#endif
#endif
    value.pointerBits = sizeof(void*) * 8;
    value.cppStandard = _MSVC_LANG;
    value.iteratorDebug = _ITERATOR_DEBUG_LEVEL;
#if !defined(QT_NO_DEBUG)
    value.debug = true;
#endif
    return value;
}
namespace {
ErrorInfo error(ErrorCode code, const char* message)
{
    auto value = makeError(code, message);
    value.phase = "module.qt.preflight";
    return value;
}
QJsonObject contract(const QtToolchain& value)
{
    return {{"sdkVersion", QString::fromStdString(value.sdkVersion)},
            {"qtVersion", QString::fromStdString(value.qtVersion)},
            {"compiler", QString::fromStdString(value.compiler)},
            {"compilerVersion", QString::fromStdString(value.compilerVersion)},
            {"runtime", QString::fromStdString(value.runtime)},
            {"pointerBits", static_cast<int>(value.pointerBits)},
            {"cppStandard", static_cast<int>(value.cppStandard)},
            {"iteratorDebug", static_cast<int>(value.iteratorDebug)},
            {"debug", value.debug}};
}
} // namespace
namespace detail {
Result<void> verifyQtMetadata(const PreflightArtifact& artifact, QPluginLoader& reader)
{
    const auto host = qtToolchain();
    if (host.pointerBits != 64 || host.qtVersion != "5.15.2" || host.qtVersion != qVersion() || host.runtime.empty())
        return Result<void>::failure(error(ErrorCode::AbiMismatch, "unsupported host Qt toolchain"));
    const auto metadata = reader.metaData();
    if (metadata.value("IID") != QJsonValue(QStringLiteral("org.cordis.IQtPluginV1/1.0")) ||
        metadata.value("version") != QJsonValue(QT_VERSION & 0xffff00) ||
        metadata.value("debug") != QJsonValue(host.debug))
        return Result<void>::failure(error(ErrorCode::AbiMismatch, "Qt plugin IID/version/debug differs from host"));
    auto data = metadata.value("MetaData").toObject();
    const auto declared = data.value("plugins").toArray();
    if (declared.size() != static_cast<int>(artifact.manifest().plugins.size()))
        return Result<void>::failure(error(ErrorCode::AbiMismatch, "embedded plugin catalog differs from manifest"));
    std::vector<std::string> actual;
    for (const auto& plugin : declared) {
        if (!plugin.isString())
            return Result<void>::failure(error(ErrorCode::AbiMismatch, "embedded plugin identifier is not a string"));
        actual.push_back(plugin.toString().toStdString());
    }
    auto expectedPlugins = artifact.manifest().plugins;
    std::sort(actual.begin(), actual.end());
    std::sort(expectedPlugins.begin(), expectedPlugins.end());
    if (actual != expectedPlugins)
        return Result<void>::failure(error(ErrorCode::AbiMismatch, "embedded plugin catalog differs from manifest"));
    QJsonArray plugins;
    for (const auto& plugin : expectedPlugins)
        plugins.append(QString::fromStdString(plugin));
    data["plugins"] = plugins;
    const QJsonObject expected{{"moduleId", QString::fromStdString(artifact.manifest().moduleId)},
                               {"moduleVersion", QString::fromStdString(artifact.manifest().moduleVersion)},
                               {"plugins", plugins},
                               {"toolchain", contract(host)}};
    if (data != expected)
        return Result<void>::failure(
            error(ErrorCode::AbiMismatch, "embedded module identity/catalog/toolchain differs from host"));
    return Result<void>::success();
}
} // namespace detail
Result<QtModuleInspection> inspectQtModule(const std::string& root, const std::string& path, PreflightLimits limits)
{
    using Output = Result<QtModuleInspection>;
    auto app = QCoreApplication::instance();
    if (!app || QCoreApplication::closingDown() || QThread::currentThread() != app->thread())
        return Output::failure(error(ErrorCode::WrongThread, "Qt metadata inspection requires application owner"));
    auto source = detail::verifyModuleSource(root, path, limits);
    if (!source.hasValue())
        return Output::failure(source.error());
    const auto& artifact = source.value()->artifact;
    if (artifact.manifest().loader != ModuleLoader::QtUi)
        return Output::failure(error(ErrorCode::AbiMismatch, "Qt backend requires qt-ui loader"));
    QPluginLoader reader(QString::fromUtf8(artifact.libraryPath().c_str()));
    if (reader.isLoaded())
        return Output::failure(
            error(ErrorCode::ModulePinned, "metadata inspection encountered an already loaded plugin"));
    auto checked = detail::verifyQtMetadata(artifact, reader);
    if (!checked.hasValue())
        return Output::failure(checked.error());
    return Output::success({artifact, qtToolchain()});
}
} // namespace cordis::module
