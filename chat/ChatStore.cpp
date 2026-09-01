#include "ChatStore.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QUuid>
#include <QDebug>

ChatStore::ChatStore(QObject *parent) : QObject(parent) {
    m_storePath = storeDir() + "/chats.json";
    load();
    if (m_chats.isEmpty()) {
        createChat();
        save();
    }
}

QString ChatStore::storeDir() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + "/.voryel";
    QDir().mkpath(base);
    return base;
}

QString ChatStore::generateId() const {
    return "chat_" + QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz")
           + "_" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

QString ChatStore::currentTimestamp() const {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString ChatStore::autoTitle(const QString &firstMessage) const {
    QString clean = firstMessage.trimmed();
    if (clean.isEmpty()) return "Nuevo chat";
    if (clean.length() <= 35) return clean;
    return clean.left(35) + "...";
}

ChatSessionData* ChatStore::activeChat() {
    if (m_activeIndex < 0 || m_activeIndex >= m_chats.size()) return nullptr;
    return &m_chats[m_activeIndex];
}

ChatSessionData* ChatStore::chatAt(int index) {
    if (index < 0 || index >= m_chats.size()) return nullptr;
    return &m_chats[index];
}

int ChatStore::createChat() {
    ChatSessionData chat;
    chat.id = generateId();
    chat.title = "Nuevo chat";
    chat.createdAt = currentTimestamp();
    chat.updatedAt = chat.createdAt;
    m_chats.prepend(chat);
    m_activeIndex = 0;
    save();
    emit chatCreated(m_activeIndex);
    emit chatSwitched(m_activeIndex);
    return m_activeIndex;
}

void ChatStore::switchToChat(int index) {
    if (index < 0 || index >= m_chats.size()) return;
    if (index == m_activeIndex) return;
    m_activeIndex = index;
    save();
    emit chatSwitched(m_activeIndex);
}

void ChatStore::deleteChat(int index) {
    if (index < 0 || index >= m_chats.size()) return;
    if (m_chats.size() <= 1) {
        // Don't delete the last chat — reset it instead
        m_chats[0].title = "Nuevo chat";
        m_chats[0].messages.clear();
        m_chats[0].lastSnippet.clear();
        m_chats[0].contextSummary.clear();
        m_chats[0].compactedUpToIndex = -1;
        m_chats[0].compactionPending = false;
        m_chats[0].updatedAt = currentTimestamp();
        m_activeIndex = 0;
        save();
        emit chatUpdated(0);
        emit chatSwitched(0);
        return;
    }
    m_chats.removeAt(index);
    if (m_activeIndex >= m_chats.size())
        m_activeIndex = m_chats.size() - 1;
    else if (m_activeIndex > index)
        m_activeIndex--;
    save();
    emit chatDeleted(index);
    emit chatSwitched(m_activeIndex);
}

void ChatStore::renameChat(int index, const QString &newTitle) {
    if (index < 0 || index >= m_chats.size()) return;
    m_chats[index].title = newTitle;
    m_chats[index].updatedAt = currentTimestamp();
    save();
    emit chatUpdated(index);
}

void ChatStore::addMessage(int chatIndex, const ChatMessageData &msg) {
    if (chatIndex < 0 || chatIndex >= m_chats.size()) return;
    m_chats[chatIndex].messages.append(msg);
    m_chats[chatIndex].updatedAt = currentTimestamp();
    if (msg.role == "user" && m_chats[chatIndex].messages.size() == 1) {
        m_chats[chatIndex].title = autoTitle(msg.content);
    }
    m_chats[chatIndex].lastSnippet = msg.content.left(60);
    save();
    emit messagesChanged(chatIndex);
}

void ChatStore::updateLastMessage(int chatIndex, const QString &content) {
    if (chatIndex < 0 || chatIndex >= m_chats.size()) return;
    if (m_chats[chatIndex].messages.isEmpty()) return;
    m_chats[chatIndex].messages.last().content = content;
    m_chats[chatIndex].updatedAt = currentTimestamp();
    m_chats[chatIndex].lastSnippet = content.left(60);
    save();
    emit messagesChanged(chatIndex);
}

void ChatStore::save() {
    QJsonObject root;
    root["activeChatId"] = m_activeIndex >= 0 && m_activeIndex < m_chats.size()
                           ? m_chats[m_activeIndex].id : QString();

    QJsonArray chatsArray;
    for (const auto &chat : m_chats) {
        QJsonObject chatObj;
        chatObj["id"] = chat.id;
        chatObj["title"] = chat.title;
        chatObj["createdAt"] = chat.createdAt;
        chatObj["updatedAt"] = chat.updatedAt;
        chatObj["lastSnippet"] = chat.lastSnippet;

        if (!chat.contextSummary.isEmpty()) chatObj["contextSummary"] = chat.contextSummary;
        if (chat.compactedUpToIndex >= 0) chatObj["compactedUpToIndex"] = chat.compactedUpToIndex;
        if (chat.compactionPending) chatObj["compactionPending"] = true;
        if (!chat.compactionCreatedAt.isEmpty()) chatObj["compactionCreatedAt"] = chat.compactionCreatedAt;
        if (chat.estimatedTokensBeforeCompaction > 0) chatObj["estimatedTokensBeforeCompaction"] = chat.estimatedTokensBeforeCompaction;
        if (chat.estimatedTokensAfterCompaction > 0) chatObj["estimatedTokensAfterCompaction"] = chat.estimatedTokensAfterCompaction;
        if (chat.lastEstimatedContextTokens > 0) chatObj["lastEstimatedContextTokens"] = chat.lastEstimatedContextTokens;
        if (chat.lastEstimatedContextMaxTokens > 0) chatObj["lastEstimatedContextMaxTokens"] = chat.lastEstimatedContextMaxTokens;
        if (!chat.lastContextProviderId.isEmpty()) chatObj["lastContextProviderId"] = chat.lastContextProviderId;
        if (!chat.lastContextModelId.isEmpty()) chatObj["lastContextModelId"] = chat.lastContextModelId;
        if (!chat.tokenEstimateAccuracy.isEmpty()) chatObj["tokenEstimateAccuracy"] = chat.tokenEstimateAccuracy;

        QJsonArray msgsArray;
        for (const auto &msg : chat.messages) {
            QJsonObject msgObj;
            msgObj["role"] = msg.role;
            msgObj["content"] = msg.content;
            msgObj["timestamp"] = msg.timestamp;
            if (!msg.modelId.isEmpty()) msgObj["modelId"] = msg.modelId;
            if (!msg.provider.isEmpty()) msgObj["provider"] = msg.provider;
            if (!msg.attachments.isEmpty()) {
                QJsonArray attachmentsArray;
                for (const QString &attachment : msg.attachments) {
                    const QString name = QFileInfo(attachment).fileName();
                    attachmentsArray.append(name.isEmpty() ? attachment : name);
                }
                msgObj["attachments"] = attachmentsArray;
            }
            if (msg.usage.valid()) {
                QJsonObject usageObj;
                usageObj["input"] = static_cast<double>(msg.usage.input);
                usageObj["output"] = static_cast<double>(msg.usage.output);
                usageObj["reasoning"] = static_cast<double>(msg.usage.reasoning);
                usageObj["cacheRead"] = static_cast<double>(msg.usage.cacheRead);
                usageObj["cacheWrite"] = static_cast<double>(msg.usage.cacheWrite);
                usageObj["providerTotal"] = static_cast<double>(msg.usage.providerTotal);
                usageObj["contextLimit"] = static_cast<double>(msg.usage.contextLimit);
                usageObj["providerId"] = msg.usage.providerId;
                usageObj["modelId"] = msg.usage.modelId;
                usageObj["source"] = msg.usage.source;
                usageObj["timestamp"] = msg.usage.timestamp;
                msgObj["usage"] = usageObj;
            }
            msgsArray.append(msgObj);
        }
        chatObj["messages"] = msgsArray;
        chatsArray.append(chatObj);
    }
    root["chats"] = chatsArray;

    QSaveFile file(m_storePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "ChatStore: no se pudo guardar" << m_storePath;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
        qWarning() << "ChatStore: no se pudo confirmar guardado" << m_storePath;
}

void ChatStore::load() {
    m_chats.clear();
    m_activeIndex = -1;

    QFile file(m_storePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "ChatStore: JSON corrupto, creando backup y chat vacío";
        QFile::rename(m_storePath, m_storePath + ".backup." + QDateTime::currentDateTime().toString("yyyyMMddHHmmss"));
        return;
    }

    QJsonObject root = doc.object();
    QString activeId = root["activeChatId"].toString();
    QJsonArray chatsArray = root["chats"].toArray();

    for (const auto &chatVal : chatsArray) {
        QJsonObject chatObj = chatVal.toObject();
        ChatSessionData chat;
        chat.id = chatObj["id"].toString();
        chat.title = chatObj["title"].toString("Nuevo chat");
        chat.createdAt = chatObj["createdAt"].toString();
        chat.updatedAt = chatObj["updatedAt"].toString();
        chat.lastSnippet = chatObj["lastSnippet"].toString();

        chat.contextSummary = chatObj["contextSummary"].toString();
        chat.compactedUpToIndex = chatObj["compactedUpToIndex"].toInt(-1);
        chat.compactionPending = chatObj["compactionPending"].toBool(false);
        chat.compactionCreatedAt = chatObj["compactionCreatedAt"].toString();
        chat.estimatedTokensBeforeCompaction = chatObj["estimatedTokensBeforeCompaction"].toInt(0);
        chat.estimatedTokensAfterCompaction = chatObj["estimatedTokensAfterCompaction"].toInt(0);
        chat.lastEstimatedContextTokens = chatObj["lastEstimatedContextTokens"].toInt(0);
        chat.lastEstimatedContextMaxTokens = chatObj["lastEstimatedContextMaxTokens"].toInt(0);
        chat.lastContextProviderId = chatObj["lastContextProviderId"].toString();
        chat.lastContextModelId = chatObj["lastContextModelId"].toString();
        chat.tokenEstimateAccuracy = chatObj["tokenEstimateAccuracy"].toString();

        QJsonArray msgsArray = chatObj["messages"].toArray();
        for (const auto &msgVal : msgsArray) {
            QJsonObject msgObj = msgVal.toObject();
            ChatMessageData msg;
            msg.role = msgObj["role"].toString();
            msg.content = msgObj["content"].toString();
            msg.timestamp = msgObj["timestamp"].toString();
            msg.modelId = msgObj["modelId"].toString();
            msg.provider = msgObj["provider"].toString();
            QJsonArray attachmentsArray = msgObj["attachments"].toArray();
            for (const auto &attachmentVal : attachmentsArray) {
                const QString attachment = attachmentVal.toString();
                if (!attachment.isEmpty()) {
                    const QString name = QFileInfo(attachment).fileName();
                    msg.attachments.append(name.isEmpty() ? attachment : name);
                }
            }
            const QJsonObject usageObj = msgObj["usage"].toObject();
            if (!usageObj.isEmpty()) {
                msg.usage.input = static_cast<qint64>(usageObj["input"].toDouble());
                msg.usage.output = static_cast<qint64>(usageObj["output"].toDouble());
                msg.usage.reasoning = static_cast<qint64>(usageObj["reasoning"].toDouble());
                msg.usage.cacheRead = static_cast<qint64>(usageObj["cacheRead"].toDouble());
                msg.usage.cacheWrite = static_cast<qint64>(usageObj["cacheWrite"].toDouble());
                msg.usage.providerTotal = static_cast<qint64>(usageObj["providerTotal"].toDouble());
                msg.usage.contextLimit = static_cast<qint64>(usageObj["contextLimit"].toDouble());
                msg.usage.providerId = usageObj["providerId"].toString();
                msg.usage.modelId = usageObj["modelId"].toString();
                msg.usage.source = usageObj["source"].toString();
                msg.usage.timestamp = usageObj["timestamp"].toString();
            }
            chat.messages.append(msg);
        }
        m_chats.append(chat);
    }

    // Restore active chat
    for (int i = 0; i < m_chats.size(); ++i) {
        if (m_chats[i].id == activeId) {
            m_activeIndex = i;
            break;
        }
    }
    if (m_activeIndex < 0 && !m_chats.isEmpty())
        m_activeIndex = 0;
}
