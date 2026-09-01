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
#include <QTextDocument>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTextBlock>
#include <QTextLayout>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QLinearGradient>

#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QScreen>
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
#include <limits>
#include <numeric>

namespace {

constexpr int LARGE_PASTE_LINE_THRESHOLD = 250;
constexpr int MAX_COMPOSER_PASTE_LINES = 600;
const QString PASTED_TEXT_MARKER = QStringLiteral(
    "\n\n--- TEXTO PEGADO POR EL USUARIO (CONTENIDO COMPLETO) ---\n");

int textLineCount(const QString &text) {
    if (text.isEmpty()) return 0;
    int lines = text.count('\n') + 1;
    if (text.endsWith('\n')) --lines;
    return qMax(1, lines);
}

class ComposerEdit final : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;
    std::function<bool(const QString&)> largePasteHandler;

    int visualLineCount(int lineWidth) const {
        int lines = 0;
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            const QString text = block.text();
            if (text.isEmpty()) {
                ++lines;
                continue;
            }

            QTextLayout layout(text, font());
            QTextOption option = document()->defaultTextOption();
            option.setWrapMode(wordWrapMode());
            layout.setTextOption(option);
            layout.beginLayout();
            int blockLines = 0;
            while (true) {
                QTextLine line = layout.createLine();
                if (!line.isValid()) break;
                line.setLineWidth(qMax(1, lineWidth));
                ++blockLines;
            }
            layout.endLayout();
            lines += qMax(1, blockLines);
        }
        return qMax(1, lines);
    }

protected:
    void insertFromMimeData(const QMimeData *source) override {
        if (source && source->hasText() && largePasteHandler
            && largePasteHandler(source->text())) {
            return;
        }
        QPlainTextEdit::insertFromMimeData(source);
    }
};

class FadedBottomLabel final : public QLabel {
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent *event) override {
        QLabel::paintEvent(event);
        QPainter painter(this);
        QLinearGradient fade(0, height() * 0.45, 0, height());
        fade.setColorAt(0.0, QColor(255, 255, 255, 0));
        fade.setColorAt(1.0, QColor(255, 255, 255, 245));
        painter.fillRect(rect(), fade);
    }
};

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

void configureMarkdownDocument(QTextDocument &document, const QString &markdown) {
    document.setDefaultStyleSheet(QString(
        "body { color: %1; font-size: 13px; line-height: 1.35; }"
        "h1 { font-size: 19px; margin: 10px 0 6px 0; }"
        "h2 { font-size: 17px; margin: 9px 0 5px 0; }"
        "h3 { font-size: 15px; margin: 8px 0 4px 0; }"
        "p { margin: 3px 0 7px 0; }"
        "ul, ol { margin: 3px 0 7px 18px; }"
        "li { margin: 2px 0; }"
        "hr { border: none; border-top: 2px solid %2; margin: 9px 0; }"
        "code { font-family: monospace; background: %3; color: %1; }"
        "pre { font-family: monospace; background: %3; color: %1; border: 1px solid %2; "
        "padding: 8px; margin: 7px 0; white-space: pre-wrap; }"
        "table { border-collapse: collapse; margin: 7px 0; }"
        "th { background: %3; font-weight: 700; }"
        "th, td { border: 1px solid %2; padding: 4px 7px; }"
        "blockquote { color: %4; border-left: 3px solid %5; margin: 7px 0; padding-left: 9px; }"
        "a { color: %5; text-decoration: underline; }"
    ).arg(Style::INK, Style::BORDER_SOFT, Style::BG_LILAC,
          Style::TEXT_MUTED, Style::VIOLET));
    const QTextDocument::MarkdownFeatures features =
        QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)
        | QTextDocument::MarkdownNoHTML;
    document.setMarkdown(markdown, features);
}

QString markdownToHtml(const QString &markdown) {
    QTextDocument document;
    configureMarkdownDocument(document, markdown);
    return document.toHtml();
}

int markdownVisibleLength(const QString &markdown) {
    QTextDocument document;
    configureMarkdownDocument(document, markdown);
    return qMax(0, document.characterCount() - 1);
}

QString markdownPrefixToHtml(const QString &markdown, int visibleCharacters) {
    QTextDocument document;
    configureMarkdownDocument(document, markdown);
    const int length = qBound(0, visibleCharacters,
                              qMax(0, document.characterCount() - 1));
    QTextCursor cursor(&document);
    cursor.setPosition(0);
    cursor.setPosition(length, QTextCursor::KeepAnchor);
    return cursor.selection().toHtml();
}

QWidget* makeCodeBlock(const QString &language, const QString &code, QWidget *parent,
                       bool initiallyEmpty = false) {
    auto *panel = new SolidPanel(parent);
    panel->setFillColor(QColor("#f3edff"));
    panel->setFullBorder(QColor(Style::INK), 2);
    panel->setHardShadow(QColor(Style::INK), 3, 3);
    panel->setCornerRadius(9);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(6);

    auto *header = new QWidget(panel);
    header->setStyleSheet("background: transparent; border: none;");
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    auto *languageLabel = new QLabel(language.trimmed().isEmpty()
        ? QStringLiteral("Código") : language.trimmed());
    languageLabel->setStyleSheet(QString(
        "font-size: 10px; font-weight: 800; color: %1; border: none; background: transparent;"
    ).arg(Style::TEXT_MUTED));
    headerLayout->addWidget(languageLabel);
    headerLayout->addStretch();

    auto *copyButton = new QPushButton();
    copyButton->setObjectName("assistantCodeCopy");
    copyButton->setEnabled(!initiallyEmpty);
    copyButton->setCursor(Qt::PointingHandCursor);
    copyButton->setToolTip("Copiar bloque");
    copyButton->setIcon(QIcon(":/icons/icons/copy.svg"));
    copyButton->setIconSize(QSize(14, 14));
    copyButton->setFixedSize(26, 26);
    copyButton->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: 1px solid #c4b5fd; border-radius: 6px; "
        "padding: 0; }"
        "QPushButton:hover { background: #e9ddff; }"
        "QPushButton:disabled { color: #9ca3af; background: #f5f3ff; }"
    ).arg(Style::WHITE, Style::INK));
    QObject::connect(copyButton, &QPushButton::clicked, copyButton,
        [copyButton, code]() {
            QApplication::clipboard()->setText(code);
            copyButton->setToolTip("Copiado");
            copyButton->setIcon(QIcon(":/icons/icons/check-circle.svg"));
            QTimer::singleShot(1400, copyButton, [copyButton]() {
                copyButton->setToolTip("Copiar bloque");
                copyButton->setIcon(QIcon(":/icons/icons/copy.svg"));
            });
        });
    headerLayout->addWidget(copyButton);
    layout->addWidget(header);

    auto *editor = new QPlainTextEdit(panel);
    editor->setObjectName("assistantCodeEditor");
    editor->setPlainText(initiallyEmpty ? QString() : code);
    editor->setReadOnly(true);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editor->setStyleSheet(QString(
        "QPlainTextEdit { background: #eee6ff; color: %1; border: none; border-radius: 6px; "
        "padding: 7px; font-family: monospace; font-size: 12px; selection-background-color: #c4b5fd; }"
        "QScrollBar:vertical { width: 8px; background: transparent; }"
        "QScrollBar:horizontal { height: 8px; background: transparent; }"
        "QScrollBar::handle { background: #c4b5fd; border-radius: 4px; min-width: 24px; min-height: 24px; }"
    ).arg(Style::INK));
    const int lines = qMax(1, code.count('\n') + 1);
    editor->setFixedHeight(initiallyEmpty ? 42 : qBound(42, lines * 19 + 18, 900));
    layout->addWidget(editor);
    return panel;
}

struct AnimatedAssistantSegment {
    enum class Kind { Markdown, Code };
    Kind kind = Kind::Markdown;
    QPointer<QWidget> container;
    QPointer<QLabel> label;
    QPointer<QPlainTextEdit> editor;
    QString content;
    int length = 0;
};

QString normalizeCopyablePayload(const QString &markdown) {
    const QString trimmed = markdown.trimmed();
    if (trimmed.contains("```")) return markdown;
    if (trimmed.startsWith("<!DOCTYPE html", Qt::CaseInsensitive)
        || trimmed.startsWith("<html", Qt::CaseInsensitive)) {
        return "```html\n" + trimmed + "\n```";
    }
    return markdown;
}

QVector<AnimatedAssistantSegment> prepareAssistantAnimation(QLabel *label,
                                                             const QString &markdown) {
    QVector<AnimatedAssistantSegment> segments;
    if (!label || !label->parentWidget()) return segments;
    auto *layout = qobject_cast<QVBoxLayout*>(label->parentWidget()->layout());
    if (!layout) return segments;

    const QString normalized = normalizeCopyablePayload(markdown);
    const QRegularExpression fence(
        QStringLiteral("(?:^|\\n)```([^\\n`]*)\\n([\\s\\S]*?)(?:\\n```(?=\\n|$)|$)"));
    QRegularExpressionMatchIterator matches = fence.globalMatch(normalized);
    int sourcePosition = 0;
    int insertPosition = layout->indexOf(label);
    bool usedOriginalLabel = false;

    auto addMarkdown = [&](const QString &source) {
        const QString trimmed = source.trimmed();
        if (trimmed.isEmpty()) return;
        QLabel *segmentLabel = nullptr;
        if (!usedOriginalLabel) {
            segmentLabel = label;
            usedOriginalLabel = true;
        } else {
            segmentLabel = textLabel(QString(), 13, Style::INK, 600);
            layout->insertWidget(insertPosition, segmentLabel);
        }
        segmentLabel->clear();
        segmentLabel->setTextFormat(Qt::RichText);
        segmentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
        segmentLabel->setVisible(false);
        segments.append({AnimatedAssistantSegment::Kind::Markdown,
                         segmentLabel, segmentLabel, nullptr,
                         trimmed, markdownVisibleLength(trimmed)});
        ++insertPosition;
    };

    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        addMarkdown(normalized.mid(sourcePosition,
                                 match.capturedStart() - sourcePosition));
        const QString code = match.captured(2);
        QWidget *block = makeCodeBlock(match.captured(1), code,
                                       label->parentWidget(), true);
        block->setVisible(false);
        layout->insertWidget(insertPosition, block);
        segments.append({AnimatedAssistantSegment::Kind::Code,
                         block, nullptr,
                         block->findChild<QPlainTextEdit*>("assistantCodeEditor"),
                         code, static_cast<int>(code.size())});
        ++insertPosition;
        sourcePosition = match.capturedEnd();
    }
    addMarkdown(normalized.mid(sourcePosition));

    if (!usedOriginalLabel)
        label->hide();
    return segments;
}

void renderAssistantMarkdown(QLabel *label, const QString &markdown) {
    if (!label) return;
    auto *layout = label->parentWidget()
        ? qobject_cast<QVBoxLayout*>(label->parentWidget()->layout()) : nullptr;

    const QString normalized = normalizeCopyablePayload(markdown);
    const QRegularExpression fence(
        QStringLiteral("(?:^|\\n)```([^\\n`]*)\\n([\\s\\S]*?)(?:\\n```(?=\\n|$)|$)"));
    QRegularExpressionMatchIterator matches = fence.globalMatch(normalized);
    if (layout && matches.hasNext()) {
        int sourcePosition = 0;
        int insertPosition = layout->indexOf(label);
        bool usedOriginalLabel = false;

        auto addMarkdownSegment = [&](const QString &segment) {
            if (segment.trimmed().isEmpty()) return;
            QLabel *segmentLabel = nullptr;
            if (!usedOriginalLabel) {
                segmentLabel = label;
                usedOriginalLabel = true;
            } else {
                segmentLabel = textLabel(QString(), 13, Style::INK, 600);
                segmentLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
            }
            segmentLabel->setTextFormat(Qt::RichText);
            segmentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
            segmentLabel->setOpenExternalLinks(false);
            segmentLabel->setText(markdownToHtml(segment.trimmed()));
            if (layout->indexOf(segmentLabel) < 0)
                layout->insertWidget(insertPosition, segmentLabel);
            ++insertPosition;
        };

        matches = fence.globalMatch(normalized);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            addMarkdownSegment(normalized.mid(sourcePosition,
                match.capturedStart() - sourcePosition));
            layout->insertWidget(insertPosition,
                makeCodeBlock(match.captured(1), match.captured(2), label->parentWidget()));
            ++insertPosition;
            sourcePosition = match.capturedEnd();
        }
        addMarkdownSegment(normalized.mid(sourcePosition));
        if (!usedOriginalLabel)
            label->hide();
        return;
    }

    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    label->setOpenExternalLinks(false);
    label->setText(markdownToHtml(normalized));
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

class ConversationMinimap final : public QWidget {
public:
    explicit ConversationMinimap(QScrollArea *scroll, QWidget *content,
                                 QWidget *parent = nullptr)
        : QWidget(parent), m_scroll(scroll), m_content(content) {
        setFixedWidth(28);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QString("background: %1; border: none;").arg(Style::BG_LILAC));

        m_preview = new QWidget(this, Qt::ToolTip | Qt::FramelessWindowHint
                                      | Qt::WindowStaysOnTopHint);
        m_preview->setAttribute(Qt::WA_TranslucentBackground, true);
        m_preview->setAttribute(Qt::WA_ShowWithoutActivating, true);
        m_preview->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto *previewRoot = new QVBoxLayout(m_preview);
        previewRoot->setContentsMargins(2, 2, 6, 6);
        auto *panel = new SolidPanel(m_preview);
        panel->setFillColor(QColor(Style::WHITE));
        panel->setFullBorder(QColor(Style::INK), 2);
        panel->setHardShadow(QColor(Style::INK), 4, 4);
        panel->setCornerRadius(11);
        panel->setFixedWidth(330);
        auto *panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(12, 10, 15, 13);
        panelLayout->setSpacing(5);
        m_previewUser = new QLabel(panel);
        m_previewUser->setWordWrap(false);
        m_previewUser->setStyleSheet(QString(
            "font-size: 11px; font-weight: 800; color: %1; border: none; background: transparent;")
            .arg(Style::INK));
        m_previewAssistant = new QLabel(panel);
        m_previewAssistant->setWordWrap(true);
        m_previewAssistant->setMaximumHeight(38);
        m_previewAssistant->setStyleSheet(QString(
            "font-size: 11px; font-weight: 600; color: %1; border: none; background: transparent;")
            .arg(Style::TEXT_MUTED));
        panelLayout->addWidget(m_previewUser);
        panelLayout->addWidget(m_previewAssistant);
        previewRoot->addWidget(panel);
        m_preview->hide();

        if (m_scroll && m_scroll->verticalScrollBar()) {
            connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
                    this, [this]() { update(); });
            connect(m_scroll->verticalScrollBar(), &QScrollBar::rangeChanged,
                    this, [this]() { update(); });
        }
    }

    void clearEntries() {
        m_entries.clear();
        m_hovered = -1;
        if (m_preview) m_preview->hide();
        update();
    }

    void addUserTurn(QWidget *anchor, const QString &text) {
        Entry entry;
        entry.anchor = anchor;
        entry.user = previewText(text, QStringLiteral("Mensaje del usuario"));
        m_entries.append(entry);
        update();
    }

    void attachAssistantResponse(QWidget *anchor, const QString &text) {
        const QString response = previewText(text, QStringLiteral("Respuesta de Voryel"));
        if (!m_entries.isEmpty() && m_entries.last().assistant.isEmpty()) {
            m_entries.last().assistant = response;
        } else {
            Entry entry;
            entry.anchor = anchor;
            entry.user = QStringLiteral("Respuesta de Voryel");
            entry.assistant = response;
            m_entries.append(entry);
        }
        if (m_hovered >= 0) showPreview(m_hovered);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int active = activeEntry();
        for (int i = 0; i < m_entries.size(); ++i) {
            if (!m_entries[i].anchor) continue;
            const int y = entryY(i);
            const bool emphasized = i == active || i == m_hovered;
            QPen pen(QColor(emphasized ? Style::VIOLET : Style::TEXT_FAINT));
            pen.setWidth(emphasized ? 3 : 2);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            const int halfWidth = emphasized ? 9 : 3;
            painter.drawLine(width() / 2 - halfWidth, y,
                             width() / 2 + halfWidth, y);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        int nearest = -1;
        int nearestDistance = height() + 1;
        for (int i = 0; i < m_entries.size(); ++i) {
            if (!m_entries[i].anchor) continue;
            const int distance = qAbs(entryY(i) - event->position().y());
            if (distance < nearestDistance) {
                nearest = i;
                nearestDistance = distance;
            }
        }
        if (nearest != m_hovered) {
            m_hovered = nearest;
            if (m_hovered >= 0) showPreview(m_hovered);
            else if (m_preview) m_preview->hide();
            update();
        } else if (nearest >= 0) {
            positionPreview(nearest);
        }
        QWidget::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && m_hovered >= 0
            && m_hovered < m_entries.size() && m_entries[m_hovered].anchor
            && m_scroll && m_scroll->verticalScrollBar()) {
            const int target = m_entries[m_hovered].anchor->y()
                - m_scroll->viewport()->height() / 3;
            m_scroll->verticalScrollBar()->setValue(qBound(
                m_scroll->verticalScrollBar()->minimum(), target,
                m_scroll->verticalScrollBar()->maximum()));
        }
        QWidget::mousePressEvent(event);
    }

    void leaveEvent(QEvent *event) override {
        m_hovered = -1;
        if (m_preview) m_preview->hide();
        update();
        QWidget::leaveEvent(event);
    }

private:
    struct Entry {
        QPointer<QWidget> anchor;
        QString user;
        QString assistant;
    };

    static QString previewText(QString text, const QString &fallback) {
        text.remove(QRegularExpression(QStringLiteral("<VORYEL_TASK[\\s\\S]*?</VORYEL_TASK>"),
                                       QRegularExpression::CaseInsensitiveOption));
        text.replace(QRegularExpression(QStringLiteral("```[^\\n]*")), QString());
        text.replace(QStringLiteral("```"), QString());
        text.replace(QRegularExpression(QStringLiteral("[#*_>|`]+")), QStringLiteral(" "));
        text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        text = text.trimmed();
        return text.isEmpty() ? fallback : text;
    }

    int entryY(int index) const {
        if (index < 0 || index >= m_entries.size() || !m_entries[index].anchor)
            return height() / 2;
        const int contentHeight = qMax(1, m_content ? m_content->height() : 1);
        const qreal ratio = qBound(0.0,
            (m_entries[index].anchor->y() + m_entries[index].anchor->height() * 0.5)
                / qreal(contentHeight), 1.0);
        return 10 + qRound(ratio * qMax(1, height() - 20));
    }

    int activeEntry() const {
        if (!m_scroll || m_entries.isEmpty()) return -1;
        const int center = m_scroll->verticalScrollBar()->value()
            + m_scroll->viewport()->height() / 2;
        int nearest = -1;
        int distance = std::numeric_limits<int>::max();
        for (int i = 0; i < m_entries.size(); ++i) {
            if (!m_entries[i].anchor) continue;
            const int candidate = qAbs(m_entries[i].anchor->y() - center);
            if (candidate < distance) {
                distance = candidate;
                nearest = i;
            }
        }
        return nearest;
    }

    void showPreview(int index) {
        if (!m_preview || index < 0 || index >= m_entries.size()) return;
        const Entry &entry = m_entries[index];
        const QFontMetrics userMetrics(m_previewUser->font());
        m_previewUser->setText(userMetrics.elidedText(entry.user, Qt::ElideRight, 296));
        const QString response = entry.assistant.isEmpty()
            ? QStringLiteral("Esperando la respuesta…") : entry.assistant;
        const QFontMetrics assistantMetrics(m_previewAssistant->font());
        const int lineWidth = 296;
        QString first = assistantMetrics.elidedText(response, Qt::ElideRight, lineWidth);
        QString remaining = response.mid(qMin(response.size(), first.size()));
        if (remaining.trimmed().isEmpty()) {
            m_previewAssistant->setText(first);
        } else {
            m_previewAssistant->setText(first + "\n"
                + assistantMetrics.elidedText(remaining.trimmed(), Qt::ElideRight, lineWidth));
        }
        m_preview->adjustSize();
        positionPreview(index);
        m_preview->show();
        m_preview->raise();
    }

    void positionPreview(int index) {
        if (!m_preview || index < 0 || index >= m_entries.size()) return;
        QPoint position = mapToGlobal(QPoint(-m_preview->width() - 8,
                                             entryY(index) - m_preview->height() / 2));
        QScreen *screen = QApplication::screenAt(mapToGlobal(rect().center()));
        if (!screen) screen = QApplication::primaryScreen();
        if (screen) {
            const QRect available = screen->availableGeometry();
            position.setX(qMax(available.left() + 8, position.x()));
            position.setY(qBound(available.top() + 8, position.y(),
                                 available.bottom() - m_preview->height() - 8));
        }
        m_preview->move(position);
    }

    QPointer<QScrollArea> m_scroll;
    QPointer<QWidget> m_content;
    QVector<Entry> m_entries;
    QWidget *m_preview = nullptr;
    QLabel *m_previewUser = nullptr;
    QLabel *m_previewAssistant = nullptr;
    int m_hovered = -1;
};

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
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(Style::scrollAreaStyle());
    m_scroll->viewport()->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));
    m_scroll->viewport()->installEventFilter(this);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
        if (!m_scroll || !m_scroll->verticalScrollBar()) return;
        const QScrollBar *bar = m_scroll->verticalScrollBar();
        m_followLatest = (bar->maximum() - value) <= 4;
    });

    m_messagesContent = new QWidget();
    m_messagesContent->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));
    m_messagesContent->installEventFilter(this);
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
    m_conversationMinimap = new ConversationMinimap(
        m_scroll, m_messagesContent, middle);
    middleLayout->addWidget(m_conversationMinimap, 0);

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
    inputRow->addWidget(m_attachButton, 0, Qt::AlignBottom);

    auto *composer = new ComposerEdit();
    m_input = composer;
    composer->largePasteHandler = [this](const QString &text) {
        return captureLargePaste(text);
    };
    m_input->setPlaceholderText("Pedile algo a Voryel...");
    m_input->setFixedHeight(42);
    m_input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_input->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_input->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_input->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input->setTabChangesFocus(false);
    // Un pequeño margen interno evita que el primer glifo de cada línea
    // envuelta quede contra el clip del viewport y pierda sus píxeles izquierdos.
    m_input->document()->setDocumentMargin(3);
    m_input->setStyleSheet(QString(
        "QPlainTextEdit {"
        "  background: %1; color: %2; border: 2px solid %3; border-radius: 12px;"
        "  padding: 10px 16px; font-size: 14px; font-weight: 600;"
        "}"
        "QPlainTextEdit:focus { border-color: %4; }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 8px 2px; }"
        "QScrollBar::handle:vertical { background: #c4b5fd; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ).arg(Style::BG_LILAC, Style::INK, Style::BORDER_SOFT, Style::VIOLET));
    m_input->installEventFilter(this);
    connect(m_input, &QPlainTextEdit::textChanged, this, [this]() {
        QTimer::singleShot(0, this, &ChatView::updateComposerHeight);
    });
    inputRow->addWidget(m_input, 1, Qt::AlignBottom);

    m_sendButton = new QPushButton();
    m_sendButton->setFixedSize(42, 42);
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setIconSize(QSize(18, 18));
    connect(m_sendButton, &QPushButton::clicked, this, &ChatView::handleSend);
    inputRow->addWidget(m_sendButton, 0, Qt::AlignBottom);
    inputLayout->addLayout(inputRow);
    updateSendButton();

    m_pastedTextCard = new SolidPanel(m_inputBar);
    auto *pasteCard = static_cast<SolidPanel*>(m_pastedTextCard);
    pasteCard->setFillColor(QColor("#f3edff"));
    pasteCard->setCornerRadius(10);
    pasteCard->setFullBorder(QColor(Style::INK), 2);
    pasteCard->setHardShadow(QColor(Style::INK), 2, 2);
    pasteCard->setVisible(false);
    auto *pasteLayout = new QHBoxLayout(pasteCard);
    pasteLayout->setContentsMargins(12, 8, 14, 10);
    pasteLayout->setSpacing(9);
    pasteLayout->addWidget(iconLabel(":/icons/icons/file-text.svg",
                                     QColor(Style::VIOLET), 15));
    auto *pasteTextColumn = new QVBoxLayout();
    pasteTextColumn->setSpacing(1);
    auto *pasteTitle = textLabel("Texto pegado", 12, Style::INK, 900);
    pasteTextColumn->addWidget(pasteTitle);
    m_pastedTextInfo = textLabel(QString(), 10, Style::TEXT_MUTED, 700);
    pasteTextColumn->addWidget(m_pastedTextInfo);
    pasteLayout->addLayout(pasteTextColumn, 1);

    m_restorePastedTextButton = new QPushButton("Poner en el campo de texto");
    m_restorePastedTextButton->setCursor(Qt::PointingHandCursor);
    m_restorePastedTextButton->setMinimumHeight(26);
    m_restorePastedTextButton->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: 1.5px solid %2; border-radius: 7px;"
        " padding: 3px 8px; font-size: 10px; font-weight: 800; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:disabled { color: %4; border-color: %4; background: %1; }"
    ).arg(Style::WHITE, Style::INK, Style::VIOLET_LIGHT, Style::TEXT_FAINT));
    connect(m_restorePastedTextButton, &QPushButton::clicked,
            this, &ChatView::restorePastedTextToComposer);
    pasteLayout->addWidget(m_restorePastedTextButton, 0, Qt::AlignVCenter);

    auto *removePasteButton = new QPushButton();
    removePasteButton->setFixedSize(24, 24);
    removePasteButton->setCursor(Qt::PointingHandCursor);
    removePasteButton->setToolTip("Quitar texto pegado");
    removePasteButton->setIcon(IconUtil::coloredIcon(
        ":/icons/icons/x.svg", QColor(Style::INK), QSize(12, 12)));
    removePasteButton->setIconSize(QSize(12, 12));
    removePasteButton->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 6px; }"
        "QPushButton:hover { background: #fee2e2; }");
    connect(removePasteButton, &QPushButton::clicked, this, [this]() {
        m_pendingPastedText.clear();
        refreshPastedTextCard();
    });
    pasteLayout->addWidget(removePasteButton, 0, Qt::AlignVCenter);
    inputLayout->insertWidget(1, m_pastedTextCard);

    m_pasteRestoreUndoRow = new QWidget(m_inputBar);
    m_pasteRestoreUndoRow->setStyleSheet("background: transparent; border: none;");
    m_pasteRestoreUndoRow->setVisible(false);
    auto *undoLayout = new QHBoxLayout(m_pasteRestoreUndoRow);
    undoLayout->setContentsMargins(0, 0, 2, 0);
    undoLayout->setSpacing(0);
    undoLayout->addStretch();
    auto *undoButton = new QPushButton("↶  Deshacer");
    undoButton->setCursor(Qt::PointingHandCursor);
    undoButton->setMinimumHeight(27);
    undoButton->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: 2px solid %2; border-radius: 7px;"
        " padding: 3px 10px; font-size: 10px; font-weight: 900; }"
        "QPushButton:hover { background: %3; }"
    ).arg(Style::WHITE, Style::INK, Style::VIOLET_LIGHT));
    connect(undoButton, &QPushButton::clicked,
            this, &ChatView::undoPastedTextRestore);
    undoLayout->addWidget(undoButton);
    inputLayout->insertWidget(2, m_pasteRestoreUndoRow);

    m_pasteRestoreUndoTimer = new QTimer(this);
    m_pasteRestoreUndoTimer->setSingleShot(true);
    connect(m_pasteRestoreUndoTimer, &QTimer::timeout,
            this, &ChatView::clearPastedTextRestoreUndo);

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
            m_input->setPlainText(label);
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
        m_input->setPlainText(prompt);
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
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty() && m_pendingAttachments.isEmpty()
        && m_pendingPastedText.isEmpty()) {
        qDebug() << "ChatView::handleSend: empty text and no attachments, returning";
        return;
    }

    qDebug() << "ChatView::handleSend: text length =" << text.length()
             << "m_working =" << m_working
             << "m_demoMode =" << m_demoMode
             << "activeChatIndex =" << (m_chatStore ? m_chatStore->activeChatIndex() : -1);

    clearPastedTextRestoreUndo();
    ensureConversationStarted();
    m_followLatest = true;
    const QStringList attachments = m_pendingAttachments;
    QString outgoingText = text;
    if (!m_pendingPastedText.isEmpty()) {
        if (outgoingText.isEmpty()) outgoingText = "Texto pegado";
        outgoingText += PASTED_TEXT_MARKER + m_pendingPastedText;
    } else if (outgoingText.isEmpty()) {
        outgoingText = "Archivos adjuntos";
    }
    addUserMessage(outgoingText, attachments);
    m_input->clear();
    m_pendingAttachments.clear();
    m_pendingPastedText.clear();
    refreshAttachmentChips();
    refreshPastedTextCard();
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
            bubble->setObjectName("typingActivityBubble");
            bubble->setProperty("thinkingToggle", true);
            bubble->setCursor(Qt::PointingHandCursor);
            bubble->installEventFilter(this);
            bubble->setAttribute(Qt::WA_StyledBackground, true);
            bubble->setStyleSheet(QString(
                "QWidget#typingActivityBubble { background: %1; border: 1px solid %2; border-radius: 12px; }"
                "QWidget#typingActivityBubble QWidget { background: transparent; border: none; }"
            ).arg(Style::WHITE, Style::BORDER_SOFT));
            auto *bLayout = new QVBoxLayout(bubble);
            bLayout->setContentsMargins(14, 10, 16, 12);
            bLayout->setSpacing(7);
            auto *dotsRow = new QWidget(bubble);
            dotsRow->setProperty("thinkingToggle", true);
            dotsRow->installEventFilter(this);
            auto *dotsLayout = new QHBoxLayout(dotsRow);
            dotsLayout->setContentsMargins(0, 0, 0, 0);
            dotsLayout->setSpacing(7);
            auto makeDot = [this](const QString &color, int size) -> QWidget* {
                auto *c = new QWidget();
                c->setFixedSize(size, size + 10);
                c->setProperty("thinkingToggle", true);
                c->setStyleSheet("background: transparent; border: none;");
                c->installEventFilter(this);
                auto *dot = new QLabel(c);
                dot->setFixedSize(size, size);
                dot->move(0, 5);
                dot->setProperty("thinkingToggle", true);
                dot->installEventFilter(this);
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
            dotsLayout->addWidget(container1);
            dotsLayout->addWidget(container2);
            dotsLayout->addWidget(container3);
            dotsLayout->addStretch();
            bLayout->addWidget(dotsRow);

            m_thinkingDetails = new QWidget(bubble);
            m_thinkingDetails->setVisible(false);
            auto *detailsLayout = new QVBoxLayout(m_thinkingDetails);
            detailsLayout->setContentsMargins(0, 1, 0, 0);
            detailsLayout->setSpacing(3);
            m_thinkingLine1 = textLabel(QString(),
                                        11, "#77717f", 650);
            m_thinkingLine2 = textLabel(QString(),
                                        11, "#85808d", 600);
            m_thinkingLine3 = new FadedBottomLabel(
                QString(), m_thinkingDetails);
            m_thinkingLine3->setStyleSheet(
                "font-size: 11px; font-weight: 600; color: #85808d; border: none; background: transparent;");
            for (QLabel *line : {m_thinkingLine1, m_thinkingLine2, m_thinkingLine3}) {
                line->setWordWrap(false);
                line->setMinimumWidth(285);
                detailsLayout->addWidget(line);
            }
            m_thinkingLine1->hide();
            m_thinkingLine2->hide();
            m_thinkingLine3->hide();
            refreshThinkingActivity();
            bLayout->addWidget(m_thinkingDetails);
            m_typingBubble = bubble;
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
            m_typingBubble = nullptr;
            m_thinkingDetails = nullptr;
            m_thinkingLine1 = nullptr;
            m_thinkingLine2 = nullptr;
            m_thinkingLine3 = nullptr;
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

void ChatView::updateComposerHeight() {
    if (!m_input || !m_input->document()) return;
    auto *composer = static_cast<ComposerEdit*>(m_input);
    const int lineHeight = QFontMetrics(m_input->font()).lineSpacing();
    const int documentMargins = qCeil(m_input->document()->documentMargin() * 2.0);
    const int layoutWidth = qMax(1, m_input->viewport()->width() - documentMargins);
    const int visibleLines = composer->visualLineCount(layoutWidth);
    const int chromeHeight = qMax(24, m_input->height() - m_input->viewport()->height());
    const int minimumHeight = 42;
    const int maximumVisibleLines = 10;
    const int layoutSlack = 6;
    const int maximumHeight = lineHeight * maximumVisibleLines + chromeHeight + layoutSlack;
    const int contentHeight = lineHeight * visibleLines + chromeHeight + layoutSlack;
    const int targetHeight = qBound(minimumHeight, contentHeight, maximumHeight);
    if (m_input->height() != targetHeight)
        m_input->setFixedHeight(targetHeight);
    m_input->setVerticalScrollBarPolicy(
        visibleLines > maximumVisibleLines ? Qt::ScrollBarAsNeeded
                                           : Qt::ScrollBarAlwaysOff);
    QTimer::singleShot(0, m_input, [input = m_input, visibleLines, maximumVisibleLines]() {
        if (input->horizontalScrollBar())
            input->horizontalScrollBar()->setValue(input->horizontalScrollBar()->minimum());
        if (visibleLines <= maximumVisibleLines && input->verticalScrollBar())
            input->verticalScrollBar()->setValue(0);
        input->ensureCursorVisible();
    });
}

void ChatView::addUserMessage(const QString &text, const QStringList &attachments) {
    const int markerPosition = text.indexOf(PASTED_TEXT_MARKER);
    if (markerPosition >= 0) {
        const QString instruction = text.left(markerPosition).trimmed();
        const QString pastedText = text.mid(markerPosition + PASTED_TEXT_MARKER.size());
        if (!instruction.isEmpty() && instruction != "Texto pegado")
            addMessageBubble(instruction, instruction, true, nullptr, QString(), attachments);
        else if (!attachments.isEmpty())
            addMessageBubble("Archivos adjuntos", "Archivos adjuntos", true,
                             nullptr, QString(), attachments);
        addPastedTextMessageCard(pastedText);
        return;
    }
    if (textLineCount(text) >= LARGE_PASTE_LINE_THRESHOLD) {
        if (!attachments.isEmpty())
            addMessageBubble("Archivos adjuntos", "Archivos adjuntos", true,
                             nullptr, QString(), attachments);
        addPastedTextMessageCard(text);
        return;
    }
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

    auto segments = std::make_shared<QVector<AnimatedAssistantSegment>>(
        prepareAssistantAnimation(contentLabel, text));
    if (segments->isEmpty()) {
        renderAssistantMarkdown(contentLabel, text);
        emit taskStateChanged("Respuesta lista");
        if (onFinished) QTimer::singleShot(90, this, [onFinished]() { onFinished(); });
        return;
    }

    auto *timer = new QTimer(this);
    auto segmentIndex = std::make_shared<int>(0);
    auto characterIndex = std::make_shared<int>(0);
    const int totalLength = std::accumulate(segments->cbegin(), segments->cend(), 0,
        [](int total, const AnimatedAssistantSegment &segment) {
            return total + segment.length;
        });
    const int chunk = totalLength > 120 ? 3 : 2;

    connect(timer, &QTimer::timeout, this,
            [this, timer, segments, segmentIndex, characterIndex,
             onFinished, chunk]() mutable {
        while (*segmentIndex < segments->size()
               && segments->at(*segmentIndex).length == 0) {
            if (segments->at(*segmentIndex).container)
                segments->at(*segmentIndex).container->show();
            ++*segmentIndex;
        }
        if (*segmentIndex >= segments->size()) {
            timer->stop();
            timer->deleteLater();
            emit taskStateChanged("Respuesta lista");
            if (onFinished)
                QTimer::singleShot(90, this, [onFinished]() { onFinished(); });
            return;
        }

        AnimatedAssistantSegment &segment = (*segments)[*segmentIndex];
        if (!segment.container) {
            timer->stop();
            timer->deleteLater();
            return;
        }
        segment.container->show();
        const int next = qMin(*characterIndex + chunk, segment.length);
        if (segment.kind == AnimatedAssistantSegment::Kind::Markdown && segment.label) {
            segment.label->setText(markdownPrefixToHtml(segment.content, next));
        } else if (segment.kind == AnimatedAssistantSegment::Kind::Code && segment.editor) {
            const QString visibleCode = segment.content.left(next);
            segment.editor->setPlainText(visibleCode);
            const int visibleLines = qMax(1, visibleCode.count('\n') + 1);
            segment.editor->setFixedHeight(qBound(42, visibleLines * 19 + 18, 900));
            QTextCursor cursor = segment.editor->textCursor();
            cursor.movePosition(QTextCursor::End);
            segment.editor->setTextCursor(cursor);
        }
        *characterIndex = next;
        scrollToBottom();

        if (*characterIndex >= segment.length) {
            if (segment.kind == AnimatedAssistantSegment::Kind::Code && segment.container) {
                if (auto *button = segment.container->findChild<QPushButton*>("assistantCodeCopy"))
                    button->setEnabled(true);
            }
            ++*segmentIndex;
            *characterIndex = 0;
        }
    });

    timer->start(9);
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
    outerLayout->setSpacing(user ? 5 : 9);

    QLabel *contentLabel = nullptr;
    auto *bubble = makeBubble(sizingText, visibleText, user, &contentLabel, copyText);
    if (outLabel) *outLabel = contentLabel;
    auto *time = new QLabel(QDateTime::currentDateTime().toString("hh:mm"));
    time->setStyleSheet(QString("font-size: 10px; color: %1; border: none; background: transparent;").arg(Style::TEXT_FAINT));

    auto *stack = new QWidget(outer);
    stack->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    if (user && attachments.isEmpty())
        stack->setMaximumWidth(430);
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
    if (m_conversationMinimap) {
        if (user)
            m_conversationMinimap->addUserTurn(outer, sizingText);
        else
            m_conversationMinimap->attachAssistantResponse(outer, sizingText);
    }
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

bool ChatView::captureLargePaste(const QString &text) {
    if (textLineCount(text) < LARGE_PASTE_LINE_THRESHOLD)
        return false;
    if (!m_pendingPastedText.isEmpty())
        m_pendingPastedText += '\n';
    m_pendingPastedText += text;
    refreshPastedTextCard();
    return true;
}

void ChatView::refreshPastedTextCard() {
    if (!m_pastedTextCard) return;
    const bool visible = !m_pendingPastedText.isEmpty();
    m_pastedTextCard->setVisible(visible);
    if (!visible || !m_pastedTextInfo) return;
    const int lines = textLineCount(m_pendingPastedText);
    const qint64 bytes = m_pendingPastedText.toUtf8().size();
    const QString size = bytes < 1024
        ? QString::number(bytes) + " B"
        : QString::number(double(bytes) / 1024.0, 'f', 1) + " KB";
    const bool tooLongForComposer = lines > MAX_COMPOSER_PASTE_LINES;
    if (tooLongForComposer) {
        m_pastedTextInfo->setText(
            QString("Demasiado largo para mostrarse en el campo de texto · %1 líneas · %2")
                .arg(lines).arg(size));
    } else {
        m_pastedTextInfo->setText(QString("%1 líneas · %2 · se enviará completo")
                                  .arg(lines).arg(size));
    }
    if (m_restorePastedTextButton) {
        m_restorePastedTextButton->setEnabled(!tooLongForComposer);
        m_restorePastedTextButton->setToolTip(tooLongForComposer
            ? QString("El límite para mostrarlo es de %1 líneas; igualmente se enviará completo.")
                  .arg(MAX_COMPOSER_PASTE_LINES)
            : QString());
    }
}

void ChatView::restorePastedTextToComposer() {
    if (!m_input || m_pendingPastedText.isEmpty()) return;
    if (textLineCount(m_pendingPastedText) > MAX_COMPOSER_PASTE_LINES) {
        refreshPastedTextCard();
        return;
    }

    clearPastedTextRestoreUndo();
    QTextCursor cursor = m_input->textCursor();
    const QString currentText = m_input->toPlainText();
    m_pasteRestoreStart = cursor.selectionStart();
    m_replacedTextBeforePasteRestore = currentText.mid(
        cursor.selectionStart(), cursor.selectionEnd() - cursor.selectionStart());
    m_lastRestoredPastedText = m_pendingPastedText;

    cursor.insertText(m_lastRestoredPastedText);
    m_input->setTextCursor(cursor);
    m_pendingPastedText.clear();
    refreshPastedTextCard();
    if (m_pasteRestoreUndoRow) m_pasteRestoreUndoRow->setVisible(true);
    if (m_pasteRestoreUndoTimer) m_pasteRestoreUndoTimer->start(5000);
    m_input->setFocus();
}

void ChatView::undoPastedTextRestore() {
    if (!m_input || m_lastRestoredPastedText.isEmpty()) {
        clearPastedTextRestoreUndo();
        return;
    }

    const QString restoredText = m_lastRestoredPastedText;
    const QString currentText = m_input->toPlainText();
    int actualStart = m_pasteRestoreStart;
    if (actualStart < 0
        || currentText.mid(actualStart, restoredText.size()) != restoredText) {
        actualStart = currentText.indexOf(restoredText);
    }

    if (actualStart >= 0) {
        QTextCursor cursor(m_input->document());
        cursor.setPosition(actualStart);
        cursor.setPosition(actualStart + restoredText.size(), QTextCursor::KeepAnchor);
        cursor.insertText(m_replacedTextBeforePasteRestore);
        m_input->setTextCursor(cursor);
        if (m_pendingPastedText.isEmpty())
            m_pendingPastedText = restoredText;
        else
            m_pendingPastedText = restoredText + '\n' + m_pendingPastedText;
    }

    clearPastedTextRestoreUndo();
    refreshPastedTextCard();
    m_input->setFocus();
}

void ChatView::clearPastedTextRestoreUndo() {
    if (m_pasteRestoreUndoTimer) m_pasteRestoreUndoTimer->stop();
    if (m_pasteRestoreUndoRow) m_pasteRestoreUndoRow->setVisible(false);
    m_lastRestoredPastedText.clear();
    m_replacedTextBeforePasteRestore.clear();
    m_pasteRestoreStart = -1;
}

void ChatView::addPastedTextMessageCard(const QString &text) {
    if (!m_messagesLayout || text.isEmpty()) return;
    if (m_emptyState && m_emptyState->parent()) {
        m_messagesLayout->removeWidget(m_emptyState);
        m_emptyState->hide();
    }

    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::VIOLET));
    card->setCornerRadius(11);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 3, 3);
    card->setMaximumWidth(430);
    card->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);

    auto *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(13, 10, 16, 13);
    cardLayout->setSpacing(9);
    cardLayout->addWidget(iconLabel(":/icons/icons/file-text.svg",
                                    QColor(Style::WHITE), 15), 0, Qt::AlignTop);

    auto *textColumn = new QVBoxLayout();
    textColumn->setSpacing(2);
    auto *title = textLabel("Texto pegado", 13, Style::WHITE, 900);
    title->setWordWrap(false);
    textColumn->addWidget(title);
    const int lines = textLineCount(text);
    const qint64 bytes = text.toUtf8().size();
    const QString size = bytes < 1024
        ? QString::number(bytes) + " B"
        : QString::number(double(bytes) / 1024.0, 'f', 1) + " KB";
    auto *details = textLabel(QString("%1 líneas · %2 · contenido completo")
                              .arg(lines).arg(size), 10, "#ede9fe", 700);
    details->setWordWrap(false);
    textColumn->addWidget(details);
    cardLayout->addLayout(textColumn, 1);

    auto *copyButton = new QPushButton();
    copyButton->setFixedSize(25, 25);
    copyButton->setCursor(Qt::PointingHandCursor);
    copyButton->setToolTip("Copiar texto pegado");
    copyButton->setIcon(IconUtil::coloredIcon(
        ":/icons/icons/copy.svg", QColor(Style::WHITE), QSize(13, 13)));
    copyButton->setIconSize(QSize(13, 13));
    copyButton->setStyleSheet(
        "QPushButton { background: transparent; border: 1px solid #c4b5fd; border-radius: 6px; }"
        "QPushButton:hover { background: #6d28d9; }");
    connect(copyButton, &QPushButton::clicked, this, [copyButton, text]() {
        QApplication::clipboard()->setText(text);
        copyButton->setToolTip("Copiado");
        copyButton->setIcon(QIcon(":/icons/icons/check-circle.svg"));
        QTimer::singleShot(1400, copyButton, [copyButton]() {
            copyButton->setToolTip("Copiar texto pegado");
            copyButton->setIcon(QIcon(":/icons/icons/copy.svg"));
        });
    });
    cardLayout->addWidget(copyButton, 0, Qt::AlignVCenter);

    auto *stack = new QWidget();
    stack->setMaximumWidth(430);
    stack->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    auto *stackLayout = new QVBoxLayout(stack);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    stackLayout->setSpacing(2);
    stackLayout->addWidget(card, 0, Qt::AlignRight);
    auto *time = new QLabel(QDateTime::currentDateTime().toString("hh:mm"));
    time->setStyleSheet(QString(
        "font-size: 10px; color: %1; border: none; background: transparent;")
        .arg(Style::TEXT_FAINT));
    stackLayout->addWidget(time, 0, Qt::AlignRight);

    auto *outer = new QWidget();
    outer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *outerLayout = new QHBoxLayout(outer);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(5);
    outerLayout->addStretch(1);
    outerLayout->addWidget(stack, 0, Qt::AlignTop | Qt::AlignRight);
    outerLayout->addWidget(makeAvatar(false), 0, Qt::AlignTop);
    m_messagesLayout->addWidget(outer, 0, Qt::AlignTop);
    if (m_conversationMinimap)
        m_conversationMinimap->addUserTurn(outer, QStringLiteral("Texto pegado"));
    ++m_messageCount;
    scrollToBottom();
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
        bubble->setFillColor(Qt::transparent);
        bubble->setFullBorder(Qt::transparent, 0);
        bubble->setHardShadow(Qt::transparent, 0, 0);
    }
    bubble->setCornerRadius(11);
    bubble->setMaximumWidth(user ? 430 : 680);
    const int textLen = static_cast<int>(sizingText.size());
    const int naturalWidth = qBound(user ? 120 : 220, textLen * 6 + 38, user ? 400 : 640);
    bubble->setMinimumWidth(naturalWidth);
    bubble->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(bubble);
    // Reserve the painted border and hard-shadow area. Without real layout
    // margins, multiline labels can be measured against pixels that belong to
    // the border, clipping the first or last row of glyphs.
    layout->setContentsMargins(user ? 2 : 0, user ? 2 : 0,
                               user ? 5 : 0, user ? 5 : 0);
    auto *inner = new QWidget(bubble);
    inner->setAttribute(Qt::WA_StyledBackground, true);
    inner->setStyleSheet("background: transparent; border: none;");
    auto *innerLayout = new QVBoxLayout(inner);
    innerLayout->setContentsMargins(user ? 12 : 0, user ? 8 : 2,
                                    user ? 14 : 2, user ? 9 : 2);
    innerLayout->setSpacing(8);
    auto *lbl = textLabel(visibleText, 13, user ? Style::WHITE : Style::INK, 600);
    if (user) {
        lbl->setTextFormat(Qt::PlainText);
        lbl->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        lbl->setMargin(2);
        lbl->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse
                                     | Qt::TextSelectableByKeyboard);
    }
    if (contentLabel) *contentLabel = lbl;
    innerLayout->addWidget(lbl);
    if (!user && !visibleText.isEmpty())
        renderAssistantMarkdown(lbl, visibleText);

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
            m_input->setPlainText(s);
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
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *widget = qobject_cast<QWidget*>(obj);
        if (widget && widget->property("thinkingToggle").toBool()
            && m_thinkingDetails) {
            const auto *mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_thinkingDetails->setVisible(!m_thinkingDetails->isVisible());
                if (m_typingBubble) m_typingBubble->adjustSize();
                scrollToBottom();
                return true;
            }
        }
    }
    if ((obj == (m_scroll ? m_scroll->viewport() : nullptr)
         || obj == m_messagesContent)
        && event->type() == QEvent::Wheel) {
        const auto *wheel = static_cast<QWheelEvent*>(event);
        if (wheel->angleDelta().y() > 0 || wheel->pixelDelta().y() > 0)
            m_followLatest = false;
    }
    if (obj == m_input) {
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent*>(event);
            const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
            if (enter && !(key->modifiers() & Qt::ShiftModifier)) {
                handleSend();
                return true;
            }
        } else if (event->type() == QEvent::Resize) {
            QTimer::singleShot(0, this, &ChatView::updateComposerHeight);
        }
    }
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
    if (m_conversationMinimap)
        m_conversationMinimap->clearEntries();
    m_emptyState = makeWelcomeState();
    m_messagesLayout->addStretch(1);
    m_messagesLayout->addWidget(m_emptyState, 0, Qt::AlignCenter);
    m_messagesLayout->addStretch(1);
}

void ChatView::updateContextUsage(const TokenUsage &usage, qint64 contextLimit,
                                  const QString &provider, const QString &model,
                                  const QString &limitSource,
                                  bool compactionPending) {
    if (m_contextMeter)
        m_contextMeter->setUsage(usage, contextLimit, provider, model,
                                 limitSource, compactionPending);
}

void ChatView::loadChatMessages(int chatIndex) {
    if (!m_chatStore) return;
    if (m_working) return;
    const auto &chats = m_chatStore->chats();
    if (chatIndex < 0 || chatIndex >= chats.size()) return;

    m_followLatest = true;
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
        if (m_followLatest && m_scroll && m_scroll->verticalScrollBar()) {
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

void ChatView::beginStreaming(const QStringList &attachments) {
    qDebug() << "ChatView::beginStreaming: clearing buffer, previous length =" << m_streamBuffer.length();
    m_streamBuffer.clear();
    m_reasoningBuffer.clear();
    m_activityLog.clear();
    m_liveReasoningLine.clear();
    m_embeddedReasoningLength = 0;
    m_generationActivityShown = false;
    m_receivingActivityShown = false;
    if (!m_working)
        applyWorkingUi(true);
    reportActivity(QStringLiteral("Historial leído y contexto preparado"));
    if (!attachments.isEmpty()) {
        QStringList names;
        for (const QString &path : attachments)
            names.append(QFileInfo(path).fileName());
        const QString files = names.size() <= 2
            ? names.join(QStringLiteral(" y "))
            : QStringLiteral("%1 archivos adjuntos").arg(names.size());
        reportActivity(QStringLiteral("Incluyendo %1 en la solicitud").arg(files));
    }
}

void ChatView::reportActivity(const QString &activity) {
    QString clean = activity.simplified();
    if (clean.isEmpty()) return;
    if (m_activityLog.isEmpty() || m_activityLog.last() != clean)
        m_activityLog.append(clean);
    while (m_activityLog.size() > 8)
        m_activityLog.removeFirst();
    refreshThinkingActivity();
}

void ChatView::refreshThinkingActivity() {
    if (!m_thinkingLine1 || !m_thinkingLine2 || !m_thinkingLine3) return;
    QStringList visible = m_activityLog;
    if (!m_liveReasoningLine.isEmpty())
        visible.append(m_liveReasoningLine);
    visible = visible.mid(qMax(0, visible.size() - 3));

    const QList<QLabel*> labels = {m_thinkingLine1, m_thinkingLine2, m_thinkingLine3};
    for (int i = 0; i < labels.size(); ++i) {
        if (i >= visible.size()) {
            labels[i]->hide();
            continue;
        }
        QString line = visible[i];
        if (line.size() > 115) line = line.left(112).trimmed() + QStringLiteral("…");
        labels[i]->setText(line);
        labels[i]->show();
    }
    if (m_typingBubble) m_typingBubble->adjustSize();
}

void ChatView::appendReasoningSummary(const QString &summaryDelta) {
    if (summaryDelta.isEmpty()) return;
    m_reasoningBuffer += summaryDelta;
    if (m_reasoningBuffer.size() > 12000)
        m_reasoningBuffer = m_reasoningBuffer.right(12000);
    if (!m_thinkingLine1 || !m_thinkingLine2 || !m_thinkingLine3) return;

    QString visible = m_reasoningBuffer;
    visible.replace(QRegularExpression(QStringLiteral("```[^\\n]*")), QString());
    visible.replace(QStringLiteral("```"), QString());
    visible.replace(QRegularExpression(QStringLiteral("[#*_>`]+")), QStringLiteral(" "));
    visible.replace('\r', '\n');

    QStringList steps;
    const QStringList candidates = visible.split(
        QRegularExpression(QStringLiteral("(?:\\n+|(?<=[.!?])\\s+)")),
        Qt::SkipEmptyParts);
    for (QString candidate : candidates) {
        candidate.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        candidate = candidate.trimmed();
        if (!candidate.isEmpty()) steps.append(candidate);
    }
    if (steps.isEmpty()) return;

    if (!m_activityLog.contains(QStringLiteral("El modelo está razonando")))
        reportActivity(QStringLiteral("El modelo está razonando"));
    m_liveReasoningLine = QStringLiteral("Razonamiento: ") + steps.last();
    refreshThinkingActivity();
}

void ChatView::appendStreamToken(const QString &token) {
    m_streamBuffer += token;
    // Algunos servidores locales envían el razonamiento dentro de <think>
    // en lugar de usar un campo estructurado. Se muestra mientras llega, pero
    // PromptBuilder lo elimina de la respuesta final destinada al chat.
    QRegularExpression expression(
        QStringLiteral("<think\\b[^>]*>([\\s\\S]*?)(?:</think>|$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (m_reasoningBuffer.isEmpty() || m_embeddedReasoningLength > 0) {
        QString embedded;
        auto matches = expression.globalMatch(m_streamBuffer);
        while (matches.hasNext())
            embedded += matches.next().captured(1);
        if (embedded.size() > m_embeddedReasoningLength) {
            const QString delta = embedded.mid(m_embeddedReasoningLength);
            m_embeddedReasoningLength = embedded.size();
            appendReasoningSummary(delta);
        }
    }

    QString answerProbe = m_streamBuffer;
    answerProbe.remove(expression);
    const QString trimmedProbe = answerProbe.trimmed();
    const bool partialThinkTag = trimmedProbe.startsWith('<')
        && QStringLiteral("<think>").startsWith(trimmedProbe, Qt::CaseInsensitive);
    const bool hasAnswerContent = !trimmedProbe.isEmpty() && !partialThinkTag;
    if (hasAnswerContent && !m_generationActivityShown) {
        m_generationActivityShown = true;
        m_liveReasoningLine.clear();
        reportActivity(QStringLiteral("El modelo está generando la respuesta"));
    } else if (hasAnswerContent && !m_receivingActivityShown
               && answerProbe.size() >= 160) {
        m_receivingActivityShown = true;
        reportActivity(QStringLiteral("Recibiendo y organizando la respuesta"));
    }
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
