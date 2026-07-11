#pragma once
#include <QObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "ModelTypes.h"

class OpenAIClient : public QObject {
    Q_OBJECT
public:
    explicit OpenAIClient(QObject *parent = nullptr);

    void sendChatCompletion(const QVector<Message> &messages, const ProviderConfig &config);
    void cancel();
    void detectModels(const QString &baseUrl);

signals:
    void tokenReceived(const QString &token);
    void finished(const QString &fullResponse);
    void errorOccurred(const QString &message);
    void modelsDetected(const QStringList &modelIds);
    void modelsDetectedWithContext(const QMap<QString, int> &modelContexts); // modelId → contextWindowTokens

private slots:
    void onReadyRead();
    void onFinished();

private:
    QNetworkAccessManager *m_manager;
    QNetworkReply *m_currentReply = nullptr;
    QByteArray m_sseBuffer;
    QString m_fullResponse;
    bool m_streamActive = false;
    bool m_firstToken = true;
    ProviderConfig m_currentConfig;

    void processSSE();
    void handleNonStreamResponse(const QByteArray &data);
};
