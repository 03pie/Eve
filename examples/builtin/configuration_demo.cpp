/** @file configuration_demo.cpp
 * @brief 从两份有界 YAML 文件验证内置配置并打印目标差异，不挂载插件或保存文件。
 */
#include <cordis/loader/yaml.hpp>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace cordis;
using namespace cordis::loader;
namespace {
/** @brief 读取前检查开放状态，按块累计并在超过预算前停止。 */
Result<std::string> readFile(const std::string& path, std::size_t maximum)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return Result<std::string>::failure(makeError(ErrorCode::ConfigInvalid, "cannot open " + path));
    std::string text;
    std::array<char, 4096> chunk{};
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto size = static_cast<std::size_t>(input.gcount());
        if (size > maximum - text.size()) return Result<std::string>::failure(makeError(ErrorCode::ConfigInvalid, "input byte limit exceeded: " + path));
        text.append(chunk.data(), size);
    }
    if (!input.eof()) return Result<std::string>::failure(makeError(ErrorCode::ConfigInvalid, "cannot read " + path));
    return Result<std::string>::success(std::move(text));
}
/** @brief 示例只允许无配置插件和 work/personal 枚举配置，演示真正拒绝无效负载的 schema。 */
std::vector<PluginDefinition> catalog()
{
    return {
        {"demo.empty", {}, [](const std::string& config) {
            if (config == "{}") return Result<void>::success();
            return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "demo.empty config must be an empty mapping"));
        }},
        {"demo.profile", {{"store", 1, 2, {"read"}}}, [](const std::string& config) {
            if (config == "\"work\"" || config == "\"personal\"") return Result<void>::success();
            return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "demo.profile config must be work or personal"));
        }}
    };
}
/** @brief 源文件读取、解析和定义校验失败都带文件名报告，不使用部分候选。 */
Result<ValidatedCandidate> load(const std::string& path)
{
    const auto policy = YamlLimits{};
    auto text = readFile(path, policy.maximumInputBytes);
    if (!text.hasValue()) return Result<ValidatedCandidate>::failure(text.error());
    auto parsed = parseConfiguration(text.value(), policy);
    if (!parsed.hasValue()) {
        auto error = parsed.error();
        error.message = path + ": " + error.message;
        return Result<ValidatedCandidate>::failure(std::move(error));
    }
    auto result = validateCandidate(std::move(parsed).value(), catalog());
    if (!result.hasValue()) {
        auto error = result.error();
        error.message = path + ": " + error.message;
        for (const auto& cause : error.causes) error.message += "; " + cause.message;
        return Result<ValidatedCandidate>::failure(std::move(error));
    }
    return result;
}
/** @brief 动作名用于命令行预览，不表示操作已执行。 */
const char* name(ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::NoOp: return "NoOp";
    case ChangeKind::Add: return "Add";
    case ChangeKind::Remove: return "Remove";
    case ChangeKind::Disable: return "Disable";
    case ChangeKind::Restart: return "Restart";
    case ChangeKind::Recreate: return "Recreate";
    }
    return "Unknown";
}
} // namespace
/** @brief 用法为配置预览程序加基线文件和候选文件；所有输出均是只读预览。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 3) { std::cerr << "usage: cordis_configuration_demo BASE.yaml CANDIDATE.yaml\n"; return EXIT_FAILURE; }
        auto base = load(argv[1]);
        if (!base.hasValue()) { std::cerr << base.error().message << '\n'; return EXIT_FAILURE; }
        auto candidate = load(argv[2]);
        if (!candidate.hasValue()) { std::cerr << candidate.error().message << '\n'; return EXIT_FAILURE; }
        const auto preview = previewChanges(base.value().source().structure(), candidate.value().source().structure());
        std::cout << "Validated builtin configuration preview; desiredChanged=" << preview.desiredChanged << '\n';
        for (const auto& item : preview.entries) std::cout << item.path << ": " << name(item.kind) << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
}
