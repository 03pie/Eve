/** @file host.cpp
 * @brief 五个场景共用真实装配；运行器只负责输入、断言、打印和进程退出。
 */
#include "pipeline.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cordis/loader/qt_file.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace cordis;
using namespace cordis::module;
using namespace cordis::loader;
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class T> T take(Result<T> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().phase + ": " + result.error().message);
    return std::move(result).value();
}
void take(Result<void> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
}
template <class F> void guarded(F callback)
{
    try {
        callback();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        QCoreApplication::exit(EXIT_FAILURE);
    } catch (...) {
        std::cerr << "unexpected native service host exception\n";
        QCoreApplication::exit(EXIT_FAILURE);
    }
}
class Runner : public std::enable_shared_from_this<Runner> {
public:
    Runner(QString root, std::string mode, std::shared_ptr<cordis::qt::QtScheduler> scheduler)
        : root_(std::move(root)), mode_(std::move(mode))
    {
        require(mode_ == "normal" || mode_ == "multi" || mode_ == "realms" || mode_ == "start_failure" ||
                    mode_ == "cleanup_failure",
                "unknown scenario");
        require(scratch_.isValid(), "temporary session directory unavailable");
        path_ = scratch_.path() + "/session.yaml";
        pipeline_ = take(native_services::Pipeline::open(root_, path_, std::move(scheduler)));
    }
    void start()
    {
        auto text = take(readConfigurationFile(root_ + "/pipeline.yaml")).text;
        require(!text.empty(), "missing pipeline.yaml");
        if (mode_ == "cleanup_failure")
            replace(text, "module: counter, config: null", "module: counter, config: {failCleanup: true}");
        if (mode_ == "start_failure")
            replace(text, "module: reporter, config: null", "module: reporter, config: {failStart: true}");
        if (mode_ == "multi")
            text += "  - {id: reporter2, plugin: example.reporter, module: reporter, config: null, realms: "
                    "{example.report: second}}\n";
        if (mode_ == "realms")
            text += "  - {id: counter2, plugin: example.counter, module: counter, config: null, realms: "
                    "{example.counter: second}}\n"
                    "  - {id: reporter2, plugin: example.reporter, module: reporter, config: null, realms: "
                    "{example.counter: second, example.report: second}}\n";
        const auto draft = take(pipeline_->session()->preview(text));
        require(take(pipeline_->counter().session->snapshot()).instances.empty() &&
                    take(pipeline_->reporter().session->snapshot()).instances.empty(),
                "preview activated a DLL");
        const auto accepted = take(pipeline_->session()->apply(draft));
        const auto self = shared_from_this();
        watch(accepted.settled, [self, revision = accepted.revision](const Result<PlanReport>& result) {
            const auto report = take(result);
            require(report.outcome == (self->mode_ == "start_failure" ? PlanOutcome::Failed : PlanOutcome::Applied),
                    "unexpected plan outcome");
            require(!take(self->pipeline_->counter().session->snapshot()).faulted &&
                        !take(self->pipeline_->reporter().session->snapshot()).faulted,
                    "ordinary activation failure isolated a module");
            if (self->mode_ != "start_failure")
                self->checkBusiness();
            self->watch(take(self->pipeline_->session()->save(revision)), [self](const Result<SaveReport>& saved) {
                require(take(saved).outcome == SaveOutcome::Written, "session save failed");
                const auto disk = take(readConfigurationFile(self->path_));
                require(disk.version.exists && parseConfiguration(disk.text).hasValue(), "disk YAML invalid");
                const auto store = take(self->pipeline_->session()->snapshot()).store;
                require(!store.dirty && store.persisted && disk.text == store.persisted->source().normalizedYaml(),
                        "disk content differs from saved session");
                std::cout << "configuration committed to disk\n";
                self->closePipeline();
            });
        });
    }

private:
    static void replace(std::string& text, const std::string& from, const std::string& to)
    {
        const auto found = text.find(from);
        require(found != std::string::npos, "sample YAML changed unexpectedly");
        text.replace(found, from.size(), to);
    }
    template <class T, class F> void watch(const Task<T>& task, F callback)
    {
        task.thenOn(pipeline_->scheduler(),
                    [callback = std::move(callback)](const Result<T>& result) { guarded([&] { callback(result); }); });
    }
    void checkBusiness()
    {
        const auto state = take(pipeline_->reporter().session->snapshot());
        require(state.instances.size() == (mode_ == "multi" || mode_ == "realms" ? 2u : 1u),
                "incorrect reporter instance count");
        std::vector<std::string> values;
        for (const auto& instance : state.instances) {
            require(instance.state == NativeInstanceState::Started && instance.committedServices == 1,
                    "reporter not active with one borrowed input");
            for (const auto& log : instance.logs)
                values.push_back(log.text);
        }
        std::sort(values.begin(), values.end());
        const std::vector<std::string> expected =
            mode_ == "multi" ? std::vector<std::string>{"start result {\"value\":1}", "start result {\"value\":2}"}
                             : std::vector<std::string>(state.instances.size(), "start result {\"value\":1}");
        require(values == expected, "counter values do not match instance/realm isolation");
        for (const auto& value : values)
            std::cout << value << '\n';
    }
    void closePipeline()
    {
        const auto self = shared_from_this();
        watch(pipeline_->close(), [self](const Result<native_services::PipelineCloseReport>& result) {
            const auto report = take(result);
            require(report.session.hasValue() != (self->mode_ == "cleanup_failure"), "incorrect session close result");
            require(take(report.kernel).quiescent != (self->mode_ == "cleanup_failure"),
                    "incorrect kernel cleanup report");
            require(take(report.counter).quiescent != (self->mode_ == "cleanup_failure"),
                    "incorrect module close report");
            require(take(report.reporter).quiescent, "reporter cleanup did not quiesce");
            self->finish(report);
        });
    }
    void finish(const native_services::PipelineCloseReport& report)
    {
        if (mode_ != "cleanup_failure")
            require(take(report.remainingServices).empty(), "service keys survived successful close");
        if (!report.reporterDrain.hasValue()) {
            require(mode_ == "cleanup_failure" && report.reporterDrain.error().code == ErrorCode::ContextClosing,
                    "reporter references survived successful close");
            const auto states = take(pipeline_->manager()->snapshot());
            const auto reporter = std::find_if(states.begin(), states.end(), [&](const auto& value) {
                return value.generation == pipeline_->reporter().generation;
            });
            require(reporter != states.end() && !reporter->resources.records.empty() &&
                        std::all_of(reporter->resources.records.begin(), reporter->resources.records.end(),
                                    [](const auto& record) {
                                        return record.kind == ResourceKind::ExternalHandle &&
                                               record.label.rfind("runtime.definition.", 0) == 0;
                                    }),
                    "reporter retained business resources");
        }
        if (mode_ == "cleanup_failure") {
            require(!report.counterDrain.hasValue(), "failed cleanup falsely drained");
            const auto states = take(pipeline_->manager()->snapshot());
            const auto counter = std::find_if(states.begin(), states.end(), [&](const auto& value) {
                return value.generation == pipeline_->counter().generation;
            });
            require(counter != states.end() && counter->nativePinned && !counter->resources.records.empty(),
                    "failed DLL resources were not retained");
            std::cout << "failed cleanup retained DLL and execution references\n";
        } else {
            take(report.reporterDrain);
            take(report.counterDrain);
            for (const auto& state : take(pipeline_->manager()->snapshot()))
                require(state.state == ModuleState::Resident && state.nativePinned && state.resources.drained(),
                        "module generation did not drain to Resident");
            std::cout << "session, kernel and modules quiescent; DLLs remain resident\n";
        }
        // Return to Qt before closing its queue, after all native retirement receipts above have run.
        const auto self = shared_from_this();
        QTimer::singleShot(0, [self] {
            guarded([&] {
                const auto queue = take(self->pipeline_->scheduler()->snapshot());
                require(queue.queued == 0 && queue.discarded == 0 && queue.callbackErrors == 0,
                        "scheduler has pending or discarded receipts");
                take(self->pipeline_->scheduler()->close());
                require(take(self->pipeline_->scheduler()->snapshot()).callbackErrors == 0, "scheduler callback error");
                QCoreApplication::exit(EXIT_SUCCESS);
            });
        });
    }
    QString root_, path_;
    std::string mode_;
    QTemporaryDir scratch_;
    std::shared_ptr<native_services::Pipeline> pipeline_;
};
} // namespace
int main(int argc, char** argv)
{
    try {
        QCoreApplication app(argc, argv);
        const auto scheduler = std::make_shared<cordis::qt::QtScheduler>();
        const auto arguments = app.arguments();
        const auto root =
            arguments.size() > 1 ? arguments.at(1) : QCoreApplication::applicationDirPath() + "/native-services";
        const auto runner = std::make_shared<Runner>(
            QDir(root).absolutePath(), arguments.size() > 2 ? arguments.at(2).toStdString() : "normal", scheduler);
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &app, [] {
            std::cerr << "native service pipeline timed out\n";
            QCoreApplication::exit(EXIT_FAILURE);
        });
        deadline.start(10000);
        QTimer::singleShot(0, [runner] { guarded([&] { runner->start(); }); });
        return app.exec();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
