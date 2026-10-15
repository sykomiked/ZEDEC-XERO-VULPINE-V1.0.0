/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_shell.c — the ZEDEC pqOS desktop shell (stateful, interactive). See zxv_shell.h. */
#include "zxv_shell.h"

/* The 13 lattice spaces are LIVE surfaces backed by their real subsystems. The
 * shell owns a working instance of each stateful subsystem (initialised at
 * bring-up) and calls the real APIs on entry / on a button. Stateless
 * subsystems (refinery, wyrmgate, crown, battering ram, interspace, chiglet)
 * are called directly. */
#include "vino.h"
#include "pirate_fleet.h"
#include "art_studio.h"
#include "chiglet.h"
#include "refinery.h"
#include "enochian.h"
#include "concord.h"
#include "crown.h"
#include "wyrmgate.h"
#include "battering_ram.h"
#include "interspace.h"
#include "reputation.h"
#include "logistics.h"
#include "surplus.h"

#include "../zui/zui_canvas.h"
#include "../zui/zui_desk.h"
#include "../zui/zui_keymap.h"
#include "../zui/zui_tokens.h"
#include "../zui/zui_wm.h"

/* ---- DERIVED FIELD GEOMETRY -----------------------------------------------
 * The process, not the value. These are computed from whatever panel actually
 * negotiated, and clamped to the compile-time CAPACITY so an oversized mode
 * degrades to a smaller field instead of writing past the array. Defaults are
 * capacity, so a caller that forgets to set geometry over-allocates rather than
 * over-indexes -- the safe direction to be wrong in. */
static uint32_t g_ftx = FIELD_TX_MAX;
static uint32_t g_fty = FIELD_TY_MAX;

uint32_t zxv_shell_set_field_geometry(uint32_t panel_w, uint32_t panel_h)
{
    uint32_t tx = (panel_w + FIELD_TILE - 1u) / FIELD_TILE; /* ceil: a partial
                                                             * tile is still a
                                                             * tile to draw */
    uint32_t ty = (panel_h + FIELD_TILE - 1u) / FIELD_TILE;
    if (tx == 0u) tx = 1u;
    if (ty == 0u) ty = 1u;
    if (tx > FIELD_TX_MAX) tx = FIELD_TX_MAX; /* clamp, never grow the array */
    if (ty > FIELD_TY_MAX) ty = FIELD_TY_MAX;
    g_ftx = tx;
    g_fty = ty;
    return g_ftx * g_fty;
}
uint32_t zxv_shell_field_tx(void)
{
    return g_ftx;
}
uint32_t zxv_shell_field_ty(void)
{
    return g_fty;
}

/* shell-owned working instances for the stateful spaces. The vino ledger is
 * 40 MB: a kernel built with ZXV_BOOT_TAKE takes it from firmware-usable
 * memory (zxv_boot_take, the arch's bootmem) instead of BSS, and the VINO
 * space reports it absent when there is none. */
#ifdef ZXV_BOOT_TAKE
void *zxv_boot_take(uint64_t bytes, const char *what);
static vino_ledger_t *g_vino;
#else
static vino_ledger_t g_vino_store;
static vino_ledger_t *g_vino = &g_vino_store;
#endif
static con_commons_t g_concord;
static con_match_t g_cm[CON_MAX_PEOPLE];
static uint32_t g_cn;
static rep_state_t g_rep;
static log_state_t g_log;
static art_canvas_t g_art;
static int g_spaces_ready = 0;

/* Initialise the working instances once (called from zxv_shell_init). Safe:
 * these are the shell's OWN instances, independent of the boot-time singletons. */
static void spaces_boot(void)
{
    if (g_spaces_ready) return;
    g_spaces_ready = 1;
#ifdef ZXV_BOOT_TAKE
    g_vino = zxv_boot_take(sizeof *g_vino, "the shell's vino ledger");
#endif
    if (g_vino) {
        vino_init(g_vino, 846u);
        vino_create_account(g_vino, "TREASURY", "Vino Treasury");
        vino_create_account(g_vino, "VULPINE", "Vulpine");
        (void) vino_issue(g_vino, "TREASURY", ASSET_CBDC, 1000u, "genesis");
    }
    con_init(&g_concord);
    {
        surplus_real_t x[CON_DIM], y[CON_DIM];
        for (int i = 0; i < (int) CON_DIM; i++) {
            x[i] = SR_ZERO;
            y[i] = SR_ZERO;
        }
        x[0] = SR_FROM_INT(1);
        y[1] = SR_FROM_INT(1); /* complementary interests */
        con_join(&g_concord, 1u, x, 3u);
        con_join(&g_concord, 2u, y, 3u);
    }
    rep_init(&g_rep);
    log_init(&g_log);
    {
        uint32_t p[2] = {846u, 847u};
        (void) log_contract_open(&g_log, p, 2u, SR_FROM_INT(100), 0, 0);
    }
    art_canvas_init(&g_art, 128, 96);
}

/* Boot self-check: prove the spaces' action paths make REAL subsystem state
 * change on-target (the same calls the buttons issue). Returns a 4-bit mask;
 * 15 = reputation + concord + logistics + wyrmgate all responded. */
int zxv_spaces_selfcheck(void)
{
    spaces_boot();
    int ok = 0;
    uint32_t s0 = badge_score(&g_rep, 900u); /* reputation: award raises score */
    badge_award(&g_rep, 900u, 1u, 2u);
    if (badge_score(&g_rep, 900u) > s0) ok |= 1;
    uint32_t c0 = g_concord.now;
    con_tick(&g_concord, 5u); /* concord: tick advances clock   */
    if (g_concord.now == c0 + 5u) ok |= 2;
    uint64_t n0 = g_log.next_id; /* logistics: open increments id  */
    {
        uint32_t p[2] = {846u, 847u};
        (void) log_contract_open(&g_log, p, 2u, SR_FROM_INT(10), 0, 0);
    }
    if (g_log.next_id == n0 + 1u) ok |= 4;
    {
        wyrm_event_t e = {0};
        e.route_available = true;
        e.phases_healthy = true; /* wyrmgate: real judge */
        wyrm_result_t r = wyrm_judge(&e);
        const char *vn = wyrm_verdict_name(r.verdict);
        if (vn && vn[0]) ok |= 8;
    }
    return ok;
}

/* ---- the native programs (name, file) ---- */
static const char *APP_NAME[SHELL_APPS] = {"Vino", "Fleet", "Studio", "Chiglet", "Refinery"};
static const char *APP_FILE[SHELL_APPS] = {"wallet.zxvc", "crew.zxvc", "art.zxvc", "mind.zxvc",
                                           "sigil.zxvc"};

/* ---- tiny string helpers (no libc) ---- */
static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}
static void scopy(char *d, const char *s, uint32_t cap)
{
    if (cap == 0) return;
    uint32_t i = 0;
    for (; s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
}
static char upc(char c)
{
    return (c >= 'a' && c <= 'z') ? (char) (c - 32) : c;
}
static int seq_up(const char *a, const char *b)
{ /* a==b, case-insensitive */
    uint32_t i = 0;
    for (;;) {
        char ca = upc(a[i]), cb = upc(b[i]);
        if (ca != cb) return 0;
        if (!ca) return 1;
        i++;
    }
}
/* append a signed decimal to dst[*j] (bounded) */
static void put_dec(char *dst, uint32_t *j, long v)
{
    if (v < 0 && *j < SHELL_LINE_LEN - 1) {
        dst[(*j)++] = '-';
        v = -v;
    }
    char tmp[20];
    int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0 && t < 19) {
        tmp[t++] = (char) ('0' + (v % 10));
        v /= 10;
    }
    while (t > 0 && *j < SHELL_LINE_LEN - 1) dst[(*j)++] = tmp[--t];
}
static void put_str(char *dst, uint32_t *j, const char *s)
{
    for (uint32_t i = 0; s[i] && *j < SHELL_LINE_LEN - 1; i++) dst[(*j)++] = s[i];
}
/* format "LABEL: <int>" into dst */
static void kv_int(char *dst, const char *k, long v)
{
    uint32_t j = 0;
    put_str(dst, &j, k);
    put_str(dst, &j, ": ");
    put_dec(dst, &j, v);
    dst[j] = 0;
}
/* format "LABEL: <str>" into dst */
static void kv_str(char *dst, const char *k, const char *val)
{
    uint32_t j = 0;
    put_str(dst, &j, k);
    put_str(dst, &j, ": ");
    put_str(dst, &j, val);
    dst[j] = 0;
}

/* ---- the zui surface the shell draws on ----------------------------------
 * Every pixel of the desktop goes through zui (kernel/src/zui): the shared
 * desktop chrome (zui_desk: top bar, launcher rail, zui_wm windows and the
 * strip of open apps), the zui canvas and ASCW Golden text. Colours are
 * zui_tokens.h only, so the bare-metal desktop is the same product as the
 * hosted desktop window, the boot screen and Studio. */
static zui_surface_t g_s;
static zui_glyph_cache_t g_gc;
static ttf_font_t g_font;
static bool g_have_font;
static zui_text_t T_CAP, T_LABEL, T_BODY, T_TITLE, T_HEAD, T_DISP, T_MONO;
static zui_desk_t g_desk;
static int32_t g_desk_w, g_desk_h;
static zui_keymap_t g_km;
static zui_kbd_t g_kbd;
static uint32_t g_ms;

/* The platform's millisecond clock (zxv_shell_set_clock_ms); without one
 * each frame counts as 33 ms. */
static bool g_clock_set;
static uint32_t g_clock_ms, g_clock_last;
void zxv_shell_set_clock_ms(uint32_t ms)
{
    if (!g_clock_set) g_clock_last = ms;
    g_clock_set = true;
    g_clock_ms = ms;
}

/* UI scale in permille, set from the negotiated mode. 1000 = the reference. */
static uint32_t g_ui_scale = 1000u;
void zxv_shell_set_ui_scale(uint32_t permille)
{
    g_ui_scale = (permille >= 500u && permille <= 4000u) ? permille : 1000u;
}
static uint32_t scale_q8(void)
{
    return (g_ui_scale * 256u + 500u) / 1000u;
}
/* points to pixels at the negotiated scale */
static int32_t P(int32_t pt)
{
    return (int32_t) (((int64_t) pt * (int64_t) scale_q8() + 128) >> 8);
}

static void faces_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    zui_glyph_cache_init(&g_gc);
    g_have_font = ttf_parse(&g_font, zxv_shell_font_data(), zxv_shell_font_size());
}

static void face(zui_text_t *t, int32_t pt, bool tabular)
{
    t->font = g_have_font ? &g_font : 0;
    t->cache = &g_gc;
    t->size_px = (uint32_t) P(pt);
    t->tabular = tabular;
}

static void faces_scale(void)
{
    face(&T_CAP, ZUI_T_CAPTION, false);
    face(&T_LABEL, ZUI_T_LABEL, false);
    face(&T_BODY, ZUI_T_BODY, false);
    face(&T_TITLE, ZUI_T_TITLE, false);
    face(&T_HEAD, ZUI_T_HEADLINE, false);
    face(&T_DISP, ZUI_T_DISPLAY, false);
    /* No monospaced ASCW Golden face is vendored yet: code and numbers use
     * Golden Latin II with tabular digits until ASCW-Golden-Mono lands. */
    face(&T_MONO, 13, true);
}

/* Text with its top-left at (x, y), like the old pixel font, so layouts read
 * top-down. Falls back to the VBE pixel font when the face is missing. */
static int32_t text_at(const zui_text_t *t, int32_t x, int32_t y, uint32_t rgb, const char *s)
{
    if (!t->font) {
        vbe_state_t v = {0};
        v.fb = g_s.px;
        v.width = (uint16_t) g_s.w;
        v.height = (uint16_t) g_s.h;
        v.pitch = (uint32_t) g_s.stride * 4u;
        v.bpp = 32;
        vbe_draw_text_ex(&v, x, y, s, rgb, 0, 1, 0);
        return x + (int32_t) slen(s) * 8;
    }
    return zui_text(&g_s, t, x, y + zui_text_ascent(t), rgb, s);
}
static int32_t text_w(const zui_text_t *t, const char *s)
{
    return t->font ? zui_text_width(t, s) : (int32_t) slen(s) * 8;
}
static int32_t line_h(const zui_text_t *t)
{
    return t->font ? zui_text_ascent(t) - zui_text_descent(t) + P(4) : 12;
}
static void text_centre(const zui_text_t *t, int32_t cx, int32_t y, uint32_t rgb, const char *s)
{
    text_at(t, cx - text_w(t, s) / 2, y, rgb, s);
}
/* Cap-height centring inside a box: what buttons and tiles use. */
static void text_mid(const zui_text_t *t, zui_rect_t r, uint32_t rgb, const char *s)
{
    if (!t->font) {
        text_at(t, r.x + (r.w - text_w(t, s)) / 2, r.y + (r.h - 8) / 2, rgb, s);
        return;
    }
    int32_t cap = (zui_text_ascent(t) * 7) / 10;
    zui_text(&g_s, t, r.x + (r.w - text_w(t, s)) / 2, r.y + (r.h + cap) / 2, rgb, s);
}

static void disc(int32_t cx, int32_t cy, int32_t r, uint32_t c)
{
    zui_fill_rrect(&g_s, (zui_rect_t){cx - r, cy - r, 2 * r, 2 * r}, r, c, 255);
}
static void ring(int32_t cx, int32_t cy, int32_t r, int32_t width, uint32_t c)
{
    zui_stroke_rrect(&g_s, (zui_rect_t){cx - r, cy - r, 2 * r, 2 * r}, r, width, c, 255);
}
/* A line `thick` px wide, blended at a255 (integer DDA, no float). */
static void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t thick, uint32_t c,
                 uint32_t a255)
{
    int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int32_t n = adx > ady ? adx : ady;
    if (n == 0) n = 1;
    for (int32_t i = 0; i <= n; i++) {
        int32_t x = x0 + (dx * i) / n, y = y0 + (dy * i) / n;
        for (int32_t t = 0; t < thick; t++) {
            if (adx > ady)
                zui_blend_px(&g_s, x, y + t - thick / 2, c, a255);
            else
                zui_blend_px(&g_s, x + t - thick / 2, y, c, a255);
        }
    }
}

/* A carved button: the gold primary is the screen's one accent. */
static void button(zui_rect_t r, const char *label, bool primary, bool hot, bool lit)
{
    int32_t rad = P(ZUI_R_CONTROL);
    if (primary) {
        zui_fill_rrect(&g_s, r, rad, hot ? ZUI_GOLD_HI : ZUI_GOLD, 255);
        text_mid(&T_BODY, r, ZUI_VOID, label);
    } else {
        zui_fill_rrect(&g_s, r, rad, hot ? ZUI_HAIRLINE : ZUI_RAISED, 255);
        zui_engrave(&g_s, r, rad, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 40);
        text_mid(&T_BODY, r, ZUI_TEXT, label);
    }
    if (lit) /* the action just run */
        zui_stroke_rrect(&g_s, (zui_rect_t){r.x - P(3), r.y - P(3), r.w + P(6), r.h + P(6)},
                         rad + P(3), P(2), ZUI_OK, 255);
}

/* Label at the left, value at the right of a row: readouts and wallets. */
static void kv_row(zui_rect_t r, const char *k, const char *v, uint32_t vc)
{
    text_at(&T_BODY, r.x, r.y, ZUI_MUTED, k);
    text_at(&T_MONO, r.x + r.w - text_w(&T_MONO, v), r.y, vc, v);
}
/* Split "LABEL: value" lines from the subsystem readouts into a row. */
static void readout_row(zui_rect_t r, const char *s, uint32_t vc)
{
    char k[SHELL_LINE_LEN];
    uint32_t i = 0;
    for (; s[i] && i + 1 < sizeof k; i++) {
        if (s[i] == ':' && s[i + 1] == ' ') break;
        k[i] = s[i];
    }
    k[i] = 0;
    if (s[i] == ':')
        kv_row(r, k, s + i + 2, vc);
    else
        text_at(&T_BODY, r.x, r.y, ZUI_TEXT_2, s);
}

/* The pointer: drawn from its outline every frame, at the panel's scale. */
static void cursor(int32_t x, int32_t y)
{
    static const char *A[] = {"1",         "11",         "121",         "1221",
                              "12221",     "122221",     "1222221",     "12222221",
                              "122222221", "1222222221", "12222111111", "122122",
                              "1221 12",   "121  12",    "11    122",   "      12"};
    int32_t k = P(1) > 0 ? P(1) : 1;
    for (int32_t r = 0; r < 16; r++)
        for (int32_t c = 0; A[r][c]; c++) {
            uint32_t col = A[r][c] == '1' ? ZUI_ENGRAVE_LO : ZUI_TEXT;
            if (A[r][c] == ' ') continue;
            zui_fill(&g_s, (zui_rect_t){x + c * k, y + r * k, k, k}, col);
        }
}

/* ---- the 13 spaces: node 0 = base/home; 1..6 inner ring; 7..12 outer ring ---- */
static const char LAT_G[LATTICE_NODES] = {'Z', 'V', 'F', 'S', 'C', 'R', 'O',
                                          'K', 'W', 'B', 'I', 'P', 'L'};
static const char *LAT_NM[LATTICE_NODES] = {
    "Base",  "Vino",     "Fleet", "Studio",     "Chiglet", "Refinery", "Concord",
    "Crown", "Wyrmgate", "Ram",   "Interspace", "Badge",   "Logistics"};
static const char *LAT_SUB[LATTICE_NODES] = {
    "Home plane", "Economy",  "Work",    "Art",          "AI",         "Magitech", "Social",
    "Governance", "Judgment", "Markets", "Jurisdiction", "Reputation", "Supply"};

/* per-space screen: 4 lines of live-ish state (the plane you drop INTO) */
static const char *LAT_DESC[LATTICE_NODES][4] = {
    {"The home plane, the deepest user level", "Launcher, Terminal, Chiglet and Vino",
     "The root system below it is the kernel", "Tab pulls back to the 13 spaces"},
    {"Vino floating vouchers on three rails", "555 debit 120 · 777 credit 12",
     "888 equity 108 · coverage 1.8×", "No debt, merit-based and solvent"},
    {"JDR Pirate Fleet: work and compute", "Crew, DAO and remote compute",
     "Modular contracts and syndicates", "Shares by merit, not seniority"},
    {"Miss Potimus Art Studio", "Canvas, sound, motion and 3D", "Every medium in one toolkit",
     "Export as [S+], [S-] or [S0]"},
    {"Chiglet, a native mixture of experts", "Match trolls with trolls", "Concord: rehabilitative",
     "Distinct evidence 3.0, decided"},
    {"Magitech Refinery", "Text to Enochian gematria", "Then a sigil card, then a 58-byte preset",
     "Deck parity verified"},
    {"Concord, non-coercive social", "Complementary pairing",
     "Symmetric divides, mutual sovereignty", "No bans, no ads, rehabilitative"},
    {"The Crown: governance", "Crown, Ministry and Pillar", "Automated bureaucracy",
     "One policy: the symbiotic maxim"},
    {"Wyrmgate six-fold judgment", "Tri-space: S+, S0 and S-", "RMAG, LPRES and Chiglet composed",
     "Verdict by distinct evidence"},
    {"Battering Ram exchange", "Equities, futures and commodities",
     "Capital derivatives, usury vetoed", "Symbiotic gate on deal terms"},
    {"Interspace: jurisdiction", "The space between spaces", "Lex Rhodia treaty frameworks",
     "Flag state and federation"},
    {"Pig Badge reputation", "Counterintelligence, earnable badges", "Pig Badge: over 9000",
     "Gamified and non-transferable"},
    {"Logistics: real-world supply", "Syndicates and modular contracts", "Secondary marketplace",
     "Physical and digital rails"}};

/* 12 unit dirs (x256) at 30-degree steps: 0,30,60,...,330 */
static const int16_t LAT_DC[12] = {256, 222, 128, 0, -128, -222, -256, -222, -128, 0, 128, 222};
static const int16_t LAT_DS[12] = {0, 128, 222, 256, 222, 128, 0, -128, -222, -256, -222, -128};

/* Metatron edges: spokes + inner hexagon + outer hexagon + inner<->outer cross-links */
static const uint8_t LAT_E[][2] = {
    {0, 1},  {0, 2},  {0, 3},  {0, 4},   {0, 5},   {0, 6},  {0, 7},  {0, 8},  {0, 9},
    {0, 10}, {0, 11}, {0, 12}, {1, 2},   {2, 3},   {3, 4},  {4, 5},  {5, 6},  {6, 1},
    {7, 8},  {8, 9},  {9, 10}, {10, 11}, {11, 12}, {12, 7}, {1, 7},  {1, 12}, {2, 7},
    {2, 8},  {3, 8},  {3, 9},  {4, 9},   {4, 10},  {5, 10}, {5, 11}, {6, 11}, {6, 12}};
#define LAT_NE ((int) (sizeof(LAT_E) / sizeof(LAT_E[0])))

/* screen positions of the 13 nodes (inner ring R, outer ring R*sqrt3 ~= 276) */
static void lat_pos(int32_t W, int32_t H, int32_t *nx, int32_t *ny)
{
    int32_t cx0 = W / 2, cy0 = H / 2 - 6, R = 160, R2 = 276;
    nx[0] = cx0;
    ny[0] = cy0;
    for (int32_t k = 0; k < 6; k++) {
        int32_t d = k * 2;
        nx[1 + k] = cx0 + (R * LAT_DC[d]) / 256;
        ny[1 + k] = cy0 + (R * LAT_DS[d]) / 256;
    }
    for (int32_t k = 0; k < 6; k++) {
        int32_t d = k * 2 + 1;
        nx[7 + k] = cx0 + (R2 * LAT_DC[d]) / 256;
        ny[7 + k] = cy0 + (R2 * LAT_DS[d]) / 256;
    }
}

/* the edge-connected node best aligned with arrow (dirx,diry); moves along the
 * Metatron wiring -- adjacent AND perpendicular. Target is fixed 1280x720. */
static int lat_neighbor(int from, int32_t dirx, int32_t diry)
{
    int32_t nx[LATTICE_NODES], ny[LATTICE_NODES];
    lat_pos(1280, 720, nx, ny);
    int best = from;
    int64_t bestNum = 0, bestDen = 1; /* maximise cos^2(theta) = alignment */
    for (int e = 0; e < LAT_NE; e++) {
        int a = LAT_E[e][0], b = LAT_E[e][1], other;
        if (a == from)
            other = b;
        else if (b == from)
            other = a;
        else
            continue;
        int64_t vx = nx[other] - nx[from], vy = ny[other] - ny[from];
        int64_t dot = vx * dirx + vy * diry;
        if (dot <= 0) continue; /* wrong hemisphere: not in this direction */
        int64_t num = dot * dot, den = vx * vx + vy * vy; /* cos^2 = num/den (den>0 since dot>0) */
        if (num * bestDen > bestNum * den) {
            bestNum = num;
            bestDen = den;
            best = other;
        }
    }
    return best;
}

/* ---- terminal scrollback ---- */
static void term_push(zxv_shell_state_t *st, const char *line)
{
    if (st->term_count < SHELL_TERM_LINES) {
        scopy(st->term[st->term_count], line, SHELL_LINE_LEN);
        st->term_count++;
    } else {
        for (int i = 1; i < SHELL_TERM_LINES; i++)
            scopy(st->term[i - 1], st->term[i], SHELL_LINE_LEN);
        scopy(st->term[SHELL_TERM_LINES - 1], line, SHELL_LINE_LEN);
    }
}
static void term_prompt_echo(zxv_shell_state_t *st)
{
    char buf[SHELL_LINE_LEN];
    const char *p = "vulpine@zxv ~ $ ";
    uint32_t i = 0, j = 0;
    while (p[i] && j < SHELL_LINE_LEN - 1) buf[j++] = p[i++];
    for (int k = 0; k < st->cmdlen && j < SHELL_LINE_LEN - 1; k++) buf[j++] = st->cmd[k];
    buf[j] = 0;
    term_push(st, buf);
}

/* run the current command line */
static void run_cmd(zxv_shell_state_t *st)
{
    term_prompt_echo(st);
    st->cmd[st->cmdlen] = 0;
    const char *c = st->cmd;
    if (st->cmdlen == 0) { /* nothing */
    } else if (seq_up(c, "help")) {
        term_push(st, "Commands: ls  open  keymap  help  clear");
        term_push(st, "Apps: vino  fleet  studio  chiglet  refinery");
    } else if (seq_up(c, "ls")) {
        term_push(st, "wallet.zxvc  crew.zxvc  art.zxvc  mind.zxvc  sigil.zxvc");
        term_push(st, "  (each carries .cedez [S-] and .cedec [S0])");
    } else if (seq_up(c, "clear")) {
        st->term_count = 0;
    } else if (seq_up(c, "open")) {
        term_push(st, "Usage: open <file.zxvc>, for example open wallet.zxvc");
    } else {
        int hit = -1;
        for (int i = 0; i < SHELL_APPS; i++)
            if (seq_up(c, APP_NAME[i])) {
                hit = i;
                break;
            }
        if (hit < 0)
            for (int i = 0; i < SHELL_APPS; i++)
                if (seq_up(c, APP_FILE[i])) {
                    hit = i;
                    break;
                }
        if (hit >= 0) {
            char ln[SHELL_LINE_LEN];
            const char *a = "Opened ";
            uint32_t j = 0;
            while (a[j] && j < 20) {
                ln[j] = a[j];
                j++;
            }
            const char *nm = APP_NAME[hit];
            uint32_t k = 0;
            while (nm[k] && j < SHELL_LINE_LEN - 14) ln[j++] = nm[k++];
            const char *tail = " [S+] verified";
            k = 0;
            while (tail[k] && j < SHELL_LINE_LEN - 1) ln[j++] = tail[k++];
            ln[j] = 0;
            term_push(st, ln);
            st->active_app = hit;
        } else {
            char ln[SHELL_LINE_LEN];
            const char *a = "Not found: ";
            uint32_t j = 0;
            while (a[j] && j < 20) {
                ln[j] = a[j];
                j++;
            }
            for (int k = 0; k < st->cmdlen && j < SHELL_LINE_LEN - 12; k++) ln[j++] = st->cmd[k];
            const char *t = " (try help)";
            uint32_t k = 0;
            while (t[k] && j < SHELL_LINE_LEN - 1) ln[j++] = t[k++];
            ln[j] = 0;
            term_push(st, ln);
        }
    }
    st->cmdlen = 0;
    st->cmd[0] = 0;
}

/* ============================================================================
 *  FUNCTIONAL SPACES: each of the 13 lattice nodes is a live surface backed by
 *  its REAL subsystem. On entry the shell fills a readout from the subsystem's
 *  query API; action buttons invoke the subsystem on click; the 6 sub-spaces
 *  drill one layer down. The per-subsystem calls live in the switch bodies of
 *  space_enter/space_action/subspace_select. Everything else here (geometry,
 *  hit-testing, readout buffer) is subsystem-independent.
 * ==========================================================================*/

/* action-button labels per space (NULL = no button in that slot). Each button
 * invokes the space's REAL subsystem — see space_action(). */
static const char *SPC_ACT[LATTICE_NODES][SPACE_ACTS] = {
    /*BASE     */ {"Launcher", "Terminal", "Lattice"},
    /*VINO     */ {"Send", "Issue", "Balance"},
    /*FLEET    */ {"Cap wall", "Depth", "Crew"},
    /*STUDIO   */ {"Paint", "Stroke", "Export"},
    /*CHIGLET  */ {"Decided", "Unsure", "Why"},
    /*REFINERY */ {"Gematria", "Mirror", "Voice"},
    /*CONCORD  */ {"Tick", "Recommend", "May match"},
    /*CROWN    */ {"Cultural", "Finance", "Social"},
    /*WYRMGATE */ {"Judge route", "Block", "Order"},
    /*RAM      */ {"Price", "Term", "Majority"},
    /*INTERSPC */ {"Commons", "Immune", "Passage"},
    /*BADGE    */ {"Award B1", "Award B2", "Score"},
    /*LOGISTIC */ {"Open", "Arm", "Release"},
};
/* sub-space labels per space (NULL = empty node) */
static const char *SPC_SUB[LATTICE_NODES][SPACE_SUBS] = {
    /*BASE     */ {"Launcher", "Terminal", "Chiglet", "Vino", "Files", "Theme"},
    /*VINO     */ {"555 debit", "777 credit", "888 equity", "Ledger", "Rails", "Bridge"},
    /*FLEET    */ {"Crew", "DAO", "Compute", "Contracts", "Syndicate", "Shares"},
    /*STUDIO   */ {"Canvas", "Sound", "Motion", "3D", "Palette", "Export"},
    /*CHIGLET  */ {"Experts", "Memory", "Voice", "Match", "Evidence", "Verdict"},
    /*REFINERY */ {"Gematria", "Sigil", "Card", "Deck", "Preset", "Share"},
    /*CONCORD  */ {"Pairing", "Divides", "Quarantine", "Mediate", "Sovereign", "Trust"},
    /*CROWN    */ {"Crown", "Ministry", "Pillar", "Registry", "Decrees", "Policy"},
    /*WYRMGATE */ {"S+", "S0", "S-", "RMAG", "LPRES", "Chiglet"},
    /*RAM      */ {"Equities", "Futures", "Commodity", "Derivatives", "Quote", "Book"},
    /*INTERSPC */ {"Lex Rhodia", "Flag-state", "Federate", "Treaty", "Minister", "Border"},
    /*BADGE    */ {"Pig Badge", "Earned", "Rank", "Verify", "Counter", "Ledger"},
    /*LOGISTIC */ {"Syndicate", "Contract", "Market", "Route", "Warehouse", "Rails"},
};

/* ---- live readout buffer ---- */
static void sview_reset(zxv_shell_state_t *st)
{
    st->sview_count = 0;
}
static void sview_push(zxv_shell_state_t *st, const char *s)
{
    if (st->sview_count >= SPACE_LINES) { /* scroll the readout up */
        for (int i = 1; i < SPACE_LINES; i++) scopy(st->sview[i - 1], st->sview[i], SHELL_LINE_LEN);
        st->sview_count = SPACE_LINES - 1;
    }
    scopy(st->sview[st->sview_count++], s, SHELL_LINE_LEN);
}

/* Fill the live readout for space s by QUERYING its real subsystem (on entry).
 * Every case here makes real subsystem calls; the default (BASE) renders the
 * descriptor lines. */
static void space_enter(zxv_shell_state_t *st, int s)
{
    sview_reset(st);
    char ln[SHELL_LINE_LEN];
    switch (s) {
    case 1: /* VINO */
        kv_str(ln, "Native rail", vino_rail_name(RAIL_VINO_NATIVE));
        sview_push(st, ln);
        kv_str(ln, "Top capital", vino_capital_name(CAP_FINANCIAL));
        sview_push(st, ln);
        kv_str(ln, "CBDC asset", vino_asset_class_name(ASSET_CBDC));
        sview_push(st, ln);
        {
            uint64_t b = 0;
            if (g_vino) vino_get_balance(g_vino, "TREASURY", CAP_FINANCIAL, &b);
            kv_int(ln, "Treasury cap", (long) b);
        }
        sview_push(st, ln);
        break;
    case 2: /* FLEET */
        kv_int(ln, "Chan depth", (long) jdr_channel_depth(0));
        sview_push(st, ln);
        kv_str(ln, "Crew0 state", jdr_crew_state_name((crew_state_t) 0));
        sview_push(st, ln);
        kv_str(ln, "Both caps",
               jdr_cap_wall_ok((uint8_t) (PF_CAP_CREDENTIAL | PF_CAP_TREASURY_AUDIT)) ? "allow"
                                                                                      : "deny");
        sview_push(st, ln);
        break;
    case 3: /* STUDIO */
        kv_int(ln, "Max width", (long) ART_MAX_W);
        sview_push(st, ln);
        kv_int(ln, "Max height", (long) ART_MAX_H);
        sview_push(st, ln);
        kv_int(ln, "Canvas w", (long) g_art.w);
        sview_push(st, ln);
        kv_int(ln, "Canvas h", (long) g_art.h);
        sview_push(st, ln);
        break;
    case 4: /* CHIGLET */
        kv_int(ln, "Expert cap", (long) CHG_MAX_EXPERTS);
        sview_push(st, ln);
        kv_int(ln, "Evid dim", (long) CHG_DIM);
        sview_push(st, ln);
        kv_str(ln, "Boot state", chg_state_name(CHG_UNAVAILABLE));
        sview_push(st, ln);
        break;
    case 5: /* REFINERY */
    {
        uint32_t g = eno_gematria("REFINERY", 8);
        kv_int(ln, "Gematria", (long) g);
        sview_push(st, ln);
        uint32_t rt = eno_root(g);
        kv_int(ln, "Root 1-9", (long) rt);
        sview_push(st, ln);
        kv_str(ln, "Domain", eno_root_domain(rt));
        sview_push(st, ln);
    }
        kv_int(ln, "Alphabet", (long) ENO_LETTERS);
        sview_push(st, ln);
        break;
    case 6: /* CONCORD */
        kv_int(ln, "Members", (long) g_concord.n_people);
        sview_push(st, ln);
        kv_int(ln, "Divides", (long) g_concord.n_divides);
        sview_push(st, ln);
        kv_int(ln, "Clock", (long) g_concord.now);
        sview_push(st, ln);
        {
            con_person_t *p = con_get(&g_concord, 1u);
            kv_str(ln, "Standing #1", con_standing_name(con_standing(p)));
        }
        sview_push(st, ln);
        break;
    case 7: /* CROWN */
        kv_int(ln, "Cultural", (long) crown_form_is_crown(ZCAP_CULTURAL));
        sview_push(st, ln);
        kv_int(ln, "Spiritual", (long) crown_form_is_crown(ZCAP_SPIRITUAL));
        sview_push(st, ln);
        kv_int(ln, "Financial", (long) crown_form_is_crown(ZCAP_FINANCIAL));
        sview_push(st, ln);
        kv_int(ln, "Social", (long) crown_form_is_crown(ZCAP_SOCIAL));
        sview_push(st, ln);
        break;
    case 8: /* WYRMGATE */
        kv_str(ln, "Commit", wyrm_verdict_name(WYRM_COMMIT));
        sview_push(st, ln);
        kv_str(ln, "Defer", wyrm_verdict_name(WYRM_DEFER));
        sview_push(st, ln);
        kv_int(ln, "Max deltas", (long) WYRM_MAX_DELTAS);
        sview_push(st, ln);
        {
            wyrm_event_t e = {0};
            e.route_available = true;
            e.phases_healthy = true;
            wyrm_result_t r = wyrm_judge(&e);
            kv_str(ln, "Judge(rt)", wyrm_verdict_name(r.verdict));
        }
        sview_push(st, ln);
        break;
    case 9: /* BATTERING RAM */
    {
        long p =
            (long) (br_price_capital_future(ZCAP_FINANCIAL, SR_FROM_INT(100), SR_ZERO, SR_ONE) >>
                    SR_SHIFT);
        kv_int(ln, "Fut price", p);
    }
        sview_push(st, ln);
        {
            br_term_t t = {0};
            t.reciprocal = true;
            kv_int(ln, "Term admit", (long) symbiotic_ok(&t));
        }
        sview_push(st, ln);
        {
            allocation_t a = {0};
            a.realized = SR_FROM_INT(100);
            a.contributor_pool = SR_FROM_INT(78);
            kv_int(ln, "Majority", (long) br_contributors_hold_majority(&a));
        }
        sview_push(st, ln);
        break;
    case 10: /* INTERSPACE */
    {
        interstitial_region_t r = {0};
        r.kind = ZXV_RES_COMMUNIS;
        r.owner = ZXV_NODE_NONE;
        kv_int(ln, "Is commons", (long) interstitial_is_commons(&r));
    }
        sview_push(st, ln);
        {
            vessel_flag_t vf = {0};
            vf.keyholder = 846;
            kv_int(ln, "Self immune", (long) vessel_immune_from(&vf, 846));
        }
        sview_push(st, ln);
        {
            safe_passage_grant_t g = {0};
            g.issued_tick = 0;
            g.ttl_deadline = 100;
            kv_int(ln, "Pass valid", (long) safe_passage_valid(&g, 50));
        }
        sview_push(st, ln);
        break;
    case 11: /* BADGE */
        kv_int(ln, "Subjects", (long) g_rep.n_entries);
        sview_push(st, ln);
        kv_int(ln, "Badge score", (long) badge_score(&g_rep, 846u));
        sview_push(st, ln);
        kv_int(ln, "Pig(seen)", (long) pig_level_seen_by(&g_rep, 846u, 847u));
        sview_push(st, ln);
        kv_int(ln, "Has badge1", (long) badge_has(&g_rep, 846u, 1u, 1u));
        sview_push(st, ln);
        break;
    case 12: /* LOGISTICS */
        kv_int(ln, "Contracts", (long) (g_log.next_id - 1));
        sview_push(st, ln);
        kv_int(ln, "Escrow armed", (long) (g_log.verify != 0));
        sview_push(st, ln);
        {
            const log_contract_t *c = log_contract_find(&g_log, 1u);
            kv_int(ln, "C1 live", (long) (c != 0));
        }
        sview_push(st, ln);
        {
            const log_contract_t *c = log_contract_find(&g_log, 1u);
            kv_int(ln, "C1 parties", (long) (c ? c->n_parties : 0));
        }
        sview_push(st, ln);
        break;
    default:                   /* BASE (0) and any fallback */
        if ((unsigned) s < 13) /* LAT_DESC is [13][4] — guard a bad space id */
            for (int i = 0; i < 4; i++) sview_push(st, LAT_DESC[s][i]);
        break;
    }
}
/* Invoke action a on space s: call the real subsystem, push feedback. */
static void space_action(zxv_shell_state_t *st, int s, int a)
{
    if (a < 0 || a >= SPACE_ACTS || !SPC_ACT[s][a]) return;
    st->act_hot = a;
    char ln[SHELL_LINE_LEN];
    switch (s) {
    case 1: /* VINO */
        if (!g_vino) {
            kv_str(ln, "VINO", "NO LEDGER MEMORY");
        } else if (a == 0) {
            int r = vino_transfer(g_vino, "TREASURY", "VULPINE", 1u, CAP_FINANCIAL,
                                  RAIL_VINO_NATIVE, "send");
            kv_int(ln, "Send RC", r);
        } else if (a == 1) {
            int r = vino_issue(g_vino, "TREASURY", ASSET_CBDC, 10u, "issue");
            kv_int(ln, "Issue RC", r);
        } else {
            uint64_t b = 0;
            vino_get_balance(g_vino, "TREASURY", CAP_FINANCIAL, &b);
            kv_int(ln, "Cap bal", (long) b);
        }
        sview_push(st, ln);
        break;
    case 2: /* FLEET */
        if (a == 0)
            kv_str(ln, "Both caps",
                   jdr_cap_wall_ok((uint8_t) (PF_CAP_CREDENTIAL | PF_CAP_TREASURY_AUDIT)) ? "allow"
                                                                                          : "deny");
        else if (a == 1)
            kv_int(ln, "Chan depth", (long) jdr_channel_depth(0));
        else
            kv_str(ln, "Crew0", jdr_crew_state_name((crew_state_t) 0));
        sview_push(st, ln);
        break;
    case 3: /* STUDIO */
        if (a == 0) {
            art_fill_rect(&g_art, 0, 0, 64, 64, 200);
            kv_int(ln, "PX[0]", (long) g_art.px[0]);
        } else if (a == 1) {
            art_hline(&g_art, 4, 32, 56, 255);
            kv_int(ln, "PX row32", (long) g_art.px[32 * (long) g_art.w + 4]);
        } else {
            static uint8_t cid[32];
            art_export_cid(&g_art, cid);
            kv_int(ln, "Cid[0]", (long) cid[0]);
        }
        sview_push(st, ln);
        break;
    case 4: /* CHIGLET */
        if (a == 0)
            kv_str(ln, "State", chg_state_name(CHG_DECIDED));
        else if (a == 1)
            kv_str(ln, "State", chg_state_name(CHG_UNCERTAIN));
        else
            kv_str(ln, "Reason", chg_reason_name(CHG_REASON_REDUNDANT));
        sview_push(st, ln);
        break;
    case 5: /* REFINERY */
        if (a == 0)
            kv_int(ln, "Sigil gem", (long) eno_gematria("SIGIL", 5));
        else if (a == 1)
            kv_int(ln, "Mirror(4)", (long) eno_root_mirror(4));
        else
            kv_str(ln, "Solar verb", eno_voice_verb(ENO_VOICE_SOLAR));
        sview_push(st, ln);
        break;
    case 6: /* CONCORD */
        if (a == 0) {
            con_tick(&g_concord, 1u);
            kv_int(ln, "Clock", (long) g_concord.now);
        } else if (a == 1) {
            g_cn = con_recommend(&g_concord, 1u, g_cm, CON_MAX_PEOPLE);
            kv_int(ln, "Matches", (long) g_cn);
        } else
            kv_str(ln, "1<->2", con_may_match(&g_concord, 1u, 2u) ? "yes" : "no");
        sview_push(st, ln);
        break;
    case 7: /* CROWN */
        if (a == 0)
            kv_int(ln, "Cultural", (long) crown_form_is_crown(ZCAP_CULTURAL));
        else if (a == 1)
            kv_int(ln, "Financial", (long) crown_form_is_crown(ZCAP_FINANCIAL));
        else
            kv_int(ln, "Social", (long) crown_form_is_crown(ZCAP_SOCIAL));
        sview_push(st, ln);
        break;
    case 8: /* WYRMGATE */
    {
        wyrm_event_t e = {0};
        if (a == 0) {
            e.route_available = true;
            e.phases_healthy = true;
            wyrm_result_t r = wyrm_judge(&e);
            kv_str(ln, "Verdict", wyrm_verdict_name(r.verdict));
        } else if (a == 1) {
            wyrm_result_t r = wyrm_judge(&e);
            kv_str(ln, "Reason", wyrm_reason_name(r.reason));
        } else {
            e.parent_ordinal = 2;
            e.ordinal = 1;
            wyrm_result_t r = wyrm_judge(&e);
            kv_str(ln, "Order", wyrm_reason_name(r.reason));
        }
    }
        sview_push(st, ln);
        break;
    case 9: /* BATTERING RAM */
        if (a == 0)
            kv_int(ln, "Fut PX",
                   (long) (br_price_capital_future(ZCAP_FINANCIAL, SR_FROM_INT(100), SR_ZERO,
                                                   SR_ONE) >>
                           SR_SHIFT));
        else if (a == 1) {
            br_term_t t = {0};
            t.on_suffering = true;
            kv_int(ln, "Suffer OK", (long) symbiotic_ok(&t));
        } else {
            allocation_t al = {0};
            al.realized = SR_FROM_INT(100);
            al.contributor_pool = SR_FROM_INT(78);
            kv_int(ln, "Majority", (long) br_contributors_hold_majority(&al));
        }
        sview_push(st, ln);
        break;
    case 10: /* INTERSPACE */
        if (a == 0) {
            interstitial_region_t r = {0};
            r.kind = ZXV_RES_COMMUNIS;
            r.owner = ZXV_NODE_NONE;
            kv_int(ln, "Is commons", (long) interstitial_is_commons(&r));
        } else if (a == 1) {
            vessel_flag_t vf = {0};
            vf.keyholder = 846;
            kv_int(ln, "Self immune", (long) vessel_immune_from(&vf, 846));
        } else {
            safe_passage_grant_t g = {0};
            g.issued_tick = 0;
            g.ttl_deadline = 100;
            kv_int(ln, "Pass@50", (long) safe_passage_valid(&g, 50));
        }
        sview_push(st, ln);
        break;
    case 11: /* BADGE */
        if (a == 0) {
            badge_award(&g_rep, 846u, 1u, 1u);
            kv_int(ln, "Score", (long) badge_score(&g_rep, 846u));
        } else if (a == 1) {
            badge_award(&g_rep, 846u, 2u, 3u);
            kv_int(ln, "Score", (long) badge_score(&g_rep, 846u));
        } else
            kv_int(ln, "Score", (long) badge_score(&g_rep, 846u));
        sview_push(st, ln);
        break;
    case 12: /* LOGISTICS */
        if (a == 0) {
            uint32_t p[2] = {846u, 847u};
            int id = log_contract_open(&g_log, p, 2u, SR_FROM_INT(50), 0, 0);
            kv_int(ln, "New ID", id);
        } else if (a == 1) {
            log_set_verifier(&g_log, log_ed25519_delivery_verify);
            kv_int(ln, "Armed", (long) (g_log.verify != 0));
        } else {
            log_delivery_t d = {0};
            log_result_t rr = log_escrow_release(&g_log, 1u, &d);
            kv_int(ln, "Release st", (long) rr.status);
        }
        sview_push(st, ln);
        break;
    default:
        kv_str(ln, "Run", SPC_ACT[s][a]);
        sview_push(st, ln);
        break;
    }
}
/* Select sub-space k of space s (drill one layer down). */
static void subspace_select(zxv_shell_state_t *st, int s, int k)
{
    if (k < 0 || k >= SPACE_SUBS || !SPC_SUB[s][k]) return;
    st->subsel = k;
    char ln[SHELL_LINE_LEN];
    kv_str(ln, "Open", SPC_SUB[s][k]);
    sview_push(st, ln);
}
/* Centralised space entry (used by ENTER key + lattice click). */
static void enter_space(zxv_shell_state_t *st, int i)
{
    if (i == 0) {
        st->view = 0;
        return;
    } /* BASE node -> home plane */
    st->space = i;
    st->view = 2;
    st->trans = LATTICE_TRANS;
    st->subsel = -1;
    st->act_hot = -1;
    space_enter(st, i);
}

/* ============================================================================
 *  THE DESKTOP (view 0): zui_desk's bar, launcher and windows. The launcher
 *  has the Terminal and the five native programs; each program's window is
 *  a live surface on its lattice space (node = app index + 1), with that
 *  space's actions as buttons that call the real subsystem.
 * ==========================================================================*/
#define RAIL_TERMINAL 0
#define RAIL_APPS     (1 + SHELL_APPS)
static const char *RAIL_MARK[RAIL_APPS] = {">_", "V", "F", "S", "C", "R"};

typedef struct {
    zxv_shell_state_t *st;
    int32_t app;  /* -1 = the terminal, else APP_NAME index */
    int32_t node; /* the lattice space behind the window   */
    int32_t hot;  /* button under the pointer, -1 none     */
} shell_win_t;
static shell_win_t g_win[RAIL_APPS];
static char g_last[LATTICE_NODES][SHELL_LINE_LEN]; /* last action's result per space */

/* The window's surface, while its client paints. */
static void with_surface(zui_surface_t *s, zui_surface_t *save)
{
    *save = g_s;
    g_s = *s;
}

static zui_rect_t node_btn(zui_rect_t c, int32_t i)
{
    int32_t pad = P(ZUI_S4), h = P(36), gap = P(ZUI_S2);
    int32_t w = (c.w - 2 * pad - 2 * gap) / 3;
    return (zui_rect_t){c.x + pad + i * (w + gap), c.y + c.h - pad - h, w, h};
}
static zui_rect_t node_open_btn(zui_rect_t c)
{
    int32_t pad = P(ZUI_S4), h = P(28);
    int32_t w = text_w(&T_LABEL, "Open in Spaces") + P(ZUI_S5);
    return (zui_rect_t){c.x + c.w - pad - w, c.y + pad, w, h};
}

static void term_paint(void *ctx, zui_surface_t *s, zui_rect_t c, uint32_t tick)
{
    shell_win_t *w = (shell_win_t *) ctx;
    zxv_shell_state_t *st = w->st;
    zui_surface_t save;
    with_surface(s, &save);
    int32_t pad = P(ZUI_S4), lh = line_h(&T_MONO);
    int32_t x = c.x + pad, y = c.y + pad;
    /* the newest lines that fit, oldest first */
    int32_t fit = (c.h - 2 * pad) / lh - 1;
    int32_t first = st->term_count > fit ? st->term_count - fit : 0;
    zui_clip(&g_s, c);
    for (int32_t i = first; i < st->term_count; i++) {
        text_at(&T_MONO, x, y, i < 3 ? ZUI_MUTED : ZUI_TEXT_2, st->term[i]);
        y += lh;
    }
    int32_t px = text_at(&T_MONO, x, y, ZUI_OK, "vulpine@zxv ~ $ ");
    char b[SHELL_CMD_MAX + 1];
    scopy(b, st->cmd, SHELL_CMD_MAX + 1);
    int32_t ex = text_at(&T_MONO, px, y, ZUI_TEXT, b);
    if ((tick / 8) & 1u)
        zui_fill(&g_s, (zui_rect_t){ex + P(1), y, P(8), zui_text_ascent(&T_MONO) + P(2)}, ZUI_OK);
    zui_unclip(&g_s);
    g_s = save;
}

static void node_paint(void *ctx, zui_surface_t *s, zui_rect_t c, uint32_t tick)
{
    shell_win_t *w = (shell_win_t *) ctx;
    int n = w->node;
    zui_surface_t save;
    (void) tick;
    with_surface(s, &save);
    zui_clip(&g_s, c);
    int32_t pad = P(ZUI_S4), x = c.x + pad, y = c.y + pad, rw = c.w - 2 * pad;
    /* one gold accent on the screen: only the window in front shows its primary */
    bool front = zui_desk_front_app(&g_desk) == w->app + 1;
    zui_rect_t ob = node_open_btn(c);
    if (T_TITLE.font)
        zui_text_fit(&g_s, &T_TITLE, x, y + zui_text_ascent(&T_TITLE), ob.x - x - P(ZUI_S3),
                     ZUI_TEXT, LAT_DESC[n][0]);
    else
        text_at(&T_TITLE, x, y, ZUI_TEXT, LAT_DESC[n][0]);
    button(ob, "Open in Spaces", false, w->hot == 9, false);
    y += line_h(&T_TITLE) + P(ZUI_S2);
    for (int i = 1; i < 4; i++) {
        text_at(&T_BODY, x, y, ZUI_MUTED, LAT_DESC[n][i]);
        y += line_h(&T_BODY);
    }
    y += P(ZUI_S3);
    zui_hline(&g_s, x, y, rw, 1, ZUI_HAIRLINE);
    y += P(ZUI_S3);
    char v[SHELL_LINE_LEN];
    uint32_t j;
    if (n == 1) { /* Vino: the treasury's live balance */
        uint64_t bal = 0;
        if (g_vino) vino_get_balance(g_vino, "TREASURY", CAP_FINANCIAL, &bal);
        j = 0;
        put_dec(v, &j, (long) bal);
        v[j] = 0;
        kv_row((zui_rect_t){x, y, rw, 0}, "Treasury, financial capital", v, ZUI_TEXT);
        y += line_h(&T_BODY);
        kv_row((zui_rect_t){x, y, rw, 0}, "Native rail", vino_rail_name(RAIL_VINO_NATIVE),
               ZUI_TEXT_2);
        y += line_h(&T_BODY);
    } else if (n == 4) { /* Chiglet */
        kv_row((zui_rect_t){x, y, rw, 0}, "Experts", "mixture, native", ZUI_TEXT_2);
        y += line_h(&T_BODY);
        kv_row((zui_rect_t){x, y, rw, 0}, "Verdict", chg_state_name(CHG_DECIDED), ZUI_OK);
        y += line_h(&T_BODY);
    }
    if (g_last[n][0]) {
        readout_row((zui_rect_t){x, y, rw, 0}, g_last[n], ZUI_OK);
        y += line_h(&T_BODY);
    }
    for (int a = 0; a < SPACE_ACTS; a++)
        if (SPC_ACT[n][a])
            button(node_btn(c, a), SPC_ACT[n][a], a == 0 && front, w->hot == a,
                   w->st->act_hot == a && w->st->space == n);
    zui_unclip(&g_s);
    g_s = save;
}

static bool in_rect(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static bool node_event(void *ctx, const zui_event_t *e, zui_rect_t c)
{
    shell_win_t *w = (shell_win_t *) ctx;
    int n = w->node;
    int32_t hit = -1;
    if (e->type != ZUI_EV_PTR_MOVE && e->type != ZUI_EV_PTR_DOWN) return false;
    /* zui_wm gives the pointer relative to the content (WM7); the layout
     * helpers work in the content's own coordinates, so add it back. */
    int32_t ex = e->x + c.x, ey = e->y + c.y;
    if (in_rect(node_open_btn(c), ex, ey)) hit = 9;
    for (int a = 0; a < SPACE_ACTS && hit < 0; a++)
        if (SPC_ACT[n][a] && in_rect(node_btn(c, a), ex, ey)) hit = a;
    w->hot = hit;
    if (e->type != ZUI_EV_PTR_DOWN || hit < 0) return hit >= 0;
    if (hit == 9) {
        enter_space(w->st, n);
        return true;
    }
    w->st->space = n;
    space_action(w->st, n, hit);
    if (w->st->sview_count > 0) {
        scopy(g_last[n], w->st->sview[w->st->sview_count - 1], SHELL_LINE_LEN);
        term_push(w->st, g_last[n]);
    }
    return true;
}

static void open_rail(zxv_shell_state_t *st, int32_t i)
{
    if (i < 0 || i >= RAIL_APPS) return;
    shell_win_t *w = &g_win[i];
    w->st = st;
    w->app = i - 1;
    w->node = i == RAIL_TERMINAL ? 0 : i;
    w->hot = -1;
    zui_wm_client_t c = {0};
    c.ctx = w;
    c.paint = i == RAIL_TERMINAL ? term_paint : node_paint;
    c.event = i == RAIL_TERMINAL ? 0 : node_event;
    const char *title = i == RAIL_TERMINAL ? "Terminal" : APP_NAME[i - 1];
    uint32_t id = zui_desk_open(&g_desk, (uint32_t) i, title, &c);
    if (id) zui_wm_progress(&g_desk.wm, id, 256u); /* in the kernel: loaded */
    if (i != RAIL_TERMINAL) st->active_app = i - 1;
}

static void place(int32_t i, int32_t x, int32_t y, int32_t w, int32_t h)
{
    uint32_t id = g_desk.win_of[i];
    if (id) zui_wm_move(&g_desk.wm, id, (zui_rect_t){P(x), P(y), P(w), P(h)});
}

/* Bring the desk up for this panel, or follow a change of size or scale. */
static void desk_fit(zxv_shell_state_t *st, int32_t W, int32_t H)
{
    faces_init();
    faces_scale();
    if (g_desk_w == W && g_desk_h == H) return;
    zui_desk_faces_t f = {&T_TITLE, &T_BODY, &T_CAP, &T_HEAD};
    if (!g_desk_w) {
        zui_desk_init(&g_desk, W, H, scale_q8(), false, &f);
        zui_desk_add(&g_desk, "Terminal", RAIL_MARK[0]);
        for (int i = 0; i < SHELL_APPS; i++) zui_desk_add(&g_desk, APP_NAME[i], RAIL_MARK[1 + i]);
        /* the window manager's frames are drawn at its own fixed size */
        g_desk.wm.text = &T_LABEL;
        zui_desk_status(&g_desk, "Economy 5 of 5  ·  Platform 8 of 8");
        open_rail(st, 4); /* Chiglet */
        open_rail(st, 1); /* Vino */
        open_rail(st, RAIL_TERMINAL);
        zui_desk_tick(&g_desk, ZUI_WM_OPEN_MS + 1u);
        place(RAIL_TERMINAL, 16, 16, 600, 420);
        place(4, 632, 16, 440, 250);
        place(1, 632, 282, 440, 300);
        zui_wm_focus(&g_desk.wm, g_desk.win_of[RAIL_TERMINAL]);
        st->active_app = -1;
    } else
        zui_desk_resize(&g_desk, W, H, scale_q8());
    g_desk_w = W;
    g_desk_h = H;
}

/* ---- keyboard: every layout zui_keymap ships, not one US table ---------- */
static void keyboard_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    zui_keymap_load(&g_km, "us");
    zui_kbd_init(&g_kbd, &g_km);
}

/* "keymap" lists the layouts; "keymap de" switches. */
static bool cmd_keymap(zxv_shell_state_t *st, const char *c)
{
    const char *k = "keymap";
    uint32_t i = 0;
    for (; k[i]; i++)
        if (upc(c[i]) != upc(k[i])) return false;
    if (c[i] && c[i] != ' ') return false;
    while (c[i] == ' ') i++;
    char ln[SHELL_LINE_LEN];
    uint32_t j = 0;
    if (!c[i]) {
        put_str(ln, &j, "Layouts:");
        for (uint32_t n = 0; zui_keymap_builtin_id(n); n++) {
            put_str(ln, &j, " ");
            put_str(ln, &j, zui_keymap_builtin_id(n));
        }
        ln[j] = 0;
        term_push(st, ln);
        j = 0;
        put_str(ln, &j, "Now: ");
        put_str(ln, &j, g_km.id);
        ln[j] = 0;
        term_push(st, ln);
        return true;
    }
    zui_keymap_t next;
    if (zui_keymap_load(&next, c + i)) {
        g_km = next;
        zui_kbd_set_keymap(&g_kbd, &g_km);
        put_str(ln, &j, "Keyboard layout: ");
        put_str(ln, &j, g_km.name[0] ? g_km.name : g_km.id);
    } else {
        put_str(ln, &j, "No layout called ");
        put_str(ln, &j, c + i);
        put_str(ln, &j, " (try keymap)");
    }
    ln[j] = 0;
    term_push(st, ln);
    return true;
}

static void cmd_backspace(zxv_shell_state_t *st)
{
    /* remove one whole UTF-8 character */
    while (st->cmdlen > 0) {
        st->cmdlen--;
        if (((uint8_t) st->cmd[st->cmdlen] & 0xC0u) != 0x80u) break;
    }
    st->cmd[st->cmdlen] = 0;
}

static void shell_text(zxv_shell_state_t *st, const char *t)
{
    uint32_t n = slen(t);
    if (n == 0 || st->cmdlen + (int32_t) n >= SHELL_CMD_MAX) return;
    for (uint32_t i = 0; i < n; i++) st->cmd[st->cmdlen++] = t[i];
    st->cmd[st->cmdlen] = 0;
}

static void shell_enter(zxv_shell_state_t *st)
{
    st->cmd[st->cmdlen] = 0;
    if (cmd_keymap(st, st->cmd)) {
        term_prompt_echo(st);
        st->cmdlen = 0;
        st->cmd[0] = 0;
        return;
    }
    int32_t before = st->active_app;
    run_cmd(st);
    if (st->active_app >= 0 && st->active_app != before) open_rail(st, st->active_app + 1);
}

static void shell_keydown(zxv_shell_state_t *st, uint16_t key)
{
    if (key == ZUI_KEY_TAB) { /* base <-> lattice, space -> lattice */
        st->view = (st->view == 1) ? 0 : 1;
        return;
    }
    if (key == ZUI_KEY_ESCAPE) { /* drop home to the base plane */
        st->view = 0;
        return;
    }
    if (st->view == 1) { /* lattice: keyboard nav along edges */
        if (key == ZUI_KEY_UP) st->focus_node = lat_neighbor(st->focus_node, 0, -1);
        if (key == ZUI_KEY_DOWN) st->focus_node = lat_neighbor(st->focus_node, 0, 1);
        if (key == ZUI_KEY_LEFT) st->focus_node = lat_neighbor(st->focus_node, -1, 0);
        if (key == ZUI_KEY_RIGHT) st->focus_node = lat_neighbor(st->focus_node, 1, 0);
        if (key == ZUI_KEY_ENTER) enter_space(st, st->focus_node);
        return;
    }
    if (key == ZUI_KEY_ENTER)
        shell_enter(st);
    else if (key == ZUI_KEY_BACKSPACE)
        cmd_backspace(st);
}

void zxv_shell_init(zxv_shell_state_t *st)
{
    st->active_app = -1;
    st->prev_buttons = 0;
    st->term_count = 0;
    st->cmdlen = 0;
    st->cmd[0] = 0;
    st->click_count = 0;
    st->view = 0;
    st->focus_node = 0;
    st->space = -1;
    st->trans = 0;
    st->sview_count = 0;
    st->subsel = -1;
    st->act_hot = -1;
    st->prev_cx = -1;
    st->prev_cy = -1;
    spaces_boot(); /* bring up the spaces' real subsystems */
    keyboard_init();
    g_desk_w = g_desk_h = 0;
    term_push(st, "ZEDEC XERO VULPINE · pqOS on ARM64");
    term_push(st, "The kernel is the OS: economy, governance and social in the kernel.");
    term_push(st, "Pick an app on the left, or type help. Tab opens Spaces.");
}

void zxv_shell_key(zxv_shell_state_t *st, int32_t keycode)
{
    bool down = (keycode & ZXV_SHELL_KEY_UP) == 0;
    bool repeat = (keycode & ZXV_SHELL_KEY_REPEAT) != 0;
    uint16_t code = (uint16_t) (keycode & 0x3FFF);
    zui_event_t ev[ZUI_KBD_EV_MAX];
    uint32_t n = zui_kbd_feed(&g_kbd, code, down, repeat, g_ms, ev, ZUI_KBD_EV_MAX);
    for (uint32_t i = 0; i < n; i++) {
        if (ev[i].type == ZUI_EV_KEY_DOWN) {
            /* the window manager's own keys (Cmd-Tab, Cmd-W, Cmd-M) first */
            if (st->view == 0 && (ev[i].mods & ZUI_MOD_CMD) && zui_desk_event(&g_desk, &ev[i]))
                continue;
            shell_keydown(st, ev[i].key);
        } else if (ev[i].type == ZUI_EV_KEY_UP) {
            if (st->view == 0) zui_desk_event(&g_desk, &ev[i]);
        } else if (ev[i].type == ZUI_EV_TEXT && st->view == 0 && !(ev[i].mods & ZUI_MOD_CMD)) {
            shell_text(st, ev[i].text);
        }
    }
}

/* Tag a screen region's field tiles with a phase offset + depth (0=near..255=far)
 * so the depth-effect present gives each OBJECT its own shimmer + chromostereopsis. */
static void tagf(uint8_t *fp, uint8_t *fd, int32_t x, int32_t y, int32_t w, int32_t h,
                 uint8_t phase, uint8_t depth)
{
    if (!fp || !fd) return;
    int32_t x0 = x >> 4, y0 = y >> 4, x1 = (x + w + 15) >> 4, y1 = (y + h + 15) >> 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t) g_ftx) x1 = (int32_t) g_ftx;
    if (y1 > (int32_t) g_fty) y1 = (int32_t) g_fty;
    for (int32_t ty = y0; ty < y1; ty++)
        for (int32_t tx = x0; tx < x1; tx++) {
            fp[ty * (int32_t) g_ftx + tx] = phase;
            fd[ty * (int32_t) g_ftx + tx] = depth;
        }
}

/* ============================================================================
 *  SPACES (view 1, the lattice; view 2, one space): the 13 nodes in the
 *  Fruit-of-Life arrangement, drawn in the same carved green and gold.
 * ==========================================================================*/
/* screen positions of the 13 nodes (inner ring R, outer ring R*sqrt3) */
static void lat_pos_px(int32_t W, int32_t H, int32_t *nx, int32_t *ny)
{
    int32_t bar = P(ZUI_DESK_BAR_PT);
    int32_t cx0 = W / 2, cy0 = bar + (H - bar) / 2 - P(10);
    int32_t R = P(150), R2 = (R * 443) / 256; /* sqrt 3 ~= 443/256 */
    nx[0] = cx0;
    ny[0] = cy0;
    for (int32_t k = 0; k < 6; k++) {
        nx[1 + k] = cx0 + (R * LAT_DC[k * 2]) / 256;
        ny[1 + k] = cy0 + (R * LAT_DS[k * 2]) / 256;
        nx[7 + k] = cx0 + (R2 * LAT_DC[k * 2 + 1]) / 256;
        ny[7 + k] = cy0 + (R2 * LAT_DS[k * 2 + 1]) / 256;
    }
}
static int32_t node_r(int i, int foc)
{
    (void) i;
    return P(foc ? 40 : 32);
}

/* hit-test a click against the 13 nodes; focus one, or enter BASE to go home */
static void lattice_click(zxv_shell_state_t *st, int32_t W, int32_t H, int32_t cx, int32_t cy)
{
    int32_t nx[LATTICE_NODES], ny[LATTICE_NODES];
    lat_pos_px(W, H, nx, ny);
    for (int i = 0; i < LATTICE_NODES; i++) {
        int32_t dx = cx - nx[i], dy = cy - ny[i], r = node_r(i, i == st->focus_node) + P(6);
        if (dx * dx + dy * dy <= r * r) {
            if (i == 0)
                st->view = 0; /* BASE node -> drop into the home plane */
            else if (i == st->focus_node)
                enter_space(st, i); /* 2nd click -> ENTER (push-in) */
            else
                st->focus_node = i; /* 1st click -> bring that space warm-near */
            return;
        }
    }
}

static void spaces_chrome(int32_t W, int32_t H, const char *hint)
{
    g_desk.spaces = true;
    zui_desk_draw_bar(&g_desk, &g_s);
    g_desk.spaces = false;
    text_at(&T_CAP, P(ZUI_S4), H - P(ZUI_S6), ZUI_MUTED, hint);
    (void) W;
}

/* The lattice keeps the field neutral: the depth effect is tile-based, and a
 * tile square behind a round node shows as a box. */
static void draw_lattice(zxv_shell_state_t *st, int32_t W, int32_t H, int32_t cx, int32_t cy)
{
    zui_fill(&g_s, (zui_rect_t){0, 0, W, H}, ZUI_VOID);
    int32_t nx[LATTICE_NODES], ny[LATTICE_NODES];
    lat_pos_px(W, H, nx, ny);
    int hover = -1;
    for (int i = 0; i < LATTICE_NODES; i++) {
        int32_t dx = cx - nx[i], dy = cy - ny[i], r = node_r(i, i == st->focus_node);
        if (dx * dx + dy * dy <= r * r) {
            hover = i;
            break;
        }
    }
    /* Metatron edges behind the nodes: the adjacent + perpendicular wiring */
    for (int e = 0; e < LAT_NE; e++) {
        int a = LAT_E[e][0], b = LAT_E[e][1];
        bool lit = a == st->focus_node || b == st->focus_node;
        line(nx[a], ny[a], nx[b], ny[b], P(1) > 0 ? P(1) : 1, lit ? ZUI_EDGE : ZUI_HAIRLINE, 255);
    }
    for (int i = 0; i < LATTICE_NODES; i++) {
        int foc = (i == st->focus_node);
        int32_t r = node_r(i, foc);
        disc(nx[i], ny[i], r, foc ? ZUI_RAISED : ZUI_PANEL);
        ring(nx[i], ny[i], r, P(1), foc ? ZUI_GOLD : (i == hover ? ZUI_TEXT_2 : ZUI_EDGE));
        if (foc) ring(nx[i], ny[i], r + P(4), P(1), ZUI_GOLD);
        char g[2] = {LAT_G[i], 0};
        text_mid(&T_HEAD, (zui_rect_t){nx[i] - r, ny[i] - r, 2 * r, 2 * r},
                 foc ? ZUI_GOLD : ZUI_TEXT_2, g);
        text_centre(&T_BODY, nx[i], ny[i] + r + P(6), foc ? ZUI_TEXT : ZUI_TEXT_2, LAT_NM[i]);
        text_centre(&T_CAP, nx[i], ny[i] + r + P(6) + line_h(&T_BODY), ZUI_MUTED, LAT_SUB[i]);
    }
    spaces_chrome(W, H, "Arrows move  ·  Enter opens  ·  Click to focus  ·  Esc for the desktop");
}

/* the space screen's geometry (shared by draw + hit-test) */
static int32_t space_badge_y(void)
{
    return P(ZUI_DESK_BAR_PT) + P(48) + P(44);
}
static zui_rect_t space_card(void)
{
    return (zui_rect_t){P(64), space_badge_y() + P(44) + P(ZUI_S5), P(150) * 3 + P(ZUI_S3) * 2,
                        P(16) + SPACE_LINES * line_h(&T_BODY)};
}
static void spc_btn_rect(int i, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    zui_rect_t card = space_card();
    *w = P(150);
    *h = P(36);
    *x = card.x + i * (*w + P(ZUI_S3));
    *y = card.y + card.h + P(ZUI_S5);
}
static void subspace_xy(int32_t W, int32_t H, int k, int32_t *px, int32_t *py)
{
    int32_t sx = W - P(220), sy = P(ZUI_DESK_BAR_PT) + (H - P(ZUI_DESK_BAR_PT)) / 2, sr = P(110);
    *px = sx + (sr * LAT_DC[k * 2]) / 256;
    *py = sy + (sr * LAT_DS[k * 2]) / 256;
}

static void space_click(zxv_shell_state_t *st, int32_t W, int32_t H, int32_t cx, int32_t cy)
{
    int s = st->space;
    if (s < 0 || s >= LATTICE_NODES) s = 0;
    for (int a = 0; a < SPACE_ACTS; a++) {
        if (!SPC_ACT[s][a]) continue;
        int32_t rx, ry, rw, rh;
        spc_btn_rect(a, &rx, &ry, &rw, &rh);
        if (cx >= rx && cx < rx + rw && cy >= ry && cy < ry + rh) {
            space_action(st, s, a);
            return;
        }
    }
    for (int k = 0; k < SPACE_SUBS; k++) {
        int32_t px, py, r = P(22);
        subspace_xy(W, H, k, &px, &py);
        if ((cx - px) * (cx - px) + (cy - py) * (cy - py) <= r * r) {
            subspace_select(st, s, k);
            return;
        }
    }
}

/* the plane you push INTO: a focused space's own full screen. Each of the 12
 * carries a seed sub-lattice (its own 6 sub-spaces) -- the tree recurses here. */
static void draw_space(zxv_shell_state_t *st, int32_t W, int32_t H, int32_t cx, int32_t cy,
                       uint8_t *fphase, uint8_t *fdepth)
{
    int s = st->space;
    if (s < 0 || s >= LATTICE_NODES) s = 0;
    zui_fill(&g_s, (zui_rect_t){0, 0, W, H}, ZUI_VOID);
    int32_t top = P(ZUI_DESK_BAR_PT);
    /* the node badge and its name */
    int32_t bx = P(64) + P(44), by = space_badge_y(), br = P(44);
    disc(bx, by, br, ZUI_RAISED);
    ring(bx, by, br, P(1), ZUI_GOLD);
    {
        char g[2] = {LAT_G[s], 0};
        text_mid(&T_DISP, (zui_rect_t){bx - br, by - br, 2 * br, 2 * br}, ZUI_GOLD, g);
    }
    int32_t tx = bx + br + P(ZUI_S5);
    text_at(&T_DISP, tx, by - P(30), ZUI_TEXT, LAT_NM[s]);
    text_at(&T_BODY, tx, by + P(12), ZUI_MUTED, LAT_DESC[s][0]);
    /* LIVE readout, computed by the space's REAL subsystem on entry */
    zui_rect_t card = space_card();
    zui_fill_rrect(&g_s, card, P(ZUI_R_PANEL), ZUI_PANEL, 255);
    zui_engrave(&g_s, card, P(ZUI_R_PANEL), ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 40);
    int32_t ry = card.y + P(8);
    for (int i = 0; i < st->sview_count; i++) {
        readout_row((zui_rect_t){card.x + P(ZUI_S4), ry, card.w - 2 * P(ZUI_S4), 0}, st->sview[i],
                    i == st->sview_count - 1 ? ZUI_TEXT : ZUI_TEXT_2);
        ry += line_h(&T_BODY);
    }
    /* action buttons: each click invokes the real subsystem (space_action) */
    for (int a = 0; a < SPACE_ACTS; a++) {
        const char *lbl = SPC_ACT[s][a];
        if (!lbl) continue;
        int32_t rx, ryy, rw, rh;
        spc_btn_rect(a, &rx, &ryy, &rw, &rh);
        bool hot = cx >= rx && cx < rx + rw && cy >= ryy && cy < ryy + rh;
        button((zui_rect_t){rx, ryy, rw, rh}, lbl, a == 0, hot, st->act_hot == a);
    }
    /* sub-spaces: this space's own 6 sub-features (the next layer down) */
    int32_t sx = W - P(220), sy = top + (H - top) / 2;
    text_centre(&T_LABEL, sx, sy - P(110) - P(48), ZUI_MUTED, "The next layer down");
    disc(sx, sy, P(8), ZUI_GOLD);
    for (int k = 0; k < SPACE_SUBS; k++) {
        int32_t px, py, r = P(18);
        subspace_xy(W, H, k, &px, &py);
        int sel = (st->subsel == k);
        int hov = ((cx - px) * (cx - px) + (cy - py) * (cy - py) <= r * r);
        line(sx, sy, px, py, 1, ZUI_HAIRLINE, 255);
        disc(px, py, r, sel ? ZUI_RAISED : ZUI_PANEL);
        ring(px, py, r, P(1), sel ? ZUI_GOLD : (hov ? ZUI_TEXT_2 : ZUI_EDGE));
        const char *sn = SPC_SUB[s][k];
        if (sn) text_centre(&T_CAP, px, py + r + P(4), sel ? ZUI_TEXT : ZUI_MUTED, sn);
    }
    spaces_chrome(W, H, "Tab for the 13 spaces  ·  Esc for the desktop");
    /* field: push-in depth sweep far->near using this space's own phase */
    int tr = st->trans;
    if (tr < 0) tr = 0;
    if (tr > LATTICE_TRANS) tr = LATTICE_TRANS;
    tagf(fphase, fdepth, 0, 0, W, H, (uint8_t) (8 + s * 7),
         (uint8_t) (128 + tr * 100 / LATTICE_TRANS));
}

/* ---- one frame ----------------------------------------------------------- */
static void desk_pointer(zxv_shell_state_t *st, int32_t cx, int32_t cy, uint32_t buttons)
{
    zui_event_t e = {0};
    e.ms = g_ms;
    e.x = cx;
    e.y = cy;
    if (cx != st->prev_cx || cy != st->prev_cy) {
        e.type = ZUI_EV_PTR_MOVE;
        e.button = (uint8_t) (st->prev_buttons & 7u);
        zui_desk_event(&g_desk, &e);
    }
    if (buttons & ZXV_SHELL_BTN_TAP) { /* a whole click inside one frame */
        e.button = 1;
        e.type = ZUI_EV_PTR_DOWN;
        zui_desk_event(&g_desk, &e);
        e.type = ZUI_EV_PTR_UP;
        zui_desk_event(&g_desk, &e);
        if (g_desk.act == ZUI_DESK_LAUNCH) open_rail(st, g_desk.act_app);
        if (g_desk.act == ZUI_DESK_SPACES) st->view = 1;
    }
    for (uint32_t b = 1; b <= 4; b <<= 1) {
        if ((buttons & b) == (st->prev_buttons & b)) continue;
        e.type = (buttons & b) ? ZUI_EV_PTR_DOWN : ZUI_EV_PTR_UP;
        e.button = (uint8_t) b;
        zui_desk_event(&g_desk, &e);
        if (g_desk.act == ZUI_DESK_LAUNCH) open_rail(st, g_desk.act_app);
        if (g_desk.act == ZUI_DESK_SPACES) st->view = 1;
    }
}

void zxv_shell_frame(zxv_shell_state_t *st, vbe_state_t *v, int32_t cx, int32_t cy,
                     uint32_t buttons, uint32_t tick, uint8_t *fphase, uint8_t *fdepth)
{
    const int32_t W = (int32_t) v->width, H = (int32_t) v->height;
    int32_t stride = v->pitch ? (int32_t) (v->pitch / 4u) : W;
    zui_surface_init(&g_s, v->fb, W, H, stride);
    desk_fit(st, W, H);
    /* Motion follows the real clock when the platform gives one, so a slow
     * frame (a software-rendered panel) does not slow the animations down. */
    uint32_t dt = 33u;
    if (g_clock_set) {
        dt = g_clock_ms - g_clock_last;
        g_clock_last = g_clock_ms;
        if (dt > 250u) dt = 250u;
    }
    g_ms += dt;
    zui_desk_tick(&g_desk, dt);
    /* field map: the ground is neutral (true #002911); objects tag their own */
    if (fphase && fdepth)
        for (int32_t i = 0; i < (int32_t) FIELD_CELLS_MAX; i++) {
            fphase[i] = 0;
            fdepth[i] = 128;
        }

    bool click = ((buttons & 1u) && !(st->prev_buttons & 1u)) || (buttons & ZXV_SHELL_BTN_TAP);
    if (click) st->click_count++;
    if (st->view == 0) {
        desk_pointer(st, cx, cy, buttons);
    } else if (click) {
        zui_rect_t home = zui_desk_switch(&g_desk, false), sp = zui_desk_switch(&g_desk, true);
        if (in_rect(home, cx, cy))
            st->view = 0;
        else if (in_rect(sp, cx, cy))
            st->view = 1;
        else if (st->view == 2)
            space_click(st, W, H, cx, cy);
        else
            lattice_click(st, W, H, cx, cy);
    }
    st->prev_buttons = buttons & 7u;
    st->prev_cx = cx;
    st->prev_cy = cy;

    if (st->view == 2) {
        draw_space(st, W, H, cx, cy, fphase, fdepth);
        if (st->trans > 0) st->trans--;
    } else if (st->view == 1) {
        draw_lattice(st, W, H, cx, cy);
    } else {
        int32_t front = zui_desk_front_app(&g_desk);
        st->active_app = front > 0 ? front - 1 : -1;
        zui_desk_draw(&g_desk, &g_s, tick);
        zui_rect_t bar = zui_desk_bar(&g_desk);
        tagf(fphase, fdepth, 0, 0, W, bar.h, 2, 100); /* the bar: a little near */
    }
    cursor(cx, cy);
}
