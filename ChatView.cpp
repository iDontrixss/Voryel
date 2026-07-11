#include "ChatView.h"
#include "Style.h"
#include "SolidPanel.h"
#include "IconUtil.h"
#include "TokenRadialMeter.h"
#include "chat/ChatStore.h"
#include "model/ModelTypes.h"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QUrl>
#include <QDebug>
#include <QPointer>

#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QTimer>
#include <QDateTime>
#include <QLayoutItem>
#include <QSizePolicy>
#include <QtGlobal>
#include <memory>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QAbstractAnimation>
#include <QObject>
#include <functional>

namespace {

QLabel* iconLabel(const QString &iconRes, const QColor &tint, int size) {
    auto *lbl = new QLabel();
    lbl->setFixedSize(size, size);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setPixmap(IconUtil::coloredIcon(iconRes, tint, QSize(size, size)).pixmap(size, size));
    lbl->setStyleSheet("border: none; background: transparent;");
    return lbl;
}

QWidget* decoShape(int size, const QString &color, int radius) {
    auto *w = new QWidget();
    w->setFixedSize(size, size);
    w->setAttribute(Qt::WA_StyledBackground, true);
    w->setStyleSheet(QString("background: %1; border: none; border-radius: %2px;").arg(color).arg(radius));
    return w;
}

QLabel* textLabel(const QString &text, int px, const QString &color, int weight = 600) {
    auto *lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QString("font-size: %1px; font-weight: %2; color: %3; border: none; background: transparent;")
                           .arg(px).arg(weight).arg(color));
    return lbl;
}

QWidget* statusDot(const QString &color, int size = 7) {
    auto *dot = new QLabel();
    dot->setAttribute(Qt::WA_StyledBackground, true);
    dot->setFixedSize(size, size);
    dot->setStyleSheet(QString("background: %1; border-radius: %2px; border: none;")
                           .arg(color).arg(size / 2));
    return dot;
}

void bounceDot(QWidget *dot, std::function<void()> onDone = nullptr) {
    if (!dot || !dot->parentWidget()) { if (onDone) onDone(); return; }
    auto *up = new QPropertyAnimation(dot, "pos", dot);
    up->setDuration(180);
    up->setStartValue(QPoint(0, 5));
    up->setEndValue(QPoint(0, 0));
    up->setEasingCurve(QEasingCurve::OutInSine);
    QObject::connect(up, &QPropertyAnimation::finished, dot, [dot, up, onDone]() {
        up->deleteLater();
        auto *down = new QPropertyAnimation(dot, "pos", dot);
        down->setDuration(180);
        down->setStartValue(QPoint(0, 0));
        down->setEndValue(QPoint(0, 5));
        down->setEasingCurve(QEasingCurve::OutInSine);
        QObject::connect(down, &QPropertyAnimation::finished, dot, [dot, down, onDone]() {
            down->deleteLater();
            if (onDone) onDone();
        });
        down->start();
    });
    up->start();
}

void bounceSequence(const QVector<QWidget*> &dots, int gap, int waveGap,
                    std::function<void()> onWave = nullptr) {
    auto *timer = new QTimer(dots.first());
    timer->setInterval(waveGap);
    auto left = std::make_shared<int>(0);
    auto wave = [dots, gap, timer, left, onWave]() {
        *left = dots.size();
        for (int i = 0; i < dots.size(); ++i)
            QTimer::singleShot(i * gap, dots[i], [dot = dots[i], timer, left]() {
                bounceDot(dot, [timer, left]() { if (--*left == 0) timer->start(); });
            });
        if (onWave) onWave();
    };
    QObject::connect(timer, &QTimer::timeout, dots.first(), wave);
    wave();
}

void fadeIn(QWidget *widget, int = 170) {
    Q_UNUSED(widget);
}

bool isPreviewImageFile(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "png" || suffix == "jpg" || suffix == "jpeg"
        || suffix == "webp" || suffix == "gif" || suffix == "bmp";
}

} // namespace

ChatView::ChatView(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);
    buildUi();
}

void ChatView::buildUi() {
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header: SolidPanel with bottom border ──
    auto *header = new SolidPanel(this);
    header->setFillColor(QColor(Style::WHITE));
    header->setCornerRadius(0);
    header->setBottomBorder(QColor(Style::INK), 2);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 10, 16, 10);
    headerLayout->setSpacing(10);

    m_headerIcon = iconLabel(":/icons/icons/bot.svg", QColor(Style::VIOLET), 15);
    headerLayout->addWidget(m_headerIcon);

    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(1);
    m_headerTitle = new QLabel("Chat con Voryel");
    m_headerTitle->setStyleSheet(QString("font-size: 14px; font-weight: 900; color: %1; border: none; background: transparent;").arg(Style::INK));
    titleCol->addWidget(m_headerTitle);
    m_headerSubtitle = new QLabel("Sin modelo");
    m_headerSubtitle->setStyleSheet(QString("font-size: 11px; font-weight: 600; color: %1; border: none; background: transparent;").arg(Style::TEXT_MUTED));
    titleCol->addWidget(m_headerSubtitle);
    headerLayout->addLayout(titleCol, 1);

    // Context meter
    m_contextMeter = new TokenRadialMeter(header);
    headerLayout->addWidget(m_contextMeter, 0, Qt::AlignVCenter);

    auto *menuCard = new SolidPanel(header);
    menuCard->setFillColor(QColor(Style::WHITE));
    menuCard->setFullBorder(QColor(Style::INK), 2);
    menuCard->setHardShadow(QColor(Style::INK), 2, 2);
    menuCard->setCornerRadius(8);
    menuCard->setCursor(Qt::PointingHandCursor);
    auto *menuCardLayout = new QVBoxLayout(menuCard);
    menuCardLayout->setContentsMargins(0, 0, 2, 2);
    m_chatMenuButton = new QPushButton("Mis Chats");
    m_chatMenuButton->setFlat(true);
    m_chatMenuButton->setCursor(Qt::PointingHandCursor);
    m_chatMenuButton->setMinimumHeight(30);
    m_chatMenuButton->setStyleSheet(QString(
        "QPushButton { background: transparent; color: %1; border: none; padding: 0 14px; font-size: 11px; font-weight: 900; }"
        "QPushButton:hover { color: %2; }"
    ).arg(Style::INK, Style::VIOLET));
    connect(m_chatMenuButton, &QPushButton::clicked, this, &ChatView::showChatPopup);
    menuCardLayout->addWidget(m_chatMenuButton);
    headerLayout->addWidget(menuCard);

    root->addWidget(header);

    // ── Middle: messages only (no left panel) ──
    auto *middle = new QWidget();
    middle->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));
    auto *middleLayout = new QHBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);

    m_scroll = new QScrollArea();
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(Style::scrollAreaStyle());
    m_scroll->viewport()->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));

    m_messagesContent = new QWidget();
    m_messagesContent->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));
    m_messagesLayout = new QVBoxLayout(m_messagesContent);
    m_messagesLayout->setContentsMargins(24, 16, 24, 24);
    m_messagesLayout->setSpacing(8);
    m_messagesLayout->setAlignment(Qt::AlignTop);
    m_emptyState = makeWelcomeState();
    m_messagesLayout->addStretch(1);
    m_messagesLayout->addWidget(m_emptyState, 0, Qt::AlignCenter);
    m_messagesLayout->addStretch(1);

    m_scroll->setWidget(m_messagesContent);
    middleLayout->addWidget(m_scroll, 1);

    root->addWidget(middle, 1);

    // ── Input bar ──
    m_inputBar = new QWidget(this);
    m_inputBar->setAcceptDrops(true);
    m_inputBar->setAttribute(Qt::WA_StyledBackground, true);
    m_inputBar->setStyleSheet(QString("background: %1; border-top: 2px solid %2;").arg(Style::WHITE, Style::INK));
    auto *inputLayout = new QVBoxLayout(m_inputBar);
    inputLayout->setContentsMargins(20, 10, 20, 12);
    inputLayout->setSpacing(8);

    m_dropHintLabel = new QLabel("Suelta para adjuntar");
    m_dropHintLabel->setAlignment(Qt::AlignCenter);
    m_dropHintLabel->setVisible(false);
    m_dropHintLabel->setStyleSheet(QString(
        "background: %1; color: %2; border: 2px dashed %3; border-radius: 10px;"
        "padding: 8px 12px; font-size: 12px; font-weight: 900;"
    ).arg(Style::VIOLET_LIGHT, Style::INK, Style::VIOLET));
    inputLayout->addWidget(m_dropHintLabel);

    auto *inputRow = new QHBoxLayout();
    inputRow->setSpacing(10);
    m_attachButton = new QPushButton();
    m_attachButton->setFixedSize(42, 42);
    m_attachButton->setCursor(Qt::PointingHandCursor);
    m_attachButton->setToolTip("Adjuntar archivo");
    m_attachButton->setIcon(IconUtil::coloredIcon(":/icons/icons/paperclip.svg", QColor(Style::VIOLET), QSize(18, 18)));
    m_attachButton->setIconSize(QSize(18, 18));
    m_attachButton->setStyleSheet(QString(
        "QPushButton { background: %1; border: 2px solid %2; border-radius: 12px; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:disabled { background: %4; }"
    ).arg(Style::BG_LILAC, Style::INK, Style::VIOLET_LIGHT, Style::BORDER_SOFT));
    connect(m_attachButton, &QPushButton::clicked, this, &ChatView::chooseAttachments);
    inputRow->addWidget(m_attachButton);

    m_input = new QLineEdit();
    m_input->setPlaceholderText("Pedile algo a Voryel...");
    m_input->setMinimumHeight(42);
    m_input->setStyleSheet(QString(
        "QLineEdit {"
        "  background: %1; color: %2; border: 2px solid %3; border-radius: 12px;"
        "  padding: 0 16px; font-size: 14px; font-weight: 600;"
        "}"
        "QLineEdit:focus { border-color: %4; }"
    ).arg(Style::BG_LILAC, Style::INK, Style::BORDER_SOFT, Style::VIOLET));
    connect(m_input, &QLineEdit::returnPressed, this, &ChatView::handleSend);
    inputRow->addWidget(m_input, 1);

    m_sendButton = new QPushButton();
    m_sendButton->setFixedSize(42, 42);
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setIconSize(QSize(18, 18));
    connect(m_sendButton, &QPushButton::clicked, this, &ChatView::handleSend);
    inputRow->addWidget(m_sendButton);
    inputLayout->addLayout(inputRow);
    updateSendButton();

    m_attachmentChipsWidget = new QWidget(m_inputBar);
    m_attachmentChipsWidget->setStyleSheet("background: transparent; border: none;");
    m_attachmentChipsLayout = new QHBoxLayout(m_attachmentChipsWidget);
    m_attachmentChipsLayout->setContentsMargins(0, 0, 0, 0);
    m_attachmentChipsLayout->setSpacing(6);
    m_attachmentChipsWidget->setVisible(false);
    inputLayout->addWidget(m_attachmentChipsWidget);

    // Chips below input
    auto *chipsRow = new QHBoxLayout();
    chipsRow->setSpacing(6);
    struct Chip { QString label; };
    const QVector<Chip> chips = {
        { "Arreglar bug" },
        { "Mejorar UI" },
        { "Explicar código" },
    };
    for (const auto &c : chips) {
        auto *chip = new QPushButton(c.label);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; border: 2px solid %3; border-radius: 8px;"
            "  padding: 4px 12px; font-size: 11px; font-weight: 800; }"
            "QPushButton:hover { background: %4; border-color: %5; }"
        ).arg(Style::WHITE, Style::INK, Style::BORDER_SOFT, Style::BG_LILAC, Style::VIOLET));
        connect(chip, &QPushButton::clicked, this, [this, label = c.label]() {
            m_input->setText(label);
            m_input->setFocus();
        });
        chipsRow->addWidget(chip);
    }
    chipsRow->addStretch();
    inputLayout->addLayout(chipsRow);

    root->addWidget(m_inputBar);
}

void ChatView::setActiveModel(const QString &model) {
    m_activeModel = model.isEmpty() ? "Sin modelo" : model;
    if (m_modelBadgeLabel) m_modelBadgeLabel->setText(m_activeModel);
    if (m_headerSubtitle) m_headerSubtitle->setText(m_activeModel);
}

void ChatView::setAnimationsEnabled(bool enabled) {
    m_animationsEnabled = enabled;
    if (!enabled && m_typingAvatar) {
        for (auto *child : m_typingAvatar->children()) {
            if (auto *anim = qobject_cast<QPropertyAnimation*>(child)) {
                anim->stop();
                anim->deleteLater();
            }
        }
        m_typingAvatar->move(3, 7);
    }
}

void ChatView::bounceAvatarOnce() {
    if (!m_typingAvatar) return;
    auto *up = new QPropertyAnimation(m_typingAvatar, "pos", m_typingAvatar);
    up->setDuration(180);
    up->setStartValue(QPoint(3, 7));
    up->setEndValue(QPoint(3, 0));
    up->setEasingCurve(QEasingCurve::OutInSine);
    connect(up, &QPropertyAnimation::finished, this, [this, up]() {
        up->deleteLater();
        if (!m_typingAvatar) return;
        auto *down = new QPropertyAnimation(m_typingAvatar, "pos", m_typingAvatar);
        down->setDuration(180);
        down->setStartValue(QPoint(3, 0));
        down->setEndValue(QPoint(3, 7));
        down->setEasingCurve(QEasingCurve::OutInSine);
        down->start(QAbstractAnimation::DeleteWhenStopped);
    });
    up->start();
}

void ChatView::setDraftPrompt(const QString &prompt) {
    if (!prompt.isEmpty() && m_input) {
        m_input->setText(prompt);
        m_input->setFocus();
    }
}

void ChatView::ensureConversationStarted() {
    if (!m_emptyState) return;
    if (!m_messagesLayout) return;
    // Quitamos el welcome + stretches del estado vacío la primera vez.
    while (QLayoutItem *item = m_messagesLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_emptyState = nullptr;
}

void ChatView::handleSend() {
    qDebug() << "ChatView::handleSend ENTER";
    if (m_working) {
        emit cancelRequested();
        return;
    }
    if (!m_input || !m_messagesLayout) {
        qWarning() << "ChatView::handleSend: m_input or m_messagesLayout is null";
        return;
    }
    const QString text = m_input->text().trimmed();
    if (text.isEmpty() && m_pendingAttachments.isEmpty()) {
        qDebug() << "ChatView::handleSend: empty text and no attachments, returning";
        return;
    }

    qDebug() << "ChatView::handleSend: text length =" << text.length()
             << "m_working =" << m_working
             << "m_demoMode =" << m_demoMode
             << "activeChatIndex =" << (m_chatStore ? m_chatStore->activeChatIndex() : -1);

    ensureConversationStarted();
    const QStringList attachments = m_pendingAttachments;
    const QString outgoingText = text.isEmpty() ? "Archivos adjuntos" : text;
    addUserMessage(outgoingText, attachments);
    m_input->clear();
    m_pendingAttachments.clear();
    refreshAttachmentChips();
    emit messageSent(outgoingText, attachments);

    if (m_demoMode) {
        setWorking(true, "Voryel está pensando");
        if (m_demoResponseTimer) {
            m_demoResponseTimer->stop();
            m_demoResponseTimer->deleteLater();
        }
        m_demoResponseTimer = new QTimer(this);
        m_demoResponseTimer->setSingleShot(true);
        connect(m_demoResponseTimer, &QTimer::timeout, this, [this]() {
            m_demoResponseTimer->deleteLater();
            m_demoResponseTimer = nullptr;
            setWorking(false);
            addAssistantMessageAnimated(
                "Entendido. Analicé el proyecto y preparé un plan de acción. Revisá los pasos antes de continuar.",
                [this]() {
                    addPlanCard();
                    emit taskStateChanged("Plan listo");
                }
            );
        });
        m_demoResponseTimer->start(15000);
    }
}

void ChatView::chooseAttachments() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Adjuntar archivo",
        QString(),
        "Archivos compatibles (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg *.zip *.7z *.rar *.txt *.md *.json *.jsonc *.yaml *.yml *.toml *.xml *.ini *.cfg *.conf *.c *.h *.cpp *.hpp *.cs *.java *.kt *.kts *.py *.rb *.php *.go *.rs *.dart *.lua *.js *.jsx *.ts *.tsx *.vue *.svelte *.html *.css *.scss *.sass *.less *.sql *.graphql *.proto *.sh *.ps1 *.bat *.cmd *.cmake *.gradle *.pro *.pri *.qrc *.ui *.qml *.qss *.pdf *.docx *.xlsx *.pptx);;Todos los archivos (*.*)"
    );
    if (files.isEmpty()) return;
    for (const QString &file : files) {
        if (!file.isEmpty() && isCompatibleDropFile(file) && !m_pendingAttachments.contains(file))
            m_pendingAttachments.append(file);
    }
    refreshAttachmentChips();
}

void ChatView::setWorking(bool working, const QString &label) {
    if (working == m_working) return;
    applyWorkingUi(working);
    emit taskStateChanged(label.isEmpty() ? "Pensando" : label);
}

void ChatView::setCoreState(CoreState state) {
    const bool shouldWork = (state == CoreState::Thinking
                          || state == CoreState::Planning
                          || state == CoreState::Executing
                          || state == CoreState::RollingBack);
    if (shouldWork == m_working) return;
    applyWorkingUi(shouldWork);
}

void ChatView::applyWorkingUi(bool working) {
    m_working = working;
    updateSendButton();
    if (m_input) m_input->setEnabled(!working);
    if (m_attachButton) m_attachButton->setEnabled(!working);
    if (working) {
        if (!m_typingRow) {
            ensureConversationStarted();
            auto *row = new QWidget();
            row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(9);
            auto *avatarSlot = new QWidget();
            avatarSlot->setFixedSize(36, 42);
            avatarSlot->setAttribute(Qt::WA_StyledBackground, false);
            auto *slotAvatar = makeAvatar(true);
            slotAvatar->setParent(avatarSlot);
            slotAvatar->move(3, 7);
            slotAvatar->show();
            m_typingAvatarSlot = avatarSlot;
            m_typingAvatar = slotAvatar;
            rowLayout->addWidget(avatarSlot, 0, Qt::AlignTop);
            auto *bubble = new QWidget();
            bubble->setAttribute(Qt::WA_StyledBackground, true);
            bubble->setStyleSheet(QString(
                "background: %1; border: 1px solid %2; border-radius: 12px;"
            ).arg(Style::WHITE, Style::BORDER_SOFT));
            auto *bLayout = new QHBoxLayout(bubble);
            bLayout->setContentsMargins(14, 10, 16, 13);
            bLayout->setSpacing(7);
            auto makeDot = [](const QString &color, int size) -> QWidget* {
                auto *c = new QWidget();
                c->setFixedSize(size, size + 10);
                auto *dot = new QLabel(c);
                dot->setFixedSize(size, size);
                dot->move(0, 5);
                dot->setAttribute(Qt::WA_StyledBackground, true);
                dot->setStyleSheet(QString("background: %1; border-radius: %2px; border: none;")
                                       .arg(color).arg(size / 2));
                return c;
            };
            auto *container1 = makeDot(Style::VIOLET, 8);
            auto *container2 = makeDot(Style::VIOLET, 8);
            auto *container3 = makeDot(Style::VIOLET, 8);
            auto *dot1 = container1->findChild<QLabel*>();
            auto *dot2 = container2->findChild<QLabel*>();
            auto *dot3 = container3->findChild<QLabel*>();
            bLayout->addWidget(container1);
            bLayout->addWidget(container2);
            bLayout->addWidget(container3);
            rowLayout->addWidget(bubble, 0, Qt::AlignTop | Qt::AlignLeft);
            rowLayout->addStretch(1);
            m_typingRow = row;
            m_messagesLayout->addWidget(m_typingRow);
            fadeIn(m_typingRow, 150);

            auto dots = QVector<QWidget*>{dot1, dot2, dot3};
            bounceSequence(dots, 100, 1000, [this]() {
                if (m_animationsEnabled) bounceAvatarOnce();
            });
        }
        if (m_animationsEnabled && m_typingAvatar) {
            bounceAvatarOnce();
        }
    } else {
        if (m_typingAvatar) {
            for (auto *child : m_typingAvatar->children()) {
                if (auto *anim = qobject_cast<QPropertyAnimation*>(child)) {
                    anim->stop();
                    anim->deleteLater();
                }
            }
            m_typingAvatar->move(3, 7);
        }
        if (m_typingRow) {
            m_typingRow->deleteLater();
            m_typingRow = nullptr;
            m_typingAvatarSlot = nullptr;
            m_typingAvatar = nullptr;
        }
    }
    scrollToBottom();
}

void ChatView::updateSendButton() {
    if (!m_sendButton) return;
    if (m_working) {
        m_sendButton->setToolTip("Cancelar respuesta");
        m_sendButton->setIcon(IconUtil::coloredIcon(":/icons/icons/stop.svg", QColor(Style::WHITE), QSize(18, 18)));
        m_sendButton->setStyleSheet(QString(
            "QPushButton { background: #dc2626; border: 2px solid %1; border-radius: 12px; }"
            "QPushButton:hover { background: #991b1b; }"
        ).arg(Style::INK));
    } else {
        m_sendButton->setToolTip("Enviar mensaje");
        m_sendButton->setIcon(IconUtil::coloredIcon(":/icons/icons/send.svg", QColor(Style::WHITE), QSize(18, 18)));
        m_sendButton->setStyleSheet(QString(
            "QPushButton { background: %1; border: 2px solid %2; border-radius: 12px; }"
            "QPushButton:hover { background: %3; }"
            "QPushButton:disabled { background: %4; }"
        ).arg(Style::VIOLET, Style::INK, Style::VIOLET_DARK, Style::TEXT_FAINT));
    }
    m_sendButton->setEnabled(true);
}

void ChatView::addUserMessage(const QString &text, const QStringList &attachments) {
    addMessageBubble(text, text, true, nullptr, QString(), attachments);
}
void ChatView::addAssistantMessage(const QString &text) { addAssistantMessageAnimated(text); }
void ChatView::addAssistantMessageInstant(const QString &text) {
    ensureConversationStarted();
    addMessageBubble(text, text, false, nullptr, text);
}

void ChatView::addAssistantMessageAnimated(const QString &text, std::function<void()> onFinished) {
    ensureConversationStarted();

    QLabel *contentLabel = addMessageBubble(text, QString(), false, nullptr, text);
    if (!contentLabel) {
        if (onFinished) onFinished();
        return;
    }

    emit taskStateChanged("Voryel está escribiendo");

    auto *timer = new QTimer(this);
    auto index = std::make_shared<int>(0);
    const int chunk = text.size() > 120 ? 3 : 2;
    QPointer<QLabel> safeLabel = contentLabel;

    connect(timer, &QTimer::timeout, this, [this, timer, index, text, safeLabel, onFinished, chunk]() mutable {
        if (!safeLabel) {
            timer->stop();
            timer->deleteLater();
            return;
        }
        const int next = qMin(*index + chunk, text.size());
        safeLabel->setText(text.left(next));
        *index = next;
        scrollToBottom();

        if (*index >= text.size()) {
            timer->stop();
            timer->deleteLater();
            emit taskStateChanged("Respuesta lista");
            if (onFinished) {
                QTimer::singleShot(90, this, [onFinished]() { onFinished(); });
            }
        }
    });

    timer->start(14);
    scrollToBottom();
}

QLabel* ChatView::addMessageBubble(const QString &sizingText, const QString &visibleText, bool user, QLabel **outLabel, const QString &copyText, const QStringList &attachments) {
    if (!m_messagesLayout) {
        qWarning() << "ChatView::addMessageBubble: m_messagesLayout is null";
        return nullptr;
    }
    if (m_emptyState && m_emptyState->parent()) {
        m_messagesLayout->removeWidget(m_emptyState);
        m_emptyState->hide();
    }

    auto *outer = new QWidget();
    outer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *outerLayout = new QHBoxLayout(outer);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(9);

    QLabel *contentLabel = nullptr;
    auto *bubble = makeBubble(sizingText, visibleText, user, &contentLabel, copyText);
    if (outLabel) *outLabel = contentLabel;
    auto *time = new QLabel(QDateTime::currentDateTime().toString("hh:mm"));
    time->setStyleSheet(QString("font-size: 10px; color: %1; border: none; background: transparent;").arg(Style::TEXT_FAINT));

    auto *stack = new QWidget(outer);
    stack->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    auto *stackLayout = new QVBoxLayout(stack);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    stackLayout->setSpacing(2);
    if (user && !attachments.isEmpty()) {
        auto *attachmentRow = new QWidget(stack);
        attachmentRow->setStyleSheet("background: transparent; border: none;");
        auto *attachmentLayout = new QHBoxLayout(attachmentRow);
        attachmentLayout->setContentsMargins(0, 0, 0, 0);
        attachmentLayout->setSpacing(5);
        attachmentLayout->addStretch();
        for (int i = 0; i < attachments.size(); ++i) {
            attachmentLayout->addWidget(makeAttachmentPreview(attachments[i]));
        }
        stackLayout->addWidget(attachmentRow, 0, Qt::AlignRight);
    }
    stackLayout->addWidget(bubble, 0, user ? Qt::AlignRight : Qt::AlignLeft);
    stackLayout->addWidget(time, 0, user ? Qt::AlignRight : Qt::AlignLeft);

    if (user) {
        // Usuario: derecha.
        outerLayout->addStretch(1);
        outerLayout->addWidget(stack, 0, Qt::AlignTop | Qt::AlignRight);
        outerLayout->addWidget(makeAvatar(false), 0, Qt::AlignTop);
    } else {
        // Voryel/IA: izquierda.
        outerLayout->addWidget(makeAvatar(true), 0, Qt::AlignTop);
        outerLayout->addWidget(stack, 0, Qt::AlignTop | Qt::AlignLeft);
        outerLayout->addStretch(1);
    }

    m_messagesLayout->addWidget(outer, 0, Qt::AlignTop);
    fadeIn(outer);

    ++m_messageCount;
    scrollToBottom();
    return contentLabel;
}

QString ChatView::attachmentDisplayName(const QString &path) const {
    QFileInfo info(path);
    return info.fileName().isEmpty() ? path : info.fileName();
}

QString ChatView::attachmentSummary(const QStringList &attachments) const {
    if (attachments.isEmpty()) return QString();
    QStringList names;
    for (const QString &path : attachments)
        names.append(attachmentDisplayName(path));
    return "Archivos adjuntos: " + names.join(", ");
}

QString ChatView::attachmentKindLabel(const QString &path) const {
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QString fileName = info.fileName().toLower();
    if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "webp" || suffix == "gif" || suffix == "bmp")
        return "Imagen";
    if (suffix == "svg")
        return "SVG";
    if (suffix == "zip" || suffix == "7z" || suffix == "rar")
        return "Comprimido";
    if (suffix == "cpp" || suffix == "cc" || suffix == "cxx" || suffix == "c" || suffix == "h" || suffix == "hpp" || suffix == "hh")
        return "C/C++";
    if (suffix == "js" || suffix == "jsx" || suffix == "ts" || suffix == "tsx" || suffix == "mjs" || suffix == "cjs")
        return "JS/TS";
    if (suffix == "py" || suffix == "pyw")
        return "Python";
    if (suffix == "rs")
        return "Rust";
    if (suffix == "java" || suffix == "kt" || suffix == "kts")
        return "JVM";
    if (suffix == "qml" || suffix == "qss" || suffix == "pro" || suffix == "pri" || suffix == "qrc" || suffix == "ui")
        return "Qt";
    if (suffix == "cs")
        return "C#";
    if (suffix == "html" || suffix == "css" || suffix == "scss" || suffix == "sass" || suffix == "less" || suffix == "vue" || suffix == "svelte")
        return "Web";
    if (suffix == "json" || suffix == "yaml" || suffix == "yml" || suffix == "toml" || suffix == "xml" || suffix == "ini" || suffix == "env")
        return "Config";
    if (suffix == "md" || suffix == "txt" || suffix == "log")
        return "Texto";
    if (fileName == "dockerfile" || fileName == "makefile" || fileName == "cmakelists.txt")
        return "Build";
    if (fileName.startsWith(".env") || fileName == ".gitignore" || fileName == ".gitattributes" || fileName == ".editorconfig")
        return "Config";
    return suffix.isEmpty() ? "Archivo" : suffix.toUpper();
}

QWidget* ChatView::makeAttachmentChip(const QString &path, int index, bool removable) {
    auto *chip = new SolidPanel();
    chip->setFillColor(QColor(Style::BG_LILAC));
    chip->setCornerRadius(8);
    chip->setFullBorder(QColor(Style::INK), 2);
    chip->setHardShadow(QColor(Style::INK), 2, 2);
    chip->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);

    auto *layout = new QHBoxLayout(chip);
    layout->setContentsMargins(8, 4, removable ? 4 : 8, 5);
    layout->setSpacing(5);
    layout->addWidget(iconLabel(":/icons/icons/file-text.svg", QColor(Style::VIOLET), 13), 0, Qt::AlignVCenter);

    auto *label = new QLabel(attachmentKindLabel(path) + " · " + attachmentDisplayName(path));
    label->setStyleSheet(QString("font-size: 11px; font-weight: 800; color: %1; border: none; background: transparent;").arg(Style::INK));
    QFontMetrics fm(label->font());
    label->setText(fm.elidedText(label->text(), Qt::ElideMiddle, 170));
    layout->addWidget(label, 0, Qt::AlignVCenter);

    if (removable) {
        auto *removeBtn = new QPushButton();
        removeBtn->setFixedSize(20, 20);
        removeBtn->setCursor(Qt::PointingHandCursor);
        removeBtn->setToolTip("Quitar adjunto");
        removeBtn->setIcon(IconUtil::coloredIcon(":/icons/icons/x.svg", QColor(Style::INK), QSize(11, 11)));
        removeBtn->setIconSize(QSize(11, 11));
        removeBtn->setStyleSheet(
            "QPushButton { background: transparent; border: none; border-radius: 5px; }"
            "QPushButton:hover { background: #fee2e2; }"
        );
        connect(removeBtn, &QPushButton::clicked, this, [this, index]() {
            removePendingAttachment(index);
        });
        layout->addWidget(removeBtn, 0, Qt::AlignVCenter);
    }

    return chip;
}

QWidget* ChatView::makeAttachmentPreview(const QString &path) {
    if (!isPreviewImageFile(path)) {
        return makeAttachmentChip(path, -1, false);
    }

    QPixmap pix(path);
    if (pix.isNull()) {
        return makeAttachmentChip(path, -1, false);
    }

    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(10);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 2, 2);
    card->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(6, 6, 8, 8);
    layout->setSpacing(4);

    auto *preview = new QLabel();
    preview->setFixedSize(150, 94);
    preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet(QString("background: %1; border: 1px solid %2; border-radius: 7px;").arg(Style::BG_LILAC, Style::INK));
    preview->setPixmap(pix.scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(preview);

    auto *label = new QLabel(attachmentDisplayName(path));
    label->setStyleSheet(QString("font-size: 10px; font-weight: 800; color: %1; border: none; background: transparent;").arg(Style::TEXT_MUTED));
    QFontMetrics fm(label->font());
    label->setText(fm.elidedText(label->text(), Qt::ElideMiddle, 145));
    layout->addWidget(label);

    return card;
}

void ChatView::refreshAttachmentChips() {
    if (!m_attachmentChipsLayout || !m_attachmentChipsWidget) return;
    while (QLayoutItem *item = m_attachmentChipsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    if (m_pendingAttachments.isEmpty()) {
        m_attachmentChipsWidget->setVisible(false);
        return;
    }
    for (int i = 0; i < m_pendingAttachments.size(); ++i) {
        m_attachmentChipsLayout->addWidget(makeAttachmentChip(m_pendingAttachments[i], i, true));
    }
    m_attachmentChipsLayout->addStretch();
    m_attachmentChipsWidget->setVisible(true);
}

bool ChatView::isCompatibleDropFile(const QString &path) const {
    if (path.isEmpty()) return false;
    QFileInfo info(path);
    QString suffix = info.suffix().toLower();
    const QString fileName = info.fileName().toLower();
    static const QStringList allowed = {
        "png", "jpg", "jpeg", "webp", "gif", "bmp", "svg",
        "zip", "7z", "rar",
        "txt", "md", "markdown", "rst", "log", "csv", "tsv",
        "json", "jsonc", "json5", "yaml", "yml", "toml", "xml", "svg", "ini", "cfg", "conf", "properties", "env", "editorconfig",
        "c", "h", "cpp", "cc", "cxx", "hpp", "hh", "hxx", "ino",
        "cs", "java", "kt", "kts", "scala", "swift", "m", "mm",
        "py", "pyw", "ipynb", "rb", "php", "go", "rs", "dart", "lua", "pl", "pm", "r",
        "js", "jsx", "ts", "tsx", "mjs", "cjs", "vue", "svelte",
        "html", "htm", "css", "scss", "sass", "less",
        "sql", "graphql", "gql", "proto",
        "sh", "bash", "zsh", "fish", "ps1", "bat", "cmd",
        "cmake", "gradle", "pro", "pri", "qrc", "ui", "qml", "qss", "qbs", "bzl", "bazel",
        "dockerfile", "gitignore", "gitattributes", "npmrc", "yarnrc",
        "lock", "mod", "sum",
        "pdf", "docx", "xlsx", "pptx"
    };
    if (!suffix.isEmpty() && allowed.contains(suffix))
        return true;

    static const QStringList knownNames = {
        "dockerfile", "makefile", "cmakelists.txt", "package.json", "package-lock.json",
        "pnpm-lock.yaml", "yarn.lock", "cargo.toml", "cargo.lock", "go.mod", "go.sum",
        "composer.json", "composer.lock", "gemfile", "gemfile.lock", "build.gradle", "settings.gradle",
        "pom.xml", "mix.exs", "deno.json", "tsconfig.json", "vite.config.ts", "vite.config.js",
        "webpack.config.js", "rollup.config.js", "eslint.config.js", "prettier.config.js",
        "tailwind.config.js", "tailwind.config.ts", "next.config.js", "next.config.ts",
        "svelte.config.js", "astro.config.mjs", "nuxt.config.ts", "app.config.ts",
        "build", "workspace", "build.bazel", "workspace.bazel",
        "requirements.txt", "pyproject.toml", "setup.py", "setup.cfg",
        ".gitignore", ".gitattributes", ".env", ".env.local", ".env.example",
        ".editorconfig", ".prettierrc", ".eslintrc", ".babelrc", ".npmrc"
    };
    return knownNames.contains(fileName);
}

void ChatView::setDropTargetActive(bool active) {
    if (!m_inputBar) return;
    if (m_dropHintLabel)
        m_dropHintLabel->setVisible(active);
    if (active) {
        m_inputBar->setStyleSheet(QString(
            "background: %1; border-top: 2px solid %2; border-left: 3px dashed %3;"
            "border-right: 3px dashed %3; border-bottom: 3px dashed %3;"
        ).arg(Style::VIOLET_LIGHT, Style::INK, Style::VIOLET));
    } else {
        m_inputBar->setStyleSheet(QString("background: %1; border-top: 2px solid %2;").arg(Style::WHITE, Style::INK));
    }
}

void ChatView::removePendingAttachment(int index) {
    if (index < 0 || index >= m_pendingAttachments.size()) return;
    m_pendingAttachments.removeAt(index);
    refreshAttachmentChips();
}

QWidget* ChatView::makeAvatar(bool assistant) {
    auto *avatar = new SolidPanel();
    avatar->setFillColor(QColor(assistant ? Style::VIOLET : Style::WHITE));
    avatar->setCornerRadius(9);
    avatar->setFullBorder(QColor(Style::INK), 2);
    avatar->setHardShadow(QColor(Style::INK), 2, 2);
    avatar->setFixedSize(30, 30);
    auto *layout = new QVBoxLayout(avatar);
    layout->setContentsMargins(0, 0, 2, 2);
    layout->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel(assistant ? ":/icons/icons/bot.svg" : ":/icons/icons/user.svg",
                                assistant ? QColor(Style::WHITE) : QColor(Style::TEXT_MUTED), 15),
                      0, Qt::AlignCenter);
    return avatar;
}

QWidget* ChatView::makeBubble(const QString &sizingText, const QString &visibleText, bool user, QLabel **contentLabel, const QString &copyText) {
    auto *bubble = new SolidPanel();
    if (user) {
        bubble->setFillColor(QColor(Style::VIOLET));
        bubble->setFullBorder(QColor(Style::INK), 2);
        bubble->setHardShadow(QColor(Style::INK), 3, 3);
    } else {
        bubble->setFillColor(QColor(Style::WHITE));
        bubble->setFullBorder(QColor(Style::INK), 2);
        bubble->setHardShadow(QColor(Style::INK), 3, 3);
    }
    bubble->setCornerRadius(11);
    bubble->setMaximumWidth(user ? 430 : 560);
    const int textLen = static_cast<int>(sizingText.size());
    const int naturalWidth = qBound(user ? 120 : 220, textLen * 6 + 38, user ? 400 : 520);
    bubble->setMinimumWidth(naturalWidth);
    bubble->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(bubble);
    layout->setContentsMargins(0, 0, 3, 3);
    auto *inner = new QWidget(bubble);
    inner->setAttribute(Qt::WA_StyledBackground, true);
    inner->setStyleSheet("background: transparent; border: none;");
    auto *innerLayout = new QVBoxLayout(inner);
    innerLayout->setContentsMargins(12, 7, 14, 8);
    auto *lbl = textLabel(visibleText, 13, user ? Style::WHITE : Style::INK, 600);
    if (contentLabel) *contentLabel = lbl;
    innerLayout->addWidget(lbl);

    if (!user) {
        auto *copyRow = new QHBoxLayout();
        copyRow->addStretch();
        auto *copyBtn = new QPushButton();
        copyBtn->setFixedSize(22, 22);
        copyBtn->setCursor(Qt::PointingHandCursor);
        copyBtn->setIcon(QIcon(":/icons/icons/copy.svg"));
        copyBtn->setIconSize(QSize(14, 14));
        copyBtn->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: #f0e6ff; border-radius: 4px; }");
        copyBtn->setToolTip("Copiar mensaje");
        QString textToCopy = copyText.isEmpty() ? visibleText : copyText;
        connect(copyBtn, &QPushButton::clicked, this, [copyBtn, textToCopy]() {
            QApplication::clipboard()->setText(textToCopy);
            copyBtn->setIcon(QIcon(":/icons/icons/check-circle.svg"));
            QTimer::singleShot(1500, copyBtn, [copyBtn]() {
                copyBtn->setIcon(QIcon(":/icons/icons/copy.svg"));
            });
        });
        copyRow->addWidget(copyBtn);
        copyRow->setContentsMargins(0, 0, 0, 0);
        innerLayout->addLayout(copyRow);
    }

    layout->addWidget(inner);
    return bubble;
}

QWidget* ChatView::makeWelcomeState() {
    auto *wrap = new QWidget();
    wrap->setMinimumHeight(360);
    auto *layout = new QVBoxLayout(wrap);
    layout->setContentsMargins(40, 24, 40, 24);
    layout->setSpacing(16);
    layout->setAlignment(Qt::AlignCenter);

    // Logo
    auto *logo = new SolidPanel(wrap);
    logo->setFillColor(QColor(Style::VIOLET));
    logo->setCornerRadius(16);
    logo->setFullBorder(QColor(Style::INK), 2);
    logo->setHardShadow(QColor(Style::INK), 3, 3);
    logo->setFixedSize(56, 56);
    auto *logoLayout = new QVBoxLayout(logo);
    logoLayout->setContentsMargins(0, 0, 3, 3);
    logoLayout->setAlignment(Qt::AlignCenter);
    logoLayout->addWidget(iconLabel(":/icons/icons/bot.svg", QColor(Style::WHITE), 26), 0, Qt::AlignCenter);
    layout->addWidget(logo, 0, Qt::AlignHCenter);

    // Title
    auto *title = textLabel("¿En qué trabajamos?", 20, Style::INK, 900);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    // Description
    auto *desc = textLabel("Pedile a Voryel que te ayude con una idea, error o explicación.", 13, Style::TEXT_MUTED, 600);
    desc->setAlignment(Qt::AlignCenter);
    desc->setMaximumWidth(400);
    layout->addWidget(desc);

    layout->addSpacing(6);

    // Suggestion chips
    auto *chips = new QHBoxLayout();
    chips->setSpacing(8);
    chips->setAlignment(Qt::AlignCenter);
    struct Starter { QString label; };
    const QVector<Starter> starters = {
        { "Arreglar bug" },
        { "Mejorar UI" },
        { "Explicar código" },
    };
    for (const auto &s : starters) {
        auto *chip = new QPushButton(s.label);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setStyleSheet(QString(
            "QPushButton {"
            "  background: %1; color: %2; border: 1px solid %3; border-radius: 8px;"
            "  padding: 6px 14px; font-size: 12px; font-weight: 700;"
            "}"
            "QPushButton:hover { background: %4; border-color: %5; }"
        ).arg(Style::WHITE, Style::INK, Style::BORDER_SOFT, Style::BG_LILAC, Style::VIOLET));
        connect(chip, &QPushButton::clicked, this, [this, s = s.label]() {
            m_input->setText(s);
            m_input->setFocus();
        });
        chips->addWidget(chip);
    }
    layout->addLayout(chips);

    return wrap;
}

void ChatView::addPlanCard() {
    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(13);
    card->setFullBorder(QColor(Style::VIOLET), 2);
    card->setHardShadow(QColor(Style::VIOLET).darker(150), 3, 3);
    card->setMaximumWidth(640);
    card->setMinimumWidth(500);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 3, 3);
    layout->setSpacing(0);

    auto *head = new QWidget(card);
    head->setAttribute(Qt::WA_StyledBackground, true);
    head->setStyleSheet(QString("background: %1; border-top-left-radius: 13px; border-top-right-radius: 13px;").arg(Style::VIOLET));
    head->setMinimumHeight(36);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(12, 9, 12, 9);
    headLayout->setSpacing(8);
    headLayout->addWidget(iconLabel(":/icons/icons/bot.svg", QColor(Style::WHITE), 13));
    auto *headText = new QLabel("Plan de Voryel");
    headText->setStyleSheet("font-size: 13px; font-weight: 900; color: #ffffff; border: none; background: transparent;");
    headLayout->addWidget(headText);
    headLayout->addStretch();
    headLayout->addWidget(iconLabel(":/icons/icons/chevron-up.svg", QColor("#ddd6fe"), 13));
    layout->addWidget(head);

    auto *body = new QWidget(card);
    body->setStyleSheet("background: transparent; border: none;");
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 10, 15, 12);
    bodyLayout->setSpacing(6);

    struct Step { QString text; QString file; };
    const QVector<Step> steps = {
        { "Revisar main.js para identificar el error", "main.js" },
        { "Revisar preload.js", "preload.js" },
        { "Modificar index.html con los cambios necesarios", "index.html" },
        { "Ejecutar npm run build para verificar", "" },
    };
    for (int i = 0; i < steps.size(); ++i) {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        auto *num = textLabel(QString::number(i + 1) + ".", 12, Style::VIOLET, 900);
        num->setFixedWidth(22);
        row->addWidget(num, 0, Qt::AlignTop);
        auto *col = new QVBoxLayout();
        col->setSpacing(2);
        col->addWidget(textLabel(steps[i].text, 13, Style::INK, 700));
        if (!steps[i].file.isEmpty()) {
            auto *file = textLabel("▸ " + steps[i].file, 11, Style::TEXT_MUTED, 700);
            file->setStyleSheet(QString("font-size: 11px; color: %1; font-family: 'Consolas','Courier New',monospace; border: none;").arg(Style::TEXT_MUTED));
            col->addWidget(file);
        }
        row->addLayout(col, 1);
        bodyLayout->addLayout(row);
    }

    auto *files = textLabel("Archivos que podría tocar:   main.js   preload.js   index.html", 11, Style::TEXT_MUTED, 800);
    files->setStyleSheet(QString("font-size: 11px; color: %1; background: %2; border-top: 2px solid %2; padding-top: 8px;")
                             .arg(Style::TEXT_MUTED, Style::BG_LILAC));
    bodyLayout->addWidget(files);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    auto *continueBtn = makePrimaryButton("▶  Continuar");
    connect(continueBtn, &QPushButton::clicked, this, &ChatView::continuePlan);
    buttons->addWidget(continueBtn);
    buttons->addWidget(makeGhostButton("Editar plan"));
    auto *cancelBtn = makeDangerButton("Cancelar");
    connect(cancelBtn, &QPushButton::clicked, this, &ChatView::cancelPlan);
    buttons->addWidget(cancelBtn);
    buttons->addStretch();
    bodyLayout->addLayout(buttons);

    layout->addWidget(body);

    auto *outer = new QWidget();
    outer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *outerLayout = new QHBoxLayout(outer);
    outerLayout->setContentsMargins(39, 0, 0, 0);
    outerLayout->setSpacing(9);
    // Alineado con el contenido de Voryel/IA del lado izquierdo, dejando
    // el espacio del avatar para que la card pertenezca al mismo flujo.
    outerLayout->addWidget(card, 0, Qt::AlignTop | Qt::AlignLeft);
    outerLayout->addStretch(1);
    m_messagesLayout->addWidget(outer, 0, Qt::AlignTop);
    fadeIn(outer);
    scrollToBottom();
}

void ChatView::continuePlan() {
    addAssistantMessageAnimated(
        "Necesito ejecutar un comando para verificar que los cambios compilen correctamente.",
        [this]() {
            addTerminalPermissionCard("npm run build");
            emit taskStateChanged("Esperando permiso");
        }
    );
}

void ChatView::allowTerminal() {
    setWorking(true, "Ejecutando comando");
    QTimer::singleShot(800, this, [this]() {
        setWorking(false);
        addAssistantMessageAnimated(
            "Comando ejecutado. Encontré los cambios necesarios. Revisá el diff antes de aplicar:",
            [this]() {
                addDiffCard();
                emit taskStateChanged("Diff listo");
            }
        );
    });
}

void ChatView::cancelPlan() {
    addAssistantMessage("Cancelado. ¿Querés ajustar el plan o pedir otra cosa?");
    emit taskStateChanged("Cancelado");
}

void ChatView::addTerminalPermissionCard(const QString &command) {
    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(13);
    card->setFullBorder(QColor("#d97706"), 2);
    card->setHardShadow(QColor("#d97706").darker(150), 3, 3);
    card->setMaximumWidth(640);
    card->setMinimumWidth(500);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 3, 3);
    layout->setSpacing(0);

    auto *head = new QWidget(card);
    head->setAttribute(Qt::WA_StyledBackground, true);
    head->setStyleSheet(QString("background: %1; border-top-left-radius: 13px; border-top-right-radius: 13px;").arg("#d97706"));
    head->setMinimumHeight(36);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(12, 9, 12, 9);
    headLayout->setSpacing(8);
    headLayout->addWidget(iconLabel(":/icons/icons/terminal.svg", QColor(Style::WHITE), 13));
    auto *headText = new QLabel("Voryel quiere ejecutar:");
    headText->setStyleSheet("font-size: 13px; font-weight: 900; color: #ffffff; border: none; background: transparent;");
    headLayout->addWidget(headText);
    layout->addWidget(head);

    auto *body = new QWidget(card);
    body->setAttribute(Qt::WA_TranslucentBackground, true);
    body->setStyleSheet("background: transparent; border: none;");
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 10, 15, 12);
    bodyLayout->setSpacing(7);
    auto *cmdBox = new SolidPanel(body);
    cmdBox->setFillColor(QColor(Style::INK));
    cmdBox->setCornerRadius(8);
    cmdBox->setFullBorder(QColor(Style::INK), 2);
    auto *cmdLayout = new QVBoxLayout(cmdBox);
    cmdLayout->setContentsMargins(12, 9, 12, 9);
    auto *cmd = textLabel("$ " + command, 13, "#fde68a", 800);
    cmd->setStyleSheet("font-size: 13px; font-weight: 800; color: #fde68a; font-family: 'Consolas','Courier New',monospace; border: none; background: transparent;");
    cmdLayout->addWidget(cmd);
    bodyLayout->addWidget(cmdBox);
    bodyLayout->addWidget(textLabel("Motivo: Verificar que los cambios compilen correctamente.", 11, Style::TEXT_MUTED, 600));
    auto *buttons = new QHBoxLayout();
    auto *allow = makeGhostButton("Permitir una vez");
    allow->setStyleSheet("QPushButton { background: #fef9c3; color: #92400e; border: 2px solid #d97706; border-radius: 8px; padding: 0 11px; font-size: 12px; font-weight: 800; } QPushButton:hover { background: #fde68a; }");
    connect(allow, &QPushButton::clicked, this, &ChatView::allowTerminal);
    buttons->addWidget(allow);
    buttons->addWidget(makeGhostButton("Permitir siempre aquí"));
    auto *deny = makeDangerButton("Cancelar");
    connect(deny, &QPushButton::clicked, this, &ChatView::cancelPlan);
    buttons->addWidget(deny);
    buttons->addStretch();
    bodyLayout->addLayout(buttons);
    layout->addWidget(body);

    auto *outer = new QWidget();
    outer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *outerLayout = new QHBoxLayout(outer);
    outerLayout->setContentsMargins(39, 0, 0, 0);
    outerLayout->setSpacing(9);
    // Alineado con el contenido de Voryel/IA del lado izquierdo, dejando
    // el espacio del avatar para que la card pertenezca al mismo flujo.
    outerLayout->addWidget(card, 0, Qt::AlignTop | Qt::AlignLeft);
    outerLayout->addStretch(1);
    m_messagesLayout->addWidget(outer, 0, Qt::AlignTop);
    fadeIn(outer);
    scrollToBottom();
}

void ChatView::addDiffCard() {
    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(13);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 3, 3);
    card->setMaximumWidth(640);
    card->setMinimumWidth(500);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 3, 3);
    layout->setSpacing(0);

    auto *head = new SolidPanel(card);
    head->setFillColor(QColor(Style::BG_LILAC));
    head->setBottomBorder(QColor(Style::INK), 2);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(12, 9, 12, 9);
    headLayout->setSpacing(8);
    headLayout->addWidget(iconLabel(":/icons/icons/git-compare.svg", QColor(Style::VIOLET), 13));
    auto *file = textLabel("styles.css", 12, Style::VIOLET, 900);
    file->setStyleSheet(QString("font-size: 12px; color: %1; font-family: 'Consolas','Courier New',monospace; font-weight: 900; border: none;").arg(Style::VIOLET));
    headLayout->addWidget(file);
    headLayout->addStretch();
    layout->addWidget(head);

    auto makeLine = [&](const QString &text, const QString &bg, const QString &fg) {
        auto *line = new QLabel(text);
        line->setStyleSheet(QString("font-size: 12px; color: %1; background: %2; padding: 5px 12px; font-family: 'Consolas','Courier New',monospace; border: none;").arg(fg, bg));
        return line;
    };
    layout->addWidget(makeLine("-   color: green;", "#fee2e2", "#991b1b"));
    layout->addWidget(makeLine("+   color: #8A5CF6;", "#d1fae5", "#065f46"));
    layout->addWidget(makeLine("-   overflow-x: scroll;", "#fee2e2", "#991b1b"));
    layout->addWidget(makeLine("+   overflow-x: hidden;", "#d1fae5", "#065f46"));

    auto *footer = new SolidPanel(card);
    footer->setFillColor(QColor(Style::BG_LILAC));
    footer->setTopBorder(QColor(Style::INK), 2);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(10, 8, 10, 8);
    footerLayout->setSpacing(8);
    auto *approve = makeGhostButton("Aprobar cambio");
    approve->setStyleSheet("QPushButton { background: #d1fae5; color: #065f46; border: 2px solid #065f46; border-radius: 8px; padding: 0 11px; font-size: 12px; font-weight: 800; } QPushButton:hover { background: #a7f3d0; }");
    footerLayout->addWidget(approve);
    auto *reject = makeDangerButton("Rechazar");
    footerLayout->addWidget(reject);
    footerLayout->addWidget(makeGhostButton("Aprobar todo"));
    footerLayout->addStretch();
    layout->addWidget(footer);

    auto *outer = new QWidget();
    outer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *outerLayout = new QHBoxLayout(outer);
    outerLayout->setContentsMargins(39, 0, 0, 0);
    outerLayout->setSpacing(9);
    // Alineado con el contenido de Voryel/IA del lado izquierdo, dejando
    // el espacio del avatar para que la card pertenezca al mismo flujo.
    outerLayout->addWidget(card, 0, Qt::AlignTop | Qt::AlignLeft);
    outerLayout->addStretch(1);
    m_messagesLayout->addWidget(outer, 0, Qt::AlignTop);
    fadeIn(outer);
    scrollToBottom();
}

QPushButton* ChatView::makePrimaryButton(const QString &text) {
    auto *btn = new QPushButton(text);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(34);
    btn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: 2px solid %2; border-radius: 9px; padding: 0 14px; font-size: 12px; font-weight: 900; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:disabled { background: %4; }"
    ).arg(Style::VIOLET, Style::VIOLET_DARK, Style::VIOLET_DARK, Style::TEXT_FAINT));
    return btn;
}

QPushButton* ChatView::makeGhostButton(const QString &text) {
    auto *btn = new QPushButton(text);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(34);
    btn->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: 2px solid %3; border-radius: 9px; padding: 0 14px; font-size: 12px; font-weight: 800; }"
        "QPushButton:hover { background: %4; color: %5; border-color: %5; }"
    ).arg(Style::WHITE, Style::INK, Style::BORDER_SOFT, Style::BG_LILAC, Style::VIOLET));
    return btn;
}

QPushButton* ChatView::makeDangerButton(const QString &text) {
    auto *btn = new QPushButton(text);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(34);
    btn->setStyleSheet(
        "QPushButton { background: #fee2e2; color: #991b1b; border: 2px solid #fca5a5; border-radius: 9px; padding: 0 14px; font-size: 12px; font-weight: 800; }"
        "QPushButton:hover { background: #fecaca; }"
    );
    return btn;
}

// ── Chat list: "Mis Chats" dropdown ──

void ChatView::setChatStore(ChatStore *store) {
    m_chatStore = store;
    if (!m_chatStore) return;
    connect(m_chatStore, &ChatStore::chatSwitched, this, &ChatView::loadChatMessages);
    connect(m_chatStore, &ChatStore::messagesChanged, this, [this]() { /* popup rebuilt on open */ });
    loadChatMessages(m_chatStore->activeChatIndex());
}

void ChatView::showChatPopup() {
    closeChatPopup();
    if (!m_chatMenuButton || !m_chatStore) return;

    auto *popup = new QWidget(this);
    popup->setAttribute(Qt::WA_StyledBackground, true);
    popup->setStyleSheet("background: transparent;");

    auto *outerLayout = new QVBoxLayout(popup);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto *card = new SolidPanel(popup);
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(10);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 4, 4);
    card->setMinimumWidth(240);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(4, 4, 4, 4);
    cardLayout->setSpacing(2);

    const auto &chats = m_chatStore->chats();
    int activeIdx = m_chatStore->activeChatIndex();

    if (chats.isEmpty()) {
        auto *emptyLabel = new QLabel("No tienes Chats");
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet(QString("font-size: 13px; font-weight: 700; color: %1; padding: 20px 16px; border: none; background: transparent;").arg(Style::TEXT_MUTED));
        cardLayout->addWidget(emptyLabel);

        auto *newBtn = new QPushButton("+  Crear nuevo chat");
        newBtn->setCursor(Qt::PointingHandCursor);
        newBtn->setMinimumHeight(36);
        newBtn->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; border: 2px solid %2; border-radius: 8px; padding: 0 14px; font-size: 12px; font-weight: 900; }"
            "QPushButton:hover { background: %2; color: white; }"
        ).arg(Style::WHITE, Style::VIOLET));
        connect(newBtn, &QPushButton::clicked, this, [this]() { closeChatPopup(); onNewChatClicked(); });
        cardLayout->addWidget(newBtn);
    } else {
        for (int i = 0; i < chats.size(); ++i) {
            const auto &chat = chats[i];
            bool active = (i == activeIdx);

            if (i == m_pendingDeleteChatIndex) {
                auto *confirm = new QWidget();
                confirm->setStyleSheet(QString(
                    "QWidget { background: %1; border: 1px solid %2; border-radius: 6px; }"
                ).arg(Style::BG_LILAC, Style::BORDER_SOFT));

                auto *confirmLayout = new QVBoxLayout(confirm);
                confirmLayout->setContentsMargins(10, 8, 10, 8);
                confirmLayout->setSpacing(8);

                auto *confirmLabel = new QLabel(QString("Eliminar \"%1\"?").arg(chat.title));
                confirmLabel->setWordWrap(true);
                confirmLabel->setStyleSheet("font-size: 12px; font-weight: 700; color: #1a1a1a; border: none; background: transparent;");
                confirmLayout->addWidget(confirmLabel);

                auto *btnRow = new QHBoxLayout();
                btnRow->setSpacing(6);

                auto *cancelBtn = new QPushButton("Cancelar");
                cancelBtn->setCursor(Qt::PointingHandCursor);
                cancelBtn->setMinimumHeight(32);
                cancelBtn->setStyleSheet(
                    "QPushButton { background: white; color: #1a1a1a; border: 2px solid #1a1a1a; border-radius: 6px; padding: 0 12px; font-size: 11px; font-weight: 800; }"
                    "QPushButton:hover { background: #ede8f7; }"
                );
                int chatIndex = i;
                connect(cancelBtn, &QPushButton::clicked, this, [this]() {
                    m_pendingDeleteChatIndex = -1;
                    m_pendingDeleteChatTitle.clear();
                    showChatPopup();
                });
                btnRow->addWidget(cancelBtn);

                auto *deleteBtn = new QPushButton("Eliminar");
                deleteBtn->setCursor(Qt::PointingHandCursor);
                deleteBtn->setMinimumHeight(32);
                deleteBtn->setStyleSheet(
                    "QPushButton { background: #dc2626; color: white; border: 2px solid #b91c1b; border-radius: 6px; padding: 0 12px; font-size: 11px; font-weight: 900; }"
                    "QPushButton:hover { background: #991b1b; }"
                );
                connect(deleteBtn, &QPushButton::clicked, this, [this, chatIndex]() {
                    m_chatStore->deleteChat(chatIndex);
                    m_pendingDeleteChatIndex = -1;
                    m_pendingDeleteChatTitle.clear();
                    closeChatPopup();
                });
                btnRow->addWidget(deleteBtn);
                btnRow->addStretch();

                confirmLayout->addLayout(btnRow);
                cardLayout->addWidget(confirm);
            } else {
                auto *item = new SolidPanel();
                item->setCursor(Qt::PointingHandCursor);
                item->setProperty("chatIndex", i);
                item->setMinimumHeight(44);

                if (active) {
                    item->setFillColor(QColor(Style::VIOLET_LIGHT));
                    item->setFullBorder(QColor(Style::VIOLET), 2);
                    item->setHardShadow(QColor(Style::VIOLET), 2, 2);
                } else {
                    item->setFillColor(QColor(Style::WHITE));
                    item->setFullBorder(QColor(Style::INK), 2);
                    item->setHardShadow(QColor(Style::INK), 2, 2);
                }
                item->setCornerRadius(8);

                auto *itemRow = new QHBoxLayout(item);
                itemRow->setContentsMargins(12, 8, 8, 8);
                itemRow->setSpacing(8);

                auto *textCol = new QVBoxLayout();
                textCol->setSpacing(1);
                auto *title = new QLabel(chat.title);
                title->setStyleSheet(QString("font-size: 12px; font-weight: 700; color: %1; border: none; background: transparent;").arg(active ? Style::VIOLET : Style::INK));
                textCol->addWidget(title);
                if (!chat.lastSnippet.isEmpty()) {
                    auto *snippet = new QLabel(chat.lastSnippet);
                    snippet->setStyleSheet(QString("font-size: 10px; color: %1; border: none; background: transparent;").arg(Style::TEXT_MUTED));
                    snippet->setMaximumHeight(14);
                    QFontMetrics fm(snippet->font());
                    snippet->setText(fm.elidedText(chat.lastSnippet, Qt::ElideRight, 160));
                    textCol->addWidget(snippet);
                }
                itemRow->addLayout(textCol, 1);

                auto *delBtn = new QPushButton();
                delBtn->setFixedSize(24, 24);
                delBtn->setCursor(Qt::PointingHandCursor);
                delBtn->setToolTip("Eliminar chat");
                delBtn->setIcon(IconUtil::coloredIcon(":/icons/icons/trash.svg", QColor("#dc2626"), QSize(14, 14)));
                delBtn->setIconSize(QSize(14, 14));
                delBtn->setStyleSheet(
                    "QPushButton { border: none; background: transparent; border-radius: 4px; }"
                    "QPushButton:hover { background: #fee2e2; }"
                );
                int chatIndex = i;
                connect(delBtn, &QPushButton::clicked, this, [this, chatIndex, title = chat.title]() {
                    m_pendingDeleteChatIndex = chatIndex;
                    m_pendingDeleteChatTitle = title;
                    showChatPopup();
                });
                itemRow->addWidget(delBtn);

                item->installEventFilter(this);
                cardLayout->addWidget(item);
            }
        }

        auto *sep = new QFrame();
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet(QString("background: %1; max-height: 1px; border: none; margin: 4px 0;").arg(Style::BORDER_SOFT));
        cardLayout->addWidget(sep);

        auto *newBtn = new QPushButton("+  Crear nuevo chat");
        newBtn->setCursor(Qt::PointingHandCursor);
        newBtn->setMinimumHeight(36);
        newBtn->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; border: 2px solid %2; border-radius: 8px; padding: 0 14px; font-size: 12px; font-weight: 900; }"
            "QPushButton:hover { background: %2; color: white; }"
        ).arg(Style::WHITE, Style::VIOLET));
        connect(newBtn, &QPushButton::clicked, this, [this]() { closeChatPopup(); onNewChatClicked(); });
        cardLayout->addWidget(newBtn);
    }

    outerLayout->addWidget(card);

    popup->adjustSize();
    QPoint btnTL = m_chatMenuButton->mapTo(this, QPoint(0, 0));
    int popupX = btnTL.x() - popup->width() - 8;
    if (popupX < 0) popupX = 8;
    popup->move(popupX, btnTL.y() + 4);
    popup->show();
    popup->raise();

    m_chatPopup = popup;
}

void ChatView::closeChatPopup() {
    if (m_chatPopup) {
        m_chatPopup->deleteLater();
        m_chatPopup = nullptr;
    }
}

void ChatView::mousePressEvent(QMouseEvent *event) {
    if (m_chatPopup) {
        QRect popupRect(m_chatPopup->pos(), m_chatPopup->size());
        QRect btnRect(m_chatMenuButton->mapTo(this, QPoint(0, 0)), m_chatMenuButton->size());
        if (!popupRect.contains(event->pos()) && !btnRect.contains(event->pos())) {
            m_pendingDeleteChatIndex = -1;
            m_pendingDeleteChatTitle.clear();
            closeChatPopup();
        }
    }
    QWidget::mousePressEvent(event);
}

void ChatView::rebuildChatList() {
}

bool ChatView::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto *widget = qobject_cast<QWidget*>(obj);
        if (widget && widget->property("chatIndex").isValid()) {
            int idx = widget->property("chatIndex").toInt();
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                closeChatPopup();
                onChatSelected(idx);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ChatView::onChatSelected(int index) {
    if (!m_chatStore) return;
    if (m_working) {
        QDialog dlg(this);
        dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        dlg.setAttribute(Qt::WA_StyledBackground, true);

        auto *root = new QVBoxLayout(&dlg);
        root->setContentsMargins(3, 3, 6, 6);
        root->setSpacing(0);

        auto *card = new QWidget(&dlg);
        card->setStyleSheet(
            "background: #ffffff;"
            "border: 3px solid #1a1a1a;"
            "border-radius: 10px;"
        );

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(0);

        auto *head = new QWidget(card);
        head->setStyleSheet(QString("background: %1; border-top-left-radius: 7px; border-top-right-radius: 7px;").arg(Style::VIOLET));
        head->setFixedHeight(42);
        auto *headLayout = new QHBoxLayout(head);
        headLayout->setContentsMargins(16, 0, 16, 0);
        auto *headLabel = new QLabel("Chat activo");
        headLabel->setStyleSheet("font-size: 14px; font-weight: 900; color: white; border: none; background: transparent;");
        headLayout->addWidget(headLabel);
        headLayout->addStretch();
        cardLayout->addWidget(head);

        auto *body = new QWidget(card);
        body->setStyleSheet("background: white;");
        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(16, 16, 16, 16);
        bodyLayout->setSpacing(14);

        auto *msg = new QLabel("Esperá a que termine la respuesta actual antes de cambiar de chat.");
        msg->setWordWrap(true);
        msg->setStyleSheet("font-size: 13px; font-weight: 700; color: #1a1a1a; border: none; background: transparent;");
        bodyLayout->addWidget(msg);

        auto *okBtn = new QPushButton("Entendido");
        okBtn->setCursor(Qt::PointingHandCursor);
        okBtn->setFixedHeight(36);
        okBtn->setStyleSheet(QString(
            "QPushButton { background: %1; color: white; border: 3px solid %2; border-radius: 8px; padding: 0 14px; font-size: 12px; font-weight: 900; }"
            "QPushButton:hover { background: %3; }"
        ).arg(Style::VIOLET, Style::VIOLET_DARK, Style::VIOLET_DARK));
        connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        bodyLayout->addWidget(okBtn, 0, Qt::AlignHCenter);
        cardLayout->addWidget(body);

        root->addWidget(card);

        dlg.setModal(true);
        dlg.setFixedSize(330, 190);

        QWidget *parentWin = this->window();
        if (parentWin) {
            QPoint center = parentWin->mapToGlobal(QPoint(parentWin->width() / 2, parentWin->height() / 2));
            dlg.move(center.x() - 165, center.y() - 95);
        }

        dlg.exec();
        return;
    }
    m_chatStore->switchToChat(index);
}

void ChatView::onNewChatClicked() {
    if (!m_chatStore) return;
    if (m_working) return;
    m_chatStore->createChat();
}

void ChatView::clearMessages() {
    if (!m_messagesLayout) return;
    QLayoutItem *item;
    while ((item = m_messagesLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_messageCount = 0;
    m_emptyState = makeWelcomeState();
    m_messagesLayout->addStretch(1);
    m_messagesLayout->addWidget(m_emptyState, 0, Qt::AlignCenter);
    m_messagesLayout->addStretch(1);
}

void ChatView::updateContextMeter(int currentTokens, int maxTokens,
                                   const QString &provider, const QString &model,
                                   const QString &accuracy,
                                   bool compactionPending)
{
    if (m_contextMeter)
        m_contextMeter->setValue(currentTokens, maxTokens, provider, model, accuracy, compactionPending);
}

void ChatView::loadChatMessages(int chatIndex) {
    if (!m_chatStore) return;
    if (m_working) return;
    const auto &chats = m_chatStore->chats();
    if (chatIndex < 0 || chatIndex >= chats.size()) return;

    clearMessages();

    const auto &msgs = chats[chatIndex].messages;
    for (const auto &msg : msgs) {
        if (msg.role == "user") {
            addUserMessage(msg.content, msg.attachments);
        } else if (msg.role == "assistant") {
            addAssistantMessageInstant(msg.content);
        }
    }

    rebuildChatList();
}

void ChatView::dragEnterEvent(QDragEnterEvent *event) {
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile() && isCompatibleDropFile(url.toLocalFile())) {
            setDropTargetActive(true);
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void ChatView::dragMoveEvent(QDragMoveEvent *event) {
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void ChatView::dragLeaveEvent(QDragLeaveEvent *event) {
    setDropTargetActive(false);
    event->accept();
}

void ChatView::dropEvent(QDropEvent *event) {
    setDropTargetActive(false);
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    bool added = false;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        if (!isCompatibleDropFile(path)) continue;
        if (!m_pendingAttachments.contains(path)) {
            m_pendingAttachments.append(path);
            added = true;
        }
    }
    if (added) {
        refreshAttachmentChips();
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}



void ChatView::scrollToBottom() {
    auto doScroll = [this]() {
        if (m_scroll && m_scroll->verticalScrollBar()) {
            m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
        }
    };
    QTimer::singleShot(0, this, doScroll);
    QTimer::singleShot(35, this, doScroll);
}

// ── Streaming API (real mode) ──
// Tokens se acumulan en m_streamBuffer sin mostrar nada en la UI.
// Solo se ve el typing indicator (tres puntitos + avatar).
// Cuando el stream termina, finishStreaming() muestra la respuesta
// completa con la animación de escritura/letra por letra.

void ChatView::beginStreaming() {
    qDebug() << "ChatView::beginStreaming: clearing buffer, previous length =" << m_streamBuffer.length();
    m_streamBuffer.clear();
    if (!m_working)
        applyWorkingUi(true);
}

void ChatView::appendStreamToken(const QString &token) {
    m_streamBuffer += token;
}

void ChatView::finishStreaming(const QString &fullResponse) {
    qDebug() << "ChatView::finishStreaming ENTER: fullResponse length =" << fullResponse.length()
             << "m_streamBuffer length =" << m_streamBuffer.length()
             << "m_working =" << m_working;
    const QString &text = fullResponse.isEmpty() ? m_streamBuffer : fullResponse;
    m_streamBuffer.clear();

    if (m_working) {
        applyWorkingUi(false);
    }

    if (!text.isEmpty()) {
        ensureConversationStarted();
        addAssistantMessageAnimated(text);
        qDebug() << "ChatView::finishStreaming: message widget created, response length =" << text.length();
    } else {
        qWarning() << "ChatView::finishStreaming: empty response, nothing to display";
    }
    qDebug() << "ChatView::finishStreaming EXIT";
}

void ChatView::abortStream() {
    qDebug() << "ChatView::abortStream";
    m_streamBuffer.clear();
    if (m_demoResponseTimer) {
        m_demoResponseTimer->stop();
        m_demoResponseTimer->deleteLater();
        m_demoResponseTimer = nullptr;
    }
    if (m_working) {
        applyWorkingUi(false);
    }
    if (m_messagesLayout) {
        ensureConversationStarted();
        addAssistantMessage("Respuesta cancelada.");
    }
}

void ChatView::showErrorMessage(const QString &message) {
    qDebug() << "ChatView::showErrorMessage:" << message;
    m_streamBuffer.clear();
    if (m_working) {
        applyWorkingUi(false);
    }
    if (!m_messagesLayout) return;
    ensureConversationStarted();
    auto *outer = new QWidget();
    outer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *outerLayout = new QHBoxLayout(outer);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(9);

    auto *errorLabel = new QLabel(message);
    errorLabel->setWordWrap(true);
    errorLabel->setStyleSheet(QString(
        "background: #fee2e2; color: #991b1b; border: 1px solid #fca5a5; border-radius: 11px;"
        "padding: 10px 14px; font-size: 13px; font-weight: 600;"
    ));
    errorLabel->setMaximumWidth(560);
    outerLayout->addWidget(makeAvatar(true), 0, Qt::AlignTop);
    outerLayout->addWidget(errorLabel, 0, Qt::AlignTop | Qt::AlignLeft);
    outerLayout->addStretch(1);
    m_messagesLayout->addWidget(outer, 0, Qt::AlignTop);
    scrollToBottom();
}
