#pragma once
#include <QUrl>

namespace eve::ui::detail {
inline bool isExtensionWebLink(const QUrl& url)
{
    return url.isValid() && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http")) &&
           !url.host().isEmpty() && !url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'));
}
}
