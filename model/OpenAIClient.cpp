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
#include <QDateTime>

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

    // Send the complete file. If it exceeds the selected model's context,
    // the provider must report that explicitly; silently cutting user input
    // produces misleading analysis and fabricated "missing" sections.
    QByteArray data = file.readAll();
    file.close();

    QString content = QString::fromUtf8(data);
    if (content.isEmpty() && !data.isEmpty())
        content = QString::fromLocal8Bit(data);
    content.replace("\r\n", "\n");
    return QString("\n\n--- Archivo: %1 ---\n```%2\n%3\n```")
        .arg(fileDisplayName(path), info.suffix().toLower(), content);
}

QString providerIdFor(const ProviderConfig &config) {
    if (!config.providerId.trimmed().isEmpty())
        return config.providerId.trimmed().toLower();
    const QString url = config.baseUrl.toLower();
    if (url.contains("api.anthropic.com")) return "anthropic";
    if (url.contains("generativelanguage.googleapis.com")) return "gemini";
    return QString();
}

QString messageTextWithAttachments(const Message &message) {
    QString text = message.content;
    QStringList skipped;
    for (const QString &path : message.attachments) {
        if (isTextAttachment(path))
            text += readTextAttachmentForPrompt(path, skipped);
    }
    if (!skipped.isEmpty())
        text += "\nArchivos con nota: " + skipped.join(", ");
    return text;
}

bool appendImagePart(const QString &path, QJsonArray &parts, const QString &flavor) {
    if (!isImageAttachment(path) || QFileInfo(path).size() > 3 * 1024 * 1024)
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = file.readAll().toBase64();
    file.close();
    if (data.size() > 4 * 1024 * 1024) return false;

    if (flavor == "anthropic") {
        QJsonObject source;
        source["type"] = "base64";
        source["media_type"] = mimeTypeForImage(path);
        source["data"] = QString::fromLatin1(data);
        QJsonObject part;
        part["type"] = "image";
        part["source"] = source;
        parts.append(part);
    } else {
        QJsonObject inlineData;
        inlineData["mime_type"] = mimeTypeForImage(path);
        inlineData["data"] = QString::fromLatin1(data);
        QJsonObject part;
        part["inline_data"] = inlineData;
        parts.append(part);
    }
    return true;
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
    m_finishedEmitted = false;

    const QString providerId = providerIdFor(config);
    const QString modelLower = config.modelId.toLower();
    const bool openCodeGateway = providerId == "opencode_zen" || providerId == "opencode_go";
    const bool openCodeMessages =
        (providerId == "opencode_zen"
            && (modelLower.startsWith("claude-") || modelLower.startsWith("qwen")))
        || (providerId == "opencode_go"
            && (modelLower.startsWith("qwen") || modelLower.startsWith("minimax-")));
    const bool openCodeGemini = providerId == "opencode_zen" && modelLower.startsWith("gemini-");
    const bool openCodeResponses = providerId == "opencode_zen" && modelLower.startsWith("gpt-");

    if (providerId == "anthropic" || providerId == "claude" || openCodeMessages) {
        m_apiFlavor = ApiFlavor::Anthropic;
        QString baseUrl = config.baseUrl.trimmed();
        while (baseUrl.endsWith('/')) baseUrl.chop(1);
        QNetworkRequest request(QUrl(baseUrl + "/messages"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Accept", "text/event-stream");
        request.setRawHeader("anthropic-version", "2023-06-01");
        if (!config.apiKey.isEmpty()) {
            request.setRawHeader("x-api-key", config.apiKey.toUtf8());
            if (openCodeGateway)
                request.setRawHeader("Authorization", ("Bearer " + config.apiKey).toUtf8());
        }

        QJsonObject body;
        body["model"] = config.modelId;
        const int maxTokens = config.maxOutputTokens > 0 ? config.maxOutputTokens : 4096;
        body["max_tokens"] = maxTokens;
        body["stream"] = config.streaming;
        const bool supportsVisibleThinking =
            modelLower.contains("claude-3-7")
            || modelLower.contains("claude-sonnet-4")
            || modelLower.contains("claude-opus-4")
            || modelLower.contains("claude-haiku-4");
        if (supportsVisibleThinking && maxTokens >= 2048) {
            QJsonObject thinking;
            thinking["type"] = "enabled";
            thinking["budget_tokens"] = 1024;
            body["thinking"] = thinking;
        }
        QJsonArray nativeMessages;
        QStringList systemParts;
        for (const Message &message : messages) {
            if (message.role == "system") {
                systemParts.append(message.content);
                continue;
            }
            QJsonArray parts;
            QJsonObject textPart;
            textPart["type"] = "text";
            textPart["text"] = messageTextWithAttachments(message);
            parts.append(textPart);
            for (const QString &path : message.attachments)
                appendImagePart(path, parts, "anthropic");
            QJsonObject nativeMessage;
            nativeMessage["role"] = message.role == "assistant" ? "assistant" : "user";
            nativeMessage["content"] = parts;
            nativeMessages.append(nativeMessage);
        }
        if (!systemParts.isEmpty()) body["system"] = systemParts.join("\n\n");
        body["messages"] = nativeMessages;
        m_streamActive = config.streaming;
        m_currentReply = m_manager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(m_currentReply, &QNetworkReply::readyRead, this, &OpenAIClient::onReadyRead);
        connect(m_currentReply, &QNetworkReply::finished, this, &OpenAIClient::onFinished);
        return;
    }
    if (providerId == "gemini" || openCodeGemini) {
        m_apiFlavor = ApiFlavor::Gemini;
        QString baseUrl = config.baseUrl.trimmed();
        while (baseUrl.endsWith('/')) baseUrl.chop(1);
        QString model = config.modelId.trimmed();
        if (!model.startsWith("models/")) model.prepend("models/");
        const QString operation = config.streaming ? ":streamGenerateContent?alt=sse" : ":generateContent";
        QNetworkRequest request(QUrl(baseUrl + "/" + model + operation));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Accept", config.streaming ? "text/event-stream" : "application/json");
        if (!config.apiKey.isEmpty()) {
            request.setRawHeader("x-goog-api-key", config.apiKey.toUtf8());
            if (openCodeGateway)
                request.setRawHeader("Authorization", ("Bearer " + config.apiKey).toUtf8());
        }

        QJsonObject body;
        QJsonArray contents;
        QJsonArray systemParts;
        for (const Message &message : messages) {
            if (message.role == "system") {
                QJsonObject part; part["text"] = message.content; systemParts.append(part);
                continue;
            }
            QJsonArray parts;
            QJsonObject textPart;
            textPart["text"] = messageTextWithAttachments(message);
            parts.append(textPart);
            for (const QString &path : message.attachments)
                appendImagePart(path, parts, "gemini");
            QJsonObject content;
            content["role"] = message.role == "assistant" ? "model" : "user";
            content["parts"] = parts;
            contents.append(content);
        }
        if (!systemParts.isEmpty()) {
            QJsonObject instruction;
            instruction["parts"] = systemParts;
            body["system_instruction"] = instruction;
        }
        body["contents"] = contents;
        QJsonObject generationConfig;
        if (config.maxOutputTokens > 0)
            generationConfig["maxOutputTokens"] = config.maxOutputTokens;
        const bool supportsThoughtSummaries =
            modelLower.contains("gemini-2.5") || modelLower.contains("gemini-3");
        if (supportsThoughtSummaries) {
            QJsonObject thinkingConfig;
            thinkingConfig["includeThoughts"] = true;
            generationConfig["thinkingConfig"] = thinkingConfig;
        }
        if (!generationConfig.isEmpty())
            body["generationConfig"] = generationConfig;
        m_streamActive = config.streaming;
        m_currentReply = m_manager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(m_currentReply, &QNetworkReply::readyRead, this, &OpenAIClient::onReadyRead);
        connect(m_currentReply, &QNetworkReply::finished, this, &OpenAIClient::onFinished);
        return;
    }
    if (openCodeResponses) {
        m_apiFlavor = ApiFlavor::OpenAIResponses;
        QString baseUrl = config.baseUrl.trimmed();
        while (baseUrl.endsWith('/')) baseUrl.chop(1);
        QNetworkRequest request(QUrl(baseUrl + "/responses"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Accept", config.streaming ? "text/event-stream" : "application/json");
        if (!config.apiKey.isEmpty())
            request.setRawHeader("Authorization", ("Bearer " + config.apiKey).toUtf8());

        QJsonObject body;
        body["model"] = config.modelId;
        body["stream"] = config.streaming;
        QJsonArray input;
        QStringList instructions;
        for (const Message &message : messages) {
            if (message.role == "system") {
                instructions.append(message.content);
                continue;
            }
            QJsonObject item;
            item["role"] = message.role == "assistant" ? "assistant" : "user";
            item["content"] = messageTextWithAttachments(message);
            input.append(item);
        }
        if (!instructions.isEmpty()) body["instructions"] = instructions.join("\n\n");
        body["input"] = input;
        if (config.maxOutputTokens > 0) body["max_output_tokens"] = config.maxOutputTokens;
        if (modelLower.startsWith("gpt-5") || modelLower.startsWith("o1")
            || modelLower.startsWith("o3") || modelLower.startsWith("o4")) {
            QJsonObject reasoning;
            reasoning["summary"] = "auto";
            body["reasoning"] = reasoning;
        }
        m_streamActive = config.streaming;
        m_currentReply = m_manager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(m_currentReply, &QNetworkReply::readyRead, this, &OpenAIClient::onReadyRead);
        connect(m_currentReply, &QNetworkReply::finished, this, &OpenAIClient::onFinished);
        return;
    }
    m_apiFlavor = ApiFlavor::OpenAICompatible;

    QString baseUrl = config.baseUrl.trimmed();
    while (baseUrl.endsWith('/')) baseUrl.chop(1);
    QUrl url(baseUrl + "/chat/completions");
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
    if (requestStreaming) {
        QJsonObject streamOptions;
        streamOptions["include_usage"] = true;
        body["stream_options"] = streamOptions;
    }
    if (config.maxOutputTokens > 0)
        body["max_tokens"] = config.maxOutputTokens;
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

    if (m_streamActive || !m_fullResponse.isEmpty()) {
        m_sseBuffer.append(reply->readAll());
        processSSE();
        if (!m_finishedEmitted && !m_fullResponse.isEmpty() && m_streamActive)
            finishStream();
        if (m_finishedEmitted || !m_fullResponse.isEmpty()) {
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
    m_sseBuffer.replace("\r\n", "\n");
    while (true) {
        int idx = m_sseBuffer.indexOf("\n\n");
        if (idx < 0) break;

        QByteArray event = m_sseBuffer.left(idx);
        m_sseBuffer.remove(0, idx + 2);

        const QList<QByteArray> lines = event.split('\n');
        for (const QByteArray &line : lines) {
            if (!line.startsWith("data:")) continue;
            QByteArray data = line.mid(5).trimmed();
            if (data == "[DONE]") {
                finishStream();
                return;
            }
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) continue;

            QJsonObject obj = doc.object();
            QString content;
            if (m_apiFlavor == ApiFlavor::Anthropic) {
                processAnthropicUsage(obj);
                if (obj.value("type").toString() == "content_block_delta") {
                    const QJsonObject delta = obj.value("delta").toObject();
                    const QString deltaType = delta.value("type").toString();
                    if (deltaType == "thinking_delta")
                        emit reasoningReceived(delta.value("thinking").toString());
                    else
                        content = delta.value("text").toString();
                }
                if (obj.value("type").toString() == "message_stop") {
                    finishStream();
                    return;
                }
            } else if (m_apiFlavor == ApiFlavor::Gemini) {
                processGeminiUsage(obj);
                const QJsonArray candidates = obj.value("candidates").toArray();
                if (!candidates.isEmpty()) {
                    const QJsonArray parts = candidates.first().toObject()
                        .value("content").toObject().value("parts").toArray();
                    for (const QJsonValue &part : parts) {
                        const QJsonObject partObject = part.toObject();
                        const QString text = partObject.value("text").toString();
                        if (partObject.value("thought").toBool())
                            emit reasoningReceived(text);
                        else
                            content += text;
                    }
                }
            } else if (m_apiFlavor == ApiFlavor::OpenAIResponses) {
                const QString type = obj.value("type").toString();
                if (type == "response.output_text.delta")
                    content = obj.value("delta").toString();
                else if (type == "response.reasoning_summary_text.delta"
                         || type == "response.reasoning_text.delta")
                    emit reasoningReceived(obj.value("delta").toString());
                if (type == "response.completed") {
                    processResponsesUsage(obj.value("response").toObject());
                    finishStream();
                    return;
                }
            } else {
                processUsage(obj);
                QJsonArray choices = obj["choices"].toArray();
                if (choices.isEmpty()) continue;
                QJsonObject choice = choices[0].toObject();
                QJsonObject delta = choice["delta"].toObject();
                content = delta["content"].toString();
                QString reasoning = delta.value("reasoning_content").toString();
                if (reasoning.isEmpty())
                    reasoning = delta.value("reasoning").toString();
                if (!reasoning.isEmpty())
                    emit reasoningReceived(reasoning);
            }
            if (!content.isEmpty()) {
                m_fullResponse += content;
                emit tokenReceived(content);
            }

        }
    }
}

void OpenAIClient::finishStream() {
    if (m_finishedEmitted) return;
    m_streamActive = false;
    m_finishedEmitted = true;
    emit finished(m_fullResponse);
}

void OpenAIClient::processUsage(const QJsonObject &object) {
    const QJsonObject usageObject = object["usage"].toObject();
    if (usageObject.isEmpty()) return;

    auto number = [](const QJsonObject &obj, const QString &key) -> qint64 {
        const QJsonValue value = obj.value(key);
        if (value.isDouble()) return qMax<qint64>(0, static_cast<qint64>(value.toDouble()));
        if (value.isString()) {
            bool ok = false;
            const qint64 parsed = value.toString().toLongLong(&ok);
            return ok ? qMax<qint64>(0, parsed) : 0;
        }
        return 0;
    };

    const qint64 rawInput = number(usageObject, "prompt_tokens") > 0
        ? number(usageObject, "prompt_tokens")
        : number(usageObject, "input_tokens");
    const qint64 rawOutput = number(usageObject, "completion_tokens") > 0
        ? number(usageObject, "completion_tokens")
        : number(usageObject, "output_tokens");
    const qint64 rawTotal = number(usageObject, "total_tokens");

    const QJsonObject promptDetails = usageObject["prompt_tokens_details"].toObject();
    const QJsonObject inputDetails = usageObject["input_tokens_details"].toObject();
    const QJsonObject completionDetails = usageObject["completion_tokens_details"].toObject();
    const QJsonObject outputDetails = usageObject["output_tokens_details"].toObject();

    qint64 cacheRead = number(promptDetails, "cached_tokens");
    if (cacheRead == 0) cacheRead = number(inputDetails, "cached_tokens");
    if (cacheRead == 0) cacheRead = number(usageObject, "cache_read_input_tokens");

    qint64 cacheWrite = number(promptDetails, "cache_creation_tokens");
    if (cacheWrite == 0) cacheWrite = number(inputDetails, "cache_creation_tokens");
    if (cacheWrite == 0) cacheWrite = number(usageObject, "cache_creation_input_tokens");

    qint64 reasoning = number(completionDetails, "reasoning_tokens");
    if (reasoning == 0) reasoning = number(outputDetails, "reasoning_tokens");
    if (reasoning == 0) reasoning = number(usageObject, "reasoning_tokens");

    TokenUsage usage;
    usage.cacheRead = qMin(cacheRead, rawInput);
    usage.cacheWrite = qMin(cacheWrite, qMax<qint64>(0, rawInput - usage.cacheRead));
    usage.reasoning = qMin(reasoning, rawOutput);
    usage.input = qMax<qint64>(0, rawInput - usage.cacheRead - usage.cacheWrite);
    usage.output = qMax<qint64>(0, rawOutput - usage.reasoning);
    usage.providerTotal = rawTotal;
    if (usage.input + usage.output + usage.reasoning + usage.cacheRead + usage.cacheWrite == 0)
        usage.input = rawTotal;
    usage.providerId = m_currentConfig.baseUrl;
    usage.modelId = m_currentConfig.modelId;
    usage.source = "provider_reported";
    usage.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    if (usage.valid())
        emit usageReceived(usage);
}

void OpenAIClient::processAnthropicUsage(const QJsonObject &object) {
    QJsonObject usageObject = object.value("usage").toObject();
    if (usageObject.isEmpty()) usageObject = object.value("message").toObject().value("usage").toObject();
    if (usageObject.isEmpty()) return;
    auto number = [](const QJsonObject &obj, const QString &key) -> qint64 {
        const QJsonValue value = obj.value(key);
        return value.isDouble() ? qMax<qint64>(0, qint64(value.toDouble())) : 0;
    };
    TokenUsage usage;
    const qint64 rawInput = number(usageObject, "input_tokens");
    const qint64 rawOutput = number(usageObject, "output_tokens");
    usage.cacheRead = qMin(number(usageObject, "cache_read_input_tokens"), rawInput);
    usage.cacheWrite = qMin(number(usageObject, "cache_creation_input_tokens"), rawInput - usage.cacheRead);
    usage.input = qMax<qint64>(0, rawInput - usage.cacheRead - usage.cacheWrite);
    usage.output = rawOutput;
    usage.providerTotal = rawInput + rawOutput;
    usage.providerId = m_currentConfig.providerId;
    usage.modelId = m_currentConfig.modelId;
    usage.source = "provider_reported";
    usage.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (usage.valid()) emit usageReceived(usage);
}

void OpenAIClient::processGeminiUsage(const QJsonObject &object) {
    const QJsonObject metadata = object.value("usageMetadata").toObject();
    if (metadata.isEmpty()) return;
    auto number = [&metadata](const QString &key) -> qint64 {
        const QJsonValue value = metadata.value(key);
        return value.isDouble() ? qMax<qint64>(0, qint64(value.toDouble())) : 0;
    };
    const qint64 rawInput = number("promptTokenCount");
    const qint64 rawOutput = number("candidatesTokenCount");
    TokenUsage usage;
    usage.cacheRead = qMin(number("cachedContentTokenCount"), rawInput);
    usage.input = qMax<qint64>(0, rawInput - usage.cacheRead);
    usage.output = rawOutput;
    usage.reasoning = number("thoughtsTokenCount");
    usage.providerTotal = number("totalTokenCount");
    usage.providerId = m_currentConfig.providerId;
    usage.modelId = m_currentConfig.modelId;
    usage.source = "provider_reported";
    usage.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (usage.valid()) emit usageReceived(usage);
}

void OpenAIClient::processResponsesUsage(const QJsonObject &object) {
    const QJsonObject usageObject = object.value("usage").toObject();
    if (usageObject.isEmpty()) return;
    auto number = [](const QJsonObject &obj, const QString &key) -> qint64 {
        const QJsonValue value = obj.value(key);
        return value.isDouble() ? qMax<qint64>(0, qint64(value.toDouble())) : 0;
    };
    const qint64 rawInput = number(usageObject, "input_tokens");
    const qint64 rawOutput = number(usageObject, "output_tokens");
    const QJsonObject inputDetails = usageObject.value("input_tokens_details").toObject();
    const QJsonObject outputDetails = usageObject.value("output_tokens_details").toObject();
    TokenUsage usage;
    usage.cacheRead = qMin(number(inputDetails, "cached_tokens"), rawInput);
    usage.input = qMax<qint64>(0, rawInput - usage.cacheRead);
    usage.reasoning = qMin(number(outputDetails, "reasoning_tokens"), rawOutput);
    usage.output = qMax<qint64>(0, rawOutput - usage.reasoning);
    usage.providerTotal = number(usageObject, "total_tokens");
    usage.providerId = m_currentConfig.providerId;
    usage.modelId = m_currentConfig.modelId;
    usage.source = "provider_reported";
    usage.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (usage.valid()) emit usageReceived(usage);
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
    if (m_apiFlavor == ApiFlavor::OpenAIResponses) {
        processResponsesUsage(obj);
        QString content = obj.value("output_text").toString();
        if (content.isEmpty()) {
            for (const QJsonValue &outputItem : obj.value("output").toArray()) {
                const QJsonObject item = outputItem.toObject();
                for (const QJsonValue &summary : item.value("summary").toArray()) {
                    const QString text = summary.toObject().value("text").toString();
                    if (!text.isEmpty()) emit reasoningReceived(text);
                }
                for (const QJsonValue &part : item.value("content").toArray()) {
                    const QJsonObject partObject = part.toObject();
                    if (partObject.value("type").toString() == "output_text")
                        content += partObject.value("text").toString();
                }
            }
        }
        if (content.isEmpty()) {
            emit errorOccurred("El modelo no devolvió texto.");
            return;
        }
        m_fullResponse = content;
        emit tokenReceived(content);
        emit finished(content);
        return;
    }
    if (m_apiFlavor == ApiFlavor::Anthropic) {
        processAnthropicUsage(obj);
        QString content;
        for (const QJsonValue &part : obj.value("content").toArray()) {
            const QJsonObject partObject = part.toObject();
            if (partObject.value("type").toString() == "thinking")
                emit reasoningReceived(partObject.value("thinking").toString());
            else
                content += partObject.value("text").toString();
        }
        if (content.isEmpty()) {
            emit errorOccurred("El modelo no devolvió texto.");
            return;
        }
        m_fullResponse = content;
        emit tokenReceived(content);
        emit finished(content);
        return;
    }
    if (m_apiFlavor == ApiFlavor::Gemini) {
        processGeminiUsage(obj);
        QString content;
        const QJsonArray candidates = obj.value("candidates").toArray();
        if (!candidates.isEmpty()) {
            for (const QJsonValue &part : candidates.first().toObject()
                 .value("content").toObject().value("parts").toArray()) {
                const QJsonObject partObject = part.toObject();
                if (partObject.value("thought").toBool())
                    emit reasoningReceived(partObject.value("text").toString());
                else
                    content += partObject.value("text").toString();
            }
        }
        if (content.isEmpty()) {
            emit errorOccurred("El modelo no devolvió texto.");
            return;
        }
        m_fullResponse = content;
        emit tokenReceived(content);
        emit finished(content);
        return;
    }
    processUsage(obj);
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
    const QJsonObject message = choices[0].toObject()["message"].toObject();
    QString reasoning = message.value("reasoning_content").toString();
    if (reasoning.isEmpty()) reasoning = message.value("reasoning").toString();
    if (!reasoning.isEmpty()) emit reasoningReceived(reasoning);
    QString content = message["content"].toString();
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
