/** @file log_query.cpp
 * @brief 离线查询工作区运行日志。标准输出承载查询结果，标准错误和退出码报告归档或查询异常。
 */
#include "runtime_log_query_p.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>
#include <stdexcept>

namespace {
using namespace eve;
using namespace eve::ui::detail;

quint64 positiveId(const QCommandLineParser& parser, const char* name)
{
    bool valid = false;
    const auto id = parser.value(name).toULongLong(&valid);
    if (!valid || !id)
        throw std::invalid_argument(std::string("Expected a positive integer for --") + name);
    return id;
}
qint64 utcTime(const QCommandLineParser& parser, const char* name)
{
    const auto value = QDateTime::fromString(parser.value(name), Qt::ISODateWithMs);
    if (!value.isValid() || value.timeSpec() == Qt::LocalTime)
        throw std::invalid_argument(std::string("Expected an ISO 8601 time with an offset for --") + name);
    return value.toMSecsSinceEpoch();
}
LogFilter selectedFilter(const QCommandLineParser& parser)
{
    LogFilter filter;
    filter.sourceId = parser.value("source");
    filter.moduleId = parser.value("module");
    filter.moduleVersion = parser.value("module-version");
    filter.instanceKey = parser.value("instance-key");
    filter.channel = parser.value("channel");
    filter.text = parser.value("text");
    if (parser.isSet("instance"))
        filter.instance = InstanceId{positiveId(parser, "instance")};
    if (parser.isSet("epoch"))
        filter.epoch = positiveId(parser, "epoch");
    if (parser.isSet("from"))
        filter.fromUtcMs = utcTime(parser, "from");
    if (parser.isSet("through"))
        filter.throughUtcMs = utcTime(parser, "through");
    if (filter.fromUtcMs && filter.throughUtcMs && *filter.throughUtcMs < *filter.fromUtcMs)
        throw std::invalid_argument("--through precedes --from");
    if (parser.isSet("level")) {
        const auto level = parser.value("level").toLower();
        const std::array<const char*, 5> names{"trace", "debug", "info", "warning", "error"};
        const auto found = std::find(names.begin(), names.end(), level);
        if (found == names.end())
            throw std::invalid_argument("Expected trace, debug, info, warning or error for --level");
        filter.minimumSeverity = static_cast<LogSeverity>(std::distance(names.begin(), found) - 2);
    }
    return filter;
}
bool emitLine(QFile& output, const QString& line)
{
    const auto utf8 = line.toUtf8() + '\n';
    return output.write(utf8) == utf8.size();
}
}

int main(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Read an existing Eve runtime archive.");
    parser.addHelpOption();
    parser.addOptions({
        {{"w", "workspace"}, "Existing workspace directory.", "directory"},
        {{"r", "run"}, "Run UUID. Omit to list runs.", "uuid"},
        {"sources", "List recorded sources in the selected run."},
        {"event", "Locate the exact accepted event in the selected run.", "id"},
        {"source", "Filter by source ID.", "id"},
        {"module", "Filter by known module ID.", "id"},
        {"channel", "Filter by logical channel.", "channel"},
        {"instance", "Filter by instance ID.", "id"},
        {"instance-key", "Filter by known configuration instance key.", "key"},
        {"epoch", "Filter by instance epoch.", "number"},
        {"module-version", "Filter by known module version.", "version"},
        {"level", "Minimum level: trace, debug, info, warning or error.", "level"},
        {"text", "Filter by message text.", "substring"},
        {"from", "Lower time bound, inclusive.", "ISO8601"},
        {"through", "Upper time bound, inclusive.", "ISO8601"}
    });
    parser.process(app);
    try {
        const auto workspace = parser.value("workspace");
        const auto run = parser.value("run");
        if (workspace.isEmpty() || !QDir(workspace).exists() || !parser.positionalArguments().isEmpty())
            throw std::invalid_argument("Specify an existing workspace with --workspace.");
        if ((run.isEmpty() && (parser.isSet("sources") || parser.isSet("event"))) ||
            (parser.isSet("sources") && parser.isSet("event")))
            throw std::invalid_argument("Use either --sources or --event with --run.");
        const std::array<const char*, 11> filters{"source", "module", "channel", "instance", "instance-key", "epoch",
                                                "module-version", "level", "text", "from", "through"};
        if ((run.isEmpty() || parser.isSet("event") || parser.isSet("sources")) &&
            std::any_of(filters.begin(), filters.end(), [&](const char* option) { return parser.isSet(option); }))
            throw std::invalid_argument("Filters require a run in export mode.");
        const LogQuery query(QDir(workspace).absolutePath());
        QFile output;
        if (!output.open(stdout, QIODevice::WriteOnly))
            return EXIT_FAILURE;
        if (run.isEmpty()) {
            for (const auto& item : query.runs())
                if (!emitLine(output, item.id + '\t' + item.label))
                    return EXIT_FAILURE;
        } else if (savedLogDirectory(workspace, run).isEmpty()) {
            throw std::invalid_argument("The specified run is unavailable.");
        } else if (parser.isSet("sources")) {
            const auto catalog = query.sources(run);
            for (auto source = catalog.sources.cbegin(); source != catalog.sources.cend(); ++source)
                if (!emitLine(output, source.key() + '\t' + source->name + '\t' +
                                          QString("%1-%2").arg(source->firstEvent).arg(source->lastEvent)))
                    return EXIT_FAILURE;
            if (!catalog.warning.isEmpty()) {
                std::fprintf(stderr, "%s\n", catalog.warning.toUtf8().constData());
                return EXIT_FAILURE;
            }
        } else if (parser.isSet("event")) {
            const auto event = query.locate(run, positiveId(parser, "event"));
            if (!event.record) {
                if (event.status == LogLookupStatus::Expired) {
                    std::fputs("Event expired.\n", stderr);
                    return 3;
                }
                if (event.status == LogLookupStatus::Unavailable) {
                    const auto reason = event.warning.isEmpty() ? QByteArray("Event unavailable.") : event.warning.toUtf8();
                    std::fprintf(stderr, "%s\n", reason.constData());
                    return 4;
                }
                std::fputs("Event not found.\n", stderr);
                return 2;
            }
            if (!emitLine(output, QString::number(event.record->eventId) + '\t' + formatLogText(*event.record)))
                return EXIT_FAILURE;
        } else {
            QString error;
            if (!query.exportText(query.begin(run, selectedFilter(parser)), output, error)) {
                std::fprintf(stderr, "%s\n", error.toUtf8().constData());
                return EXIT_FAILURE;
            }
        }
        return output.flush() ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }
}
