/** @file notebook_state_main.cpp
 * @brief 实际 Notebook 状态用例的独立 QApplication 入口，不执行升级事务。
 */
#include "notebook_state_p.hpp"
#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

namespace notebook_test {
std::string manifest(const QString& variant)
{
    NOTEBOOK_CHECK(variant == "a" || variant == "b" || variant == "legacy");
    return variant == "legacy" ? "qt-workspace/notebook/module.json"
                               : "qt-upgrade/notebook-" + variant.toStdString() + "/module.json";
}
std::string draftJson(const QString& text)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("draft"), text}}).toJson(QJsonDocument::Compact).toStdString();
}
StateSnapshot draftState(const QString& text)
{
    return {{schemaId, 1}, draftJson(text)};
}
} // namespace notebook_test
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    try {
        NOTEBOOK_CHECK(argc == 3);
        const auto root = QDir(QString::fromLocal8Bit(argv[1])).absolutePath();
        const auto name = QString::fromLocal8Bit(argv[2]);
        NOTEBOOK_CHECK(notebook_test::protocolCase(name, root) || notebook_test::runtimeCase(name, root));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
