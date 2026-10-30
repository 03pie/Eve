/** @file path_p.hpp
 * @brief 预检、装载与安装共用路径限定和祖先保护，不解析声明或执行模块。
 */
#pragma once
#include "native_file_p.hpp"
#include <memory>
#include <vector>
namespace cordis::module::detail {
inline bool modulePathInput(const std::string& value)
{
    return !value.empty() && value.size() <= 32768 && value.find('\0') == std::string::npos &&
           QString::fromUtf8(value.data(), static_cast<int>(value.size())).toUtf8().toStdString() == value;
}
inline bool fullyQualifiedModulePath(QString path)
{
    path = QDir::fromNativeSeparators(path);
    if (path.startsWith(QStringLiteral("//?/UNC/"), Qt::CaseInsensitive))
        path = QStringLiteral("//") + path.mid(8);
    else if (path.startsWith(QStringLiteral("//?/")))
        path = path.mid(4);
    if (path.size() >= 3 &&
        ((path[0] >= QLatin1Char('A') && path[0] <= QLatin1Char('Z')) ||
         (path[0] >= QLatin1Char('a') && path[0] <= QLatin1Char('z'))) &&
        path[1] == QLatin1Char(':') && path[2] == QLatin1Char('/'))
        return true;
    if (!path.startsWith(QStringLiteral("//")))
        return false;
    const auto serverEnd = path.indexOf(QLatin1Char('/'), 2);
    if (serverEnd <= 2)
        return false;
    const auto server = path.mid(2, serverEnd - 2);
    const auto share = path.mid(serverEnd + 1).section(QLatin1Char('/'), 0, 0);
    return server != QLatin1String(".") && server != QLatin1String("..") && server != QLatin1String("?") &&
           !share.isEmpty() && share != QLatin1String(".") && share != QLatin1String("..");
}
/** @brief 先保护盘根，再逐级固定已经解析的目录；不沿中途替换的 junction 继续。 */
inline bool protectModuleDirectories(const QString& directory, std::vector<std::unique_ptr<OpenedPath>>& guards)
{
    std::vector<QString> paths;
    auto current = directory;
    for (;;) {
        if (paths.size() >= 128)
            return false;
        paths.push_back(current);
        QDir parent(current);
        if (parent.isRoot())
            break;
        if (!parent.cdUp() || parent.absolutePath() == current)
            return false;
        current = parent.absolutePath();
    }
    for (auto at = paths.rbegin(); at != paths.rend(); ++at) {
        auto guard = std::make_unique<OpenedPath>(*at, true);
        if (!guard->valid() || guard->finalPath() != *at)
            return false;
        guards.push_back(std::move(guard));
    }
    return true;
}
} // namespace cordis::module::detail
