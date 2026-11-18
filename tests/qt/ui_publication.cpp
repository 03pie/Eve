/** @file ui_publication.cpp
 * @brief 真实 UI 发布用例的独立进程入口。
 */
#include <ElaApplication.h>
#include <QApplication>
#include <QString>
#include <iostream>
#include <stdexcept>
bool pagePresentationCase(const QString&);
bool commandPresentationCase(const QString&);
bool elaPresentationCase(const QString&);
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try {
        if (argc != 2)
            throw std::runtime_error("expected one UI publication case");
        const auto name = QString::fromUtf8(argv[1]);
        if (name.startsWith("ela_"))
            eApp->init();
        if (!pagePresentationCase(name) && !commandPresentationCase(name) && !elaPresentationCase(name))
            throw std::runtime_error("unknown UI publication case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
