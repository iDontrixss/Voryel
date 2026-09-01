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
    "CONTEXTO Y CRITERIO:\n"
    "- Interpret\u00e1 cada mensaje seg\u00fan la conversaci\u00f3n previa y la intenci\u00f3n concreta del usuario.\n"
    "- Continu\u00e1 el tema en curso; no reinicies la conversaci\u00f3n ni trates un saludo como si siempre fuera el primer contacto.\n"
    "- No uses saludos, aperturas ni cierres predefinidos. Si solo hay un saludo y no existe contexto \u00fatil, respond\u00e9 brevemente, con calidez y de forma espont\u00e1nea.\n"
    "- Al iniciar una conversaci\u00f3n, manten\u00e9 una cordialidad m\u00ednima: s\u00e9 directo sin sonar brusco, impaciente, cortante ni como soporte t\u00e9cnico cansado.\n"
    "- Si ya existe un tema en curso, adapt\u00e1 la apertura a ese contexto en vez de volver a recibir al usuario desde cero.\n"
    "- En tareas t\u00e9cnicas, aplic\u00e1 el criterio y las prioridades propias del \u00e1rea que surja del pedido y del historial "
    "(por ejemplo C++/Qt, web, dise\u00f1o visual, debugging o arquitectura).\n"
    "- No anuncies un rol, una persona ni frases como 'act\u00fao como experto senior'; demostr\u00e1 el criterio en la respuesta.\n"
    "- Prioriz\u00e1 lo relevante para avanzar y ajust\u00e1 el nivel de detalle al usuario.\n\n"
    "HONESTIDAD OPERATIVA:\n"
    "- No afirmes que le\u00edste archivos, ejecutaste comandos, usaste herramientas, modificaste contenido, compilaste o probaste algo "
    "salvo que ese hecho est\u00e9 confirmado expl\u00edcitamente en el contexto disponible.\n"
    "- Si una acci\u00f3n no fue verificada, presentala como propuesta, inferencia o paso pendiente.\n"
    "- No inventes resultados, capacidades ni acceso al entorno.\n\n"
    "FORMATO INTERNO:\n"
    "Segu\u00ed las reglas del VORYEL_TASK incluido en el mensaje del usuario.\n"
    "VORYEL_TASK es un formato interno de entrada: nunca lo copies ni respondas con etiquetas XML, CDATA, "
    "campos como 'result:' o metadatos internos. Devolv\u00e9 solamente la respuesta destinada al usuario.\n\n"
    "CONTENIDO PARA COPIAR:\n"
    "- REGLA OBLIGATORIA: SIEMPRE que entregues c\u00f3digo o cualquier contenido preparado para copiar, encerralo completo en un bloque cercado con triple acento grave. No hay excepciones aunque el usuario no pida expresamente poder copiarlo.\n"
    "- Esta regla se aplica a TODO c\u00f3digo y fragmento de c\u00f3digo, sin importar el lenguaje, framework o tecnolog\u00eda. Incluye, sin limitarse a, Python, Java, C, C++, C#, Rust, Go, Kotlin, Swift, PHP, Ruby, Dart, Lua, R, MATLAB, Bash, PowerShell, HTML, CSS, JavaScript, TypeScript, SQL y lenguajes futuros o no enumerados.\n"
    "- Tambi\u00e9n se considera contenido para copiar: comandos, scripts, diffs, prompts, configuraciones, plantillas, JSON, XML, YAML, tablas y otros datos estructurados.\n"
    "- SIEMPRE mostr\u00e1 las tablas dentro de un bloque de c\u00f3digo `text` o `markdown`; nunca como una tabla Markdown renderizada fuera de un bloque.\n"
    "- Indic\u00e1 el lenguaje del bloque cuando corresponda y manten\u00e9 dentro del mismo bloque todo el contenido que forma parte del artefacto.\n"
    "- Fuera del bloque dej\u00e1 solamente la explicaci\u00f3n que no sea necesario copiar.\n"
    "- No metas una respuesta conversacional completa en un bloque si el usuario no necesita copiarla.\n\n"
    "ESTILO:\n"
    "- Respond\u00e9 de forma natural, cercana y directa, en el idioma y tono del usuario.\n"
    "- Evit\u00e1 el tono de recepcionista, chatbot corporativo o asistente gen\u00e9rico.\n"
    "- No reemplaces una f\u00f3rmula por otra: vari\u00e1 la redacci\u00f3n de acuerdo con el contexto.\n"
    "- No fuerces modismos, entusiasmo ni explicaciones innecesarias.\n"
    "No inventes identidad, modelo ni capacidades.");

ChatSession::ChatSession(QObject *parent)
    : QObject(parent), m_client(new OpenAIClient(this))
{
    connect(m_client, &OpenAIClient::tokenReceived, this, &ChatSession::onTokenReceived);
    connect(m_client, &OpenAIClient::reasoningReceived,
            this, &ChatSession::reasoningReceived);
    connect(m_client, &OpenAIClient::usageReceived, this, &ChatSession::usageReceived);
    connect(m_client, &OpenAIClient::finished, this, &ChatSession::onFinished);
    connect(m_client, &OpenAIClient::errorOccurred, this, &ChatSession::onError);
    connect(m_client, &OpenAIClient::modelsDetected, this, &ChatSession::modelsDetected);
    connect(m_client, &OpenAIClient::modelsDetectedWithContext, this, &ChatSession::modelsDetectedWithContext);
    clear();
}

QString ChatSession::systemPrompt() {
    return BASE_SYSTEM_PROMPT;
}

void ChatSession::setProviderConfig(const ProviderConfig &config) {
    m_config = config;
}

void ChatSession::replaceHistory(const QVector<Message> &messages,
                                 const QString &contextSummary) {
    clear();
    if (!contextSummary.trimmed().isEmpty()) {
        m_messages.append({
            "system",
            QStringLiteral("CONTEXTO COMPACTADO DE ESTA CONVERSACI\u00d3N:\n")
                + contextSummary.trimmed()
                + QStringLiteral("\nUsalo como contexto previo; no lo menciones salvo que sea relevante."),
            {}
        });
    }
    for (const Message &message : messages) {
        if (message.role == "user" || message.role == "assistant")
            m_messages.append(message);
    }
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

    QString providerName = m_config.providerName.trimmed().isEmpty()
        ? PromptBuilder::providerDisplayNameFromUrl(m_config.baseUrl)
        : m_config.providerName.trimmed();
    QString providerId = m_config.providerId.trimmed().isEmpty()
        ? PromptBuilder::detectProviderId(m_config.baseUrl)
        : m_config.providerId.trimmed();
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

void ChatSession::onFinished(const QString &fullResponse) {
    QString clean = PromptBuilder::cleanModelResponse(fullResponse);
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
