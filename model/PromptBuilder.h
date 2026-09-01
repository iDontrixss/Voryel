#pragma once
#include <QString>

struct TaskEnvelopeParams {
    QString mode = "chat";
    QString intent = "software_assistance";
    QString userLanguage = "es";
    QString activeProvider;
    QString activeModel;
    QString providerId;
    QString modelId;
    bool isLocal = false;
    QString supportsVision = "unknown";
    bool fallbacksEnabled = false;
    QString importSource;
    QString userMessage;
};

class PromptBuilder {
public:
    static QString buildEnvelope(const TaskEnvelopeParams &params);
    static QString detectMode(const QString &userMessage, QString &outCleanMessage);
    static QString sanitizeMessage(const QString &message);
    static QString cleanModelResponse(const QString &response);
    static QString providerDisplayNameFromUrl(const QString &baseUrl);
    static QString detectProviderId(const QString &baseUrl);
};
