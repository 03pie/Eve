/** @file artifact_p.hpp
 * @brief 两类后端共享的固定来源；装载期间持有祖先和声明保护，映像由后端接管。
 */
#pragma once
#include "native_file_p.hpp"
#include <cordis/module/manifest.hpp>
#include <memory>
#include <vector>

namespace cordis::module::detail {
bool sameManifest(const ModuleManifest& left, const ModuleManifest& right);
struct VerifiedSource {
    explicit VerifiedSource(PreflightArtifact value) : artifact(std::move(value)) {}
    PreflightArtifact artifact;
    std::unique_ptr<OpenedPath> library;
    std::unique_ptr<OpenedPath> manifest;
    std::vector<std::unique_ptr<OpenedPath>> directories;
};
Result<std::unique_ptr<VerifiedSource>> verifyModuleSource(const std::string& root, const std::string& manifest,
                                                           PreflightLimits limits);
} // namespace cordis::module::detail
