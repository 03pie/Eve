/** @brief Verify old Native host admission and host-owned logging delivery through a real C ABI DLL. */
#include "../support/module_artifacts.hpp"
#include <eve/kernel.hpp>
#include <eve/module/manager.hpp>
#include <eve/module/runtime.hpp>
#include <eve/testing/fake_scheduler.hpp>
#include <eve_abi/plugin.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <windows.h>
#include <cstring>
#include <iostream>
#include <thread>

#define REQUIRE(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(#condition) + " at line " + std::to_string(__LINE__)); } while (false)

namespace {
using namespace eve;
using namespace eve::module;
using testing::FakeScheduler;
using EmitLog = EveStatus(EVE_CALL*)(EveHandle, std::uint32_t, EveBytesV1);

template<class T> T take(Result<T> value)
{
    if (!value.hasValue())
        throw std::runtime_error(value.error().message);
    return std::move(value).value();
}

void success(const Result<void>& value)
{
    if (!value.hasValue())
        throw std::runtime_error(value.error().message);
}

EveBytesV1 bytes(const std::string& value)
{
    return {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
}

struct Scene {
    std::shared_ptr<FakeScheduler> scheduler = std::make_shared<FakeScheduler>();
    std::unique_ptr<Kernel> kernel = std::make_unique<Kernel>(scheduler);
    ModuleManager modules;
    QTemporaryDir directory;
    std::vector<NativeLogEvent> events;
    bool wrongThread = false;
    std::shared_ptr<const NativeLogSink> sink = std::make_shared<NativeLogSink>([this](NativeLogEvent value) {
        if (!scheduler->isCurrentThread()) {
            wrongThread = true;
            return;
        }
        events.push_back(std::move(value));
    });
    GenerationId generation;
    std::optional<NativeModule> module;
    EmitLog emitLog = nullptr;

    explicit Scene(const QString& library)
    {
        REQUIRE(directory.isValid());
        const auto directoryPath = directory.path();
        testing::modules::install(library, directoryPath, "org.eve.test.native.logging",
                                  {"test.native.alpha", "test.native.bravo"});
        generation = take(modules.load(directoryPath.toStdString(), "module.json")).generation;
        module = take(NativeModule::open(modules, generation, scheduler, kernel->services(), sink));
        const auto path = take(modules.snapshot()).front().libraryPath;
        const auto nativePath = QDir::toNativeSeparators(QString::fromStdString(path)).toStdWString();
        const auto image = GetModuleHandleW(nativePath.c_str());
        REQUIRE(image);
        const auto entry = GetProcAddress(image, "eve_logging_fixture_emit");
        REQUIRE(entry);
        static_assert(sizeof(entry) == sizeof(emitLog));
        std::memcpy(&emitLog, &entry, sizeof(emitLog));
    }

    void pump() { REQUIRE(scheduler->runUntilIdle().hasValue()); }

    void ready(const Task<void>& task)
    {
        pump();
        REQUIRE(task.isReady());
        success(*task.tryResult());
    }

    void quiescent(const Task<CleanupReport>& task)
    {
        pump();
        REQUIRE(task.isReady() && task.tryResult()->hasValue() && task.tryResult()->value().quiescent);
    }

    void finish()
    {
        quiescent(kernel->close());
        quiescent(module->close());
        kernel.reset();
        success(modules.drain(generation));
        REQUIRE(!wrongThread);
    }
};

void boundary(Scene& scene)
{
    auto alpha = take(scene.module->create("test.native.alpha", "null", InstanceId{9001}, 7));
    auto bravo = take(scene.module->create("test.native.bravo", "null", InstanceId{9002}, 9));
    scene.ready(alpha.start());
    scene.ready(bravo.start());
    REQUIRE(scene.events.size() == 2);
    REQUIRE(scene.events[0].record.text == scene.events[1].record.text);
    REQUIRE(scene.events[0].pluginId == "test.native.alpha" && scene.events[1].pluginId == "test.native.bravo");
    REQUIRE(scene.events[0].instance == InstanceId{9001} && scene.events[0].epoch == 7);
    REQUIRE(scene.events[1].instance == InstanceId{9002} && scene.events[1].epoch == 9);

    std::vector<EveStatus> statuses;
    std::string borrowed = u8"fixture UTF-8 日志";
    std::thread arbitraryThread([&] {
        for (auto level = EVE_LOG_TRACE; level <= EVE_LOG_ERROR; ++level)
            statuses.push_back(scene.emitLog(1, level, bytes(borrowed)));
        borrowed.assign(borrowed.size(), 'x');
    });
    arbitraryThread.join();
    REQUIRE(statuses == std::vector<EveStatus>(5, EVE_OK));
    REQUIRE(scene.events.size() == 2 && !scene.wrongThread);
    scene.pump();
    REQUIRE(scene.events.size() == 7);
    for (auto level = EVE_LOG_TRACE; level <= EVE_LOG_ERROR; ++level) {
        const auto& value = scene.events[2 + level];
        REQUIRE(value.record.level == level && value.record.text == u8"fixture UTF-8 日志");
        REQUIRE(value.sourceSequence == level + 2 && value.utcMs > 0);
        REQUIRE(value.moduleId == "org.eve.test.native.logging" && value.moduleVersion == "1.0.0");
        REQUIRE(value.generation == scene.generation && !value.operation);
    }

    const std::string full(EVE_MAX_LOG_BYTES, '"');
    REQUIRE(scene.emitLog(1, EVE_LOG_ERROR, bytes(full)) == EVE_OK);
    REQUIRE(scene.emitLog(1, EVE_LOG_INFO, {nullptr, 0}) == EVE_OK);
    REQUIRE(scene.emitLog(1, EVE_LOG_INFO, bytes(full + 'x')) == EVE_E_LIMIT);
    REQUIRE(scene.emitLog(1, EVE_LOG_ERROR + 1, bytes("invalid level")) == EVE_E_INVALID_ARGUMENT);
    REQUIRE(scene.emitLog(1, EVE_LOG_INFO, {nullptr, 1}) == EVE_E_INVALID_ARGUMENT);
    REQUIRE(scene.emitLog(1, EVE_LOG_INFO, bytes(std::string("\xC0\x80", 2))) == EVE_E_INVALID_ARGUMENT);
    REQUIRE(scene.events.size() == 7);
    scene.pump();
    REQUIRE(scene.events.size() == 9 && scene.events[7].record.text == full && scene.events[8].record.text.empty());

    std::size_t heldBytes = 0;
    for (const auto& entry : take(alpha.snapshot()).logs)
        heldBytes += entry.text.size();
    constexpr std::size_t byteLimit = 256 * 1024;
    while (heldBytes < byteLimit) {
        const auto size = std::min<std::size_t>(EVE_MAX_LOG_BYTES, byteLimit - heldBytes);
        REQUIRE(scene.emitLog(1, EVE_LOG_DEBUG, bytes(std::string(size, 'z'))) == EVE_OK);
        heldBytes += size;
    }
    REQUIRE(scene.emitLog(1, EVE_LOG_DEBUG, bytes("overflow")) == EVE_E_LIMIT);
    scene.quiescent(alpha.close());
    const auto snapshot = take(alpha.snapshot());
    REQUIRE(snapshot.state == NativeInstanceState::Destroyed && snapshot.logs[6].text == full);

    scene.quiescent(bravo.close());
    REQUIRE(scene.events.back().instance == InstanceId{9002} && scene.events.back().sourceSequence == 2 &&
            scene.events.back().record.text == "native fixture destroyed");
    const auto beforeStale = scene.events.size();
    REQUIRE(scene.emitLog(2, EVE_LOG_INFO, bytes("stale")) == EVE_E_CLOSED);
    scene.pump();
    REQUIRE(scene.events.size() == beforeStale);

    auto countCase = take(scene.module->create("test.native.alpha", "null", InstanceId{9003}, 1));
    scene.ready(countCase.start());
    for (unsigned i = 1; i < 128; ++i)
        REQUIRE(scene.emitLog(1, EVE_LOG_TRACE, {nullptr, 0}) == EVE_OK);
    REQUIRE(scene.emitLog(1, EVE_LOG_TRACE, {nullptr, 0}) == EVE_E_LIMIT);
    scene.quiescent(countCase.close());
    REQUIRE(scene.events.size() == beforeStale + 128 && take(countCase.snapshot()).logs.size() == 128);
}

void publication(Scene& scene)
{
    const auto runtime = take(scene.kernel->registerRuntime(
        "test.native.alpha", take(scene.module->runtime({"test.native.alpha", {}, {}}))));
    auto committed = take(scene.kernel->holdPublication());
    auto abandoned = take(scene.kernel->holdPublication());
    auto visible = take(scene.kernel->mountHeld(committed, runtime));
    auto discarded = take(scene.kernel->mountHeld(abandoned, runtime));
    scene.ready(visible.ready());
    scene.ready(discarded.ready());
    REQUIRE(scene.events.empty());
    const auto acceptedInstance = take(visible.snapshot()).instance;
    success(committed.commit());
    success(abandoned.abort());
    scene.pump();
    REQUIRE(scene.events.size() == 1 && scene.events.front().instance == acceptedInstance &&
            scene.events.front().record.text == "native fixture started");
    scene.quiescent(scene.kernel->close());
    REQUIRE(scene.events.size() == 2 && scene.events.back().instance == acceptedInstance &&
            scene.events.back().record.text == "native fixture destroyed");
}
}

int main(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    try {
        const auto arguments = app.arguments();
        REQUIRE(arguments.size() == 3);
        Scene scene(arguments[2]);
        if (arguments[1] == "boundary")
            boundary(scene);
        else {
            REQUIRE(arguments[1] == "publication");
            publication(scene);
        }
        scene.finish();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
