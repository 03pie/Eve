/** @file workspace_endurance_main.cpp
 * @brief 同一 Windows 应用中的固定真实业务耐久入口，不逐轮重建平台。
 */
#include "workspace_endurance_p.hpp"
#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <cstdlib>
#include <exception>
#include <iostream>

namespace workspace_endurance_test {
void runEndurance(Scene&, const QString& sourceBin, const QString& outputPrefix);
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    try {
        PLATFORM_CHECK(application.arguments().size() == 3);
        PLATFORM_CHECK(QGuiApplication::platformName() == QStringLiteral("windows"));
        const auto source = QDir(application.arguments()[1]).absolutePath();
        const auto output = QDir::cleanPath(application.arguments()[2]);
        workspace_endurance_test::Scene scene;
        workspace_endurance_test::runEndurance(scene, source, output);
        std::cout << "Workspace endurance passed: 8 warmup + 100 measured original Applied operations\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
