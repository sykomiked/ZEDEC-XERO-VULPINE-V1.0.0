/* theme.c — the ZXV palette. See theme.h. */
#include "theme.h"

static const uint32_t DEFAULT[THEME__COUNT] = {
    [THEME_VOID]     = 0x000000,
    [THEME_PANEL]    = 0x1A0000,
    [THEME_PANEL_HI] = 0x330000,
    [THEME_BORDER]   = 0x660000,
    [THEME_SCALE]    = 0x2E0A0A,
    [THEME_EMBER]    = 0xCC3300,
    [THEME_ACCENT]   = 0xFF6600,
    [THEME_GOLD]     = 0xFFD700,
    [THEME_TEXT]     = 0xF0E0E0,
    [THEME_DANGER]   = 0xAA0000,
    [THEME_JADE]     = 0x33AA66,
    [THEME_ARCANE]   = 0x9944CC,
    [THEME_AZURE]    = 0x3388CC,
};

static const uint32_t HAT[HATCOL__COUNT] = {
    [HATCOL_NONE]   = 0x555555,
    [HATCOL_WHITE]  = 0xF0F0F0,
    [HATCOL_BLACK]  = 0x2A2A2A,
    [HATCOL_RED]    = 0xCC2233,
    [HATCOL_BLUE]   = 0x3377CC,
    [HATCOL_PURPLE] = 0x9944CC,
    [HATCOL_GREEN]  = 0x33AA55,
    [HATCOL_CLEAR]  = 0xAACCDD,
    [HATCOL_YELLOW] = 0xE6C619,
    [HATCOL_ORANGE] = 0xEE7722,
    [HATCOL_GREY]   = 0x888888,
};

void theme_init(theme_t *t) {
    if (!t) return;
    for (int i = 0; i < THEME__COUNT; i++) t->color[i] = DEFAULT[i];
}

bool theme_set(theme_t *t, theme_color_t slot, uint32_t rgb) {
    if (!t || (uint32_t)slot >= THEME__COUNT) return false;
    t->color[slot] = rgb & 0x00FFFFFFu;
    return true;
}

uint32_t theme_get(const theme_t *t, theme_color_t slot) {
    if (!t || (uint32_t)slot >= THEME__COUNT) return 0;
    return t->color[slot];
}

void theme_reset(theme_t *t, theme_color_t slot) {
    if (!t) return;
    if ((uint32_t)slot >= THEME__COUNT) { theme_init(t); return; }
    t->color[slot] = DEFAULT[slot];
}

uint32_t theme_hat_color(theme_hat_t hat) {
    if ((uint32_t)hat >= HATCOL__COUNT) return HAT[HATCOL_NONE];
    return HAT[hat];
}

const char *theme_slot_name(theme_color_t s) {
    switch (s) {
    case THEME_VOID: return "void"; case THEME_PANEL: return "panel";
    case THEME_PANEL_HI: return "panel_hi"; case THEME_BORDER: return "border";
    case THEME_SCALE: return "scale"; case THEME_EMBER: return "ember";
    case THEME_ACCENT: return "accent"; case THEME_GOLD: return "gold";
    case THEME_TEXT: return "text"; case THEME_DANGER: return "danger";
    case THEME_JADE: return "jade"; case THEME_ARCANE: return "arcane";
    case THEME_AZURE: return "azure"; default: return "?";
    }
}
