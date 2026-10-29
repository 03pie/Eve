/** @file host.cpp
 * @brief 用真实 Qt 循环、两个业务 DLL 和磁盘 Session 验证激活与退出。
 */
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cordis/kernel.hpp>
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/qt_store.hpp>
#include <cordis/loader/session.hpp>
#include <cordis/module/manager.hpp>
#include <cordis/module/runtime.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
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
class Demo {
public:
    Demo(QString root, std::string mode) : root_(std::move(root)), mode_(std::move(mode))
    {
        require(mode_ == "normal" || mode_ == "multi" || mode_ == "realms" || mode_ == "start_failure" ||
                    mode_ == "cleanup_failure",
                "unknown scenario");
        require(scratch_.isValid(), "temporary session directory unavailable");
        path_ = scratch_.path() + "/session.yaml";
        counterId_ = take(manager_.load(root_.toUtf8().toStdString(), "counter/module.json")).generation;
        reporterId_ = take(manager_.load(root_.toUtf8().toStdString(), "reporter/module.json")).generation;
        counter_.emplace(take(NativeModule::open(manager_, counterId_, scheduler_, kernel_->services())));
        reporter_.emplace(take(NativeModule::open(manager_, reporterId_, scheduler_, kernel_->services())));
        auto counter =
            take(counter_->runtime({"example.counter", {}, {{{"example.counter", 1}, true, {1, {"next"}}}}}));
        auto reporter = take(reporter_->runtime({"example.reporter",
                                                 {{{"example.counter", 1}, 1, {"next"}}},
                                                 {{{"example.report", 1}, true, {1, {"read"}}}}}));
        take(kernel_->registerRuntime("example.counter", counter));
        take(kernel_->registerRuntime("example.reporter", reporter));
        auto gate = [](NativeModule module) {
            return [module] {
                auto state = module.snapshot();
                if (!state.hasValue())
                    return Result<void>::failure(state.error());
                return state.value().closing || state.value().closed || state.value().faulted
                           ? Result<void>::failure(makeError(ErrorCode::ContextClosing, "module unavailable"))
                           : Result<void>::success();
            };
        };
        const PluginModule counterModule{"counter", "counter/module.json"};
        const PluginModule reporterModule{"reporter", "reporter/module.json"};
        const auto second = take(kernel_->services().createRealm());
        driver_ = std::make_shared<KernelReconcileDriver>(
            scheduler_, kernel_,
            std::vector<KernelPlugin>{{"example.counter", counter, {}, counterModule, gate(*counter_), 128},
                                      {"example.reporter", reporter, {}, reporterModule, gate(*reporter_), 128}},
            std::map<std::string, RealmId>{{"second", second}});
        auto schema = [](const char* failure) {
            return [failure](const std::string& config) {
                return config == "null" || config == failure
                           ? Result<void>::success()
                           : Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "unsupported example config"));
            };
        };
        session_ = std::make_unique<ConfigurationSession>(
            scheduler_, driver_,
            std::vector<PluginDefinition>{{"example.counter", {}, schema("{\"failCleanup\":true}"), counterModule},
                                          {"example.reporter",
                                           {{"example.counter", 1, 1, {"next"}}},
                                           schema("{\"failStart\":true}"),
                                           reporterModule}},
            std::make_shared<QtConfigurationWriter>(path_), take(readConfigurationFile(path_)));
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
        auto draft = take(session_->preview(text));
        require(take(counter_->snapshot()).instances.empty() && take(reporter_->snapshot()).instances.empty(),
                "preview activated a DLL");
        const auto accepted = take(session_->apply(draft));
        watch(accepted.settled, [this, revision = accepted.revision](const Result<PlanReport>& result) {
            const auto report = take(result);
            require(report.outcome == (mode_ == "start_failure" ? PlanOutcome::Failed : PlanOutcome::Applied),
                    "unexpected plan outcome");
            require(!take(counter_->snapshot()).faulted && !take(reporter_->snapshot()).faulted,
                    "ordinary activation failure isolated a module");
            if (mode_ != "start_failure")
                checkBusiness();
            watch(take(session_->save(revision)), [this](const Result<SaveReport>& saved) {
                require(take(saved).outcome == SaveOutcome::Written, "session save failed");
                auto disk = take(readConfigurationFile(path_));
                require(disk.version.exists && parseConfiguration(disk.text).hasValue(), "disk YAML invalid");
                const auto store = take(session_->snapshot()).store;
                require(!store.dirty && store.persisted && disk.text == store.persisted->source().normalizedYaml(),
                        "disk content differs from saved session");
                std::cout << "configuration committed to disk\n";
                closeSession();
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
        task.thenOn(scheduler_,
                    [callback = std::move(callback)](const Result<T>& result) { guarded([&] { callback(result); }); });
    }
    void checkBusiness()
    {
        const auto state = take(reporter_->snapshot());
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
    void closeSession()
    {
        watch(session_->close(), [this](const Result<void>& result) {
            require(result.hasValue() != (mode_ == "cleanup_failure"), "incorrect session close result");
            watch(kernel_->close(), [this](const Result<CleanupReport>& result) {
                const auto report = take(result);
                require(report.quiescent != (mode_ == "cleanup_failure"), "incorrect kernel cleanup report");
                watch(counter_->close(), [this](const Result<CleanupReport>& counter) {
                    require(take(counter).quiescent != (mode_ == "cleanup_failure"), "incorrect module close report");
                    watch(reporter_->close(), [this](const Result<CleanupReport>& reporter) {
                        require(take(reporter).quiescent, "reporter cleanup did not quiesce");
                        finish();
                    });
                });
            });
        });
    }
    void finish()
    {
        take(manager_.drain(reporterId_));
        if (mode_ == "cleanup_failure") {
            require(!manager_.drain(counterId_).hasValue(), "failed cleanup falsely drained");
            const auto states = take(manager_.snapshot());
            const auto counter = std::find_if(states.begin(), states.end(),
                                              [this](const auto& value) { return value.generation == counterId_; });
            require(counter != states.end() && counter->nativePinned && !counter->resources.records.empty(),
                    "failed DLL resources were not retained");
            std::cout << "failed cleanup retained DLL and execution references\n";
        } else {
            take(manager_.drain(counterId_));
            require(take(kernel_->services().snapshot()).empty(), "service keys survived successful close");
            for (const auto& state : take(manager_.snapshot()))
                require(state.state == ModuleState::Resident && state.nativePinned && state.resources.drained(),
                        "module generation did not drain to Resident");
            std::cout << "session, kernel and modules quiescent; DLLs remain resident\n";
        }
        // Return to Qt before closing its queue, after all native retirement receipts above have run.
        QTimer::singleShot(0, [this] {
            guarded([&] {
                const auto queue = take(scheduler_->snapshot());
                require(queue.queued == 0 && queue.discarded == 0 && queue.callbackErrors == 0,
                        "scheduler has pending or discarded receipts");
                take(scheduler_->close());
                require(take(scheduler_->snapshot()).callbackErrors == 0, "scheduler callback error");
                QCoreApplication::exit(EXIT_SUCCESS);
            });
        });
    }
    QString root_, path_;
    std::string mode_;
    QTemporaryDir scratch_;
    std::shared_ptr<cordis::qt::QtScheduler> scheduler_ = std::make_shared<cordis::qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel_ = std::make_shared<Kernel>(scheduler_);
    ModuleManager manager_;
    GenerationId counterId_, reporterId_;
    std::optional<NativeModule> counter_, reporter_;
    std::shared_ptr<KernelReconcileDriver> driver_;
    std::unique_ptr<ConfigurationSession> session_;
};
} // namespace
int main(int argc, char** argv)
{
    try {
        QCoreApplication app(argc, argv);
        const auto arguments = app.arguments();
        const auto root =
            arguments.size() > 1 ? arguments.at(1) : QCoreApplication::applicationDirPath() + "/native-services";
        Demo demo(QDir(root).absolutePath(), arguments.size() > 2 ? arguments.at(2).toStdString() : "normal");
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &app, [] {
            std::cerr << "native service pipeline timed out\n";
            QCoreApplication::exit(EXIT_FAILURE);
        });
        deadline.start(10000);
        QTimer::singleShot(0, [&demo] { guarded([&] { demo.start(); }); });
        return app.exec();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
