/** @file platform_workspace_runtime.cpp
 * @brief 核对三个实际模块在同一平台的来源、依赖业务、页面和现有配置启动。
 */
#include "platform_workspace_p.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVariant>
#include <algorithm>
#include <eve/module/install.hpp>
#include <set>

namespace platform_workspace_test {
bool runtimeCase(const QString& name, const QString& sourceBin)
{
    if (name != "normal" && name != "multi" && name != "existing_current")
        return false;
    Scene scene;
    if (name == "existing_current")
        installSession(scene, sourceBin, "b");
    const auto before = take(readConfigurationFile(scene.file));
    scene.open(sourceBin);
    PLATFORM_CHECK(take(scene.state->driver->snapshot()).empty() && scene.pages().empty());
    PLATFORM_CHECK(take(take(scene.state->counter())->session->snapshot()).instances.empty() &&
                   take(take(scene.state->reporter())->session->snapshot()).instances.empty());
    verifySources(scene, name == "existing_current" ? "1.2.0" : "1.1.0");
    if (name == "existing_current") {
        PLATFORM_CHECK(before.version.exists && scene.state->initial == before.text);
        PLATFORM_CHECK(take(readConfigurationFile(scene.file)).text == before.text);
    }
    auto source = scene.state->initial;
    if (name == "multi")
        source +=
            "\n  - {id: counter2, plugin: example.counter, module: counter, config: null, realms: "
            "{example.counter: second}}\n"
            "  - {id: reporter2, plugin: example.reporter, module: reporter, config: null, realms: "
            "{example.counter: second, example.report: second}}\n"
            "  - {id: notes2, plugin: example.notebook, module: notebook, config: {title: Separate, text: other}}\n";
    scene.apply(source);
    scene.business(name == "multi" ? 2u : 1u, name == "multi");
    const auto pages = scene.pages();
    scene.edit(pages[0].owner.instance, QString::fromUtf8("同一个平台：真实笔记输出"));
    if (pages.size() == 2) {
        scene.edit(pages[1].owner.instance, "independent second draft");
        PLATFORM_CHECK(scene.page(pages[0].owner.instance)->findChild<QPlainTextEdit*>()->toPlainText() !=
                       "independent second draft");
    }
    PLATFORM_CHECK(wait(scene.state->close(), scene.scheduler)->hasValue());
    scene.verifyClosed();
    scene.release();
    return true;
}
} // namespace platform_workspace_test
