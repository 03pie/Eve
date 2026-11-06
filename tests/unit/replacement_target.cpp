/** @file replacement_target.cpp
 * @brief 合并属性保留并行接受的子项、模块和精确数字，冲突不改原目标。
 */
#include "../../src/loader/replacement_target_p.hpp"
#include <iostream>
#include <stdexcept>
using namespace cordis;
using namespace cordis::loader;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(#x);                                                                              \
    } while (false)
std::string text(const char* business, const char* peer, const char* root, const char* child, const char* other)
{
    return std::string("formatVersion: 1\nmodules: {business: {manifest: ") + business + "}, peer: {manifest: " + peer +
           "}}\nplugins: [{id: root, plugin: p, module: business, config: " + root +
           ", children: [{id: child, plugin: builtin, disabled: true, config: " + child +
           "}]}, {id: other, plugin: builtin, config: " + other + "}]\n";
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string mode = argv[1];
        auto before =
            parseConfiguration(text("old/module.json", "peer/old.json", "old", "old-child", "old-other")).value();
        auto proposed =
            parseConfiguration(text("next/module.json", "peer/old.json", "new", "old-child", "old-other")).value();
        const std::string precise = "{integer: 9007199254740993, fraction: 0.12345678901234567890123456789}";
        auto latest =
            parseConfiguration(text("old/module.json", "peer/next.json", "old", "edited-child", precise.c_str()))
                .value();
        const auto previousText = latest.normalizedYaml();
        if (mode == "merge") {
            auto merged = detail::mergeReplacementTarget(before, proposed, latest, {"root"}).value();
            const auto& entries = merged.structure().entries();
            CHECK(entries.at("root").config == "\"new\"");
            CHECK(entries.at("root/child").config == "\"edited-child\"" && entries.at("root/child").disabled);
            CHECK(entries.at("other").config == latest.structure().entries().at("other").config);
            CHECK(merged.normalizedYaml().find("9007199254740993") != std::string::npos);
            CHECK(merged.normalizedYaml().find("0.12345678901234567890123456789") != std::string::npos);
            CHECK(merged.structure().modules().at("business") == "next/module.json" &&
                  merged.structure().modules().at("peer") == "peer/next.json");
        } else if (mode == "conflict") {
            auto conflict = setEntryConfiguration(latest, "root", "external").value();
            auto rejected = detail::mergeReplacementTarget(before, proposed, conflict, {"root"});
            CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::ConfigConflict);
            auto wrongModule =
                parseConfiguration(text("third/module.json", "peer/next.json", "old", "edited-child", "old-other"))
                    .value();
            CHECK(!detail::mergeReplacementTarget(before, proposed, wrongModule, {"root"}).hasValue());
            CHECK(!detail::mergeReplacementTarget(before, proposed, latest, {}).hasValue());
        } else if (mode == "structure") {
            auto added = addConfigurationEntry(proposed, "extra", "builtin", "{}").value();
            CHECK(!detail::mergeReplacementTarget(before, added, latest, {"root"}).hasValue());
            auto accepted = addConfigurationEntry(latest, "extra", "builtin", "{v: 7}").value();
            auto merged = detail::mergeReplacementTarget(before, proposed, accepted, {"root"}).value();
            CHECK(merged.structure().entries().at("extra").config == "{\"v\":7}");
            YamlLimits limits;
            limits.maximumNormalizedBytes = 16;
            CHECK(!detail::mergeReplacementTarget(before, proposed, latest, {"root"}, limits).hasValue());
        } else
            CHECK(false);
        CHECK(latest.normalizedYaml() == previousText);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
