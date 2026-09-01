#pragma once
#include <QString>
#include <QMap>
#include "../model/ModelTypes.h"

struct TokenCountResult {
    int tokens = 0;
    QString method;
    QString accuracy;
};

struct ContextWindowInfo {
    int tokens = 0;
    QString source;
};

class TokenCounter {
public:
    static TokenCountResult estimateTokens(const QString &text);
    static int roughEstimate(const QString &text);

    static ContextWindowInfo resolveContextWindow(const QString &modelId,
                                                   const QString &providerId,
                                                   int userOverride = 0,
                                                   const QString &overrideSource = QString());

    static const QMap<QString, int>& knownModelContexts();

    static int countMessagesTokens(const QVector<Message> &messages);
};
