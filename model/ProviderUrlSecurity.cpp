#include "ProviderUrlSecurity.h"

#include <QUrl>

bool isSafeProviderUrlForApiKey(const QString &baseUrl, const QString &apiKey) {
    if (apiKey.trimmed().isEmpty()) return true;

    const QUrl url(baseUrl.trimmed());
    const QString scheme = url.scheme().toLower();
    if (scheme == "https") return true;
    if (scheme != "http") return false;

    const QString host = url.host().toLower();
    return host == "localhost" || host == "127.0.0.1" || host == "::1";
}
