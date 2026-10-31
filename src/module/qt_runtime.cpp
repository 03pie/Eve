/** @file qt_runtime.cpp
 * @brief 同工具链 Qt 工厂接到现有 Core Runtime，末尾清理等待完整 QObject 析构。
 */
#include "admission_p.hpp"
#include "qt_module_p.hpp"
#include <QCoreApplication>
#include <QThread>
#include <algorithm>
#include <cordis/module/qt_module.hpp>
#include <cordis/qt/object.hpp>
#include <stdexcept>

namespace cordis::module {
namespace {
using detail::Generation;
using detail::QtResidency;
ErrorInfo error(ErrorCode code, std::string message)
{
    auto value = makeError(code, std::move(message));
    value.phase = "module.qt.runtime";
    return value;
}
Result<void> available(const std::shared_ptr<Generation>& generation, const std::shared_ptr<QtResidency>& backend)
{
    auto app = QCoreApplication::instance();
    if (!app || QCoreApplication::closingDown() || QThread::currentThread() != app->thread())
        return Result<void>::failure(error(ErrorCode::WrongThread, "Qt runtime requires application owner"));
    std::lock_guard<std::mutex> lock(generation->mutex);
    if (generation->loading || generation->data.failure ||
        (generation->data.state != ModuleState::Loaded && generation->data.state != ModuleState::Serving))
        return Result<void>::failure(error(ErrorCode::ModulePinned, "Qt generation is unavailable"));
    std::lock_guard<std::mutex> signal(generation->businessSignal->mutex);
    if (generation->businessSignal->error)
        return Result<void>::failure(*generation->businessSignal->error);
    if (!backend || !backend->root || backend->root->parent() || backend->root->thread() != app->thread() ||
        !backend->loader.isLoaded())
        return Result<void>::failure(error(ErrorCode::ModulePinned, "resident Qt root is unavailable"));
    return Result<void>::success();
}
void fault(const std::shared_ptr<Generation>& generation, const ErrorInfo& failure)
{
    (void)generation->ledger.pin("qt.businessUncertain");
    std::lock_guard<std::mutex> lock(generation->businessSignal->mutex);
    if (!generation->businessSignal->error)
        generation->businessSignal->error = failure;
}
Result<void> negotiate(const std::shared_ptr<Generation>& generation, const std::shared_ptr<QtResidency>& backend)
{
    if (backend->factory)
        return Result<void>::success();
    if (backend->negotiating)
        return Result<void>::failure(error(ErrorCode::ModuleLoad, "Qt factory negotiation is in progress"));
    auto call = generation->ledger.acquire(ResourceKind::Call, "qt.factory.describe");
    if (!call.hasValue())
        return Result<void>::failure(call.error());
    backend->negotiating = true;
    struct Reset {
        bool& value;
        ~Reset() { value = false; }
    } reset{backend->negotiating};
    IQtPluginV1* factory = nullptr;
    std::vector<std::string> actual;
    try {
        factory = qobject_cast<IQtPluginV1*>(backend->root.data());
        if (!factory)
            return Result<void>::failure(error(ErrorCode::AbiMismatch, "Qt root does not implement IQtPluginV1"));
        if (factory->coreRuntimeIdentity() != cordis::coreRuntimeIdentity())
            return Result<void>::failure(error(ErrorCode::AbiMismatch, "Qt plugin uses another Core runtime"));
        const auto ids = factory->pluginIds();
        if (ids.size() != static_cast<int>(generation->data.manifest.plugins.size()))
            return Result<void>::failure(
                error(ErrorCode::AbiMismatch, "Qt factory catalog size differs from manifest"));
        for (const auto& id : ids) {
            if (id.size() > 128)
                return Result<void>::failure(error(ErrorCode::AbiMismatch, "Qt factory identifier exceeds budget"));
            actual.push_back(id.toUtf8().toStdString());
        }
    } catch (...) {
        return Result<void>::failure(error(ErrorCode::AbiProtocol, "exception escaped Qt factory description"));
    }
    auto expected = generation->data.manifest.plugins;
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    if (expected != actual || !backend->root)
        return Result<void>::failure(
            error(ErrorCode::AbiMismatch, "Qt factory identity/catalog differs from manifest"));
    backend->factory = factory;
    {
        std::lock_guard<std::mutex> lock(generation->mutex);
        generation->data.descriptorAccepted = true;
        generation->data.businessAccepted = true;
    }
    return Result<void>::success();
}
/** @brief 全部业务可变状态只在应用 owner 推进；失败的对象/捕获和租约保持自持。 */
struct QtBusiness : std::enable_shared_from_this<QtBusiness> {
    QtBusiness(std::shared_ptr<Generation> value, std::shared_ptr<QtResidency> factory,
               std::shared_ptr<qt::QtScheduler> target, ModuleLease lease)
        : generation(std::move(value)), backend(std::move(factory)), scheduler(std::move(target)),
          instanceLease(std::move(lease))
    {
    }
    std::shared_ptr<Generation> generation;
    std::shared_ptr<QtResidency> backend;
    std::shared_ptr<qt::QtScheduler> scheduler;
    ModuleLease instanceLease;
    ModuleLease cleanupLease;
    QObject* identity = nullptr;
    std::optional<qt::QObjectResource> resource;
    std::optional<Task<void>> activation;
    std::shared_ptr<QtBusiness> retained;
    std::optional<ErrorInfo> uncertain;
    Promise<void> closed;
    bool closing = false;
    void quarantine(ErrorInfo failure)
    {
        if (failure.phase.empty())
            failure.phase = "module.qt.destroy";
        if (!uncertain)
            uncertain = failure;
        fault(generation, failure);
        closed.reject(std::move(failure));
    }
    /** @brief 只在整个删除调用返回且 DLL Task 状态也释放后，解除本实例登记。 */
    void released()
    {
        resource.reset();
        activation.reset();
        if (identity)
            backend->businessObjects.erase(identity);
        identity = nullptr;
        {
            std::lock_guard<std::mutex> lock(generation->mutex);
            if (backend->businessObjects.empty() && generation->data.state == ModuleState::Serving)
                generation->data.state = ModuleState::Loaded;
        }
        cleanupLease.reset();
        instanceLease.reset();
        retained.reset();
        closed.resolve();
    }
    Task<void> finalize()
    {
        if (closing || uncertain)
            return closed.task();
        closing = true;
        if (!resource) {
            released();
            return closed.task();
        }
        auto lease = generation->ledger.acquire(ResourceKind::CleanupTask, "qt.instance.destroy");
        if (!lease.hasValue()) {
            quarantine(lease.error());
            return closed.task();
        }
        cleanupLease = std::move(lease.value());
        auto self = shared_from_this();
        auto receipt = resource->close().thenOn(scheduler, [self](const Result<void>& result) {
            if (!result.hasValue())
                self->quarantine(result.error());
            else
                self->released();
        });
        cordis::detail::TaskAccess::observe(receipt, [self](Task<void>::Snapshot result) {
            if (!result->hasValue())
                self->quarantine(result->error());
        });
        return closed.task();
    }
    Task<void> start(const std::string& plugin, Context context)
    {
        auto self = shared_from_this();
        retained = self;
        auto registered = context.afterCleanup([self] { return self->finalize(); }, "qt.instance.finalize");
        if (!registered.hasValue()) {
            released();
            return failedTask(registered.error());
        }
        auto call = generation->ledger.acquire(ResourceKind::Call, "qt.instance.activate");
        if (!call.hasValue())
            return failedTask(call.error());
        QObject* object = nullptr;
        try {
            backend->creating = true;
            struct ResetCreation {
                bool& value;
                ~ResetCreation() { value = false; }
            } reset{backend->creating};
            object = backend->factory->create(QString::fromStdString(plugin));
        } catch (...) {
            auto failure = error(ErrorCode::AbiProtocol, "exception escaped Qt business creation; resources uncertain");
            quarantine(failure);
            return failedTask(std::move(failure));
        }
        if (!object)
            return failedTask(error(ErrorCode::ActivationFailed, "Qt factory declined business creation"));
        auto app = QCoreApplication::instance();
        if (!app || !backend->root || object == backend->root.data() || object->parent() ||
            object->thread() != app->thread() || !backend->businessObjects.insert(object).second) {
            auto failure =
                error(ErrorCode::AbiProtocol, "Qt factory returned a root, shared, parented or foreign-thread object");
            quarantine(failure);
            return failedTask(std::move(failure));
        }
        identity = object;
        std::unique_ptr<QObject> owned(object);
        try {
            resource.emplace(owned, scheduler);
        } catch (...) {
            owned.release();
            auto failure = error(ErrorCode::CleanupFailed, "Qt business ownership transfer was not confirmed");
            quarantine(failure);
            return failedTask(std::move(failure));
        }
        auto api = qobject_cast<IQtInstanceV1*>(object);
        if (!api) {
            auto failure = error(ErrorCode::AbiMismatch, "Qt business object does not implement IQtInstanceV1");
            fault(generation, failure);
            return failedTask(std::move(failure));
        }
        // Constructors and metacast are foreign code: stop/update may have invalidated this epoch.
        auto current = context.identity();
        if (!current.hasValue())
            return failedTask(current.error());
        auto allowed = available(generation, backend);
        if (!allowed.hasValue())
            return failedTask(allowed.error());
        {
            std::lock_guard<std::mutex> lock(generation->mutex);
            generation->data.state = ModuleState::Serving;
        }
        try {
            activation = api->activate(std::move(context));
        } catch (const std::exception& failure) {
            activation = failedTask(error(ErrorCode::ActivationFailed, failure.what()));
        } catch (...) {
            activation = failedTask(error(ErrorCode::ActivationFailed, "Qt business activation threw"));
        }
        return *activation;
    }
};
/** @brief Runtime 内只含宿主闭包，原始工厂由 root/目录管理；显式持有定义引用。 */
struct QtDefinition {
    std::shared_ptr<Generation> generation;
    std::shared_ptr<QtResidency> backend;
    std::weak_ptr<qt::QtScheduler> scheduler;
    std::string plugin;
    ModuleLease definitionLease;
    Task<void> activate(Context context) const
    {
        auto target = scheduler.lock();
        if (!target || !target->isOpen())
            return failedTask(error(ErrorCode::SchedulerClosed, "Qt runtime scheduler closed"));
        auto identity = context.identity();
        if (!identity.hasValue())
            return failedTask(identity.error());
        if (!context.usesScheduler(target))
            return failedTask(error(ErrorCode::InvalidArgument, "Qt runtime requires the bound scheduler"));
        auto allowed = available(generation, backend);
        if (!allowed.hasValue())
            return failedTask(allowed.error());
        if (backend->creating)
            return failedTask(error(ErrorCode::ModuleLoad, "Qt factory creation is already in progress"));
        auto admission = detail::admitGeneration(generation->data.generation);
        if (!admission.hasValue())
            return failedTask(admission.error());
        auto lease = generation->ledger.acquire(ResourceKind::Instance,
                                                "qt.instance." + std::to_string(identity.value().instance.value()) +
                                                    "." + std::to_string(identity.value().epoch));
        if (!lease.hasValue())
            return failedTask(lease.error());
        auto business = std::make_shared<QtBusiness>(generation, backend, target, std::move(lease.value()));
        return business->start(plugin, std::move(context));
    }
};
} // namespace
Result<Runtime> qtRuntime(GenerationId id, QtPlugin plugin, std::shared_ptr<qt::QtScheduler> scheduler)
{
    using Output = Result<Runtime>;
    auto app = QCoreApplication::instance();
    if (!app || QCoreApplication::closingDown() || QThread::currentThread() != app->thread())
        return Output::failure(error(ErrorCode::WrongThread, "Qt factory requires application owner"));
    if (!scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen())
        return Output::failure(error(ErrorCode::SchedulerClosed, "Qt factory requires an open owner scheduler"));
    const auto generation = detail::findGeneration(id);
    if (!generation)
        return Output::failure(error(ErrorCode::InvalidArgument, "unknown Qt generation"));
    auto admission = detail::admitGeneration(id);
    if (!admission.hasValue())
        return Output::failure(admission.error());
    if (generation->data.manifest.loader != ModuleLoader::QtUi)
        return Output::failure(error(ErrorCode::AbiMismatch, "Qt runtime requires qt-ui loader"));
    auto backend = std::dynamic_pointer_cast<QtResidency>(generation->backend);
    auto allowed = available(generation, backend);
    if (!allowed.hasValue())
        return Output::failure(allowed.error());
    if (std::find(generation->data.manifest.plugins.begin(), generation->data.manifest.plugins.end(), plugin.id) ==
            generation->data.manifest.plugins.end() ||
        plugin.dependencies.size() > 128 || plugin.outputs.size() > 128)
        return Output::failure(error(ErrorCode::InvalidArgument, "unknown Qt plugin or contract budget exceeded"));
    auto handshake = negotiate(generation, backend);
    if (!handshake.hasValue()) {
        if (handshake.error().code != ErrorCode::ModuleLoad)
            (void)detail::failGeneration(generation, handshake.error());
        return Output::failure(handshake.error());
    }
    auto lease = generation->ledger.acquire(ResourceKind::ExternalHandle, "qt.runtime.definition");
    if (!lease.hasValue())
        return Output::failure(lease.error());
    auto definition = std::make_shared<QtDefinition>();
    definition->generation = generation;
    definition->backend = backend;
    definition->scheduler = scheduler;
    definition->plugin = std::move(plugin.id);
    definition->definitionLease = std::move(lease.value());
    try {
        return Output::success(
            Runtime([definition](Context context) { return definition->activate(std::move(context)); },
                    std::move(plugin.dependencies), std::move(plugin.outputs)));
    } catch (const std::invalid_argument& failure) {
        return Output::failure(error(ErrorCode::InvalidArgument, failure.what()));
    }
}
} // namespace cordis::module
