#pragma once
#include <QObject>
#include <QStringList>
#include <QVector>
#include "ModelTypes.h"
#include "OpenAIClient.h"

class ChatSession : public QObject {
    Q_OBJECT
public:
    explicit ChatSession(QObject *parent = nullptr);

    static QString systemPrompt();

    void setProviderConfig(const ProviderConfig &config);
    ProviderConfig providerConfig() const { return m_config; }
    bool isConfigured() const { return m_config.valid(); }

    void setFallbacksEnabled(bool enabled) { m_fallbacksEnabled = enabled; }
    bool fallbacksEnabled() const { return m_fallbacksEnabled; }

    void sendMessage(const QString &text, const QStringList &attachments = QStringList());
    void replaceHistory(const QVector<Message> &messages,
                        const QString &contextSummary = QString());
    void cancel();
    void clear();
    void detectModels(const QString &baseUrl);

signals:
    void started();
    void tokenReceived(const QString &token);
    void reasoningReceived(const QString &summaryDelta);
    void usageReceived(const TokenUsage &usage);
    void finished(const QString &fullResponse);
    void errorOccurred(const QString &message);
    void cancelled();
    void modelsDetected(const QStringList &modelIds);
    void modelsDetectedWithContext(const QMap<QString, int> &modelContexts);

private slots:
    void onTokenReceived(const QString &token);
    void onFinished(const QString &fullResponse);
    void onError(const QString &message);

private:
    OpenAIClient *m_client;
    ProviderConfig m_config;
    QVector<Message> m_messages;
    bool m_fallbacksEnabled = false;
};
