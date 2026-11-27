/** @file installed_consumer.cpp
 * @brief 使用真实宿主启动外部 SDK 模块，检查工作区配置经保存和重开后的内容。
 */
#include "workbench.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTest>
#include <algorithm>
#include <iostream>

namespace {
// 使用旧工作区的绝对目录及插件自有的嵌套字段，数字超过双精度浮点数的精确整数范围。
constexpr char savedConfig[] = R"({"custom":{"nested":[true,{"sequence":9007199254740993}]},"dataRoot":"C:/legacy/library"})";

/** @brief 泵送应用线程的事件，直到条件满足或测试超时。 */
template<class Predicate>
void until(Predicate ready, const char* failure)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready()) {
        if (timer.elapsed() >= 8000)
            throw std::runtime_error(failure);
        QTest::qWait(5);
    }
}

/** @brief 等待异步结果并在失败时保留真实的宿主诊断。 */
template<class T>
auto await(const eve::Task<T>& task)
{
    until([&] { return task.isReady(); }, "The workbench task did not settle.");
    auto result = task.tryResult();
    if (!result->hasValue())
        throw std::runtime_error(result->error().message);
    return result;
}

/** @brief 等待宿主释放资源后关闭调度器，检查线程队列和会话均已清空。 */
void close(eve::workbench::Workbench& host)
{
    await(host.close());
    host.scheduler->close().value();
    const auto scheduler = host.scheduler->snapshot().value();
    if (host.session || host.kernel || scheduler.open || scheduler.queued || scheduler.callbackErrors)
        throw std::runtime_error("The workbench retained live resources after closing.");
}

/** @brief 应用插件自有配置，并显式保存接受的修订以供下个进程读取。 */
void savePluginConfiguration(eve::workbench::Workbench& host, const eve::loader::ParsedConfiguration& source)
{
    using namespace eve::loader;
    const auto preview = [&](const char* config) {
        const auto changed = setEntryConfiguration(source, "main", config).value();
        return host.session->preview(changed.normalizedYaml());
    };
    const auto invalid = preview("[]");
    if (invalid.hasValue() || invalid.error().phase != "loader.schema")
        throw std::runtime_error("The workbench must require an object for external module configuration.");
    if (!preview(R"({"dataRoot":{"relative":["content"]}})").hasValue())
        throw std::runtime_error("The workbench imposed a schema on plugin-owned fields.");
    const auto draft = preview(savedConfig).value();
    const auto accepted = host.session->apply(draft).value();
    const auto result = await(accepted.settled);
    if (result->value().outcome != PlanOutcome::Applied || !result->value().errors.empty())
        throw std::runtime_error("The workbench could not apply the plugin configuration.");
    const auto saved = await(host.session->save(accepted.revision).value());
    if (saved->value().outcome != SaveOutcome::Written)
        throw std::runtime_error("The workbench did not write the plugin configuration.");
}

/** @brief 经冻结的旧 SDK 插件页面触发命令，检查实际实例产生的输出。 */
void verifyFrozenNotebook(eve::workbench::Workbench& host)
{
    QPlainTextEdit* editor = nullptr;
    QPushButton* publish = nullptr;
    until([&] {
        for (auto* window : QApplication::topLevelWidgets()) {
            if (auto* page = window->findChild<QWidget*>("EveNotebookPage")) {
                editor = page->findChild<QPlainTextEdit*>("EveNotebookEditor");
                publish = page->findChild<QPushButton*>("EveNotebookPublish");
                if (editor && publish)
                    return true;
            }
        }
        return false;
    }, "The frozen plugin did not create its page.");
    const std::string marker = "SDK_020_FROZEN_COMMAND";
    editor->setPlainText(QString::fromStdString(marker));
    publish->click();
    until([&] {
        const auto rows = host.desktop()->output()->snapshot().value();
        return std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
            return row.owner && row.text == marker;
        });
    }, "The frozen plugin command did not produce instance-owned output.");
}

/** @brief 在真实宿主中验证外部模块及新建或重开的工作区。 */
void verifyInstalledConsumer(const QString& manifest, const QString& workspace, const QString& mode)
{
    eve::workbench::Workbench host;
    try {
        await(host.open(manifest, workspace));
        const auto applied = await(host.applyInitial().settled);
        if (applied->value().outcome != eve::loader::PlanOutcome::Applied || !applied->value().errors.empty())
            throw std::runtime_error("The workbench could not activate the template module.");
        if (mode == "frozen") {
            verifyFrozenNotebook(host);
        } else {
            const bool reopen = mode == "reopen";
            until([&] {
                const auto pages = host.desktop()->pages()->snapshot().value();
                return std::any_of(pages.begin(), pages.end(), [](const auto& page) {
                    return page.id == "external-page" && page.widgetAlive && page.navigationPresent;
                });
            }, "The installed SDK template did not contribute its page.");
            const auto session = host.session->snapshot().value();
            const auto& source = session.coordinator.desired->source();
            if (source.structure().entries().at("main").config != (reopen ? savedConfig : "{}"))
                throw std::runtime_error("Unexpected configuration of the active external module.");
            if (!reopen)
                savePluginConfiguration(host, source);
        }
    } catch (...) {
        try {
            close(host);
        } catch (const std::exception& error) {
            std::cerr << "Cleanup: " << error.what() << '\n';
        }
        throw;
    }
    close(host);
}
} // namespace

/** @brief 每次启动单独加载驻留模块，可在同一工作区验证首次创建或冷启动。 */
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const auto args = application.arguments();
    if (args.size() < 3 || args.size() > 4 ||
        (args.size() == 4 && args[3] != "create" && args[3] != "reopen" && args[3] != "frozen")) {
        std::cerr << "Usage: eve_sdk_consumer_tests module.json workspace [create|reopen|frozen]\n";
        return 1;
    }
    QStandardPaths::setTestModeEnabled(true);
    application.setApplicationName("EveSdkConsumer-" + QFileInfo(QFileInfo(args[2]).absolutePath()).fileName());
    try {
        verifyInstalledConsumer(args[1], args[2], args.value(3));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
