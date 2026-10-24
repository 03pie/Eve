/** @file config_yaml.cpp
 * @brief 验证真实 YAML 解析、资源预算、规范文本及内置插件 schema/依赖联合。
 */
#include <cordis/loader/yaml.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cordis;
using namespace cordis::loader;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
/** @brief 使用显式格式版本和条目列表包装单个 YAML 条目。 */
std::string document(const std::string& fields)
{
    return "formatVersion: 1\nplugins:\n- id: notes\n  plugin: org.notes\n" + fields;
}
/** @brief 无约束内置定义仅用于测试基础流程；生产定义应提供实际 schema。 */
PluginDefinition definition()
{
    return {"org.notes", {}, [](const std::string&) { return Result<void>::success(); }};
}
/** @brief 解析错误必须携带配置错误码、阶段和一基行列。 */
void rejected(const std::string& text)
{
    auto result = parseConfiguration(text);
    CHECK(!result.hasValue() && result.error().code == ErrorCode::ConfigInvalid);
    CHECK(result.error().phase == "loader.yaml" || result.error().phase == "loader.structure");
    CHECK(result.error().message.find("line ") != std::string::npos && result.error().message.find("column ") != std::string::npos);
}
/** @brief 块/流式语法、中文和注释均可解析；结果完全转为宿主值。 */
void parsing()
{
    const auto text = document("  config: {title: '中文笔记', count: 2, flags: [true, false, null, yes]}\n"
        "  inject: [store/v1, {name: ui.pages, major: 1, minimumMinor: 2, capabilities: [write]}]\n"
        "  realms: {store: private}\n  children:\n  - {id: child, plugin: org.notes, disabled: true}\n");
    auto parsed = parseConfiguration(text).value();
    const auto& value = parsed.structure().entries().at("notes");
    CHECK(value.config == "{\"count\":2,\"flags\":[true,false,null,\"yes\"],\"title\":\"中文笔记\"}");
    CHECK(value.inject.size() == 2 && value.inject[0].name == "store" && value.realms.at("store") == "private");
    CHECK(parsed.structure().entries().at("notes/child").effectiveDisabled);
    CHECK(parsed.positions().at("notes").line == 3 && parsed.positions().at("notes/child").line == 9);
    auto checked = validateCandidate(parsed, {definition()});
    CHECK(checked.hasValue() && checked.value().source().structure().entries().size() == 2);
    CHECK(parseConfiguration("%YAML 1.2\n---\nformatVersion: 1\nplugins: []\n").hasValue());
    CHECK(parseConfiguration("# empty target\nformatVersion: 1\nplugins: []\n").value().structure().entries().empty());
}
/** @brief 重复键包含负载和转义同名，禁止静默覆盖，也不支持非字符串映射键。 */
void mappings()
{
    rejected("formatVersion: 1\nformatVersion: 1\nplugins: []\n");
    rejected(document("  config: {same: 1, same: 2}\n"));
    rejected(document("  config: {\"same\": 1, \"s\\u0061me\": 2}\n"));
    rejected(document("  config: {[a, b]: 1}\n"));
    rejected(document("  config: {true: 1}\n"));
    rejected(document("  config: {<<: {a: 1}}\n"));
    CHECK(parseConfiguration(document("  config: {'': 1, 'true': 'false'}\n")).value().structure().entries().at("notes").config ==
          "{\"\":1,\"true\":\"false\"}");
}
/** @brief 单文档策略拒绝 alias、anchor、显式 tag、指令及语法/编码错误。 */
void restricted()
{
    rejected(document("  config: &value {a: 1}\n"));
    rejected(document("  config: *missing\n"));
    rejected(document("  config: !custom value\n"));
    rejected(document("  config: !!str value\n"));
    rejected(document("  config: !!js/function 'x'\n"));
    rejected("%TAG !e! tag:example.com,2000:app/\n---\nformatVersion: 1\nplugins: []\n");
    rejected("%YAML 1.1\n---\nformatVersion: 1\nplugins: []\n");
    rejected("formatVersion: 1\nplugins: []\n---\nformatVersion: 1\nplugins: []\n");
    rejected(document("  config: [unclosed\n"));
    rejected(document("  config: \"") + std::string(1, static_cast<char>(0xff)) + "\"\n");
    rejected(std::string("formatVersion: 1\0plugins: []", 28));
    rejected("");
    rejected("---\n");
}
/** @brief 结构字段不做宽松转换，未知字段和漏填不能被插件 config 的自由结构掩盖。 */
void types()
{
    for (const auto& text : {"yes", "on", "1", "null", "'false'", "True"}) rejected(document("  disabled: " + std::string(text) + "\n"));
    CHECK(parseConfiguration(document("  disabled: false\n")).hasValue());
    rejected("formatVersion: '1'\nplugins: []\n");
    rejected("formatVersion: 1.0\nplugins: []\n");
    rejected("formatVersion: 4294967296\nplugins: []\n");
    rejected("formatVersion: 1\nplugins: null\n");
    rejected("formatVersion: 1\n");
    rejected("formatVersion: 1\nplugins: []\nunknown: true\n");
    rejected(document("  typo: true\n"));
    rejected(document("  inject: [{name: store, major: '1'}]\n"));
    rejected(document("  inject: [{name: store, minimumMinor: -1}]\n"));
    rejected(document("  inject: [{name: store, other: true}]\n"));
    rejected(document("  inject: [store/v0]\n"));
    rejected(document("  inject: [store/v01]\n"));
    rejected(document("  realms: {store: null}\n"));
    rejected("formatVersion: 1\nmodules: {m: {manifest: a.json, unknown: 1}}\nplugins: []\n");
}
/** @brief 每种独立预算在超限时拒绝，在精确输入边界仍成功。 */
void limits()
{
    const std::string text = "formatVersion: 1\nplugins: []\n";
    auto policy = YamlLimits{};
    policy.maximumInputBytes = text.size();
    CHECK(parseConfiguration(text, policy).hasValue());
    --policy.maximumInputBytes;
    CHECK(!parseConfiguration(text, policy).hasValue());
    policy = {};
    policy.maximumNodes = 1;
    CHECK(!parseConfiguration(text, policy).hasValue());
    policy = {};
    policy.maximumDepth = 2;
    CHECK(!parseConfiguration(document("  config: [[[]]]\n"), policy).hasValue());
    policy = {};
    policy.maximumDepth = 129;
    CHECK(!parseConfiguration(text, policy).hasValue());
    policy = {};
    policy.maximumScalarBytes = 2;
    CHECK(!parseConfiguration(text, policy).hasValue());
    policy = {};
    policy.maximumNormalizedBytes = 2;
    CHECK(!parseConfiguration(text, policy).hasValue());
    policy = {};
    policy.structure.maximumEntries = 1;
    CHECK(!parseConfiguration(document("  children: [{id: child, plugin: org.notes}]\n"), policy).hasValue());
}
/** @brief 规范输出可重新解析，映射顺序不触发重启，标量类别与转义保持。 */
void roundtrip()
{
    const auto first = parseConfiguration(document("  config: {b: \"a\\n\\u0000\\\"\\\\\", a: [1.5e2, 'true', false]}\n")).value();
    const auto second = parseConfiguration(first.normalizedYaml()).value();
    CHECK(first.normalizedYaml() == second.normalizedYaml());
    CHECK(!previewChanges(first.structure(), second.structure()).desiredChanged);
    const auto reordered = parseConfiguration(document("  config: {a: [1.5e2, 'true', false], b: \"a\\n\\u0000\\\"\\\\\"}\n")).value();
    CHECK(!previewChanges(first.structure(), reordered.structure()).desiredChanged);
    auto changed = parseConfiguration(document("  config: {a: [1.5e2, true, false], b: text}\n")).value();
    CHECK(previewChanges(first.structure(), changed.structure()).entries.front().kind == ChangeKind::Restart);
}
/** @brief schema 校验含禁用项；未知/重复定义、无 schema 或外部模块不发布结果。 */
void schemas()
{
    auto source = parseConfiguration(document("  config: {required: false}\n  disabled: true\n")).value();
    auto original = source;
    auto transferred = std::move(original);
    CHECK(!original.isValid() && transferred.isValid());
    CHECK(!validateCandidate(original, {definition()}).hasValue());
    CHECK(validateCandidate(transferred, {definition()}).hasValue());
    int called = 0;
    auto plugin = definition();
    plugin.validateConfig = [&](const std::string& config) {
        ++called;
        CHECK(config == "{\"required\":false}");
        return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "required must be true"));
    };
    auto result = validateCandidate(source, {plugin});
    CHECK(!result.hasValue() && called == 1 && result.error().phase == "loader.schema" && result.error().entryPath == "notes");
    CHECK(result.error().causes.size() == 1 && result.error().causes[0].message == "required must be true");
    plugin.validateConfig = [](const std::string&) -> Result<void> { throw std::runtime_error("schema exception"); };
    CHECK(validateCandidate(source, {plugin}).error().phase == "loader.schema");
    plugin.validateConfig = [](const std::string&) -> Result<void> { throw 7; };
    CHECK(!validateCandidate(source, {plugin}).hasValue());
    CHECK(!validateCandidate(source, {}).hasValue());
    CHECK(!validateCandidate(source, {definition(), definition()}).hasValue());
    plugin = definition(); plugin.validateConfig = {};
    CHECK(!validateCandidate(source, {plugin}).hasValue());
    plugin = definition(); plugin.requiredInject = {{"store", 0, 0, {}}};
    CHECK(!validateCandidate(source, {plugin}).hasValue());
    auto external = parseConfiguration("formatVersion: 1\nmodules: {m: {manifest: plugin.json}}\nplugins: []\n").value();
    CHECK(!validateCandidate(external, {}).hasValue());
    auto unknown = parseConfiguration("formatVersion: 1\nplugins: [{id: a, plugin: org.notes}, {id: z, plugin: missing}]\n").value();
    plugin = definition();
    called = 0;
    plugin.validateConfig = [&](const std::string&) { ++called; return Result<void>::success(); };
    CHECK(!validateCandidate(unknown, {plugin}).hasValue() && called == 0);
}
/** @brief 定义依赖始终保留，额外声明只增加约束，验证结果不保留 schema 捕获。 */
void dependencies()
{
    auto source = parseConfiguration(document("  inject: [{name: store, minimumMinor: 0, capabilities: [write]}, extra/v2]\n")).value();
    auto plugin = definition();
    plugin.requiredInject = {{"store", 1, 3, {"read"}}, {"always", 1, 0, {}}};
    auto payload = std::make_shared<int>(1);
    std::weak_ptr<int> weak = payload;
    plugin.validateConfig = [payload](const std::string&) { return Result<void>::success(); };
    auto result = validateCandidate(source, {plugin});
    CHECK(result.hasValue());
    plugin.validateConfig = {}; payload.reset();
    CHECK(weak.expired());
    const auto& inject = result.value().effectiveInject().at("notes");
    CHECK(inject.size() == 3 && inject[0].name == "always" && inject[1].name == "extra" && inject[1].major == 2);
    CHECK(inject[2].minimumMinor == 3 && inject[2].capabilities == std::vector<std::string>({"read", "write"}));
    CHECK(source.structure().entries().at("notes").inject[1].minimumMinor == 0);
}
/** @brief 每个行为独立登记，测试实际链接的 LibYAML C 库。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string name = argv[1];
        if (name == "parsing") parsing();
        else if (name == "mappings") mappings();
        else if (name == "restricted") restricted();
        else if (name == "types") types();
        else if (name == "limits") limits();
        else if (name == "roundtrip") roundtrip();
        else if (name == "schemas") schemas();
        else if (name == "dependencies") dependencies();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
