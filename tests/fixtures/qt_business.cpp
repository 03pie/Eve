/** @file qt_business.cpp
 * @brief 真实同工具链工厂/业务 DLL，测试控制仅存在此夹具。
 */
#include <QCoreApplication>
#include <QObject>
#include <QVariant>
#include <cordis/module/qt_plugin.hpp>
#include <optional>
#include <stdexcept>
#include <vector>
using namespace cordis;
using namespace cordis::module;
class Business;
namespace {
bool wrongCore = false;
unsigned char wrongCoreIdentity = 0;
// root, query, create, activate, cleanup, business destructor, child destructor, bad destruction order
int counts[8] = {};
std::vector<Business*> live;
std::vector<std::optional<Context>> contexts;
std::vector<std::uint64_t> childIds;
class Child final : public QObject {
public:
    explicit Child(QObject* parent) : QObject(parent) {}
    ~Child() override { ++counts[6]; }
};
struct Capture {
    bool& released;
    explicit Capture(bool& flag) : released(flag) {}
    ~Capture() { released = true; }
};
} // namespace
class Business final : public QObject, public IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(cordis::module::IQtInstanceV1)
public:
    Business() : index(live.size())
    {
        live.push_back(this);
        contexts.emplace_back();
        childIds.push_back(0);
        new Child(this);
        ++counts[2];
    }
    ~Business() override
    {
        if (activated && (!captureReleased || !outputReleased))
            ++counts[7];
        live[index] = nullptr;
        ++counts[5];
    }
    Task<void> activate(Context context) override
    {
        activated = true;
        ++counts[3];
        contexts[index] = context;
        mode = context.config();
        auto keep = std::make_shared<Capture>(captureReleased);
        auto accepted = context.onCleanup([this, keep](CleanupContext cleanup) {
            if (!cleanup.snapshot().hasValue())
                return failedTask(makeError(ErrorCode::CleanupFailed, "cleanup access missing"));
            ++counts[4];
            if (mode == "cleanup_failure")
                return failedTask(makeError(ErrorCode::CleanupFailed, "fixture cleanup failed"));
            if (mode == "cleanup_pending")
                return cleanupGate.task();
            return completedTask();
        });
        if (!accepted.hasValue())
            return failedTask(accepted.error());
        if (mode == "tail_pending" || mode == "tail_failure") {
            auto tail = context.afterCleanup([this] {
                return mode == "tail_failure" ? failedTask(makeError(ErrorCode::CleanupFailed, "inner owner retained"))
                                              : cleanupGate.task();
            });
            if (!tail.hasValue())
                return failedTask(tail.error());
        }
        if (mode == "output") {
            outputReleased = false;
            auto provided = context.provide<int>({"qt.value"}, std::shared_ptr<int>(new int(7), [this](int* value) {
                                                     delete value;
                                                     outputReleased = true;
                                                 }));
            if (!provided.hasValue())
                return failedTask(provided.error());
        }
        if (mode == "inject") {
            Runtime child([](Context) { return completedTask(); });
            auto created = context.inject(child);
            if (!created.hasValue())
                return failedTask(created.error());
            childIds[index] = created.value().snapshot().value().instance.value();
            return created.value().ready();
        }
        if (mode == "start_failure")
            return failedTask(makeError(ErrorCode::ActivationFailed, "fixture activation failed"));
        if (mode == "activation_throw")
            throw std::runtime_error("fixture activation threw");
        if (mode == "start_pending")
            return startGate.task();
        return completedTask();
    }
    const std::size_t index;
    bool activated = false;
    bool captureReleased = false;
    bool outputReleased = true;
    std::string mode;
    Promise<void> startGate, cleanupGate;
};
class BusinessFactory final : public QObject, public IQtPluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.cordis.IQtPluginV1/1.0" FILE "qt_metadata.json")
    Q_INTERFACES(cordis::module::IQtPluginV1)
public:
    BusinessFactory()
    {
        ++counts[0];
        wrongCore = QCoreApplication::instance()->property("cordis.qt.factoryMode").toString() == "wrong_core";
    }
    const void* coreRuntimeIdentity() const noexcept override
    {
        return wrongCore ? &wrongCoreIdentity : IQtPluginV1::coreRuntimeIdentity();
    }
    QStringList pluginIds() const override
    {
        ++counts[1];
        const auto mode = QCoreApplication::instance()->property("cordis.qt.factoryMode").toString();
        if (mode == "query_reentry")
            QCoreApplication::processEvents();
        if (mode == "query_throw")
            throw std::runtime_error("fixture query threw");
        if (mode == "bad_catalog")
            return {"org.cordis.qt.page", "org.cordis.qt.page"};
        return {"org.cordis.qt.settings", "org.cordis.qt.page"};
    }
    QObject* create(const QString& plugin) override
    {
        const auto mode = QCoreApplication::instance()->property("cordis.qt.factoryMode").toString();
        if (mode == "null")
            return nullptr;
        if (mode == "create_throw")
            throw std::runtime_error("fixture create threw");
        if (mode == "root")
            return this;
        if (mode == "parent")
            return new QObject(QCoreApplication::instance());
        if (mode == "wrong_instance")
            return new QObject;
        if (mode == "duplicate" && !live.empty() && live.front())
            return live.front();
        auto object = new Business;
        object->setProperty("pluginId", plugin);
        return object;
    }
};
extern "C" __declspec(dllexport) int __cdecl cordis_qt_business_count(int index)
{
    return index >= 0 && index < 8 ? counts[index] : -1;
}
extern "C" __declspec(dllexport) void __cdecl cordis_qt_business_release(int index, int cleanup)
{
    if (index >= 0 && static_cast<std::size_t>(index) < live.size() && live[index]) {
        if (cleanup)
            live[index]->cleanupGate.resolve();
        else
            live[index]->startGate.resolve();
    }
}
extern "C" __declspec(dllexport) QObject* __cdecl cordis_qt_business_object(int index)
{
    return index >= 0 && static_cast<std::size_t>(index) < live.size() ? live[index] : nullptr;
}
extern "C" __declspec(dllexport) int __cdecl cordis_qt_business_context(int index)
{
    return index >= 0 && static_cast<std::size_t>(index) < contexts.size() && contexts[index] &&
           contexts[index]->identity().hasValue();
}
extern "C" __declspec(dllexport) std::uint64_t __cdecl cordis_qt_business_runtime_id()
{
    Runtime value([](Context) { return completedTask(); });
    return value.id().value();
}
extern "C" __declspec(dllexport) std::uint64_t __cdecl cordis_qt_business_child_id(int index)
{
    return index >= 0 && static_cast<std::size_t>(index) < childIds.size() ? childIds[index] : 0;
}
#include "qt_business.moc"
