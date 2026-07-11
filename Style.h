#pragma once
#include <QString>

// ─────────────────────────────────────────────────────────
// Paleta Voryel — tomada 1:1 del mockup (modo claro)
// Un solo lugar para cambiar colores/medidas en toda la app.
// ─────────────────────────────────────────────────────────
namespace Style {
inline constexpr const char* BG_LILAC     = "#f0ebf8";
inline constexpr const char* SIDEBAR_BG   = "#ede8f7";
inline constexpr const char* WHITE        = "#ffffff";
inline constexpr const char* INK          = "#111111";
inline constexpr const char* VIOLET       = "#7c3aed";
inline constexpr const char* VIOLET_DARK  = "#6d28d9";
inline constexpr const char* VIOLET_LIGHT = "#d8b4fe";
inline constexpr const char* TEXT_MUTED   = "#555555";
inline constexpr const char* TEXT_FAINT   = "#888888";
inline constexpr const char* BORDER_SOFT  = "#e4dff2";
inline constexpr const char* GREEN        = "#059669";
inline constexpr const char* GREEN_LIGHT  = "#86efac";
inline constexpr const char* YELLOW       = "#fde68a";

inline constexpr int SIDEBAR_WIDTH = 56;
inline constexpr int TOPBAR_HEIGHT = 40;
inline constexpr int BOTTOMBAR_HEIGHT = 56;

// Medidas base para que las vistas respiren igual en ventana y fullscreen.
inline constexpr int PAGE_MARGIN_X = 32;
inline constexpr int PAGE_MARGIN_Y = 24;
inline constexpr int CONTENT_MAX_WIDTH = 1180;
inline constexpr int CARD_RADIUS = 12;
inline constexpr int CARD_BORDER = 2;
inline constexpr int SHADOW_TINY = 1;
inline constexpr int SHADOW_SOFT = 2;
inline constexpr int SHADOW_NORMAL = 3;

inline QString scrollAreaStyle() {
    return QString(
        "QScrollArea { background: %1; border: none; }"
        "QScrollArea::viewport { background: %1; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 8px 3px 8px 3px; }"
        "QScrollBar::handle:vertical { background: %2; border-radius: 4px; min-height: 36px; }"
        "QScrollBar::handle:vertical:hover { background: %3; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: transparent; border: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollBar:horizontal { height: 0px; background: transparent; }"
    ).arg(BG_LILAC, VIOLET_LIGHT, VIOLET);
}
}
