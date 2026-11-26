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
/** @brief 解析、条目编辑和清单编辑均遵守事件树与条目树各自的深度预算。 */
void depthLimits()
{
    const std::string text = "formatVersion: 1\nmodules: {m: {manifest: a}}\nplugins: [{id: notes, plugin: org.notes}]\n";
    const auto source = parseConfiguration(text).value();
    for (std::size_t depth : {0u, 128u, 129u}) {
        YamlLimits events, entries;
        events.maximumDepth = depth;
        entries.structure.maximumDepth = depth;
        for (const auto& policy : {events, entries}) {
            const auto verify = [depth](const Result<ParsedConfiguration>& result) {
                CHECK(result.hasValue() == (depth == 128));
                if (!result.hasValue()) {
                    CHECK(result.error().code == ErrorCode::ConfigInvalid);
                    CHECK(result.error().phase == "loader.yaml");
                }
            };
            verify(parseConfiguration(text, policy));
            verify(setEntryConfiguration(source, "notes", "{}", policy));
            verify(addConfigurationEntry(source, "extra", "org.notes", "{}", policy));
            verify(setEntryEnabled(source, "notes", false, policy));
            verify(setModuleManifest(source, "m", "next.json", policy));
        }
    }

    // 根映射、条目列表和条目映射占三层。再嵌套 124 层列表时，最深标量在第 128 层。
    const auto payload = std::string(124, '[') + "0" + std::string(124, ']');
    YamlLimits policy;
    policy.maximumDepth = 128;
    CHECK(parseConfiguration(document("  config: " + payload + "\n"), policy).hasValue());
    CHECK(!parseConfiguration(document("  config: [" + payload + "]\n"), policy).hasValue());
    CHECK(setEntryConfiguration(source, "notes", payload, policy).hasValue());
    CHECK(!setEntryConfiguration(source, "notes", "[" + payload + "]", policy).hasValue());
}
/** @brief 每种独立预算在超限时拒绝，在精确输入边界仍成功。 */
void limits()
{
    depthLimits();
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
/** @brief 模块编辑与自由配置共享原值树，不能经浮点 DOM 丢失数字或误改同名负载。 */
ParsedConfiguration moduleSource()
{
    return parseConfiguration(
               "formatVersion: 1\nmodules: {m: {manifest: old/module.json}, other: {manifest: other/module.json}}\n"
               "plugins:\n  - id: root\n    plugin: org.notes\n    module: m\n    disabled: true\n"
               "    inject: [store/v1, {name: ui.pages, major: 1, minimumMinor: 2, capabilities: [read, write]}]\n"
               "    realms: {store: private}\n"
               "    config: {above: 9007199254740993, maximum: 18446744073709551615, "
               "decimal: 1.234567890123456789012345678901234567890e+0042, locator: old/module.json}\n"
               "    children:\n      - {id: child, plugin: org.notes, module: other, "
               "config: [18446744073709551615, 9007199254740993, 0.000000000000000000001e-0099]}\n")
        .value();
}
void moduleManifest()
{
    const auto source = moduleSource();
    const auto original = source.normalizedYaml();
    const auto changed = setModuleManifest(source, "m", "hash/module.json").value();
    CHECK(changed.structure().modules().at("m") == "hash/module.json");
    CHECK(changed.structure().modules().at("other") == "other/module.json");
    CHECK(changed.structure().entries().size() == source.structure().entries().size());
    for (const auto& entry : source.structure().entries()) {
        const auto& before = entry.second;
        const auto& after = changed.structure().entries().at(entry.first);
        CHECK(before.plugin == after.plugin && before.module == after.module && before.config == after.config &&
              before.disabled == after.disabled && before.effectiveDisabled == after.effectiveDisabled &&
              before.realms == after.realms && before.inject.size() == after.inject.size());
        for (std::size_t i = 0; i != before.inject.size(); ++i)
            CHECK(before.inject[i].name == after.inject[i].name && before.inject[i].major == after.inject[i].major &&
                  before.inject[i].minimumMinor == after.inject[i].minimumMinor &&
                  before.inject[i].capabilities == after.inject[i].capabilities);
    }
    CHECK(changed.structure().entries().at("root").config ==
          "{\"above\":9007199254740993,\"decimal\":1.234567890123456789012345678901234567890e+0042,"
          "\"locator\":\"old/module.json\",\"maximum\":18446744073709551615}");
    CHECK(changed.structure().entries().at("root/child").config ==
          "[18446744073709551615,9007199254740993,0.000000000000000000001e-0099]");
    CHECK(source.normalizedYaml() == original && source.structure().modules().at("m") == "old/module.json");
    CHECK(setModuleManifest(source, "m", "old/module.json").value().normalizedYaml() == original);
    for (const std::string manifest : {"true", "null", "9007199254740993", "[a, b]", "插件/\"next\"\\module.json"}) {
        const auto literal = setModuleManifest(source, "m", manifest).value();
        CHECK(literal.structure().modules().at("m") == manifest);
        CHECK(literal.structure().entries().at("root").config == source.structure().entries().at("root").config);
        CHECK(parseConfiguration(literal.normalizedYaml()).value().normalizedYaml() == literal.normalizedYaml());
    }
}
/** @brief 未知身份或失效来源不新增声明，非法 locator 沿原解析/结构校验拒绝。 */
void moduleManifestRejected()
{
    auto source = moduleSource();
    for (const auto* alias : {"", "builtin", "missing"}) {
        const auto result = setModuleManifest(source, alias, "next.json");
        CHECK(!result.hasValue() && result.error().code == ErrorCode::InvalidArgument);
    }
    const auto withoutModules = parseConfiguration("formatVersion: 1\nplugins: []\n").value();
    CHECK(!setModuleManifest(withoutModules, "m", "next.json").hasValue());
    const auto moved = std::move(source);
    CHECK(!source.isValid() && moved.isValid());
    CHECK(setModuleManifest(source, "m", "next.json").error().code == ErrorCode::InvalidArgument);
    CHECK(setModuleManifest(moved, "m", "next.json").hasValue());
    CHECK(setModuleManifest(moved, "m", std::string(4096, 'x')).hasValue());
    const std::vector<std::string> invalid{"",
                                           std::string(4097, 'x'),
                                           std::string("a\0b", 3),
                                           std::string(1, static_cast<char>(0xff)),
                                           std::string("\xc0\xaf", 2),
                                           std::string("\xed\xa0\x80", 3),
                                           std::string("\xf0\x9f", 2)};
    for (const auto& manifest : invalid) {
        const auto result = setModuleManifest(moved, "m", manifest);
        CHECK(!result.hasValue() && result.error().code == ErrorCode::ConfigInvalid);
    }
    CHECK(moved.structure().modules().at("m") == "old/module.json");
}
/** @brief 新字符串与原规范树分别受预算约束，输出替换后仍重新验证全部限额。 */
void moduleManifestLimits()
{
    const auto source = parseConfiguration("formatVersion: 1\nmodules: {m: {manifest: a}}\nplugins: []\n").value();
    const std::string manifest = "abcd";
    const auto changed = setModuleManifest(source, "m", manifest).value();
    auto policy = YamlLimits{};
    policy.maximumInputBytes = manifest.size();
    CHECK(setModuleManifest(source, "m", manifest, policy).hasValue());
    --policy.maximumInputBytes;
    CHECK(!setModuleManifest(source, "m", manifest, policy).hasValue());
    policy = {};
    policy.maximumNormalizedBytes = changed.normalizedYaml().size();
    CHECK(setModuleManifest(source, "m", manifest, policy).hasValue());
    --policy.maximumNormalizedBytes;
    CHECK(!setModuleManifest(source, "m", manifest, policy).hasValue());
    policy.maximumNormalizedBytes = source.normalizedYaml().size() - 1;
    CHECK(!setModuleManifest(source, "m", manifest, policy).hasValue());
    policy = {};
    policy.maximumScalarBytes = 38;
    CHECK(parseConfiguration(source.normalizedYaml(), policy).hasValue());
    CHECK(!setModuleManifest(source, "m", manifest, policy).hasValue());
    policy.maximumScalarBytes = 41;
    CHECK(setModuleManifest(source, "m", manifest, policy).hasValue());
    policy = {};
    policy.maximumNodes = 11;
    CHECK(setModuleManifest(source, "m", manifest, policy).hasValue());
    --policy.maximumNodes;
    CHECK(!setModuleManifest(source, "m", manifest, policy).hasValue());
    policy = {};
    policy.maximumDepth = 4;
    CHECK(setModuleManifest(source, "m", manifest, policy).hasValue());
    policy.maximumDepth = 3;
    CHECK(!setModuleManifest(source, "m", manifest, policy).hasValue());
    policy = {};
    policy.structure.maximumBytes = 1 + manifest.size();
    CHECK(setModuleManifest(source, "m", manifest, policy).hasValue());
    --policy.structure.maximumBytes;
    CHECK(!setModuleManifest(source, "m", manifest, policy).hasValue());
    policy = {};
    policy.structure.maximumModules = 0;
    CHECK(!setModuleManifest(source, "m", manifest, policy).hasValue());
    policy = {};
    policy.maximumInputBytes = 0;
    CHECK(!setModuleManifest(source, "m", manifest, policy).hasValue());
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
        else if (name == "module_manifest")
            moduleManifest();
        else if (name == "module_manifest_rejected")
            moduleManifestRejected();
        else if (name == "module_manifest_limits")
            moduleManifestLimits();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
