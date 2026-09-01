#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QMap>
#include "Views.h"
#include "core/VoryelCore.h"
#include "model/ModelTypes.h"
#include <QColor>

class Sidebar;
class TitleBar;
class BottomBar;
class DashboardView;
class ProjectView;
class ChatView;
class SettingsView;
class ChatSession;
class ChatStore;

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

// ─────────────────────────────────────────────────────────
// RESPONSABILIDADES ACTUALES DE MainWindow
// ─────────────────────────────────────────────────────────
// 1. Frameless window (Win32 GWL_STYLE sin WS_CAPTION)
// 2. Layout raíz: Sidebar | [TitleBar + QStackedWidget + BottomBar]
// 3. Navegación entre vistas (handleNavigate)
// 4. Bridge SettingsView → resto de vistas (modelo, animaciones)
// 5. Bridge ChatView.taskStateChanged → VoryelCore.setState + SoundUtil
// 6. Bridge DashboardView/ProjectView → ChatView + navegación
// 7. Taskbar overlay Win32 (ITaskbarList3, HICON, timer 5s)
// 8. Model popover (showModelPopover)
// 9. Datos del dashboard (proyectos, sesiones y workflows)
//
// SEPARACIÓN FUTURA (gradual, sin refactor masivo):
//   a) TaskbarOverlayManager  → #7 (completo: HICON, pulse, flash, timer)
//   b) ModelPopover           → #8 (widget standalone en TitleBar)
//   c) MockDataSource         → #9 (datos de ejemplo fuera del constructor)
//   d) NavigationController   → #2, #3 (lógica de navegación + stack)
//   e) SettingsMediator       → #4 (sincroniza modelo/animaciones entre vistas)
//   f) StateSoundBridge       → #5 (reacciona a stateChanged → sonido)
// ─────────────────────────────────────────────────────────
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void changeEvent(QEvent *event) override;

private slots:
    void handleNavigate(View view);
    void showModelPopover(const QPoint &globalPos);

private:
    // ── Taskbar overlay (Win32) ──
    // FUTURO: mover a TaskbarOverlayManager
    void updateTaskbarOverlay(CoreState state);
    void clearTaskbarAlert();
#if defined(Q_OS_WIN)
    HICON createSymbolHICON(const QString &symbol, const QColor &bgColor, int alpha = 255);
    void setOverlayIcon(HICON hIcon);
    void startPulse();
    void stopPulse();
    void flashWindow(int durationMs = 2000);
    HICON iconById(const QString &id, int alpha = 255);
    QMap<QString, HICON> m_overlayIcons;
    QTimer *m_pulseTimer = nullptr;
    bool m_pulseVisible = true;
#endif

    // ── UI children ──
    Sidebar *m_sidebar = nullptr;
    TitleBar *m_titleBar = nullptr;
    BottomBar *m_bottomBar = nullptr;
    QStackedWidget *m_stack = nullptr;

    // ── Pages ──
    DashboardView *m_dashboardView = nullptr;
    ProjectView *m_projectView = nullptr;
    ChatView *m_chatView = nullptr;
    SettingsView *m_settingsView = nullptr;

    // FUTURO: ConnectionsView *m_connectionsView;
    // FUTURO: LocalModelsView  *m_localModelsView;

    QWidget *m_modelPopover = nullptr;
    // FUTURO: mover modelPopover a TitleBar o ModelPopoverWidget

    QTimer *m_overlayTimer = nullptr;
    // FUTURO: mover overlay timer a TaskbarOverlayManager

    VoryelCore *m_core = nullptr;
    ChatSession *m_chatSession = nullptr;
    ChatStore *m_chatStore = nullptr;
    QString m_activeModel = "Sin modelo";
    QString m_lastTaskLabel;
    QString m_lastUserPrompt;
    int m_fallbackErrorCount = 0;
    QString m_originalProviderId;
    QString m_originalModelId;
    QWidget *m_fallbackCard = nullptr;
    QTimer *m_fallbackCardTimer = nullptr;
    QStringList m_triedFallbackKeys;
    bool m_contextCompacting = false;
    bool m_skipNextAutoCompaction = false;
    TokenUsage m_pendingUsage;

    void handleRealChatMessage(const QString &prompt, const QStringList &attachments = QStringList());
    void showFallbackNotification(const QString &oldModel, const QString &newModel);
    void hideFallbackNotification();
    void syncProviderConfig();
    void syncChatSessionHistory();
    void updateModelBadges();
    void updateContextMeterForCurrentChat();
    void compactCurrentChat(const QString &userPendingMessage, const QStringList &pendingAttachments = QStringList());
    QWidget* makePlaceholderPage(const QString &title, const QString &subtitle);
};
