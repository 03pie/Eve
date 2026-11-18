/** @file modules_view_close.cpp
 * @brief 模块视图真实写栈与保存、关闭和回执丢弃的生命周期检查。
 */
#include "platform_workspace_p.hpp"
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <exception>
#include <optional>

namespace modules_view_test {
using namespace platform_workspace_test;
QWidget* openModules(Scene& scene);
void finishModules(Scene& scene);
namespace {
struct View {
    QPointer<QWidget> panel, dialog;
    QTableView* table = nullptr;
    QPushButton* refresh = nullptr;
    QPlainTextEdit* text = nullptr;

    explicit View(Scene& scene) : panel(openModules(scene)), dialog(panel->window())
    {
        table = panel->findChild<QTableView*>("CordisModuleTable");
        refresh = panel->findChild<QPushButton*>("CordisModuleDiagnosticsRefresh");
        text = panel->findChild<QPlainTextEdit*>("CordisModuleDiagnosticsText");
        auto* timer = panel->findChild<QTimer*>("CordisModulesRefreshTimer");
        PLATFORM_CHECK(table && refresh && text && timer);
        timer->stop();
    }
    bool extraVisible() const
    {
        for (int row = 0; row != table->model()->rowCount(); ++row)
            if (table->model()->index(row, 0).data(Qt::UserRole).toString() == "notebook")
                return table->model()->index(row, 3).data().toString().contains("extra");
        return false;
    }
};
struct Observation {
    QMetaObject::Connection connection;
    ~Observation() { QObject::disconnect(connection); }
};
void addExtra(Scene& scene)
{
    const auto revision = take(scene.state->session->snapshot()).coordinator.desiredRevision;
    const auto accepted =
        take(scene.state->session->addInstance("extra", "example.notebook", "{title: Extra}", revision));
    const auto result = wait(accepted.settled, scene.scheduler);
    PLATFORM_CHECK(result->hasValue() && result->value().outcome == PlanOutcome::Applied);
}
void savedInsideView(Scene& scene, const Task<void>& closing, const View& view)
{
    until([&] {
        const auto store = take(scene.state->session->snapshot()).store;
        return !store.dirty && !store.writing;
    });
    barrier(scene.scheduler);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    PLATFORM_CHECK(view.panel && view.dialog && scene.state->kernel && scene.state->session &&
                   !take(scene.state->kernel->snapshot()).closing && !take(scene.state->session->snapshot()).closing &&
                   !closing.isReady());
    const auto gate = scene.state->withModuleView([] {});
    PLATFORM_CHECK(!gate.hasValue() && gate.error().code == ErrorCode::ContextClosing);
}
void viewClose(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    View view(scene);
    PLATFORM_CHECK(!view.extraVisible());
    addExtra(scene);
    std::optional<Task<void>> closing;
    std::exception_ptr failure;
    bool observed = false;
    Observation observer;
    observer.connection =
        QObject::connect(view.table->model(), &QAbstractItemModel::dataChanged,
                         [&](const QModelIndex& first, const QModelIndex& last, const QVector<int>&) {
                             if (observed || first.column() > 3 || last.column() < 3 || !view.extraVisible())
                                 return;
                             observed = true;
                             try {
                                 closing.emplace(scene.state->close());
                                 savedInsideView(scene, *closing, view);
                             } catch (...) {
                                 failure = std::current_exception();
                             }
                         });
    view.refresh->click();
    if (failure)
        std::rethrow_exception(failure);
    PLATFORM_CHECK(observed && closing && wait(*closing, scene.scheduler)->hasValue());
    PLATFORM_CHECK(!view.panel && !view.dialog);
    finishModules(scene);
}
void saveConflict(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    const auto accepted = scene.apply(scene.state->initial);
    View view(scene);
    QFile external(scene.file);
    PLATFORM_CHECK(external.open(QIODevice::WriteOnly));
    PLATFORM_CHECK(external.write("external file\n") == 14);
    external.close();
    const auto result = wait(scene.state->close(), scene.scheduler);
    PLATFORM_CHECK(!result->hasValue() && result->error().code == ErrorCode::ConfigConflict);
    const auto snapshot = take(scene.state->modulesSnapshot());
    PLATFORM_CHECK(snapshot.open && !snapshot.configurationErrors.empty() && view.panel && view.dialog &&
                   !take(scene.state->kernel->snapshot()).closing && !take(scene.state->session->snapshot()).closing);
    view.refresh->click();
    PLATFORM_CHECK(view.text->toPlainText().contains(QString::fromStdString(result->error().message)));
    take(scene.state->withModuleView([] {}));
    take(scene.state->session->rebaseExternal(accepted.revision));
    PLATFORM_CHECK(wait(scene.state->close(), scene.scheduler)->hasValue() && !view.panel && !view.dialog);
    finishModules(scene);
}
void viewDiscard(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    View view(scene);
    PLATFORM_CHECK(!view.extraVisible());
    addExtra(scene);
    struct Probe {
        bool observed = false, inside = false, closed = false;
        std::exception_ptr failure;
    };
    const auto probe = std::make_shared<Probe>();
    std::optional<Task<void>> closing;
    Observation observer;
    observer.connection =
        QObject::connect(view.table->model(), &QAbstractItemModel::dataChanged,
                         [&](const QModelIndex& first, const QModelIndex& last, const QVector<int>&) {
                             if (probe->observed || first.column() > 3 || last.column() < 3 || !view.extraVisible())
                                 return;
                             probe->observed = true;
                             probe->inside = true;
                             try {
                                 closing.emplace(scene.state->close());
                                 savedInsideView(scene, *closing, view);
                                 // 写栈仍占用本轮 idle；其结算回执会排在这个真实 FIFO 工作之后。
                                 const auto scheduler = scene.scheduler;
                                 PLATFORM_CHECK(scheduler->post({[scheduler, probe] {
                                                                     try {
                                                                         PLATFORM_CHECK(!probe->inside);
                                                                         probe->closed = true;
                                                                         take(scheduler->close());
                                                                     } catch (...) {
                                                                         probe->failure = std::current_exception();
                                                                     }
                                                                 },
                                                                 [probe] {
                                                                     probe->failure = std::make_exception_ptr(
                                                                         std::runtime_error("queue closer discarded"));
                                                                 }}));
                             } catch (...) {
                                 probe->failure = std::current_exception();
                             }
                             probe->inside = false;
                         });
    view.refresh->click();
    PLATFORM_CHECK(probe->observed && closing && !probe->closed);
    until([&] { return closing->isReady() || static_cast<bool>(probe->failure); });
    if (probe->failure)
        std::rethrow_exception(probe->failure);
    QObject::disconnect(observer.connection);
    const auto failed = closing->tryResult();
    PLATFORM_CHECK(probe->closed && failed && !failed->hasValue() &&
                   failed->error().code == ErrorCode::SchedulerClosed);
    const auto result = take(scene.state->closeSnapshot());
    PLATFORM_CHECK(result.retained && result.deliveryBlocked && !result.closed && !result.session && !result.kernel &&
                   scene.state->deliveryBlocked() && view.panel && view.dialog && scene.state->kernel &&
                   scene.state->session && !take(scene.state->kernel->snapshot()).closing &&
                   !take(scene.state->session->snapshot()).closing);
    PLATFORM_CHECK(!take(scene.state->modulesSnapshot()).open);
    const auto queue = take(scene.scheduler->snapshot());
    PLATFORM_CHECK(!queue.open && queue.discarded > 0 && queue.callbackErrors == 0);
    std::weak_ptr<notebook::Workspace> workspace = scene.state;
    std::weak_ptr<Kernel> kernel = scene.state->kernel;
    std::weak_ptr<KernelReconcileDriver> driver = scene.state->driver;
    std::weak_ptr<ConfigurationSession> session = scene.state->session;
    scene.state.reset();
    PLATFORM_CHECK(!workspace.expired() && !kernel.expired() && !driver.expired() && !session.expired());
    // 独立故障进程保留真实未确认拥有者；不伪造 idle 或继续 finish。
}
} // namespace
bool closeCase(const QString& name, const QString& sourceBin)
{
    if (name == "view_close")
        viewClose(sourceBin);
    else if (name == "save_conflict")
        saveConflict(sourceBin);
    else if (name == "view_discard")
        viewDiscard(sourceBin);
    else
        return false;
    return true;
}
} // namespace modules_view_test
