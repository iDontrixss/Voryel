#include "PromptBuilder.h"
#include <QDebug>
#include <QStringList>
#include <QRegularExpression>

QString PromptBuilder::providerDisplayNameFromUrl(const QString &baseUrl) {
    QString url = baseUrl.toLower();
    if (url.contains("127.0.0.1:1234") || url.contains("localhost:1234"))
        return "LM Studio";
    if (url.contains("11434"))
        return "Ollama";
    if (url.contains("api.groq.com"))
        return "Groq";
    if (url.contains("openrouter"))
        return "OpenRouter";
    if (url.contains("generativelanguage.googleapis.com"))
        return "Google";
    if (url.contains("api.anthropic.com"))
        return "Anthropic";
    return "Custom";
}

QString PromptBuilder::detectProviderId(const QString &baseUrl) {
    QString url = baseUrl.toLower();
    if (url.contains("api.groq.com"))
        return "groq";
    if (url.contains("openrouter"))
        return "openrouter";
    if (url.contains("generativelanguage.googleapis.com"))
        return "gemini";
    if (url.contains("api.anthropic.com"))
        return "anthropic";
    if (url.contains("11434"))
        return "ollama";
    if (url.contains("127.0.0.1:1234") || url.contains("localhost:1234"))
        return "lm_studio";
    return "custom";
}

// ── Extended-mode helpers ──

static QStringList behaviorForMode(const QString &mode) {
    if (mode == "import_context") {
        return {
            "Salida solo con categor\u00edas pedidas.",
            "Sin saludo.",
            "Sin despedida.",
            "Sin relleno.",
            "No usar primera persona.",
            "No usar segunda persona.",
            "Referirse como \"el usuario\".",
            "Conservar frases literales.",
            "Incluir evidencias cuando existan.",
            "Terminar con: Importado de: <source>"
        };
    }
    if (mode == "dev") {
        return {
            "Respond\u00e9 en el idioma del usuario.",
            "Tratar al usuario como desarrollador del proyecto.",
            "Tono directo.",
            "Si pide \"dame prompt\", devolver prompt listo para pegar.",
            "Si reporta bug, explicar: s\u00edntoma, causa probable, fix sugerido, pruebas.",
            "No inventar que compil\u00f3.",
            "No decir \"est\u00e1 sint\u00e1cticamente correcto\" si no hubo build.",
            "No tocar seguridad sin aclarar impacto.",
            "No dar vueltas."
        };
    }
    if (mode == "security_review") {
        return {
            "Respond\u00e9 en el idioma del usuario.",
            "Formato: Hallazgos Alta/Media/Baja, luego Prioridad recomendada.",
            "No vender humo.",
            "No decir \"seguro\" si solo es mitigado.",
            "Separar privacidad, seguridad y performance si aplica."
        };
    }
    if (mode == "ui_review") {
        return {
            "Respond\u00e9 en el idioma del usuario.",
            "Describir problemas visuales: clipping, espaciado, color, jerarqu\u00eda, botones muertos, textos confusos, componentes que parecen Windows default.",
            "Proponer prompts o fixes concretos.",
            "No tocar l\u00f3gica de backend salvo que sea necesario."
        };
    }
    if (mode == "file_analysis") {
        return {
            "Respond\u00e9 en el idioma del usuario.",
            "Explicar qu\u00e9 se pudo leer y qu\u00e9 no.",
            "No inventar contenido de archivos no procesados.",
            "Si hay im\u00e1genes, aclarar si el modelo soporta visi\u00f3n."
        };
    }
    if (mode == "fallback_diagnostic") {
        return {
            "Explicar fallos de proveedor/modelo.",
            "Rate limits, auth, model not found, endpoint, etc.",
            "Tono claro, no alarmista."
        };
    }
    if (mode == "compact_session") {
        return {
            "Vas a compactar el historial de una conversaci\u00f3n de Voryel.",
            "El objetivo es reducir tokens sin perder informaci\u00f3n importante para continuar el trabajo.",
            "No respondas al usuario final.",
            "No saludes.",
            "No agregues relleno.",
            "No inventes datos.",
            "No incluyas claves API, secretos, tokens, contrase\u00f1as ni contenido sensible.",
            "No incluyas bloques <think>.",
            "Conserv\u00e1 nombres de archivos, clases, funciones, bugs, decisiones t\u00e9cnicas y tareas pendientes.",
            "Conserv\u00e1 preferencias expl\u00edcitas del usuario cuando sean relevantes.",
            "La salida debe ser un resumen estructurado, denso y \u00fatil.",
            "",
            "output_format:",
            "  1. Estado actual del proyecto/conversaci\u00f3n",
            "  2. Decisiones importantes",
            "  3. Arquitectura y archivos relevantes",
            "  4. Bugs/fixes ya tratados",
            "  5. Preferencias e instrucciones del usuario",
            "  6. Tareas pendientes / pr\u00f3ximos pasos",
            "  7. Detalles que no deben olvidarse"
        };
    }
    return {};
}

// ── Sanitize ──

QString PromptBuilder::sanitizeMessage(const QString &message) {
    QString s = message;
    s.replace("]]>", "]]]]><![CDATA[>");
    return s;
}

QString PromptBuilder::cleanModelResponse(const QString &response) {
    QString withoutThinking;
    int position = 0;
    while (true) {
        const int start = response.indexOf("<think>", position, Qt::CaseInsensitive);
        if (start < 0) {
            withoutThinking += response.mid(position);
            break;
        }
        withoutThinking += response.mid(position, start - position);
        const int end = response.indexOf("</think>", start + 7, Qt::CaseInsensitive);
        if (end < 0) break;
        position = end + 8;
    }

    QString result = withoutThinking.trimmed();
    const QRegularExpression envelope(
        QStringLiteral("<VORYEL_TASK\\b[^>]*>([\\s\\S]*?)</VORYEL_TASK>"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch envelopeMatch = envelope.match(result);
    if (envelopeMatch.hasMatch())
        result = envelopeMatch.captured(1).trimmed();

    result.remove(QRegularExpression(
        QStringLiteral("^\\s*result\\s*:\\s*"),
        QRegularExpression::CaseInsensitiveOption));

    const QRegularExpression cdata(
        QStringLiteral("^\\s*<!\\[CDATA\\[([\\s\\S]*?)\\]\\]>\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch cdataMatch = cdata.match(result);
    if (cdataMatch.hasMatch())
        result = cdataMatch.captured(1);

    return result.trimmed();
}

// ── Mode detection ──

QString PromptBuilder::detectMode(const QString &userMessage, QString &outCleanMessage) {
    QString trimmed = userMessage.trimmed();
    if (trimmed.startsWith('/')) {
        int spaceIdx = trimmed.indexOf(' ');
        QString cmd = (spaceIdx < 0) ? trimmed.mid(1).toLower()
                                      : trimmed.mid(1, spaceIdx - 1).toLower();
        outCleanMessage = (spaceIdx < 0) ? QString()
                                          : trimmed.mid(spaceIdx + 1).trimmed();

        if (cmd == "dev")               return "dev";
        if (cmd == "security")          return "security_review";
        if (cmd == "ui")                return "ui_review";
        if (cmd == "import")            return "import_context";
        if (cmd == "file")              return "file_analysis";
        if (cmd == "chat")              return "chat";
    }
    outCleanMessage = userMessage;
    return "chat";
}

// ── Envelope builder ──

static bool isExtendedMode(const QString &mode) {
    return mode == "dev" || mode == "security_review" || mode == "ui_review"
        || mode == "import_context" || mode == "file_analysis"
        || mode == "fallback_diagnostic" || mode == "compact_session";
}

QString PromptBuilder::buildEnvelope(const TaskEnvelopeParams &params) {
    QString header = "<VORYEL_TASK>\n";
    header += "mode: " + params.mode + "\n";
    header += "active_provider: " + params.activeProvider + "\n";
    header += "active_model: " + params.activeModel + "\n";

    // ── Extended metadata ──
    if (isExtendedMode(params.mode)) {
        header += "provider_id: " + params.providerId + "\n";
        header += "model_id: " + params.modelId + "\n";
        header += "is_local: " + QString(params.isLocal ? "true" : "false") + "\n";
        header += "supports_vision: " + params.supportsVision + "\n";
        header += "fallbacks_enabled: " + QString(params.fallbacksEnabled ? "true" : "false") + "\n";
        if (!params.importSource.isEmpty())
            header += "source_assistant: " + params.importSource + "\n";
    }

    // ── Extended behavior block ──
    QString body;
    if (isExtendedMode(params.mode)) {
        const QStringList rules = behaviorForMode(params.mode);
        if (!rules.isEmpty()) {
            body += "\nbehavior:\n";
            for (const QString &rule : rules)
                body += "  - " + rule + "\n";
        }
    }

    // ── User request (always present) ──
    body += "\nuser_request:\n";
    body += "<![CDATA[\n";
    if (params.mode == "import_context" && !params.importSource.isEmpty())
        body += "Importado de: " + params.importSource + "\n\n";
    body += sanitizeMessage(params.userMessage) + "\n";
    body += "]]>\n";
    body += "</VORYEL_TASK>";

    QString full = header + body;

    qDebug() << "PromptBuilder: mode=" << params.mode
             << "estimated envelope chars=" << full.length()
             << "provider=" << params.activeProvider
             << "model=" << params.activeModel;

    return full;
}
