#include "theme.h"

static theme_t *active_theme = NULL;

void theme_init(theme_t *t, theme_id_t id) {
    t->id = id;
    switch (id) {
        case THEME_DARK:
            t->colors[THEME_SLOT_BACKGROUND] = RGB(32, 32, 32);
            t->colors[THEME_SLOT_PANEL] = RGB(64, 64, 64);
            t->colors[THEME_SLOT_PRIMARY] = RGB(64, 128, 255);
            t->colors[THEME_SLOT_ACCENT] = RGB(255, 128, 0);
            t->colors[THEME_SLOT_TEXT] = RGB(255, 255, 255);
            t->colors[THEME_SLOT_TEXT_MUTED] = RGB(128, 128, 128);
            t->colors[THEME_SLOT_BORDER] = RGB(96, 96, 96);
            t->colors[THEME_SLOT_SUCCESS] = RGB(0, 255, 0);
            t->colors[THEME_SLOT_WARNING] = RGB(255, 165, 0);
            t->colors[THEME_SLOT_ERROR] = RGB(255, 0, 0);
            break;
        case THEME_LIGHT:
            t->colors[THEME_SLOT_BACKGROUND] = RGB(240, 240, 240);
            t->colors[THEME_SLOT_PANEL] = RGB(255, 255, 255);
            t->colors[THEME_SLOT_PRIMARY] = RGB(0, 0, 255);
            t->colors[THEME_SLOT_ACCENT] = RGB(0, 255, 165);
            t->colors[THEME_SLOT_TEXT] = RGB(32, 32, 32);
            t->colors[THEME_SLOT_TEXT_MUTED] = RGB(192, 192, 192);
            t->colors[THEME_SLOT_BORDER] = RGB(224, 224, 224);
            t->colors[THEME_SLOT_SUCCESS] = RGB(0, 255, 0);
            t->colors[THEME_SLOT_WARNING] = RGB(255, 165, 0);
            t->colors[THEME_SLOT_ERROR] = RGB(255, 0, 0);
            break;
        case THEME_HIGH_CONTRAST:
            t->colors[THEME_SLOT_BACKGROUND] = RGB(0, 0, 0);
            t->colors[THEME_SLOT_PANEL] = RGB(0, 0, 0);
            t->colors[THEME_SLOT_PRIMARY] = RGB(255, 255, 255);
            t->colors[THEME_SLOT_ACCENT] = RGB(255, 255, 0);
            t->colors[THEME_SLOT_TEXT] = RGB(255, 255, 255);
            t->colors[THEME_SLOT_TEXT_MUTED] = RGB(192, 192, 192);
            t->colors[THEME_SLOT_BORDER] = RGB(255, 255, 255);
            t->colors[THEME_SLOT_SUCCESS] = RGB(0, 255, 0);
            t->colors[THEME_SLOT_WARNING] = RGB(255, 255, 0);
            t->colors[THEME_SLOT_ERROR] = RGB(255, 0, 0);
            break;
    }
}

uint32_t theme_color(const theme_t *t, theme_slot_t slot) {
    if (slot < THEME_SLOT_MAX) {
        return t->colors[slot];
    }
    return RGB(255, 0, 255);
}

void theme_set_active(theme_t *t) {
    active_theme = t;
}

uint32_t theme_get(theme_slot_t slot) {
    if (active_theme == NULL) {
        return RGB(255, 0, 255);
    }
    return theme_color(active_theme, slot);
}

const char *theme_slot_name(theme_slot_t slot) {
    switch (slot) {
        case THEME_SLOT_BACKGROUND: return "Background";
        case THEME_SLOT_PANEL: return "Panel";
        case THEME_SLOT_PRIMARY: return "Primary";
        case THEME_SLOT_ACCENT: return "Accent";
        case THEME_SLOT_TEXT: return "Text";
        case THEME_SLOT_TEXT_MUTED: return "Text Muted";
        case THEME_SLOT_BORDER: return "Border";
        case THEME_SLOT_SUCCESS: return "Success";
        case THEME_SLOT_WARNING: return "Warning";
        case THEME_SLOT_ERROR: return "Error";
        default: return "Unknown";
    }
}

const char *theme_name(theme_id_t id) {
    switch (id) {
        case THEME_DARK: return "Dark";
        case THEME_LIGHT: return "Light";
        case THEME_HIGH_CONTRAST: return "High Contrast";
        default: return "Unknown";
    }
}