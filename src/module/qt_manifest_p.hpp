/** @file qt_manifest_p.hpp
 * @brief 对本次仍固定的产物核对 Qt 契约；不登记代次或创建 root。
 */
#pragma once
#include <cordis/module/manifest.hpp>
class QPluginLoader;
namespace cordis::module::detail {
Result<void> verifyQtMetadata(const PreflightArtifact& artifact, QPluginLoader& reader);
} // namespace cordis::module::detail
