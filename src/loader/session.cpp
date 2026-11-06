/** @file session.cpp
 * @brief 独占连接协调器和 Store，关闭重入不能使已接受目标漏过保存账目。
 */
#include "session_commit_p.hpp"
#include "target_commit_p.hpp"
#include <atomic>
#include <cordis/loader/session.hpp>
#include <exception>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace cordis::loader::detail {
/** @brief 跨线程只操作门禁及关闭 Promise，不触碰 owner 状态。 */
struct SessionDelivery {
    std::atomic_bool blocked{false};
    Promise<void> closed;
    /** @brief 丢失 owner 回执不能视为已完成关闭。 */
    void fail(ErrorInfo error)
    {
        blocked.store(true);
        closed.reject(std::move(error));
    }
};
/** @brief 会话保持唯一接受/跟踪顺序，候选仅弱持来源身份。 */
struct SessionControl : std::enable_shared_from_this<SessionControl> {
    std::shared_ptr<IScheduler> scheduler;
    std::shared_ptr<IReconcileDriver> driver;
    std::shared_ptr<IConfiguredReconcileDriver> configured;
    std::vector<PluginDefinition> definitions;
    YamlLimits limits;
    std::unique_ptr<ConfigurationCoordinator> coordinator;
    std::unique_ptr<ConfigurationStore> store;
    std::optional<AcceptedTarget> latest;
    std::shared_ptr<SessionDelivery> delivery = std::make_shared<SessionDelivery>();
    std::shared_ptr<SessionControl> pin;
    std::size_t accesses = 0, replacementHolds = 0;
    Promise<void> closingStarted;
    bool changing = false, closing = false, closed = false;
    bool coordinatorStarted = false, coordinatorDone = false, storeStarted = false, storeDone = false;
    std::vector<ErrorInfo> closeErrors;
    Result<std::shared_ptr<const ConfigurationDefinitions>> definitionView() const
    {
        using Output = Result<std::shared_ptr<const ConfigurationDefinitions>>;
        const auto source = configured;
        if (!source)
            return Output::success({});
        try {
            auto view = source->configurationDefinitions();
            if (view)
                return Output::success(std::move(view));
        } catch (...) {
            return Output::failure(makeError(ErrorCode::ConfigConflict, "definition lookup threw"));
        }
        return Output::failure(makeError(ErrorCode::ConfigConflict, "configured definitions unavailable"));
    }
    /** @brief 所有可变操作在 owner；诊断保留关闭后的状态。 */
    Result<void> check(bool business = true) const
    {
        if (!scheduler->isCurrentThread())
            return Result<void>::failure(makeError(ErrorCode::WrongThread, "session requires owner thread"));
        if (!business)
            return Result<void>::success();
        if (closing)
            return Result<void>::failure(makeError(ErrorCode::ContextClosing, "configuration session closing"));
        return available();
    }
    Result<void> available() const
    {
        if (delivery->blocked.load() || !scheduler->isOpen())
            return Result<void>::failure(
                makeError(ErrorCode::SchedulerClosed, "configuration session delivery unavailable"));
        if (changing)
            return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "configuration operation reentered"));
        if (store && store->snapshot().value().deliveryBlocked)
            return Result<void>::failure(
                makeError(ErrorCode::SchedulerClosed, "configuration store delivery unavailable"));
        return Result<void>::success();
    }
    /** @brief 业务栈返回前不关闭 Store；close 可立即关闭协调器的接受门禁。 */
    struct Access {
        SessionControl& state;
        /** @brief 先拒绝嵌套业务，再进入外部 schema/driver/调度边界。 */
        explicit Access(SessionControl& value) : state(value)
        {
            ++state.accesses;
            state.changing = true;
        }
        /** @brief 已接受目标跟踪完成后才允许最终保存。 */
        ~Access()
        {
            state.changing = false;
            --state.accesses;
            state.advanceClose();
        }
    };
    /** @brief 回执失败可从 worker 到达，资源由控制块保守自保留。 */
    void observe(const Task<void>& receipt)
    {
        cordis::detail::TaskAccess::observe(receipt, [gate = delivery](Task<void>::Snapshot result) {
            if (!result->hasValue())
                gate->fail(result->error());
        });
    }
    /** @brief 汇合驱动静止和最新目标保存两个独立证明。 */
    void advanceClose()
    {
        if (!closing || replacementHolds || closed || delivery->blocked.load())
            return;
        if (!coordinatorStarted) {
            coordinatorStarted = true;
            ++accesses;
            auto self = shared_from_this();
            observe(coordinator->close().thenOn(scheduler, [self](const Result<CleanupReport>& result) {
                self->coordinatorDone = true;
                if (!result.hasValue())
                    self->closeErrors.push_back(result.error());
                else if (!result.value().quiescent) {
                    auto error = makeError(ErrorCode::CleanupFailed, "configuration resources retained");
                    error.causes = result.value().errors;
                    self->closeErrors.push_back(std::move(error));
                }
                self->advanceClose();
            }));
            --accesses;
        }
        if (accesses)
            return;
        if (!storeStarted) {
            storeStarted = true;
            auto self = shared_from_this();
            observe(store->close().thenOn(scheduler, [self](const Result<void>& result) {
                self->storeDone = true;
                if (!result.hasValue())
                    self->closeErrors.push_back(result.error());
                self->advanceClose();
            }));
        }
        if (!coordinatorDone || !storeDone)
            return;
        closed = true;
        if (!closeErrors.empty()) {
            auto error = makeError(ErrorCode::CleanupFailed, "configuration shutdown or final save failed");
            error.causes = closeErrors;
            delivery->closed.reject(std::move(error));
            return;
        }
        delivery->closed.resolve();
        pin.reset();
    }
    /** @brief 完整校验候选后再发布差异，来源失效或关闭重入均不发布候选。 */
    Result<ConfigurationDraft> prepare(ParsedConfiguration parsed)
    {
        auto observed = definitionView();
        if (!observed.hasValue())
            return Result<ConfigurationDraft>::failure(observed.error());
        const auto view = std::move(observed).value();
        auto validated = view ? view->validate(std::move(parsed)) : validateCandidate(std::move(parsed), definitions);
        if (!validated.hasValue())
            return Result<ConfigurationDraft>::failure(validated.error());
        auto supported = [&]() -> Result<void> {
            try {
                return driver->preflight(validated.value());
            } catch (const std::exception& error) {
                return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, error.what()));
            } catch (...) {
                return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "driver preflight threw"));
            }
        }();
        if (!supported.hasValue())
            return Result<ConfigurationDraft>::failure(supported.error());
        // snapshot 也会进入外部 driver；所有回调返回后才核对定义和关闭状态。
        auto state = coordinator->snapshot();
        if (!state.hasValue())
            return Result<ConfigurationDraft>::failure(state.error());
        auto currentView = definitionView();
        if (!currentView.hasValue())
            return Result<ConfigurationDraft>::failure(currentView.error());
        if (currentView.value() ? !currentView.value()->owns(validated.value())
                                : validated.value().hasDefinitionIdentity())
            return Result<ConfigurationDraft>::failure(
                makeError(ErrorCode::ConfigConflict, "definitions changed during validation"));
        if (closing)
            return Result<ConfigurationDraft>::failure(
                makeError(ErrorCode::ContextClosing, "session closed during validation"));
        auto empty = validateDocument(Document{}).value();
        const auto& base = state.value().desired ? state.value().desired->source().structure() : empty;
        auto changes = previewChanges(base, validated.value().source().structure(), state.value().desiredRevision);
        return Result<ConfigurationDraft>::success(ConfigurationDraft(
            shared_from_this(), std::make_shared<const ValidatedCandidate>(std::move(validated).value()),
            std::move(changes)));
    }
    /** @brief Coordinator 接受是唯一修订来源，Store 的跟踪在同一 owner 栈内完成。 */
    Result<AcceptedTarget> accept(const ConfigurationDraft& draft)
    {
        if (draft.owner_.lock().get() != this || !draft.candidate_ || !draft.candidate_->source().isValid())
            return Result<AcceptedTarget>::failure(
                makeError(ErrorCode::InvalidArgument, "draft belongs to another session"));
        auto accepted = coordinator->accept(*draft.candidate_, draft.baseRevision());
        if (!accepted.hasValue())
            return accepted;
        // 接受流程可能重入 close，但 Store 直到 Access 返回后才关门。
        auto desired = coordinator->snapshot().value().desired;
        auto tracked = store->track(desired, accepted.value().revision);
        if (!tracked.hasValue()) {
            delivery->fail(tracked.error());
            return Result<AcceptedTarget>::failure(tracked.error());
        }
        latest.emplace(accepted.value());
        pin = shared_from_this();
        return accepted;
    }
};
std::shared_ptr<SessionControl> makeSession(std::shared_ptr<IScheduler> scheduler,
                                            std::shared_ptr<IReconcileDriver> driver,
                                            std::shared_ptr<IConfigurationWriter> writer,
                                            ConfigurationFileSnapshot baseline, YamlLimits limits,
                                            StoreLimits storeLimits)
{
    if (!scheduler || !scheduler->isOpen() || !scheduler->isCurrentThread() || !driver)
        throw std::invalid_argument("configuration session requires open owner scheduler and driver");
    auto state = std::make_shared<SessionControl>();
    state->scheduler = std::move(scheduler);
    state->driver = std::move(driver);
    state->limits = limits;
    state->coordinator = std::make_unique<ConfigurationCoordinator>(state->scheduler, state->driver);
    state->store =
        std::make_unique<ConfigurationStore>(state->scheduler, std::move(writer), std::move(baseline), storeLimits);
    return state;
}
struct SessionHoldControl {
    std::shared_ptr<SessionControl> owner;
    bool released = false;
    explicit SessionHoldControl(std::shared_ptr<SessionControl> state) : owner(std::move(state))
    {
        ++owner->replacementHolds;
    }
    void release()
    {
        if (!owner->scheduler->isCurrentThread()) {
            owner->delivery->fail(makeError(ErrorCode::WrongThread, "session replacement release requires owner"));
            return;
        }
        if (released)
            return;
        released = true;
        --owner->replacementHolds;
        owner->advanceClose();
    }
};
SessionReplacementHold::~SessionReplacementHold()
{
    if (control_)
        control_->release();
}
Task<void> SessionReplacementHold::closing() const
{
    return control_->owner->closingStarted.task();
}
Result<SessionReplacementHold> SessionCommitAccess::hold(ConfigurationSession& facade, const IReconcileDriver& driver,
                                                         const Task<void>& settled)
{
    using Output = Result<SessionReplacementHold>;
    auto view = inspect(facade, driver);
    if (!view.hasValue())
        return Output::failure(view.error());
    auto state = facade.control_;
    auto control = std::make_shared<SessionHoldControl>(state);
    SessionReplacementHold result(control);
    state->observe(
        settled.thenOn(state->scheduler, [weak = std::weak_ptr<SessionHoldControl>(control)](const Result<void>&) {
            if (auto value = weak.lock())
                value->release();
        }));
    return Output::success(std::move(result));
}
struct SessionCommitControl {
    std::shared_ptr<SessionControl> owner;
    std::shared_ptr<SessionHoldControl> hold;
    std::unique_ptr<SessionControl::Access> access;
    std::optional<PreparedTargetCommit> targets;
    std::optional<AcceptedTarget> next;
    Promise<PlanReport> done;
    PlanReport report;
    std::uint64_t previous = 0;
    TargetCommitMode mode = TargetCommitMode::Advance;
    std::thread::id thread = std::this_thread::get_id();
    bool published = false, finished = false;
    void release()
    {
        targets.reset();
        access.reset();
    }
    ~SessionCommitControl()
    {
        release();
        if (!finished)
            done.reject(makeError(ErrorCode::OperationCancelled, "configuration replacement completion abandoned"));
    }
};
static_assert(std::is_nothrow_swappable_v<std::optional<AcceptedTarget>>);
Result<SessionCommitAccess::View> SessionCommitAccess::inspect(ConfigurationSession& facade,
                                                               const IReconcileDriver& driver,
                                                               const SessionReplacementHold* hold)
{
    using Output = Result<View>;
    const auto state = facade.control_;
    auto admitted = state->check();
    if (!admitted.hasValue() && admitted.error().code != ErrorCode::ContextClosing)
        return Output::failure(admitted.error());
    const auto activity = hold ? hold->control_ : nullptr;
    const bool held = activity && !activity->released && activity->owner == state;
    if (!admitted.hasValue()) {
        if (!held)
            return Output::failure(admitted.error());
        auto available = state->available();
        if (!available.hasValue())
            return Output::failure(available.error());
    }
    if (state->driver.get() != &driver || !state->configured || !state->latest || state->accesses)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "replacement session does not own driver"));
    SessionControl::Access access(*state);
    auto current = state->coordinator->snapshot();
    if (!current.hasValue())
        return Output::failure(current.error());
    auto definitions = state->definitionView();
    if (!definitions.hasValue() || !definitions.value() || !current.value().desired ||
        !definitions.value()->owns(*current.value().desired))
        return Output::failure(makeError(ErrorCode::ConfigConflict, "session target definition source differs"));
    if ((state->closing && (!held || activity->released)) || state->delivery->blocked.load() || !state->latest ||
        state->latest->revision != current.value().desiredRevision || !current.value().desired)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "replacement session inspection invalidated"));
    return Output::success({current.value().desiredRevision, current.value().desired, state->latest->settled,
                            !current.value().activeRevision && state->latest->settled.isReady()});
}
Result<PreparedSessionCommit> SessionCommitAccess::prepare(ConfigurationSession& facade, const IReconcileDriver& driver,
                                                           std::shared_ptr<const ValidatedCandidate> target,
                                                           std::uint64_t expected, TargetCommitMode mode,
                                                           const std::vector<EntryOperation>& applied,
                                                           const SessionReplacementHold* hold)
{
    using Output = Result<PreparedSessionCommit>;
    const auto state = facade.control_;
    auto admitted = state->check();
    if (!admitted.hasValue() && admitted.error().code != ErrorCode::ContextClosing)
        return Output::failure(admitted.error());
    const auto activity = hold ? hold->control_ : nullptr;
    const bool held = activity && !activity->released && activity->owner == state;
    if (!admitted.hasValue()) {
        if (!held || mode != TargetCommitMode::Preserve)
            return Output::failure(admitted.error());
        auto available = state->available();
        if (!available.hasValue())
            return Output::failure(available.error());
    }
    if (state->driver.get() != &driver || !state->configured || !state->latest || state->latest->revision != expected ||
        state->changing || state->accesses || (mode != TargetCommitMode::Advance && mode != TargetCommitMode::Preserve))
        return Output::failure(
            makeError(ErrorCode::ConfigConflict, "replacement does not own this configured session"));
    auto value = std::make_shared<SessionCommitControl>();
    value->owner = state;
    if (held)
        value->hold = activity;
    value->previous = expected;
    value->mode = mode;
    value->access = std::make_unique<SessionControl::Access>(*state);
    auto targets = TargetCommitAccess::prepare(*state->coordinator, *state->store, std::move(target), expected, mode);
    if (!targets.hasValue())
        return Output::failure(targets.error());
    value->targets.emplace(std::move(targets).value());
    value->report.revision = value->targets->revision();
    value->report.outcome = PlanOutcome::Applied;
    if (mode == TargetCommitMode::Advance) {
        value->report.entries.reserve(applied.size());
        for (const auto& operation : applied)
            value->report.entries.push_back({operation, EntryStatus::Applied, {}});
        value->next.emplace(AcceptedTarget{value->report.revision, value->done.task()});
    }
    PreparedSessionCommit result(std::move(value));
    if (!result.ready())
        return Output::failure(makeError(ErrorCode::ConfigConflict, "session replacement preparation invalidated"));
    return Output::success(std::move(result));
}
bool PreparedSessionCommit::ready() const noexcept
{
    const auto& value = control_;
    return value && value->thread == std::this_thread::get_id() && !value->published && !value->finished &&
           (!value->owner->closing ||
            (value->mode == TargetCommitMode::Preserve && value->hold && !value->hold->released)) &&
           !value->owner->delivery->blocked.load() && value->owner->changing && value->owner->accesses == 1 &&
           value->owner->latest && value->owner->latest->revision == value->previous && value->targets &&
           value->targets->ready();
}
void PreparedSessionCommit::publish() noexcept
{
    if (control_->published)
        return;
    control_->targets->publish();
    if (control_->mode == TargetCommitMode::Advance)
        control_->owner->latest.swap(control_->next);
    if (!control_->owner->pin)
        control_->owner->pin = control_->owner;
    control_->published = true;
}
Result<void> PreparedSessionCommit::finish(Result<void> notifications)
{
    const auto value = control_;
    if (!value || value->thread != std::this_thread::get_id())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "session completion requires owner"));
    if (!value->published || value->finished)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "session completion is not pending"));
    if (!notifications.hasValue()) {
        value->report.errors.push_back(notifications.error());
        value->report.outcome = PlanOutcome::Failed;
    }
    value->finished = true;
    value->release();
    value->done.resolve(std::move(value->report));
    return notifications;
}
std::optional<AcceptedTarget> PreparedSessionCommit::accepted() const
{
    if (!control_ || control_->mode != TargetCommitMode::Advance)
        return std::nullopt;
    return AcceptedTarget{control_->report.revision, control_->done.task()};
}
} // namespace cordis::loader::detail

namespace cordis::loader {
ConfigurationSession::ConfigurationSession(std::shared_ptr<IScheduler> scheduler,
                                           std::shared_ptr<IReconcileDriver> driver,
                                           std::vector<PluginDefinition> definitions,
                                           std::shared_ptr<IConfigurationWriter> writer,
                                           ConfigurationFileSnapshot baseline, YamlLimits limits,
                                           StoreLimits storeLimits)
{
    auto state = detail::makeSession(std::move(scheduler), std::move(driver), std::move(writer), std::move(baseline),
                                     limits, storeLimits);
    auto configured = std::dynamic_pointer_cast<IConfiguredReconcileDriver>(state->driver);
    if (configured && configured->configurationDefinitions())
        throw std::invalid_argument(
            "configured driver owns its schemas; use the shared definition session constructor");
    state->definitions = std::move(definitions);
    control_ = std::move(state);
}
ConfigurationSession::ConfigurationSession(std::shared_ptr<IScheduler> scheduler,
                                           std::shared_ptr<IConfiguredReconcileDriver> driver,
                                           std::shared_ptr<IConfigurationWriter> writer,
                                           ConfigurationFileSnapshot baseline, YamlLimits limits,
                                           StoreLimits storeLimits)
{
    auto state =
        detail::makeSession(std::move(scheduler), driver, std::move(writer), std::move(baseline), limits, storeLimits);
    if (!driver->configurationDefinitions())
        throw std::invalid_argument("complete shared definitions required");
    state->configured = std::move(driver);
    control_ = std::move(state);
}
ConfigurationSession::~ConfigurationSession() = default;
/** @brief scheduler 构造后不替换，可安全比较固定身份。 */
bool ConfigurationSession::usesScheduler(const std::shared_ptr<IScheduler>& scheduler) const noexcept
{
    return control_->scheduler == scheduler;
}
/** @brief 文本解析同样受输入预算约束，预览无运行或写盘副作用。 */
Result<ConfigurationDraft> ConfigurationSession::preview(const std::string& text) const
{
    auto state = control_;
    auto checked = state->check();
    if (!checked.hasValue())
        return Result<ConfigurationDraft>::failure(checked.error());
    detail::SessionControl::Access access(*state);
    auto parsed = parseConfiguration(text, state->limits);
    if (!parsed.hasValue())
        return Result<ConfigurationDraft>::failure(parsed.error());
    return state->prepare(std::move(parsed).value());
}
/** @brief 旧候选不会覆盖已经变化的期望目标。 */
Result<AcceptedTarget> ConfigurationSession::apply(const ConfigurationDraft& draft)
{
    auto state = control_;
    auto checked = state->check();
    if (!checked.hasValue())
        return Result<AcceptedTarget>::failure(checked.error());
    detail::SessionControl::Access access(*state);
    return state->accept(draft);
}
/** @brief 改变配置启用标志，不能绕过协调器直接停止配置拥有的实例。 */
Result<AcceptedTarget> ConfigurationSession::setEnabled(const std::string& path, bool enabled, std::uint64_t expected)
{
    auto state = control_;
    auto checked = state->check();
    if (!checked.hasValue())
        return Result<AcceptedTarget>::failure(checked.error());
    detail::SessionControl::Access access(*state);
    auto current = state->coordinator->snapshot().value();
    if (!current.desired || current.desiredRevision != expected)
        return Result<AcceptedTarget>::failure(makeError(ErrorCode::ConfigConflict, "configuration revision changed"));
    auto parsed = setEntryEnabled(current.desired->source(), path, enabled, state->limits);
    if (!parsed.hasValue())
        return Result<AcceptedTarget>::failure(parsed.error());
    auto draft = state->prepare(std::move(parsed).value());
    if (!draft.hasValue())
        return Result<AcceptedTarget>::failure(draft.error());
    return state->accept(draft.value());
}
/** @brief 模块目录属于 Session 的冻结定义，管理界面不能猜测或替换模块来源。 */
Result<AcceptedTarget> ConfigurationSession::addInstance(const std::string& id, const std::string& plugin,
                                                         const std::string& configuration, std::uint64_t expected)
{
    auto state = control_;
    auto checked = state->check();
    if (!checked.hasValue())
        return Result<AcceptedTarget>::failure(checked.error());
    detail::SessionControl::Access access(*state);
    auto current = state->coordinator->snapshot().value();
    if (current.desiredRevision != expected)
        return Result<AcceptedTarget>::failure(makeError(ErrorCode::ConfigConflict, "configuration revision changed"));
    const auto baseline = state->store->snapshot().value().baseline;
    if (!current.desired && baseline && baseline->version.exists)
        return Result<AcceptedTarget>::failure(
            makeError(ErrorCode::ConfigConflict, "apply existing disk configuration before adding an instance"));
    auto module = [&]() -> Result<PluginModule> {
        auto observed = state->definitionView();
        if (!observed.hasValue())
            return Result<PluginModule>::failure(observed.error());
        const auto view = std::move(observed).value();
        if (view)
            return view->module(plugin);
        for (const auto& definition : state->definitions)
            if (definition.id == plugin)
                return Result<PluginModule>::success(definition.module);
        return Result<PluginModule>::failure(makeError(ErrorCode::InvalidArgument, "plugin is not registered"));
    }();
    if (!module.hasValue())
        return Result<AcceptedTarget>::failure(module.error());
    auto source = current.desired ? Result<ParsedConfiguration>::success(current.desired->source())
                                  : parseConfiguration("formatVersion: 1\nplugins: []\n", state->limits);
    if (!source.hasValue())
        return Result<AcceptedTarget>::failure(source.error());
    auto parsed = addConfigurationEntry(source.value(), id, plugin, configuration, state->limits, module.value());
    if (!parsed.hasValue())
        return Result<AcceptedTarget>::failure(parsed.error());
    auto draft = state->prepare(std::move(parsed).value());
    if (!draft.hasValue())
        return Result<AcceptedTarget>::failure(draft.error());
    return state->accept(draft.value());
}
/** @brief 保存与执行相互独立，失败目标仍可按用户意愿持久化。 */
Result<Task<SaveReport>> ConfigurationSession::save(std::uint64_t expected)
{
    auto state = control_;
    auto checked = state->check();
    if (!checked.hasValue())
        return Result<Task<SaveReport>>::failure(checked.error());
    detail::SessionControl::Access access(*state);
    return state->store->save(expected);
}
/** @brief 显式采用冲突原文的版本，仅替换基线，不采用它的目标或启动插件。 */
Result<void> ConfigurationSession::rebaseExternal(std::uint64_t expected)
{
    auto state = control_;
    auto checked = state->check();
    if (!checked.hasValue())
        return checked;
    detail::SessionControl::Access access(*state);
    auto disk = state->store->snapshot().value().external;
    if (!disk)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "no captured conflict version"));
    return state->store->rebase(*disk, expected);
}
/** @brief 不轮询插件回调，仅复制各层真实值及最新历史任务结果。 */
Result<SessionSnapshot> ConfigurationSession::snapshot() const
{
    auto state = control_;
    auto checked = state->check(false);
    if (!checked.hasValue())
        return Result<SessionSnapshot>::failure(checked.error());
    if (state->changing)
        return Result<SessionSnapshot>::failure(
            makeError(ErrorCode::ConfigConflict, "configuration write in progress"));
    auto coordinator = state->coordinator->snapshot();
    auto store = state->store->snapshot();
    if (!coordinator.hasValue())
        return Result<SessionSnapshot>::failure(coordinator.error());
    if (!store.hasValue())
        return Result<SessionSnapshot>::failure(store.error());
    SessionSnapshot output{
        coordinator.value(), store.value(), {}, {}, state->closing, state->closed, state->delivery->blocked.load()};
    if (state->latest)
        if (auto result = state->latest->settled.tryResult()) {
            if (result->hasValue())
                output.latestPlan = result->value();
            else
                output.executionError = result->error();
        }
    return Result<SessionSnapshot>::success(std::move(output));
}
/** @brief 仅复制定义名称，管理界面不能调用或保留插件 schema。 */
Result<std::vector<std::string>> ConfigurationSession::registeredPlugins() const
{
    const auto state = control_;
    auto checked = state->check(false);
    if (!checked.hasValue())
        return Result<std::vector<std::string>>::failure(checked.error());
    auto observed = state->definitionView();
    if (!observed.hasValue())
        return Result<std::vector<std::string>>::failure(observed.error());
    if (observed.value())
        return Result<std::vector<std::string>>::success(observed.value()->plugins());
    std::vector<std::string> names;
    for (const auto& definition : state->definitions)
        names.push_back(definition.id);
    return Result<std::vector<std::string>>::success(std::move(names));
}
/** @brief 先关闭接受门禁，再等业务栈结束并关闭 Store，最后汇合两个回执。 */
Task<void> ConfigurationSession::close()
{
    auto state = control_;
    auto checked = state->check(false);
    if (!checked.hasValue())
        return failedTask(checked.error());
    if (state->closing)
        return state->delivery->closed.task();
    state->closing = true;
    state->pin = state;
    ++state->accesses;
    state->closingStarted.resolve();
    --state->accesses;
    state->advanceClose();
    return state->delivery->closed.task();
}
} // namespace cordis::loader
