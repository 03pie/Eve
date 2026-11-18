/** @file platform_upgrade_main.cpp
 * @brief 独立进程运行实际业务升级及投递故障场景。
 */
#include "platform_upgrade_p.hpp"
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
        PLATFORM_CHECK(platform_upgrade_test::nativeCase(name, root) || platform_upgrade_test::qtCase(name, root) ||
                       platform_upgrade_test::contractCase(name, root));
        std::cout << "Platform upgrade passed: " << name.toStdString() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
