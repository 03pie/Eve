/** @file workspace_stress_trace.cpp
 * @brief 严格有限版本动作输入和原回执证据，不把 uint64 经 double 往返。
 */
#include "workspace_stress_trace_p.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <limits>
#include <set>

namespace workspace_stress_test {
namespace {
constexpr qint64 maximumTraceBytes = 16 * 1024 * 1024;

bool supportedGenerator(const QJsonValue& value)
{
    return value == QJsonValue("splitmix64/1") || value == QJsonValue("authored/1");
}

void fields(const QJsonObject& value, const QStringList& expected)
{
    if (value.size() != expected.size())
        throw InvalidTrace("unexpected trace fields");
    for (const auto& field : expected)
        if (!value.contains(field))
            throw InvalidTrace("missing trace field");
}
QJsonObject actionValue(const Action& action)
{
    validateAction(action);
    return {{"id", action.id}, {"kind", action.kind}, {"args", action.args}};
}
} // namespace

QString decimal(std::uint64_t value)
{
    return QString::number(static_cast<qulonglong>(value));
}
std::uint64_t parseDecimal(const QJsonValue& value)
{
    if (!value.isString())
        throw InvalidTrace("uint64 must be a decimal string");
    const auto text = value.toString();
    if (text.isEmpty() || (text.size() > 1 && text.front() == QLatin1Char('0')))
        throw InvalidTrace("noncanonical uint64");
    std::uint64_t result = 0;
    for (const auto character : text) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            throw InvalidTrace("nondecimal uint64");
        const auto digit = static_cast<std::uint64_t>(character.unicode() - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            throw InvalidTrace("uint64 overflow");
        result = result * 10 + digit;
    }
    return result;
}
QJsonObject error(const cordis::ErrorInfo& reason)
{
    QJsonArray causes;
    for (const auto& cause : reason.causes)
        causes.append(error(cause));
    QJsonObject result{{"code", decimal(static_cast<std::uint64_t>(reason.code))},
                       {"phase", QString::fromStdString(reason.phase)},
                       {"message", QString::fromStdString(reason.message)},
                       {"operation", decimal(reason.operation.value())},
                       {"causes", causes}};
    if (reason.instance)
        result.insert("instance", decimal(reason.instance->value()));
    if (reason.entryPath)
        result.insert("entry_path", QString::fromStdString(*reason.entryPath));
    return result;
}
QJsonArray artifactInputs(const QString& sourceBin)
{
    QJsonArray result;
    for (const auto& alias : {QStringLiteral("counter"), QStringLiteral("reporter"), QStringLiteral("notebook")}) {
        for (const auto& variant : {QStringLiteral("a"), QStringLiteral("b")}) {
            const auto directory = (alias == "notebook" ? "qt-upgrade/" : "native-upgrade/") + alias + '-' + variant;
            for (const auto& file :
                 {QStringLiteral("module.json"), "cordis_example_" + alias + '_' + variant + ".dll"}) {
                const auto relative = directory + '/' + file;
                QFile input(QDir(sourceBin).filePath(relative));
                if (!input.open(QIODevice::ReadOnly))
                    throw InvalidTrace("missing fixed artifact: " + relative.toStdString());
                QCryptographicHash hash(QCryptographicHash::Sha256);
                if (!hash.addData(&input))
                    throw InvalidTrace("cannot hash fixed artifact");
                result.append(
                    QJsonObject{{"relative_path", relative}, {"sha256", QString::fromLatin1(hash.result().toHex())}});
            }
        }
    }
    return result;
}
void saveTrace(const Trace& trace, const QString& path)
{
    parseDecimal(trace.seed);
    if (!supportedGenerator(trace.generator))
        throw InvalidTrace("unsupported action generator");
    QJsonArray actions;
    std::set<QString> identifiers;
    for (const auto& action : trace.actions) {
        parseDecimal(action.id);
        if (!identifiers.insert(action.id).second)
            throw InvalidTrace("duplicate action id");
        actions.append(actionValue(action));
    }
    if (trace.actions.empty() || trace.actions.size() > 64)
        throw InvalidTrace("trace action budget");
    const QJsonObject root{{"schema", "cordis.workspace.actions/1"},
                           {"generator", trace.generator},
                           {"seed", trace.seed},
                           {"artifacts", trace.artifacts},
                           {"actions", actions}};
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size() > maximumTraceBytes)
        throw InvalidTrace("trace byte budget");
    PLATFORM_CHECK(QDir().mkpath(QFileInfo(path).absolutePath()));
    QSaveFile output(path);
    PLATFORM_CHECK(output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit());
}
Trace loadTrace(const QString& path, const QString& sourceBin)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly) || input.size() > maximumTraceBytes)
        throw InvalidTrace("cannot read bounded trace");
    const auto bytes = input.readAll();
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        throw InvalidTrace("invalid JSON trace");
    // 只接收本工具的规范序列化：重复字段、替代转义和非规范数字不能被解析器折叠后接受。
    if (document.toJson(QJsonDocument::Indented) != bytes)
        throw InvalidTrace("trace is not in canonical tool format");
    const auto root = document.object();
    fields(root, {"actions", "artifacts", "generator", "schema", "seed"});
    if (root["schema"] != "cordis.workspace.actions/1" || !supportedGenerator(root["generator"]))
        throw InvalidTrace("unsupported action schema or generator");
    parseDecimal(root["seed"]);
    if (!root["actions"].isArray() || !root["artifacts"].isArray() ||
        root["artifacts"].toArray() != artifactInputs(sourceBin))
        throw InvalidTrace("changed artifact inputs or malformed action array");
    Trace trace{root["seed"].toString(), root["artifacts"].toArray(), {}};
    trace.generator = root["generator"].toString();
    std::set<QString> identifiers;
    const auto actions = root["actions"].toArray();
    if (actions.isEmpty() || actions.size() > 64)
        throw InvalidTrace("trace action budget");
    for (const auto& value : actions) {
        if (!value.isObject())
            throw InvalidTrace("action must be an object");
        const auto object = value.toObject();
        fields(object, {"args", "id", "kind"});
        parseDecimal(object["id"]);
        if (!object["kind"].isString() || !object["args"].isObject() ||
            !identifiers.insert(object["id"].toString()).second)
            throw InvalidTrace("invalid action envelope");
        Action action{object["id"].toString(), object["kind"].toString(), object["args"].toObject()};
        validateAction(action);
        trace.actions.push_back(std::move(action));
    }
    return trace;
}
Evidence::Evidence(const QString& path) : file_(path)
{
    PLATFORM_CHECK(QDir().mkpath(QFileInfo(path).absolutePath()) &&
                   file_.open(QIODevice::WriteOnly | QIODevice::Truncate));
}
void Evidence::append(QJsonObject event)
{
    event.insert("sequence", decimal(++sequence_));
    const auto bytes = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    PLATFORM_CHECK(file_.write(bytes) == bytes.size() && file_.flush());
}
} // namespace workspace_stress_test
