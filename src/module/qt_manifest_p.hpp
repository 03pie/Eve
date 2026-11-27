/** @file qt_manifest_p.hpp
 * @brief 对本次仍固定的产物核对 Qt 契约；不登记代次或创建 root。
 */
#pragma once
#include <eve/module/manifest.hpp>
class QPluginLoader;
namespace eve::module::detail {
Result<void> verifyQtMetadata(const PreflightArtifact& artifact, QPluginLoader& reader);
} // namespace eve::module::detail
