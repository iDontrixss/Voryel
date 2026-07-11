#include "SettingsView.h"
#include "Style.h"
#include "SolidPanel.h"
#include "IconUtil.h"
#include "model/ProviderUrlSecurity.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QAbstractItemView>
#include "ToggleSwitch.h"
#include <QFrame>
#include <QEvent>
#include <QSizePolicy>
#include <QApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QWheelEvent>
#include <QScrollBar>
#include <QMouseEvent>
#include <QSettings>
#include <QTimer>
#include <QDebug>
#include <functional>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {

QLabel* makeText(const QString &text, int px, const QString &color, int weight = 600) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet(QString(
        "font-size: %1px; font-weight: %2; color: %3; border: none; background: transparent;"
    ).arg(px).arg(weight).arg(color));
    return label;
}

QLabel* makeIcon(const QString &iconRes, const QColor &tint, int size = 15) {
    auto *label = new QLabel();
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    label->setPixmap(IconUtil::coloredIcon(iconRes, tint, QSize(size, size)).pixmap(size, size));
    label->setStyleSheet("border: none; background: transparent;");
    return label;
}

QLabel* makeDot(const QString &color, int size = 8) {
    auto *dot = new QLabel();
    dot->setAttribute(Qt::WA_StyledBackground, true);
    dot->setFixedSize(size, size);
    dot->setStyleSheet(QString("background: %1; border-radius: %2px; border: none;").arg(color).arg(size / 2));
    return dot;
}

QString smallButtonStyle(const QString &bg, const QString &fg, const QString &border, const QString &hover = QString()) {
    return QString(
        "QPushButton { background: %1; color: %2; border: 2px solid %3; border-radius: 9px;"
        "  padding: 6px 12px; font-size: 12px; font-weight: 900; }"
        "QPushButton:hover { background: %4; }"
    ).arg(bg, fg, border, hover.isEmpty() ? Style::BG_LILAC : hover);
}

// Preset base URLs for known cloud providers
static QString presetBaseUrl(const QString &providerId) {
    static const QMap<QString, QString> presets = {
        { "openrouter", "https://openrouter.ai/api/v1" },
        { "groq",       "https://api.groq.com/openai/v1" },
        { "openai",     "https://api.openai.com/v1" },
        { "deepseek",   "https://api.deepseek.com/v1" },
        { "ollama",     "http://127.0.0.1:11434/v1" },
    };
    return presets.value(providerId);
}

QString protectSecret(const QString &plainText) {
    if (plainText.isEmpty()) return QString();
#if defined(Q_OS_WIN)
    const QByteArray input = plainText.toUtf8();
    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(input.constData()));
    in.cbData = static_cast<DWORD>(input.size());
    DATA_BLOB out;
    if (CryptProtectData(&in, L"Voryel API key", nullptr, nullptr, nullptr, 0, &out)) {
        const QByteArray encrypted(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
        LocalFree(out.pbData);
        return "dpapi:" + QString::fromLatin1(encrypted.toBase64());
    }
#endif
    return "plain:" + QString::fromUtf8(plainText.toUtf8().toBase64());
}

QString unprotectSecret(const QString &storedText) {
    if (storedText.isEmpty()) return QString();
#if defined(Q_OS_WIN)
    if (storedText.startsWith("dpapi:")) {
        const QByteArray encrypted = QByteArray::fromBase64(storedText.mid(6).toLatin1());
        DATA_BLOB in;
        in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()));
        in.cbData = static_cast<DWORD>(encrypted.size());
        DATA_BLOB out;
        if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
            const QByteArray plain(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
            LocalFree(out.pbData);
            return QString::fromUtf8(plain);
        }
        return QString();
    }
#endif
    if (storedText.startsWith("plain:"))
        return QString::fromUtf8(QByteArray::fromBase64(storedText.mid(6).toLatin1()));
    return storedText;
}

void clearLayout(QLayout *layout) {
    if (!layout) return;
    while (auto *item = layout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        if (item->layout()) {
            clearLayout(item->layout());
        }
        delete item;
    }
}

// Central function to classify model availability based on provider + metadata
static QString classifyAvailability(const QString &providerId, const QString &modelId,
                                     const QString &promptPrice, const QString &completionPrice) {
    // Local providers
    if (providerId == "lm_studio" || providerId == "ollama")
        return "Local";

    // Groq: free tier with usage limits
    if (providerId == "groq")
        return "Gratis con l\u00EDmites";

    // OpenRouter: depends on :free suffix and pricing metadata
    if (providerId == "openrouter") {
        if (modelId.contains(":free", Qt::CaseInsensitive))
            return "Gratis";
        if (!promptPrice.isEmpty() || !completionPrice.isEmpty()) {
            if (promptPrice == "0" && completionPrice == "0")
                return "Gratis";
            return "Pago";
        }
        return "Costo seg\u00FAn proveedor";
    }

    // OpenAI: paid by default
    if (providerId == "openai")
        return "Pago";

    // DeepSeek: paid by default
    if (providerId == "deepseek")
        return "Costo seg\u00FAn proveedor";

    // Custom / fallback: no pricing info
    return "No informado";
}

} // namespace

SettingsView::SettingsView(QWidget *parent) : QWidget(parent) {
    // Available providers
    m_providers = {
        { "lm_studio",  "LM Studio",  "Local",    "Servidor local con tus modelos.",               true,  false, false, true, "http://127.0.0.1:1234/v1", "" },
        { "ollama",     "Ollama",     "Local",    "Abierto local con API compatible OpenAI.",       true,  false, false, false, "", "" },
        { "openrouter", "OpenRouter", "Cloud",    "Múltiples modelos con una sola API.",            false, true,  false, false, "", "" },
        { "groq",       "Groq",       "Cloud",    "Inferencia rápida gratis con API key.",          false, true,  false, false, "", "" },
        { "openai",     "OpenAI",     "Cloud",    "Modelos GPT-4o, GPT-4, etc.",                    false, true,  false, false, "", "" },
        { "deepseek",   "DeepSeek",   "Cloud",    "Modelo razonador R1 y V3.",                      false, true,  false, false, "", "" },
        { "gemini",     "Gemini",     "Cloud",    "Modelos de Google. Integración futura.",         false, true,  true,  false, "", "" },
        { "claude",     "Claude",     "Cloud",    "Modelos de Anthropic. Integración futura.",      false, true,  true,  false, "", "" },
    };

    loadSettings();
    m_activeModel = "Sin modelo";
    if (!m_activeModelId.isEmpty()) {
        const int activeProviderIndex = providerIndexById(m_activeProviderId);
        if (activeProviderIndex >= 0) {
            QString display = prettyModelName(m_activeModelId);
            if (display.isEmpty()) display = m_activeModelId;
            m_activeModel = m_providers[activeProviderIndex].displayName + " · " + display;
        }
    }
    buildUi();
}

void SettingsView::buildUi() {
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(Style::scrollAreaStyle());
    scroll->viewport()->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));

    auto *outer = new QWidget();
    outer->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));
    auto *outerLayout = new QHBoxLayout(outer);
    outerLayout->setContentsMargins(Style::PAGE_MARGIN_X, 28, Style::PAGE_MARGIN_X, 52);
    outerLayout->setSpacing(0);
    outerLayout->addStretch(1);

    auto *content = new QWidget();
    // Keep the original TSX-like vertical reading order, but give the column
    // more room on desktop/fullscreen. This uses the horizontal space without
    // moving sections into a different two-column layout.
    content->setMaximumWidth(1080);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    content->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(30);

    layout->addWidget(makeHeader());
    layout->addWidget(makeProvidersSection());
    layout->addWidget(makeModelsSection());
    layout->addWidget(makeFallbackSection());
    layout->addWidget(makePermissionSection());
    layout->addWidget(makeSecuritySection());
    layout->addWidget(makeAppearanceSection());
    layout->addStretch(1);

    outerLayout->addWidget(content, 1, Qt::AlignTop);
    outerLayout->addStretch(1);

    scroll->setWidget(outer);
    root->addWidget(scroll, 1);

    refreshPermissionCards();
    refreshThemeCards();
}

QWidget* SettingsView::makeHeader() {
    auto *wrap = new QWidget();
    wrap->setStyleSheet("background: transparent; border: none;");
    auto *layout = new QVBoxLayout(wrap);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    layout->addWidget(makeText("Configuración", 26, Style::INK, 950));
    layout->addWidget(makeText("Proveedores, modelos, fallbacks, permisos, seguridad y apariencia de Voryel.", 13, Style::TEXT_MUTED, 700));
    return wrap;
}

QWidget* SettingsView::makeSectionTitle(const QString &iconRes, const QString &title) {
    auto *row = new QWidget();
    row->setStyleSheet("background: transparent; border: none;");
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(makeIcon(iconRes, QColor(Style::VIOLET), 13), 0, Qt::AlignVCenter);

    auto *label = new QLabel(title.toUpper());
    label->setStyleSheet(QString(
        "font-size: 11px; font-weight: 950; letter-spacing: 1.8px; color: %1; background: transparent; border: none;"
    ).arg(Style::TEXT_MUTED));
    layout->addWidget(label, 1);
    return row;
}

QWidget* SettingsView::makeModelsSection() {
    auto *section = new QWidget();
    section->setStyleSheet("background: transparent; border: none;");
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(makeSectionTitle(":/icons/icons/cpu.svg", "Modelos"));

    // Active model display
    auto *activeCard = new SolidPanel();
    activeCard->setFillColor(QColor(Style::VIOLET_LIGHT));
    activeCard->setCornerRadius(12);
    activeCard->setFullBorder(QColor(Style::INK), 2);
    activeCard->setHardShadow(QColor(Style::INK), 2, 2);
    activeCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *activeLayout = new QHBoxLayout(activeCard);
    activeLayout->setContentsMargins(16, 12, 16, 12);
    activeLayout->setSpacing(10);
    auto *activeDot = new QLabel();
    activeDot->setAttribute(Qt::WA_StyledBackground, true);
    activeDot->setFixedSize(10, 10);
    activeDot->setStyleSheet(QString("background: %1; border-radius: 5px; border: none;").arg(Style::GREEN));
    activeLayout->addWidget(activeDot, 0, Qt::AlignVCenter);
    QString activeDisplay = m_activeModel.isEmpty() || m_activeModel == "Sin modelo"
        ? "Sin modelo activo" : m_activeModel;
    auto *activeLabel = makeText(activeDisplay, 14, Style::INK, 900);
    activeLayout->addWidget(activeLabel, 1);
    activeLayout->addWidget(makeText("Activo", 11, Style::GREEN, 950));
    layout->addWidget(activeCard);

    // Dynamic quick models container
    auto *modelsContainer = new QWidget();
    modelsContainer->setStyleSheet("background: transparent; border: none;");
    m_modelsListLayout = new QVBoxLayout(modelsContainer);
    m_modelsListLayout->setContentsMargins(0, 0, 0, 0);
    m_modelsListLayout->setSpacing(10);
    layout->addWidget(modelsContainer);
    refreshModelsList();

    // Add model button
    auto *addBtn = makeGhostButton("+ Agregar modelo");
    connect(addBtn, &QPushButton::clicked, this, [this, layout]() {
        if (m_addModelPanel) {
            closeAddModelPanel();
        }
        if (m_quickModels.size() >= MAX_QUICK_MODELS) {
            emit statusMessageRequested("Máximo 5 modelos de acceso rápido.");
            return;
        }
        auto *dlg = makeAddModelDialog();
        if (dlg) {
            layout->addWidget(dlg);
            m_addModelPanel = dlg;
            QApplication::instance()->installEventFilter(this);
        }
    });
    layout->addWidget(addBtn);

    return section;
}

void SettingsView::closeAddModelPanel() {
    if (m_addModelPanel) {
        QApplication::instance()->removeEventFilter(this);
        m_addModelPanel->setEnabled(false);
        m_addModelPanel->deleteLater();
        m_addModelPanel = nullptr;
    }
}

void SettingsView::finishAddingQuickModel() {
    const QString status = m_pendingQuickModelStatus;
    m_pendingQuickModelStatus.clear();
    closeAddModelPanel();
    saveSettings();
    refreshModelsList();
    if (!status.isEmpty())
        emit statusMessageRequested(status);
    emit providerConfigChanged();
}

void SettingsView::refreshModelsList() {
    if (!m_modelsListLayout) return;
    clearLayout(m_modelsListLayout);

    if (m_quickModels.isEmpty()) {
        auto *emptyCard = new SolidPanel();
        emptyCard->setFillColor(QColor(Style::WHITE));
        emptyCard->setCornerRadius(12);
        emptyCard->setFullBorder(QColor(Style::INK), 2);
        emptyCard->setHardShadow(QColor(Style::INK), 2, 2);
        auto *emptyLayout = new QVBoxLayout(emptyCard);
        emptyLayout->setContentsMargins(16, 14, 16, 14);
        emptyLayout->setSpacing(4);
        emptyLayout->addWidget(makeText("Conectá un proveedor para agregar modelos.", 12, Style::TEXT_MUTED, 700));
        emptyLayout->addWidget(makeText("Podés tener hasta 5 modelos de acceso rápido.", 11, Style::TEXT_FAINT, 600));
        m_modelsListLayout->addWidget(emptyCard);
    } else {
        auto *quickLabel = makeText(QString("Acceso rápido %1/%2").arg(m_quickModels.size()).arg(MAX_QUICK_MODELS), 12, Style::TEXT_MUTED, 800);
        m_modelsListLayout->addWidget(quickLabel);
        for (int i = 0; i < m_quickModels.size(); ++i) {
            m_modelsListLayout->addWidget(makeQuickModelRow(i));
        }
    }
}

QWidget* SettingsView::makeQuickModelRow(int idx) {
    if (idx < 0 || idx >= m_quickModels.size()) return new QWidget();
    const auto &qm = m_quickModels[idx];

    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(12);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 2, 2);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    auto *rowLayout = new QHBoxLayout(card);
    rowLayout->setContentsMargins(14, 10, 14, 10);
    rowLayout->setSpacing(10);

    rowLayout->addWidget(makeIcon(":/icons/icons/cpu.svg", QColor(Style::VIOLET), 13), 0, Qt::AlignVCenter);
    QString display = qm.displayName.isEmpty() ? prettyModelName(qm.modelId) : qm.displayName;
    if (display.isEmpty()) display = qm.modelId;
    QString label = qm.providerName + " · " + display;
    rowLayout->addWidget(makeText(label, 13, Style::INK, 850), 1);

    // Availability badge
    if (!qm.availability.isEmpty()) {
        QString badgeColor;
        if (qm.availability == "Local") badgeColor = Style::VIOLET;
        else if (qm.availability == "Gratis") badgeColor = Style::GREEN;
        else if (qm.availability == "Pago") badgeColor = "#d97706";
        else badgeColor = Style::TEXT_FAINT;
        auto *badge = makeText(qm.availability, 9, badgeColor, 900);
        QString badgeBg = (qm.availability == "Local") ? (QString(Style::VIOLET) + "22")
            : (qm.availability == "Gratis") ? (QString(Style::GREEN) + "22")
            : (qm.availability == "Pago") ? "#d9770622"
            : (QString(Style::TEXT_FAINT) + "22");
        badge->setStyleSheet(badge->styleSheet() + QString(
            " background: %1; border-radius: 4px; padding: 1px 5px;"
        ).arg(badgeBg));
        rowLayout->addWidget(badge, 0, Qt::AlignVCenter);
    }

    // Active indicator
    bool isActive = (qm.providerId == m_activeProviderId && qm.modelId == m_activeModelId);
    if (isActive) {
        rowLayout->addWidget(makeText("En uso", 10, Style::VIOLET, 950));
    } else {
        auto *useBtn = makeGhostButton("Usar");
        useBtn->setFixedWidth(60);
        connect(useBtn, &QPushButton::clicked, this, [this, qm]() {
            m_activeProviderId = qm.providerId;
            m_activeModelId = qm.modelId;
            setActiveModelFromProvider();
            saveSettings();
            emit activeModelChanged(m_activeProviderId, m_activeModelId);
            emit providerConfigChanged();
            emit statusMessageRequested("Modelo activo: " + qm.providerName + " · " + qm.modelId);
        });
        rowLayout->addWidget(useBtn, 0, Qt::AlignVCenter);
    }

    auto *removeBtn = new QPushButton();
    removeBtn->setFixedSize(24, 24);
    removeBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setStyleSheet(QString(
        "QPushButton { background: transparent; border: none; color: %1; font-size: 16px; font-weight: 900; }"
        "QPushButton:hover { color: #e53935; }"
    ).arg(Style::TEXT_MUTED));
    removeBtn->setText(QString::fromUtf8("\u00D7"));
    connect(removeBtn, &QPushButton::clicked, this, [this, idx]() {
        if (idx >= 0 && idx < m_quickModels.size()) {
            QString removedId = m_quickModels[idx].id;
            const bool removedActive = (m_quickModels[idx].providerId == m_activeProviderId
                                     && m_quickModels[idx].modelId == m_activeModelId);
            m_quickModels.remove(idx);
            if (removedActive) {
                if (m_quickModels.isEmpty()) {
                    m_activeModelId.clear();
                    m_activeModel = "Sin modelo";
                } else {
                    m_activeProviderId = m_quickModels.first().providerId;
                    m_activeModelId = m_quickModels.first().modelId;
                    setActiveModelFromProvider();
                }
            }
            saveSettings();
            refreshModelsList();
            emit providerConfigChanged();
            emit statusMessageRequested("Modelo quitado del acceso rápido.");
        }
    });
    rowLayout->addWidget(removeBtn, 0, Qt::AlignVCenter);

    return card;
}

QWidget* SettingsView::makeAddModelDialog() {
    // Find configured providers
    QVector<int> configuredProviders;
    for (int i = 0; i < m_providers.size(); ++i) {
        if (m_providers[i].configured && !m_providers[i].comingSoon) {
            configuredProviders.push_back(i);
        }
    }

    if (configuredProviders.isEmpty()) {
        emit statusMessageRequested("Conectá un proveedor primero.");
        return nullptr;
    }

    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(12);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 2, 2);
    card->setProperty("addModelPanel", true);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    layout->addWidget(makeText("Agregar modelo", 14, Style::INK, 900));

    // ── Provider selector ──
    auto *providerLabel = makeText("Proveedor:", 12, Style::TEXT_MUTED, 800);
    layout->addWidget(providerLabel);

    auto *providerCombo = new QComboBox();
    for (int idx : configuredProviders) {
        providerCombo->addItem(m_providers[idx].displayName, idx);
    }
    // Combo style with explicit dropdown styling (fixes dark dropdown)
    providerCombo->setStyleSheet(QString(
        "QComboBox { background: %1; color: %2; border: 2px solid %3; border-radius: 8px;"
        "  padding: 6px 10px; font-size: 12px; font-weight: 700; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox::down-arrow { image: none; }"
        "QComboBox QAbstractItemView {"
        "  background: %4; color: %2; border: 2px solid %3; border-radius: 4px;"
        "  selection-background-color: %5; selection-color: %2;"
        "  outline: none;"
        "}"
    ).arg(Style::BG_LILAC, Style::INK, Style::INK, Style::WHITE, Style::VIOLET_LIGHT));
    layout->addWidget(providerCombo);

    // ── Model detection area ──
    auto *detectArea = new QWidget();
    detectArea->setStyleSheet("background: transparent; border: none;");
    auto *detectLayout = new QVBoxLayout(detectArea);
    detectLayout->setContentsMargins(0, 0, 0, 0);
    detectLayout->setSpacing(8);
    layout->addWidget(detectArea);

    // Loading label (hidden initially)
    auto *loadingLabel = makeText("Detectando modelos...", 12, Style::TEXT_MUTED, 700);
    loadingLabel->setVisible(false);
    detectLayout->addWidget(loadingLabel);

    // Results container: will be populated with model rows
    auto *resultLayout = new QVBoxLayout();
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(4);
    detectLayout->addLayout(resultLayout);

    // Manual fallback: Model ID input (hidden initially, shown if detection fails)
    auto *manualLabel = makeText("O escribí el Model ID manualmente:", 11, Style::TEXT_FAINT, 700);
    manualLabel->setObjectName("_manualLabel");
    manualLabel->setVisible(false);
    detectLayout->addWidget(manualLabel);

    auto *modelIdEdit = new QLineEdit();
    modelIdEdit->setObjectName("_manualModelIdEdit");
    modelIdEdit->setPlaceholderText("Ej: qwen2.5-coder-0.5b-instruct");
    modelIdEdit->setMinimumHeight(32);
    modelIdEdit->setVisible(false);
    modelIdEdit->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 2px solid %3; border-radius: 8px;"
        "  padding: 4px 10px; font-size: 12px; font-weight: 600; font-family: 'Consolas','Courier New',monospace; }"
        "QLineEdit:focus { border-color: %4; }"
    ).arg(Style::BG_LILAC, Style::INK, Style::BORDER_SOFT, Style::VIOLET));
    detectLayout->addWidget(modelIdEdit);

    // Error label (hidden initially)
    auto *errorLabel = makeText("", 11, "#e53935", 700);
    errorLabel->setObjectName("_detectErrorLabel");
    errorLabel->setVisible(false);
    errorLabel->setWordWrap(true);
    detectLayout->addWidget(errorLabel);

    // ── Buttons ──
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);

    auto *cancelBtn = makeGhostButton("Cancelar");
    auto *addModelBtn = makePrimaryButton("Agregar a acceso rápido");
    addModelBtn->setEnabled(false); // Disabled until a model is selected
    btnRow->addWidget(cancelBtn, 1);
    btnRow->addWidget(addModelBtn, 1);
    layout->addLayout(btnRow);

    // ── When provider changes, fetch models ──
    auto onProviderChanged = [this, providerCombo, resultLayout, loadingLabel, errorLabel,
                              manualLabel, modelIdEdit, addModelBtn, card]() {
        int pi = providerCombo->currentData().toInt();
        if (pi < 0 || pi >= m_providers.size()) return;
        const auto &prov = m_providers[pi];

        // Clear previous results
        clearLayout(resultLayout);
        errorLabel->setVisible(false);
        manualLabel->setVisible(false);
        modelIdEdit->setVisible(false);
        modelIdEdit->clear();
        addModelBtn->setEnabled(false);

        // Check if provider has API key if required
        if (prov.requiresApiKey && prov.apiKey.isEmpty()) {
            errorLabel->setText("Este proveedor requiere API key. Configurá la API key en Proveedores.");
            errorLabel->setVisible(true);
            return;
        }

        // Show loading
        loadingLabel->setVisible(true);

        // Fetch models
        fetchModelsForProvider(pi, providerCombo, resultLayout, loadingLabel);
    };
    connect(providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, onProviderChanged);

    // ── Add model button (manual entry only; per-row buttons handle detected models) ──
    connect(addModelBtn, &QPushButton::clicked, this, [this, providerCombo, modelIdEdit]() {
        QString modelId = modelIdEdit->text().trimmed();
        if (!modelIdEdit->isVisible() || modelId.isEmpty()) {
            emit statusMessageRequested("Escribí un Model ID.");
            return;
        }
        int pi = providerCombo->currentData().toInt();
        if (pi < 0 || pi >= m_providers.size()) {
            emit statusMessageRequested("Seleccioná un proveedor.");
            return;
        }
        const auto &prov = m_providers[pi];
        if (prov.requiresApiKey && prov.apiKey.isEmpty()) {
            emit statusMessageRequested("Este proveedor requiere API key. Configurá la API key en Proveedores.");
            return;
        }
        for (const auto &qm : m_quickModels) {
            if (qm.providerId == prov.id && qm.modelId == modelId) {
                emit statusMessageRequested("Este modelo ya está en acceso rápido.");
                return;
            }
        }
        if (m_quickModels.size() >= MAX_QUICK_MODELS) {
            emit statusMessageRequested("Ya tenés 5 modelos de acceso rápido. Quitá uno para agregar otro.");
            return;
        }

        QuickModelEntry qm;
        qm.id = prov.id + ":" + modelId;
        qm.providerId = prov.id;
        qm.providerName = prov.displayName;
        qm.modelId = modelId;
        qm.displayName = prettyModelName(modelId);
        qm.availability = classifyAvailability(prov.id, modelId, QString(), QString());
        m_quickModels.append(qm);

        if (m_activeModelId.isEmpty()) {
            m_activeProviderId = prov.id;
            m_activeModelId = modelId;
            setActiveModelFromProvider();
            emit activeModelChanged(m_activeProviderId, m_activeModelId);
        }

        m_pendingQuickModelStatus = "Modelo agregado: " + prov.displayName + " · " + modelId;
        QTimer::singleShot(0, this, &SettingsView::finishAddingQuickModel);
    });

    // ── Manual entry enables add button when text is entered ──
    connect(modelIdEdit, &QLineEdit::textChanged, this, [addModelBtn, modelIdEdit]() {
        if (!modelIdEdit->isVisible()) return;
        addModelBtn->setEnabled(!modelIdEdit->text().trimmed().isEmpty());
    });

    // Cancel closes panel
    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        closeAddModelPanel();
    });

    // Auto-trigger detection for the first provider
    QTimer::singleShot(0, this, [onProviderChanged]() { onProviderChanged(); });

    return card;
}

void SettingsView::fetchModelsForProvider(int providerIdx, QComboBox *combo, QVBoxLayout *resultLayout, QWidget *loadingLabel) {
    if (providerIdx < 0 || providerIdx >= m_providers.size()) return;
    const auto &prov = m_providers[providerIdx];

    QString baseUrl = prov.baseUrl;
    if (baseUrl.isEmpty()) {
        baseUrl = presetBaseUrl(prov.id);
    }
    if (baseUrl.isEmpty()) {
        emit statusMessageRequested("No hay URL configurada para " + prov.displayName);
        return;
    }

    // Ensure trailing slash is correct for /models endpoint
    if (!baseUrl.endsWith("/v1") && !baseUrl.endsWith("/v1/")) {
        baseUrl += "/v1";
    }

    if (!m_networkManager) {
        m_networkManager = new QNetworkAccessManager(this);
    }

    if (!isSafeProviderUrlForApiKey(baseUrl, prov.apiKey)) {
        if (loadingLabel) loadingLabel->setVisible(false);
        emit statusMessageRequested("No se puede enviar una API key con HTTP externo. Usa HTTPS o un proveedor local.");
        return;
    }

    QUrl url(baseUrl + "/models");
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "Voryel/1.0");
    if (!prov.apiKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + prov.apiKey).toUtf8());
    }

    QNetworkReply *reply = m_networkManager->get(request);

    // Store provider info for the lambda
    struct FetchCtx {
        int providerIdx;
        bool isLocal;
        QString providerId;
        QString providerName;
    };
    auto *ctx = new FetchCtx{ providerIdx, prov.isLocal, prov.id, prov.displayName };

    connect(reply, &QNetworkReply::finished, this, [this, reply, resultLayout, loadingLabel, combo, ctx]() {
        reply->deleteLater();
        if (!m_addModelPanel) {
            delete ctx;
            return;
        }

        loadingLabel->setVisible(false);
        QString provId = ctx->providerId;
        QString provName = ctx->providerName;

        if (reply->error() != QNetworkReply::NoError) {
            if (m_addModelPanel) {
                auto *detectArea = resultLayout->parentWidget();
                if (!detectArea) detectArea = m_addModelPanel;
                auto allLabels = detectArea->findChildren<QLabel*>();
                for (auto *l : allLabels) {
                    if (l->objectName() == "_manualLabel" || l->objectName() == "_detectErrorLabel") {
                        l->setVisible(true);
                        if (l->objectName() == "_detectErrorLabel")
                            l->setText("Error al detectar modelos. Usá el modo manual.");
                    }
                }
                auto allEdits = detectArea->findChildren<QLineEdit*>();
                for (auto *e : allEdits) {
                    if (e->objectName() == "_manualModelIdEdit") {
                        e->setVisible(true);
                    }
                }
            }
            delete ctx;
            return;
        }

        QByteArray data = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            auto *errMsg = makeText("Respuesta inválida del servidor. Usá el modo manual.", 11, "#e53935", 700);
            resultLayout->addWidget(errMsg);
            delete ctx;
            return;
        }

        QJsonObject obj = doc.object();
        QJsonArray modelsArray = obj["data"].toArray();
        if (modelsArray.isEmpty()) {
            auto *errMsg = makeText("No se encontraron modelos. Usá el modo manual.", 11, "#e53935", 700);
            resultLayout->addWidget(errMsg);
            delete ctx;
            return;
        }

        // ── Build models list using central classifyAvailability ──
        struct DetectedModel {
            QString id;
            QString displayName;
            QString availability; // classified badge text
        };
        QVector<DetectedModel> models;

        for (const auto &mVal : modelsArray) {
            QJsonObject mObj = mVal.toObject();
            QString id = mObj["id"].toString();
            if (id.isEmpty()) continue;

            QJsonObject pricing = mObj["pricing"].toObject();
            QString promptPrice = pricing["prompt"].toString();
            QString completionPrice = pricing["completion"].toString();

            DetectedModel dm;
            dm.id = id;
            dm.displayName = prettyModelName(id);
            dm.availability = classifyAvailability(provId, id, promptPrice, completionPrice);
            models.append(dm);
        }

        if (models.isEmpty()) {
            auto *errMsg = makeText("No se encontraron modelos. Usá el modo manual.", 11, "#e53935", 700);
            resultLayout->addWidget(errMsg);
            delete ctx;
            return;
        }

        // ── Scrollable model list ──
        auto *modelScroll = new QScrollArea();
        modelScroll->setWidgetResizable(true);
        modelScroll->setFrameShape(QFrame::NoFrame);
        modelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        modelScroll->setMaximumHeight(220);
        modelScroll->setStyleSheet(
            "QScrollArea { background: transparent; border: 1px solid #e4dff2; border-radius: 6px; }"
            "QScrollArea::viewport { background: transparent; }"
            "QScrollBar:vertical { background: transparent; width: 6px; margin: 2px; }"
            "QScrollBar::handle:vertical { background: #d8b4fe; border-radius: 3px; min-height: 20px; }"
            "QScrollBar::handle:vertical:hover { background: #7c3aed; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        );

        // Install event filter on the model scroll viewport to trap wheel events
        modelScroll->viewport()->installEventFilter(this);
        modelScroll->viewport()->setProperty("_modelListScroll", true);

        auto *modelListWidget = new QWidget();
        modelListWidget->setStyleSheet("background: transparent; border: none;");
        auto *modelListLayout = new QVBoxLayout(modelListWidget);
        modelListLayout->setContentsMargins(6, 4, 6, 4);
        modelListLayout->setSpacing(2);

        // ── Search filter ──
        auto *searchEdit = new QLineEdit();
        searchEdit->setPlaceholderText("Buscar modelos...");
        searchEdit->setMinimumHeight(28);
        searchEdit->setStyleSheet(QString(
            "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 6px;"
            "  padding: 3px 8px; font-size: 11px; font-weight: 600; }"
            "QLineEdit:focus { border-color: %4; }"
        ).arg(Style::BG_LILAC, Style::INK, Style::BORDER_SOFT, Style::VIOLET));
        modelListLayout->addWidget(searchEdit);

        // Container for model rows
        auto *modelRowsWidget = new QWidget();
        modelRowsWidget->setStyleSheet("background: transparent; border: none;");
        auto *modelRowsLayout = new QVBoxLayout(modelRowsWidget);
        modelRowsLayout->setContentsMargins(0, 0, 0, 0);
        modelRowsLayout->setSpacing(2);
        modelListLayout->addWidget(modelRowsWidget, 1);

        modelScroll->setWidget(modelListWidget);
        resultLayout->addWidget(modelScroll);

        // Helper: add a model row
        std::function<void(const DetectedModel&)> addRow = [this, modelRowsLayout, provId, provName](const DetectedModel &dm) {
            auto *row = new QWidget();
            row->setStyleSheet("background: transparent; border: none;");
            auto *rowL = new QHBoxLayout(row);
            rowL->setContentsMargins(4, 3, 4, 3);
            rowL->setSpacing(4);

            // Name label
            rowL->addWidget(makeText(provName + " · " + dm.displayName,
                                     11, Style::INK, 700), 1);
            // Badge
            QString badgeColor;
            if (dm.availability == "Local")
                badgeColor = Style::VIOLET;
            else if (dm.availability == "Gratis")
                badgeColor = Style::GREEN;
            else if (dm.availability == "Pago")
                badgeColor = "#d97706";
            else
                badgeColor = Style::TEXT_FAINT;
            auto *badge = makeText(dm.availability, 9, badgeColor, 900);
            QString badgeBg = (dm.availability == "Local") ? (QString(Style::VIOLET) + "22")
                : (dm.availability == "Gratis") ? (QString(Style::GREEN) + "22")
                : (dm.availability == "Pago") ? "#d9770622"
                : (QString(Style::TEXT_FAINT) + "22");
            badge->setStyleSheet(badge->styleSheet() + QString(
                " background: %1; border-radius: 4px; padding: 1px 5px;"
            ).arg(badgeBg));
            rowL->addWidget(badge, 0, Qt::AlignVCenter);

            // ── Per-row Agregar button ──
            auto *addRowBtn = new QPushButton("Agregar");
            addRowBtn->setFixedWidth(68);
            addRowBtn->setFixedHeight(24);
            addRowBtn->setCursor(Qt::PointingHandCursor);
            addRowBtn->setStyleSheet(QString(
                "QPushButton { background: %1; color: %2; border: 1.5px solid %3; border-radius: 5px;"
                "  font-size: 10px; font-weight: 800; padding: 2px 6px; }"
                "QPushButton:hover { background: %4; }"
            ).arg(Style::BG_LILAC, Style::INK, Style::INK, Style::VIOLET_LIGHT));
            rowL->addWidget(addRowBtn, 0, Qt::AlignVCenter);

            // Connect the per-row add button
            QString modelId = dm.id;
            QString displayName = dm.displayName;
            QString availability = dm.availability;
            connect(addRowBtn, &QPushButton::clicked, this, [this, provId, provName, modelId, displayName, availability]() {

                // Validate provider configured
                int pi = -1;
                for (int i = 0; i < m_providers.size(); ++i) {
                    if (m_providers[i].id == provId) { pi = i; break; }
                }
                if (pi < 0 || !m_providers[pi].configured) {
                    emit statusMessageRequested("El proveedor no está configurado.");
                    return;
                }
                const auto &prov = m_providers[pi];
                if (prov.requiresApiKey && prov.apiKey.isEmpty()) {
                    emit statusMessageRequested("Este proveedor requiere API key. Configurá la API key en Proveedores.");
                    return;
                }
                if (m_activeProviderId.isEmpty()) {
                    emit statusMessageRequested("Elegí un proveedor primero.");
                    return;
                }
                if (modelId.isEmpty()) {
                    emit statusMessageRequested("Elegí un modelo antes de agregarlo.");
                    return;
                }
                // Duplicate check
                for (const auto &qm : m_quickModels) {
                    if (qm.providerId == provId && qm.modelId == modelId) {
                        emit statusMessageRequested("Este modelo ya está en acceso rápido.");
                        return;
                    }
                }
                if (m_quickModels.size() >= MAX_QUICK_MODELS) {
                    emit statusMessageRequested("Ya tenés 5 modelos de acceso rápido. Quitá uno para agregar otro.");
                    return;
                }

                QuickModelEntry qm;
                qm.id = provId + ":" + modelId;
                qm.providerId = provId;
                qm.providerName = provName;
                qm.modelId = modelId;
                qm.displayName = displayName;
                qm.availability = availability;
                m_quickModels.append(qm);

                if (m_activeModelId.isEmpty()) {
                    m_activeProviderId = provId;
                    m_activeModelId = modelId;
                    setActiveModelFromProvider();
                    emit activeModelChanged(m_activeProviderId, m_activeModelId);
                }

                m_pendingQuickModelStatus = "Modelo agregado: " + provName + " · " + modelId;
                QTimer::singleShot(0, this, &SettingsView::finishAddingQuickModel);
            });

            modelRowsLayout->addWidget(row);
        };

        // ── Add all models initially ──
        for (const auto &dm : models)
            addRow(dm);

        // ── Search filter: show/hide rows ──
        connect(searchEdit, &QLineEdit::textChanged, this, [modelRowsLayout, searchEdit, models, this, provId, provName, addRow]() {
            QString filter = searchEdit->text().trimmed().toLower();
            // Rebuild the rows based on filter
            clearLayout(modelRowsLayout);
            if (filter.isEmpty()) {
                for (const auto &dm : models)
                    addRow(dm);
            } else {
                bool any = false;
                for (const auto &dm : models) {
                    if (dm.id.contains(filter, Qt::CaseInsensitive) || dm.displayName.contains(filter, Qt::CaseInsensitive)) {
                        addRow(dm);
                        any = true;
                    }
                }
                if (!any) {
                    auto *noRes = makeText("No se encontraron modelos.", 11, Style::TEXT_FAINT, 700);
                    modelRowsLayout->addWidget(noRes);
                }
            }
        });

        // ── No results message after addRow called ──

        delete ctx;
    });
}

QWidget* SettingsView::makeFallbackSection() {
    auto *section = new QWidget();
    section->setStyleSheet("background: transparent; border: none;");
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(makeSectionTitle(":/icons/icons/history.svg", "Fallbacks"));

    // Enable toggle
    auto *enableToggle = new SolidPanel();
    enableToggle->setFillColor(QColor(Style::WHITE));
    enableToggle->setCornerRadius(12);
    enableToggle->setFullBorder(QColor(Style::INK), 2);
    enableToggle->setHardShadow(QColor(Style::INK), 2, 2);
    auto *enableLayout = new QHBoxLayout(enableToggle);
    enableLayout->setContentsMargins(16, 12, 16, 13);
    enableLayout->setSpacing(14);
    auto *fs = new ToggleSwitch();
    fs->setOn(m_fallbacksEnabled, false);
    enableLayout->addWidget(fs, 0, Qt::AlignVCenter);
    enableLayout->addWidget(makeText("Activar fallbacks", 13, Style::INK, 850), 1);
    connect(fs, &ToggleSwitch::toggled, this, [this](bool on) {
        m_fallbacksEnabled = on;
    });
    layout->addWidget(enableToggle);

    if (m_quickModels.size() < 2) {
        auto *emptyCard = new SolidPanel();
        emptyCard->setFillColor(QColor(Style::WHITE));
        emptyCard->setCornerRadius(12);
        emptyCard->setFullBorder(QColor(Style::INK), 2);
        emptyCard->setHardShadow(QColor(Style::INK), 2, 2);
        auto *emptyL = new QVBoxLayout(emptyCard);
        emptyL->setContentsMargins(16, 14, 16, 14);
        emptyL->setSpacing(4);
        emptyL->addWidget(makeText("Necesitás al menos 2 modelos configurados para usar fallbacks.", 12, Style::TEXT_MUTED, 700));
        emptyL->addWidget(makeText("Agregá más modelos desde la sección de Modelos.", 11, Style::TEXT_FAINT, 600));
        layout->addWidget(emptyCard);
    } else {
        // List fallback models (use quick models minus active one)
        auto *fallbackCard = new SolidPanel();
        fallbackCard->setFillColor(QColor(Style::WHITE));
        fallbackCard->setCornerRadius(12);
        fallbackCard->setFullBorder(QColor(Style::INK), 2);
        fallbackCard->setHardShadow(QColor(Style::INK), 2, 2);
        auto *fallbackLayout = new QVBoxLayout(fallbackCard);
        fallbackLayout->setContentsMargins(16, 14, 16, 14);
        fallbackLayout->setSpacing(7);

        for (int i = 0; i < m_quickModels.size(); ++i) {
            const auto &qm = m_quickModels[i];
            auto *row = new QWidget();
            row->setStyleSheet("background: transparent; border: none;");
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(8);
            rowLayout->addWidget(makeText(QString::number(i + 1) + ".", 14, Style::TEXT_FAINT, 950), 0, Qt::AlignVCenter);
            QString display = qm.displayName.isEmpty() ? prettyModelName(qm.modelId) : qm.displayName;
            if (display.isEmpty()) display = qm.modelId;
            rowLayout->addWidget(makeText(qm.providerName + " · " + display, 13, Style::INK, 800), 1);
            bool isActive = (qm.providerId == m_activeProviderId && qm.modelId == m_activeModelId);
            if (isActive)
                rowLayout->addWidget(makeText("(activo)", 11, Style::GREEN, 900), 0, Qt::AlignRight);
            fallbackLayout->addWidget(row);
        }
        layout->addWidget(fallbackCard);
    }

    return section;
}

SolidPanel* SettingsView::makeToggleRow(const QString &label, ToggleSwitch **outCheck, bool checked) {
    auto *row = new SolidPanel();
    row->setFillColor(QColor(Style::WHITE));
    row->setCornerRadius(12);
    row->setFullBorder(QColor(Style::INK), 2);
    row->setHardShadow(QColor(Style::INK), 2, 2);
    row->setHoverEffect(true);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(16, 12, 16, 13);
    layout->setSpacing(14);

    auto *ts = new ToggleSwitch();
    ts->setOn(checked, false);
    layout->addWidget(ts, 0, Qt::AlignVCenter);
    layout->addWidget(makeText(label, 13, Style::INK, 850), 1);

    if (outCheck) *outCheck = ts;
    return row;
}

QWidget* SettingsView::makePermissionSection() {
    auto *section = new QWidget();
    section->setStyleSheet("background: transparent; border: none;");
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(makeSectionTitle(":/icons/icons/shield.svg", "Permisos"));

    auto *gridWrap = new QWidget();
    gridWrap->setStyleSheet("background: transparent; border: none;");
    auto *grid = new QGridLayout(gridWrap);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    const QVector<QVector<QString>> modes = {
        { "readonly", "Solo lectura", "Puede leer y explicar, pero no editar.", ":/icons/icons/shield.svg" },
        { "suggest", "Sugerencias", "Puede proponer cambios, pero no aplicarlos.", ":/icons/icons/volume.svg" },
        { "approved", "Edición aprobada", "Puede editar solo si aceptás el diff.", ":/icons/icons/check-circle.svg" },
        { "commands", "Comandos aprobados", "Puede ejecutar comandos solo con permiso.", ":/icons/icons/terminal.svg" },
        { "auto", "Automático limitado", "Puede trabajar solo dentro del proyecto.", ":/icons/icons/play.svg" },
        { "free", "Libre", "Modo avanzado con advertencias.", ":/icons/icons/x.svg" },
    };

    for (int i = 0; i < modes.size(); ++i) {
        auto *card = makePermissionCard(modes[i][0], modes[i][1], modes[i][2], modes[i][3]);
        grid->addWidget(card, i / 2, i % 2);
        m_permissionCards.push_back(card);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    layout->addWidget(gridWrap);
    return section;
}

SolidPanel* SettingsView::makePermissionCard(const QString &id, const QString &label, const QString &description, const QString &iconRes) {
    auto *card = new SolidPanel();
    card->setProperty("permissionId", id);
    card->setCursor(Qt::PointingHandCursor);
    card->installEventFilter(this);
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(12);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 2, 2);
    card->setHoverEffect(true);
    card->setMinimumHeight(78);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 10, 14, 11);
    layout->setSpacing(5);

    auto *top = new QHBoxLayout();
    top->setSpacing(8);
    top->addWidget(makeIcon(iconRes, QColor(id == "free" ? "#d97706" : Style::VIOLET), 14), 0, Qt::AlignVCenter);
    top->addWidget(makeText(label, 13, Style::INK, 950), 1);
    auto *check = makeIcon(":/icons/icons/check-circle.svg", QColor(Style::VIOLET), 12);
    check->setProperty("checkPermissionFor", id);
    check->setVisible(id == m_permissionMode);
    top->addWidget(check, 0, Qt::AlignVCenter);
    layout->addLayout(top);
    layout->addWidget(makeText(description, 12, Style::TEXT_MUTED, 650));
    return card;
}

QWidget* SettingsView::makeSecuritySection() {
    auto *section = new QWidget();
    section->setStyleSheet("background: transparent; border: none;");
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(makeSectionTitle(":/icons/icons/x.svg", "Seguridad"));

    layout->addWidget(makeBlockRow("No tocar .env", true));
    layout->addWidget(makeBlockRow("No borrar carpetas", true));
    layout->addWidget(makeBlockRow("No salir del proyecto", true));
    layout->addWidget(makeBlockRow("No ejecutar comandos peligrosos", true));
    layout->addWidget(makeBlockRow("No hacer git push sin permiso", true));
    return section;
}

SolidPanel* SettingsView::makeBlockRow(const QString &label, bool enabled) {
    return makeToggleRow(label, nullptr, enabled);
}

QWidget* SettingsView::makeAppearanceSection() {
    auto *section = new QWidget();
    section->setStyleSheet("background: transparent; border: none;");
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(makeSectionTitle(":/icons/icons/settings.svg", "Apariencia"));

    auto *gridWrap = new QWidget();
    gridWrap->setStyleSheet("background: transparent; border: none;");
    auto *grid = new QGridLayout(gridWrap);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    auto *light = makeThemeCard("light", "Voryel Light", "Fondo crema/lila, bordes negros", ":/icons/icons/settings.svg");
    auto *dark = makeThemeCard("dark", "Voryel Dark", "Fondo oscuro, acentos violeta", ":/icons/icons/eye.svg");
    grid->addWidget(light, 0, 0);
    grid->addWidget(dark, 0, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    m_themeCards.push_back(light);
    m_themeCards.push_back(dark);
    layout->addWidget(gridWrap);

    layout->addWidget(makeToggleRow("Animaciones suaves", &m_animCheck, true));
    layout->addWidget(makeToggleRow("Sonidos cortos al terminar tareas", &m_soundCheck, true));
    if (m_animCheck) connect(m_animCheck, &ToggleSwitch::toggled, this, &SettingsView::settingsChanged);
    if (m_soundCheck) connect(m_soundCheck, &ToggleSwitch::toggled, this, &SettingsView::settingsChanged);
    return section;
}

SolidPanel* SettingsView::makeThemeCard(const QString &id, const QString &label, const QString &desc, const QString &iconRes) {
    auto *card = new SolidPanel();
    card->setProperty("themeId", id);
    card->setCursor(Qt::PointingHandCursor);
    card->installEventFilter(this);
    card->setCornerRadius(12);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setFillColor(QColor(id == "light" ? Style::VIOLET : Style::WHITE));
    card->setHardShadow(QColor(Style::INK), id == "light" ? 3 : 2, id == "light" ? 3 : 2);
    card->setHoverEffect(true);
    card->setMinimumHeight(126);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(15, 14, 15, 14);
    layout->setSpacing(8);

    const bool active = id == "light";
    const QString text = active ? Style::WHITE : Style::INK;
    const QString muted = active ? "rgba(255,255,255,0.78)" : Style::TEXT_MUTED;
    auto *top = new QHBoxLayout();
    top->setSpacing(8);
    top->addWidget(makeIcon(iconRes, QColor(active ? Style::WHITE : Style::VIOLET), 15), 0, Qt::AlignVCenter);
    top->addWidget(makeText(label, 14, text, 950), 1);
    if (active) top->addWidget(makeIcon(":/icons/icons/check-circle.svg", QColor(Style::WHITE), 13));
    layout->addLayout(top);
    layout->addWidget(makeText(desc, 12, muted, 700));

    auto *preview = new SolidPanel(card);
    preview->setFillColor(QColor(id == "dark" ? "#0a0a14" : Style::BG_LILAC));
    preview->setCornerRadius(9);
    preview->setFullBorder(QColor(active ? "#ffffff55" : "#11111155"), 2);
    auto *previewLayout = new QHBoxLayout(preview);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(0);

    auto *miniSidebar = new SolidPanel(preview);
    miniSidebar->setFillColor(QColor(id == "dark" ? "#12121f" : Style::SIDEBAR_BG));
    miniSidebar->setFixedWidth(32);
    previewLayout->addWidget(miniSidebar);

    auto *lines = new QWidget(preview);
    lines->setStyleSheet("background: transparent; border: none;");
    auto *linesLayout = new QVBoxLayout(lines);
    linesLayout->setContentsMargins(10, 9, 10, 9);
    linesLayout->setSpacing(5);
    for (int i = 0; i < 2; ++i) {
        auto *line = new QLabel();
        line->setFixedHeight(7);
        line->setStyleSheet(QString("background: %1; border: 1px solid %2; border-radius: 3px;")
            .arg(id == "dark" ? "#2a2a3e" : Style::WHITE, active ? "#ffffff55" : "#dddddd"));
        line->setMaximumWidth(i == 0 ? 150 : 110);
        linesLayout->addWidget(line);
    }
    previewLayout->addWidget(lines, 1);
    layout->addWidget(preview, 1);
    return card;
}

QPushButton* SettingsView::makePrimaryButton(const QString &text) {
    auto *btn = new QPushButton(text);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(34);
    btn->setStyleSheet(smallButtonStyle(Style::VIOLET, Style::WHITE, Style::INK, Style::VIOLET_DARK));
    return btn;
}

QPushButton* SettingsView::makeGhostButton(const QString &text) {
    auto *btn = new QPushButton(text);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(34);
    btn->setStyleSheet(smallButtonStyle(Style::WHITE, Style::INK, Style::INK, Style::BG_LILAC));
    return btn;
}

bool SettingsView::eventFilter(QObject *watched, QEvent *event) {
    auto *widget = qobject_cast<QWidget*>(watched);
    if (!widget) return QWidget::eventFilter(watched, event);

    // ── Model list scroll: trap wheel events to prevent propagation to settings ──
    if (widget->property("_modelListScroll").toBool() && event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent*>(event);
        // Find scroll area parent and get its scrollbar
        QScrollArea *sa = nullptr;
        QObject *p = widget->parent();
        while (p) {
            sa = qobject_cast<QScrollArea*>(p);
            if (sa) break;
            p = p->parent();
        }
        if (sa) {
            auto *vbar = sa->verticalScrollBar();
            if (vbar) {
                int delta = we->angleDelta().y();
                int newVal = vbar->value() - delta;
                newVal = qMax(vbar->minimum(), qMin(vbar->maximum(), newVal));
                vbar->setValue(newVal);
            }
        }
        event->accept();
        return true;
    }

    // ── Provider scroll: trap wheel events to prevent propagation ──
    if (widget->property("_isProviderScroll").toBool() && event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent*>(event);
        if (m_providerScroll) {
            auto *vbar = m_providerScroll->verticalScrollBar();
            if (vbar) {
                int delta = we->angleDelta().y();
                int newVal = vbar->value() - delta;
                newVal = qMax(vbar->minimum(), qMin(vbar->maximum(), newVal));
                vbar->setValue(newVal);
            }
        }
        event->accept();
        return true;
    }

    // ── Outside-click closes add model panel ──
    if (m_addModelPanel && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        QPoint globalPos = me->globalPosition().toPoint();
        QWidget *clickedWidget = QApplication::widgetAt(globalPos);

        // If click is inside the panel or any of its children, don't close
        if (clickedWidget && (clickedWidget == m_addModelPanel || m_addModelPanel->isAncestorOf(clickedWidget))) {
            return false;
        }
        // If the combo popup is open, don't close
        auto *combo = m_addModelPanel->findChild<QComboBox*>();
        if (combo && combo->view() && combo->view()->isVisible()) {
            return false;
        }

        // Click outside: close with a tiny delay to let the combo process first
        QTimer::singleShot(0, this, &SettingsView::closeAddModelPanel);
        return true;
    }

    const QString permissionId = widget->property("permissionId").toString();
    const QString themeId = widget->property("themeId").toString();

    if (event->type() == QEvent::MouseButtonRelease) {
        if (!permissionId.isEmpty()) { choosePermission(permissionId); return true; }
        if (!themeId.isEmpty()) {
            emit statusMessageRequested(themeId == "light" ? "Tema claro activo" : "Modo oscuro pendiente");
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SettingsView::choosePermission(const QString &modeId) {
    if (modeId.isEmpty()) return;
    m_permissionMode = modeId;
    refreshPermissionCards();
    emit statusMessageRequested(QString("Permisos: %1").arg(modeId));
}

void SettingsView::setActiveModel(const QString &model) {
    if (model.isEmpty()) return;
    m_activeModel = model;
}

void SettingsView::setActiveModelFromProvider() {
    for (const auto &p : m_providers) {
        if (p.id == m_activeProviderId) {
            QString mid = m_activeModelId;
            if (mid.isEmpty()) {
                m_activeModel = "Sin modelo";
                return;
            }
            QString display = prettyModelName(mid);
            if (display.isEmpty()) display = mid;
            m_activeModel = p.displayName + " · " + display;
            emit modelSelected(m_activeModel);
            return;
        }
    }
    m_activeModel = "Sin modelo";
}

void SettingsView::activateModel(const QString &providerId, const QString &modelId, int contextWindowTokens) {
    m_activeProviderId = providerId;
    m_activeModelId = modelId;
    m_activeModelContextWindow = contextWindowTokens;
    setActiveModelFromProvider();
    emit activeModelChanged(providerId, modelId);
    emit providerConfigChanged();
}

void SettingsView::refreshPermissionCards() {
    for (auto *card : m_permissionCards) {
        if (!card) continue;
        const QString id = card->property("permissionId").toString();
        const bool active = id == m_permissionMode;
        const bool danger = id == "free";
        card->setFillColor(QColor(Style::WHITE));
        card->setFullBorder(QColor(active ? (danger ? "#d97706" : Style::VIOLET) : Style::INK), 2);
        card->setHardShadow(QColor(active ? (danger ? "#d97706" : Style::VIOLET) : Style::INK), active ? 3 : 2, active ? 3 : 2);
        const QList<QLabel*> labels = card->findChildren<QLabel*>();
        for (auto *label : labels) {
            if (label->property("checkPermissionFor").toString() == id) label->setVisible(active);
        }
    }
}

void SettingsView::refreshThemeCards() {
    for (auto *card : m_themeCards) {
        if (!card) continue;
        const bool active = card->property("themeId").toString() == "light";
        card->setFillColor(QColor(active ? Style::VIOLET : Style::WHITE));
        card->setHardShadow(QColor(Style::INK), active ? 3 : 2, active ? 3 : 2);
    }
}

int SettingsView::providerIndexById(const QString &id) const {
    for (int i = 0; i < m_providers.size(); ++i) {
        if (m_providers[i].id == id)
            return i;
    }
    return -1;
}

void SettingsView::loadSettings() {
    QSettings settings("Loryq", "Voryel");
    bool needsMigration = false;

    settings.beginGroup("providers");
    for (auto &provider : m_providers) {
        settings.beginGroup(provider.id);
        provider.configured = settings.value("configured", provider.configured).toBool();
        provider.baseUrl = settings.value("baseUrl", provider.baseUrl).toString();
        const QString protectedKey = settings.value("apiKeyProtected").toString();
        const bool hasLegacyKey = settings.contains("apiKey");
        provider.apiKey = protectedKey.isEmpty()
            ? settings.value("apiKey", provider.apiKey).toString()
            : unprotectSecret(protectedKey);
        if (hasLegacyKey || (!provider.apiKey.isEmpty() && protectedKey.isEmpty()))
            needsMigration = true;
        settings.endGroup();
    }
    settings.endGroup();

    m_activeProviderId = settings.value("models/activeProviderId", m_activeProviderId).toString();
    m_activeModelId = settings.value("models/activeModelId", m_activeModelId).toString();

    const int count = settings.beginReadArray("quickModels");
    m_quickModels.clear();
    for (int i = 0; i < count && m_quickModels.size() < MAX_QUICK_MODELS; ++i) {
        settings.setArrayIndex(i);
        QuickModelEntry entry;
        entry.id = settings.value("id").toString();
        entry.providerId = settings.value("providerId").toString();
        entry.providerName = settings.value("providerName").toString();
        entry.modelId = settings.value("modelId").toString();
        entry.displayName = settings.value("displayName").toString();
        entry.availability = settings.value("availability").toString();
        if (entry.id.isEmpty() && !entry.providerId.isEmpty() && !entry.modelId.isEmpty())
            entry.id = entry.providerId + ":" + entry.modelId;
        if (!entry.providerId.isEmpty() && !entry.modelId.isEmpty())
            m_quickModels.append(entry);
    }
    settings.endArray();

    m_fallbacksEnabled = settings.value("fallbacks/enabled", false).toBool();

    if (needsMigration)
        saveSettings();
}

void SettingsView::saveSettings() const {
    QSettings settings("Loryq", "Voryel");

    settings.beginGroup("providers");
    for (const auto &provider : m_providers) {
        settings.beginGroup(provider.id);
        settings.setValue("configured", provider.configured);
        settings.setValue("baseUrl", provider.baseUrl);
        settings.remove("apiKey");
        settings.setValue("apiKeyProtected", protectSecret(provider.apiKey));
        settings.endGroup();
    }
    settings.endGroup();

    settings.setValue("fallbacks/enabled", m_fallbacksEnabled);
    settings.setValue("models/activeProviderId", m_activeProviderId);
    settings.setValue("models/activeModelId", m_activeModelId);

    settings.beginWriteArray("quickModels");
    for (int i = 0; i < m_quickModels.size(); ++i) {
        settings.setArrayIndex(i);
        const auto &entry = m_quickModels[i];
        settings.setValue("id", entry.id);
        settings.setValue("providerId", entry.providerId);
        settings.setValue("providerName", entry.providerName);
        settings.setValue("modelId", entry.modelId);
        settings.setValue("displayName", entry.displayName);
        settings.setValue("availability", entry.availability);
    }
    settings.endArray();
}

void SettingsView::refreshProvidersList() {
    if (!m_providerListLayout) return;
    clearLayout(m_providerListLayout);
    for (int i = 0; i < m_providers.size(); ++i)
        m_providerListLayout->addWidget(makeProviderRow(i));
}

QWidget* SettingsView::makeProvidersSection() {
    auto *section = new QWidget();
    section->setStyleSheet("background: transparent; border: none;");
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(makeSectionTitle(":/icons/icons/cpu.svg", "Proveedores"));

    // ── Provider list (scrollable, max ~4 visible) ──
    m_providerScroll = new QScrollArea(section);
    m_providerScroll->setWidgetResizable(true);
    m_providerScroll->setFrameShape(QFrame::NoFrame);
    m_providerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_providerScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_providerScroll->setMaximumHeight(4 * 72 + 10); // ~4 cards + spacing
    m_providerScroll->viewport()->installEventFilter(this);
    m_providerScroll->viewport()->setProperty("_isProviderScroll", true);
    m_providerScroll->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea::viewport { background: transparent; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 2px; }"
        "QScrollBar::handle:vertical { background: #d8b4fe; border-radius: 4px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: #7c3aed; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );

    auto *providerContainer = new QWidget();
    providerContainer->setStyleSheet("background: transparent; border: none;");
    m_providerListLayout = new QVBoxLayout(providerContainer);
    m_providerListLayout->setContentsMargins(0, 0, 0, 0);
    m_providerListLayout->setSpacing(6);

    for (int i = 0; i < m_providers.size(); ++i) {
        m_providerListLayout->addWidget(makeProviderRow(i));
    }
    m_providerScroll->setWidget(providerContainer);
    layout->addWidget(m_providerScroll);

    // ── Dynamic config block (hidden by default) ──
    // We create placeholders; fields are added/removed dynamically when a provider is selected.
    m_providerConfigBlock = new QWidget(section);
    m_providerConfigBlock->setStyleSheet("background: transparent; border: none;");
    m_providerConfigBlock->setVisible(false);
    auto *configBlock = m_providerConfigBlock;
    auto *cfgL = new QVBoxLayout(configBlock);
    cfgL->setContentsMargins(0, 0, 0, 0);
    cfgL->setSpacing(4);

    // Containers for dynamic fields
    auto *baseUrlContainer = new QWidget(configBlock);
    baseUrlContainer->setStyleSheet("background: transparent; border: none;");
    auto *baseUrlL = new QHBoxLayout(baseUrlContainer);
    baseUrlL->setContentsMargins(0, 4, 0, 4);
    baseUrlL->setSpacing(10);
    auto *baseUrlLabel = makeText("Base URL", 12, Style::TEXT_MUTED, 800);
    baseUrlLabel->setMinimumWidth(75);
    baseUrlL->addWidget(baseUrlLabel, 0, Qt::AlignVCenter);
    m_baseUrlEdit = new QLineEdit();
    m_baseUrlEdit->setPlaceholderText("http://127.0.0.1:1234/v1");
    m_baseUrlEdit->setMinimumHeight(32);
    m_baseUrlEdit->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 2px solid %3; border-radius: 8px;"
        "  padding: 4px 10px; font-size: 12px; font-weight: 600; font-family: 'Consolas','Courier New',monospace; }"
        "QLineEdit:focus { border-color: %4; }"
    ).arg(Style::BG_LILAC, Style::INK, Style::BORDER_SOFT, Style::VIOLET));
    connect(m_baseUrlEdit, &QLineEdit::textChanged, this, &SettingsView::providerConfigChanged);
    baseUrlL->addWidget(m_baseUrlEdit, 1);
    cfgL->addWidget(baseUrlContainer);

    auto *apiKeyContainer = new QWidget(configBlock);
    apiKeyContainer->setStyleSheet("background: transparent; border: none;");
    auto *apiKeyL = new QHBoxLayout(apiKeyContainer);
    apiKeyL->setContentsMargins(0, 4, 0, 4);
    apiKeyL->setSpacing(10);
    auto *apiKeyLabel = makeText("API Key", 12, Style::TEXT_MUTED, 800);
    apiKeyLabel->setMinimumWidth(75);
    apiKeyL->addWidget(apiKeyLabel, 0, Qt::AlignVCenter);
    m_apiKeyEdit = new QLineEdit();
    m_apiKeyEdit->setPlaceholderText("Opcional — vacío permitido");
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setMinimumHeight(32);
    m_apiKeyEdit->setStyleSheet(m_baseUrlEdit->styleSheet());
    connect(m_apiKeyEdit, &QLineEdit::textChanged, this, &SettingsView::providerConfigChanged);
    apiKeyL->addWidget(m_apiKeyEdit, 1);
    cfgL->addWidget(apiKeyContainer);

    // Buttons
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    auto *cancelBtn = makeGhostButton("Cancelar");
    auto *saveBtn = makePrimaryButton("Guardar");
    btnRow->addWidget(cancelBtn, 1);
    btnRow->addWidget(saveBtn, 1);
    cfgL->addLayout(btnRow);
    layout->addWidget(configBlock);

    // Save handler
    connect(saveBtn, &QPushButton::clicked, this, [this, configBlock]() {
        int idx = m_editingProviderIndex;
        if (idx < 0 || idx >= m_providers.size()) return;
        auto &p = m_providers[idx];
        QString url = m_baseUrlEdit ? m_baseUrlEdit->text().trimmed() : QString();
        QString key = m_apiKeyEdit  ? m_apiKeyEdit->text().trimmed()  : QString();

        // For cloud providers, use preset URL if user left it empty
        if (url.isEmpty() && !p.isLocal) {
            url = presetBaseUrl(p.id);
            if (url.isEmpty()) url = m_baseUrlEdit ? m_baseUrlEdit->placeholderText() : QString();
        }
        if (url.isEmpty()) {
            emit statusMessageRequested("Completá la Base URL o elegí un proveedor cloud.");
            return;
        }
        if (!isSafeProviderUrlForApiKey(url, key)) {
            emit statusMessageRequested("No se puede guardar una API key con HTTP externo. Usá HTTPS o un proveedor local.");
            return;
        }

        p.baseUrl = url;
        p.apiKey  = key;
        p.configured = !p.baseUrl.isEmpty();
        saveSettings();
        refreshProvidersList();

        m_editingProviderIndex = -1;
        configBlock->setVisible(false);
        emit configurationSaved(p.displayName, QString());
        emit providerConfigChanged();
        emit statusMessageRequested("Proveedor configurado: " + p.displayName);
    });

    // Cancel handler
    connect(cancelBtn, &QPushButton::clicked, this, [this, configBlock]() {
        m_editingProviderIndex = -1;
        configBlock->setVisible(false);
    });

    m_editingProviderIndex = -1;

    // Streaming toggle (global)
    layout->addWidget(makeToggleRow("Usar streaming si el proveedor lo soporta", &m_streamingCheck, true));

    return section;
}

QWidget* SettingsView::makeProviderRow(int idx) {
    if (idx < 0 || idx >= m_providers.size()) return new QWidget();
    auto &prov = m_providers[idx];

    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(12);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 2, 2);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    auto *mainLayout = new QVBoxLayout(card);
    mainLayout->setContentsMargins(16, 10, 16, 10);
    mainLayout->setSpacing(0);

    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(10);

    // Status dot
    auto *dot = new QLabel();
    dot->setAttribute(Qt::WA_StyledBackground, true);
    dot->setFixedSize(10, 10);
    QString dotColor;
    if (prov.comingSoon) dotColor = Style::TEXT_FAINT;
    else if (prov.configured) dotColor = Style::GREEN;
    else dotColor = Style::TEXT_MUTED;
    dot->setStyleSheet(QString("background: %1; border-radius: 5px; border: none;").arg(dotColor));
    headerRow->addWidget(dot, 0, Qt::AlignVCenter);

    // Info column
    auto *infoCol = new QVBoxLayout();
    infoCol->setSpacing(1);
    infoCol->addWidget(makeText(prov.displayName, 14, Style::INK, 900));
    QString statusLine;
    if (prov.comingSoon) statusLine = "Próximamente";
    else if (prov.configured) {
        QString url = prov.baseUrl;
        if (url.length() > 45) url = url.left(42) + "...";
        statusLine = QString("%1 · %2").arg(prov.isLocal ? "Local" : "Cloud", url);
    } else {
        statusLine = prov.requiresApiKey ? "Requiere API key" : "Local";
    }
    infoCol->addWidget(makeText(statusLine, 11, prov.configured ? Style::GREEN : Style::TEXT_MUTED, 700));
    headerRow->addLayout(infoCol, 1);

    // Action buttons
    if (!prov.comingSoon) {
        // Detect button always visible for configured providers
        if (prov.configured) {
            auto *detectBtn = makeGhostButton("Detectar");
            detectBtn->setFixedWidth(80);
            connect(detectBtn, &QPushButton::clicked, this, [this, idx]() {
                if (idx >= 0 && idx < m_providers.size() && !m_providers[idx].baseUrl.isEmpty())
                    emit statusMessageRequested("Detectando modelos desde " + m_providers[idx].baseUrl + "...");
            });
            headerRow->addWidget(detectBtn, 0, Qt::AlignVCenter);
        }

        // Configurar/Conectar button
        auto *cfgBtn = makeGhostButton(prov.configured ? "Configurar" : "Conectar");
        cfgBtn->setFixedWidth(prov.configured ? 90 : 85);
        connect(cfgBtn, &QPushButton::clicked, this, [this, idx]() {
            m_editingProviderIndex = idx;
            const auto &p = m_providers[idx];

            // Show/hide Base URL based on provider type
            if (p.isLocal) {
                // Local: show Base URL, populate with saved or default
                if (m_baseUrlEdit) {
                    m_baseUrlEdit->setText(p.baseUrl.isEmpty() ? presetBaseUrl(p.id) : p.baseUrl);
                    m_baseUrlEdit->parentWidget()->setVisible(true);
                }
            } else if (p.id == "custom") {
                // Custom: show Base URL
                if (m_baseUrlEdit) {
                    m_baseUrlEdit->setText(p.baseUrl);
                    m_baseUrlEdit->parentWidget()->setVisible(true);
                }
            } else {
                // Cloud with preset: hide Base URL, set preset internally
                if (m_baseUrlEdit) {
                    m_baseUrlEdit->setText(p.baseUrl.isEmpty() ? presetBaseUrl(p.id) : p.baseUrl);
                    m_baseUrlEdit->parentWidget()->setVisible(false);
                }
            }

            // API Key always visible
            if (m_apiKeyEdit) {
                m_apiKeyEdit->setText(p.apiKey);
                m_apiKeyEdit->parentWidget()->setVisible(true);
                // API Key placeholder: required for cloud, optional for local
                if (p.requiresApiKey && !p.isLocal) {
                    m_apiKeyEdit->setPlaceholderText("Pegá tu API key...");
                } else {
                    m_apiKeyEdit->setPlaceholderText("Opcional — vacío permitido");
                }
            }

            if (m_providerConfigBlock) m_providerConfigBlock->setVisible(true);
            emit statusMessageRequested("Configurando " + p.displayName + "...");
        });
        headerRow->addWidget(cfgBtn, 0, Qt::AlignVCenter);
    }

    mainLayout->addLayout(headerRow);
    return card;
}

ProviderConfig SettingsView::providerConfig() const {
    ProviderConfig cfg;

    // Prefer live edit fields (visible during inline editing)
    if (m_baseUrlEdit && m_apiKeyEdit && m_modelIdEdit) {
        cfg.baseUrl   = m_baseUrlEdit->text().trimmed();
        cfg.apiKey    = m_apiKeyEdit->text().trimmed();
        cfg.modelId   = m_modelIdEdit->text().trimmed();
        // If edit fields are non-empty, use them
        if (!cfg.baseUrl.isEmpty()) {
            cfg.streaming = m_streamingCheck ? m_streamingCheck->isOn() : true;
            cfg.contextWindowTokens = m_activeModelContextWindow;
            return cfg;
        }
    }

    // Fall back to active provider config
    for (const auto &p : m_providers) {
        if (p.id == m_activeProviderId && p.configured) {
            cfg.baseUrl   = p.baseUrl;
            cfg.apiKey    = p.apiKey;
            cfg.modelId   = m_activeModelId;
            cfg.streaming = m_streamingCheck ? m_streamingCheck->isOn() : true;
            cfg.contextWindowTokens = m_activeModelContextWindow;
            return cfg;
        }
    }

    // Last resort defaults
    cfg.baseUrl   = "http://localhost:1234/v1";
    cfg.streaming = m_streamingCheck ? m_streamingCheck->isOn() : true;
    return cfg;
}

void SettingsView::setProviderConfig(const ProviderConfig &config) {
    if (!isSafeProviderUrlForApiKey(config.baseUrl, config.apiKey)) {
        emit statusMessageRequested("No se puede guardar una API key con HTTP externo. Usa HTTPS o un proveedor local.");
        return;
    }

    // Update the LM Studio provider (default)
    for (auto &p : m_providers) {
        if (p.id == "lm_studio") {
            p.baseUrl = config.baseUrl;
            p.apiKey  = config.apiKey;
            p.configured = !config.baseUrl.isEmpty();
            m_activeProviderId = "lm_studio";
            m_activeModelId = config.modelId;
            break;
        }
    }
    // Update fields if editing
    if (m_baseUrlEdit) m_baseUrlEdit->setText(config.baseUrl);
    if (m_apiKeyEdit)  m_apiKeyEdit->setText(config.apiKey);
    if (m_modelIdEdit) m_modelIdEdit->setText(config.modelId);

    setActiveModelFromProvider();
    saveSettings();
    refreshProvidersList();
    refreshModelsList();
}

void SettingsView::addQuickModel(const QuickModelEntry &entry) {
    if (m_quickModels.size() >= MAX_QUICK_MODELS) return;
    // Check duplicate
    for (const auto &qm : m_quickModels) {
        if (qm.providerId == entry.providerId && qm.modelId == entry.modelId)
            return;
    }
    m_quickModels.append(entry);
    saveSettings();
    refreshModelsList();
}

void SettingsView::removeQuickModel(const QString &id) {
    for (int i = 0; i < m_quickModels.size(); ++i) {
        if (m_quickModels[i].id == id) {
            const bool removedActive = (m_quickModels[i].providerId == m_activeProviderId
                                     && m_quickModels[i].modelId == m_activeModelId);
            m_quickModels.remove(i);
            if (removedActive) {
                if (m_quickModels.isEmpty()) {
                    m_activeModelId.clear();
                    m_activeModel = "Sin modelo";
                } else {
                    m_activeProviderId = m_quickModels.first().providerId;
                    m_activeModelId = m_quickModels.first().modelId;
                    setActiveModelFromProvider();
                }
            }
            saveSettings();
            refreshModelsList();
            return;
        }
    }
}
