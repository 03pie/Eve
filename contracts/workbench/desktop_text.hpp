#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTranslator>
#include <memory>
#include <utility>

namespace eve {
namespace detail {
inline constexpr char desktopTextContext[] = "EveDesktop";
inline constexpr int maxDesktopMessageBytes = 512 * 1024;

inline QJsonObject readDesktopMessages()
{
    const QDir application(QCoreApplication::applicationDirPath());
    QFile file(application.filePath(QStringLiteral("system/desktop/messages.en.json")));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.read(maxDesktopMessageBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maxDesktopMessageBytes) return {};
    const auto utf8 = bytes.startsWith("\xef\xbb\xbf") ? bytes.mid(3) : bytes;
    if (QString::fromUtf8(utf8).toUtf8() != utf8) return {};
    return QJsonDocument::fromJson(utf8).object();
}

inline const QJsonObject& desktopDefaultMessages()
{
    static const auto messages = readDesktopMessages();
    return messages;
}

inline QString defaultDesktopMessage(const QString& id)
{
    const auto english = desktopDefaultMessages().value(id).toString();
    return english.isEmpty() ? id : english;
}

/** Resolve optional language data before the editable English message catalog. */
class DesktopMessageTranslator : public QTranslator {
public:
    explicit DesktopMessageTranslator(QJsonObject overrides = {})
        : overrides_(std::move(overrides)) {}

    bool isEmpty() const override { return false; }

    QString translate(const char* context, const char* source, const char*, int) const override
    {
        if (QLatin1String(context) != QLatin1String(desktopTextContext)) return {};
        return message(QString::fromUtf8(source));
    }

protected:
    QString message(const QString& id) const
    {
        const auto translated = overrides_.value(id).toString();
        if (!translated.isEmpty()) return translated;
        return defaultDesktopMessage(id);
    }

private:
    QJsonObject overrides_;
};
}

/** Install the product's default messages before the desktop module is activated. */
inline std::unique_ptr<QTranslator> installDesktopDefaultTranslator()
{
    auto translator = std::make_unique<detail::DesktopMessageTranslator>();
    QCoreApplication::installTranslator(translator.get());
    return translator;
}

/** The source field is a stable catalog ID, used as a fallback when both files lack it. */
inline QString desktopText(const char* id)
{
    const auto text = QCoreApplication::translate(detail::desktopTextContext, id);
    const auto key = QString::fromUtf8(id);
    if (text != key || !QCoreApplication::instance()) return text;
    return detail::defaultDesktopMessage(key);
}
}
