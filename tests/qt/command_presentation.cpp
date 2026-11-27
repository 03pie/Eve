/** @file command_presentation.cpp
 * @brief 真实发布组内命令准备、外部门禁与建议缓存复查。
 */
#include "ui_publication_p.hpp"
#include <ElaSuggestBox.h>
#include <QAbstractItemModel>
#include <QLineEdit>
#include <QListView>
#include <QMetaObject>
#include <QSortFilterProxyModel>
#include <optional>

namespace {
using namespace presentation_test;

void commandHeld()
{
    Scene scene;
    auto* list = scene.find<QListView>("EveCommandList");
    auto* search = scene.find<QLineEdit>("EveCommandFilter");
    search->setText("publication-command");
    CHECK(list->model()->rowCount() == 0);
    auto group = take(scene.kernel->holdPublication());
    std::optional<CommandHandle> command;
    unsigned predicates = 0, executions = 0;
    auto runtime = take(scene.kernel->registerPlugin("publication.commands", [&](Context context) {
        command.emplace(take(scene.commands->add(context, {"publication-command", "publication-command",
                                                           [&] {
                                                               ++executions;
                                                               return completedTask();
                                                           },
                                                           [&] {
                                                               ++predicates;
                                                               return true;
                                                           }})));
        return command->ready();
    }));
    auto instance = take(scene.kernel->mountHeld(group, runtime));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    CHECK(command && command->ready().isReady() && take(scene.commands->snapshot()).size() == 1);
    CHECK(list->model()->rowCount() == 0 && !command->enabled().hasValue());
    CHECK(!wait(command->execute(), scene.scheduler)->hasValue());
    CHECK(predicates == 0 && executions == 0 && take(command->snapshot()).activeCalls == 0);
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(list->model());
    CHECK(proxy && proxy->sourceModel()->rowCount() > 0);
    const auto rowsBefore = proxy->sourceModel()->rowCount();
    take(group.commit());
    until([&] { return list->model()->rowCount() == 1; });
    CHECK(proxy->sourceModel()->rowCount() == rowsBefore && predicates == 0);
    CHECK(take(command->enabled()));
    CHECK(wait(command->execute(), scene.scheduler)->hasValue());
    CHECK(predicates == 2 && executions == 1 && take(command->snapshot()).activeCalls == 0);

    // 撤销同步关门，排空尚未消费列表行时也不记录“开始执行”。
    const auto cached = list->model()->index(0, 0);
    CHECK(cached.isValid());
    const auto before = take(scene.shell.output()->snapshot()).size();
    const auto stopped = instance.stop();
    CHECK(QMetaObject::invokeMethod(list, "activated", Qt::DirectConnection, Q_ARG(QModelIndex, cached)));
    CHECK(take(scene.shell.output()->snapshot()).size() == before && executions == 1);
    CHECK(wait(stopped, scene.scheduler)->value().quiescent);
    CHECK(!command->enabled().hasValue() && take(scene.commands->snapshot()).empty());
    scene.close();
}

void commandAborted()
{
    Scene scene;
    auto* list = scene.find<QListView>("EveCommandList");
    scene.find<QLineEdit>("EveCommandFilter")->setText("aborted-command");
    auto group = take(scene.kernel->holdPublication());
    std::optional<CommandHandle> command;
    std::weak_ptr<int> capture;
    unsigned predicates = 0, executions = 0;
    auto runtime = take(scene.kernel->registerPlugin("aborted.commands", [&](Context context) {
        auto payload = std::make_shared<int>(7);
        capture = payload;
        command.emplace(take(scene.commands->add(context, {"aborted-command", "aborted-command",
                                                           [payload, &executions] {
                                                               ++executions;
                                                               return completedTask();
                                                           },
                                                           [&] {
                                                               ++predicates;
                                                               return true;
                                                           }})));
        return command->ready();
    }));
    auto instance = take(scene.kernel->mountHeld(group, runtime));
    CHECK(wait(instance.ready(), scene.scheduler)->hasValue());
    CHECK(!capture.expired() && list->model()->rowCount() == 0);
    take(group.abort());
    CHECK(wait(instance.stop(), scene.scheduler)->value().quiescent);
    CHECK(capture.expired() && predicates == 0 && executions == 0);
    CHECK(list->model()->rowCount() == 0 && take(scene.commands->snapshot()).empty());
    CHECK(!wait(command->execute(), scene.scheduler)->hasValue());
    scene.close();
}

void suggestionGate()
{
    Scene scene;
    auto* box = new ElaSuggestBox(scene.window);
    box->setObjectName("PresentationSuggestions");
    bool presented = false;
    unsigned chosen = 0;
    box->setSuggestionFilter([&](const ElaSuggestBox::SuggestData&) { return presented; });
    const auto key = box->addSuggestion("private-presentation-page", {{"page", "candidate"}});
    QString replacement;
    QObject::connect(box, &ElaSuggestBox::suggestionClicked, box, [&](const ElaSuggestBox::SuggestData& data) {
        if (data.getSuggestKey() == key)
            CHECK(data.getSuggestData().value("page").toString() == QStringLiteral("candidate"));
        else
            CHECK(data.getSuggestKey() == replacement &&
                  data.getSuggestData().value("page").toString() == QStringLiteral("replacement"));
        ++chosen;
    });
    auto* model = box->findChild<QAbstractItemModel*>();
    auto* edit = box->findChild<QLineEdit*>();
    CHECK(model && edit);
    QListView* view = nullptr;
    for (auto* candidate : scene.window->findChildren<QListView*>())
        if (candidate->model() == model)
            view = candidate;
    CHECK(view);
    box->setSearchText("private-presentation");
    CHECK(model->rowCount() == 0);
    presented = true;
    box->setSearchText("private-presentation");
    CHECK(model->rowCount() == 1);
    const auto cached = model->index(0, 0);
    presented = false;
    CHECK(QMetaObject::invokeMethod(view, "clicked", Qt::DirectConnection, Q_ARG(QModelIndex, cached)));
    CHECK(chosen == 0 && model->rowCount() == 0);
    presented = true;
    box->setSearchText("private-presentation");
    CHECK(model->rowCount() == 1);
    presented = false;
    CHECK(QMetaObject::invokeMethod(edit, "returnPressed", Qt::DirectConnection));
    CHECK(chosen == 0);
    presented = true;
    box->setSearchText("private-presentation");
    CHECK(QMetaObject::invokeMethod(edit, "returnPressed", Qt::DirectConnection));
    CHECK(chosen == 1);

    replacement = box->addSuggestion("replacement-presentation-page", {{"page", "replacement"}});
    bool replaceSelection = false;
    QObject::connect(edit, &QLineEdit::textChanged, box, [&](const QString& text) {
        if (replaceSelection && text == QStringLiteral("private-presentation-page")) {
            replaceSelection = false;
            box->setSearchText("replacement-presentation");
        }
    });
    // 点击和 Return 的内部 setText 均会发同步信号；新查询必须替代整个旧选择。
    for (const bool keyboard : {false, true}) {
        box->setSearchText("private-presentation");
        CHECK(model->rowCount() == 1);
        replaceSelection = true;
        if (keyboard)
            CHECK(QMetaObject::invokeMethod(edit, "returnPressed", Qt::DirectConnection));
        else
            CHECK(QMetaObject::invokeMethod(view, "clicked", Qt::DirectConnection,
                                            Q_ARG(QModelIndex, model->index(0, 0))));
        CHECK(!replaceSelection && chosen == 1);
        CHECK(box->getSearchText() == QStringLiteral("replacement-presentation") && model->rowCount() == 1);
    }
    CHECK(QMetaObject::invokeMethod(edit, "returnPressed", Qt::DirectConnection));
    CHECK(chosen == 2);
    delete box;
    scene.close();
}
} // namespace

bool commandPresentationCase(const QString& name)
{
    if (name == "commands_held")
        commandHeld();
    else if (name == "commands_aborted")
        commandAborted();
    else if (name == "suggestion_gate")
        suggestionGate();
    else
        return false;
    return true;
}
