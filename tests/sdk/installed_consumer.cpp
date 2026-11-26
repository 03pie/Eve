#include "workbench.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTest>
#include <algorithm>
#include <iostream>

namespace {
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

template<class T>
auto await(const cordis::Task<T>& task)
{
    until([&] { return task.isReady(); }, "The workbench task did not settle.");
    auto result = task.tryResult();
    if (!result->hasValue())
        throw std::runtime_error(result->error().message);
    return result;
}

void close(cordis::workbench::Workbench& host)
{
    await(host.close());
    host.scheduler->close().value();
    const auto scheduler = host.scheduler->snapshot().value();
    if (host.session || host.kernel || scheduler.open || scheduler.queued || scheduler.callbackErrors)
        throw std::runtime_error("The workbench retained live resources after closing.");
}

void verifyInstalledConsumer(const QString& manifest, const QString& workspace)
{
    cordis::workbench::Workbench host;
    try {
        host.open(manifest, workspace);
        host.publishUi();
        const auto applied = await(host.applyInitial().settled);
        if (applied->value().outcome != cordis::loader::PlanOutcome::Applied || !applied->value().errors.empty())
            throw std::runtime_error("The workbench could not activate the template module.");
        until([&] {
            const auto pages = host.shell.pages()->snapshot().value();
            return std::any_of(pages.begin(), pages.end(), [](const auto& page) {
                return page.id == "external-page" && page.widgetAlive && page.navigationPresent;
            });
        }, "The installed SDK template did not contribute its page.");
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

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const auto args = application.arguments();
    if (args.size() != 3) {
        std::cerr << "Usage: cordis_sdk_consumer_tests module.json workspace\n";
        return 1;
    }
    QStandardPaths::setTestModeEnabled(true);
    application.setApplicationName("CordisSdkConsumer-" + QFileInfo(QFileInfo(args[2]).absolutePath()).fileName());
    try {
        verifyInstalledConsumer(args[1], args[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
