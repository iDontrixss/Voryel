#pragma once
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QPropertyAnimation>
#include <QPointer>
#include <functional>
#include "core/VoryelCore.h"

class QVBoxLayout;
class QHBoxLayout;
class QScrollArea;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class ChatStore;
class QFileInfo;
class QTimer;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class TokenRadialMeter;
class ConversationMinimap;
struct TokenUsage;

class ChatView : public QWidget {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);

    void setChatStore(ChatStore *store);
    void setActiveModel(const QString &model);
    void setDraftPrompt(const QString &prompt);
    void setAnimationsEnabled(bool enabled);
    void setCoreState(CoreState state);

    void setDemoMode(bool demo) { m_demoMode = demo; }
    bool isDemoMode() const { return m_demoMode; }

    void beginStreaming(const QStringList &attachments = QStringList());
    void appendStreamToken(const QString &token);
    void appendReasoningSummary(const QString &summaryDelta);
    void reportActivity(const QString &activity);
    void finishStreaming(const QString &fullResponse);
    void abortStream();
    void showErrorMessage(const QString &message);

    void refreshChatList();
    void loadChatMessages(int chatIndex);
    void updateContextUsage(const TokenUsage &usage, qint64 contextLimit,
                            const QString &provider, const QString &model,
                            const QString &limitSource,
                            bool compactionPending = false);

signals:
    void messageSent(const QString &prompt, const QStringList &attachments);
    void cancelRequested();
    void taskStateChanged(const QString &label);

private slots:
    void handleSend();
    void chooseAttachments();
    void continuePlan();
    void allowTerminal();
    void cancelPlan();

private:
    QString m_activeModel = "Sin modelo";
    bool m_working = false;
    bool m_animationsEnabled = true;
    bool m_demoMode = false;
    bool m_followLatest = true;
    int m_messageCount = 0;
    ChatStore *m_chatStore = nullptr;

    QScrollArea *m_scroll = nullptr;
    ConversationMinimap *m_conversationMinimap = nullptr;
    QWidget *m_messagesContent = nullptr;
    QVBoxLayout *m_messagesLayout = nullptr;
    QWidget *m_emptyState = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QWidget *m_inputBar = nullptr;
    QLabel *m_dropHintLabel = nullptr;
    QPushButton *m_attachButton = nullptr;
    QPushButton *m_sendButton = nullptr;
    QWidget *m_attachmentChipsWidget = nullptr;
    QHBoxLayout *m_attachmentChipsLayout = nullptr;
    QWidget *m_pastedTextCard = nullptr;
    QLabel *m_pastedTextInfo = nullptr;
    QPushButton *m_restorePastedTextButton = nullptr;
    QWidget *m_pasteRestoreUndoRow = nullptr;
    QTimer *m_pasteRestoreUndoTimer = nullptr;
    QLabel *m_modelBadgeLabel = nullptr;
    TokenRadialMeter *m_contextMeter = nullptr;
    QLabel *m_headerTitle = nullptr;
    QLabel *m_headerSubtitle = nullptr;
    QWidget *m_typingRow = nullptr;
    QLabel *m_headerIcon = nullptr;
    QWidget *m_typingAvatarSlot = nullptr;
    QWidget *m_typingAvatar = nullptr;
    QWidget *m_typingBubble = nullptr;
    QWidget *m_thinkingDetails = nullptr;
    QLabel *m_thinkingLine1 = nullptr;
    QLabel *m_thinkingLine2 = nullptr;
    QLabel *m_thinkingLine3 = nullptr;
    QPushButton *m_chatMenuButton = nullptr;
    QTimer *m_demoResponseTimer = nullptr;
    QPointer<QWidget> m_chatPopup;
    int m_pendingDeleteChatIndex = -1;
    QString m_pendingDeleteChatTitle;

    QString m_streamBuffer;
    QString m_reasoningBuffer;
    QStringList m_activityLog;
    QString m_liveReasoningLine;
    int m_embeddedReasoningLength = 0;
    bool m_generationActivityShown = false;
    bool m_receivingActivityShown = false;
    QStringList m_pendingAttachments;
    QString m_pendingPastedText;
    QString m_lastRestoredPastedText;
    QString m_replacedTextBeforePasteRestore;
    int m_pasteRestoreStart = -1;

    void buildUi();
    void setWorking(bool working, const QString &label = QString());
    void applyWorkingUi(bool working);
    void updateSendButton();
    void updateComposerHeight();
    void refreshThinkingActivity();
    void ensureConversationStarted();
    void scrollToBottom();

    void addUserMessage(const QString &text, const QStringList &attachments = QStringList());
    void addAssistantMessage(const QString &text);
    void addAssistantMessageInstant(const QString &text);
    void addAssistantMessageAnimated(const QString &text, std::function<void()> onFinished = {});
    QLabel* addMessageBubble(const QString &sizingText, const QString &visibleText, bool user, QLabel **outLabel = nullptr, const QString &copyText = QString(), const QStringList &attachments = QStringList());
    void addPlanCard();
    void addTerminalPermissionCard(const QString &command);
    void addDiffCard();

    void bounceAvatarOnce();

    QWidget* makeWelcomeState();
    QWidget* makeAvatar(bool assistant);
    QWidget* makeBubble(const QString &sizingText, const QString &visibleText, bool user, QLabel **contentLabel = nullptr, const QString &copyText = QString());
    QPushButton* makePrimaryButton(const QString &text);
    QPushButton* makeGhostButton(const QString &text);
    QPushButton* makeDangerButton(const QString &text);
    QWidget* makeAttachmentChip(const QString &path, int index, bool removable);
    QWidget* makeAttachmentPreview(const QString &path);
    QString attachmentDisplayName(const QString &path) const;
    QString attachmentSummary(const QStringList &attachments) const;
    QString attachmentKindLabel(const QString &path) const;
    bool isCompatibleDropFile(const QString &path) const;
    void refreshAttachmentChips();
    bool captureLargePaste(const QString &text);
    void refreshPastedTextCard();
    void restorePastedTextToComposer();
    void undoPastedTextRestore();
    void clearPastedTextRestoreUndo();
    void addPastedTextMessageCard(const QString &text);
    void setDropTargetActive(bool active);
    void removePendingAttachment(int index);

    void rebuildChatList();
    void onChatSelected(int index);
    void onNewChatClicked();
    void showChatPopup();
    void closeChatPopup();

    void clearMessages();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};
