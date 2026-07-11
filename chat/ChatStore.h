#pragma once
#include <QObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QJsonDocument>

struct ChatMessageData {
    QString role;
    QString content;
    QString timestamp;
    QString modelId;
    QString provider;
    QStringList attachments;
};

struct ChatSessionData {
    QString id;
    QString title;
    QString createdAt;
    QString updatedAt;
    QString lastSnippet;
    QVector<ChatMessageData> messages;

    // Context/compaction state
    QString contextSummary;
    int compactedUpToIndex = -1;
    bool compactionPending = false;
    QString compactionCreatedAt;
    int estimatedTokensBeforeCompaction = 0;
    int estimatedTokensAfterCompaction = 0;
    int lastEstimatedContextTokens = 0;
    int lastEstimatedContextMaxTokens = 0;
    QString lastContextProviderId;
    QString lastContextModelId;
    QString tokenEstimateAccuracy;
};

class ChatStore : public QObject {
    Q_OBJECT
public:
    explicit ChatStore(QObject *parent = nullptr);

    QVector<ChatSessionData> chats() const { return m_chats; }
    int chatCount() const { return m_chats.size(); }
    int activeChatIndex() const { return m_activeIndex; }
    ChatSessionData* activeChat();
    ChatSessionData* chatAt(int index);

    int createChat();
    void switchToChat(int index);
    void deleteChat(int index);
    void renameChat(int index, const QString &newTitle);

    void addMessage(int chatIndex, const ChatMessageData &msg);
    void updateLastMessage(int chatIndex, const QString &content);

    void save();
    void load();

signals:
    void chatCreated(int index);
    void chatDeleted(int index);
    void chatSwitched(int index);
    void chatUpdated(int index);
    void messagesChanged(int chatIndex);

private:
    QVector<ChatSessionData> m_chats;
    int m_activeIndex = -1;
    QString m_storePath;

    QString generateId() const;
    QString currentTimestamp() const;
    QString autoTitle(const QString &firstMessage) const;
    QString storeDir() const;
};
