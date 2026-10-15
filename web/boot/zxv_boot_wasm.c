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
static const zui_login_person_t PEOPLE[] = {
    {"Guest", 0, ZUI_LOCK_NONE, true},
};
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

static int MODE; /* 0 = the boot runs; 1 = the login answers; 2 = the desk */

/* the rail's sixteen: base layers 1..16, named as the matrix names them */
static zui_desk_app_t RAIL[RAIL_APPS];
static char RAIL_NAME[RAIL_APPS][24], RAIL_APP[RAIL_APPS][24], RAIL_MARK[RAIL_APPS][3];
static uint8_t ICONS[RAIL_APPS][ICON_CAP];
static uint32_t icon_len[RAIL_APPS];
static int8_t icon_st[RAIL_APPS];    /* 0 nothing, 1 asked for, 2 here, -1 failed */
static uint32_t icon_win[RAIL_APPS]; /* the launch waiting on it, 0 none   */
static uint32_t icon_off[RAIL_APPS], icon_n[RAIL_APPS]; /* a logo's bytes in LOGO */

static zui_desk_t DESK;
static zui_launch_theme_t THEME;
static zxl_player_t PLAY; /* the launch cues, after the boot's own player */
static int16_t AUD2[AUD_MAX * 2];
static zui_vec_work_t VECW; /* scratch for every vector draw, about 0.5 MB */
static zui_glyph_cache_t GC2;
static zui_text_t T_TITLE, T_BODY, T_CAP, T_MARK;
static bool desk_up;

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
            LOGIN.chosen = index;
            return 1;
        }
        if (h == ZUI_LOGIN_ENTER && LOGIN.people[LOGIN.chosen].lock == ZUI_LOCK_NONE) {
            MODE = 2;
            if (!desk_up) desk_init();
            return 1;
        }
        return 0;
    }
    zui_event_t e = {0};
    e.type = type == 1 ? ZUI_EV_PTR_DOWN : type == 2 ? ZUI_EV_PTR_UP : ZUI_EV_PTR_MOVE;
    e.x = x;
    e.y = y;
    e.button = (uint8_t) button;
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
    int used = zui_desk_event(&DESK, &e) ? 1 : 0;
    desk_act();
    return used;
}

EXPORT(zb_key) int zb_key(int key, int down)
{
    if (!down) return 0;
    if (MODE == 1) {
        if (key == ZUI_KEY_ENTER && LOGIN.people[LOGIN.chosen].lock == ZUI_LOCK_NONE) {
            MODE = 2;
            if (!desk_up) desk_init();
            return 1;
        }
        return 0;
    }
    if (MODE != 2) return 0;
    zui_event_t e = {0};
    e.type = ZUI_EV_KEY_DOWN;
    e.key = (uint16_t) key;
    int used = zui_desk_event(&DESK, &e) ? 1 : 0;
    desk_act();
    return used;
}

EXPORT(zb_desk) int zb_desk(void)
{
    return MODE;
}

/* Which rail app's icon the page should stream in next: its index + 1, or 0
 * when nothing is waiting. */
EXPORT(zb_icon_want) uint32_t zb_icon_want(void)
{
    for (uint32_t i = 0; i < RAIL_APPS; i++)
        if (icon_st[i] == 1) return i + 1;
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

EXPORT(zb_init) void zb_init(int reduce_motion, int sound)
{
    zxl_prefs_t p = zxl_prefs_default();
    if (!sound) p.sounds = 0;
    zui_boot_init(&BOOT, &p, reduce_motion != 0);
    logo_used = 0;
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
    const uint32_t *src;
    if (MODE == 2) {
        zui_desk_tick(&DESK, ms);
        zui_desk_draw(&DESK, &S, BOOT.tick);
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

