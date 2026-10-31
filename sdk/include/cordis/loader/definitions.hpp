/** @file definitions.hpp
 * @brief schema、模块来源和实际 Runtime 的同一不可变定义目录。
 */
#pragma once
#include <cordis/instance.hpp>
#include <cordis/loader/yaml.hpp>
#include <memory>

namespace cordis::loader {
/** @brief 静态装配兼容入口的运行定义；完整配置使用 ConfiguredPlugin。 */
struct KernelPlugin {
    std::string id;
    Runtime runtime;
    std::vector<ServiceRequirement> configurableInject{};
    PluginModule module{};
    std::function<Result<void>()> available{};
    std::size_t maximumDependencies = 0;
};
namespace detail {
struct DefinitionsData;
struct KernelDriverControl;
} // namespace detail
/** @brief 完整宿主定义；schema 必需依赖不能削弱 Runtime，额外接口须有类型化契约。 */
struct ConfiguredPlugin {
    PluginDefinition configuration;
    Runtime runtime;
    std::vector<ServiceRequirement> configurableInject{};
    std::function<Result<void>()> available{};
    std::size_t maximumDependencies = 0;
};
/** @brief 创建后不修改。不同目录即使内容相同也不是同一次定义校验的权限来源。 */
class ConfigurationDefinitions {
public:
    static Result<std::shared_ptr<const ConfigurationDefinitions>> create(std::vector<ConfiguredPlugin> plugins);
    ConfigurationDefinitions(const ConfigurationDefinitions&) = delete;
    ConfigurationDefinitions& operator=(const ConfigurationDefinitions&) = delete;
    Result<ValidatedCandidate> validate(ParsedConfiguration source) const;
    bool owns(const ValidatedCandidate& candidate) const noexcept;
    std::vector<std::string> plugins() const;
    Result<PluginModule> module(const std::string& plugin) const;

private:
    friend class KernelReconcileDriver;
    friend struct detail::KernelDriverControl;
    explicit ConfigurationDefinitions(std::shared_ptr<const detail::DefinitionsData> data) : data_(std::move(data)) {}
    static std::shared_ptr<const ConfigurationDefinitions> runtimeOnly(std::vector<KernelPlugin> plugins);
    std::shared_ptr<const detail::DefinitionsData> data_;
};
} // namespace cordis::loader
