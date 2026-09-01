#include "ProviderIconResolver.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QUrl>
#include <QVector>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

namespace {

struct ProviderIconEntry {
    QString id;
    QString normalizedId;
    QString normalizedName;
    QString normalizedHost;
};

QString normalized(QString value) {
    value = value.toLower();
    value.remove(QRegularExpression("[^a-z0-9]"));
    return value;
}

const QVector<ProviderIconEntry>& catalog() {
    static const QVector<ProviderIconEntry> entries = [] {
        QVector<ProviderIconEntry> result;
        QFile file(":/icons/icons/providers/catalog.json");
        if (!file.open(QIODevice::ReadOnly)) return result;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        for (const QJsonValue &value : document.array()) {
            const QJsonObject object = value.toObject();
            const QString id = object.value("id").toString();
            if (id.isEmpty()) continue;
            result.append({
                id,
                normalized(id),
                normalized(object.value("name").toString()),
                normalized(QUrl(object.value("api").toString()).host())
            });
        }
        return result;
    }();
    return entries;
}

QString resourceForId(const QString &id) {
    const QString path = ":/icons/icons/providers/" + id + ".svg";
    return QFile::exists(path) ? path : QString();
}

QString aliasMatch(const QString &candidate) {
    static const QMap<QString, QString> aliases = {
        { "amazonbedrock", "amazon-bedrock" },
        { "anthropic", "anthropic" },
        { "azureopenai", "azure" },
        { "bedrock", "amazon-bedrock" },
        { "cerebras", "cerebras" },
        { "claude", "anthropic" },
        { "cloudflare", "cloudflare-workers-ai" },
        { "cohere", "cohere" },
        { "deepinfra", "deepinfra" },
        { "deepseek", "deepseek" },
        { "fireworks", "fireworks-ai" },
        { "gemini", "google" },
        { "githubcopilot", "github-copilot" },
        { "githubmodels", "github-models" },
        { "googlevertex", "google-vertex" },
        { "groq", "groq" },
        { "huggingface", "huggingface" },
        { "kimi", "moonshotai" },
        { "mistral", "mistral" },
        { "moonshot", "moonshotai" },
        { "nvidia", "nvidia" },
        { "ollama", "ollama-cloud" },
        { "openai", "openai" },
        { "opencode", "opencode" },
        { "openrouter", "openrouter" },
        { "perplexity", "perplexity" },
        { "together", "togetherai" },
        { "venice", "venice" },
        { "vercel", "vercel" },
        { "xai", "xai" },
        { "zai", "zai" }
    };

    QString bestId;
    int bestLength = 0;
    for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
        if ((candidate == it.key() || candidate.contains(it.key()))
            && it.key().size() > bestLength) {
            bestId = it.value();
            bestLength = it.key().size();
        }
    }
    return resourceForId(bestId);
}

QString catalogMatch(const QString &candidate) {
    if (candidate.isEmpty()) return {};
    QString bestId;
    int bestScore = 0;
    for (const ProviderIconEntry &entry : catalog()) {
        const QStringList keys = {
            entry.normalizedId, entry.normalizedName, entry.normalizedHost
        };
        for (const QString &key : keys) {
            if (key.size() < 3) continue;
            int score = 0;
            if (candidate == key) score = 10000 + key.size();
            else if (key.size() >= 4 && candidate.contains(key))
                score = 5000 + key.size();
            else if (candidate.size() >= 4 && key.contains(candidate))
                score = 3000 + candidate.size();
            if (score > bestScore) {
                bestScore = score;
                bestId = entry.id;
            }
        }
    }
    return resourceForId(bestId);
}

QString resolveCandidate(const QString &candidate) {
    const QString normalizedCandidate = normalized(candidate);
    QString path = aliasMatch(normalizedCandidate);
    if (!path.isEmpty()) return path;
    return catalogMatch(normalizedCandidate);
}

} // namespace

QString ProviderIconResolver::iconPath(const QString &identity) {
    const QString providerPart = identity.section(QChar(0x00B7), 0, 0).trimmed();
    QString path = resolveCandidate(providerPart);
    if (!path.isEmpty()) return path;
    return resolveCandidate(identity);
}

QPixmap ProviderIconResolver::normalizedPixmap(const QString &identity, int size) {
    const QString path = iconPath(identity);
    if (path.isEmpty() || size <= 0) return {};

    // Render oversampled first, crop transparent SVG padding, then place the
    // mark inside a consistent 88% visual area. This keeps dense logos such as
    // NVIDIA as legible as compact ones without modifying third-party assets.
    constexpr int canvasSize = 256;
    QImage canvas(canvasSize, canvasSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) return {};
    QPainter painter(&canvas);
    renderer.render(&painter);
    painter.end();

    int left = canvas.width(), top = canvas.height(), right = -1, bottom = -1;
    for (int y = 0; y < canvas.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(canvas.constScanLine(y));
        for (int x = 0; x < canvas.width(); ++x) {
            if (qAlpha(line[x]) == 0) continue;
            left = qMin(left, x); top = qMin(top, y);
            right = qMax(right, x); bottom = qMax(bottom, y);
        }
    }
    if (right < left || bottom < top) return {};

    const QImage cropped = canvas.copy(QRect(QPoint(left, top), QPoint(right, bottom)));
    const int target = qMax(1, qRound(size * 0.88));
    const QImage scaled = cropped.scaled(target, target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap result(size, size);
    result.fill(Qt::transparent);
    QPainter resultPainter(&result);
    resultPainter.drawImage((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    return result;
}
