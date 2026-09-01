#pragma once

#include <QString>
#include <QPixmap>

class ProviderIconResolver {
public:
    // Accepts a provider name, URL, or the combined "Provider · Model" label.
    // Returns an embedded resource path or an empty string when no branded
    // asset can be matched safely.
    static QString iconPath(const QString &identity);

    // Renders branded SVGs to the same visible footprint. Provider SVGs use
    // different view boxes, so a plain QIcon makes detailed logos look tiny.
    static QPixmap normalizedPixmap(const QString &identity, int size);
};
