/** @file loader.cpp
 * @brief 验证配置结构边界、规范化、禁用级联及无副作用差异预览。
 */
#include <cordis/loader/configuration.hpp>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace cordis;
using namespace cordis::loader;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
/** @brief 创建最小内置插件条目，不引入任何内核实例。 */
EntrySpec entry(std::string id = "notes")
{
    EntrySpec result;
    result.id = std::move(id);
    result.plugin = "org.example.notes";
    return result;
}
/** @brief 根据规范路径查找预览行，缺失时使测试明确失败。 */
const EntryChange& change(const PlanPreview& preview, const std::string& path)
{
    for (const auto& value : preview.entries) if (value.path == path) return value;
    throw std::runtime_error("missing preview path " + path);
}
/** @brief 结构错误必须使用配置错误码和阶段，不能返回部分成功候选。 */
void rejected(const Document& document, const std::string& path = {})
{
    auto result = validateDocument(document);
    CHECK(!result.hasValue() && result.error().code == ErrorCode::ConfigInvalid && result.error().phase == "loader.structure");
    if (!path.empty()) CHECK(result.error().entryPath && *result.error().entryPath == path);
}
/** @brief 验证版本、模块、标识和兄弟重复；不同父节点下的同名子条目合法。 */
void structure()
{
    Document document;
    document.formatVersion = 2;
    rejected(document);
    document.formatVersion = 1;
    auto parent = entry("parent");
    parent.children = {entry("child"), entry("child")};
    document.plugins = {parent};
    rejected(document, "parent/child");
    parent.children.pop_back();
    auto other = parent;
    other.id = "other";
    document.plugins = {parent, other};
    auto candidate = validateDocument(document).value();
    CHECK(candidate.entries().size() == 4 && candidate.entries().count("other/child"));
    document.plugins[0].id = "../invalid";
    rejected(document);
    document.plugins = {entry()};
    document.plugins[0].module = "external";
    rejected(document, "notes");
    document.modules["external"] = "plugins/manifest.json";
    CHECK(validateDocument(document).hasValue());
    document.modules["builtin"] = "reserved.json";
    rejected(document);
    document.modules.erase("builtin");
    document.modules["external"] = std::string("path\0suffix", 11);
    rejected(document);
}
/** @brief 父禁用只影响有效状态，保留子条目自己的开关、原始配置及候选值隔离。 */
void disabled()
{
    auto root = entry("root");
    root.disabled = true;
    auto explicitlyDisabled = entry("off");
    explicitlyDisabled.disabled = true;
    root.children = {entry("child"), explicitlyDisabled};
    Document document;
    document.plugins = {root};
    auto candidate = validateDocument(document).value();
    CHECK(candidate.entries().at("root").disabled);
    const auto& child = candidate.entries().at("root/child");
    CHECK(child.effectiveDisabled && !child.disabled);
    document.plugins[0].children[0].config = "edited after validation";
    CHECK(child.config.empty());
    auto preview = previewChanges(validateDocument(Document{}).value(), candidate, 7);
    CHECK(preview.baseDesiredRevision == 7 && preview.desiredChanged && !preview.modulesChanged);
    CHECK(change(preview, "root/child").kind == ChangeKind::NoOp && change(preview, "root/child").desiredChanged);
    document.plugins[0].disabled = false;
    const auto enabled = validateDocument(document).value();
    preview = previewChanges(candidate, enabled);
    CHECK(change(preview, "root").kind == ChangeKind::Add && change(preview, "root/child").kind == ChangeKind::Add);
    CHECK(change(preview, "root/off").kind == ChangeKind::NoOp && enabled.entries().at("root/off").effectiveDisabled);
}
/** @brief 校验条目、深度、字段字节和依赖数量限额，边界值成功，超限返回配置错误。 */
void limits()
{
    Document document;
    document.plugins = {entry("a")};
    auto budget = ConfigurationLimits{};
    budget.maximumEntries = 1;
    CHECK(validateDocument(document, budget).hasValue());
    document.plugins[0].children = {entry("b")};
    CHECK(!validateDocument(document, budget).hasValue());
    budget.maximumEntries = 2;
    budget.maximumDepth = 1;
    CHECK(!validateDocument(document, budget).hasValue());
    budget.maximumDepth = 2;
    CHECK(validateDocument(document, budget).hasValue());
    budget.maximumDepth = 129;
    CHECK(!validateDocument(document, budget).hasValue());
    budget = {};
    document.plugins[0].children.clear();
    const auto& value = document.plugins[0];
    budget.maximumBytes = value.id.size() + value.plugin.size() + value.module.size();
    CHECK(validateDocument(document, budget).hasValue());
    --budget.maximumBytes;
    CHECK(!validateDocument(document, budget).hasValue());
    budget = {};
    budget.maximumInjects = 0;
    document.plugins[0].inject = {{"store", 1, 0, {}}};
    CHECK(!validateDocument(document, budget).hasValue());
    budget = {};
    budget.maximumCapabilities = 1;
    document.plugins[0].inject[0].capabilities = {"read", "write"};
    CHECK(!validateDocument(document, budget).hasValue());
    budget = {};
    budget.maximumModules = 0;
    document.modules["external"] = "manifest.json";
    CHECK(!validateDocument(document, budget).hasValue());
}
/** @brief 输入排列和重复 capability 不造成重建；重复接口键及无效声明拒绝。 */
void normalization()
{
    Document before;
    before.plugins = {entry()};
    before.plugins[0].inject = {{"store", 1, 2, {"write", "read", "write"}}, {"ui.pages", 1, 0, {}}};
    auto after = before;
    after.plugins[0].inject = {{"ui.pages", 1, 0, {}}, {"store", 1, 2, {"read", "write"}}};
    auto preview = previewChanges(validateDocument(before).value(), validateDocument(after).value());
    CHECK(!preview.desiredChanged && change(preview, "notes").kind == ChangeKind::NoOp);
    after.plugins[0].inject.push_back({"store", 1, 3, {}});
    rejected(after, "notes");
    after.plugins[0].inject.pop_back();
    after.plugins[0].inject[0].major = 0;
    rejected(after, "notes");
    after.plugins[0].inject.clear();
    after.plugins[0].realms["store"] = "../realm";
    rejected(after, "notes");
}
/** @brief 新增、删除、开关、配置重启与定义重建互相区分，预览不依赖声明顺序。 */
void actions()
{
    Document before;
    before.plugins = {entry("remove"), entry("disable"), entry("enable"), entry("restart"), entry("recreate"), entry("same")};
    before.plugins[2].disabled = true;
    auto after = before;
    after.plugins.erase(after.plugins.begin());
    after.plugins[0].disabled = true;
    after.plugins[1].disabled = false;
    after.plugins[2].config = "new config";
    after.plugins[3].plugin = "org.example.replacement";
    after.plugins.push_back(entry("add"));
    const auto base = validateDocument(before).value();
    const auto candidate = validateDocument(after).value();
    auto preview = previewChanges(base, candidate, 42);
    CHECK(preview.baseDesiredRevision == 42 && preview.entries.size() == 7 && preview.desiredChanged);
    CHECK(change(preview, "remove").kind == ChangeKind::Remove);
    CHECK(change(preview, "disable").kind == ChangeKind::Disable);
    CHECK(change(preview, "enable").kind == ChangeKind::Add);
    CHECK(change(preview, "restart").kind == ChangeKind::Restart);
    CHECK(change(preview, "recreate").kind == ChangeKind::Recreate);
    CHECK(change(preview, "same").kind == ChangeKind::NoOp && !change(preview, "same").desiredChanged);
    CHECK(change(preview, "add").kind == ChangeKind::Add);
    CHECK(preview.entries.front().path == "add" && preview.entries.back().path == "same");
    CHECK(base.entries().at("restart").config.empty() && candidate.entries().at("restart").config == "new config");
}
/** @brief 父代重启隐含启用子树重建，精确祖先匹配不影响名称前缀相同的独立根。 */
void subtree()
{
    Document before;
    auto parent = entry("a");
    auto child = entry("child");
    child.children = {entry("grandchild")};
    parent.children = {child};
    before.plugins = {parent, entry("ab")};
    auto after = before;
    after.plugins[0].config = "restart parent";
    auto preview = previewChanges(validateDocument(before).value(), validateDocument(after).value());
    CHECK(change(preview, "a").kind == ChangeKind::Restart);
    CHECK(change(preview, "a/child").kind == ChangeKind::Recreate && !change(preview, "a/child").desiredChanged);
    CHECK(change(preview, "a/child/grandchild").kind == ChangeKind::Recreate);
    CHECK(change(preview, "ab").kind == ChangeKind::NoOp);
    after.plugins[0].disabled = true;
    preview = previewChanges(validateDocument(before).value(), validateDocument(after).value());
    CHECK(change(preview, "a/child").kind == ChangeKind::Disable);
    CHECK(!after.plugins[0].children[0].disabled);
}
/** @brief manifest、realm 和 inject 变化使启用条目重建，未使用模块变化仍属于目标变化。 */
void definitions()
{
    Document before;
    before.modules["external"] = "old.json";
    before.plugins = {entry()};
    before.plugins[0].module = "external";
    auto after = before;
    after.modules["external"] = "new.json";
    auto preview = previewChanges(validateDocument(before).value(), validateDocument(after).value());
    CHECK(preview.modulesChanged && change(preview, "notes").kind == ChangeKind::Recreate);
    after = before;
    after.plugins[0].realms["store"] = "private";
    preview = previewChanges(validateDocument(before).value(), validateDocument(after).value());
    CHECK(change(preview, "notes").kind == ChangeKind::Recreate);
    after = before;
    after.plugins[0].inject = {{"store", 1, 1, {"write"}}};
    preview = previewChanges(validateDocument(before).value(), validateDocument(after).value());
    CHECK(change(preview, "notes").kind == ChangeKind::Recreate);
    before.plugins.clear();
    after = before;
    after.modules["external"] = "unused-but-changed.json";
    preview = previewChanges(validateDocument(before).value(), validateDocument(after).value());
    CHECK(preview.entries.empty() && preview.modulesChanged && preview.desiredChanged);
}
/** @brief 独立无 Qt 场景由 CTest 分别登记。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string name = argv[1];
        if (name == "structure") structure();
        else if (name == "disabled") disabled();
        else if (name == "limits") limits();
        else if (name == "normalization") normalization();
        else if (name == "actions") actions();
        else if (name == "subtree") subtree();
        else if (name == "definitions") definitions();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
