#include "MainWindow.h"
#include "Sidebar.h"
#include "TitleBar.h"
#include "BottomBar.h"
#include "DashboardView.h"
#include "ProjectView.h"
#include "ChatView.h"
#include "SettingsView.h"
#include "Style.h"
#include "SolidPanel.h"
#include "SoundUtil.h"
#include "core/VoryelCore.h"
#include "core/TokenCounter.h"
#include "model/ChatSession.h"
#include "model/PromptBuilder.h"
#include "chat/ChatStore.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QApplication>
#include <QScreen>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QDebug>
#include <QAbstractAnimation>
#include <QEvent>
#include <QTimer>
#include <QPainter>
#include <QFont>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <objbase.h>
#endif

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_core(new VoryelCore(this)) {
    // ── NOTA DE ARQUITECTURA ──
    // Este constructor centraliza demasiado: crea vistas, conecta señales,
    // inyecta datos mock y configura Win32. En una refactorización futura:
    //   - MockDataSource inyectará datos de ejemplo
    //   - AppController orquestará la creación y conexión de vistas
    //   - MainWindow solo manejará el layout raíz + frameless window
    // ─────────────────────────
    setWindowTitle("Voryel");
    setWindowIcon(QIcon(":/icons/icons/Logo-Voryel.ico"));
    resize(1180, 760);
    setMinimumSize(980, 620);

#if defined(Q_OS_WIN)
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    QTimer::singleShot(0, this, [this]() {
        HWND hwnd = (HWND)winId();
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        style &= ~WS_CAPTION;
        SetWindowLong(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    });
    // Overlay timer: clears taskbar overlay 5s after gaining focus
    // FUTURO: mover a TaskbarOverlayManager
    m_overlayTimer = new QTimer(this);
    m_overlayTimer->setSingleShot(true);
    m_overlayTimer->setInterval(5000);
    connect(m_overlayTimer, &QTimer::timeout, this, &MainWindow::clearTaskbarAlert);
#endif

    auto *central = new QWidget();
    central->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));
    setCentralWidget(central);

    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_sidebar = new Sidebar(central);
    rootLayout->addWidget(m_sidebar);

    auto *right = new QWidget();
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_titleBar = new TitleBar(right);
    connect(m_titleBar, &TitleBar::modelBadgeClicked, this, &MainWindow::showModelPopover);
    rightLayout->addWidget(m_titleBar);

    m_stack = new QStackedWidget(right);
    m_stack->setObjectName("pageStack");
    m_stack->setStyleSheet(QString("QStackedWidget#pageStack { background: %1; border: none; }").arg(Style::BG_LILAC));

    const QVector<DashProject> dashProjects = {
        { "voryel-native", "voryel-native", "Electron", "#7c3aed", "Arreglar error CSS", "hace 5 min" },
        { "landing-page",  "landing-page",  "Web",      "#0ea5e9", "Deploy a producción", "ayer" },
        { "api-gateway",   "api-gateway",   "Rust",     "#059669", "Agregar rate limiting", "hace 3 días" },
    };

    const QVector<ProjectEntry> projectEntries = {
        { "voryel-native", "voryel-native", "E:/Users/Nadru/Descargas/voryel", "Electron", "#7c3aed", "hace 5 min" },
        { "landing-page",  "landing-page",  "E:/Users/Nadru/Desktop/loryq-site", "Web", "#0ea5e9", "ayer" },
        { "api-gateway",   "api-gateway",   "E:/Users/Nadru/Desktop/api-gateway", "Rust", "#059669", "hace 3 días" },
    };

    m_dashboardView = new DashboardView();
    m_dashboardView->setActiveModel(m_activeModel);
    m_dashboardView->setProjects(dashProjects);
    m_dashboardView->setSessions({
        { "s1", "Analizar proyecto",       "completed", "Voryel", 3, "hace 5 min" },
        { "s2", "Arreglar error CSS",      "active",    "Voryel", 1, "hace 12 min" },
        { "s3", "Ejecutar npm run build",  "error",     "Voryel", 0, "ayer" },
    });
    m_dashboardView->setWorkflows({
        { "wf1", ":/icons/icons/cpu.svg",          "Analizar código" },
        { "wf2", ":/icons/icons/play.svg",         "Ejecutar tests" },
        { "wf3", ":/icons/icons/check-circle.svg", "Revisar seguridad" },
        { "wf4", ":/icons/icons/list.svg",         "Generar docs" },
    });

    m_projectView = new ProjectView();
    m_projectView->setProjects(projectEntries);
    m_projectView->setActiveProject("voryel-native");

    m_chatView = new ChatView();
    m_chatView->setActiveModel(m_activeModel);
    m_chatView->setAnimationsEnabled(true);

    // ── ChatSession (real LLM provider) ──
    m_chatSession = new ChatSession(this);
    m_chatView->setDemoMode(!m_chatSession->isConfigured());

    // ── ChatStore (multi-chat persistence) ──
    m_chatStore = new ChatStore(this);
    m_chatView->setChatStore(m_chatStore);

    // Update context meter when chat switches
    connect(m_chatStore, &ChatStore::chatSwitched, this, [this](int) {
        updateContextMeterForCurrentChat();
    });
    connect(m_chatStore, &ChatStore::messagesChanged, this, [this](int) {
        updateContextMeterForCurrentChat();
    });

    m_settingsView = new SettingsView();
    m_settingsView->setActiveModel(m_activeModel);
    m_chatSession->setProviderConfig(m_settingsView->providerConfig());
    m_chatView->setDemoMode(!m_chatSession->isConfigured());

    // Initialize model display from actual provider config
    updateModelBadges();

    connect(m_settingsView, &SettingsView::modelSelected, this, [this](const QString &model) {
        m_activeModel = model.isEmpty() ? "Sin modelo" : model;
        updateModelBadges();
        updateContextMeterForCurrentChat();
        m_core->setCurrentTask("Modelo activo actualizado");
        m_core->setState(CoreState::Idle);
    });
    connect(m_settingsView, &SettingsView::configurationSaved, this, [this](const QString &provider, const QString &model) {
        m_core->setCurrentTask(QString("Configuración guardada: %1 · %2").arg(provider, model));
        m_core->setState(CoreState::Idle);
    });
    connect(m_settingsView, &SettingsView::statusMessageRequested, this, [this](const QString &message) {
        m_core->setCurrentTask(message);
        m_core->setState(CoreState::Idle);
    });
    connect(m_settingsView, &SettingsView::settingsChanged, this, [this]() {
        m_chatView->setAnimationsEnabled(m_settingsView->isAnimationEnabled());
    });

    connect(m_dashboardView, &DashboardView::chatRequested, this, [this](const QString &prompt, const QString &projectId) {
        if (!projectId.isEmpty()) {
            m_titleBar->setProjectName(projectId);
            m_projectView->setActiveProject(projectId);
        }
        m_chatView->setDraftPrompt(prompt);
        m_core->setCurrentTask(prompt.isEmpty() ? "Nueva sesión" : prompt);
        m_core->setState(CoreState::Thinking);
        handleNavigate(View::Chat);
    });
    connect(m_dashboardView, &DashboardView::projectOpened, this, [this](const QString &projectId) {
        m_titleBar->setProjectName(projectId);
        m_projectView->setActiveProject(projectId);
        handleNavigate(View::Project);
    });
    connect(m_projectView, &ProjectView::createProjectRequested, this, [this]() {
        m_core->setCurrentTask("Crear proyecto");
        m_core->setState(CoreState::Thinking);
        m_chatView->setDraftPrompt("Crear un nuevo proyecto en Voryel");
        handleNavigate(View::Chat);
    });
    connect(m_projectView, &ProjectView::projectOpened, this, [this](const QString &projectId) {
        m_titleBar->setProjectName(projectId);
        m_projectView->setActiveProject(projectId);
        m_core->setCurrentTask(QString("Proyecto abierto: %1").arg(projectId));
        m_core->setState(CoreState::Idle);
    });
    // ── ChatView → MainWindow (messageSent) ──
    // En real mode: handleRealChatMessage orquesta ChatSession + VoryelCore + ChatView.
    // En demo mode: el flujo mock vive dentro de ChatView (handleSend internamente).
    connect(m_chatView, &ChatView::messageSent, this, &MainWindow::handleRealChatMessage);
    connect(m_chatView, &ChatView::cancelRequested, this, [this]() {
        if (m_chatSession)
            m_chatSession->cancel();
        else if (m_chatView)
            m_chatView->abortStream();
    });
    connect(m_chatView, &ChatView::taskStateChanged, this, [this](const QString &label) {
        m_lastTaskLabel = label;
        m_core->setCurrentTask(label);
        const bool soundOn = m_settingsView->isSoundEnabled();
        if (label == "Voryel está pensando") {
            m_core->setState(CoreState::Thinking);
        } else if (label == "Plan listo") {
            m_core->setState(CoreState::AwaitingApproval);
            if (soundOn) SoundUtil::play(SoundUtil::Sound::Complete);
        } else if (label == "Esperando permiso") {
            m_core->setState(CoreState::AwaitingApproval);
            if (soundOn) SoundUtil::play(SoundUtil::Sound::Question);
        } else if (label == "Ejecutando comando") {
            m_core->setState(CoreState::Executing);
        } else if (label == "Diff listo") {
            m_core->setState(CoreState::AwaitingApproval);
            if (soundOn) SoundUtil::play(SoundUtil::Sound::Complete);
        } else if (label == "Cancelado") {
            m_core->setState(CoreState::Idle);
            if (soundOn) SoundUtil::play(SoundUtil::Sound::Error);
        }
        // "Voryel está escribiendo" / "Respuesta lista" → only update task label
    });

    // ── ChatSession → UI wiring ──
    connect(m_chatSession, &ChatSession::started, this, [this]() {
        qDebug() << "ChatSession::started → setState(Thinking)";
        m_core->setState(CoreState::Thinking);
    });
    connect(m_chatSession, &ChatSession::tokenReceived, this, [this](const QString &token) {
        m_chatView->appendStreamToken(token);
    });
    connect(m_chatSession, &ChatSession::finished, this, [this](const QString &response) {
        if (m_fallbackErrorCount > 0 && !m_originalProviderId.isEmpty()) {
            ProviderConfig current = m_settingsView->providerConfig();
            bool onOriginal = (m_settingsView->activeProviderId() == m_originalProviderId
                               && current.modelId == m_originalModelId);
            if (onOriginal) {
                m_fallbackErrorCount = 0;
                m_originalProviderId.clear();
                m_originalModelId.clear();
                m_triedFallbackKeys.clear();
                hideFallbackNotification();
            } else {
                if (m_fallbackErrorCount < 2) {
                    m_settingsView->activateModel(m_originalProviderId, m_originalModelId);
                    m_triedFallbackKeys.clear();
                    hideFallbackNotification();
                } else {
                    m_fallbackErrorCount = 0;
                    m_originalProviderId.clear();
                    m_originalModelId.clear();
                    m_triedFallbackKeys.clear();
                }
            }
        }
        m_chatView->finishStreaming(response);
        m_core->setState(CoreState::Idle);
        if (m_chatStore && !response.isEmpty()) {
            int idx = m_chatStore->activeChatIndex();
            m_chatStore->addMessage(idx, {"assistant", response});
        }
        if (m_settingsView->isSoundEnabled())
            SoundUtil::play(SoundUtil::Sound::Complete);
        updateContextMeterForCurrentChat();
    });
    connect(m_chatSession, &ChatSession::errorOccurred, this, [this](const QString &msg) {
        m_core->setState(CoreState::Idle);

        // ── 1. Classify error ──
        QString low = msg.toLower();
        QString prov = m_settingsView->activeProviderId();
        bool isAuth = low.contains("401") || low.contains("403")
                      || low.contains("unauthorized") || low.contains("authentication")
                      || low.contains("invalid api key") || low.contains("invalid_api_key")
                      || low.contains("api key") || low.contains("expir");
        bool isRateLimit = low.contains("429") || low.contains("rate limit")
                           || low.contains("rate_limit") || low.contains("too many requests");
        bool isModelNotFound = low.contains("model not found") || low.contains("model_not_found")
                               || low.contains("model does not exist") || low.contains("model_does_not_exist")
                               || low.contains("no encontrado")
                               || (low.contains("404") && (low.contains("model") || low.contains("not found") || low.contains("does not exist")));
        bool isEndpointError = low.contains("404") && !isModelNotFound;
        bool isNetworkError = low.contains("connection refused") || low.contains("connection reset")
                              || low.contains("timeout") || low.contains("host not found")
                              || low.contains("ssl") || low.contains("certificate");

        QString displayMsg;
        if (isAuth)
            displayMsg = QString("La API key de %1 no es v\u00e1lida o expir\u00f3.").arg(prov);
        else if (isEndpointError)
            displayMsg = QString("Endpoint no encontrado. Revis\u00e1 la URL base de %1.").arg(prov);
        else if (isRateLimit) {
            ProviderConfig rlCfg = m_settingsView->providerConfig();
            displayMsg = QString("L\u00edmite alcanzado para %1.\nVoryel est\u00e1 probando un fallback.").arg(providerDisplay(rlCfg));
        }
        else if (isModelNotFound)
            displayMsg = QString("El modelo actual no est\u00e1 disponible en %1.").arg(prov);
        else
            displayMsg = msg;
        m_chatView->showErrorMessage(displayMsg);
        if (m_settingsView->isSoundEnabled())
            SoundUtil::play(SoundUtil::Sound::Error);

        // Auth / config errors → never use fallback
        if (isAuth) {
            m_fallbackErrorCount = 0;
            m_originalProviderId.clear();
            m_originalModelId.clear();
            m_triedFallbackKeys.clear();
            hideFallbackNotification();
            return;
        }

        // ── 2. Endpoint 404 → switch provider, no cuenta como fallo del modelo ──
        if (isEndpointError) {
            ProviderConfig cfg = m_settingsView->providerConfig();
            const auto models = m_settingsView->quickModels();
            QString cur = m_settingsView->activeProviderId() + ":" + cfg.modelId;
            bool found = false;
            for (const auto &qm : models) {
                QString key = qm.providerId + ":" + qm.modelId;
                if (key == cur || m_triedFallbackKeys.contains(key))
                    continue;
                m_triedFallbackKeys.append(key);
                m_settingsView->activateModel(qm.providerId, qm.modelId);
                QTimer::singleShot(500, this, [this]() {
                    if (m_chatSession && m_chatSession->isConfigured())
                        m_chatSession->sendMessage(m_lastUserPrompt);
                });
                found = true;
                break;
            }
            if (!found) {
                m_chatView->showErrorMessage("Todos los proveedores de respaldo fallaron.");
                m_triedFallbackKeys.clear();
            }
            return;
        }

        // ── 3. Fallback rescue (model-level errors) ──
        if (!m_settingsView->fallbacksEnabled() || m_lastUserPrompt.isEmpty())
            return;

        ProviderConfig currentCfg = m_settingsView->providerConfig();
        const auto models = m_settingsView->quickModels();

        if (m_originalProviderId.isEmpty()) {
            m_originalProviderId = m_settingsView->activeProviderId();
            m_originalModelId = currentCfg.modelId;
        }

        // Add original to tried set to prevent A→B→A loop
        QString origKey = m_originalProviderId + ":" + m_originalModelId;
        if (!m_triedFallbackKeys.contains(origKey))
            m_triedFallbackKeys.append(origKey);

        if (isModelNotFound)
            m_fallbackErrorCount += 2; // permanent immediately
        else
            m_fallbackErrorCount++;

        // Find untried fallback different from current
        QString curKey = m_settingsView->activeProviderId() + ":" + currentCfg.modelId;
        bool found = false;
        for (const auto &qm : models) {
            QString key = qm.providerId + ":" + qm.modelId;
            if (key == curKey || m_triedFallbackKeys.contains(key))
                continue;
            m_triedFallbackKeys.append(key);
            QString fallbackName = qm.displayName.isEmpty() ? qm.modelId : qm.displayName;
            m_settingsView->activateModel(qm.providerId, qm.modelId);
            if (m_fallbackErrorCount >= 2)
                showFallbackNotification(m_originalModelId, fallbackName);
            QTimer::singleShot(500, this, [this]() {
                if (m_chatSession && m_chatSession->isConfigured())
                    m_chatSession->sendMessage(m_lastUserPrompt);
            });
            found = true;
            break;
        }

        // Anti-loop: all fallbacks exhausted
        if (!found) {
            m_chatView->showErrorMessage("Todos los modelos de respaldo fallaron.");
            m_fallbackErrorCount = 0;
            m_originalProviderId.clear();
            m_originalModelId.clear();
            m_triedFallbackKeys.clear();
        }
        updateContextMeterForCurrentChat();
    });
    connect(m_chatSession, &ChatSession::cancelled, this, [this]() {
        m_fallbackErrorCount = 0;
        m_originalProviderId.clear();
        m_originalModelId.clear();
        m_triedFallbackKeys.clear();
        hideFallbackNotification();
        m_core->setState(CoreState::Idle);
        m_chatView->abortStream();
    });

    // Model detection from LM Studio
    connect(m_chatSession, &ChatSession::modelsDetected, this, [this](const QStringList &modelIds) {
        qDebug() << "Models detected from LM Studio:" << modelIds;
        if (modelIds.isEmpty()) return;
        // If current model ID is empty or not in detected list, use first detected
        ProviderConfig cfg = m_settingsView->providerConfig();
        if (cfg.modelId.isEmpty() || !modelIds.contains(cfg.modelId)) {
            cfg.modelId = modelIds.first();
            m_settingsView->setProviderConfig(cfg);
            m_chatSession->setProviderConfig(cfg);
            updateModelBadges();
            m_core->setCurrentTask("Modelo detectado: " + prettyModelName(cfg.modelId));
            m_core->setState(CoreState::Idle);
        }
    });

    // ── Settings → Provider config sync ──
    connect(m_settingsView, &SettingsView::providerConfigChanged, this, &MainWindow::syncProviderConfig);

    m_stack->addWidget(m_dashboardView);
    m_stack->addWidget(m_projectView);
    m_stack->addWidget(m_chatView);
    m_stack->addWidget(m_settingsView);
    rightLayout->addWidget(m_stack, 1);

    m_bottomBar = new BottomBar(right);
    rightLayout->addWidget(m_bottomBar);

    rootLayout->addWidget(right, 1);

    connect(m_sidebar, &Sidebar::navigate, this, &MainWindow::handleNavigate);

    // Core → UI wiring
    connect(m_core, &VoryelCore::stateChanged, this, [this](CoreState state, CoreState) {
        m_chatView->setCoreState(state);
    });
    connect(m_core, &VoryelCore::stateChanged, this, [this](CoreState state, CoreState) {
        m_bottomBar->setCoreState(state);
    });
    // Taskbar overlay reflects current state whenever window not focused
    connect(m_core, &VoryelCore::stateChanged, this, [this](CoreState state, CoreState) {
        updateTaskbarOverlay(state);
    });
    connect(m_core, &VoryelCore::currentTaskChanged, m_bottomBar, &BottomBar::setCurrentTask);

    // Estado inicial
    m_titleBar->setActiveModel(m_activeModel);
    m_bottomBar->setActiveModel(m_activeModel);
    m_core->setCurrentTask("Voryel listo");
    m_core->setState(CoreState::Idle);

    handleNavigate(View::Dashboard);
}

MainWindow::~MainWindow() {
#if defined(Q_OS_WIN)
    for (auto iter = m_overlayIcons.constBegin(); iter != m_overlayIcons.constEnd(); ++iter)
        DestroyIcon(iter.value());
    m_overlayIcons.clear();
#endif
}

// ── Real chat: messageSent → ChatSession → LM Studio → streaming → ChatView ──
void MainWindow::handleRealChatMessage(const QString &prompt, const QStringList &attachments) {
    m_lastUserPrompt = prompt;
    m_triedFallbackKeys.clear();

    // ── Handle /compact command ──
    if (prompt.trimmed().startsWith("/compact", Qt::CaseInsensitive)) {
        compactCurrentChat(QString(), {});
        return;
    }

    // ── Auto-compaction check: if >=70%, compact first ──
    if (!m_contextCompacting && m_chatStore) {
        ChatSessionData *chat = m_chatStore->activeChat();
        if (chat && chat->compactionPending) {
            compactCurrentChat(prompt, attachments);
            return;
        }
    }

    m_core->setCurrentTask(prompt.isEmpty() ? "Chat con Voryel" : prompt);
    if (!m_chatSession || !m_chatSession->isConfigured()) {
        qDebug() << "MainWindow::handleRealChatMessage: NOT configured, returning early";
        return;
    }
    qDebug() << "MainWindow::handleRealChatMessage: provider config valid, sending to ChatSession";
    m_chatSession->setFallbacksEnabled(m_settingsView->fallbacksEnabled());
    if (m_chatStore) {
        int idx = m_chatStore->activeChatIndex();
        qDebug() << "MainWindow::handleRealChatMessage: saving user message to ChatStore, activeChatIndex =" << idx;
        ChatMessageData msg;
        msg.role = "user";
        msg.content = prompt;
        msg.attachments = attachments;
        m_chatStore->addMessage(idx, msg);
    }
    m_chatView->beginStreaming();
    m_chatSession->sendMessage(prompt, attachments);
    qDebug() << "MainWindow::handleRealChatMessage: sendMessage called, request started";
}

void MainWindow::syncProviderConfig() {
    ProviderConfig cfg = m_settingsView->providerConfig();
    m_chatSession->setProviderConfig(cfg);
    m_chatView->setDemoMode(!cfg.valid());
    if (cfg.valid()) {
        m_core->setCurrentTask("Proveedor local conectado: " + cfg.baseUrl);
    }
    updateModelBadges();
    updateContextMeterForCurrentChat();
    // Auto-detect models from LM Studio (local only — cloud providers
    // need auth headers, and Qt HTTP2 can crash on 401 response)
    if (cfg.valid() && (cfg.baseUrl.contains("localhost", Qt::CaseInsensitive)
                        || cfg.baseUrl.contains("127.0.0.1")
                        || cfg.baseUrl.contains("0.0.0.0"))) {
        m_chatSession->detectModels(cfg.baseUrl);
    }
}

void MainWindow::updateModelBadges() {
    ProviderConfig cfg = m_settingsView->providerConfig();
    bool demo = !m_chatSession || !m_chatSession->isConfigured();
    QString display = providerDisplay(cfg, demo);
    m_activeModel = display;
    m_titleBar->setActiveModel(display);
    if (m_bottomBar) m_bottomBar->setActiveModel(display);
    m_dashboardView->setActiveModel(display);
    m_chatView->setActiveModel(display);
    m_settingsView->setActiveModel(display);
}

void MainWindow::updateContextMeterForCurrentChat() {
    if (!m_chatStore || !m_settingsView || !m_chatSession) return;
    ProviderConfig cfg = m_settingsView->providerConfig();
    if (!cfg.valid()) {
        m_chatView->updateContextMeter(0, 32768, "-", "-", "-");
        return;
    }

    // Resolve context window
    QString providerId = PromptBuilder::detectProviderId(cfg.baseUrl);
    ContextWindowInfo winfo = TokenCounter::resolveContextWindow(
        cfg.modelId, providerId, cfg.contextWindowTokens);

    // Count messages that would be sent
    ChatSessionData *chat = m_chatStore->activeChat();
    if (!chat) {
        m_chatView->updateContextMeter(0, winfo.tokens, PromptBuilder::providerDisplayNameFromUrl(cfg.baseUrl), cfg.modelId, winfo.source);
        return;
    }

    // System prompt + envelope overhead
    int totalTokens = 0;
    totalTokens += TokenCounter::roughEstimate(
        QStringLiteral("Respond\u00e9s dentro de Voryel.\n\nIDENTIDAD:\n...\nNo inventes identidad, modelo ni capacidades."));

    // Current envelope estimate
    TaskEnvelopeParams dummyEnv;
    dummyEnv.mode = "chat";
    dummyEnv.activeProvider = PromptBuilder::providerDisplayNameFromUrl(cfg.baseUrl);
    dummyEnv.activeModel = cfg.modelId;
    dummyEnv.providerId = providerId;
    dummyEnv.modelId = cfg.modelId;
    dummyEnv.userMessage = "x"; // minimal
    QString env = PromptBuilder::buildEnvelope(dummyEnv);
    int envelopeBase = TokenCounter::roughEstimate(env) - 1;
    totalTokens += envelopeBase;

    // Context summary (if compacted)
    if (!chat->contextSummary.isEmpty()) {
        totalTokens += TokenCounter::roughEstimate(chat->contextSummary);
        totalTokens += TokenCounter::roughEstimate(
            "Resumen compacto de la conversaci\u00f3n hasta este punto:");
    }

    // Messages to send
    // If compacted, only send from compactedUpToIndex onwards (last 8-12 exact)
    int startIdx = 0;
    if (chat->compactedUpToIndex >= 0) {
        startIdx = qMax(chat->compactedUpToIndex,
                        chat->messages.size() - 12);
    }
    for (int i = startIdx; i < chat->messages.size(); ++i) {
        totalTokens += TokenCounter::roughEstimate(chat->messages[i].content);
        totalTokens += 8; // role + overhead
    }

    // Reserved output
    int reservedOutput = 2048;
    totalTokens += reservedOutput;

    // Cap at max
    int maxTokens = winfo.tokens;
    int displayTokens = qMin(totalTokens, maxTokens);

    // Store in chat session data
    chat->lastEstimatedContextTokens = displayTokens;
    chat->lastEstimatedContextMaxTokens = maxTokens;
    chat->lastContextProviderId = providerId;
    chat->lastContextModelId = cfg.modelId;
    chat->tokenEstimateAccuracy = winfo.source;

    // Check compaction threshold
    double pct = maxTokens > 0 ? (double)displayTokens / maxTokens * 100.0 : 0.0;
    chat->compactionPending = (pct >= 70.0 && chat->messages.size() > 6);

    // Update meter
    QString accuracyLabel;
    if (winfo.source == "provider")          accuracyLabel = "exacta";
    else if (winfo.source == "local_catalog") accuracyLabel = "informada por proveedor";
    else if (winfo.source == "user_manual")   accuracyLabel = "exacta";
    else                                       accuracyLabel = "estimada";

    m_chatView->updateContextMeter(displayTokens, maxTokens,
                                    PromptBuilder::providerDisplayNameFromUrl(cfg.baseUrl),
                                    cfg.modelId, accuracyLabel, chat->compactionPending);

    qDebug() << "ContextMeter: chatId=" << chat->id.left(16)
             << "providerId=" << providerId
             << "modelId=" << cfg.modelId
             << "contextWindowTokens=" << winfo.tokens
             << "contextWindowSource=" << winfo.source
             << "estimatedInputTokens=" << (displayTokens - reservedOutput)
             << "reservedOutputTokens=" << reservedOutput
             << "totalContextTokens=" << displayTokens
             << "usagePercent=" << QString::number(pct, 'f', 1)
             << "accuracy=" << accuracyLabel;
}

void MainWindow::compactCurrentChat(const QString &userPendingMessage, const QStringList &pendingAttachments) {
    if (!m_chatStore || !m_chatSession || m_contextCompacting) return;
    ChatSessionData *chat = m_chatStore->activeChat();
    if (!chat || chat->messages.isEmpty()) {
        handleRealChatMessage(userPendingMessage, pendingAttachments);
        return;
    }

    m_contextCompacting = true;
    m_core->setCurrentTask("Compactando contexto...");
    m_chatView->setCoreState(CoreState::Thinking);
    if (m_bottomBar) m_bottomBar->setCurrentTask("Compactando contexto...");

    QString oldMessages;
    for (int i = 0; i < chat->messages.size(); ++i) {
        const auto &msg = chat->messages[i];
        oldMessages += msg.role + ": " + msg.content.left(500) + "\n---\n";
    }

    ProviderConfig cfg = m_settingsView->providerConfig();
    TaskEnvelopeParams envParams;
    envParams.mode = "compact_session";
    envParams.activeProvider = PromptBuilder::providerDisplayNameFromUrl(cfg.baseUrl);
    envParams.activeModel = cfg.modelId;
    envParams.providerId = PromptBuilder::detectProviderId(cfg.baseUrl);
    envParams.modelId = cfg.modelId;
    envParams.userMessage = oldMessages;

    QString compactPrompt = PromptBuilder::buildEnvelope(envParams);
    int beforeTokens = chat->lastEstimatedContextTokens;

    QVector<Message> compactMessages;
    compactMessages.append({"system", QStringLiteral(
        "Respond\u00e9s dentro de Voryel. Segu\u00ed las reglas del VORYEL_TASK que se incluye en el mensaje del usuario. "
        "No inventes identidad, modelo ni capacidades.")});
    compactMessages.append({"user", compactPrompt});

    // Temporary OpenAIClient for the compact request
    auto *compactClient = new OpenAIClient(this);
    connect(compactClient, &OpenAIClient::finished, this,
        [this, compactClient, chat, userPendingMessage, pendingAttachments, beforeTokens](const QString &response) {
            compactClient->deleteLater();

            chat->contextSummary = response;
            chat->compactedUpToIndex = chat->messages.size() - 1;
            chat->compactionCreatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            chat->compactionPending = false;
            chat->estimatedTokensBeforeCompaction = beforeTokens;
            chat->estimatedTokensAfterCompaction = beforeTokens / 3;

            m_contextCompacting = false;
            m_core->setCurrentTask("Contexto compactado");
            m_core->setState(CoreState::Idle);
            if (m_bottomBar) m_bottomBar->setCurrentTask("");

            updateContextMeterForCurrentChat();

            if (!userPendingMessage.isEmpty() || !pendingAttachments.isEmpty())
                handleRealChatMessage(userPendingMessage, pendingAttachments);
        });
    connect(compactClient, &OpenAIClient::errorOccurred, this,
        [this, compactClient, chat, userPendingMessage, pendingAttachments](const QString &) {
            compactClient->deleteLater();
            m_contextCompacting = false;
            m_core->setCurrentTask("No se pudo compactar. Enviando mensaje normalmente.");
            m_core->setState(CoreState::Idle);
            if (m_bottomBar) m_bottomBar->setCurrentTask("");

            if (!userPendingMessage.isEmpty() || !pendingAttachments.isEmpty())
                handleRealChatMessage(userPendingMessage, pendingAttachments);
        });

    compactClient->sendChatCompletion(compactMessages, cfg);
}

void MainWindow::showFallbackNotification(const QString &oldModel, const QString &newModel) {
    hideFallbackNotification();
    auto *contentArea = m_titleBar->parentWidget();
    int cardW = 340, cardH = 90;
    int margin = 10;
    int x = contentArea->width() - cardW - margin;
    int y = margin;

    m_fallbackCard = new QWidget(contentArea);
    m_fallbackCard->setObjectName("fallbackCard");
    m_fallbackCard->setFixedSize(cardW, cardH);
    m_fallbackCard->move(x, y);
    m_fallbackCard->setStyleSheet(
        "QWidget#fallbackCard { background: white; border: 2px solid black; border-radius: 8px; }"
    );

    // Hard-shadow widget positioned behind the card
    auto *shadow = new QWidget(contentArea);
    shadow->setObjectName("fallbackShadow");
    shadow->setFixedSize(cardW, cardH);
    shadow->move(x + 3, y + 3);
    shadow->setStyleSheet(
        "QWidget#fallbackShadow { background: black; border-radius: 8px; }"
    );
    shadow->lower();

    auto *layout = new QVBoxLayout(m_fallbackCard);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(4);

    auto *header = new QWidget();
    header->setStyleSheet("border: none;");
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel("Cambio de modelo autom\u00e1tico");
    title->setStyleSheet("font-weight: bold; font-size: 13px; color: black; border: none;");
    hl->addWidget(title);
    hl->addStretch();

    auto *closeBtn = new QPushButton("\u2715");
    closeBtn->setFixedSize(20, 20);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; font-size: 14px; color: #666; }"
        "QPushButton:hover { color: black; }"
    );
    connect(closeBtn, &QPushButton::clicked, this, &MainWindow::hideFallbackNotification);
    hl->addWidget(closeBtn);

    layout->addWidget(header);

    auto *msg = new QLabel(QString("El modelo <b>%1</b> fall\u00f3 repetidamente.<br>"
                                   "Se usar\u00e1 <b>%2</b> como activo.")
                           .arg(oldModel.toHtmlEscaped(), newModel.toHtmlEscaped()));
    msg->setWordWrap(true);
    msg->setStyleSheet("font-size: 12px; color: #333; border: none;");
    layout->addWidget(msg);

    if (m_fallbackCardTimer) m_fallbackCardTimer->stop();
    m_fallbackCardTimer = new QTimer(this);
    m_fallbackCardTimer->setSingleShot(true);
    m_fallbackCardTimer->setInterval(10000);
    connect(m_fallbackCardTimer, &QTimer::timeout, this, &MainWindow::hideFallbackNotification);
    m_fallbackCardTimer->start();
    m_fallbackCard->show();
}

void MainWindow::hideFallbackNotification() {
    if (m_fallbackCardTimer) {
        m_fallbackCardTimer->stop();
        m_fallbackCardTimer = nullptr;
    }
    if (m_fallbackCard) {
        auto *contentArea = m_fallbackCard->parentWidget();
        if (contentArea) {
            for (auto *child : contentArea->children()) {
                if (auto *w = qobject_cast<QWidget*>(child)) {
                    if (w->objectName() == "fallbackShadow") {
                        w->deleteLater();
                        break;
                    }
                }
            }
        }
        m_fallbackCard->deleteLater();
        m_fallbackCard = nullptr;
    }
}

#if defined(Q_OS_WIN)
HICON MainWindow::createSymbolHICON(const QString &symbol, const QColor &bgColor, int alpha) {
    QColor fill = bgColor;
    fill.setAlpha(alpha);
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        // Shadow
        p.setBrush(QColor(0, 0, 0, 200));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRect(5, 5, 25, 25), 4, 4);
        // Background
        p.setBrush(fill);
        p.setPen(QPen(QColor(0, 0, 0), 3));
        p.drawRoundedRect(QRect(2, 2, 25, 25), 4, 4);
        // Symbol
        p.setFont(QFont("Segoe UI", 17, QFont::Bold));
        p.setPen(Qt::white);
        p.drawText(QRect(2, 2, 25, 25), Qt::AlignCenter, symbol);
    }
    QImage argb = img.convertToFormat(QImage::Format_ARGB32);
    int w = argb.width(), h = argb.height();
    HDC hdc = GetDC(nullptr);
    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = -h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP hbmColor = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bits) memcpy(bits, argb.constBits(), w * h * 4);
    HBITMAP hbmMask = CreateBitmap(w, h, 1, 1, nullptr);
    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;
    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    ReleaseDC(nullptr, hdc);
    return hIcon;
}

HICON MainWindow::iconById(const QString &id, int alpha) {
    QString key = id + (alpha < 255 ? "_dim" : "");
    if (!m_overlayIcons.contains(key)) {
        QColor bg;
        if (id == "\u25B6")          bg = QColor(0x28,0xC8,0x40); // green play
        else if (id == "?")           bg = QColor(0x7C,0x3A,0xED); // violet
        else if (id == "error")       bg = QColor(0xF9,0x73,0x16); // orange
        else                          bg = QColor(0xE5,0x39,0x35); // red
        m_overlayIcons[key] = createSymbolHICON(id, bg, alpha);
    }
    return m_overlayIcons[key];
}

void MainWindow::setOverlayIcon(HICON hIcon) {
    HWND hwnd = (HWND)winId();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ITaskbarList3 *pTaskbar = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITaskbarList3, (void**)&pTaskbar);
    if (SUCCEEDED(hr) && pTaskbar) {
        pTaskbar->SetOverlayIcon(hwnd, hIcon, L"Voryel");
        pTaskbar->Release();
    }
    CoUninitialize();
}

void MainWindow::startPulse() {
    stopPulse();
    m_pulseTimer = new QTimer(this);
    m_pulseVisible = true;
    connect(m_pulseTimer, &QTimer::timeout, this, [this]() {
        m_pulseVisible = !m_pulseVisible;
        setOverlayIcon(m_pulseVisible ? iconById("\u25B6") : iconById("\u25B6", 50));
    });
    m_pulseTimer->start(600);
    setOverlayIcon(iconById("\u25B6"));
}

void MainWindow::stopPulse() {
    if (m_pulseTimer) {
        m_pulseTimer->stop();
        m_pulseTimer->deleteLater();
        m_pulseTimer = nullptr;
    }
}

void MainWindow::flashWindow(int durationMs) {
    HWND hwnd = (HWND)winId();
    FlashWindow(hwnd, TRUE);
    if (durationMs > 0)
        QTimer::singleShot(durationMs, this, [hwnd]() { FlashWindow(hwnd, FALSE); });
}
#endif

void MainWindow::clearTaskbarAlert() {
#if defined(Q_OS_WIN)
    stopPulse();
    setOverlayIcon(nullptr);
#endif
}

void MainWindow::updateTaskbarOverlay(CoreState state) {
    QString symbol;
    QColor bgColor;
    switch (state) {
    case CoreState::AwaitingApproval:
        if (m_lastTaskLabel.contains("permiso", Qt::CaseInsensitive)) {
            symbol = "?"; bgColor = QColor(0x7C,0x3A,0xED);
        } else {
            symbol = "!"; bgColor = QColor(0xE5,0x39,0x35);
        }
        break;
    default:
        break;
    }
    // Sidebar chat badge — only set; cleared when entering chat page
    if (symbol.isEmpty())
        m_sidebar->clearChatBadge();
    else
        m_sidebar->setChatBadge(symbol, bgColor);
    // Taskbar overlay — shown when unfocused, cleared after 5s delay on focus
#if defined(Q_OS_WIN)
    if (symbol.isEmpty() || isMinimized()) {
        clearTaskbarAlert();
    } else if (!isActiveWindow()) {
        setOverlayIcon(iconById(symbol == "?" ? "?" : "!"));
    }
#endif
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
#if defined(Q_OS_WIN)
    MSG *msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST) {
        const QPoint screenPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
        const QPoint titleBarPos = m_titleBar->mapFromGlobal(screenPos);
        if (titleBarPos.y() >= 0 && titleBarPos.y() <= Style::TOPBAR_HEIGHT) {
            if (m_titleBar->isDragRegion(titleBarPos)) {
                *result = HTCAPTION;
                return true;
            }
        }
        // Let Windows handle resize edges natively via WS_THICKFRAME
        return false;
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::WindowStateChange) {
        m_titleBar->syncMaximizeButton();
    }
    if (event->type() == QEvent::WindowStateChange || event->type() == QEvent::ActivationChange) {
        if (isMinimized()) {
            clearTaskbarAlert();
        } else if (isActiveWindow()) {
            if (m_overlayTimer) m_overlayTimer->start();
        } else {
            if (m_overlayTimer) m_overlayTimer->stop();
            updateTaskbarOverlay(m_core ? m_core->state() : CoreState::Idle);
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::handleNavigate(View view) {
    const int targetIndex = static_cast<int>(view);
    m_sidebar->setActiveView(view);
    if (m_stack->currentIndex() == targetIndex) return;

    // Clear sidebar badge when user enters chat
    if (view == View::Chat) m_sidebar->clearChatBadge();

    // Importante: no animamos el QStackedWidget completo con QGraphicsOpacityEffect.
    // En estas páginas hay muchos hijos con QGraphicsEffect propios (sombras duras
    // neobrutalistas, popovers, cards animadas). Aplicar otro efecto encima del
    // árbol completo puede provocar cierres/crashes diferidos al cambiar de sección.
    // El cambio de sección queda instantáneo y estable; las microanimaciones viven
    // dentro de cada vista/componente, donde no rompen el árbol principal.
    m_stack->setCurrentIndex(targetIndex);
}


void MainWindow::showModelPopover(const QPoint &globalPos) {
    if (m_modelPopover) {
        m_modelPopover->close();
        m_modelPopover = nullptr;
        return;
    }

    m_modelPopover = new QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint);
    m_modelPopover->setAttribute(Qt::WA_DeleteOnClose, true);
    m_modelPopover->setAttribute(Qt::WA_TranslucentBackground, true);
    connect(m_modelPopover, &QObject::destroyed, this, [this]() { m_modelPopover = nullptr; });

    auto *outer = new QVBoxLayout(m_modelPopover);
    outer->setContentsMargins(0, 0, 3, 3);
    outer->setSpacing(0);

    auto *card = new SolidPanel(m_modelPopover);
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(12);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 3, 3);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 17, 15);
    layout->setSpacing(10);

    auto makeSection = [](const QString &text) {
        auto *label = new QLabel(text);
        label->setStyleSheet(QString("font-size: 11px; font-weight: 900; color: %1; letter-spacing: 0.8px; border: none;").arg(Style::TEXT_MUTED));
        return label;
    };

    auto makeDivider = []() {
        auto *line = new QFrame();
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(QString("background: %1; max-height: 2px; border: none;").arg(Style::BORDER_SOFT));
        return line;
    };

    layout->addWidget(makeSection("MODELO ACTIVO"));
    auto *modelRow = new QHBoxLayout();
    modelRow->setSpacing(8);
    auto *dot = new QLabel();
    dot->setAttribute(Qt::WA_StyledBackground, true);
    dot->setFixedSize(8, 8);
    dot->setStyleSheet(QString("background: %1; border-radius: 4px; border: none;").arg(Style::GREEN));
    modelRow->addWidget(dot);
    auto *modelName = new QLabel(m_activeModel);
    modelName->setStyleSheet(QString("font-size: 13px; font-weight: 900; color: %1; border: none;").arg(Style::INK));
    modelRow->addWidget(modelName, 1);
    layout->addLayout(modelRow);

    auto *fallbackTitle = new QLabel("Configuración:");
    fallbackTitle->setStyleSheet(QString("font-size: 11px; color: %1; font-weight: 800; border: none;").arg(Style::TEXT_MUTED));
    layout->addWidget(fallbackTitle);
    ProviderConfig cfg = m_settingsView->providerConfig();
    const QStringList info = { cfg.baseUrl, cfg.modelId.isEmpty() ? "Sin modelo" : cfg.modelId };
    for (int i = 0; i < info.size(); ++i) {
        auto *row = new QHBoxLayout();
        row->setSpacing(6);
        auto *num = new QLabel(QString("%1.").arg(i + 1));
        num->setFixedWidth(18);
        num->setStyleSheet(QString("font-size: 11px; font-weight: 900; color: %1; border: none;").arg(Style::TEXT_FAINT));
        auto *name = new QLabel(info[i]);
        name->setStyleSheet(QString("font-size: 11px; color: %1; border: none;").arg(Style::TEXT_MUTED));
        row->addWidget(num);
        row->addWidget(name, 1);
        layout->addLayout(row);
    }

    layout->addWidget(makeDivider());
    layout->addWidget(makeSection("MENSAJES"));
    auto *msgInfo = new QLabel("Los mensajes se guardan localmente en chats.json");
    msgInfo->setWordWrap(true);
    msgInfo->setStyleSheet(QString("font-size: 11px; color: %1; border: none;").arg(Style::TEXT_MUTED));
    layout->addWidget(msgInfo);

    outer->addWidget(card);
    m_modelPopover->resize(260, m_modelPopover->sizeHint().height());

    QPoint pos = globalPos + QPoint(-m_modelPopover->width(), 8);
    if (auto *screen = QApplication::screenAt(globalPos)) {
        const QRect available = screen->availableGeometry();
        if (pos.x() < available.left() + 8) pos.setX(available.left() + 8);
        if (pos.y() + m_modelPopover->height() > available.bottom() - 8) {
            pos.setY(available.bottom() - m_modelPopover->height() - 8);
        }
    }
    m_modelPopover->move(pos);

    auto *effect = new QGraphicsOpacityEffect(m_modelPopover);
    effect->setOpacity(0.0);
    m_modelPopover->setGraphicsEffect(effect);
    m_modelPopover->show();

    auto *anim = new QPropertyAnimation(effect, "opacity", m_modelPopover);
    anim->setDuration(140);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

QWidget* MainWindow::makePlaceholderPage(const QString &title, const QString &subtitle) {
    auto *page = new QWidget();
    page->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto *titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(QString("font-size: 20px; font-weight: 900; color: %1;").arg(Style::INK));
    auto *subLabel = new QLabel(subtitle);
    subLabel->setStyleSheet(QString("font-size: 13px; color: %1;").arg(Style::TEXT_MUTED));

    layout->addWidget(titleLabel);
    layout->addWidget(subLabel);
    return page;
}
