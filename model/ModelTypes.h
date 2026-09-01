#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

struct TokenUsage {
    qint64 input = 0;
    qint64 output = 0;
    qint64 reasoning = 0;
    qint64 cacheRead = 0;
    qint64 cacheWrite = 0;
    qint64 providerTotal = 0;
    qint64 contextLimit = 0;
    QString providerId;
    QString modelId;
    QString source; // provider_reported | estimated
    QString timestamp;

    qint64 total() const {
        const qint64 normalized = input + output + reasoning + cacheRead + cacheWrite;
        return normalized > 0 ? normalized : providerTotal;
    }
    bool valid() const { return total() > 0; }
    bool reported() const { return source == "provider_reported"; }
};

struct Message {
    QString role;
    QString content;
    QStringList attachments;
};

struct ProviderConfig {
    QString baseUrl  = "http://localhost:1234/v1";
    QString apiKey;
    QString modelId;
    QString providerId;
    QString providerName;
    bool streaming   = true;
    int contextWindowTokens = 0; // 0=unknown, will use catalog/default
    QString contextWindowSource; // provider | models.dev | configured
    int maxOutputTokens = 0; // 0=provider default
    bool valid() const { return !baseUrl.isEmpty() && !modelId.isEmpty(); }
};

inline QString prettyModelName(const QString &raw) {
    if (raw.isEmpty()) return QString();
    QString name = raw.trimmed().toLower();
    // Strip common suffixes
    if (name.endsWith("-gguf")) name.chop(5);
    if (name.endsWith("-q4_k_m")) name.chop(8);
    if (name.endsWith("-q8_0")) name.chop(6);
    if (name.endsWith("-f16")) name.chop(4);

    // Known patterns: qwen2.5-coder-Xb-instruct → Qwen2.5 Coder XB
    static const struct { const char *pattern; const char *display; } knownModels[] = {
        { "qwen2.5-coder-0.5b-instruct",   "Qwen2.5 Coder 0.5B" },
        { "qwen2.5-coder-1.5b-instruct",   "Qwen2.5 Coder 1.5B" },
        { "qwen2.5-coder-3b-instruct",     "Qwen2.5 Coder 3B" },
        { "qwen2.5-coder-7b-instruct",     "Qwen2.5 Coder 7B" },
        { "qwen2.5-coder-14b-instruct",    "Qwen2.5 Coder 14B" },
        { "qwen2.5-coder-32b-instruct",    "Qwen2.5 Coder 32B" },
        { "qwen2.5-coder-72b-instruct",    "Qwen2.5 Coder 72B" },
        { "qwen2.5-0.5b-instruct",         "Qwen2.5 0.5B" },
        { "qwen2.5-1.5b-instruct",         "Qwen2.5 1.5B" },
        { "qwen2.5-3b-instruct",           "Qwen2.5 3B" },
        { "qwen2.5-7b-instruct",           "Qwen2.5 7B" },
        { "qwen2.5-14b-instruct",          "Qwen2.5 14B" },
        { "qwen2.5-32b-instruct",          "Qwen2.5 32B" },
        { "qwen2.5-72b-instruct",          "Qwen2.5 72B" },
        { "mistral-nemo-instruct-2407",    "Mistral NeMo Instruct" },
        { "mistral-small-instruct-2409",   "Mistral Small Instruct" },
        { "llama-3.2-1b-instruct",         "Llama 3.2 1B" },
        { "llama-3.2-3b-instruct",         "Llama 3.2 3B" },
        { "llama-3.1-8b-instruct",         "Llama 3.1 8B" },
        { "llama-3.1-70b-instruct",        "Llama 3.1 70B" },
        { "llama-3-8b-instruct",           "Llama 3 8B" },
        { "llama-3-70b-instruct",          "Llama 3 70B" },
        { "codellama-7b-instruct",         "CodeLlama 7B" },
        { "codellama-13b-instruct",        "CodeLlama 13B" },
        { "codellama-34b-instruct",        "CodeLlama 34B" },
        { "deepseek-coder-v2-lite",        "DeepSeek Coder V2 Lite" },
        { "deepseek-r1-distill-qwen-1.5b","DeepSeek R1 1.5B" },
        { "deepseek-r1-distill-qwen-7b",  "DeepSeek R1 7B" },
        { "deepseek-r1-distill-qwen-14b", "DeepSeek R1 14B" },
        { "deepseek-r1-distill-qwen-32b", "DeepSeek R1 32B" },
        { "deepseek-r1-distill-llama-8b",  "DeepSeek R1 Llama 8B" },
        { "deepseek-r1-distill-llama-70b", "DeepSeek R1 Llama 70B" },
        { "gemma-2-2b-it",                 "Gemma 2 2B" },
        { "gemma-2-9b-it",                 "Gemma 2 9B" },
        { "gemma-2-27b-it",                "Gemma 2 27B" },
        { "phi-3-mini-4k-instruct",        "Phi-3 Mini 4K" },
        { "phi-3-medium-4k-instruct",      "Phi-3 Medium 4K" },
    };

    for (const auto &m : knownModels) {
        if (name == m.pattern) return m.display;
    }

    // Fallback: replace hyphens with spaces, capitalize first letter of each word
    name = raw.trimmed();
    name.replace('-', ' ');
    name.replace('_', ' ');
    // Capitalize first letter
    if (!name.isEmpty()) name[0] = name[0].toUpper();
    return name;
}

inline QString providerDisplay(const ProviderConfig &cfg, bool demoMode = false) {
    if (demoMode) return "Modo demo";
    if (!cfg.valid()) return "Sin modelo";

    // Detect provider from baseUrl
    QString baseUrl = cfg.baseUrl.toLower();
    QString provider = cfg.providerName.trimmed();
    if (!provider.isEmpty()) {
        // Keep the user-defined provider name.
    } else if (baseUrl.contains("127.0.0.1:1234") || baseUrl.contains("localhost:1234"))
        provider = "LM Studio";
    else if (baseUrl.contains("11434"))
        provider = "Ollama";
    else if (baseUrl.contains("api.groq.com"))
        provider = "Groq";
    else if (baseUrl.contains("openrouter"))
        provider = "OpenRouter";
    else if (baseUrl.contains("generativelanguage.googleapis.com"))
        provider = "Google";
    else if (baseUrl.contains("api.anthropic.com"))
        provider = "Anthropic";
    else
        provider = "Custom";

    QString model = prettyModelName(cfg.modelId);
    if (model.isEmpty()) model = cfg.modelId;
    return provider + " · " + model;
}
