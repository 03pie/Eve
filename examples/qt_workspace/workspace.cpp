/** @file workspace.cpp
 * @brief caller 队列准入、原宿主页与会话接线；不创建第二个业务平台。
 */
#include "workspace_p.hpp"
#include <QApplication>
#include <QThread>
#include <QWidget>

namespace notebook {
using namespace cordis;
using namespace cordis::loader;
using namespace cordis::ui;
Workspace::Workspace(std::shared_ptr<qt::QtScheduler> target)
    : scheduler(std::move(target)), kernel(std::make_shared<Kernel>(scheduler)), shell(scheduler),
      manager(std::make_shared<module::ModuleManager>())
{
}
Result<std::shared_ptr<Workspace>> Workspace::open(const QString& root, const QString& file,
                                                   std::shared_ptr<qt::QtScheduler> scheduler)
{
    return create(root, {}, file, std::move(scheduler), SourceMode::Notebook);
}
Result<std::shared_ptr<Workspace>> Workspace::openQualification(const QString& sourceBin, const QString& storeRoot,
                                                                const QString& file,
                                                                std::shared_ptr<qt::QtScheduler> scheduler)
{
    return create(sourceBin, storeRoot, file, std::move(scheduler), SourceMode::Qualification);
}
Result<std::shared_ptr<Workspace>> Workspace::create(const QString& root, const QString& store, const QString& file,
                                                     std::shared_ptr<qt::QtScheduler> scheduler, SourceMode mode)
{
    using Output = Result<std::shared_ptr<Workspace>>;
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app || QCoreApplication::closingDown() || !scheduler)
        return Output::failure(makeError(ErrorCode::InvalidArgument, "workspace requires QApplication and scheduler"));
    if (QThread::currentThread() != app->thread() || !scheduler->isCurrentThread())
        return Output::failure(makeError(ErrorCode::WrongThread, "workspace requires application owner thread"));
    if (!scheduler->isOpen())
        return Output::failure(makeError(ErrorCode::SchedulerClosed, "workspace scheduler closed"));
    std::shared_ptr<Workspace> self;
    try {
        // UI 对象只在全部 caller/应用准入通过后构造。
        self = std::shared_ptr<Workspace>(new Workspace(std::move(scheduler)));
        self->initialize(root, store, file, mode);
        return Output::success(self);
    } catch (const detail::InitializationFailure& failure) {
        if (self)
            (void)self->close();
        return Output::failure(failure.error);
    } catch (const std::exception& failure) {
        if (self)
            (void)self->close();
        return Output::failure(makeError(ErrorCode::CallbackFailed, failure.what()));
    } catch (...) {
        if (self)
            (void)self->close();
        return Output::failure(makeError(ErrorCode::CallbackFailed, "workspace initialization threw"));
    }
}
void Workspace::publishUi()
{
    detail::require(shell.management()->attach(kernel));
    const auto publish = [&](const auto& value, const char* name) {
        auto provider = detail::require(kernel->services().stage({name}, value));
        providers_.push_back(std::move(provider));
        detail::require(providers_.back().publish());
    };
    publish(shell.pages(), "ui.pages");
    publish(shell.commands(), "ui.commands");
    publish(shell.output(), "ui.output");
}
void Workspace::initializeHost(const QString& file, ConfigurationFileSnapshot baseline,
                               std::vector<ConfiguredPlugin> plugins, SourceMode mode)
{
    definitions_ = detail::require(ConfigurationDefinitions::create(std::move(plugins)));
    // 业务装配先核完整目标；原单 Notebook 仍允许打开坏配置后在 UI 中修正。
    if (mode != SourceMode::Notebook)
        (void)detail::require(definitions_->validate(detail::require(parseConfiguration(initial))));
    std::map<std::string, RealmId> realms;
    if (mode != SourceMode::Notebook)
        realms.emplace("second", detail::require(kernel->services().createRealm()));
    driver = KernelReconcileDriver::configured(scheduler, kernel, definitions_, std::move(realms));
    session = std::make_shared<ConfigurationSession>(scheduler, driver, std::make_shared<QtConfigurationWriter>(file),
                                                     std::move(baseline));
    configurationManagement = createConfigurationManagement(session, scheduler);
    const std::weak_ptr<Workspace> weak = shared_from_this();
    const auto definition = detail::require(kernel->registerPlugin("host.notebook", [weak](Context context) {
        auto self = weak.lock();
        if (!self)
            return failedTask(makeError(ErrorCode::ContextClosing, "workspace unavailable"));
        self->configurationPage =
            detail::require(self->shell.pages()->add(context, {"configuration", "配置文件", "host.configuration",
                                                               [session = self->session, initial = self->initial] {
                                                                   return createConfigurationPage(session, initial);
                                                               },
                                                               PagePresentation::Dialog}));
        self->modulesPage = detail::require(
            self->shell.pages()->add(context, {"modules", "模块管理", "host.modules",
                                               [weak] { return modulesPanel(weak); }, PagePresentation::Dialog}));
        return self->configurationPage->ready();
    }));
    std::vector<RuntimeId> protectedRuntimes{notebook_->runtime->id(), definition.id()};
    if (counter_)
        protectedRuntimes.push_back(counter_->runtime->id());
    if (reporter_)
        protectedRuntimes.push_back(reporter_->runtime->id());
    detail::require(shell.management()->attachConfiguration(configurationManagement, std::move(protectedRuntimes)));
    host_ = detail::require(kernel->mount(definition));
    detail::require(shell.onConfigurationRequested([weak] {
        auto self = weak.lock();
        return self && self->configurationPage
                   ? self->configurationPage->navigate()
                   : Result<void>::failure(makeError(ErrorCode::ContextClosing, "configuration page unavailable"));
    }));
    detail::require(shell.onModulesRequested([weak] {
        auto self = weak.lock();
        return self && self->modulesPage
                   ? self->modulesPage->navigate()
                   : Result<void>::failure(makeError(ErrorCode::ContextClosing, "module page unavailable"));
    }));
}
Task<void> Workspace::ready() const
{
    if (!scheduler->isCurrentThread())
        return failedTask(makeError(ErrorCode::WrongThread, "workspace ready requires owner"));
    if (closeState_->closing || closeState_->closed || !host_)
        return failedTask(makeError(ErrorCode::ContextClosing, "workspace closing"));
    return host_->ready();
}
Result<AcceptedTarget> Workspace::apply(const std::string& text)
{
    if (!scheduler->isCurrentThread())
        return Result<AcceptedTarget>::failure(makeError(ErrorCode::WrongThread, "workspace apply requires owner"));
    if (closeState_->closing || closeState_->closed || !session)
        return Result<AcceptedTarget>::failure(makeError(ErrorCode::ContextClosing, "workspace closing"));
    auto draft = session->preview(text);
    return draft.hasValue() ? session->apply(draft.value()) : Result<AcceptedTarget>::failure(draft.error());
}
Result<WorkspaceCloseSnapshot> Workspace::closeSnapshot() const
{
    if (!scheduler->isCurrentThread())
        return Result<WorkspaceCloseSnapshot>::failure(
            makeError(ErrorCode::WrongThread, "workspace close snapshot requires owner"));
    auto value = *closeState_;
    value.deliveryBlocked = deliveryBlocked();
    value.retained = value.retained || value.deliveryBlocked;
    return Result<WorkspaceCloseSnapshot>::success(std::move(value));
}
} // namespace notebook
