/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_chgskin.c — the Chiglet's base schematic and its skins. See zui_chgskin.h. */
#include "zui_chgskin.h"
#include "zui_tokens.h"
#include "holo.h"

enum { G_EMOTION, G_TEMPER, G_CURIOUS, G_CAUTION, G_WARMTH, G_PACE, G_VERBOSE, G_SPECIALTY };

/* ---- the built-in Anime skin (tools/chgskin/anime_skin.py) --------------- */
#include "zui_chgskin_anime.inc"

/* ---- reading a .zxcs ----------------------------------------------------- */

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}
static int16_t rds16(const uint8_t *p)
{
    return (int16_t) rd16(p);
}

static void zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *) p;
    while (n--) *b++ = 0;
}

/* a text field: one length byte, then that many bytes of the allowed kind */
static zui_skin_status_t text(const uint8_t *b, uint32_t n, uint32_t *at, char *out, uint32_t cap,
                              uint32_t min, bool spdx)
{
    if (*at >= n) return ZUI_SKIN_E_TRUNCATED;
    uint32_t len = b[(*at)++];
    if (len < min || len >= cap) return ZUI_SKIN_E_TEXT;
    if (n - *at < len) return ZUI_SKIN_E_TRUNCATED;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t ch = b[*at + i];
        bool ok = ch >= 0x20u && ch <= 0x7Eu;
        if (spdx)
            ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                 ch == '.' || ch == '-' || ch == '+' || ch == ' ' || ch == '(' || ch == ')';
        if (!ok) return ZUI_SKIN_E_TEXT;
        out[i] = (char) ch;
    }
    out[len] = 0;
    *at += len;
    return ZUI_SKIN_OK;
}

static bool in_box(int32_t v)
{
    return v >= -ZUI_SKIN_COORD && v <= ZUI_SKIN_COORD;
}

zui_skin_status_t zui_chg_skin_parse(zui_chg_skin_t *out, const uint8_t *b, uint32_t n)
{
    if (!out) return ZUI_SKIN_E_ARG;
    zero(out, sizeof *out);
    if (!b) return ZUI_SKIN_E_ARG;
    zui_chg_skin_t k;
    zero(&k, sizeof k);
    zui_skin_status_t st = ZUI_SKIN_OK;
    if (n < 10u) return ZUI_SKIN_E_TRUNCATED;
    if (b[0] != 'Z' || b[1] != 'X' || b[2] != 'C' || b[3] != 'S') return ZUI_SKIN_E_MAGIC;
    if (b[4] != ZUI_SKIN_VERSION) return ZUI_SKIN_E_VERSION;
    k.flags = b[5];
    if (k.flags & (uint8_t) ~ZUI_SKIN_EXPRESSIVE) return ZUI_SKIN_E_FLAGS;
    k.nshapes = rd16(b + 6);
    k.npoints = rd16(b + 8);
    if (k.nshapes < 1u || k.nshapes > ZUI_SKIN_MAX_SHAPES || k.npoints < 3u ||
        k.npoints > ZUI_SKIN_MAX_POINTS)
        return ZUI_SKIN_E_LIMIT;
    uint32_t at = 10u;
    if ((st = text(b, n, &at, k.name, sizeof k.name, 1u, false)) != ZUI_SKIN_OK) return st;
    if ((st = text(b, n, &at, k.author, sizeof k.author, 0u, false)) != ZUI_SKIN_OK) return st;
    if ((st = text(b, n, &at, k.licence, sizeof k.licence, 1u, true)) != ZUI_SKIN_OK) return st;
    uint32_t need =
        (uint32_t) k.nshapes * ZUI_SKIN_SHAPE_B + (uint32_t) k.npoints * ZUI_SKIN_POINT_B;
    if (n - at < need) return ZUI_SKIN_E_TRUNCATED;
    if (n - at > need) return ZUI_SKIN_E_TRAILING;
    k.shapes = b + at;
    k.points = k.shapes + (uint32_t) k.nshapes * ZUI_SKIN_SHAPE_B;

    uint32_t first = 0;
    for (uint32_t i = 0; i < k.nshapes; i++) {
        const uint8_t *r = k.shapes + i * ZUI_SKIN_SHAPE_B;
        if (r[0] >= ZUI_SKG_COUNT || r[1] >= ZUI_SKR_COUNT || r[2] >= ZUI_SKP_COUNT || r[7] != 0)
            return ZUI_SKIN_E_ENUM;
        if (!in_box(rds16(r + 8)) || !in_box(rds16(r + 10)) || !in_box(rds16(r + 12)))
            return ZUI_SKIN_E_COORD;
        uint32_t np = rd16(r + 14);
        if (np < 3u || np > ZUI_SKIN_MAX_SHAPE) return ZUI_SKIN_E_LIMIT;
        if (first + np > k.npoints) return ZUI_SKIN_E_LIMIT;
        bool prev_off = false;
        for (uint32_t j = 0; j < np; j++) {
            const uint8_t *p = k.points + (first + j) * ZUI_SKIN_POINT_B;
            if (!in_box(rds16(p)) || !in_box(rds16(p + 2))) return ZUI_SKIN_E_COORD;
            if (p[4] & ~1u) return ZUI_SKIN_E_OUTLINE;
            bool off = !(p[4] & 1u);
            if (j == 0 && off) return ZUI_SKIN_E_OUTLINE;   /* starts on the outline */
            if (off && prev_off) return ZUI_SKIN_E_OUTLINE; /* one control point at a time */
            prev_off = off;
        }
        first += np;
    }
    if (first != k.npoints) return ZUI_SKIN_E_LIMIT;
    *out = k;
    return ZUI_SKIN_OK;
}

const uint8_t *zui_chg_skin_anime_bytes(uint32_t *len)
{
    if (len) *len = (uint32_t) sizeof ZUI_CHGSKIN_ANIME;
    return ZUI_CHGSKIN_ANIME;
}

const zui_chg_skin_t *zui_chg_skin_anime(void)
{
    static zui_chg_skin_t k;
    static int state; /* 0 unread, 1 good, -1 broken */
    if (!state)
        state = zui_chg_skin_parse(&k, ZUI_CHGSKIN_ANIME, (uint32_t) sizeof ZUI_CHGSKIN_ANIME) ==
                        ZUI_SKIN_OK
                    ? 1
                    : -1;
    return state > 0 ? &k : 0;
}

bool zui_chg_skin_expressive(const zui_chg_skin_t *skin)
{
    return skin && (skin->flags & ZUI_SKIN_EXPRESSIVE);
}

/* ---- the pose ------------------------------------------------------------ */

void zui_chg_pose_from(zui_chg_pose_t *p, const zui_chiglet_t *c)
{
    if (!p) return;
    zero(p, sizeof *p);
    swarm_feeling_t f = {SWARM_EMO_NEUTRAL, 0};
    p->face = zui_face_feeling(f);
    if (!c) return;
    if (c->mood == ZUI_CHG_UNCERTAIN) p->face.brow_raise = 256;
    if (c->mood == ZUI_CHG_SPEAKING)
        p->mouth = c->mouth ? c->mouth : 128u;
    else
        p->mouth = c->mouth;
}

uint16_t zui_chg_blink(const zui_chiglet_t *c, uint32_t now_ms, bool reduce_motion)
{
    if (!c || reduce_motion) return 0;
    /* every 3.2 .. 5.0 s, its own rhythm and phase; 140 ms down and up */
    uint32_t period = 3200u + 200u * (c->genes[G_PACE] % 10u);
    uint32_t t = (now_ms + 977u * c->genes[G_TEMPER]) % period;
    if (t >= 140u) return 0;
    uint32_t h = t < 70u ? t : 140u - t; /* 0 .. 70 .. 0 */
    return (uint16_t) (h * 256u / 70u);
}

void zui_chg_gaze(int32_t cx, int32_t cy, int32_t r, int32_t px, int32_t py, int16_t *gx,
                  int16_t *gy)
{
    int64_t dx = (int64_t) px - cx, dy = (int64_t) py - cy;
    if (r < 1) r = 1;
    int64_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int64_t m = ax > ay ? ax : ay; /* square reach keeps it integer and cheap */
    int64_t x = 0, y = 0;
    if (m > 0) {
        if (m <= r) {
            x = dx * 256 / r;
            y = dy * 256 / r;
        } else {
            x = dx * 256 / m;
            y = dy * 256 / m;
        }
    }
    if (gx) *gx = (int16_t) x;
    if (gy) *gy = (int16_t) y;
}

/* ---- colour -------------------------------------------------------------- */

static const uint32_t HAIR[8] = {0x2B1B17u, 0x6B3E26u, 0xC98B4Bu, 0xF2D27Au,
                                 0xCFE8D8u, 0x3A6EA5u, 0x8C4FA3u, 0xE06A8Au};
static const uint32_t IRIS[6] = {0x3E7CB1u, 0x4FA36Bu, 0x8B5A2Bu, 0xF2C230u, 0x9B59B6u, 0x2BB3A3u};
static const uint32_t TONE[6] = {0xFFE7D6u, 0xF6D0B1u, 0xE0AC85u, 0xC68863u, 0x8D5A3Bu, 0x5C3A25u};

static uint32_t paint(const zui_chiglet_t *c, uint8_t kind, const uint8_t *rgb)
{
    uint32_t fixed = ((uint32_t) rgb[0] << 16) | ((uint32_t) rgb[1] << 8) | rgb[2];
    const uint8_t *g = c->genes;
    uint32_t base;
    switch (kind) {
    case ZUI_SKP_FIXED:
        return fixed;
    case ZUI_SKP_ACCENT:
        base = zui_chiglet_accent(c);
        break;
    case ZUI_SKP_BODY:
        base = holo_shade(0x1D5C3Bu, 0, ((int32_t) (g[G_WARMTH] % 9u) - 4) * 20, 8, 40);
        break;
    case ZUI_SKP_HAIR:
        base = HAIR[(g[G_WARMTH] * 3u + g[G_EMOTION] + g[G_TEMPER]) % 8u];
        break;
    case ZUI_SKP_IRIS:
        base = IRIS[(g[G_CURIOUS] + 2u * g[G_CAUTION]) % 6u];
        break;
    case ZUI_SKP_TONE:
        base = TONE[(g[G_TEMPER] + g[G_VERBOSE] + g[G_SPECIALTY]) % 6u];
        break;
    case ZUI_SKP_INK:
        base = ZUI_TEXT;
        break;
    default:
        base = ZUI_VOID;
        break;
    }
    base = zui_mix(base, 0x000000u, rgb[0]);
    return zui_mix(base, 0xFFFFFFu, rgb[1]);
}

/* ---- the filler: nonzero winding, 4 sub-rows x 16 sub-columns ------------- */

#define MAX_EDGES 4096
#define MAX_COLS  4096

typedef struct {
    int32_t x0, y0, x1, y1; /* Q4 pixels, y0 < y1 */
    int8_t w;
} edge_t;

static edge_t E[MAX_EDGES];
static int32_t XS[MAX_EDGES];
static int8_t WS[MAX_EDGES];
static int32_t ACC[MAX_COLS];
static int32_t PX[ZUI_SKIN_MAX_SHAPE], PY[ZUI_SKIN_MAX_SHAPE];
static uint8_t PON[ZUI_SKIN_MAX_SHAPE];

static uint32_t ne;
static int32_t bx0, by0, bx1, by1; /* Q4 bounds */

static void add_edge(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (x0 < bx0) bx0 = x0;
    if (x1 < bx0) bx0 = x1;
    if (x0 > bx1) bx1 = x0;
    if (x1 > bx1) bx1 = x1;
    if (y0 < by0) by0 = y0;
    if (y1 < by0) by0 = y1;
    if (y0 > by1) by1 = y0;
    if (y1 > by1) by1 = y1;
    if (y0 == y1 || ne >= MAX_EDGES) return;
    edge_t *e = &E[ne++];
    if (y0 < y1) {
        *e = (edge_t){x0, y0, x1, y1, 1};
    } else {
        *e = (edge_t){x1, y1, x0, y0, -1};
    }
}

static void quad(int32_t x0, int32_t y0, int32_t cxq, int32_t cyq, int32_t x1, int32_t y1)
{
    int32_t px = x0, py = y0;
    for (int32_t i = 1; i <= 8; i++) {
        int64_t t = i, u = 8 - i; /* B(t) = u^2 p0 + 2ut c + t^2 p1, over 64 */
        int32_t qx = (int32_t) ((u * u * x0 + 2 * u * t * cxq + t * t * x1) / 64);
        int32_t qy = (int32_t) ((u * u * y0 + 2 * u * t * cyq + t * t * y1) / 64);
        add_edge(px, py, qx, qy);
        px = qx;
        py = qy;
    }
}

static void fill(zui_surface_t *s, uint32_t rgb, uint32_t a255)
{
    if (!ne || !a255) return;
    zui_rect_t c = s->clip;
    int32_t ry0 = by0 >> 4, ry1 = (by1 + 15) >> 4;
    int32_t cx0 = bx0 >> 4, cx1 = (bx1 + 15) >> 4;
    if (ry0 < c.y) ry0 = c.y;
    if (ry1 > c.y + c.h) ry1 = c.y + c.h;
    if (cx0 < c.x) cx0 = c.x;
    if (cx1 > c.x + c.w) cx1 = c.x + c.w;
    if (cx1 - cx0 > MAX_COLS) cx1 = cx0 + MAX_COLS;
    if (ry0 >= ry1 || cx0 >= cx1) return;
    int32_t lim0 = cx0 * 16, lim1 = cx1 * 16;
    for (int32_t row = ry0; row < ry1; row++) {
        for (int32_t i = 0; i < cx1 - cx0; i++) ACC[i] = 0;
        bool any = false;
        for (int32_t k = 0; k < 4; k++) {
            int32_t sy = row * 16 + 2 + 4 * k;
            uint32_t nx = 0;
            for (uint32_t i = 0; i < ne; i++) {
                const edge_t *e = &E[i];
                if (sy < e->y0 || sy >= e->y1) continue;
                int32_t x =
                    e->x0 + (int32_t) ((int64_t) (sy - e->y0) * (e->x1 - e->x0) / (e->y1 - e->y0));
                uint32_t j = nx++; /* insertion keeps them sorted */
                while (j > 0 && XS[j - 1] > x) {
                    XS[j] = XS[j - 1];
                    WS[j] = WS[j - 1];
                    j--;
                }
                XS[j] = x;
                WS[j] = e->w;
            }
            int32_t wind = 0;
            for (uint32_t i = 0; i + 1 < nx; i++) {
                wind += WS[i];
                if (!wind) continue;
                int32_t xa = XS[i], xb = XS[i + 1];
                if (xa < lim0) xa = lim0;
                if (xb > lim1) xb = lim1;
                if (xa >= xb) continue;
                any = true;
                for (int32_t p = xa >> 4; p <= (xb - 1) >> 4; p++) {
                    int32_t lo = p * 16 > xa ? p * 16 : xa;
                    int32_t hi = p * 16 + 16 < xb ? p * 16 + 16 : xb;
                    ACC[p - cx0] += hi - lo;
                }
            }
        }
        if (!any) continue;
        for (int32_t i = 0; i < cx1 - cx0; i++) {
            if (!ACC[i]) continue;
            uint32_t cov = (uint32_t) (ACC[i] > 64 ? 64 : ACC[i]);
            zui_blend_px(s, cx0 + i, row, rgb, a255 * cov / 64u);
        }
    }
}

/* ---- drawing a skin ------------------------------------------------------ */

static const int16_t SIN16[16] = {0, 392,  724,  946,  1024,  946,  724,  392,
                                  0, -392, -724, -946, -1024, -946, -724, -392};

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void zui_chg_skin_draw(zui_surface_t *s, const zui_chg_skin_t *skin, const zui_chiglet_t *c,
                       const zui_chg_pose_t *p, int32_t cx, int32_t cy, int32_t r)
{
    if (!s || !c || r < 8) return;
    zui_chg_pose_t still;
    if (!p) {
        zui_chg_pose_from(&still, c);
        p = &still;
    }
    if (!skin) { /* classic: its genes are its look */
        zui_chiglet_t who = *c;
        if (p->mouth) {
            who.mood = ZUI_CHG_SPEAKING;
            who.mouth = p->mouth;
        }
        zui_chiglet_draw(s, &who, cx, cy, r, p->tick);
        return;
    }
    zui_chiglet_draw_aura(s, c, cx, cy, r, p->tick);

    const zui_face_t *f = &p->face;
    int32_t blink = clamp(p->blink, 0, 256);
    int32_t eye = clamp(f->eye_open, 0, 256) * (256 - blink) / 256;
    if (eye < 16) eye = 16;
    bool squint = f->squint >= 128;
    int32_t open = p->mouth ? (int32_t) p->mouth * 256 / 255 : clamp(f->mouth_open, 0, 256);
    bool mouth_open = open >= 24;
    int32_t curve = clamp(f->mouth_curve, -256, 256);
    int32_t gx = clamp(p->gaze_x, -256, 256), gy = clamp(p->gaze_y, -256, 256);
    /* the head turns a little toward the gaze and breathes */
    int32_t head_dx = gx * 40 / 256;
    int32_t head_dy = gy * 24 / 256 + SIN16[(p->tick + c->genes[G_PACE]) & 15u] * 14 / 1024;
    if (!p->tick) head_dy = gy * 24 / 256;

    uint32_t first = 0;
    for (uint32_t i = 0; i < skin->nshapes; i++) {
        const uint8_t *rec = skin->shapes + i * ZUI_SKIN_SHAPE_B;
        uint32_t np = rd16(rec + 14);
        const uint8_t *pts = skin->points + first * ZUI_SKIN_POINT_B;
        first += np;
        uint8_t group = rec[0], react = rec[1];
        uint32_t alpha = rec[3];
        int32_t pvx = rds16(rec + 8), pvy = rds16(rec + 10), amt = rds16(rec + 12);
        /* scale of y about the pivot (Q8), shear, offsets */
        int32_t sy = 256, shear = 0, ox = 0, oy = 0;
        switch (react) {
        case ZUI_SKR_EYE:
            if (squint) continue;
            sy = eye;
            break;
        case ZUI_SKR_PUPIL:
            if (squint) continue;
            sy = eye;
            ox = gx * amt / 256;
            oy = gy * amt / 256;
            break;
        case ZUI_SKR_SQUINT:
            if (!squint) continue;
            break;
        case ZUI_SKR_MOUTH_OPEN:
            if (!mouth_open) continue;
            sy = open < 32 ? 32 : open;
            break;
        case ZUI_SKR_MOUTH_SMILE:
            if (mouth_open || curve < 0) continue;
            sy = curve < 32 ? 32 : curve;
            break;
        case ZUI_SKR_MOUTH_FROWN:
            if (mouth_open || curve >= 0) continue;
            sy = -curve < 32 ? 32 : -curve;
            break;
        case ZUI_SKR_BROW:
            shear = f->brow_tilt * amt / 256; /* Q10 slope */
            /* worry lifts both; wondering lifts the left one (amt > 0) */
            oy = -(f->brow_tilt > 0 ? f->brow_tilt / 12 : 0) -
                 (amt > 0 ? clamp(f->brow_raise, 0, 256) * 4 / 25 : 0);
            break;
        case ZUI_SKR_BLUSH:
            alpha = alpha * (uint32_t) clamp(f->blush, 0, 256) / 256u;
            break;
        case ZUI_SKR_TEAR:
            alpha = alpha * (uint32_t) clamp(f->tear, 0, 256) / 256u;
            break;
        case ZUI_SKR_CURIOUS:
            if ((int32_t) (c->genes[G_CURIOUS] % 4u) <= amt) continue;
            break;
        case ZUI_SKR_EMBLEM:
            if ((int32_t) (c->genes[G_SPECIALTY] % 4u) != amt) continue;
            break;
        default:
            break;
        }
        if (!alpha) continue;
        if (group == ZUI_SKG_HEAD) {
            ox += head_dx;
            oy += head_dy;
        }
        for (uint32_t j = 0; j < np; j++) {
            const uint8_t *q = pts + j * ZUI_SKIN_POINT_B;
            int32_t x = rds16(q), y = rds16(q + 2);
            y = pvy + (y - pvy) * sy / 256;
            y -= (x - pvx) * shear / 1024;
            x += ox;
            y += oy;
            PX[j] = (int32_t) ((int64_t) cx * 16 + (int64_t) x * r * 16 / 1024);
            PY[j] = (int32_t) ((int64_t) cy * 16 + (int64_t) y * r * 16 / 1024);
            PON[j] = q[4] & 1u;
        }
        ne = 0;
        bx0 = by0 = INT32_MAX;
        bx1 = by1 = INT32_MIN;
        for (uint32_t j = 0; j < np;) {
            uint32_t a = (j + 1) % np;
            if (PON[a]) {
                add_edge(PX[j], PY[j], PX[a], PY[a]);
                j += 1;
            } else {
                uint32_t b2 = (j + 2) % np;
                quad(PX[j], PY[j], PX[a], PY[a], PX[b2], PY[b2]);
                j += 2;
            }
        }
        fill(s, paint(c, rec[2], rec + 4), alpha);
    }
}
