#include "ChatSession.h"
#include "PromptBuilder.h"
#include <QDebug>

static const QString BASE_SYSTEM_PROMPT = QStringLiteral(
    "Respond\u00e9s dentro de Voryel.\n\n"
    "IDENTIDAD:\n"
    "- Voryel es la app/interfaz/orquestador.\n"
    "- Loryq es el ecosistema.\n"
    "- iDontrixss es el creador/desarrollador.\n"
    "- El modelo actual responde dentro de Voryel.\n"
    "- El usuario no es Voryel.\n"
    "- No inventes identidad propia.\n\n"
    "Segu\u00ed las reglas del VORYEL_TASK que se incluye en el mensaje del usuario.\n"
    "No inventes identidad, modelo ni capacidades.");

ChatSession::ChatSession(QObject *parent)
    : QObject(parent), m_client(new OpenAIClient(this))
{
    connect(m_client, &OpenAIClient::tokenReceived, this, &ChatSession::onTokenReceived);
    connect(m_client, &OpenAIClient::finished, this, &ChatSession::onFinished);
    connect(m_client, &OpenAIClient::errorOccurred, this, &ChatSession::onError);
    connect(m_client, &OpenAIClient::modelsDetected, this, &ChatSession::modelsDetected);
    connect(m_client, &OpenAIClient::modelsDetectedWithContext, this, &ChatSession::modelsDetectedWithContext);
    clear();
}

void ChatSession::setProviderConfig(const ProviderConfig &config) {
    m_config = config;
}

void ChatSession::sendMessage(const QString &text, const QStringList &attachments) {
    qDebug() << "ChatSession::sendMessage: text length =" << text.length()
             << "attachments =" << attachments.size()
             << "isConfigured =" << isConfigured();
    if (!isConfigured()) {
        qDebug() << "ChatSession::sendMessage: NOT configured, emitting error";
        emit errorOccurred("Seleccion\u00e1 o escrib\u00ed un modelo local antes de enviar.");
        return;
    }
    if (text.trimmed().isEmpty() && attachments.isEmpty()) {
        qDebug() << "ChatSession::sendMessage: empty text and no attachments, returning";
        return;
    }

    // --- Build Voryel Task Envelope ---
    QString cleanText;
    QString mode = PromptBuilder::detectMode(text, cleanText);

    QString providerName = PromptBuilder::providerDisplayNameFromUrl(m_config.baseUrl);
    QString providerId   = PromptBuilder::detectProviderId(m_config.baseUrl);
    bool isLocal = m_config.baseUrl.contains("localhost", Qt::CaseInsensitive)
                || m_config.baseUrl.contains("127.0.0.1")
                || m_config.baseUrl.contains("0.0.0.0");
    bool supportsVision = m_config.modelId.contains("vision", Qt::CaseInsensitive)
                       || m_config.modelId.contains("-vl", Qt::CaseInsensitive)
                       || m_config.modelId.contains("multimodal", Qt::CaseInsensitive);

    QString importSource;
    if (mode == "import_context" && !cleanText.isEmpty()) {
        importSource = cleanText.section(' ', 0, 0);
    }

    TaskEnvelopeParams envParams;
    envParams.mode = mode;
    envParams.userLanguage = "es";
    envParams.activeProvider = providerName;
    envParams.activeModel = m_config.modelId;
    envParams.providerId = providerId;
    envParams.modelId = m_config.modelId;
    envParams.isLocal = isLocal;
    envParams.supportsVision = supportsVision ? "yes" : "unknown";
    envParams.fallbacksEnabled = m_fallbacksEnabled;
    envParams.importSource = importSource;
    envParams.userMessage = cleanText;

    QString envelope = PromptBuilder::buildEnvelope(envParams);

    // Short system prompt (stable, minimal)
    if (!m_messages.isEmpty() && m_messages.first().role == "system")
        m_messages[0].content = BASE_SYSTEM_PROMPT;

    // Store clean user message in m_messages (raw text, with slash command if any)
    Message msg;
    msg.role = "user";
    msg.content = text;
    msg.attachments = attachments;
    m_messages.append(msg);

    // Create API copy with wrapped last user message
    QVector<Message> apiMessages = m_messages;
    if (!apiMessages.isEmpty()) {
        apiMessages.last().content = envelope;
    }

    qDebug() << "ChatSession::sendMessage: emitting started, messages count =" << m_messages.size();
    emit started();
    m_client->sendChatCompletion(apiMessages, m_config);
}

void ChatSession::cancel() {
    m_client->cancel();
    emit cancelled();
}

void ChatSession::clear() {
    m_messages.clear();
    m_messages.append({"system", BASE_SYSTEM_PROMPT});
}

void ChatSession::onTokenReceived(const QString &token) {
    emit tokenReceived(token);
}

static QString stripThinkTags(const QString &text) {
    QString result;
    int pos = 0;
    while (true) {
        int start = text.indexOf("<think>", pos, Qt::CaseInsensitive);
        if (start < 0) {
            result += text.mid(pos);
            break;
        }
        result += text.mid(pos, start - pos);
        int end = text.indexOf("</think>", start + 7, Qt::CaseInsensitive);
        if (end < 0) break;
        pos = end + 8;
    }
    return result;
}

void ChatSession::onFinished(const QString &fullResponse) {
    QString clean = stripThinkTags(fullResponse);
    m_messages.append({"assistant", clean});
    emit finished(clean);
}

void ChatSession::onError(const QString &message) {
    qDebug() << "ChatSession::onError:" << message << "messages count =" << m_messages.size();
    if (!m_messages.isEmpty() && m_messages.last().role == "user") {
        qDebug() << "ChatSession::onError: removing last user message from history";
        m_messages.removeLast();
    }
    emit errorOccurred(message);
}

void ChatSession::detectModels(const QString &baseUrl) {
    m_client->detectModels(baseUrl);
}
