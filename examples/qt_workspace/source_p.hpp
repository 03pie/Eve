/** @file source_p.hpp
 * @brief 装配与升级共用业务身份/实际来源和 Notebook 固定契约。
 */
#pragma once
#include "workspace_p.hpp"

namespace notebook::detail {
struct BusinessArtifact {
    std::string alias, manifest;
    eve::module::ModuleManifest identity;
};
std::string businessPlugin(const std::string& alias);
eve::module::PreflightArtifact reviewBusinessArtifact(const std::string& alias, const std::string& root,
                                                         const std::string& manifest);
std::vector<BusinessArtifact> businessArtifacts(const std::string& root);
void requireApproved(const eve::module::PreflightArtifact& artifact,
                     const eve::module::NativeUnloadWhitelist& approvals);
eve::loader::ConfiguredPlugin prepareNotebook(const std::shared_ptr<NotebookModule>& record,
                                                 const std::shared_ptr<eve::module::ModuleManager>& manager,
                                                 const std::shared_ptr<eve::qt::QtScheduler>& scheduler,
                                                 eve::loader::PluginModule source, bool stateful);
} // namespace notebook::detail
