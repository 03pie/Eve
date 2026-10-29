/** @file module_artifacts.hpp
 * @brief 原生/Qt 装载检查复用的临时产物安装设施，不绑定测试全局或入口。
 */
#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

namespace cordis::testing::modules {
inline void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size() || !file.flush())
        throw std::runtime_error("cannot write module fixture");
}
inline QByteArray read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("cannot read module fixture");
    auto data = file.readAll();
    if (file.error() != QFile::NoError)
        throw std::runtime_error("module fixture read failed");
    return data;
}
inline QString install(const QString& source, const QString& directory, const QString& id, const QJsonArray& plugins,
                       const QString& loader = "c-abi")
{
    if (!QDir().mkpath(directory) || !QFile::copy(source, directory + "/module.dll"))
        throw std::runtime_error("cannot install isolated module fixture");
    const QJsonObject manifest{
        {"manifestVersion", 1},
        {"moduleId", id},
        {"moduleVersion", "1.0.0"},
        {"loader", loader},
        {"library", "module.dll"},
        {"sha256", QString::fromLatin1(
                       QCryptographicHash::hash(read(directory + "/module.dll"), QCryptographicHash::Sha256).toHex())},
        {"platform", "windows-x64"},
        {"abi", QJsonObject{{"major", 1}, {"minor", 0}}},
        {"plugins", plugins},
        {"unloadPolicy", "resident"}};
    const auto path = directory + "/module.json";
    write(path, QJsonDocument(manifest).toJson());
    return path;
}
} // namespace cordis::testing::modules
