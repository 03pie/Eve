/** @file contract.hpp
 * @brief 示例的纯值配置契约；宿主预览与 DLL 防御检查共用，不含进程身份或运行控制。
 */
#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <cordis/result.hpp>

namespace notebook {
struct Configuration {
    QString title = QString::fromUtf8("插件笔记"), text;
    bool failStart = false, failCleanup = false;
};
inline cordis::Result<Configuration> configuration(const std::string& source)
{
    using Output = cordis::Result<Configuration>;
    auto invalid = [] {
        return Output::failure(
            cordis::makeError(cordis::ErrorCode::ConfigInvalid,
                              "notebook expects title/text strings and optional failStart/failCleanup booleans"));
    };
    const auto json = QJsonDocument::fromJson(QByteArray::fromStdString(source));
    if (!json.isObject())
        return invalid();
    const auto object = json.object();
    Configuration value;
    for (auto item = object.begin(); item != object.end(); ++item) {
        if (item.key() == "title" || item.key() == "text") {
            if (!item.value().isString())
                return invalid();
            (item.key() == "title" ? value.title : value.text) = item.value().toString();
        } else if (item.key() == "failStart" || item.key() == "failCleanup") {
            if (!item.value().isBool())
                return invalid();
            (item.key() == "failStart" ? value.failStart : value.failCleanup) = item.value().toBool();
        } else
            return invalid();
    }
    if (value.title.trimmed().isEmpty() || value.title.toUtf8().size() > 128 || value.text.toUtf8().size() > 65536)
        return invalid();
    return Output::success(std::move(value));
}
} // namespace notebook
