/** @file module_inspect.cpp
 * @brief 默认只读预检；显式 --load 执行原生装载和描述握手，不启动业务插件。
 */
#include <QCoreApplication>
#include <QFileInfo>
#include <cordis/module/manager.hpp>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    const bool load = arguments.size() > 1 && arguments[1] == QLatin1String("--load");
    if ((load && arguments.size() != 4) || (!load && arguments.size() != 3)) {
        std::cerr << "Usage: cordis_module_inspect [--load] <plugin-root> <manifest-path-relative-to-root>\n";
        return 2;
    }
    const int first = load ? 2 : 1;
    const auto root = QFileInfo(arguments[first]).absoluteFilePath().toUtf8().toStdString();
    const auto path = arguments[first + 1].toUtf8().toStdString();
    if (load) {
        cordis::module::ModuleManager manager;
        const auto result = manager.load(root, path);
        if (!result.hasValue()) {
            std::cerr << result.error().phase << ": " << result.error().message << '\n';
            return 1;
        }
        const auto values = manager.snapshot().value();
        for (const auto& value : values) {
            if (value.generation == result.value().generation)
                std::cout << "Discovery accepted; no business plugin activated\nModule: " << value.manifest.moduleId
                          << "\nGeneration: " << value.generation.value() << "\nLibrary: " << value.libraryPath
                          << "\nResident until process exit: "
                          << (value.nativePinned ? "yes" : "loader reference retained") << '\n';
        }
        return 0;
    }
    const auto result = cordis::module::preflightModule(root, path);
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
