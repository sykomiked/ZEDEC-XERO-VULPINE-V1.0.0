/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* zxv_shell.c — the ZEDEC pqOS desktop shell. See zxv_shell.h. */
#include "zxv_shell.h"

/* ---- ZXV house palette (XRGB8888: 0x00RRGGBB) ---- */
#define C_BAR      0x0F0F16u
#define C_BAR_HI   0x1B1B28u
#define C_GOLD     0xE6C158u   /* Crown gold */
#define C_GOLD_DIM 0x8A7434u
#define C_RED      0x6E1417u   /* dragon red */
#define C_RED_DK   0x2A0B0Du
#define C_WINBODY  0x0A0A12u
#define C_TEXT     0xCED0DAu
#define C_DIM      0x7A7C8Au
#define C_GREEN    0x66DD88u
#define C_CYAN     0x62C6D6u
#define C_WHITE    0xF4F6FBu
#define C_BLACK    0x000000u

static void box(vbe_state_t *v, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    vbe_fill_rect(v, x, y, w, h, c);
}
static void frame(vbe_state_t *v, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    vbe_fill_rect(v, x, y, w, 1, c);
    vbe_fill_rect(v, x, y + h - 1, w, 1, c);
    vbe_fill_rect(v, x, y, 1, h, c);
    vbe_fill_rect(v, x + w - 1, y, 1, h, c);
}
/* transparent scaled text (glyph pixels only) */
static void gt(vbe_state_t *v, int32_t x, int32_t y, const char *s, uint32_t fg, int32_t scale) {
    vbe_draw_text_ex(v, x, y, s, fg, 0, scale, 0);
}

/* a titled window: drop shadow, gold edge, red title bar, controls */
static int32_t window(vbe_state_t *v, int32_t x, int32_t y, int32_t w, int32_t h,
                      const char *title, uint32_t accent) {
    box(v, x + 6, y + 6, w, h, 0x05050A);          /* shadow */
    box(v, x, y, w, h, C_WINBODY);
    frame(v, x, y, w, h, accent);
    box(v, x + 1, y + 1, w - 2, 24, C_RED_DK);      /* title bar */
    vbe_fill_rect(v, x + 1, y + 25, w - 2, 1, C_GOLD_DIM);
    gt(v, x + 12, y + 5, title, C_GOLD, 1);
    box(v, x + w - 20, y + 9, 8, 8, C_GREEN);        /* traffic lights */
    box(v, x + w - 34, y + 9, 8, 8, C_GOLD);
    box(v, x + w - 48, y + 9, 8, 8, C_RED);
    return y + 34;                                   /* first content baseline */
}

/* a dock icon = a native .zxvc program */
static void icon(vbe_state_t *v, int32_t x, int32_t y, char glyph, uint32_t accent,
                 const char *name, const char *file) {
    const int32_t S = 76;
    box(v, x, y, S, S, C_WINBODY);
    frame(v, x, y, S, S, accent);
    frame(v, x + 1, y + 1, S - 2, S - 2, C_BAR_HI);
    char g[2] = { glyph, 0 };
    gt(v, x + S / 2 - 12, y + S / 2 - 24, g, accent, 3);   /* 24x48 glyph */
    gt(v, x + 6, y + S + 4, name, C_TEXT, 1);
    gt(v, x - 4, y + S + 20, file, C_GOLD_DIM, 1);
}

/* the mouse pointer — white arrow with a black outline */
static void cursor(vbe_state_t *v, int32_t x, int32_t y) {
    static const char *A[] = {
        "1","11","121","1221","12221","122221","1222221","12222221",
        "122222221","1222222221","12222111111","122122","1221 12","121  12",
        "11    122","      12" };
    for (int32_t r = 0; r < 16; r++)
        for (int32_t c = 0; A[r][c]; c++) {
            if (A[r][c] == '1') vbe_set_pixel(v, x + c, y + r, C_BLACK);
            else if (A[r][c] == '2') vbe_set_pixel(v, x + c, y + r, C_WHITE);
        }
}

void zxv_shell_render(vbe_state_t *v, int32_t cursor_x, int32_t cursor_y, uint32_t tick) {
    const int32_t W = (int32_t)v->width, H = (int32_t)v->height;

    /* ===== menu bar ===== */
    box(v, 0, 0, W, 40, C_BAR);
    vbe_fill_rect(v, 0, 39, W, 1, C_GOLD_DIM);
    box(v, 12, 8, 24, 24, C_GOLD);                     /* vulpine lozenge */
    box(v, 17, 13, 14, 14, C_RED);
    gt(v, 48, 4, "ZEDEC pqOS", C_GOLD, 2);              /* wordmark 16x32 */
    gt(v, W - 470, 4, "ZEDEC XERO VULPINE  --  post-quantum OS", C_DIM, 1);
    gt(v, W - 470, 21, "economy 5/5   platform 8/8   EL0   phase-tick", C_GOLD_DIM, 1);

    /* ===== left dock: five native programs ===== */
    int32_t dx = 26, dy = 70, step = 118;
    icon(v, dx, dy,            'V', C_GOLD,  "Vino",     "wallet.zxvc"); dy += step;
    icon(v, dx, dy,            'F', C_RED,   "Fleet",    "crew.zxvc");   dy += step;
    icon(v, dx, dy,            'S', C_CYAN,  "Studio",   "art.zxvc");    dy += step;
    icon(v, dx, dy,            'C', C_GREEN, "Chiglet",  "mind.zxvc");   dy += step;
    icon(v, dx, dy,            'R', C_GOLD,  "Refinery", "sigil.zxvc");

    /* ===== terminal window (the main focus) ===== */
    {
        int32_t x = 176, y = 70, w = 660, h = 566;
        int32_t ty = window(v, x, y, w, h, "ZEDEC Terminal  --  vulpine@zxv:~", C_GOLD);
        int32_t tx = x + 14;
        gt(v, tx, ty, "ZEDEC XERO VULPINE  --  pqOS [ARM64]", C_CYAN, 1);          ty += 18;
        gt(v, tx, ty, "the kernel IS the OS: economy, governance, social in-kernel", C_DIM, 1); ty += 26;
        gt(v, tx, ty, "vulpine@zxv:~$ ls", C_TEXT, 1);                             ty += 18;
        gt(v, tx, ty, "wallet.zxvc  crew.zxvc  art.zxvc  mind.zxvc  sigil.zxvc", C_GOLD, 1); ty += 18;
        gt(v, tx, ty, "wallet.cedez  wallet.cedec        S- / S0 of the triad", C_GOLD_DIM, 1); ty += 26;
        gt(v, tx, ty, "vulpine@zxv:~$ open wallet.zxvc", C_TEXT, 1);               ty += 18;
        gt(v, tx, ty, "  [S+] positive-space program -- verified, releasable", C_GREEN, 1); ty += 18;
        gt(v, tx, ty, "  One Policy: symbiotic; no usury, no kill-switch", C_DIM, 1); ty += 26;
        gt(v, tx, ty, "vulpine@zxv:~$ chiglet 'is this term fair?'", C_TEXT, 1);   ty += 18;
        gt(v, tx, ty, "  R=3.0 distinct experts -> DECIDED: symbiotic", C_CYAN, 1); ty += 26;
        gt(v, tx, ty, "vulpine@zxv:~$", C_TEXT, 1);
        if ((tick / 8) & 1u) box(v, tx + 8 * 15, ty, 9, 15, C_GREEN);
    }

    /* ===== Chiglet companion panel (right) ===== */
    {
        int32_t x = 858, y = 70, w = 396, h = 250;
        int32_t ty = window(v, x, y, w, h, "Chiglet -- AI companion", C_GREEN);
        int32_t tx = x + 14;
        gt(v, tx, ty, "native mixture-of-experts", C_DIM, 1);       ty += 24;
        gt(v, tx, ty, "> match trolls with trolls", C_TEXT, 1);     ty += 18;
        gt(v, tx, ty, "> concord: rehabilitative", C_TEXT, 1);      ty += 18;
        gt(v, tx, ty, "> Pig Badge: over 9000", C_GOLD, 1);         ty += 26;
        gt(v, tx, ty, "R (distinct evidence): 3.0", C_CYAN, 1);     ty += 18;
        gt(v, tx, ty, "verdict: DECIDED", C_GREEN, 1);
    }

    /* ===== Vino wallet panel (right, lower) ===== */
    {
        int32_t x = 858, y = 336, w = 396, h = 300;
        int32_t ty = window(v, x, y, w, h, "Vino -- floating vouchers", C_GOLD);
        int32_t tx = x + 14;
        gt(v, tx, ty, "triple rail (ISO 4217)", C_DIM, 1);         ty += 24;
        gt(v, tx, ty, "846 debit    120.00", C_TEXT, 1);           ty += 18;
        gt(v, tx, ty, "888 credit    12.00", C_TEXT, 1);           ty += 18;
        gt(v, tx, ty, "999 equity   108.00", C_GREEN, 1);          ty += 26;
        gt(v, tx, ty, "coverage 1.8x  solvent", C_CYAN, 1);        ty += 18;
        gt(v, tx, ty, "no debt, merit-based", C_DIM, 1);           ty += 26;
        gt(v, tx, ty, "[Send]  [Swap]  [Redeem]", C_GOLD, 1);
    }

    /* ===== taskbar ===== */
    box(v, 0, H - 32, W, 32, C_BAR);
    vbe_fill_rect(v, 0, H - 32, W, 1, C_GOLD_DIM);
    box(v, 8, H - 26, 74, 20, C_RED_DK);
    frame(v, 8, H - 26, 74, 20, C_GOLD_DIM);
    gt(v, 16, H - 24, "Start", C_GOLD, 1);
    gt(v, 98, H - 24, "Terminal", C_TEXT, 1);
    gt(v, 190, H - 24, "Vino", C_DIM, 1);
    gt(v, 246, H - 24, "Fleet", C_DIM, 1);
    gt(v, 310, H - 24, "Chiglet", C_DIM, 1);
    {
        char buf[20]; uint32_t t = tick; char tmp[12]; int i = 0;
        if (t == 0) tmp[i++] = '0';
        while (t > 0 && i < 10) { tmp[i++] = (char)('0' + (t % 10)); t /= 10; }
        int j = 0; const char *p = "tick ";
        while (*p) buf[j++] = *p++;
        while (i > 0) buf[j++] = tmp[--i];
        buf[j] = 0;
        gt(v, W - 110, H - 24, buf, C_CYAN, 1);
    }

    /* ===== pointer ===== */
    cursor(v, cursor_x, cursor_y);
}
