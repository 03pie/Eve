/** @file module_inspect.cpp
 * @brief 从明确插件根目录只读预检 manifest 和产物；不会装载 DLL 或启动插件。
 */
#include <QCoreApplication>
#include <QFileInfo>
#include <cordis/module/manifest.hpp>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() != 3) {
        std::cerr << "Usage: cordis_module_inspect <plugin-root> <manifest-path-relative-to-root>\n";
        return 2;
    }
    const auto root = QFileInfo(arguments[1]).absoluteFilePath().toUtf8().toStdString();
    const auto result = cordis::module::preflightModule(root, arguments[2].toUtf8().toStdString());
    if (!result.hasValue()) {
        std::cerr << result.error().phase << ": " << result.error().message << '\n';
        return 1;
    }
    const auto& artifact = result.value();
    std::cout << "Preflight passed (library not loaded)\nModule: " << artifact.manifest().moduleId
              << "\nVersion: " << artifact.manifest().moduleVersion << "\nLibrary: " << artifact.libraryPath()
              << "\nSHA256: " << artifact.manifest().sha256 << "\nBytes: " << artifact.libraryBytes() << '\n';
    for (const auto& plugin : artifact.manifest().plugins)
        std::cout << "Plugin: " << plugin << '\n';
    return 0;
}
