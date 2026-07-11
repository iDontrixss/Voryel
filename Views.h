#pragma once
// ─────────────────────────────────────────────────────────
// Vistas de navegación principal
// ─────────────────────────────────────────────────────────
// FUTURO: Agregar aquí sin romper nada:
//   Connections,   // → ConnectionsView   (proveedores/API keys externas)
//   LocalModels,   // → LocalModelsView   (modelos locales estilo Ollama)
//
// El Sidebar itera Views vía `static_cast<int>` para asignar índices.
// Agregar un nuevo enum antes de Settings NO rompe índices existentes
// (Dashboard=0, Project=1, Chat=2, Settings=3 se mantienen).
// ─────────────────────────────────────────────────────────
enum class View {
    Dashboard,
    Project,
    Chat,
    Settings,
    // ── futuros ──
    // Connections,
    // LocalModels,
};
