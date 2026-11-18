/** @file platform_workspace_main.cpp
 * @brief 每个有限场景使用独立进程、真实三业务 DLL 和同一个 Workspace。
 */
#include "platform_workspace_p.hpp"
#include <QApplication>
#include <QDir>
#include <iostream>

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    try {
        PLATFORM_CHECK(application.arguments().size() == 3);
        const auto root = QDir(application.arguments()[1]).absolutePath();
        const auto name = application.arguments()[2];
        PLATFORM_CHECK(platform_workspace_test::runtimeCase(name, root) ||
                       platform_workspace_test::closeCase(name, root));
        std::cout << "Platform Workspace case passed: " << name.toStdString() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
