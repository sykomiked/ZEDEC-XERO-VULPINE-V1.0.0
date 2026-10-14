/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_emote.c — live emoji faces that morph. See zui_emote.h. */
#include "zui_emote.h"
#include "zui_tokens.h"

/* ---- the eight faces, in swarm_emotion_t order -------------------------- */
/*            curve open  eye  sqnt  tilt raise blush tear  colour          */
static const zui_face_t FACES[SWARM_EMO_COUNT] = {
    {    0,   0, 256,   0,    0,   0,   0,   0, 0xB2E0C2u }, /* 😐 neutral     */
    {  110,   0,  48,   0,    0,   0,  60,   0, 0x7BE8A6u }, /* 😌 calm        */
    {  256, 200, 256, 256,    0,   0,  90,   0, 0xF2C230u }, /* 😄 joy         */
    {  -40,   0, 256,   0,  -40, 256,   0,   0, 0x8FD0FFu }, /* 🤔 curious     */
    {  220,   0, 256, 200,    0,   0, 256,   0, 0xFF9DB0u }, /* 🥰 love        */
    { -150,  40, 256,   0,  230,   0,   0,   0, 0xE8D27Au }, /* 😟 worry       */
    { -210,   0, 200,   0, -256,   0,   0,   0, 0xF2865Au }, /* 😠 frustration */
    { -256,   0, 170,   0,  150,   0,   0, 256, 0x7FA8E0u }, /* 😢 sadness     */
};

static const char *WORDS[SWARM_EMO_COUNT] = {
    "neutral", "calm", "joyful", "curious", "warm", "worried", "frustrated", "sad" };

#define INK 0x0A2416u   /* features: a deep green ink, never pure black */

zui_face_t zui_face_of(swarm_emotion_t e) {
    return (unsigned)e < SWARM_EMO_COUNT ? FACES[e] : FACES[SWARM_EMO_NEUTRAL];
}

const char *zui_emote_word(swarm_emotion_t e) {
    return (unsigned)e < SWARM_EMO_COUNT ? WORDS[e] : WORDS[0];
}

static int16_t lerp16(int16_t a, int16_t b, uint32_t t) {
    return (int16_t)(a + ((int32_t)(b - a) * (int32_t)t) / 256);
}

zui_face_t zui_face_lerp(zui_face_t a, zui_face_t b, uint32_t t) {
    if (t >= 256u) return b;
    zui_face_t f;
    f.mouth_curve = lerp16(a.mouth_curve, b.mouth_curve, t);
    f.mouth_open  = lerp16(a.mouth_open,  b.mouth_open,  t);
    f.eye_open    = lerp16(a.eye_open,    b.eye_open,    t);
    f.squint      = lerp16(a.squint,      b.squint,      t);
    f.brow_tilt   = lerp16(a.brow_tilt,   b.brow_tilt,   t);
    f.brow_raise  = lerp16(a.brow_raise,  b.brow_raise,  t);
    f.blush       = lerp16(a.blush,       b.blush,       t);
    f.tear        = lerp16(a.tear,        b.tear,        t);
    f.color       = t == 0u ? a.color : zui_mix(a.color, b.color, t > 255u ? 255u : t);
    return f;
}

zui_face_t zui_face_feeling(swarm_feeling_t f) {
    if (f.emotion == SWARM_EMO_NEUTRAL || (unsigned)f.emotion >= SWARM_EMO_COUNT)
        return FACES[SWARM_EMO_NEUTRAL];
    uint64_t ch = swarm_emotion_charge(f.intensity);           /* 0 1 2 3 5 8 */
    zui_face_t out = zui_face_lerp(FACES[SWARM_EMO_NEUTRAL], FACES[f.emotion], (uint32_t)(ch * 256u / 8u));
    /* the expression follows the charge; the colour names the feeling sooner,
     * so even a faint feeling is recognisable at status-bar size */
    uint32_t k = f.intensity > SWARM_EMO_MAX_INTENSITY ? SWARM_EMO_MAX_INTENSITY : f.intensity;
    out.color = zui_mix(FACES[SWARM_EMO_NEUTRAL].color, FACES[f.emotion].color, k * 255u / 5u);
    return out;
}

uint32_t zui_emote_ease(uint32_t t) {
    if (t >= 256u) return 256u;
    return (t * t * (768u - 2u * t)) / 65536u;
}

void zui_emote_init(zui_emote_t *m, swarm_feeling_t f, uint32_t now_ms) {
    if (!m) return;
    m->from = m->to = zui_face_feeling(f);
    m->start_ms = now_ms;
}

zui_face_t zui_emote_now(const zui_emote_t *m, uint32_t now_ms, int reduce_motion) {
    uint32_t el = now_ms - m->start_ms;
    if (reduce_motion || el >= ZUI_EMOTE_MORPH_MS) return m->to;
    return zui_face_lerp(m->from, m->to, zui_emote_ease(el * 256u / ZUI_EMOTE_MORPH_MS));
}

void zui_emote_set(zui_emote_t *m, swarm_feeling_t f, uint32_t now_ms) {
    if (!m) return;
    zui_face_t target = zui_face_feeling(f);
    zui_face_t here = zui_emote_now(m, now_ms, 0);
    m->from = here;
    m->to = target;
    m->start_ms = now_ms;
}

lpx_t zui_emote_tag(swarm_feeling_t f, zui_health_t h) {
    const swarm_emotion_profile_t *p = swarm_emotion_profile(f.emotion);
    int v = p ? p->valence : 0;
    if (f.intensity == 0) v = 0;
    uint8_t base = v > 0 ? LPX_B_TRUE : v < 0 ? LPX_B_FALSE : LPX_B_NEITHER;
    uint8_t pol = h == ZUI_HEALTH_OK ? LPX_P_NEGENTROPIC
                : h == ZUI_HEALTH_FAILING ? LPX_P_ENTROPIC : LPX_P_NEUTRAL;
    uint8_t ph = h == ZUI_HEALTH_STOPPED ? LPX_PH_DISSOLVING
               : f.intensity == 0 ? LPX_PH_POTENTIAL
               : f.intensity <= 2 ? LPX_PH_EMERGING : LPX_PH_MANIFEST;
    return lpx_make(base, pol, ph);
}

/* ---- anti-aliased primitives in Q8 pixel coordinates ---------------------- */
static void ellipse(zui_surface_t *s, int32_t cx, int32_t cy, int32_t rx, int32_t ry,
                    uint32_t rgb, uint32_t a) {
    if (rx < 64) rx = 64;
    if (ry < 32) ry = 32;
    int32_t x0 = (cx - rx) >> 8, x1 = (cx + rx) >> 8, y0 = (cy - ry) >> 8, y1 = (cy + ry) >> 8;
    int64_t rx2 = (int64_t)rx * rx, ry2 = (int64_t)ry * ry, lim = rx2 * ry2;
    for (int32_t y = y0; y <= y1; y++)
        for (int32_t x = x0; x <= x1; x++) {
            uint32_t k = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    int64_t dx = ((int64_t)x * 256) + 32 + i * 64 - cx;
                    int64_t dy = ((int64_t)y * 256) + 32 + j * 64 - cy;
                    if (dx * dx * ry2 + dy * dy * rx2 <= lim) k++;
                }
            if (k) zui_blend_px(s, x, y, rgb, (a * k) / 16u);
        }
}

static void dot(zui_surface_t *s, int32_t cx, int32_t cy, int32_t r, uint32_t rgb, uint32_t a) {
    ellipse(s, cx, cy, r, r, rgb, a);
}

/* quadratic curve p0 -> p1 bent toward c, stamped with round dots */
static void curve(zui_surface_t *s, int32_t x0, int32_t y0, int32_t cxq, int32_t cyq,
                  int32_t x1, int32_t y1, int32_t thick, uint32_t rgb, uint32_t a) {
    int32_t len = (x1 > x0 ? x1 - x0 : x0 - x1) + (y1 > y0 ? y1 - y0 : y0 - y1);
    int32_t n = len / (thick / 3 + 1) + 4;
    if (n > 96) n = 96;
    for (int32_t i = 0; i <= n; i++) {
        int64_t t = ((int64_t)i << 8) / n, u = 256 - t;
        int32_t px = (int32_t)((u * u * x0 + 2 * u * t * cxq + t * t * x1) >> 16);
        int32_t py = (int32_t)((u * u * y0 + 2 * u * t * cyq + t * t * y1) >> 16);
        dot(s, px, py, thick / 2, rgb, a);
    }
}

void zui_face_draw(zui_surface_t *s, const zui_face_t *f, zui_health_t h,
                   int32_t cx_px, int32_t cy_px, int32_t r_px, uint32_t tick) {
    if (!s || !f || r_px < 4) return;
    int32_t cx = cx_px * 256, cy = cy_px * 256, r = r_px * 256;
    uint32_t col = f->color, ink = INK;
    if (h == ZUI_HEALTH_STOPPED) { col = zui_mix(col, ZUI_FAINT, 170u); ink = zui_mix(ink, ZUI_FAINT, 90u); }

    /* health ring, outside the face */
    if (h == ZUI_HEALTH_SLOW || h == ZUI_HEALTH_FAILING) {
        int32_t r1 = r + r / 8 + 128, r2 = r1 + (r / 10 > 256 ? r / 10 : 256);
        uint32_t rc = h == ZUI_HEALTH_FAILING ? ZUI_ALERT : ZUI_GOLD;
        int32_t bx0 = (cx - r2) >> 8, bx1 = (cx + r2) >> 8, by0 = (cy - r2) >> 8, by1 = (cy + r2) >> 8;
        for (int32_t y = by0; y <= by1; y++)
            for (int32_t x = bx0; x <= bx1; x++) {
                uint32_t k = 0;
                int32_t mx = (x * 256) + 128 - cx, my = (y * 256) + 128 - cy;
                if (h == ZUI_HEALTH_SLOW) {   /* dashes that turn slowly */
                    int32_t ax = mx < 0 ? -mx : mx, ay = my < 0 ? -my : my;
                    int32_t pa = ax + ay ? (my * 256) / (ax + ay) : 0;   /* -256..256 */
                    int32_t q = (mx >= 0 ? 256 - pa : 768 + pa);          /* 0..1024 */
                    if ((((uint32_t)q + tick * 16u) / 64u) & 1u) continue;
                }
                for (int j = 0; j < 4; j++)
                    for (int i = 0; i < 4; i++) {
                        int64_t dx = ((int64_t)x * 256) + 32 + i * 64 - cx;
                        int64_t dy = ((int64_t)y * 256) + 32 + j * 64 - cy;
                        int64_t d2 = dx * dx + dy * dy;
                        if (d2 >= (int64_t)r1 * r1 && d2 <= (int64_t)r2 * r2) k++;
                    }
                if (k) zui_blend_px(s, x, y, rc, (255u * k) / 16u);
            }
    }

    /* the face: a carved rim and the body */
    dot(s, cx, cy, r, zui_mix(col, ZUI_VOID, 110u), 255u);
    dot(s, cx, cy, r - (r / 12 > 256 ? r / 12 : 256), col, 255u);

    int32_t ex = r * 36 / 100, ey = cy - r * 14 / 100, er = r * 13 / 100;
    int32_t th = r / 11 > 220 ? r / 11 : 220;     /* stroke thickness */

    /* blush under the eyes */
    if (f->blush > 0) {
        uint32_t a = (uint32_t)f->blush * 150u / 256u;
        ellipse(s, cx - r * 56 / 100, cy + r * 16 / 100, r * 19 / 100, r * 11 / 100, 0xFF6F8Eu, a);
        ellipse(s, cx + r * 56 / 100, cy + r * 16 / 100, r * 19 / 100, r * 11 / 100, 0xFF6F8Eu, a);
    }

    /* eyes: round when open, thin when lidded, smiling arcs when squinting */
    for (int side = -1; side <= 1; side += 2) {
        int32_t x = cx + side * ex;
        int32_t open = (int32_t)f->eye_open * (256 - f->squint) / 256;
        int32_t ry = er * 13 / 10 * open / 256;
        if (f->squint < 200) {
            uint32_t a = 255u - (uint32_t)f->squint * 255u / 256u;
            if (ry * 4 < er) {   /* shut lids: a soft downward curve */
                curve(s, x - er, ey, x, ey + er, x + er, ey, th, ink, a);
            } else {
                ellipse(s, x, ey, er, ry, ink, a);
            }
        }
        if (f->squint > 0)
            curve(s, x - er * 12 / 10, ey + er / 3, x, ey - er * 14 / 10, x + er * 12 / 10, ey + er / 3,
                  th, ink, (uint32_t)f->squint * 255u / 256u);
    }

    /* brows: only as strong as the feeling that needs them */
    {
        int32_t tilt = f->brow_tilt, raise = f->brow_raise;
        int32_t base = ey - er * 22 / 10;
        for (int side = -1; side <= 1; side += 2) {
            int32_t outer = cx + side * (ex + er * 12 / 10), inner = cx + side * (ex - er * 10 / 10);
            int32_t lift = side > 0 ? raise * er * 12 / 10 / 256 : 0;
            int32_t mag = tilt < 0 ? -tilt : tilt;
            if (side > 0 && raise > mag) mag = raise;
            if (mag < 8) continue;
            int32_t yi = base - tilt * er * 9 / 10 / 256 - lift;
            int32_t yo = base + tilt * er * 3 / 10 / 256 - lift;
            curve(s, outer, yo, (outer + inner) / 2, (yo + yi) / 2 - (side > 0 ? lift / 2 : 0),
                  inner, yi, th, ink, (uint32_t)(mag > 256 ? 256 : mag) * 255u / 256u);
        }
    }

    /* tear */
    if (f->tear > 0) {
        uint32_t a = (uint32_t)f->tear * 230u / 256u;
        int32_t tx = cx - ex - er / 2, ty = ey + er * 24 / 10;
        ellipse(s, tx, ty, er * 6 / 10, er * 8 / 10, ZUI_INFO, a);
        ellipse(s, tx, ty - er * 6 / 10, er * 3 / 10, er * 5 / 10, ZUI_INFO, a);
    }

    /* mouth: a curve whose bend is the feeling; an opening below it */
    {
        int32_t hw = r * 36 / 100, my = cy + r * 36 / 100 - f->mouth_curve * (r * 5 / 100) / 256;
        int32_t ctl = my + f->mouth_curve * (r * 34 / 100) / 256;
        int32_t oh = f->mouth_open * (r * 30 / 100) / 256;
        if (oh > 0) {   /* fill between the lip curve and a rounded lower lip */
            int32_t xa = (cx - hw) >> 8, xb = (cx + hw) >> 8;
            for (int32_t x = xa; x <= xb; x++) {
                int64_t t = (((int64_t)x * 256) + 128 - (cx - hw)) * 256 / (2 * hw);
                if (t < 0 || t > 256) continue;
                int64_t u = 256 - t;
                int32_t top = (int32_t)((u * u * my + 2 * u * t * ctl + t * t * my) >> 16);
                int32_t bot = top + (int32_t)(oh * 4 * t * u / 65536);
                for (int32_t y = top >> 8; y <= (bot >> 8) + 1; y++) {
                    uint32_t k = 0;
                    for (int j = 0; j < 4; j++) {
                        int32_t sy = (y * 256) + 32 + j * 64;
                        if (sy >= top && sy <= bot) k++;
                    }
                    if (k) zui_blend_px(s, x, y, ink, 255u * k / 4u);
                }
            }
        }
        curve(s, cx - hw, my, cx, ctl, cx + hw, my, th, ink, 255u);
    }
}
