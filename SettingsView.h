#pragma once
#include <QWidget>
#include <QVector>
#include <QString>
#include <QMap>
#include "ToggleSwitch.h"
#include "model/ModelTypes.h"

class QLabel;
class QVBoxLayout;
class QLineEdit;
class QComboBox;
class QPushButton;
class SolidPanel;
class QScrollArea;
class QNetworkAccessManager;
class VoryelDialog;

struct ProviderDef {
    QString id;
    QString displayName;
    QString subtitle;
    QString description;
    bool isLocal = false;
    bool requiresApiKey = false;
    bool comingSoon = false;
    // Configured state
    bool configured = false;
    QString baseUrl;
    QString apiKey;
    bool custom = false;
};

struct QuickModelEntry {
    QString id;
    QString providerId;
    QString providerName;
    QString modelId;
    QString displayName;
    QString availability; // "Local", "Gratis", "Pago", "No informado", etc.
    int contextWindowTokens = 0; // 0=use catalog/default
    QString contextWindowSource;
};

class SettingsView : public QWidget {
    Q_OBJECT
public:
    explicit SettingsView(QWidget *parent = nullptr);

    void setActiveModel(const QString &model);
    QString activeModel() const { return m_activeModel; }
    bool isSoundEnabled() const { return m_soundCheck ? m_soundCheck->isOn() : true; }
    bool isAnimationEnabled() const { return m_animCheck ? m_animCheck->isOn() : true; }

    // Provider config
    ProviderConfig providerConfig() const;
    QString activeProviderId() const { return m_activeProviderId; }
    void setProviderConfig(const ProviderConfig &config);

    // Quick models
    QVector<QuickModelEntry> quickModels() const { return m_quickModels; }
    void addQuickModel(const QuickModelEntry &entry);
    void removeQuickModel(const QString &id);

    // Fallback
    bool fallbacksEnabled() const { return m_fallbacksEnabled; }
    void activateModel(const QString &providerId, const QString &modelId, int contextWindowTokens = 0);

signals:
    void modelSelected(const QString &display);
    void configurationSaved(const QString &provider, const QString &model);
    void statusMessageRequested(const QString &message);
    void settingsChanged();
    void providerConfigChanged();
    void activeModelChanged(const QString &providerId, const QString &modelId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString m_activeModel = "Sin modelo";
    QString m_permissionMode = "approved";

    // Providers
    QVector<ProviderDef> m_providers;
    int m_editingProviderIndex = -1;

    // Quick models
    QVector<QuickModelEntry> m_quickModels;
    static constexpr int MAX_QUICK_MODELS = 5;

    // Fallbacks
    QVector<QString> m_fallbackQuickModelIds;
    bool m_fallbacksEnabled = false;

    // Old decorative model state (removed — now using real provider config)

    QVector<SolidPanel*> m_permissionCards;
    QVector<SolidPanel*> m_themeCards;

    ToggleSwitch *m_animCheck = nullptr;
    ToggleSwitch *m_soundCheck = nullptr;
    ToggleSwitch *m_streamingCheck = nullptr;

    // Shared provider config fields
    QLineEdit *m_baseUrlEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLineEdit *m_providerNameEdit = nullptr;
    QLineEdit *m_modelIdEdit = nullptr;
    QWidget *m_providerConfigBlock = nullptr;
    VoryelDialog *m_providerDialog = nullptr;
    QLabel *m_providerConfigTitle = nullptr;
    bool m_addingCustomProvider = false;

    // Provider scroll area (for scroll propagation fix)
    QScrollArea *m_providerScroll = nullptr;
    QVBoxLayout *m_providerListLayout = nullptr;

    // Models section layout (for refresh after add/remove)
    QVBoxLayout *m_modelsListLayout = nullptr;
    QLabel *m_activeModelLabel = nullptr;

    // Add-model panel guard
    QWidget *m_addModelPanel = nullptr;
    QString m_pendingQuickModelStatus;

    // Network for model detection
    QNetworkAccessManager *m_networkManager = nullptr;
    QMap<QString, int> m_modelsDevContexts;
    QMap<QString, QString> m_modelsDevProviderNames;
    QMap<QString, QString> m_modelsDevProviderApis;
    bool m_modelsDevRequested = false;

    // Active provider+model tracking
    QString m_activeProviderId = "lm_studio";
    QString m_activeModelId;
    int m_activeModelContextWindow = 0;
    QString m_activeModelContextSource;

    void buildUi();
    void refreshPermissionCards();
    void refreshThemeCards();
    void choosePermission(const QString &modeId);
    void setActiveModelFromProvider();
    void finishAddingQuickModel();
    void closeAddModelPanel();
    void refreshProvidersList();
    void beginAddingCustomProvider();
    void removeCustomProvider(int index);
    void confirmDisconnectProvider(int index);
    void disconnectProvider(int index);
    void refreshModelsList();
    void loadSettings();
    void saveSettings() const;
    int providerIndexById(const QString &id) const;
    bool ensureQuickModel(const QString &providerId, const QString &modelId,
                          int contextWindowTokens = 0);
    void updateActiveModelLabel();

    QWidget* makeHeader();
    QWidget* makeSectionTitle(const QString &iconRes, const QString &title);
    QWidget* makeProvidersSection();
    QWidget* makeModelsSection();
    QWidget* makeFallbackSection();
    QWidget* makePermissionSection();
    QWidget* makeSecuritySection();
    QWidget* makeAppearanceSection();

    SolidPanel* makePermissionCard(const QString &id, const QString &label, const QString &description, const QString &iconRes);
    SolidPanel* makeToggleRow(const QString &label, ToggleSwitch **outCheck, bool checked = true);
    SolidPanel* makeBlockRow(const QString &label, bool enabled = true);
    SolidPanel* makeThemeCard(const QString &id, const QString &label, const QString &desc, const QString &iconRes);

    QPushButton* makePrimaryButton(const QString &text);
    QPushButton* makeGhostButton(const QString &text);
    QWidget* makeProviderRow(int idx);
    QWidget* makeQuickModelRow(int idx);
    QWidget* makeAddModelDialog();
    void fetchModelsForProvider(int providerIdx, QComboBox *combo, QVBoxLayout *resultLayout, QWidget *loadingLabel);
    void fetchModelsDevCatalog();
    int modelsDevContextFor(const ProviderDef &provider, const QString &modelId) const;
};
