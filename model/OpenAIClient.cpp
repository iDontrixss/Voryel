#include "OpenAIClient.h"
#include "ProviderUrlSecurity.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

namespace {

bool isImageAttachment(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "png" || suffix == "jpg" || suffix == "jpeg"
        || suffix == "webp" || suffix == "gif";
}

bool isTextAttachment(const QString &path) {
    QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QString fileName = info.fileName().toLower();
    static const QStringList textSuffixes = {
        "txt", "md", "markdown", "rst", "log", "csv", "tsv",
        "json", "jsonc", "json5", "yaml", "yml", "toml", "xml", "svg", "ini", "cfg", "conf", "properties", "env", "editorconfig",
        "c", "h", "cpp", "cc", "cxx", "hpp", "hh", "hxx", "ino",
        "cs", "java", "kt", "kts", "scala", "swift", "m", "mm",
        "py", "pyw", "rb", "php", "go", "rs", "dart", "lua", "pl", "pm", "r",
        "js", "jsx", "ts", "tsx", "mjs", "cjs", "vue", "svelte",
        "html", "htm", "css", "scss", "sass", "less",
        "sql", "graphql", "gql", "proto",
        "sh", "bash", "zsh", "fish", "ps1", "bat", "cmd",
        "cmake", "gradle", "pro", "pri", "qrc", "ui", "qml", "qss", "qbs", "bzl", "bazel",
        "dockerfile", "gitignore", "gitattributes", "npmrc", "yarnrc",
        "lock", "mod", "sum"
    };
    if (textSuffixes.contains(suffix))
        return true;

    static const QStringList knownNames = {
        "dockerfile", "makefile", "cmakelists.txt", "package.json", "package-lock.json",
        "pnpm-lock.yaml", "yarn.lock", "cargo.toml", "cargo.lock", "go.mod", "go.sum",
        "composer.json", "composer.lock", "gemfile", "gemfile.lock", "build.gradle", "settings.gradle",
        "pom.xml", "mix.exs", "deno.json", "tsconfig.json", "vite.config.ts", "vite.config.js",
        "webpack.config.js", "rollup.config.js", "eslint.config.js", "prettier.config.js",
        "tailwind.config.js", "tailwind.config.ts", "next.config.js", "next.config.ts",
        "svelte.config.js", "astro.config.mjs", "nuxt.config.ts", "app.config.ts",
        "build", "workspace", "build.bazel", "workspace.bazel",
        "requirements.txt", "pyproject.toml", "setup.py", "setup.cfg",
        ".gitignore", ".gitattributes", ".env", ".env.local", ".env.example",
        ".editorconfig", ".prettierrc", ".eslintrc", ".babelrc", ".npmrc"
    };
    return knownNames.contains(fileName);
}

bool isSensitiveAttachment(const QString &path) {
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QString fileName = info.fileName().toLower();
    static const QStringList sensitiveSuffixes = {
        "pem", "key", "p12", "pfx", "crt", "cer", "der", "kdbx"
    };
    if (sensitiveSuffixes.contains(suffix))
        return true;
    if (fileName == ".env" || fileName.startsWith(".env."))
        return true;
    if (fileName == ".npmrc" || fileName == ".yarnrc" || fileName == ".pypirc")
        return true;
    if (fileName == "id_rsa" || fileName == "id_ed25519" || fileName == "known_hosts")
        return true;
    return false;
}

QString mimeTypeForImage(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "jpg" || suffix == "jpeg") return "image/jpeg";
    if (suffix == "webp") return "image/webp";
    if (suffix == "gif") return "image/gif";
    return "image/png";
}

QString fileDisplayName(const QString &path) {
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}

QString readTextAttachmentForPrompt(const QString &path, QStringList &skippedFiles) {
    QFileInfo info(path);
    if (isSensitiveAttachment(path)) {
        skippedFiles.append(fileDisplayName(path) + " (omitido por posible secreto)");
        return QString();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        skippedFiles.append(fileDisplayName(path) + " (no se pudo leer)");
        return QString();
    }

    QByteArray data = file.read(180 * 1024 + 1);
    file.close();
    if (data.size() > 180 * 1024) {
        data = data.left(180 * 1024);
        skippedFiles.append(fileDisplayName(path) + " (recortado a 180 KB)");
    }

    QString content = QString::fromUtf8(data);
    if (content.isEmpty() && !data.isEmpty())
        content = QString::fromLocal8Bit(data);
    content.replace("\r\n", "\n");
    return QString("\n\n--- Archivo: %1 ---\n```%2\n%3\n```")
        .arg(fileDisplayName(path), info.suffix().toLower(), content);
}

} // namespace

OpenAIClient::OpenAIClient(QObject *parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)) {}

void OpenAIClient::sendChatCompletion(const QVector<Message> &messages, const ProviderConfig &config) {
    qDebug() << "OpenAIClient::sendChatCompletion: url =" << config.baseUrl << "model =" << config.modelId
             << "streaming =" << config.streaming << "messages count =" << messages.size();
    cancel();
    if (!isSafeProviderUrlForApiKey(config.baseUrl, config.apiKey)) {
        emit errorOccurred("Refusing to send API key over insecure HTTP provider URL. Use HTTPS or a local provider.");
        return;
    }
    m_currentConfig = config;
    m_sseBuffer.clear();
    m_fullResponse.clear();
    m_firstToken = true;

    QUrl url(config.baseUrl + "/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "text/event-stream");
    if (!config.apiKey.isEmpty())
        request.setRawHeader("Authorization", ("Bearer " + config.apiKey).toUtf8());

    QJsonObject body;
    body["model"] = config.modelId;
    QJsonArray messagesArray;
    bool requestHasImages = false;
    int lastUserWithAttachments = -1;
    for (int i = 0; i < messages.size(); ++i) {
        if (messages[i].role == "user" && !messages[i].attachments.isEmpty())
            lastUserWithAttachments = i;
    }

    for (int msgIndex = 0; msgIndex < messages.size(); ++msgIndex) {
        const auto &msg = messages[msgIndex];
        QJsonObject m;
        m["role"] = msg.role;
        if (msg.attachments.isEmpty()) {
            m["content"] = msg.content;
        } else {
            QJsonArray contentParts;
            QString text = msg.content;
            QStringList attachedFiles;
            QStringList skippedImages;
            QStringList skippedFiles;
            QString textAttachmentContent;
            int imageCount = 0;
            bool messageHasImages = false;
            const bool includeAttachmentPayload = (msgIndex == lastUserWithAttachments);

            for (const QString &path : msg.attachments) {
                attachedFiles.append(fileDisplayName(path));
                if (!includeAttachmentPayload)
                    continue;
                if (isTextAttachment(path)) {
                    textAttachmentContent += readTextAttachmentForPrompt(path, skippedFiles);
                    continue;
                }
                if (!isImageAttachment(path))
                    continue;
                if (imageCount >= 5) {
                    skippedImages.append(fileDisplayName(path) + " (límite de 5 imágenes)");
                    continue;
                }

                const qint64 maxImageBytes = 3 * 1024 * 1024;
                const qint64 fileSize = QFileInfo(path).size();
                if (fileSize > maxImageBytes) {
                    skippedImages.append(fileDisplayName(path) + " (supera 3 MB)");
                    continue;
                }

                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    skippedImages.append(fileDisplayName(path) + " (no se pudo leer)");
                    continue;
                }
                const QByteArray encoded = file.readAll().toBase64();
                file.close();
                if (encoded.size() > 4 * 1024 * 1024) {
                    skippedImages.append(fileDisplayName(path) + " (supera 4 MB en base64)");
                    continue;
                }

                QJsonObject imageUrl;
                imageUrl["url"] = "data:" + mimeTypeForImage(path) + ";base64," + QString::fromLatin1(encoded);

                QJsonObject imagePart;
                imagePart["type"] = "image_url";
                imagePart["image_url"] = imageUrl;
                contentParts.append(imagePart);
                requestHasImages = true;
                messageHasImages = true;
                ++imageCount;
            }

            if (!attachedFiles.isEmpty())
                text += "\n\nArchivos adjuntos: " + attachedFiles.join(", ");
            if (!textAttachmentContent.isEmpty())
                text += "\n\nContenido de archivos de texto/código:" + textAttachmentContent;
            if (!skippedFiles.isEmpty())
                text += "\nArchivos de texto/código con nota: " + skippedFiles.join(", ");
            if (!skippedImages.isEmpty())
                text += "\nImágenes no enviadas al modelo: " + skippedImages.join(", ");

            QJsonObject textPart;
            textPart["type"] = "text";
            textPart["text"] = text;
            if (messageHasImages) {
                contentParts.prepend(textPart);
                m["content"] = contentParts;
            } else {
                m["content"] = text;
            }
        }
        messagesArray.append(m);
    }
    const bool requestStreaming = config.streaming && !requestHasImages;
    m_streamActive = requestStreaming;
    body["stream"] = requestStreaming;
    body["messages"] = messagesArray;

    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    m_currentReply = m_manager->post(request, payload);

    connect(m_currentReply, &QNetworkReply::readyRead, this, &OpenAIClient::onReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &OpenAIClient::onFinished);
}

void OpenAIClient::cancel() {
    if (m_currentReply) {
        QNetworkReply *reply = m_currentReply;
        m_currentReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    m_sseBuffer.clear();
    m_fullResponse.clear();
    m_streamActive = false;
}

void OpenAIClient::onReadyRead() {
    if (!m_currentReply) return;
    if (!m_streamActive) return;
    m_sseBuffer.append(m_currentReply->readAll());
    processSSE();
}

void OpenAIClient::onFinished() {
    if (!m_currentReply) {
        qDebug() << "OpenAIClient::onFinished: m_currentReply is null, returning";
        return;
    }
    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            qDebug() << "OpenAIClient::onFinished: request cancelled";
            emit errorOccurred("Cancelado");
        } else {
            QString detail = reply->errorString();
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus > 0)
                emit errorOccurred(QString("Error HTTP %1: %2").arg(httpStatus).arg(detail));
            else
                emit errorOccurred(QString("No pude conectar con LM Studio. Verificá que el servidor local esté activo en %1").arg(m_currentConfig.baseUrl));
        }
        reply->deleteLater();
        return;
    }

    if (!m_fullResponse.isEmpty()) {
        qDebug() << "OpenAIClient::onFinished: streaming complete, response length =" << m_fullResponse.length();
        reply->deleteLater();
        return;
    }

    if (m_streamActive) {
        processSSE();
        if (!m_fullResponse.isEmpty()) {
            qDebug() << "OpenAIClient::onFinished: after SSE flush, response length =" << m_fullResponse.length();
            reply->deleteLater();
            return;
        }
    }

    QByteArray data = m_sseBuffer;
    data.append(reply->readAll());
    m_sseBuffer.clear();
    qDebug() << "OpenAIClient::onFinished: non-streaming fallback, data length =" << data.length();
    if (!data.isEmpty()) {
        handleNonStreamResponse(data);
    } else {
        emit errorOccurred("El proveedor no devolvió respuesta.");
    }
    reply->deleteLater();
}

void OpenAIClient::processSSE() {
    while (true) {
        int idx = m_sseBuffer.indexOf("\n\n");
        if (idx < 0) break;

        QByteArray event = m_sseBuffer.left(idx);
        m_sseBuffer.remove(0, idx + 2);

        const QList<QByteArray> lines = event.split('\n');
        for (const QByteArray &line : lines) {
            if (!line.startsWith("data: ")) continue;
            QByteArray data = line.mid(6).trimmed();
            if (data == "[DONE]") {
                m_streamActive = false;
                emit finished(m_fullResponse);
                return;
            }
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) continue;

            QJsonObject obj = doc.object();
            QJsonArray choices = obj["choices"].toArray();
            if (choices.isEmpty()) continue;

            QJsonObject choice = choices[0].toObject();
            QJsonObject delta = choice["delta"].toObject();
            QString content = delta["content"].toString();
            if (!content.isEmpty()) {
                m_fullResponse += content;
                emit tokenReceived(content);
            }

            QString finishReason = choice["finish_reason"].toString();
            if (!finishReason.isEmpty() && finishReason != "null") {
                m_streamActive = false;
            }
        }
    }
}

void OpenAIClient::handleNonStreamResponse(const QByteArray &data) {
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred("Error al procesar la respuesta del servidor.");
        return;
    }
    if (!doc.isObject()) {
        emit errorOccurred("Respuesta inesperada del servidor.");
        return;
    }
    QJsonObject obj = doc.object();
    if (obj.contains("error")) {
        QJsonObject error = obj["error"].toObject();
        QString message = error["message"].toString();
        if (message.isEmpty()) message = "El proveedor devolvió un error.";
        emit errorOccurred(message);
        return;
    }
    QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) {
        emit errorOccurred("El modelo no devolvió respuestas.");
        return;
    }
    QString content = choices[0].toObject()["message"].toObject()["content"].toString();
    m_fullResponse = content;
    emit tokenReceived(content);
    emit finished(content);
}

void OpenAIClient::detectModels(const QString &baseUrl) {
    qDebug() << "OpenAIClient::detectModels: checking" << baseUrl;
    QUrl url(baseUrl + "/models");
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "OpenAIClient::detectModels: failed:" << reply->errorString();
            emit modelsDetected({});
            return;
        }
        QByteArray data = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qDebug() << "OpenAIClient::detectModels: parse error";
            emit modelsDetected({});
            return;
        }
        QJsonObject obj = doc.object();
        QJsonArray modelsArray = obj["data"].toArray();
        QStringList ids;
        QMap<QString, int> modelContexts;
        for (const auto &m : modelsArray) {
            QJsonObject modelObj = m.toObject();
            QString id = modelObj["id"].toString();
            if (id.isEmpty()) continue;
            ids.append(id);

            // Parse context_length or similar fields
            int ctx = 0;
            const QStringList possibleKeys = {
                "context_length", "contextWindow", "context_window",
                "max_context_length", "max_context_tokens", "context_size",
                "n_ctx", "num_ctx", "input_token_limit", "max_input_tokens"
            };
            for (const QString &key : possibleKeys) {
                if (modelObj.contains(key)) {
                    QJsonValue val = modelObj[key];
                    if (val.isDouble())
                        ctx = (int)val.toDouble();
                    else if (val.isString()) {
                        bool ok = false;
                        int parsed = val.toString().toInt(&ok);
                        if (ok) ctx = parsed;
                    }
                    if (ctx > 0) break;
                }
            }
            if (ctx > 0)
                modelContexts[id] = ctx;
        }
        qDebug() << "OpenAIClient::detectModels: found" << ids.size() << "models";
        if (!modelContexts.isEmpty())
            qDebug() << "OpenAIClient::detectModels: context info for" << modelContexts.size() << "models";
        emit modelsDetected(ids);
        if (!modelContexts.isEmpty())
            emit modelsDetectedWithContext(modelContexts);
    });
}
