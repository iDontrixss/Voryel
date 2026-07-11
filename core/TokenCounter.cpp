#include "TokenCounter.h"
#include "../model/ModelTypes.h"
#include <QDebug>
#include <QtMath>

// ── Local catalog of known model context windows ──
static const QMap<QString, int> buildCatalog() {
    QMap<QString, int> c;

    // Groq models
    c["llama-3.3-70b-versatile"]          = 131072;
    c["llama-3.2-1b-preview"]             = 131072;
    c["llama-3.2-3b-preview"]             = 131072;
    c["llama-3.2-11b-vision-preview"]     = 131072;
    c["llama-3.2-90b-vision-preview"]     = 131072;
    c["llama-3.1-8b-instant"]             = 131072;
    c["llama-3.1-70b-versatile"]          = 131072;
    c["llama-3.1-405b-reasoning"]         = 131072;
    c["llama-guard-3-8b"]                 = 8192;
    c["llama3-70b-8192"]                  = 8192;
    c["llama3-8b-8192"]                   = 8192;
    c["mixtral-8x7b-32768"]               = 32768;
    c["gemma2-9b-it"]                     = 8192;
    c["gemma-7b-it"]                      = 8192;
    c["qwen-2.5-32b"]                     = 131072;
    c["qwen-2.5-coder-32b"]               = 131072;
    c["qwen-qwq-32b"]                     = 131072;
    c["deepseek-r1-distill-qwen-32b"]     = 131072;
    c["deepseek-r1-distill-llama-70b"]    = 131072;
    c["whisper-large-v3"]                 = 0; // audio only

    // OpenRouter common
    c["openai/gpt-4o"]                    = 128000;
    c["openai/gpt-4o-mini"]               = 128000;
    c["openai/gpt-4-turbo"]               = 128000;
    c["openai/gpt-3.5-turbo"]             = 16385;
    c["anthropic/claude-3.5-sonnet"]      = 200000;
    c["anthropic/claude-3-haiku"]         = 200000;
    c["anthropic/claude-3-opus"]          = 200000;
    c["google/gemini-pro-1.5"]            = 1048576;
    c["google/gemini-flash-1.5"]          = 1048576;
    c["meta-llama/llama-3.1-8b-instruct"] = 131072;
    c["meta-llama/llama-3.1-70b-instruct"]= 131072;
    c["mistralai/mistral-large"]          = 131072;
    c["qwen/qwen-2.5-32b-instruct"]       = 131072;
    c["qwen/qwen-2.5-coder-32b-instruct"] = 131072;
    c["deepseek/deepseek-chat"]           = 128000;
    c["nousresearch/hermes-3-llama-3.1"]  = 131072;

    // LM Studio / local: default to 8192 (most local models)
    // These will be matched by prefix fallback
    return c;
}

static const QMap<QString, int>& catalog() {
    static const QMap<QString, int> c = buildCatalog();
    return c;
}

// ── Known model family prefixes for estimation ──
static int familyDefault(const QString &modelId) {
    QString m = modelId.toLower();
    if (m.contains("gpt-4o"))     return 128000;
    if (m.contains("gpt-4"))      return 8192;
    if (m.contains("gpt-3.5"))    return 16385;
    if (m.contains("claude"))     return 200000;
    if (m.contains("gemini"))     return 1048576;
    if (m.contains("qwen") && (m.contains("2.5") || m.contains("qwq"))) return 131072;
    if (m.contains("qwen"))       return 32768;
    if (m.contains("deepseek"))   return 128000;
    if (m.contains("llama-3.3") || m.contains("llama-3.1")) return 131072;
    if (m.contains("llama-3.2"))  return 131072;
    if (m.contains("llama-3"))    return 8192;
    if (m.contains("llama"))      return 4096;
    if (m.contains("mistral") || m.contains("mixtral")) return 32768;
    if (m.contains("mixtral"))    return 32768;
    if (m.contains("gemma"))      return 8192;
    if (m.contains("phi"))        return 4096;
    if (m.contains("codellama"))  return 16384;
    if (m.contains("starcoder"))  return 8192;
    if (m.contains("yi-"))        return 32768;
    if (m.contains("command-r"))  return 131072;
    return 0;
}

// ── Public API ──

TokenCountResult TokenCounter::estimateTokens(const QString &text) {
    TokenCountResult result;

    if (text.isEmpty()) {
        result.tokens = 0;
        result.method = "exact_local";
        result.accuracy = "exacta";
        return result;
    }

    // Use character-based rough estimate (more accurate than /4 for mixed content)
    int chars = text.length();
    double est = 0;

    // Better heuristic: count words, numbers, special chars
    int spaces = text.count(' ');
    int newlines = text.count('\n');
    int codeChars = 0;
    for (const QChar &c : text) {
        if (c.isPunct() || c.isDigit())
            codeChars++;
    }

    // Estimate: ~1 token per 4 chars for prose, ~1 per 3 for code-heavy
    double ratio = (codeChars > chars * 0.3) ? 3.0 : 4.0;
    est = qMax(1.0, (double)chars / ratio);

    // Account for newlines and special tokens
    est += newlines * 0.5;

    result.tokens = (int)qCeil(est);
    result.method = "model_family_estimate";
    result.accuracy = "estimada";
    return result;
}

int TokenCounter::roughEstimate(const QString &text) {
    return estimateTokens(text).tokens;
}

ContextWindowInfo TokenCounter::resolveContextWindow(const QString &modelId,
                                                      const QString &providerId,
                                                      int userOverride)
{
    ContextWindowInfo info;

    // Priority 1: user manual override
    if (userOverride > 0) {
        info.tokens = userOverride;
        info.source = "user_manual";
        return info;
    }

    // Priority 2: exact match in catalog
    if (catalog().contains(modelId)) {
        int val = catalog().value(modelId);
        if (val > 0) {
            info.tokens = val;
            info.source = "local_catalog";
            return info;
        }
    }

    // Priority 3: try prefix match in catalog
    for (auto it = catalog().cbegin(); it != catalog().cend(); ++it) {
        if (modelId.startsWith(it.key(), Qt::CaseInsensitive) ||
            modelId.endsWith(it.key(), Qt::CaseInsensitive))
        {
            if (it.value() > 0) {
                info.tokens = it.value();
                info.source = "local_catalog";
                return info;
            }
        }
    }

    // Priority 4: family-based default
    int family = familyDefault(modelId);
    if (family > 0) {
        info.tokens = family;
        info.source = "default_estimate";
        return info;
    }

    // Priority 5: safe default based on provider
    if (providerId == "lm_studio" || providerId == "ollama")
        info.tokens = 8192;
    else if (providerId == "groq")
        info.tokens = 32768;
    else if (providerId == "openrouter")
        info.tokens = 32768;
    else if (providerId == "gemini")
        info.tokens = 1048576;
    else
        info.tokens = 32768;
    info.source = "default_estimate";

    return info;
}

int TokenCounter::countMessagesTokens(const QVector<Message> &messages) {
    int total = 0;
    for (const auto &msg : messages) {
        total += roughEstimate(msg.content);
        // Role tokens (~4 each)
        total += 4;
    }
    // Approx overhead
    total += qMax(0, (int)messages.size() * 8);
    return total;
}
