/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_boot_wasm.c — the browser's door to zui_boot: the same boot screen,
 * the same sounds, the same vault as bare metal and the desktop app, built
 * to WebAssembly (tools/web/build_boot_wasm.sh). boot.js streams the sealed
 * assets, tells this how much has really loaded, and pulls frames and sound.
 *
 * Exports (all integer but zb_load's counts, which are JS numbers):
 *   zb_init(reduce_motion, sound)   start a boot
 *   zb_size(w, h)                   the canvas size; returns the RGBA buffer
 *   zb_logo_buf(n)                  room for one logo's ZXVG bytes
 *   zb_logo_set(which, n)           hand over the bytes just written; 0 / -1
 *   zb_load(done, total)            the real load; 0 / -1
 *   zb_ready(), zb_fail()
 *   zb_frame(ms)                    advance and draw; RGBA ready to show
 *   zb_audio(frames)                pull 48 kHz stereo int16; returns them
 *   zb_phase(), zb_progress()
 *
 * No allocation: every buffer is static. */
#include "zui_boot.h"
#include "zui_login.h"
#include "zui_tokens.h"
#include "truetype.h"

#include "zui_desk.h"
#include "zui_launch_theme.h"
#include "zui_vec.h"

#define EXPORT(name) __attribute__((export_name(#name)))

#define MAX_W    1920
#define MAX_H    1200
#define LOGO_MAX (2u * 1024u * 1024u) /* the five logos' ZXVG, together */
#define FONT_MAX (256u * 1024u)       /* the login screen's one font    */
#define AUD_MAX  8192u

static zui_boot_t BOOT;
static uint32_t PX[MAX_W * MAX_H], FACE[MAX_W * MAX_H], BEHIND[MAX_W * MAX_H];
static uint8_t RGBA[MAX_W * MAX_H * 4];
static zui_surface_t S, F, B;
static uint8_t LOGO[LOGO_MAX];
static uint32_t logo_used, logo_at;
static int16_t AUD[AUD_MAX * 2];

/* What the vault opens onto: the login screen, drawn into the behind
 * surface. The browser door seats a visitor as a guest with no lock; the
 * screen's shapes draw even before the font arrives. */
static uint8_t FONT[FONT_MAX];
static ttf_font_t TF;
static zui_glyph_cache_t GC;
static zui_text_t T_BIG, T_SMALL, T_INI;
static zui_login_fonts_t FONTS;
static bool have_font;
/* The vault's people: the guest, plus player cards the page seats (each a
 * vino account the door verifies with the pin below). The card's person
 * shows on the login screen with a pass field; the page answers the pin. */
#define ZXV_CARDS 4u
static zui_login_person_t PEOPLE[1 + ZXV_CARDS] = {
    {"Guest", 0, ZUI_LOCK_NONE, true},
};
static char CARD_NAME[ZXV_CARDS][24];
static uint32_t n_cards;
static bool CARD_KEY[ZXV_CARDS]; /* the person authenticates by key, not pin */
static char PIN_BUF[16]; /* the typed pin, hex digits */
static uint32_t pin_len;
static int card_pending; /* a locked person waiting on the page's answer */
static zui_login_t LOGIN = {PEOPLE, 1, 0, 0, 0, false, false, 0, 0, 0, 0, 0, 0, 0};

static void draw_login(zui_surface_t *b, int32_t w, int32_t h)
{
    /* the same scale as the vault: 1280x800 is reference size (sc 256) */
    int32_t sx = (int32_t)((int64_t) w * 256 / 1280),
            sy = (int32_t)((int64_t) h * 256 / 800),
            sc = sx < sy ? sx : sy;
    if (sc < 128) sc = 128;
    if (sc > 1024) sc = 1024;
    if (have_font) {
        T_BIG.size_px   = (uint32_t)((int64_t) sc * 34 / 256);
        T_SMALL.size_px = (uint32_t)((int64_t) sc * 15 / 256);
        T_INI.size_px   = (uint32_t)((int64_t) sc * 44 / 256);
        FONTS = (zui_login_fonts_t){&T_BIG, &T_SMALL, &T_INI, &T_SMALL};
    }
    zui_login_draw(b, (zui_rect_t){0, 0, w, h}, sc, &LOGIN, have_font ? &FONTS : 0, 0);
}

/* the freestanding C library pieces the compiler may call for */
void *memcpy(void *d, const void *s, unsigned long n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memset(void *d, int c, unsigned long n)
{
    uint8_t *a = d;
    while (n--) *a++ = (uint8_t) c;
    return d;
}
void *memmove(void *d, const void *s, unsigned long n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    if (a < b)
        while (n--) *a++ = *b++;
    else
        while (n--) a[n] = b[n];
    return d;
}
int memcmp(const void *x, const void *y, unsigned long n)
{
    const uint8_t *a = x, *b = y;
    for (; n; n--, a++, b++)
        if (*a != *b) return *a < *b ? -1 : 1;
    return 0;
}

/* ---- the session above the boot -----------------------------------------
 * The vault's doors open onto the login screen; its Enter seats the guest
 * on the real desk (zui_desk): the status bar, the rail of sixteen base
 * apps, and windows on their own launch screens (zui_launch_theme) with the
 * catalogue's themes and cues. A tile's app opens when its icon arrives
 * from the sealed tree — the only load there is — and its card shows the
 * icon and layer. The browser feeds events and icon bytes; every picture
 * and sound is the OS's own. */

#define RAIL_APPS 16u           /* ZUI_DESK_APPS: the rail holds sixteen tiles */
#define ICON_CAP  (16u * 1024u) /* one app's ZXVG icon (they run < 4 KB) */
#define CAT_MAX   407u          /* the whole catalogue, one tile per app */

static int MODE; /* 0 = the boot runs; 1 = the login answers; 2 = the desk */

/* Events carry the platform's monotonic ms (zui_input.h): the window
 * manager times double-presses and the dock matches a touch to its pointer
 * twin on it. A browser gives this file no clock of its own, so frames feed
 * one — the same elapsed ms the animation already runs on. */
static uint32_t NOW_MS;

/* The rail's sixteen: base layers 1..16, named as the matrix names them */
static zui_desk_app_t RAIL[RAIL_APPS];
static char RAIL_NAME[RAIL_APPS][24], RAIL_APP[RAIL_APPS][24], RAIL_MARK[RAIL_APPS][3];
static uint8_t ICONS[RAIL_APPS][ICON_CAP];
static uint32_t icon_len[RAIL_APPS];
static int8_t icon_st[RAIL_APPS];    /* 0 nothing, 1 asked for, 2 here, -1 failed */
static uint32_t icon_win[RAIL_APPS]; /* the launch waiting on it, 0 none   */
static uint32_t icon_off[RAIL_APPS], icon_n[RAIL_APPS]; /* a logo's bytes in LOGO */

/* The catalogue (boot.json's apps, fed by the page): the desk pages over
 * it sixteen tiles at a time; a rail slot maps to catalogue index
 * cat_page*16 + slot, so every tile's icon streams from the same pack. */
static char CAT_NAME[CAT_MAX][24];
static char CAT_MARK[CAT_MAX][3];
static uint32_t cat_n, cat_page, cat_pages;

/* The shared spaces (the node's vna_space directory, fed by the page):
 * the Spaces face of the desk lists them with their kind and members. */
#define SPACES_MAX 16u
static char SP_NAME[SPACES_MAX][24];
static uint8_t SP_KIND[SPACES_MAX], SP_PHASE[SPACES_MAX];
static uint16_t SP_MEM[SPACES_MAX];
static uint32_t sp_n;

static zui_desk_t DESK;
static zui_launch_theme_t THEME;
static zxl_player_t PLAY; /* the launch cues, after the boot's own player */
static int16_t AUD2[AUD_MAX * 2];
static zui_vec_work_t VECW; /* scratch for every vector draw, about 0.5 MB */
static zui_glyph_cache_t GC2;
static zui_text_t T_TITLE, T_BODY, T_CAP, T_MARK;
static bool desk_up;

/* ---- the L2 state bus ---------------------------------------------------
 * Deltas arrive over the doorway's transport (boot.js writes them into
 * L2_IN) as canonical vna_delta bytes — the same bytes a Vinea
 * VNA_SPACE_EV_DELTA space event carries (vna_space.h). They join the state
 * page through lpres_kjoin: two peers disagreeing about one cell leave it
 * BOTH rather than one silently winning, and the desk's status line says
 * what the page holds. A delta this node writes leaves through zb_l2_emit's
 * bytes, which the transport carries back to the bus. */
#include "vna_delta.h"
#include "robin_debanks/sha256.h"

static vna_delta_page_t L2;
static uint8_t L2_IN[VNA_DELTA_HDR + VNA_DELTA_PAYLOAD_MAX];
static uint8_t L2_OUT[VNA_DELTA_HDR + VNA_DELTA_PAYLOAD_MAX];

/* the bus's own cells: cell 1 says the bus link is up, cell 2 is the member
 * count the last presence delta carried; 3 on are the session's */
#define ZB_L2_LINK     1u
#define ZB_L2_PRESENCE 2u

static int rail_of(const char *app)
{
    for (uint32_t i = 0; i < RAIL_APPS; i++) {
        const char *a = RAIL_APP[i], *b = app;
        while (*a && *a == *b) a++, b++;
        if (!*a && !*b) return (int) i;
    }
    return -1;
}

/* zui_lt_shell_t: the launch screen's pieces, from the sealed bytes. */
static void sh_icon(void *ctx, const char *app, zxl_icon_ink_t ink, zui_surface_t *s,
                    zui_rect_t box, uint32_t lit)
{
    (void) ctx;
    int i = rail_of(app);
    if (i < 0 || icon_st[i] != 2) return;
    zui_vec_draw_simple(s, &VECW, ICONS[i], icon_len[i], box, lit,
                        ink == ZXL_ICON_PALETTE ? 0xFFF2C230u : 0);
}

static void sh_logo(void *ctx, uint32_t which, zui_surface_t *s, zui_rect_t box, uint32_t lit)
{
    (void) ctx;
    if (which >= ZUI_LT_LOGOS || !icon_n[which]) return;
    zui_vec_draw_simple(s, &VECW, LOGO + icon_off[which], icon_n[which], box, lit, 0);
}

static void sh_play(void *ctx, uint32_t window, const zxl_theme_t *theme, const zxl_event_t *ev)
{
    (void) ctx;
    (void) window;
    zxl_player_play(&PLAY, theme, ev);
}

/* What an open window shows once its doors are up: the app's own card —
 * its icon at full strength and its catalogue name and layer, the facts
 * the sealed matrix holds. */
static void card_paint(void *ctx, zui_surface_t *s, zui_rect_t c, uint32_t tick)
{
    (void) tick;
    int i = (int) (uintptr_t) ctx;
    zui_fill(s, c, 0x001A09u);
    if (i < 0) return;
    int32_t sc = DESK.scale_q8;
    int32_t isz = c.w < c.h ? c.w * 2 / 5 : c.h * 2 / 5;
    if (icon_st[i] == 2) {
        zui_rect_t b = {c.x + (c.w - isz) / 2, c.y + c.h / 6, isz, isz};
        zui_vec_draw_simple(s, &VECW, ICONS[i], icon_len[i], b, 256, 0xFFF2C230u);
    }
    if (have_font) {
        int32_t base = c.y + c.h / 6 + isz + (int32_t) sc * 40 / 256;
        const char *nm = RAIL_NAME[i];
        zui_text_a(s, &T_TITLE, c.x + (c.w - zui_text_width(&T_TITLE, nm)) / 2, base, 0xF2C230u,
                   255, nm);
        char line[40];
        const char *hex = "Layer %03d \xC2\xB7 base app";
        char tmp[8];
        int p = 0, n = i + 1;
        tmp[0] = (char) ('0' + n / 100);
        tmp[1] = (char) ('0' + n / 10 % 10);
        tmp[2] = (char) ('0' + n % 10);
        tmp[3] = 0;
        for (const char *f = hex; *f && p < 39; f++) {
            if (f[0] == '%' && f[1] == '0' && f[2] == '3' && f[3] == 'd') {
                for (int q = 0; tmp[q]; q++) line[p++] = tmp[q];
                f += 3;
            } else
                line[p++] = *f;
        }
        line[p] = 0;
        base += (int32_t) sc * 22 / 256;
        zui_text_a(s, &T_BODY, c.x + (c.w - zui_text_width(&T_BODY, line)) / 2, base, 0x9FB28Au,
                   255, line);
    }
}

static bool card_event(void *ctx, const zui_event_t *e, zui_rect_t content)
{
    (void) ctx;
    (void) e;
    (void) content;
    return true; /* the card takes its clicks: nothing underneath is hit */
}

static void card_closed(void *ctx)
{
    (void) ctx;
}

static void desk_open_app(uint32_t app)
{
    if (app >= RAIL_APPS) return;
    if (DESK.win_of[app]) {
        zui_desk_open(&DESK, app, RAIL_NAME[app], (const zui_wm_client_t *) 0);
        return;
    }
    zui_wm_client_t c = {card_paint, card_event, card_closed, (void *) (uintptr_t) app};
    bool warm = icon_st[app] == 2; /* its bytes are already here: END only */
    uint32_t id = zui_launch_theme_open(&THEME, RAIL_APP[app], RAIL_NAME[app], 0, true, warm, &c);
    if (id && !warm && icon_st[app] != 2) {
        icon_st[app] = 1; /* the page streams it in, then zb_icon_set */
        icon_win[app] = id;
    }
    if (id) DESK.win_of[app] = id;
}

static void desk_init(void)
{
    static const char *NAMES[RAIL_APPS] = {
        "The Banker", "Hollow",    "Threshold", "Keystone", "Scent",    "Tideturn",
        "Satchel",    "Gatehouse", "Lockbox",   "Keepsake", "Lifeboat", "Ferry",
        "Flint",      "Wake",      "Tidewatch", "Sounding",
    };
    static const char *APPS[RAIL_APPS] = {
        "Banker",  "Hollow",    "Threshold", "Keystone", "Scent",    "Tideturn",
        "Satchel", "Gatehouse", "Lockbox",   "Keepsake", "Lifeboat", "Ferry",
        "Flint",   "Wake",      "Tidewatch", "Sounding",
    };
    for (uint32_t i = 0; i < RAIL_APPS; i++) {
        unsigned p = 0;
        for (const char *s = NAMES[i]; *s && p < 23; s++) RAIL_NAME[i][p++] = (uint8_t) *s;
        RAIL_NAME[i][p] = 0;
        p = 0;
        for (const char *s = APPS[i]; *s && p < 23; s++) RAIL_APP[i][p++] = (uint8_t) *s;
        RAIL_APP[i][p] = 0;
        zui_initials(APPS[i], RAIL_MARK[i]);
        RAIL[i].name = RAIL_NAME[i];
        RAIL[i].mark = RAIL_MARK[i];
    }
    int32_t sx = (int32_t) ((int64_t) S.w * 256 / 1280), sy = (int32_t) ((int64_t) S.h * 256 / 800),
            sc = sx < sy ? sx : sy;
    if (sc < 128) sc = 128;
    if (sc > 1024) sc = 1024;
    if (have_font) {
        zui_glyph_cache_init(&GC2);
        T_TITLE = (zui_text_t){&TF, &GC2, (uint32_t) ((int64_t) sc * 15 / 256), false};
        T_BODY = (zui_text_t){&TF, &GC2, (uint32_t) ((int64_t) sc * 12 / 256), false};
        T_CAP = (zui_text_t){&TF, &GC2, (uint32_t) ((int64_t) sc * 10 / 256), false};
        T_MARK = (zui_text_t){&TF, &GC2, (uint32_t) ((int64_t) sc * 18 / 256), false};
    }
    zui_desk_faces_t f = {have_font ? &T_TITLE : 0, have_font ? &T_BODY : 0, have_font ? &T_CAP : 0,
                          have_font ? &T_MARK : 0};
    zui_desk_init(&DESK, S.w, S.h, (uint32_t) sc, false, &f);
    for (uint32_t i = 0; i < RAIL_APPS; i++) zui_desk_add(&DESK, RAIL[i].name, RAIL[i].mark);
    zui_desk_status(&DESK, "ZXV · the Web4 doorway · Guest");
    zxl_player_init(&PLAY);
    zui_lt_shell_t sh = {sh_icon, sh_logo, sh_play, 0};
    zui_launch_theme_attach(&THEME, &DESK.wm, &sh, 0);
    desk_up = true;
}

/* The catalogue page on the rail: sixteen tiles, icons re-asked for, the
 * launch windows of the old page closed. A slot is catalogue index
 * cat_page*16 + slot; zb_icon_want reports the global index so the page
 * serves the right manifest entry, and the icon APIs keep taking the slot. */
static void cat_page_load(void)
{
    uint32_t i, g;
    if (!desk_up) return;
    for (i = 0; i < RAIL_APPS; i++) {
        unsigned p;
        g = cat_page * RAIL_APPS + i;
        if (g < cat_n) {
            for (p = 0; p < 23 && CAT_NAME[g][p]; p++) RAIL_NAME[i][p] = CAT_NAME[g][p];
            RAIL_NAME[i][p] = 0;
            for (p = 0; p < 23 && CAT_NAME[g][p]; p++) RAIL_APP[i][p] = CAT_NAME[g][p];
            RAIL_APP[i][p] = 0;
            RAIL_MARK[i][0] = CAT_MARK[g][0];
            RAIL_MARK[i][1] = CAT_MARK[g][1];
            RAIL_MARK[i][2] = 0;
        } else {
            RAIL_NAME[i][0] = RAIL_APP[i][0] = RAIL_MARK[i][0] = 0;
        }
        RAIL[i].name = RAIL_NAME[i];
        RAIL[i].mark = RAIL_MARK[i];
        icon_st[i] = 0;
        icon_len[i] = 0;
        icon_win[i] = 0;
        if (DESK.win_of[i]) {
            zui_wm_close(&DESK.wm, DESK.win_of[i]);
            DESK.win_of[i] = 0;
        }
    }
    DESK.napps = 0;
    for (i = 0; i < RAIL_APPS; i++) zui_desk_add(&DESK, RAIL[i].name, RAIL[i].mark);
    {
        char st[40];
        unsigned p = 0;
        static const char pfx[] = "ZXV · the Web4 doorway · page ";
        for (i = 0; pfx[i] && p < sizeof st - 8; i++) st[p++] = pfx[i];
        st[p++] = (char) ('1' + (cat_page % 10));
        if (cat_pages > 1) {
            st[p++] = '/';
            st[p++] = (char) ('0' + (cat_pages % 10));
        }
        st[p] = 0;
        zui_desk_status(&DESK, st);
    }
}

static void desk_act(void)
{
    if (DESK.act == ZUI_DESK_LAUNCH && DESK.act_app >= 0)
        desk_open_app((uint32_t) DESK.act_app);
    else if (DESK.act == ZUI_DESK_SPACES)
        DESK.spaces = !DESK.spaces;
    else if (DESK.act == ZUI_DESK_HOME)
        DESK.spaces = false;
    DESK.act = ZUI_DESK_NONE;
    DESK.act_app = -1;
}

/* The desk's status line says what the L2 page holds: the lattice letter of
 * the bus cell (N neither, T true, F false, B both) and how many cells the
 * page has seen. */
static void l2_status(void)
{
    if (!desk_up) return;
    static const char gly[4] = {'N', 'T', 'F', 'B'};
    char st[ZUI_DESK_STATUS];
    const char *pre = "ZXV · the Web4 doorway · Guest · mesh ";
    uint32_t n = 0;
    while (pre[n] && n < ZUI_DESK_STATUS - 8) st[n] = pre[n], n++;
    uint8_t lat = (uint8_t) LPRES_STATE_NEITHER;
    const vna_delta_cell_t *c = vna_delta_cell(&L2, ZB_L2_LINK);
    if (c) lat = c->lattice;
    st[n++] = gly[lat & 3];
    st[n++] = (char) 0xc2; /* ·, the same UTF-8 the prefix carries */
    st[n++] = (char) 0xb7;
    st[n++] = ' ';
    uint32_t nc = L2.ncell, div = 100;
    while (div > 1 && nc < div) div /= 10;
    while (div && n < ZUI_DESK_STATUS - 1) {
        st[n++] = (char) ('0' + nc / div % 10);
        div /= 10;
    }
    st[n] = 0;
    zui_desk_status(&DESK, st);
}

/* type: 0 move, 1 down, 2 up. Button: ZUI_BTN_LEFT mask on moves, the button
 * pressed or let go otherwise. Returns nonzero when the event did anything. */
EXPORT(zb_pointer) int zb_pointer(int type, int32_t x, int32_t y, int button)
{
    if (!S.px || MODE == 0) return 0;
    if (MODE == 1) {
        if (type != 2) return 0; /* only the click landing matters */
        uint32_t index = 0;
        int32_t sx = (int32_t) ((int64_t) S.w * 256 / 1280),
                sy = (int32_t) ((int64_t) S.h * 256 / 800), sc = sx < sy ? sx : sy;
        if (sc < 128) sc = 128;
        if (sc > 1024) sc = 1024;
        zui_login_hit_t h = zui_login_hit((zui_rect_t){0, 0, S.w, S.h}, sc, &LOGIN, x, y, &index);
        if (h == ZUI_LOGIN_PERSON && index < LOGIN.n) {
            /* pressing the person already chosen enters them, the same as
             * Enter — choosing one more time is how the door reads "come
             * in". A key person's enter asks the page instead. */
            if (index == LOGIN.chosen && LOGIN.people[LOGIN.chosen].lock == ZUI_LOCK_NONE &&
                !(index > 0 && CARD_KEY[index - 1])) {
                MODE = 2;
                if (!desk_up) desk_init();
                return 1;
            }
            if (index == LOGIN.chosen && index > 0 && CARD_KEY[index - 1] && !card_pending) {
                card_pending = (int) LOGIN.chosen + 1;
                return 1;
            }
            LOGIN.chosen = index;
            draw_login(&B, S.w, S.h); /* the chosen person moved */
            return 1;
        }
        if (h == ZUI_LOGIN_ENTER) {
            bool keyed = LOGIN.chosen > 0 && CARD_KEY[LOGIN.chosen - 1];
            if (LOGIN.people[LOGIN.chosen].lock == ZUI_LOCK_NONE && !keyed) {
                MODE = 2;
                if (!desk_up) desk_init();
                return 1;
            }
            if ((keyed || pin_len >= 8) && !card_pending) {
                card_pending = (int) LOGIN.chosen + 1;
                return 1;
            }
            return 0;
        }
        return 0;
    }
    zui_event_t e = {0};
    e.type = type == 1 ? ZUI_EV_PTR_DOWN : type == 2 ? ZUI_EV_PTR_UP : ZUI_EV_PTR_MOVE;
    e.x = x;
    e.y = y;
    e.button = (uint8_t) button;
    e.ms = NOW_MS;
    int used = zui_desk_event(&DESK, &e) ? 1 : 0;
    desk_act();
    return used;
}

EXPORT(zb_scroll) int zb_scroll(int32_t x, int32_t y, int32_t dx, int32_t dy)
{
    if (MODE != 2) return 0;
    zui_event_t e = {0};
    e.type = ZUI_EV_SCROLL;
    e.x = x;
    e.y = y;
    e.dx = dx;
    e.dy = dy;
    e.ms = NOW_MS;
    int used = zui_desk_event(&DESK, &e) ? 1 : 0;
    desk_act();
    return used;
}

/* key: ZUI_KEY_* or ASCII; down: 1 press, 0 release; mods: ZUI_MOD_* mask.
 * Callers that predate the third argument pass nothing and get mods = 0. */
EXPORT(zb_key) int zb_key(int key, int down, int mods)
{
    if (MODE == 1) {
        if (!down) return 0;
        uint8_t lock = LOGIN.people[LOGIN.chosen].lock;
        if (lock != ZUI_LOCK_NONE || CARD_KEY[LOGIN.chosen > 0 ? LOGIN.chosen - 1 : 0]) {
            /* the pass field: hex digits, backspace, Enter asks the page.
             * A key person needs no pin: Enter raises the seat directly. */
            if (key == ZUI_KEY_ENTER) {
                bool keyed = LOGIN.chosen > 0 && CARD_KEY[LOGIN.chosen - 1];
                if ((keyed || pin_len >= 8) && !card_pending) {
                    card_pending = (int) LOGIN.chosen + 1;
                    return 1;
                }
                return 0;
            }
            if (key == ZUI_KEY_BACKSPACE) {
                if (pin_len) {
                    pin_len--;
                    LOGIN.typed = pin_len;
                    draw_login(&B, S.w, S.h);
                }
                return 1;
            }
            if (pin_len < sizeof PIN_BUF &&
                ((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f') ||
                 (key >= 'A' && key <= 'F'))) {
                PIN_BUF[pin_len++] = (char) key;
                LOGIN.typed = pin_len;
                draw_login(&B, S.w, S.h);
                return 1;
            }
            return 0;
        }
        if (key == ZUI_KEY_ENTER) {
            MODE = 2;
            if (!desk_up) desk_init();
            return 1;
        }
        if (LOGIN.n > 1 &&
            (key == ZUI_KEY_LEFT || key == ZUI_KEY_RIGHT || key == ZUI_KEY_TAB)) {
            if (key == ZUI_KEY_LEFT || (key == ZUI_KEY_TAB && (mods & ZUI_MOD_SHIFT)))
                LOGIN.chosen = LOGIN.chosen ? LOGIN.chosen - 1 : LOGIN.n - 1;
            else
                LOGIN.chosen = LOGIN.chosen + 1 < LOGIN.n ? LOGIN.chosen + 1 : 0;
            draw_login(&B, S.w, S.h);
            return 1;
        }
        return 0;
    }
    if (MODE != 2) return 0;
    /* page the catalogue with PageUp/PageDown; a page turn closes the
     * old page's windows and asks for the new page's icons */
    if (down && (key == 0x108 || key == 0x109)) {
        if (cat_pages > 1) {
            uint32_t np = key == 0x109
                              ? (cat_page + 1 < cat_pages ? cat_page + 1 : 0)
                              : (cat_page ? cat_page - 1 : cat_pages - 1);
            if (np != cat_page) {
                cat_page = np;
                cat_page_load();
                return 1;
            }
        }
        return 0;
    }
    zui_event_t e = {0};
    e.type = down ? ZUI_EV_KEY_DOWN : ZUI_EV_KEY_UP;
    e.key = (uint16_t) key;
    e.mods = (uint8_t) mods;
    e.ms = NOW_MS;
    int used = zui_desk_event(&DESK, &e) ? 1 : 0;
    desk_act();
    return used;
}

EXPORT(zb_desk) int zb_desk(void)
{
    return MODE;
}

/* Seat a player card on the login screen: adds a person with a pass field.
 * Returns the person's index + 1, or 0 when the rail is full. */
EXPORT(zb_card) uint32_t zb_card(const char *name)
{
    uint32_t i, n = 0;
    if (!name || MODE > 1 || LOGIN.n > ZXV_CARDS) return 0;
    while (name[n] && n < sizeof CARD_NAME[0] - 1) n++;
    for (i = 0; i < n_cards; i++)
        if (CARD_NAME[i][0] == 0) break;
    if (i == n_cards) {
        if (n_cards >= ZXV_CARDS) return 0;
        i = n_cards++;
    }
    for (uint32_t j = 0; j < n; j++) CARD_NAME[i][j] = name[j];
    CARD_NAME[i][n] = 0;
    CARD_KEY[i] = false;
    PEOPLE[1 + i].name = CARD_NAME[i];
    PEOPLE[1 + i].color = 0;
    PEOPLE[1 + i].lock = ZUI_LOCK_PASS;
    PEOPLE[1 + i].guest = false;
    if (LOGIN.n <= 1 + i) LOGIN.n = 1 + i + 1;
    draw_login(&B, S.w, S.h);
    return i + 2;
}

/* Mark the last card person as key-authenticated: Enter raises the seat
 * without a pin, and the page answers with the sovereign-key frame. */
EXPORT(zb_card_key) int zb_card_key(uint32_t i)
{
    if (i < 1 || i > ZXV_CARDS || !CARD_NAME[i - 1][0]) return -1;
    CARD_KEY[i - 1] = true;
    PEOPLE[i].lock = ZUI_LOCK_NONE;
    draw_login(&B, S.w, S.h);
    return 0;
}

/* A locked person pressed Enter with a full pin: the index + 1 the page
 * must answer, 0 when nothing is waiting. */
EXPORT(zb_card_pending) uint32_t zb_card_pending(void)
{
    return card_pending > 0 ? (uint32_t) card_pending : 0;
}

EXPORT(zb_pin_buf) char *zb_pin_buf(void)
{
    return PIN_BUF;
}

EXPORT(zb_pin_len) uint32_t zb_pin_len(void)
{
    return pin_len;
}

/* The page's answer to a pending card: 0 seats the player, anything else
 * shakes the login and clears the pin. */
EXPORT(zb_card_result) int zb_card_result(int ok)
{
    if (card_pending <= 0) return -1;
    card_pending = 0;
    pin_len = 0;
    LOGIN.typed = 0;
    if (ok == 0) {
        MODE = 2;
        if (!desk_up) desk_init();
        return 0;
    }
    LOGIN.wrong = true;
    draw_login(&B, S.w, S.h);
    return 0;
}

/* Which rail app's icon the page should stream in next: the catalogue
 * index + 1 (the page's slot + page*16), or 0 when nothing is waiting. */
EXPORT(zb_icon_want) uint32_t zb_icon_want(void)
{
    for (uint32_t i = 0; i < RAIL_APPS; i++)
        if (icon_st[i] == 1) return cat_page * RAIL_APPS + i + 1;
    return 0;
}

EXPORT(zb_icon_buf) uint8_t *zb_icon_buf(uint32_t i, uint32_t n)
{
    return i < RAIL_APPS && n <= ICON_CAP ? ICONS[i] : 0;
}

/* How much of the wanted icon has streamed in; the launch screen's parts
 * light with it. */
EXPORT(zb_icon_prog) int zb_icon_prog(uint32_t i, uint32_t done, uint32_t total)
{
    if (i >= RAIL_APPS || !icon_win[i] || !total) return -1;
    return zui_launch_theme_load(&THEME, icon_win[i], ZUI_LT_FETCH, done, total);
}

EXPORT(zb_icon_set) int zb_icon_set(uint32_t i, uint32_t n)
{
    if (i >= RAIL_APPS || n > ICON_CAP || n < 8) return -1;
    if (ICONS[i][0] != 'Z' || ICONS[i][1] != 'X' || ICONS[i][2] != 'V' || ICONS[i][3] != 'G')
        return -1;
    icon_len[i] = n;
    icon_st[i] = 2;
    uint32_t id = icon_win[i];
    icon_win[i] = 0;
    if (id) {
        zui_launch_theme_load(&THEME, id, ZUI_LT_FETCH, n, n);
        zui_launch_theme_load(&THEME, id, ZUI_LT_DONE, n, n);
    }
    return 0;
}

EXPORT(zb_icon_fail) int zb_icon_fail(uint32_t i)
{
    if (i >= RAIL_APPS) return -1;
    icon_st[i] = -1;
    uint32_t id = icon_win[i];
    icon_win[i] = 0;
    if (id) zui_launch_theme_load(&THEME, id, ZUI_LT_FAILED, 0, 1);
    return 0;
}

/* The catalogue (boot.json's apps): the page feeds it before the desk
 * opens, so every tile is a real app, not a placeholder. */
EXPORT(zb_apps) uint32_t zb_apps(uint32_t n)
{
    if (n > CAT_MAX) n = CAT_MAX;
    cat_n = n;
    cat_pages = cat_n ? (cat_n + RAIL_APPS - 1) / RAIL_APPS : 1;
    if (cat_page >= cat_pages) cat_page = 0;
    if (desk_up) cat_page_load();
    return cat_n;
}

EXPORT(zb_apps_name) int zb_apps_name(uint32_t i, const char *name)
{
    unsigned p = 0;
    if (i >= CAT_MAX || !name) return -1;
    for (p = 0; name[p] && p < 23; p++) CAT_NAME[i][p] = name[p];
    CAT_NAME[i][p] = 0;
    zui_initials(name, CAT_MARK[i]);
    return 0;
}

EXPORT(zb_apps_page) uint32_t zb_apps_page(void)
{
    return cat_page;
}

EXPORT(zb_apps_page_set) int zb_apps_page_set(uint32_t p)
{
    if (p >= cat_pages) return -1;
    cat_page = p;
    if (desk_up) cat_page_load();
    return 0;
}

/* The shared spaces (from the door's /spaces): the Spaces face lists them.
 * kind: 1 room, 2 hall; phase: 3 = a member here; members: the count. */
EXPORT(zb_spaces) uint32_t zb_spaces(uint32_t n)
{
    if (n > SPACES_MAX) n = SPACES_MAX;
    sp_n = n;
    return sp_n;
}

EXPORT(zb_spaces_name)
int zb_spaces_name(uint32_t i, const char *name, int kind, int phase, int members)
{
    unsigned p = 0;
    if (i >= SPACES_MAX || !name) return -1;
    for (p = 0; name[p] && p < 23; p++) SP_NAME[i][p] = name[p];
    SP_NAME[i][p] = 0;
    SP_KIND[i] = (uint8_t) kind;
    SP_PHASE[i] = (uint8_t) phase;
    SP_MEM[i] = members > 0 ? (uint16_t) members : 0;
    return 0;
}

/* ---- the L2 exports: the transport's bytes, the page's answers -------- */

/* Room for one inbound delta: write its canonical bytes here, then call
 * zb_l2_apply with the length and the event's Lamport field (two halves,
 * staying integer). Returns the cell's lattice state after the join
 * (0..3) or a negative vna_status_t. */
EXPORT(zb_l2_buf) uint8_t *zb_l2_buf(void) { return L2_IN; }

EXPORT(zb_l2_apply) int zb_l2_apply(uint32_t n, uint32_t tick_lo, uint32_t tick_hi)
{
    uint64_t t = (uint64_t) tick_lo | ((uint64_t) tick_hi << 32);
    int st = vna_delta_apply(&L2, L2_IN, n, t);
    if (st >= 0) l2_status();
    return st;
}

/* The lattice state a cell holds, or -1 when it has seen nothing. */
EXPORT(zb_l2_state) int zb_l2_state(uint32_t cell)
{
    const vna_delta_cell_t *c = vna_delta_cell(&L2, (uint16_t) cell);
    return c ? c->lattice : -1;
}

/* Write this node's own delta's payload into L2_IN first (n bytes), then
 * call: the canonical delta lands in L2_OUT, its length is the return (or a
 * negative vna_status_t). The transport sends those bytes to the bus. */
EXPORT(zb_l2_emit) int zb_l2_emit(uint32_t cell, int lattice, uint32_t n)
{
    uint64_t tick = 0;
    int r = vna_delta_issue(&L2, (uint16_t) cell, (lpres_state_t) lattice, L2_IN, (uint16_t) n,
                            L2_OUT, sizeof L2_OUT, &tick);
    if (r >= 0) l2_status();
    return r;
}

EXPORT(zb_l2_out) uint8_t *zb_l2_out(void) { return L2_OUT; }

/* ---- the streaming hash: the kernel's SHA-256 for origins WebCrypto ---
 * does not reach (plain-http seeds are not a secure context). The loader
 * feeds the pack through SHA_IN in spans and reads the digest at SHA_OUT. */
static uint8_t SHA_IN[65536], SHA_OUT[32];
static sha256_ctx_t SHA;
EXPORT(zb_sha_buf) uint8_t *zb_sha_buf(void) { return SHA_IN; }
EXPORT(zb_sha_begin) void zb_sha_begin(void) { sha256_init(&SHA); }
EXPORT(zb_sha_feed) void zb_sha_feed(uint32_t n)
{
    if (n > sizeof SHA_IN) n = sizeof SHA_IN;
    sha256_update(&SHA, SHA_IN, n);
}
EXPORT(zb_sha_out) uint8_t *zb_sha_out(void)
{
    sha256_final(&SHA, SHA_OUT);
    return SHA_OUT;
}

EXPORT(zb_init) void zb_init(int reduce_motion, int sound)
{
    zxl_prefs_t p = zxl_prefs_default();
    if (!sound) p.sounds = 0;
    zui_boot_init(&BOOT, &p, reduce_motion != 0);
    logo_used = 0;
    vna_delta_page_init(&L2);
}

EXPORT(zb_size) uint8_t *zb_size(int32_t w, int32_t h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > MAX_W) w = MAX_W;
    if (h > MAX_H) h = MAX_H;
    zui_surface_init(&S, PX, w, h, w);
    zui_surface_init(&F, FACE, w, h, w);
    zui_surface_init(&B, BEHIND, w, h, w);
    draw_login(&B, w, h); /* behind the doors: the login screen */
    if (desk_up) {
        int32_t sx = (int32_t) ((int64_t) w * 256 / 1280),
                sy = (int32_t) ((int64_t) h * 256 / 800),
                sc = sx < sy ? sx : sy;
        if (sc < 128) sc = 128;
        if (sc > 1024) sc = 1024;
        zui_desk_resize(&DESK, w, h, (uint32_t) sc);
    }
    return RGBA;
}

EXPORT(zb_font_buf) uint8_t *zb_font_buf(uint32_t n)
{
    return n <= FONT_MAX ? FONT : 0;
}

EXPORT(zb_font_set) int zb_font_set(uint32_t n)
{
    if (n > FONT_MAX || !ttf_parse(&TF, FONT, n) || !TF.valid) return -1;
    zui_glyph_cache_init(&GC);
    T_BIG = (zui_text_t){&TF, &GC, 34, false};
    T_SMALL = (zui_text_t){&TF, &GC, 15, false};
    T_INI = (zui_text_t){&TF, &GC, 44, false};
    have_font = true;
    if (B.px) draw_login(&B, B.w, B.h);
    return 0;
}

EXPORT(zb_width) int32_t zb_width(void)
{
    return S.w;
}
EXPORT(zb_height) int32_t zb_height(void)
{
    return S.h;
}

EXPORT(zb_logo_buf) uint8_t *zb_logo_buf(uint32_t n)
{
    if (n > LOGO_MAX - logo_used) return 0;
    logo_at = logo_used;
    return LOGO + logo_at;
}

EXPORT(zb_logo_set) int zb_logo_set(uint32_t which, uint32_t n)
{
    if (n > LOGO_MAX - logo_at) return -1;
    if (zui_boot_logo(&BOOT, which, LOGO + logo_at, n)) return -1;
    logo_used = logo_at + n;
    return 0;
}

EXPORT(zb_load) int zb_load(double done, double total)
{
    if (!(done >= 0) || !(total >= 0) || total > 9.0e15) return -1;
    return zui_boot_load(&BOOT, (uint64_t) done, (uint64_t) total);
}

EXPORT(zb_ready) void zb_ready(void)
{
    zui_boot_ready(&BOOT);
}
EXPORT(zb_fail) void zb_fail(void)
{
    zui_boot_fail(&BOOT);
}

EXPORT(zb_frame) void zb_frame(uint32_t ms)
{
    if (!S.px) return;
    NOW_MS += ms;
    const uint32_t *src;
    if (MODE == 2) {
        zui_desk_tick(&DESK, ms);
        zui_desk_draw(&DESK, &S, BOOT.tick);
        if (DESK.spaces && sp_n) {
            /* the Spaces face: the shared spaces this node is in or found,
             * drawn over the desk with the same text faces */
            int32_t sx = (int32_t) ((int64_t) S.w * 256 / 1280),
                    sy = (int32_t) ((int64_t) S.h * 256 / 800), sc = sx < sy ? sx : sy;
            if (sc < 128) sc = 128;
            if (sc > 1024) sc = 1024;
            int32_t y = (int32_t) ((int64_t) sc * 24), step = (int32_t) ((int64_t) sc * 22);
            static const char hdr[] = "shared spaces";
            zui_text(&S, &T_CAP, (int32_t) ((int64_t) sc * 18), y, ZUI_GOLD, hdr);
            y += step;
            for (uint32_t i = 0; i < sp_n; i++) {
                char line[40];
                unsigned p = 0, j;
                for (j = 0; SP_NAME[i][j] && p < 24; j++) line[p++] = SP_NAME[i][j];
                line[p++] = ' ';
                line[p++] = ' ';
                line[p++] = SP_KIND[i] == 2 ? 'H' : 'R';
                line[p++] = ' ';
                line[p++] = ' ';
                line[p++] = 'm';
                line[p++] = ':';
                line[p++] = (char) ('0' + (SP_MEM[i] % 10));
                if (SP_PHASE[i] == 3) {
                    line[p++] = ' ';
                    line[p++] = ' ';
                    line[p++] = 'h';
                    line[p++] = 'e';
                    line[p++] = 'r';
                    line[p++] = 'e';
                }
                line[p] = 0;
                zui_text(&S, &T_CAP, (int32_t) ((int64_t) sc * 18), y, ZUI_TEXT, line);
                y += step;
            }
        }
        src = PX;
    } else {
        zui_boot_tick(&BOOT, ms);
        zui_boot_draw(&BOOT, &S, &F, &B);
        if (MODE == 0 && zui_boot_phase(&BOOT) == ZUI_BOOT_DONE) MODE = 1;
        src = PX;
    }
    uint32_t n = (uint32_t) (S.w * S.h);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = src[i];
        RGBA[4 * i] = (uint8_t) (c >> 16);
        RGBA[4 * i + 1] = (uint8_t) (c >> 8);
        RGBA[4 * i + 2] = (uint8_t) c;
        RGBA[4 * i + 3] = 255;
    }
}

EXPORT(zb_audio) int16_t *zb_audio(uint32_t frames)
{
    if (frames > AUD_MAX) frames = AUD_MAX;
    zui_boot_audio(&BOOT, AUD, frames);
    /* the desk's launch cues mix over whatever the boot left playing */
    if (MODE == 2) {
        zxl_player_render(&PLAY, AUD2, frames);
        for (uint32_t i = 0; i < frames * 2; i++) {
            int32_t v = (int32_t) AUD[i] + AUD2[i];
            AUD[i] = (int16_t) (v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
    return AUD;
}

EXPORT(zb_phase) int zb_phase(void)
{
    return (int) zui_boot_phase(&BOOT);
}
EXPORT(zb_progress) uint32_t zb_progress(void)
{
    return zui_boot_progress(&BOOT);
}

