/** @file workspace_stress_generate.cpp
 * @brief 固定覆盖骨架加有界合法随机尾部；生成完成后执行器仅消费保存动作。
 */
#include "workspace_stress_trace_p.hpp"
#include <QJsonDocument>

namespace workspace_stress_test {
namespace {
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next()
    {
        auto value = (state_ += UINT64_C(0x9e3779b97f4a7c15));
        value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
        return value ^ (value >> 31);
    }

private:
    std::uint64_t state_;
};
QString configuration(const QString& text)
{
    // 这里只生成字符串配置；withdraw/realm 的既有数字片段由动作层原样保留。
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"title", "Stress notebook"}, {"text", text}}).toJson(QJsonDocument::Compact));
}
} // namespace

Trace generateTrace(const QString& sourceBin, const QString& seed)
{
    SplitMix64 random(parseDecimal(seed));
    Trace trace{seed, artifactInputs(sourceBin), {}};
    const auto add = [&](const QString& kind, QJsonObject args = {}) {
        Action action{decimal(trace.actions.size() + 1), kind, std::move(args)};
        validateAction(action);
        trace.actions.push_back(std::move(action));
    };
    const auto config = [&] { return configuration("seed=" + seed + "; draw=" + decimal(random.next())); };
    add("mount", {{"id", "scratch"}, {"configuration", config()}});
    add("configure", {{"path", "scratch"}, {"configuration", config()}});
    add("enable", {{"path", "scratch"}, {"enabled", false}});
    add("enable", {{"path", "scratch"}, {"enabled", true}});
    add("withdraw", {{"path", "scratch"}});
    add("enable", {{"path", "counter"}, {"enabled", false}});
    add("enable", {{"path", "counter"}, {"enabled", true}});
    add("realmPair", {{"present", true}});
    add("configure", {{"path", "notes"}, {"configuration", config()}});
    add("realmPair", {{"present", false}});
    add("nativeUpgrade", {{"alias", "counter"}, {"variant", "b"}});
    add("nativeUpgrade", {{"alias", "reporter"}, {"variant", "b"}});
    add("nativeUpgrade", {{"alias", "counter"}, {"variant", "a"}});
    add("nativeUpgrade", {{"alias", "reporter"}, {"variant", "a"}});
    add("preparedQueuedCancel",
        {{"variant", "b"}, {"path", "notes"}, {"firstConfiguration", config()}, {"secondConfiguration", config()}});
    add("save");
    add("configure", {{"path", "notes"}, {"configuration", config()}});
    add("saveConflictRebase", {{"text", "# external writer seed=" + seed + "\nformatVersion: 1\nplugins: []\n"}});
    bool exists = false, enabled = false;
    for (unsigned index = 0; index < 12; ++index) {
        const auto choice = random.next() % 4;
        if (choice == 0) {
            add("configure", {{"path", "notes"}, {"configuration", config()}});
        } else if (!exists) {
            add("mount", {{"id", "random"}, {"configuration", config()}});
            exists = enabled = true;
        } else if (choice == 1) {
            add("withdraw", {{"path", "random"}});
            exists = enabled = false;
        } else if (choice == 2) {
            enabled = !enabled;
            add("enable", {{"path", "random"}, {"enabled", enabled}});
        } else {
            add("configure", {{"path", "random"}, {"configuration", config()}});
        }
    }
    if (exists)
        add("withdraw", {{"path", "random"}});
    add("draft", {{"path", "notes"}, {"text", "final draft seed=" + seed + " \"quoted\" 😀"}});
    add(seed == "18446744073709551615" ? "buildingCancelNotebook" : "notebookUpgrade", {{"variant", "b"}});
    add("save");
    return trace;
}
} // namespace workspace_stress_test
