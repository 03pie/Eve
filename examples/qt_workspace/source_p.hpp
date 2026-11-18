/** @file source_p.hpp
 * @brief 装配与升级共用业务身份/实际来源和 Notebook 固定契约。
 */
#pragma once
#include "workspace_p.hpp"

namespace notebook::detail {
struct BusinessArtifact {
    std::string alias, manifest;
    cordis::module::ModuleManifest identity;
};
std::string businessPlugin(const std::string& alias);
cordis::module::PreflightArtifact reviewBusinessArtifact(const std::string& alias, const std::string& root,
                                                         const std::string& manifest);
std::vector<BusinessArtifact> businessArtifacts(const std::string& root);
void requireApproved(const cordis::module::PreflightArtifact& artifact,
                     const cordis::module::NativeUnloadWhitelist& approvals);
cordis::loader::ConfiguredPlugin prepareNotebook(const std::shared_ptr<NotebookModule>& record,
                                                 const std::shared_ptr<cordis::module::ModuleManager>& manager,
                                                 const std::shared_ptr<cordis::qt::QtScheduler>& scheduler,
                                                 cordis::loader::PluginModule source, bool stateful);
} // namespace notebook::detail
