/** @file ui_services.cpp
 * @brief 真实 Qt/Ela 中验证命令排空、重入、服务恢复及纯值通知。
 */
#include <cordis/kernel.hpp>
#include <cordis/ui/ela_shell.hpp>
#include <QAbstractItemModel>
#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QTest>
#include <QTimer>
#include <QWidget>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace cordis;
using namespace cordis::ui;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
/** @brief 超时仅用于诊断失败，业务断言依赖任务和显式 gate。 */
void until(const std::function<bool()>& predicate)
{
    if (predicate()) return;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (predicate()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(0); deadline.start(5000); loop.exec();
    CHECK(predicate());
}
/** @brief 等待真实 owner 通知，标志不借用已退出的等待栈。 */
template<class T> void wait(const Task<T>& task, const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    auto seen = std::make_shared<bool>(false);
    auto receipt = task.thenOn(scheduler, [seen](const Result<T>&) { *seen = true; });
    until([&] { return receipt.isReady(); });
    CHECK(*seen && receipt.tryResult()->hasValue() && task.tryResult()->hasValue());
}
/** @brief 每项测试独立外壳，正常结束保持 Kernel、UI、scheduler 关闭顺序。 */
struct Scenario {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    Kernel kernel{scheduler};
    ElaShell shell{scheduler};
    std::shared_ptr<CommandService> commands = shell.commands();
    std::shared_ptr<NotificationService> notices = shell.notifications();
    /** @brief 全部 effect 确认静止后才关闭原生窗口及调度器。 */
    void close()
    {
        auto closed = kernel.close();
        wait(closed, scheduler);
        CHECK(closed.tryResult()->value().quiescent && commands->snapshot().value().empty());
        wait(shell.close(), scheduler);
        CHECK(scheduler->snapshot().value().callbackErrors == 0);
        CHECK(scheduler->close().hasValue());
    }
};
/** @brief 默认无谓词命令用于隔离注册和生命周期行为。 */
CommandSpec command(std::string id = "run")
{
    return {std::move(id), "执行", [] { return completedTask(); }, {}};
}
/** @brief 探针在 MSVC std::function 的第二次移动中关闭外壳，模拟注册入口检查后的重入。 */
struct RegistrationProbe {
    bool armed = false;
    int moves = 0;
    std::function<void()> action;
};
/** @brief 小对象存储的可调用对象，移动本身也能执行用户逻辑。 */
class ClosingCallable {
public:
    /** @brief 状态由测试持有，必须覆盖所有回调捕获的生命周期。 */
    explicit ClosingCallable(RegistrationProbe& probe) : probe_(&probe) {}
    /** @brief std::function 要求可复制。 */
    ClosingCallable(const ClosingCallable&) = default;
    /** @brief 先跳过入参移动，在接管到命令记录时触发重入。 */
    ClosingCallable(ClosingCallable&& other) noexcept : probe_(other.probe_)
    {
        if (probe_->armed && ++probe_->moves == 2) probe_->action();
    }
    /** @brief 本测试不应进入被拒绝的命令执行体。 */
    Task<void> operator()() const { return completedTask(); }
private:
    RegistrationProbe* probe_;
};
/** @brief 参数检查后移动回调可关闭宿主，注册不得在关闭成功后留下新 effect。 */
void registrationReentry()
{
    Scenario scene;
    std::optional<Context> context;
    auto runtime = scene.kernel.registerPlugin("registration", [&](Context ctx) { context = ctx; return completedTask(); }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    const auto effectsBefore = instance.snapshot().value().resources.effects.size();
    RegistrationProbe probe;
    std::optional<Task<void>> closed;
    probe.action = [&] { closed = scene.shell.close(); };
    CommandSpec spec{"registration", "注册重入", ClosingCallable(probe), {}};
    probe.armed = true;
    auto added = scene.commands->add(*context, std::move(spec));
#ifdef _MSC_VER
    CHECK(probe.moves >= 2 && closed.has_value());
#endif
    if (closed) {
        CHECK(!added.hasValue() && scene.commands->snapshot().value().empty());
        CHECK(instance.snapshot().value().resources.effects.size() == effectsBefore);
        wait(*closed, scene.scheduler);
    }
    scene.close();
}
/** @brief Qt 模型插入信号可以重入关闭；初始化返回前不能销毁命令捕获或窗口。 */
void rowReentry()
{
    Scenario scene;
    std::optional<Context> context;
    auto runtime = scene.kernel.registerPlugin("row", [&](Context ctx) { context = ctx; return completedTask(); }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    QListWidget* list = nullptr;
    for (auto window : QApplication::topLevelWidgets()) if (auto found = window->findChild<QListWidget*>("CordisCommandList")) list = found;
    CHECK(list);
    bool captureHeldInside = false;
    std::optional<Task<void>> closed;
    auto payload = std::make_shared<int>(1);
    std::weak_ptr<int> weak = payload;
    auto connection = QObject::connect(list->model(), &QAbstractItemModel::rowsInserted, list, [&] {
        closed = scene.shell.close();
        wait(completedTask(), scene.scheduler);
        captureHeldInside = !weak.expired() && !closed->isReady();
    });
    CommandSpec spec{"row", "插入重入", [payload] { return completedTask(); }, {}};
    payload.reset();
    auto added = scene.commands->add(*context, std::move(spec));
    QObject::disconnect(connection);
    CHECK(added.hasValue() && closed.has_value() && captureHeldInside);
    wait(*closed, scene.scheduler);
    CHECK(weak.expired() && scene.commands->snapshot().value().empty());
    scene.close();
}
/** @brief 名字按实例隔离；重复拒绝、手动关闭关门和旧代不可复活。 */
void namespaces()
{
    Scenario scene;
    std::vector<Context> contexts;
    std::vector<CommandHandle> handles;
    auto runtime = scene.kernel.registerPlugin("commands", [&](Context ctx) {
        contexts.push_back(ctx);
        handles.push_back(scene.commands->add(ctx, command()).value());
        return handles.back().ready();
    }).value();
    auto first = scene.kernel.mount(runtime).value();
    auto second = scene.kernel.mount(runtime).value();
    wait(first.ready(), scene.scheduler); wait(second.ready(), scene.scheduler);
    CHECK(scene.commands->snapshot().value().size() == 2);
    CHECK(!scene.commands->add(contexts.front(), command()).hasValue());
    wait(handles[0].execute(), scene.scheduler);
    auto closed = handles[0].close();
    CHECK(handles[0].snapshot().value().retiring && !handles[0].execute().tryResult()->hasValue());
    wait(closed, scene.scheduler);
    CHECK(closed.tryResult()->value().quiescent && scene.commands->snapshot().value().size() == 1);
    CHECK(handles[1].enabled().value());
    wait(first.stop(), scene.scheduler);
    CHECK(!scene.commands->add(contexts.front(), command("late")).hasValue());
    wait(second.stop(), scene.scheduler);
    CHECK(!handles[1].enabled().hasValue());
    scene.close();
}
/** @brief 并发调用分别完成；worker 源完成不等于 owner 退出，停止须等待最后一项。 */
void inflight()
{
    Scenario scene;
    Promise<void> firstGate, lastGate;
    std::optional<CommandHandle> handle;
    int entered = 0;
    auto payload = std::make_shared<int>(1);
    std::weak_ptr<int> weak = payload;
    auto runtime = scene.kernel.registerPlugin("async", [&](Context ctx) {
        handle = scene.commands->add(ctx, {"async", "异步", [&, payload] { return ++entered == 1 ? firstGate.task() : lastGate.task(); }, {}}).value();
        return handle->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value();
    wait(instance.ready(), scene.scheduler); payload.reset();
    auto invoked = handle->execute();
    auto last = handle->execute();
    CHECK(entered == 0 && !invoked.isReady() && !last.isReady() && handle->snapshot().value().activeCalls == 2);
    until([&] { return entered == 2; });
    auto stopped = instance.stop();
    until([&] { return handle->snapshot().value().retiring; });
    CHECK(!stopped.isReady() && !weak.expired() && !handle->execute().tryResult()->hasValue());
    std::thread worker([&] { firstGate.resolve(); }); worker.join();
    CHECK(!invoked.isReady() && !stopped.isReady() && !weak.expired());
    wait(invoked, scene.scheduler);
    CHECK(!last.isReady() && !stopped.isReady() && !weak.expired() && handle->snapshot().value().activeCalls == 1);
    std::thread finalWorker([&] { lastGate.resolve(); }); finalWorker.join();
    wait(last, scene.scheduler); wait(stopped, scene.scheduler);
    CHECK(weak.expired() && handle->snapshot().value().activeCalls == 0);
    scene.close();
}
/** @brief 已接受但尚未进入的调用在撤销后跳过插件代码，同时释放排队保留。 */
void queuedCancel()
{
    Scenario scene;
    int calls = 0, predicates = 0;
    std::optional<CommandHandle> handle;
    auto runtime = scene.kernel.registerPlugin("queued", [&](Context ctx) {
        handle = scene.commands->add(ctx, {"queued", "排队", [&] { ++calls; return completedTask(); }, [&] { ++predicates; return true; }}).value();
        return handle->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    CHECK(scene.commands->snapshot().value().size() == 1 && predicates == 0);
    auto invoked = handle->execute();
    auto closed = handle->close();
    CHECK(!closed.isReady() && !invoked.isReady());
    until([&] { return invoked.isReady(); });
    CHECK(!invoked.tryResult()->hasValue() && calls == 0 && predicates == 0);
    wait(closed, scene.scheduler);
    CHECK(handle->snapshot().value().activeCalls == 0);
    scene.close();
}
/** @brief 谓词禁用、谓词异常和执行异常均有结果，不泄漏调用计数。 */
void failures()
{
    Scenario scene;
    std::vector<CommandHandle> handles;
    int entered = 0;
    auto runtime = scene.kernel.registerPlugin("failures", [&](Context ctx) {
        handles.push_back(scene.commands->add(ctx, {"disabled", "禁用", [&] { ++entered; return completedTask(); }, [] { return false; }}).value());
        handles.push_back(scene.commands->add(ctx, {"predicate", "谓词错误", [] { return completedTask(); }, []() -> bool { throw std::runtime_error("predicate"); }}).value());
        handles.push_back(scene.commands->add(ctx, {"callback", "执行错误", []() -> Task<void> { throw std::runtime_error("callback"); }, {}}).value());
        return handles.back().ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    CHECK(!handles[0].enabled().value() && handles[1].enabled().error().code == ErrorCode::CallbackFailed);
    for (const auto& handle : handles) {
        auto task = handle.execute();
        until([&] { return task.isReady(); });
        CHECK(!task.tryResult()->hasValue() && handle.snapshot().value().activeCalls == 0);
    }
    CHECK(entered == 0);
    scene.close();
}
/** @brief 谓词中嵌套循环推进关闭，也必须等谓词返回才能释放回调和完成清理。 */
void predicateReentry()
{
    Scenario scene;
    std::optional<CommandHandle> handle;
    std::optional<Task<CleanupReport>> closed;
    bool pendingInside = false;
    int executed = 0;
    auto payload = std::make_shared<int>(1);
    std::weak_ptr<int> weak = payload;
    auto runtime = scene.kernel.registerPlugin("predicate", [&](Context ctx) {
        handle = scene.commands->add(ctx, {"predicate", "重入", [&] { ++executed; return completedTask(); }, [&, payload] {
            closed = handle->close();
            until([&] { return handle->snapshot().value().retiring; });
            QCoreApplication::processEvents();
            pendingInside = !closed->isReady() && !weak.expired();
            return true;
        }}).value();
        return handle->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler); payload.reset();
    auto invoked = handle->execute();
    until([&] { return invoked.isReady(); });
    CHECK(pendingInside && executed == 0 && !invoked.tryResult()->hasValue());
    wait(*closed, scene.scheduler);
    CHECK(weak.expired());
    scene.close();
}
/** @brief 提供者退出令消费者清理，新服务绑定恢复为新 epoch，旧句柄与 Context 拒绝。 */
void recovery()
{
    Scenario scene;
    std::vector<CommandHandle> handles;
    std::vector<Context> contexts;
    auto provider = scene.kernel.registerPlugin("ui", [&](Context ctx) {
        ctx.provide({"ui.commands"}, scene.commands).value();
        ctx.provide({"ui.notifications"}, scene.notices).value();
        return completedTask();
    }, {}, {ServiceOutput::of<CommandService>({"ui.commands"}), ServiceOutput::of<NotificationService>({"ui.notifications"})}).value();
    auto consumer = scene.kernel.registerPlugin("consumer", [&](Context ctx) {
        contexts.push_back(ctx);
        ctx.consume<CommandService>({"ui.commands"}).value().withService([&](CommandService& commands) { handles.push_back(commands.add(ctx, command()).value()); }).value();
        ctx.consume<NotificationService>({"ui.notifications"}).value().withService([&](NotificationService& notices) { notices.post(ctx, {NotificationLevel::Info, "已启动", {}}).value(); }).value();
        return handles.back().ready();
    }, {ServiceRequirement::of<CommandService>({"ui.commands"}), ServiceRequirement::of<NotificationService>({"ui.notifications"})}).value();
    auto client = scene.kernel.mount(consumer).value();
    until([&] { return client.snapshot().value().state == InstanceState::Pending; });
    auto initial = scene.kernel.mount(provider).value(); wait(initial.ready(), scene.scheduler); wait(client.ready(), scene.scheduler);
    auto old = handles.front();
    wait(initial.stop(), scene.scheduler);
    until([&] { return client.snapshot().value().state == InstanceState::Pending; });
    CHECK(scene.commands->snapshot().value().empty() && !old.execute().tryResult()->hasValue());
    CHECK(!scene.notices->post(contexts.front(), {NotificationLevel::Info, "旧代", {}}).hasValue());
    auto replacement = scene.kernel.mount(provider).value(); wait(replacement.ready(), scene.scheduler);
    until([&] { return client.snapshot().value().epoch == 2; }); wait(client.awaitActive(2), scene.scheduler);
    CHECK(handles.size() == 2 && handles.back().snapshot().value().owner.epoch == 2 && scene.notices->snapshot().value().size() == 2);
    wait(handles.back().execute(), scene.scheduler);
    scene.close();
}
/** @brief 通知按纯文本显示，历史有界，失败提交不占序号，实例停止不持插件引用。 */
void notifications()
{
    Scenario scene;
    std::optional<Context> context;
    auto runtime = scene.kernel.registerPlugin("notice", [&](Context ctx) { context = ctx; return completedTask(); }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    CHECK(!scene.notices->post(*context, {NotificationLevel::Info, "", {}}).hasValue());
    CHECK(!scene.notices->post(*context, {NotificationLevel::Info, std::string(NotificationService::textLimit + 1, 'x'), {}}).hasValue());
    CHECK(!scene.notices->post(*context, {static_cast<NotificationLevel>(99), "bad", {}}).hasValue());
    for (std::uint64_t i = 1; i <= 120; ++i)
        CHECK(scene.notices->post(*context, {NotificationLevel::Warning, "<b>纯文本</b>", OperationId(i)}).value() == i);
    auto history = scene.notices->snapshot().value();
    CHECK(history.size() == NotificationService::historyLimit && history.front().sequence == 21 && history.back().sequence == 120);
    CHECK(history.back().owner.instance == context->identity().value().instance && history.back().notification.operation == OperationId(120));
    QLabel* label = nullptr;
    for (auto window : QApplication::topLevelWidgets()) if (auto found = window->findChild<QLabel*>("CordisNotification")) label = found;
    CHECK(label && label->textFormat() == Qt::PlainText && label->text().contains("<b>"));
    wait(instance.stop(), scene.scheduler);
    CHECK(scene.notices->snapshot().value().size() == NotificationService::historyLimit);
    CHECK(!scene.notices->post(*context, {NotificationLevel::Info, "late", {}}).hasValue());
    scene.close();
    CHECK(scene.notices->snapshot().value().back().sequence == 120);
}
/** @brief worker 请求与异 scheduler Context 都拒绝，不破坏现有贡献或通知历史。 */
void guards()
{
    Scenario scene;
    std::optional<Context> context;
    std::optional<CommandHandle> handle;
    auto runtime = scene.kernel.registerPlugin("guard", [&](Context ctx) { context = ctx; handle = scene.commands->add(ctx, command()).value(); return handle->ready(); }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    bool rejected = false;
    std::thread worker([&] {
        rejected = !scene.commands->add(*context, command("worker")).hasValue() && !scene.commands->snapshot().hasValue() &&
            handle->execute().tryResult()->error().code == ErrorCode::WrongThread && !handle->enabled().hasValue() &&
            handle->close().tryResult()->error().code == ErrorCode::WrongThread && !handle->snapshot().hasValue() &&
            !scene.notices->post(*context, {NotificationLevel::Info, "worker", {}}).hasValue() && !scene.notices->snapshot().hasValue();
    });
    worker.join(); CHECK(rejected && handle->enabled().value());
    auto otherScheduler = std::make_shared<qt::QtScheduler>();
    Kernel other(otherScheduler);
    std::optional<Context> foreign;
    auto otherRuntime = other.registerPlugin("foreign", [&](Context ctx) { foreign = ctx; return completedTask(); }).value();
    auto otherInstance = other.mount(otherRuntime).value(); wait(otherInstance.ready(), otherScheduler);
    CHECK(scene.commands->add(*foreign, command()).error().code == ErrorCode::InvalidArgument);
    CHECK(scene.notices->post(*foreign, {NotificationLevel::Info, "foreign", {}}).error().code == ErrorCode::InvalidArgument);
    CHECK(!scene.commands->add(*context, {"", "bad", [] { return completedTask(); }, {}}).hasValue());
    wait(other.close(), otherScheduler); otherScheduler->close().value();
    scene.close();
}
/** @brief 外壳等待命令与页面全部退出后才销毁窗口，关闭期间通知和新贡献立即拒绝。 */
void shutdown()
{
    Scenario scene;
    Promise<void> gate;
    std::optional<Context> context;
    std::optional<CommandHandle> handle;
    QPointer<QWidget> page;
    bool entered = false;
    auto runtime = scene.kernel.registerPlugin("shutdown", [&](Context ctx) {
        context = ctx;
        handle = scene.commands->add(ctx, {"async", "异步", [&] { entered = true; return gate.task(); }, {}}).value();
        return scene.shell.pages()->add(ctx, {"page", "页面", "", [&] { auto widget = std::make_unique<QWidget>(); page = widget.get(); return widget; }}).value().ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    auto task = handle->execute(); until([&] { return entered; });
    auto closed = scene.shell.close();
    CHECK(!closed.isReady() && !scene.commands->add(*context, command()).hasValue() && !handle->enabled().hasValue());
    CHECK(!scene.notices->post(*context, {NotificationLevel::Info, "late", {}}).hasValue());
    until([&] { return page.isNull(); });
    CHECK(!closed.isReady() && handle->snapshot().value().activeCalls == 1);
    gate.resolve(); wait(task, scene.scheduler); wait(closed, scene.scheduler);
    CHECK(scene.commands->snapshot().value().empty());
    scene.close();
}
/** @brief 宿主列表通过回车执行命令，禁用错误显示为宿主纯值通知。 */
void interaction()
{
    Scenario scene;
    int entered = 0;
    std::optional<CommandHandle> handle;
    std::optional<CommandHandle> emptyError;
    auto runtime = scene.kernel.registerPlugin("interaction", [&](Context ctx) {
        handle = scene.commands->add(ctx, {"run", "执行", [&] { ++entered; return completedTask(); }, [&] { return entered == 0; }}).value();
        emptyError = scene.commands->add(ctx, {"empty-error", "空错误", [] {
            return failedTask(makeError(ErrorCode::CallbackFailed, ""));
        }, {}}).value();
        return emptyError->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    QListWidget* list = nullptr;
    for (auto window : QApplication::topLevelWidgets()) if (auto found = window->findChild<QListWidget*>("CordisCommandList")) list = found;
    CHECK(list && list->count() == 2);
    list->setCurrentRow(0);
    QTest::keyClick(list, Qt::Key_Return);
    CHECK(entered == 0);
    until([&] { return entered == 1 && handle->snapshot().value().activeCalls == 0; });
    QTest::keyClick(list, Qt::Key_Return);
    until([&] { return !scene.notices->snapshot().value().empty(); });
    CHECK(entered == 1 && scene.notices->snapshot().value().back().notification.level == NotificationLevel::Error);
    list->setCurrentRow(1);
    QTest::keyClick(list, Qt::Key_Return);
    until([&] { return emptyError->snapshot().value().activeCalls == 0; });
    // owner 还需消费宿主结果观察者，使用显式队列回执确认其已执行。
    wait(completedTask(), scene.scheduler);
    auto history = scene.notices->snapshot().value();
    CHECK(history.size() == 2 && !history.back().notification.text.empty());
    scene.close();
}
/** @brief scheduler 提前关闭不会伪报调用完成或清理成功，保留记录供诊断。 */
void blocked()
{
    Scenario scene;
    Promise<void> gate;
    bool entered = false;
    std::optional<CommandHandle> handle;
    auto runtime = scene.kernel.registerPlugin("blocked", [&](Context ctx) {
        handle = scene.commands->add(ctx, {"blocked", "阻断", [&] { entered = true; return gate.task(); }, {}}).value();
        return handle->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    auto task = handle->execute(); until([&] { return entered; });
    scene.scheduler->close().value();
    std::thread worker([&] { gate.resolve(); }); worker.join();
    CHECK(!task.isReady() && handle->snapshot().value().deliveryBlocked && handle->snapshot().value().activeCalls == 1);
    CHECK(!handle->execute().tryResult()->hasValue());
    // 故意违反正常关闭顺序；不强制清空无法证明静止的记录。
}
/** @brief 每项 CTest 运行独立 QApplication，平台由测试环境指定。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return EXIT_FAILURE;
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        const std::string name = argv[1];
        if (name == "namespaces") namespaces();
        else if (name == "registration_reentry") registrationReentry();
        else if (name == "row_reentry") rowReentry();
        else if (name == "inflight") inflight();
        else if (name == "queued_cancel") queuedCancel();
        else if (name == "failures") failures();
        else if (name == "predicate_reentry") predicateReentry();
        else if (name == "recovery") recovery();
        else if (name == "notifications") notifications();
        else if (name == "guards") guards();
        else if (name == "shutdown") shutdown();
        else if (name == "interaction") interaction();
        else if (name == "blocked") blocked();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
