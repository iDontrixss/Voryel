# Voryel — Arquitectura

## 1. Estados del Core (VoryelCore)

```
Idle ─→ Thinking ─→ Planning ─→ AwaitingApproval ─→ Executing ─→ RollingBack
 ↑                                                                        │
 └────────────────────────────────────────────────────────────────────────┘
                         (vía Cancelado / error)
```

| Estado | Significado |
|--------|------------|
| `Idle` | Sin tarea activa. Botón de enviar habilitado. |
| `Thinking` | Voryel está procesando/analizando el pedido. |
| `Planning` | Generando plan de acción. |
| `AwaitingApproval` | Esperando interacción del usuario (plan listo, permiso, diff listo). |
| `Executing` | Ejecutando comandos. |
| `RollingBack` | Revirtiendo cambios. (No usado en mock actual.) |

`setState()` es seguro: no emite nada si el estado es el mismo (`same-state no-op`).

---

## 2. Overlays (Taskbar + Sidebar Badge)

### Taskbar (Win32 ITaskbarList3)

| Condición | Símbolo | Color | Icono |
|-----------|---------|-------|-------|
| `AwaitingApproval` + label contiene "permiso" | `?` | Violeta `#7C3AED` | `iconById("?")` |
| `AwaitingApproval` + otro label (plan/diff) | `!` | Rojo `#E53935` | `iconById("!")` |
| `Thinking` / `Executing` | — | — | Sin overlay |
| `Idle` / otros | — | — | Sin overlay |

### Timing del overlay

| Evento | Comportamiento |
|--------|---------------|
| Ventana **pierde foco** (`!isActiveWindow()`) | Muestra overlay inmediatamente si corresponde |
| Ventana **gana foco** (`isActiveWindow()`) | Inicia timer 5000ms → luego `clearTaskbarAlert()` |
| Ventana **minimizada** | `clearTaskbarAlert()` inmediato |
| `overlayTimer::timeout` | `setOverlayIcon(nullptr)` via `clearTaskbarAlert()` |

### Sidebar Badge

- Misma lógica que taskbar overlay (`updateTaskbarOverlay` lo setea).
- Se **limpia solo** cuando el usuario navega a `View::Chat` (`handleNavigate`).
- No se limpia al ganar foco ni al minimizar.
- Persiste aunque se navegue a otra página (Dashboard/Project/Settings).

### Implementación técnica

- `createSymbolHICON()` → `QPainter` sobre `QImage 32×32` → `HICON` via `CreateIconIndirect`.
- Cache en `QMap<QString, HICON>` (destruidos en destructor con `DestroyIcon`).
- `setOverlayIcon(nullptr)` para limpiar.
- `flashWindow()` via Win32 `FlashWindow` (2s).

---

## 3. Sonidos (SoundUtil)

### Sonidos disponibles

| Archivo | Enum | Evento |
|---------|------|--------|
| `sounds/complete.mp3` | `Sound::Complete` | "Plan listo", "Diff listo" |
| `sounds/question.mp3` | `Sound::Question` | "Esperando permiso" |
| `sounds/error.mp3` | `Sound::Error` | "Cancelado" |

### Reglas

- **Default: OFF** (desde SettingsView).
- Siempre se verifica `m_settingsView->isSoundEnabled()` antes de reproducir.
- Si el archivo `.mp3` no existe (`QFile::exists`), se ignora silenciosamente.
- Si `mciSendString` falla (MCI no disponible), se ignora (no hay crash ni beep).
- No hay fallback a Beep ni a Qt Multimedia.
- Archivos MP3 se copian automáticamente al build output via CMake `add_custom_command`.

### Cómo se disparan (en MainWindow)

```
taskStateChanged("Plan listo")  → Sound::Complete
taskStateChanged("Diff listo")  → Sound::Complete
taskStateChanged("Esperando permiso") → Sound::Question
taskStateChanged("Cancelado")   → Sound::Error
```

---

## 4. Flujo de Chat Demo Mode (ChatView)

```
Usuario envía mensaje
  → handleSend()
  → ensureConversationStarted()     [elimina welcome state]
  → addUserMessage(text)
  → setWorking(true, "Voryel está pensando")
  → 15s timer
      → setWorking(false)
      → addAssistantMessageAnimated("Entendido...", onFinished)
          → onFinished: addPlanCard()
          → emit taskStateChanged("Plan listo")       ← overlay "!", sound Complete

      → Usuario click "▶ Continuar"
          → addAssistantMessageAnimated("Necesito ejecutar...", onFinished)
              → onFinished: addTerminalPermissionCard("npm run build")
              → emit taskStateChanged("Esperando permiso")  ← overlay "?", sound Question

          → Usuario click "Permitir una vez"
              → setWorking(true, "Ejecutando comando")
              → 800ms timer
                  → setWorking(false)
                  → addAssistantMessageAnimated("Comando ejecutado...", onFinished)
                      → onFinished: addDiffCard()
                      → emit taskStateChanged("Diff listo")    ← overlay "!", sound Complete

          → Usuario click "Cancelar"
              → addAssistantMessage("Cancelado...")
              → emit taskStateChanged("Cancelado")             ← overlay limpio, sound Error

          → Usuario click "Editar plan" / "Permitir siempre aquí" / "Aprobar cambio" / "Rechazar"
              → Sin implementación mock actual (solo UI placeholder)
```

### Reglas del flujo mock

- **No auto-advance**: cada card espera click del usuario.
- Primer mensaje: 15s de "thinking" (Timer).
- `allowTerminal`: 800ms de "ejecutando" (Timer).
- Las cards se alinean a la izquierda (como mensajes de Voryel) con margen `39px` (espacio del avatar).

---

## 5. Partes Mock vs. Reales

| Componente | Estado | Notas |
|-----------|--------|-------|
| **ChatView.15s timer** | ✅ Real → LM Studio | Streaming SSE con OpenAIClient |
| **ChatView.800ms timer** | ✅ Real → LM Studio | Mismo flujo, tokens reales |
| **Plan card** | ⏳ Mock (demo mode) | Pendiente: agente de código |
| **Terminal permission card** | ⏳ Mock (demo mode) | Pendiente: agente de código |
| **Diff card** | ⏳ Mock (demo mode) | Pendiente: agente de código |
| **ChatSession** | ✅ Implementado | Historial, streaming, cancelación |
| **OpenAIClient** | ✅ Implementado | QNetworkAccessManager + SSE parser |
| **ChatStore** | ✅ Implementado | Persistencia JSON multi-chat, CRUD sesiones |
| **Chat list panel** | ✅ Implementado | Sidebar izquierda en ChatView, crear/renombrar/eliminar chats |
| **Copy button** | ✅ Implementado | Botón copiar en burbujas de asistente, feedback visual |
| **Provider config** | ✅ Settings | Base URL, API Key, Model ID |
| **Dashboard stats** | ⏳ Mock | Datos de sesiones hardcoded |
| **Project list** | ⏳ Mock | projectEntries hardcoded |
| **Workflows** | ⏳ Mock | 4 cards fijas |
| **Model selection** | ⏳ Mock | Lista fija en SettingsView |
| **Fallbacks** | ⏳ Mock | Lista fija en model popover |
| **Permission mode** | ⏳ Mock | Solo UI |
| **Security blocks** | ⏳ Mock | Solo UI |
| **Task queue** | ⏳ Mock | 4 tareas hardcoded |

---

## 6. Clases implementadas y propuestas

| Clase | Estado | Responsabilidad |
|-------|--------|-----------------|
| **ChatSession** | ✅ Implementado | Gestiona historial de mensajes, llama a OpenAIClient, coordina streaming |
| **OpenAIClient** | ✅ Implementado | Cliente HTTP para APIs OpenAI-compatible, streaming SSE, cancelación |
| **ModelTypes** | ✅ Implementado | Structs Message, ProviderConfig |
| **VoryelCore** | ✅ Implementado | State machine central (Idle, Thinking, Planning, etc.) |
| **SessionManager** | ✅ Implementado (ChatStore) | Crear, guardar, cargar sesiones de chat. Historial persistente en JSON. |
| **ModelRouter** | ⏳ Futuro | Seleccionar modelo según tarea, manejar fallbacks, auto-switch. |
| **PermissionManager** | ⏳ Futuro | Evaluar reglas de permiso, decidir auto-aprobación vs. preguntar. |
| **ProjectContext** | ⏳ Futuro | Proyecto activo, estructura de archivos, metadatos, git info. |
| **SnapshotManager** | ⏳ Futuro | Crear/restaurar snapshots (checkpoints) antes de cambios. |
| **CommandGuard** | ⏳ Futuro | Validar comandos contra reglas de seguridad antes de ejecutar. |
| **TaskManager** | ⏳ Futuro | Cola de tareas, ejecución secuencial, estado de cada tarea. |
| **TaskbarOverlayManager** | ⏳ Futuro | Win32 overlay icons, pulse, flash, timer 5s (extraído de MainWindow). |
| **AppController** | ⏳ Futuro | Orquestar creación/conección de vistas, inicialización de servicios. |
| **MockDataSource** | ⏳ Futuro | Centralizar todos los datos de ejemplo (proyectos, stats, etc.). |

### Dependencias actuales

```
MainWindow
  ├── VoryelCore (state machine)
  ├── ChatSession
  │   └── OpenAIClient → LM Studio / APIs OpenAI-compatible
  ├── ChatView ← UI only
  ├── SettingsView (provider config)
  └── DashboardView, ProjectView, TitleBar, BottomBar, Sidebar
```

### Dependencias futuras

```
AppController
  ├── SessionManager ──── VoryelCore
  ├── ModelRouter ─────── PermissionManager
  ├── PermissionManager ── CommandGuard
  ├── ProjectContext ───── SnapshotManager
  ├── TaskManager ──────── VoryelCore, CommandGuard
  └── MainWindow ───────── TaskbarOverlayManager, todas las vistas
```

---

## 7. Chat real con LM Studio (OpenAI-compatible)

### Flujo

```
Usuario escribe "hola" → Enter
  │
  ▼
ChatView::handleSend()
  ├── addUserMessage("hola")
  ├── emit messageSent("hola")
  └── (en demo mode: lanza timer mock; en real mode: no hace nada)
         │
         ▼
MainWindow::handleRealChatMessage("hola")
  ├── m_chatView->beginStreaming()       ← crea burbuja vacía de Voryel
  ├── m_chatSession->sendMessage("hola")
  └── m_core->setState(Thinking)
         │
         ▼
ChatSession::sendMessage("hola")
  ├── agrega {"role":"user","content":"hola"} al historial
  ├── prepends {"role":"assistant","content":""} al historial
  ├── emit started()
  └── m_client->sendChatCompletion(m_messages, config)
         │
         ▼
OpenAIClient::sendChatCompletion()
  ├── POST {baseUrl}/chat/completions
  ├── Body: { model, messages, stream: true }
  └── Emite:
        ├── tokenReceived("¡Hola!") → MainWindow → ChatView::appendStreamToken()
        ├── tokenReceived(" ¿Cómo") → MainWindow → ChatView::appendStreamToken()
        ├── ... (cada chunk SSE) ...
        └── finished("¡Hola! ¿Cómo puedo ayudarte?") → MainWindow:
              ├── m_core->setState(Idle)
              └── SoundUtil::play(Complete)
```

### Modos de operación

| Modo | Condición | Comportamiento |
|------|-----------|---------------|
| **Real** | `ProviderConfig.valid()` = true | ChatSession llama a LM Studio, tokens reales |
| **Demo** | `ProviderConfig.valid()` = false | ChatView usa timer mock 15s, cards plan/permiso/diff |

### Configuración del proveedor

| Campo | Default | Notas |
|-------|---------|-------|
| Base URL | `http://localhost:1234/v1` | LM Studio por defecto, configurable |
| API Key | vacío | LM Studio local no necesita API key |
| Model ID | vacío | Ej: `llama-3.2-3b-instruct` (manual) |
| Streaming | `true` | Habilitado por defecto |

### SSE Streaming

```
data: {"id":"...","choices":[{"delta":{"role":"assistant"},"index":0}]}

data: {"id":"...","choices":[{"delta":{"content":"Hola"},"index":0}]}

data: [DONE]
```

Buffer de lectura: `m_sseBuffer` acumula `readyRead()` chunks, busca `\n\n` para eventos completos, parsea JSON.

### Cancelación

```cpp
m_chatSession->cancel()
  → OpenAIClient::cancel()
    → QNetworkReply::abort()
  → emit cancelled()
  → ChatView::abortStream()
  → VoryelCore::setState(Idle)
```

### Errores

| Error | Mensaje |
|-------|---------|
| Sin config | "Seleccioná o escribí un modelo local antes de enviar." |
| HTTP 404 | "Error HTTP 404: Not Found" |
| Sin conexión | "No pude conectar con LM Studio. Verificá que el servidor local esté activo en {baseUrl}" |
| JSON inválido | "Error al procesar la respuesta del servidor." |
| Cancelado | "Cancelado" |

---

## 8. Sistema multi-chat (ChatStore)

### Persistencia

- **Archivo:** `AppData/Voryel/chats.json` (Windows: `%LOCALAPPDATA%/Voryel/chats.json`)
- **Fallback:** `~/.voryel/chats.json`
- **Formato:** JSON con array de chats y `activeChatId`
- **Guardado:** Automático en cada mutación (crear, switch, delete, rename, addMessage, updateLastMessage)

### Estructura de datos

```cpp
struct ChatMessageData {
    QString role;     // "user" | "assistant"
    QString content;  // Texto del mensaje
    QString timestamp; // ISO 8601
};

struct ChatSessionData {
    QString id;          // UUID
    QString title;       // Auto-generado: primeros 35 chars del primer mensaje
    QString snippet;     // Último mensaje truncado (60 chars)
    QString createdAt;
    QVector<ChatMessageData> messages;
};
```

### ChatStore API

```cpp
ChatStore *store = new ChatStore(this);

// CRUD
int idx = store->createChat();          // Crea chat vacío, lo activa
store->switchToChat(idx);               // Cambia chat activo
store->deleteChat(idx);                 // Elimina chat (o resetea si es el último)
store->renameChat(idx, "Nuevo título"); // Renombra chat

// Mensajes
store->addMessage(idx, {"user", "hola"});
store->addMessage(idx, {"assistant", "¡Hola!"});
store->updateLastMessage(idx, "respuesta editada");

// Consulta
const auto &chats = store->chats();
int active = store->activeChatIndex();
```

### UI: Chat list panel en ChatView

```
┌──────────────────────────────────────────┐
│ Header: "Chat con Voryel"               │
├────────┬─────────────────────────────────┤
│ + Nuevo│                                 │
│────────│  Mensajes del chat activo       │
│ Chat 1 │  ┌─ burbuja usuario ──────┐     │
│ Chat 2 │  │ Hola Voryel            │     │
│ Chat 3 │  └────────────────────────┘     │
│        │  ┌─ burbujas asistente ───┐     │
│        │  │ ¡Hola! ...          [📋]│     │
│        │  └────────────────────────┘     │
│        │                                 │
├────────┴─────────────────────────────────┤
│ Input: "Pedile algo a Voryel..." [▶]     │
└──────────────────────────────────────────┘
```

- **Panel izquierdo:** 220px fijos, lista de chats con título + snippet
- **Chat activo:** Highlight con borde negro
- **Click:** Cambia chat activo (bloqueado durante Thinking)
- **Right-click:** Menú contextual (Renombrar, Eliminar)
- **"+ Nuevo chat":** Crea chat vacío y lo activa

### Copy button en burbujas

- Aparece solo en burbujas de asistente (no usuario)
- Icono 📋 → click → copia texto completo al clipboard
- Feedback: icono cambia a ✅ por 1.5s, luego vuelve a 📋
- Copia el texto completo (no el truncado visible)

### Protección durante generación

Si el usuario intenta cambiar de chat mientras Voryel está generando:
```
QMessageBox::information("Esperá a que termine la respuesta actual antes de cambiar de chat.")
```

---

## 9. Vistas de navegación

| # | View archivo | Widget | Descripción | Estado |
|---|-------------|--------|-------------|--------|
| 0 | DashboardView | `DashboardView.h/.cpp` | Workflows, stats, proyectos recientes | Mock |
| 1 | ProjectView | `ProjectView.h/.cpp` | Lista de proyectos, exploración | Mock |
| 2 | ChatView | `ChatView.h/.cpp` | Chat con Voryel, flujo interactivo | Mock |
| 3 | SettingsView | `SettingsView.h/.cpp` | Configuración de modelo, permisos, apariencia | Parcial |
| — | *ConnectionsView* | — | Proveedores externos, API keys, conexiones | **Futuro** |
| — | *LocalModelsView* | — | Modelos locales (Ollama, LM Studio, etc.) | **Futuro** |

### Cómo agregar una vista nueva

1. Agregar enum en `Views.h` (ej: `Connections`).
2. Crear `ConnectionsView.h/.cpp` heredando `QWidget`.
3. En `MainWindow.h` agregar `ConnectionsView *m_connectionsView`.
4. En `MainWindow.cpp`:
   - `#include "ConnectionsView.h"`
   - Crear instancia tras las vistas existentes.
   - `m_stack->addWidget(m_connectionsView)`.
   - No modificar índices existentes (el orden en `m_stack` debe coincidir con el enum).
5. En `Sidebar.cpp` agregar botón en `m_navItems`.
6. `handleNavigate` ya usa `static_cast<int>(view)` → funciona automáticamente.

---

## 10. Estructura de archivos

```
Voryel/
├── core/
│   ├── VoryelCore.h/.cpp          ← State machine
├── model/
│   ├── ModelTypes.h               ← Structs Message, ProviderConfig
│   ├── OpenAIClient.h/.cpp        ← Cliente HTTP OpenAI-compatible + streaming SSE
│   └── ChatSession.h/.cpp         ← Historial de mensajes, orquesta llamadas a OpenAIClient
├── chat/
│   └── ChatStore.h/.cpp           ← Persistencia JSON multi-chat, CRUD sesiones
├── sounds/
│   ├── complete.mp3               ← Sonido plan/diff listo
│   ├── question.mp3               ← Sonido esperando permiso
│   └── error.mp3                  ← Sonido cancelado
├── icons/                         ← 27 SVGs + 6 model logos + .ico (includes copy.svg)
├── CMakeLists.txt                 ← Qt6 Widgets+Svg+Network, winmm, copia sounds/
├── main.cpp                       ← Entry point, Fusion style, HighDPI
├── resources.qrc                  ← QRC con todos los SVGs e icono
├── Views.h                        ← Enum de navegación
├── Task.h                         ← TaskStatus, QueueStatus, Task struct
├── Style.h                        ← Paleta, layout, scrollAreaStyle()
├── IconUtil.h/.cpp                ← Tingeo de SVGs en runtime
├── SoundUtil.h/.cpp               ← Play MP3 via Win32 MCI
├── SolidPanel.h/.cpp              ← QFrame neo-brutalista pintado a mano
├── ToggleSwitch.h/.cpp            ← Toggle animado 44×24
├── Sidebar.h/.cpp                 ← Navegación vertical 56px + badge
├── TitleBar.h/.cpp                ← macOS dots, breadcrumb, model badge
├── BottomBar.h/.cpp               ← Estado, tareas, cola, modelo
├── DashboardView.h/.cpp           ← Página principal
├── ProjectView.h/.cpp             ← Explorador de proyectos
├── ChatView.h/.cpp                ← Chat con demo mode, streaming real, multi-chat, copy button
├── SettingsView.h/.cpp            ← Configuración + provider config (Base URL, API Key, Model ID)
├── MainWindow.h/.cpp              ← Layout raíz + frameless + overlay + bridge + ChatSession + ChatStore wiring
├── ARCHITECTURE.md                ← Este archivo
├── app.manifest                   ← DPI PerMonitorV2
└── app.rc                         ← Embebe manifest en .exe
```
