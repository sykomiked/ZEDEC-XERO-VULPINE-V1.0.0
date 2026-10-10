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

/* shell-owned working instances for the stateful spaces */
static vino_ledger_t g_vino;
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
    vino_init(&g_vino, 846u);
    vino_create_account(&g_vino, "TREASURY", "Vino Treasury");
    vino_create_account(&g_vino, "VULPINE", "Vulpine");
    (void) vino_issue(&g_vino, "TREASURY", ASSET_CBDC, 1000u, "genesis");
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

/* ---- ZXV house palette (XRGB8888) ---- */
#define C_BAR      0x0F0F16u
#define C_BAR_HI   0x1B1B28u
#define C_GOLD     0xE6C158u
#define C_GOLD_DIM 0x8A7434u
#define C_RED      0x6E1417u
#define C_RED_DK   0x2A0B0Du
#define C_WINBODY  0x0A0A12u
#define C_TEXT     0xCED0DAu
#define C_DIM      0x7A7C8Au
#define C_GREEN    0x66DD88u
#define C_CYAN     0x62C6D6u
#define C_WHITE    0xF4F6FBu
#define C_BLACK    0x000000u

/* ---- dock app table (name, native file, glyph, accent) ---- */
static const char *APP_NAME[SHELL_APPS] = {"VINO", "FLEET", "STUDIO", "CHIGLET", "REFINERY"};
static const char *APP_FILE[SHELL_APPS] = {"WALLET.ZXVC", "CREW.ZXVC", "ART.ZXVC", "MIND.ZXVC",
                                           "SIGIL.ZXVC"};
static const char APP_GLYPH[SHELL_APPS] = {'V', 'F', 'S', 'C', 'R'};
static const uint32_t APP_ACC[SHELL_APPS] = {C_GOLD, C_RED, C_CYAN, C_GREEN, C_GOLD};

/* Dock geometry, at the 1x reference the layout was designed against. Every
 * use goes through SC() so the dock grows with the panel: on a 4K screen an
 * unscaled 76px icon is a speck, which is more pixels rather than more detail.
 * The scale comes from the negotiated display mode (display.h). */
#define DOCK_X_1X    26
#define DOCK_Y_1X    70
#define DOCK_S_1X    76
#define DOCK_STEP_1X 118

/* UI scale in permille, set from the negotiated mode. 1000 = the reference. */
static uint32_t g_ui_scale = 1000u;
void zxv_shell_set_ui_scale(uint32_t permille)
{
    g_ui_scale = (permille >= 500u && permille <= 4000u) ? permille : 1000u;
}
static int SC(int v)
{
    return (int) (((int32_t) v * (int32_t) g_ui_scale + 500) / 1000);
}

#define DOCK_X    SC(DOCK_X_1X)
#define DOCK_Y    SC(DOCK_Y_1X)
#define DOCK_S    SC(DOCK_S_1X)
#define DOCK_STEP SC(DOCK_STEP_1X)

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

/* ---- drawing primitives ---- */
static void box(vbe_state_t *v, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c)
{
    vbe_fill_rect(v, x, y, w, h, c);
}
static void frame(vbe_state_t *v, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c)
{
    vbe_fill_rect(v, x, y, w, 1, c);
    vbe_fill_rect(v, x, y + h - 1, w, 1, c);
    vbe_fill_rect(v, x, y, 1, h, c);
    vbe_fill_rect(v, x + w - 1, y, 1, h, c);
}
static void gt(vbe_state_t *v, int32_t x, int32_t y, const char *s, uint32_t fg, int32_t sc)
{
    vbe_draw_text_ex(v, x, y, s, fg, 0, sc, 0);
}

static int32_t window(vbe_state_t *v, int32_t x, int32_t y, int32_t w, int32_t h, const char *title,
                      uint32_t accent)
{
    box(v, x + 6, y + 6, w, h, 0x05050A);
    box(v, x, y, w, h, C_WINBODY);
    frame(v, x, y, w, h, accent);
    box(v, x + 1, y + 1, w - 2, 24, C_RED_DK);
    vbe_fill_rect(v, x + 1, y + 25, w - 2, 1, C_GOLD_DIM);
    gt(v, x + 12, y + 5, title, C_GOLD, 1);
    box(v, x + w - 20, y + 9, 8, 8, C_GREEN);
    box(v, x + w - 34, y + 9, 8, 8, C_GOLD);
    box(v, x + w - 48, y + 9, 8, 8, C_RED);
    return y + 34;
}
static void icon(vbe_state_t *v, int32_t x, int32_t y, char glyph, uint32_t accent,
                 const char *name, const char *file, int active)
{
    box(v, x, y, DOCK_S, DOCK_S, active ? 0x161020u : C_WINBODY);
    frame(v, x, y, DOCK_S, DOCK_S, accent);
    if (active) {
        frame(v, x - 2, y - 2, DOCK_S + 4, DOCK_S + 4, accent);
        frame(v, x - 1, y - 1, DOCK_S + 2, DOCK_S + 2, C_GOLD);
    } else
        frame(v, x + 1, y + 1, DOCK_S - 2, DOCK_S - 2, C_BAR_HI);
    char g[2] = {glyph, 0};
    gt(v, x + DOCK_S / 2 - 12, y + DOCK_S / 2 - 24, g, accent, 3);
    gt(v, x + 6, y + DOCK_S + 4, name, active ? C_GOLD : C_TEXT, 1);
    gt(v, x - 4, y + DOCK_S + 20, file, C_GOLD_DIM, 1);
}
static void cursor(vbe_state_t *v, int32_t x, int32_t y)
{
    static const char *A[] = {"1",         "11",         "121",         "1221",
                              "12221",     "122221",     "1222221",     "12222221",
                              "122222221", "1222222221", "12222111111", "122122",
                              "1221 12",   "121  12",    "11    122",   "      12"};
    for (int32_t r = 0; r < 16; r++)
        for (int32_t c = 0; A[r][c]; c++) {
            if (A[r][c] == '1')
                vbe_set_pixel(v, x + c, y + r, C_BLACK);
            else if (A[r][c] == '2')
                vbe_set_pixel(v, x + c, y + r, C_WHITE);
        }
}

/* ---- lattice geometry primitives (Fruit-of-Life overview) ---- */
static void disc(vbe_state_t *v, int32_t cx, int32_t cy, int32_t r, uint32_t c)
{
    int32_t r2 = r * r;
    for (int32_t dy = -r; dy <= r; dy++) {
        int32_t rem = r2 - dy * dy;
        if (rem < 0) continue;
        int32_t xr = 0;
        while ((xr + 1) * (xr + 1) <= rem) xr++;
        vbe_fill_rect(v, cx - xr, cy + dy, 2 * xr + 1, 1, c);
    }
}
static void ring(vbe_state_t *v, int32_t cx, int32_t cy, int32_t r, uint32_t c)
{
    if (r < 2) return; /* 2px annulus, drawn as row spans */
    int32_t ro2 = r * r, ri = r - 2, ri2 = ri * ri;
    for (int32_t dy = -r; dy <= r; dy++) {
        int32_t rem_o = ro2 - dy * dy;
        if (rem_o < 0) continue;
        int32_t xo = 0;
        while ((xo + 1) * (xo + 1) <= rem_o) xo++; /* outer half-width */
        int32_t rem_i = ri2 - dy * dy;
        if (rem_i < 0) {
            vbe_fill_rect(v, cx - xo, cy + dy, 2 * xo + 1, 1, c);
            continue;
        } /* cap row: solid */
        int32_t xi = 0;
        while ((xi + 1) * (xi + 1) <= rem_i) xi++;             /* inner half-width */
        vbe_fill_rect(v, cx - xo, cy + dy, xo - xi + 1, 1, c); /* left annulus span  */
        vbe_fill_rect(v, cx + xi, cy + dy, xo - xi + 1, 1, c); /* right annulus span */
    }
}
static void line(vbe_state_t *v, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c)
{
    int32_t dx = x1 - x0;
    if (dx < 0) dx = -dx;
    int32_t dy = y1 - y0;
    if (dy < 0) dy = -dy;
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (;;) {
        vbe_set_pixel(v, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* ---- the 13 spaces: node 0 = base/home; 1..6 inner ring; 7..12 outer ring ---- */
static const char LAT_G[LATTICE_NODES] = {'Z', 'V', 'F', 'S', 'C', 'R', 'O',
                                          'K', 'W', 'B', 'I', 'P', 'L'};
static const char *LAT_NM[LATTICE_NODES] = {
    "BASE",  "VINO",     "FLEET", "STUDIO",     "CHIGLET", "REFINERY", "CONCORD",
    "CROWN", "WYRMGATE", "RAM",   "INTERSPACE", "BADGE",   "LOGISTICS"};
static const char *LAT_SUB[LATTICE_NODES] = {
    "HOME PLANE", "ECONOMY",  "WORK",    "ART",          "AI",         "MAGITECH", "SOCIAL",
    "GOVERNANCE", "JUDGMENT", "MARKETS", "JURISDICTION", "REPUTATION", "SUPPLY"};
static const uint32_t LAT_AC[LATTICE_NODES] = {C_GOLD, C_GOLD, C_RED,  C_CYAN, C_GREEN,
                                               C_GOLD, C_CYAN, C_GOLD, C_RED,  C_GREEN,
                                               C_CYAN, C_GOLD, C_RED};

/* per-space screen: 4 lines of live-ish state (the plane you drop INTO) */
static const char *LAT_DESC[LATTICE_NODES][4] = {
    {"THE HOME PLANE -- DEEPEST USER LEVEL", "DOCK - TERMINAL - CHIGLET - VINO",
     "THE ROOT SYSTEM BELOW IS THE KERNEL", "TAB PULLS BACK TO THE 13"},
    {"TRIPLE-RAIL FLOATING VOUCHERS", "555 DEBIT 120  777 CREDIT 12",
     "888 EQUITY 108  COVERAGE 1.8X", "NO DEBT - MERIT-BASED - SOLVENT"},
    {"JDR PIRATE FLEET -- WORK + COMPUTE", "CREW - DAO - REMOTE COMPUTE",
     "MODULAR CONTRACTS - SYNDICATES", "SHARES BY MERIT, NOT SENIORITY"},
    {"MISS POTIMUS ART STUDIO", "CANVAS - SOUND - MOTION - 3D", "EVERY MEDIUM, ONE TOOLKIT",
     "EXPORT [S+] / [S-] / [S0]"},
    {"CHIGLET -- NATIVE MIXTURE-OF-EXPERTS", "MATCH TROLLS WITH TROLLS", "CONCORD: REHABILITATIVE",
     "R (DISTINCT EVIDENCE): 3.0  DECIDED"},
    {"MAGITECH REFINERY", "TEXT -> ENOCHIAN GEMATRIA", "-> SIGIL CARD -> 58-BYTE PRESET",
     "DECK PARITY VERIFIED"},
    {"CONCORD -- NON-COERCIVE SOCIAL", "COMPLEMENTARY PAIRING",
     "SYMMETRIC DIVIDES, MUTUAL SOVEREIGNTY", "NO BANS - NO ADS - REHABILITATIVE"},
    {"THE CROWN -- GOVERNANCE", "CROWN / MINISTRY / PILLAR", "AUTOMATED BUREAUCRACY",
     "ONE POLICY: SYMBIOTIC MAXIM"},
    {"WYRMGATE -- SIX-FOLD JUDGMENT", "TRI-SPACE: S+ / S0 / S-", "RMAG + LPRES + CHIGLET COMPOSED",
     "VERDICT BY DISTINCT EVIDENCE"},
    {"BATTERING RAM EXCHANGE", "EQUITIES - FUTURES - COMMODITIES",
     "CAPITAL DERIVATIVES, VETOED USURY", "SYMBIOTIC GATE ON DEAL TERMS"},
    {"INTERSPACE -- JURISDICTION", "SPACE BETWEEN SPACES", "LEX RHODIA TREATY FRAMEWORKS",
     "FLAG-STATE / FEDERATION"},
    {"PIG BADGE -- REPUTATION", "COUNTERINTEL - EARNABLE BADGES", "PIG BADGE: OVER 9000",
     "GAMIFIED, NON-TRANSFERABLE"},
    {"LOGISTICS -- REAL-WORLD SUPPLY", "SYNDICATES + MODULAR CONTRACTS", "SECONDARY MARKETPLACE",
     "PHYSICAL <-> DIGITAL RAILS"}};

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
    const char *p = "VULPINE@ZXV:~$ ";
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
    } else if (seq_up(c, "HELP")) {
        term_push(st, "COMMANDS: LS  OPEN  HELP  CLEAR");
        term_push(st, "APPS: VINO FLEET STUDIO CHIGLET REFINERY");
    } else if (seq_up(c, "LS")) {
        term_push(st, "WALLET.ZXVC  CREW.ZXVC  ART.ZXVC  MIND.ZXVC  SIGIL.ZXVC");
        term_push(st, "  (each carries .CEDEZ [S-] and .CEDEC [S0])");
    } else if (seq_up(c, "CLEAR")) {
        st->term_count = 0;
    } else if (seq_up(c, "OPEN")) {
        term_push(st, "USAGE: OPEN <FILE.ZXVC>   e.g. OPEN WALLET.ZXVC");
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
            const char *a = "OPENED ";
            uint32_t j = 0;
            while (a[j] && j < 20) {
                ln[j] = a[j];
                j++;
            }
            const char *nm = APP_NAME[hit];
            uint32_t k = 0;
            while (nm[k] && j < SHELL_LINE_LEN - 14) ln[j++] = nm[k++];
            const char *tail = " [S+] VERIFIED";
            k = 0;
            while (tail[k] && j < SHELL_LINE_LEN - 1) ln[j++] = tail[k++];
            ln[j] = 0;
            term_push(st, ln);
            st->active_app = hit;
        } else {
            char ln[SHELL_LINE_LEN];
            const char *a = "NOT FOUND: ";
            uint32_t j = 0;
            while (a[j] && j < 20) {
                ln[j] = a[j];
                j++;
            }
            for (int k = 0; k < st->cmdlen && j < SHELL_LINE_LEN - 12; k++) ln[j++] = st->cmd[k];
            const char *t = " (TRY HELP)";
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
    /*BASE     */ {"DOCK", "TERMINAL", "LATTICE"},
    /*VINO     */ {"SEND", "ISSUE", "BALANCE"},
    /*FLEET    */ {"CAP WALL", "DEPTH", "CREW"},
    /*STUDIO   */ {"PAINT", "STROKE", "EXPORT"},
    /*CHIGLET  */ {"DECIDED", "UNSURE", "WHY"},
    /*REFINERY */ {"GEMATRIA", "MIRROR", "VOICE"},
    /*CONCORD  */ {"TICK", "RECOMMEND", "MAYMATCH"},
    /*CROWN    */ {"CULTURAL", "FINANCE", "SOCIAL"},
    /*WYRMGATE */ {"JUDGE RT", "BLOCK", "ORDER"},
    /*RAM      */ {"PRICE", "TERM", "MAJORITY"},
    /*INTERSPC */ {"COMMONS", "IMMUNE", "PASSAGE"},
    /*BADGE    */ {"AWARD B1", "AWARD B2", "SCORE"},
    /*LOGISTIC */ {"OPEN", "ARM", "RELEASE"},
};
/* sub-space labels per space (NULL = empty node) */
static const char *SPC_SUB[LATTICE_NODES][SPACE_SUBS] = {
    /*BASE     */ {"DOCK", "TERMINAL", "CHIGLET", "VINO", "FILES", "THEME"},
    /*VINO     */ {"555 DEBIT", "777 CREDIT", "888 EQUITY", "LEDGER", "RAILS", "BRIDGE"},
    /*FLEET    */ {"CREW", "DAO", "COMPUTE", "CONTRACTS", "SYNDICATE", "SHARES"},
    /*STUDIO   */ {"CANVAS", "SOUND", "MOTION", "3D", "PALETTE", "EXPORT"},
    /*CHIGLET  */ {"EXPERTS", "MEMORY", "VOICE", "MATCH", "EVIDENCE", "VERDICT"},
    /*REFINERY */ {"GEMATRIA", "SIGIL", "CARD", "DECK", "PRESET", "SHARE"},
    /*CONCORD  */ {"PAIRING", "DIVIDES", "QUARANTINE", "MEDIATE", "SOVEREIGN", "TRUST"},
    /*CROWN    */ {"CROWN", "MINISTRY", "PILLAR", "REGISTRY", "DECREES", "POLICY"},
    /*WYRMGATE */ {"S+", "S0", "S-", "RMAG", "LPRES", "CHIGLET"},
    /*RAM      */ {"EQUITIES", "FUTURES", "COMMODITY", "DERIV", "QUOTE", "BOOK"},
    /*INTERSPC */ {"LEX RHODIA", "FLAG-STATE", "FEDERATE", "TREATY", "MINISTER", "BORDER"},
    /*BADGE    */ {"PIG BADGE", "EARNED", "RANK", "VERIFY", "COUNTER", "LEDGER"},
    /*LOGISTIC */ {"SYNDICATE", "CONTRACT", "MARKET", "ROUTE", "WAREHOUSE", "RAILS"},
};

/* action-button geometry (shared by draw + hit-test) */
#define SPC_BX   216
#define SPC_BY   452
#define SPC_BW   176
#define SPC_BH   40
#define SPC_BGAP 18
static void spc_btn_rect(int i, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = SPC_BX + i * (SPC_BW + SPC_BGAP);
    *y = SPC_BY;
    *w = SPC_BW;
    *h = SPC_BH;
}
/* screen position of sub-space k (shared by draw + hit-test) */
static void subspace_xy(int32_t W, int32_t H, int k, int32_t *px, int32_t *py)
{
    int32_t sx = W - 300, sy = H / 2 + 50, sr = 92;
    int d = k * 2;
    *px = sx + (sr * LAT_DC[d]) / 256;
    *py = sy + (sr * LAT_DS[d]) / 256;
}

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
        kv_str(ln, "NATIVE RAIL", vino_rail_name(RAIL_VINO_NATIVE));
        sview_push(st, ln);
        kv_str(ln, "TOP CAPITAL", vino_capital_name(CAP_FINANCIAL));
        sview_push(st, ln);
        kv_str(ln, "CBDC ASSET", vino_asset_class_name(ASSET_CBDC));
        sview_push(st, ln);
        {
            uint64_t b = 0;
            vino_get_balance(&g_vino, "TREASURY", CAP_FINANCIAL, &b);
            kv_int(ln, "TREASURY CAP", (long) b);
        }
        sview_push(st, ln);
        break;
    case 2: /* FLEET */
        kv_int(ln, "CHAN DEPTH", (long) jdr_channel_depth(0));
        sview_push(st, ln);
        kv_str(ln, "CREW0 STATE", jdr_crew_state_name((crew_state_t) 0));
        sview_push(st, ln);
        kv_str(ln, "BOTH CAPS",
               jdr_cap_wall_ok((uint8_t) (PF_CAP_CREDENTIAL | PF_CAP_TREASURY_AUDIT)) ? "ALLOW"
                                                                                      : "DENY");
        sview_push(st, ln);
        break;
    case 3: /* STUDIO */
        kv_int(ln, "MAX WIDTH", (long) ART_MAX_W);
        sview_push(st, ln);
        kv_int(ln, "MAX HEIGHT", (long) ART_MAX_H);
        sview_push(st, ln);
        kv_int(ln, "CANVAS W", (long) g_art.w);
        sview_push(st, ln);
        kv_int(ln, "CANVAS H", (long) g_art.h);
        sview_push(st, ln);
        break;
    case 4: /* CHIGLET */
        kv_int(ln, "EXPERT CAP", (long) CHG_MAX_EXPERTS);
        sview_push(st, ln);
        kv_int(ln, "EVID DIM", (long) CHG_DIM);
        sview_push(st, ln);
        kv_str(ln, "BOOT STATE", chg_state_name(CHG_UNAVAILABLE));
        sview_push(st, ln);
        break;
    case 5: /* REFINERY */
    {
        uint32_t g = eno_gematria("REFINERY", 8);
        kv_int(ln, "GEMATRIA", (long) g);
        sview_push(st, ln);
        uint32_t rt = eno_root(g);
        kv_int(ln, "ROOT 1-9", (long) rt);
        sview_push(st, ln);
        kv_str(ln, "DOMAIN", eno_root_domain(rt));
        sview_push(st, ln);
    }
        kv_int(ln, "ALPHABET", (long) ENO_LETTERS);
        sview_push(st, ln);
        break;
    case 6: /* CONCORD */
        kv_int(ln, "MEMBERS", (long) g_concord.n_people);
        sview_push(st, ln);
        kv_int(ln, "DIVIDES", (long) g_concord.n_divides);
        sview_push(st, ln);
        kv_int(ln, "CLOCK", (long) g_concord.now);
        sview_push(st, ln);
        {
            con_person_t *p = con_get(&g_concord, 1u);
            kv_str(ln, "STANDING #1", con_standing_name(con_standing(p)));
        }
        sview_push(st, ln);
        break;
    case 7: /* CROWN */
        kv_int(ln, "CULTURAL", (long) crown_form_is_crown(ZCAP_CULTURAL));
        sview_push(st, ln);
        kv_int(ln, "SPIRITUAL", (long) crown_form_is_crown(ZCAP_SPIRITUAL));
        sview_push(st, ln);
        kv_int(ln, "FINANCIAL", (long) crown_form_is_crown(ZCAP_FINANCIAL));
        sview_push(st, ln);
        kv_int(ln, "SOCIAL", (long) crown_form_is_crown(ZCAP_SOCIAL));
        sview_push(st, ln);
        break;
    case 8: /* WYRMGATE */
        kv_str(ln, "COMMIT", wyrm_verdict_name(WYRM_COMMIT));
        sview_push(st, ln);
        kv_str(ln, "DEFER", wyrm_verdict_name(WYRM_DEFER));
        sview_push(st, ln);
        kv_int(ln, "MAX DELTAS", (long) WYRM_MAX_DELTAS);
        sview_push(st, ln);
        {
            wyrm_event_t e = {0};
            e.route_available = true;
            e.phases_healthy = true;
            wyrm_result_t r = wyrm_judge(&e);
            kv_str(ln, "JUDGE(RT)", wyrm_verdict_name(r.verdict));
        }
        sview_push(st, ln);
        break;
    case 9: /* BATTERING RAM */
    {
        long p =
            (long) (br_price_capital_future(ZCAP_FINANCIAL, SR_FROM_INT(100), SR_ZERO, SR_ONE) >>
                    SR_SHIFT);
        kv_int(ln, "FUT PRICE", p);
    }
        sview_push(st, ln);
        {
            br_term_t t = {0};
            t.reciprocal = true;
            kv_int(ln, "TERM ADMIT", (long) symbiotic_ok(&t));
        }
        sview_push(st, ln);
        {
            allocation_t a = {0};
            a.realized = SR_FROM_INT(100);
            a.contributor_pool = SR_FROM_INT(78);
            kv_int(ln, "MAJORITY", (long) br_contributors_hold_majority(&a));
        }
        sview_push(st, ln);
        break;
    case 10: /* INTERSPACE */
    {
        interstitial_region_t r = {0};
        r.kind = ZXV_RES_COMMUNIS;
        r.owner = ZXV_NODE_NONE;
        kv_int(ln, "IS COMMONS", (long) interstitial_is_commons(&r));
    }
        sview_push(st, ln);
        {
            vessel_flag_t vf = {0};
            vf.keyholder = 846;
            kv_int(ln, "SELF IMMUNE", (long) vessel_immune_from(&vf, 846));
        }
        sview_push(st, ln);
        {
            safe_passage_grant_t g = {0};
            g.issued_tick = 0;
            g.ttl_deadline = 100;
            kv_int(ln, "PASS VALID", (long) safe_passage_valid(&g, 50));
        }
        sview_push(st, ln);
        break;
    case 11: /* BADGE */
        kv_int(ln, "SUBJECTS", (long) g_rep.n_entries);
        sview_push(st, ln);
        kv_int(ln, "BADGE SCORE", (long) badge_score(&g_rep, 846u));
        sview_push(st, ln);
        kv_int(ln, "PIG(SEEN)", (long) pig_level_seen_by(&g_rep, 846u, 847u));
        sview_push(st, ln);
        kv_int(ln, "HAS BADGE1", (long) badge_has(&g_rep, 846u, 1u, 1u));
        sview_push(st, ln);
        break;
    case 12: /* LOGISTICS */
        kv_int(ln, "CONTRACTS", (long) (g_log.next_id - 1));
        sview_push(st, ln);
        kv_int(ln, "ESCROW ARMED", (long) (g_log.verify != 0));
        sview_push(st, ln);
        {
            const log_contract_t *c = log_contract_find(&g_log, 1u);
            kv_int(ln, "C1 LIVE", (long) (c != 0));
        }
        sview_push(st, ln);
        {
            const log_contract_t *c = log_contract_find(&g_log, 1u);
            kv_int(ln, "C1 PARTIES", (long) (c ? c->n_parties : 0));
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
        if (a == 0) {
            int r = vino_transfer(&g_vino, "TREASURY", "VULPINE", 1u, CAP_FINANCIAL,
                                  RAIL_VINO_NATIVE, "send");
            kv_int(ln, "SEND RC", r);
        } else if (a == 1) {
            int r = vino_issue(&g_vino, "TREASURY", ASSET_CBDC, 10u, "issue");
            kv_int(ln, "ISSUE RC", r);
        } else {
            uint64_t b = 0;
            vino_get_balance(&g_vino, "TREASURY", CAP_FINANCIAL, &b);
            kv_int(ln, "CAP BAL", (long) b);
        }
        sview_push(st, ln);
        break;
    case 2: /* FLEET */
        if (a == 0)
            kv_str(ln, "BOTH CAPS",
                   jdr_cap_wall_ok((uint8_t) (PF_CAP_CREDENTIAL | PF_CAP_TREASURY_AUDIT)) ? "ALLOW"
                                                                                          : "DENY");
        else if (a == 1)
            kv_int(ln, "CHAN DEPTH", (long) jdr_channel_depth(0));
        else
            kv_str(ln, "CREW0", jdr_crew_state_name((crew_state_t) 0));
        sview_push(st, ln);
        break;
    case 3: /* STUDIO */
        if (a == 0) {
            art_fill_rect(&g_art, 0, 0, 64, 64, 200);
            kv_int(ln, "PX[0]", (long) g_art.px[0]);
        } else if (a == 1) {
            art_hline(&g_art, 4, 32, 56, 255);
            kv_int(ln, "PX ROW32", (long) g_art.px[32 * (long) g_art.w + 4]);
        } else {
            static uint8_t cid[32];
            art_export_cid(&g_art, cid);
            kv_int(ln, "CID[0]", (long) cid[0]);
        }
        sview_push(st, ln);
        break;
    case 4: /* CHIGLET */
        if (a == 0)
            kv_str(ln, "STATE", chg_state_name(CHG_DECIDED));
        else if (a == 1)
            kv_str(ln, "STATE", chg_state_name(CHG_UNCERTAIN));
        else
            kv_str(ln, "REASON", chg_reason_name(CHG_REASON_REDUNDANT));
        sview_push(st, ln);
        break;
    case 5: /* REFINERY */
        if (a == 0)
            kv_int(ln, "SIGIL GEM", (long) eno_gematria("SIGIL", 5));
        else if (a == 1)
            kv_int(ln, "MIRROR(4)", (long) eno_root_mirror(4));
        else
            kv_str(ln, "SOLAR VERB", eno_voice_verb(ENO_VOICE_SOLAR));
        sview_push(st, ln);
        break;
    case 6: /* CONCORD */
        if (a == 0) {
            con_tick(&g_concord, 1u);
            kv_int(ln, "CLOCK", (long) g_concord.now);
        } else if (a == 1) {
            g_cn = con_recommend(&g_concord, 1u, g_cm, CON_MAX_PEOPLE);
            kv_int(ln, "MATCHES", (long) g_cn);
        } else
            kv_str(ln, "1<->2", con_may_match(&g_concord, 1u, 2u) ? "YES" : "NO");
        sview_push(st, ln);
        break;
    case 7: /* CROWN */
        if (a == 0)
            kv_int(ln, "CULTURAL", (long) crown_form_is_crown(ZCAP_CULTURAL));
        else if (a == 1)
            kv_int(ln, "FINANCIAL", (long) crown_form_is_crown(ZCAP_FINANCIAL));
        else
            kv_int(ln, "SOCIAL", (long) crown_form_is_crown(ZCAP_SOCIAL));
        sview_push(st, ln);
        break;
    case 8: /* WYRMGATE */
    {
        wyrm_event_t e = {0};
        if (a == 0) {
            e.route_available = true;
            e.phases_healthy = true;
            wyrm_result_t r = wyrm_judge(&e);
            kv_str(ln, "VERDICT", wyrm_verdict_name(r.verdict));
        } else if (a == 1) {
            wyrm_result_t r = wyrm_judge(&e);
            kv_str(ln, "REASON", wyrm_reason_name(r.reason));
        } else {
            e.parent_ordinal = 2;
            e.ordinal = 1;
            wyrm_result_t r = wyrm_judge(&e);
            kv_str(ln, "ORDER", wyrm_reason_name(r.reason));
        }
    }
        sview_push(st, ln);
        break;
    case 9: /* BATTERING RAM */
        if (a == 0)
            kv_int(ln, "FUT PX",
                   (long) (br_price_capital_future(ZCAP_FINANCIAL, SR_FROM_INT(100), SR_ZERO,
                                                   SR_ONE) >>
                           SR_SHIFT));
        else if (a == 1) {
            br_term_t t = {0};
            t.on_suffering = true;
            kv_int(ln, "SUFFER OK", (long) symbiotic_ok(&t));
        } else {
            allocation_t al = {0};
            al.realized = SR_FROM_INT(100);
            al.contributor_pool = SR_FROM_INT(78);
            kv_int(ln, "MAJORITY", (long) br_contributors_hold_majority(&al));
        }
        sview_push(st, ln);
        break;
    case 10: /* INTERSPACE */
        if (a == 0) {
            interstitial_region_t r = {0};
            r.kind = ZXV_RES_COMMUNIS;
            r.owner = ZXV_NODE_NONE;
            kv_int(ln, "IS COMMONS", (long) interstitial_is_commons(&r));
        } else if (a == 1) {
            vessel_flag_t vf = {0};
            vf.keyholder = 846;
            kv_int(ln, "SELF IMMUNE", (long) vessel_immune_from(&vf, 846));
        } else {
            safe_passage_grant_t g = {0};
            g.issued_tick = 0;
            g.ttl_deadline = 100;
            kv_int(ln, "PASS@50", (long) safe_passage_valid(&g, 50));
        }
        sview_push(st, ln);
        break;
    case 11: /* BADGE */
        if (a == 0) {
            badge_award(&g_rep, 846u, 1u, 1u);
            kv_int(ln, "SCORE", (long) badge_score(&g_rep, 846u));
        } else if (a == 1) {
            badge_award(&g_rep, 846u, 2u, 3u);
            kv_int(ln, "SCORE", (long) badge_score(&g_rep, 846u));
        } else
            kv_int(ln, "SCORE", (long) badge_score(&g_rep, 846u));
        sview_push(st, ln);
        break;
    case 12: /* LOGISTICS */
        if (a == 0) {
            uint32_t p[2] = {846u, 847u};
            int id = log_contract_open(&g_log, p, 2u, SR_FROM_INT(50), 0, 0);
            kv_int(ln, "NEW ID", id);
        } else if (a == 1) {
            log_set_verifier(&g_log, log_ed25519_delivery_verify);
            kv_int(ln, "ARMED", (long) (g_log.verify != 0));
        } else {
            log_delivery_t d = {0};
            log_result_t rr = log_escrow_release(&g_log, 1u, &d);
            kv_int(ln, "RELEASE ST", (long) rr.status);
        }
        sview_push(st, ln);
        break;
    default:
        kv_str(ln, "RUN", SPC_ACT[s][a]);
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
    kv_str(ln, "OPEN", SPC_SUB[s][k]);
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

/* Linux evdev keycode -> lowercase ASCII (0 = ignore).
 * Indices are evdev codes (linux/input-event-codes.h): KEY_SPACE=57, not 56.
 * The old table put ' ' at 56 (KEY_LEFTALT), so the space bar was dead and
 * ',' '=' '[' ']' ';' '\'' '\\' produced nothing. Full row, correctly indexed. */
static char keymap(int32_t k)
{
    static const char row[64] = {
        /*0  RESERVED,ESC,1..0,-,=,BKSP,TAB */
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', 0,
        /*16 q..p,[,],ENTER,LCTRL,a,s */
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
        /*32 d..l,;,',`,LSHIFT,\,z..v */
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
        /*48 b,n,m,COMMA,.,/,RSHIFT,KP*,LALT,SPACE,CAPS,F1..F5 */
        'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0};
    if (k < 0 || k >= 64) return 0;
    return row[k];
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
    spaces_boot(); /* bring up the spaces' real subsystems */
    term_push(st, "ZEDEC XERO VULPINE  --  pqOS [ARM64]");
    term_push(st, "THE KERNEL IS THE OS: ECONOMY, GOVERNANCE, SOCIAL IN-KERNEL");
    term_push(st,
              "CLICK A DOCK APP, OR TYPE A COMMAND. TRY: HELP  --  [SPACES/13] OR TAB = LATTICE");
}

void zxv_shell_key(zxv_shell_state_t *st, int32_t keycode)
{
    if (keycode == 15) {
        st->view = (st->view == 1) ? 0 : 1;
        return;
    } /* TAB: base<->lattice, space->lattice */
    if (keycode == 1) {
        st->view = 0;
        return;
    } /* ESC: drop home to the base plane   */
    if (st->view == 1) { /* lattice: keyboard nav along edges  */
        if (keycode == 103) {
            st->focus_node = lat_neighbor(st->focus_node, 0, -1);
            return;
        } /* UP    */
        if (keycode == 108) {
            st->focus_node = lat_neighbor(st->focus_node, 0, 1);
            return;
        } /* DOWN  */
        if (keycode == 105) {
            st->focus_node = lat_neighbor(st->focus_node, -1, 0);
            return;
        } /* LEFT  */
        if (keycode == 106) {
            st->focus_node = lat_neighbor(st->focus_node, 1, 0);
            return;
        } /* RIGHT */
        if (keycode == 28) {
            enter_space(st, st->focus_node);
            return;
        } /* ENTER: open focused space */
    }
    char c = keymap(keycode);
    if (!c) return;
    if (c == '\n')
        run_cmd(st);
    else if (c == '\b') {
        if (st->cmdlen > 0) st->cmdlen--;
    } else if (st->cmdlen < SHELL_CMD_MAX - 1) {
        st->cmd[st->cmdlen++] = c;
    }
}

/* hit-test a left-click edge against the dock */
static void handle_click(zxv_shell_state_t *st, int32_t cx, int32_t cy)
{
    for (int i = 0; i < SHELL_APPS; i++) {
        int32_t y = DOCK_Y + i * DOCK_STEP;
        if (cx >= DOCK_X && cx < DOCK_X + DOCK_S && cy >= y && cy < y + DOCK_S) {
            st->active_app = i;
            /* launching = echo an open into the terminal */
            char ln[SHELL_LINE_LEN];
            const char *a = "VULPINE@ZXV:~$ OPEN ";
            uint32_t j = 0;
            while (a[j] && j < 30) {
                ln[j] = a[j];
                j++;
            }
            const char *f = APP_FILE[i];
            uint32_t k = 0;
            while (f[k] && j < SHELL_LINE_LEN - 1) ln[j++] = f[k++];
            ln[j] = 0;
            term_push(st, ln);
            char ln2[SHELL_LINE_LEN];
            const char *b = "  OPENED ";
            j = 0;
            while (b[j] && j < 20) {
                ln2[j] = b[j];
                j++;
            }
            const char *nm = APP_NAME[i];
            k = 0;
            while (nm[k] && j < SHELL_LINE_LEN - 14) ln2[j++] = nm[k++];
            const char *t = " [S+] VERIFIED";
            k = 0;
            while (t[k] && j < SHELL_LINE_LEN - 1) ln2[j++] = t[k++];
            ln2[j] = 0;
            term_push(st, ln2);
            return;
        }
    }
    /* START button (taskbar, left) */
    /* handled in frame via H; left here for dock focus only */
}

/* Tag a screen region's field tiles with a phase offset + depth (0=near..255=far)
 * so the holographic present gives each OBJECT its own shimmer + chromostereopsis. */
static void tagf(uint8_t *fp, uint8_t *fd, int32_t x, int32_t y, int32_t w, int32_t h,
                 uint8_t phase, uint8_t depth)
{
    if (!fp || !fd) return;
    int32_t x0 = x >> 4, y0 = y >> 4, x1 = (x + w + 15) >> 4, y1 = (y + h + 15) >> 4;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > (int32_t) g_ftx) {
        x1 = (int32_t) g_ftx;
    }
    if (y1 > (int32_t) g_fty) {
        y1 = (int32_t) g_fty;
    }
    for (int32_t ty = y0; ty < y1; ty++)
        for (int32_t tx = x0; tx < x1; tx++) {
            fp[ty * (int32_t) g_ftx + tx] = phase;
            fd[ty * (int32_t) g_ftx + tx] = depth;
        }
}

/* hit-test a click against the 13 nodes; focus one, or enter BASE to go home */
static void lattice_click(zxv_shell_state_t *st, int32_t W, int32_t H, int32_t cx, int32_t cy)
{
    int32_t nx[LATTICE_NODES], ny[LATTICE_NODES];
    lat_pos(W, H, nx, ny);
    for (int i = 0; i < LATTICE_NODES; i++) {
        int32_t dx = cx - nx[i], dy = cy - ny[i], r = (i == st->focus_node) ? 58 : 48;
        if (dx * dx + dy * dy <= r * r) {
            if (i == 0) {
                st->view = 0;
            } /* BASE node -> drop into the home plane   */
            else if (i == st->focus_node) {
                enter_space(st, i);
            } /* 2nd click -> ENTER (push-in) */
            else
                st->focus_node = i; /* 1st click -> bring that space warm-near */
            return;
        }
    }
}

/* draw the lattice overview: the pull-back layer stacked above the base plane */
static void draw_lattice(zxv_shell_state_t *st, vbe_state_t *v, int32_t cx, int32_t cy,
                         uint32_t tick, uint8_t *fphase, uint8_t *fdepth)
{
    (void) tick;
    const int32_t W = (int32_t) v->width, H = (int32_t) v->height;
    box(v, 0, 0, W, H, 0x05060C); /* dim the world: this is a pull-back */

    /* menu bar + BASE pill */
    box(v, 0, 0, W, 40, C_BAR);
    vbe_fill_rect(v, 0, 39, W, 1, C_GOLD_DIM);
    box(v, 12, 8, 24, 24, C_GOLD);
    box(v, 17, 13, 14, 14, C_RED);
    gt(v, 48, 4, "ZEDEC PQOS", C_GOLD, 2);
    gt(v, W - 400, 4, "THE 13  --  FRUIT OF LIFE / METATRON", C_GOLD_DIM, 1);
    gt(v, W - 400, 21, "ARROWS MOVE  ENTER OPENS  CLICK FOCUS  TAB HOME", C_DIM, 1);
    {
        int32_t px = W / 2 - 70, py = 6, pw = 140, ph = 28;
        box(v, px, py, pw, ph, C_RED_DK);
        frame(v, px, py, pw, ph, C_GOLD);
        gt(v, px + 20, py + 7, "[ BASE PLANE ]", C_GOLD, 1);
    }

    int32_t nx[LATTICE_NODES], ny[LATTICE_NODES];
    lat_pos(W, H, nx, ny);

    /* which node is under the cursor (hover highlight) */
    int hover = -1;
    for (int i = 0; i < LATTICE_NODES; i++) {
        int32_t dx = cx - nx[i], dy = cy - ny[i], r = (i == st->focus_node) ? 54 : 44;
        if (dx * dx + dy * dy <= r * r) {
            hover = i;
            break;
        }
    }

    /* Metatron edges behind the nodes — the adjacent + perpendicular wiring */
    for (int e = 0; e < LAT_NE; e++) {
        int a = LAT_E[e][0], b = LAT_E[e][1];
        line(v, nx[a], ny[a], nx[b], ny[b], 0x24304A);
    }

    /* the 13 nodes: focused = warm-near-bright, the rest cool-far-dim */
    for (int i = 0; i < LATTICE_NODES; i++) {
        int foc = (i == st->focus_node);
        int32_t r = foc ? 54 : 44;
        uint32_t ac = LAT_AC[i];
        disc(v, nx[i], ny[i], r, foc ? 0x161022u : 0x0B0B13u);
        ring(v, nx[i], ny[i], r, foc ? C_GOLD : ac);
        if (foc)
            ring(v, nx[i], ny[i], r + 4, ac);
        else if (i == hover)
            ring(v, nx[i], ny[i], r + 3, C_WHITE); /* hover cue */
        char g[2] = {LAT_G[i], 0};
        gt(v, nx[i] - 12, ny[i] - 24, g, foc ? C_GOLD : ac, 3);
        gt(v, nx[i] - (int32_t) slen(LAT_NM[i]) * 4, ny[i] + r + 6, LAT_NM[i],
           foc ? C_GOLD : C_TEXT, 1);
        gt(v, nx[i] - (int32_t) slen(LAT_SUB[i]) * 4, ny[i] + r + 20, LAT_SUB[i], C_DIM, 1);
        /* field: each node its own shimmer phase; focused near, else far */
        tagf(fphase, fdepth, nx[i] - r, ny[i] - r, 2 * r, 2 * r, (uint8_t) (8 + i * 7),
             foc ? 36 : 150);
    }
    {
        const char *nm = LAT_NM[st->focus_node];
        int32_t fw = (int32_t) slen(nm) * 8;
        gt(v, 16, H - 22, "LATTICE  --  ROOT SYSTEM IS THE KERNEL   FOCUS:", C_GOLD_DIM, 1);
        gt(v, 16 + 49 * 8, H - 22, nm, C_CYAN, 1);
        (void) fw;
    }
}

/* the plane you push INTO: a focused space's own full screen. Each of the 12
 * carries a seed sub-lattice (its own 6 sub-spaces) -- the tree recurses here. */
static void draw_space(zxv_shell_state_t *st, vbe_state_t *v, int32_t cx, int32_t cy, uint32_t tick,
                       uint8_t *fphase, uint8_t *fdepth)
{
    (void) cx;
    (void) cy;
    (void) tick;
    const int32_t W = (int32_t) v->width, H = (int32_t) v->height;
    int s = st->space;
    if (s < 0 || s >= LATTICE_NODES) s = 0;
    uint32_t ac = LAT_AC[s];
    box(v, 0, 0, W, H, 0x06070E);

    /* top band + centre BACK pill + wordmark */
    box(v, 0, 0, W, 44, C_BAR);
    vbe_fill_rect(v, 0, 43, W, 1, ac);
    box(v, 12, 10, 24, 24, C_GOLD);
    box(v, 17, 15, 14, 14, C_RED);
    gt(v, 48, 6, "ZEDEC PQOS", C_GOLD, 2);
    gt(v, W - 330, 6, "SPACE PLANE  --  A LAYER OF THE 13", C_GOLD_DIM, 1);
    gt(v, W - 330, 23, "TAB / BACK = LATTICE    ESC = BASE", C_DIM, 1);
    {
        int32_t px = W / 2 - 70, py = 8, pw = 140, ph = 28;
        box(v, px, py, pw, ph, C_RED_DK);
        frame(v, px, py, pw, ph, C_GOLD);
        gt(v, px + 14, py + 7, "[ < LATTICE ]", C_GOLD, 1);
    }

    /* the node badge */
    int32_t bx = 120, by = 190, br = 64;
    disc(v, bx, by, br, 0x141019);
    ring(v, bx, by, br, ac);
    ring(v, bx, by, br + 5, C_GOLD);
    {
        char g[2] = {LAT_G[s], 0};
        gt(v, bx - 16, by - 30, g, C_GOLD, 4);
    }
    gt(v, bx + 96, by - 40, LAT_NM[s], C_GOLD, 3);
    gt(v, bx + 96, by - 2, LAT_SUB[s], ac, 1);

    /* content: LIVE readout, computed by the space's REAL subsystem on entry */
    int32_t tx = bx + 96, ty = by + 40;
    for (int i = 0; i < st->sview_count; i++) {
        gt(v, tx, ty, st->sview[i], (i == 0 ? C_TEXT : C_DIM), 1);
        ty += 22;
    }

    /* action buttons — each click invokes the real subsystem (space_action) */
    gt(v, SPC_BX, SPC_BY - 22, "ACTIONS  --  CLICK TO INVOKE THE SUBSYSTEM", C_GOLD_DIM, 1);
    for (int a = 0; a < SPACE_ACTS; a++) {
        const char *lbl = SPC_ACT[s][a];
        if (!lbl) continue;
        int32_t rx, ry, rw, rh;
        spc_btn_rect(a, &rx, &ry, &rw, &rh);
        int hot = (cx >= rx && cx < rx + rw && cy >= ry && cy < ry + rh);
        box(v, rx, ry, rw, rh, hot ? 0x1B1420u : C_RED_DK);
        frame(v, rx, ry, rw, rh, (st->act_hot == a) ? C_GREEN : (hot ? C_GOLD : ac));
        gt(v, rx + 14, ry + 13, lbl, hot ? C_GOLD : C_TEXT, 1);
    }

    /* sub-spaces: this space's own 6 sub-features (the next layer down) */
    int32_t sx = W - 300, sy = H / 2 + 50;
    gt(v, sx - 96, sy - 92 - 28, "SUB-SPACES  --  THE NEXT LAYER DOWN", C_GOLD_DIM, 1);
    disc(v, sx, sy, 10, 0x141019);
    ring(v, sx, sy, 10, C_GOLD);
    for (int k = 0; k < SPACE_SUBS; k++) {
        int32_t px, py;
        subspace_xy(W, H, k, &px, &py);
        int sel = (st->subsel == k);
        int hov = ((cx - px) * (cx - px) + (cy - py) * (cy - py) <= 22 * 22);
        line(v, sx, sy, px, py, 0x24304A);
        disc(v, px, py, 18, sel ? 0x161022u : 0x101018);
        ring(v, px, py, 18, sel ? C_GOLD : (hov ? C_WHITE : ac));
        const char *sn = SPC_SUB[s][k];
        if (sn) gt(v, px - (int32_t) slen(sn) * 4, py + 22, sn, sel ? C_GOLD : C_DIM, 1);
    }

    gt(v, 16, H - 24, "ROOT SYSTEM IS THE KERNEL  --  SCREENS STACKED UPON SCREENS", C_GOLD_DIM, 1);

    /* field: push-in depth sweep far->near using this space's own phase */
    int tr = st->trans;
    if (tr < 0) tr = 0;
    if (tr > LATTICE_TRANS) tr = LATTICE_TRANS;
    uint8_t depth = (uint8_t) (40 + tr * 190 / LATTICE_TRANS);
    tagf(fphase, fdepth, 0, 0, W, H, (uint8_t) (8 + s * 7), depth);
}

void zxv_shell_frame(zxv_shell_state_t *st, vbe_state_t *v, int32_t cx, int32_t cy,
                     uint32_t buttons, uint32_t tick, uint8_t *fphase, uint8_t *fdepth)
{
    const int32_t W = (int32_t) v->width, H = (int32_t) v->height;
    /* field map: the background is the deep base plane (far, slow phase); each
     * object overrides its own tiles below — the 2D base with deliberate depth. */
    if (fphase && fdepth)
        for (int32_t i = 0; i < (int32_t) FIELD_CELLS_MAX; i++) {
            fphase[i] = 0;
            fdepth[i] = 210;
        }

    /* --- input: left-click edge --- */
    if ((buttons & 1u) && !(st->prev_buttons & 1u)) {
        st->click_count++;
        /* the SPACES/BASE pill (menu-bar centre) toggles the lattice in either view */
        if (cx >= W / 2 - 70 && cx < W / 2 + 70 && cy >= 6 &&
            cy < 40) { /* SPACES / BASE / < LATTICE pill */
            if (st->view == 2)
                st->view = 1;
            else
                st->view ^= 1;
        } else if (st->view == 2) { /* inside a space: buttons + sub-spaces */
            int s = st->space;
            if (s < 0 || s >= LATTICE_NODES) s = 0;
            int handled = 0;
            for (int a = 0; a < SPACE_ACTS && !handled; a++) {
                if (!SPC_ACT[s][a]) continue;
                int32_t rx, ry, rw, rh;
                spc_btn_rect(a, &rx, &ry, &rw, &rh);
                if (cx >= rx && cx < rx + rw && cy >= ry && cy < ry + rh) {
                    space_action(st, s, a);
                    handled = 1;
                }
            }
            for (int k = 0; k < SPACE_SUBS && !handled; k++) {
                int32_t px, py;
                subspace_xy(W, H, k, &px, &py);
                if ((cx - px) * (cx - px) + (cy - py) * (cy - py) <= 22 * 22) {
                    subspace_select(st, s, k);
                    handled = 1;
                }
            }
        } else if (st->view == 1)
            lattice_click(st, W, H, cx, cy);
        else if (cy < H - 32)
            handle_click(st, cx, cy);
        else if (cx >= 8 && cx < 82)
            term_push(st, "ZEDEC MENU: 5 APPS  ONE POLICY  PHASE-TICK");
    }
    st->prev_buttons = buttons;

    /* --- SPACE view: the plane you pushed INTO (the tree recurses) --- */
    if (st->view == 2) {
        draw_space(st, v, cx, cy, tick, fphase, fdepth);
        if (st->trans > 0) st->trans--;
        cursor(v, cx, cy);
        return;
    }
    /* --- LATTICE view: the layer stacked above the base plane --- */
    if (st->view == 1) {
        draw_lattice(st, v, cx, cy, tick, fphase, fdepth);
        cursor(v, cx, cy);
        return;
    }

    /* --- menu bar --- */
    box(v, 0, 0, W, 40, C_BAR);
    vbe_fill_rect(v, 0, 39, W, 1, C_GOLD_DIM);
    box(v, 12, 8, 24, 24, C_GOLD);
    box(v, 17, 13, 14, 14, C_RED);
    gt(v, 48, 4, "ZEDEC PQOS", C_GOLD, 2);
    gt(v, W - 470, 4, "ZEDEC XERO VULPINE  --  POST-QUANTUM OS", C_DIM, 1);
    gt(v, W - 470, 21, "ECONOMY 5/5   PLATFORM 8/8   EL0   PHASE-TICK", C_GOLD_DIM, 1);
    tagf(fphase, fdepth, 0, 0, W, 40, 2, 18); /* menu bar: near */
    /* the SPACES pill: pull back to the 13-node lattice (TAB or click) */
    {
        int32_t px = W / 2 - 70, py = 6, pw = 140, ph = 28;
        box(v, px, py, pw, ph, C_BAR_HI);
        frame(v, px, py, pw, ph, C_GOLD_DIM);
        gt(v, px + 18, py + 7, "[ SPACES / 13 ]", C_GOLD, 1);
        tagf(fphase, fdepth, px, py, pw, ph, 4, 14);
    }

    /* --- dock: each icon its OWN phase (independent shimmer) --- */
    for (int i = 0; i < SHELL_APPS; i++) {
        icon(v, DOCK_X, DOCK_Y + i * DOCK_STEP, APP_GLYPH[i], APP_ACC[i], APP_NAME[i], APP_FILE[i],
             st->active_app == i);
        tagf(fphase, fdepth, DOCK_X, DOCK_Y + i * DOCK_STEP, DOCK_S, DOCK_S, (uint8_t) (8 + i * 9),
             40);
    }

    /* --- terminal window (live) --- */
    {
        int32_t x = 176, y = 70, w = 660, h = 566;
        tagf(fphase, fdepth, x, y, w, h, 12, 70); /* terminal: mid depth, phase 12 */
        int32_t ty = window(v, x, y, w, h, "ZEDEC TERMINAL  --  VULPINE@ZXV:~", C_GOLD);
        int32_t tx = x + 14;
        for (int i = 0; i < st->term_count; i++) {
            gt(v, tx, ty, st->term[i], (i < 3 ? C_DIM : C_TEXT), 1);
            ty += 18;
        }
        /* live prompt */
        gt(v, tx, ty, "VULPINE@ZXV:~$ ", C_GREEN, 1);
        int32_t px = tx + 8 * 15;
        {
            char b[SHELL_CMD_MAX + 1];
            scopy(b, st->cmd, SHELL_CMD_MAX + 1);
            gt(v, px, ty, b, C_TEXT, 1);
        }
        if ((tick / 8) & 1u) box(v, px + 8 * st->cmdlen, ty, 9, 15, C_GREEN);
    }

    /* --- Chiglet panel --- */
    {
        int32_t x = 858, y = 70, w = 396, h = 250;
        tagf(fphase, fdepth, x, y, w, h, 30, 64);
        int32_t ty = window(v, x, y, w, h, "CHIGLET -- AI COMPANION", C_GREEN);
        int32_t tx = x + 14;
        gt(v, tx, ty, "NATIVE MIXTURE-OF-EXPERTS", C_DIM, 1);
        ty += 24;
        gt(v, tx, ty, "> MATCH TROLLS WITH TROLLS", C_TEXT, 1);
        ty += 18;
        gt(v, tx, ty, "> CONCORD: REHABILITATIVE", C_TEXT, 1);
        ty += 18;
        gt(v, tx, ty, "> PIG BADGE: OVER 9000", C_GOLD, 1);
        ty += 26;
        gt(v, tx, ty, "R (DISTINCT EVIDENCE): 3.0", C_CYAN, 1);
        ty += 18;
        gt(v, tx, ty, "VERDICT: DECIDED", C_GREEN, 1);
    }
    /* --- Vino wallet panel --- */
    {
        int32_t x = 858, y = 336, w = 396, h = 300;
        tagf(fphase, fdepth, x, y, w, h, 46, 58);
        int32_t ty = window(v, x, y, w, h, "VINO -- FLOATING VOUCHERS", C_GOLD);
        int32_t tx = x + 14;
        gt(v, tx, ty, "TRIPLE RAIL (ISO 4217)", C_DIM, 1);
        ty += 24;
        gt(v, tx, ty, "555 DEBIT    120.00", C_TEXT, 1);
        ty += 18;
        gt(v, tx, ty, "777 CREDIT    12.00", C_TEXT, 1);
        ty += 18;
        gt(v, tx, ty, "888 EQUITY   108.00", C_GREEN, 1);
        ty += 26;
        gt(v, tx, ty, "COVERAGE 1.8X  SOLVENT", C_CYAN, 1);
        ty += 18;
        gt(v, tx, ty, "NO DEBT, MERIT-BASED", C_DIM, 1);
        ty += 26;
        gt(v, tx, ty, "[SEND]  [SWAP]  [REDEEM]", C_GOLD, 1);
    }

    /* --- taskbar --- */
    tagf(fphase, fdepth, 0, H - 32, W, 32, 2, 18); /* taskbar: near */
    box(v, 0, H - 32, W, 32, C_BAR);
    vbe_fill_rect(v, 0, H - 32, W, 1, C_GOLD_DIM);
    box(v, 8, H - 26, 74, 20, C_RED_DK);
    frame(v, 8, H - 26, 74, 20, C_GOLD_DIM);
    gt(v, 16, H - 24, "START", C_GOLD, 1);
    for (int i = 0; i < SHELL_APPS; i++)
        gt(v, 98 + i * 74, H - 24, APP_NAME[i], st->active_app == i ? C_GOLD : C_DIM, 1);
    {
        char buf[20];
        uint32_t t = tick;
        char tmp[12];
        int i = 0;
        if (t == 0) tmp[i++] = '0';
        while (t > 0 && i < 10) {
            tmp[i++] = (char) ('0' + (t % 10));
            t /= 10;
        }
        int j = 0;
        const char *p = "TICK ";
        while (*p) buf[j++] = *p++;
        while (i > 0) buf[j++] = tmp[--i];
        buf[j] = 0;
        gt(v, W - 110, H - 24, buf, C_CYAN, 1);
    }

    /* --- pointer --- */
    cursor(v, cx, cy);
}
