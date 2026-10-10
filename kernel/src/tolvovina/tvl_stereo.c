/* tvl_stereo.c — TOL VOVINA UPAAH LOT: tri-space stereo depth. See tvl_stereo.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Integer only. No float, no libc, no allocation. Every colour comes from a
 * theme slot; there is not one magic RGB literal below.
 */
#include "tvl_stereo.h"
#include "emu/holo.h"        /* holo_shade — chromostereopsis, already proven */

/* ------------------------------------------------------------------------
 * small integer helpers
 * --------------------------------------------------------------------- */

static inline int32_t clamp255(int32_t v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

/* Half a disparity, truncated TOWARD ZERO so that half(-d) == -half(d) for
 * every d. That exact antisymmetry is what makes S+ and S- mirror depths
 * rather than two nearly-equal effects. The cost is that an odd disparity
 * loses its last pixel: the effective separation is always 2*half(d), so a
 * magnitude of 1 displaces nothing (the chromostereopsis cue still fires at
 * full strength, because it reads the unhalved disparity). Written as a shift
 * with an explicit sign fold rather than `/2` because a signed divide is a
 * libgcc call on the 32-bit targets this tree also builds for. */
static inline int32_t half_disp(int32_t d) {
    return (d < 0) ? -((-d) >> 1) : (d >> 1);
}

/* Blend a -> b by k in 0..255, EXACT at both endpoints. k is widened to 0..256
 * so that k==255 yields b bit-for-bit; a plain (d*k)/255 would be one LSB shy
 * on some channels and the "full separation" setting would never actually be
 * full. */
static inline int32_t lerp8(int32_t a, int32_t b, int32_t k256) {
    return a + (((b - a) * k256) >> 8);
}

static inline int32_t widen_k(uint8_t k) {
    return (int32_t)k + (int32_t)(k >> 7);      /* 0->0, 255->256 */
}

static inline uint32_t pack_argb(uint32_t a, int32_t r, int32_t g, int32_t b) {
    return (a << 24) | ((uint32_t)clamp255(r) << 16)
                     | ((uint32_t)clamp255(g) << 8)
                     |  (uint32_t)clamp255(b);
}

static inline int32_t chan(uint32_t argb, int i) {   /* i: 0=R 1=G 2=B */
    return (int32_t)((argb >> (16 - 8 * i)) & 0xFFu);
}

/* ------------------------------------------------------------------------
 * setup — the separation axis is DERIVED from the theme, never written down
 * --------------------------------------------------------------------- */

/* anchor_w[c] is EMBER's share of channel c against AZURE's. With the house
 * palette (ember CC3300, azure 3388CC) this lands on roughly (204, 69, 0):
 * a warm anchor half against a cool complement, i.e. the classic red/cyan
 * separation axis — arrived at from the palette rather than asserted.
 * Re-theme the desktop and the axis moves with it.
 *
 * The variable divide here is a setup-time cost, once per theme change, never
 * per pixel; the per-pixel path below divides only by the constant 255. */
static void derive_axis(tvl_stereo_t *s, const theme_t *t) {
    uint32_t warm = theme_get(t, THEME_EMBER);
    uint32_t cool = theme_get(t, THEME_AZURE);
    for (int c = 0; c < 3; c++) {
        int32_t wv = chan(warm, c);
        int32_t cv = chan(cool, c);
        int32_t sum = wv + cv;
        /* A channel neither slot uses carries no separating information, so it
         * splits evenly — half to each eye, contributing brightness but no
         * depth. That is the honest answer, not a guess at a hue. */
        s->anchor_w[c] = sum ? (uint8_t)((wv * 255) / sum) : (uint8_t)128;
    }
    s->glow_rgb = theme_get(t, THEME_ACCENT);   /* what bright things bloom to */
    s->void_rgb = theme_get(t, THEME_VOID);     /* what dark things crush to   */
}

void tvl_stereo_set_theme(tvl_stereo_t *s, const theme_t *theme) {
    if (!s) return;
    if (theme) { derive_axis(s, theme); return; }
    /* theme_get(NULL, ...) returns 0 for every slot, which would silently
     * produce a black palette and a degenerate axis. So a NULL theme means
     * "the default house palette", built locally. */
    theme_t local;
    theme_init(&local);
    derive_axis(s, &local);
}

void tvl_stereo_init(tvl_stereo_t *s, const theme_t *theme) {
    if (!s) return;

    s->separation = 255;    /* full complementary separation by default */
    s->chroma_k   = 24;     /* a supporting warm/cool cue, not the main event */
    s->emissive   = 48;     /* ref: the emissive high-contrast look; themed    */
    s->mode       = TVL_STEREO_FULL;
    s->field      = 0;
    s->pixels     = 0;
    s->anchor_w[0] = s->anchor_w[1] = s->anchor_w[2] = 128;

    tvl_stereo_set_theme(s, theme);
}

void tvl_stereo_set_field(tvl_stereo_t *s, const shimmer_t *field) {
    if (s) s->field = field;    /* borrowed; never advanced from here */
}
void tvl_stereo_set_mode(tvl_stereo_t *s, tvl_stereo_mode_t mode) {
    if (s && (unsigned)mode <= (unsigned)TVL_STEREO_FLAT) s->mode = mode;
}
void tvl_stereo_set_separation(tvl_stereo_t *s, uint8_t separation) {
    if (s) s->separation = separation;
}
void tvl_stereo_set_emissive(tvl_stereo_t *s, uint8_t emissive) {
    if (s) s->emissive = emissive;
}

uint8_t tvl_stereo_axis_spread(const tvl_stereo_t *s) {
    if (!s) return 0;
    uint8_t lo = s->anchor_w[0], hi = s->anchor_w[0];
    for (int c = 1; c < 3; c++) {
        if (s->anchor_w[c] < lo) lo = s->anchor_w[c];
        if (s->anchor_w[c] > hi) hi = s->anchor_w[c];
    }
    return (uint8_t)(hi - lo);
}

/* ------------------------------------------------------------------------
 * THE SEMANTIC MAPPING — tri_role_t -> signed disparity
 * --------------------------------------------------------------------- */

int32_t tvl_disparity(tri_role_t role, uint32_t magnitude) {
    int32_t m = (magnitude > (uint32_t)TVL_DISPARITY_MAX)
              ? (int32_t)TVL_DISPARITY_MAX : (int32_t)magnitude;
    switch (role) {
    case TRI_POSITIVE: return  m;   /* S+ what it DOES        — protrudes  */
    case TRI_NEGATIVE: return -m;   /* S- its inverse/undo    — recedes    */
    case TRI_NEUTRAL:  return  0;   /* S0 the unresolved rest — EXACTLY 0  */
    default:           return  0;   /* an unknown role is HELD, not asserted */
    }
    /* TRI_NEUTRAL returns 0 for EVERY magnitude, including the largest one a
     * caller can express. This is the contract, not an artefact: S0 is the
     * remainder nobody has decided yet, so it sits at the glass with zero
     * parallax. A held thing that drifted even one pixel forward would be
     * rendering a decision the triad never made — the visual equivalent of
     * shipping the positive half and calling the triad complete. */
}

int32_t tvl_depth_z(tri_role_t role, uint32_t magnitude) {
    /* Same sign discipline, deliberately the same number: the warm/cool tint
     * and the channel displacement must never disagree about which way a
     * thing faces, or the two cues fight and the depth stops fusing. */
    return tvl_disparity(role, magnitude);
}

/* ------------------------------------------------------------------------
 * channel separation — the flatness proof lives here
 * --------------------------------------------------------------------- */

void tvl_split(const tvl_stereo_t *s, uint32_t argb,
               uint32_t *anchor_out, uint32_t *complement_out) {
    uint32_t a  = (argb >> 24) & 0xFFu;
    uint32_t an = a << 24;      /* the anchor half carries the alpha ... */
    uint32_t co = 0;            /* ... the complement half carries none  */

    for (int c = 0; c < 3; c++) {
        int32_t v = chan(argb, c);
        int32_t w = s ? (int32_t)s->anchor_w[c] : 128;
        /* THE COMPLEMENT IS THE REMAINDER, not a second independently rounded
         * weighting. p <= v always (w <= 255), and p + (v - p) == v exactly,
         * for all 256 values of v and all 256 values of w. That identity is
         * what makes zero disparity reproduce the source byte-for-byte, so it
         * must never be "improved" into two symmetric multiplies. */
        int32_t p = (v * w) / 255;
        an |= (uint32_t)p        << (16 - 8 * c);
        co |= (uint32_t)(v - p)  << (16 - 8 * c);
    }
    if (anchor_out)     *anchor_out = an;
    if (complement_out) *complement_out = co;
}

uint32_t tvl_merge(uint32_t anchor, uint32_t complement) {
    uint32_t a = ((anchor >> 24) & 0xFFu) + ((complement >> 24) & 0xFFu);
    if (a > 255u) a = 255u;
    return pack_argb(a,
                     chan(anchor, 0) + chan(complement, 0),
                     chan(anchor, 1) + chan(complement, 1),
                     chan(anchor, 2) + chan(complement, 2));
}

/* ------------------------------------------------------------------------
 * per-pixel shaping stages (each an exact identity at strength 0)
 * --------------------------------------------------------------------- */

/* Emissive / high-contrast shaping, ref: the emissive red-on-black flavour.
 * Bright pixels bloom toward THEME_ACCENT; dark pixels crush toward THEME_VOID.
 * Both targets are theme slots, so a user who re-themes gets a different glow
 * without touching this file, and a user who wants no glow sets emissive 0 and
 * gets the identity back exactly. */
static uint32_t emissive_px(const tvl_stereo_t *s, uint32_t argb) {
    if (!s || s->emissive == 0) return argb;

    uint32_t a = (argb >> 24) & 0xFFu;
    int32_t r = chan(argb, 0), g = chan(argb, 1), b = chan(argb, 2);

    /* Rec.601-ish luma in 8.8; integer weights sum to 256. */
    int32_t lum = (r * 77 + g * 150 + b * 29) >> 8;      /* 0..255 */
    int32_t up  = ((int32_t)s->emissive * lum) >> 8;         /* bloom share */
    int32_t dn  = ((int32_t)s->emissive * (255 - lum)) >> 8; /* crush share */

    int32_t gr = chan(s->glow_rgb, 0), gg = chan(s->glow_rgb, 1), gb = chan(s->glow_rgb, 2);
    int32_t vr = chan(s->void_rgb, 0), vg = chan(s->void_rgb, 1), vb = chan(s->void_rgb, 2);

    r += ((gr - r) * up) >> 8;  g += ((gg - g) * up) >> 8;  b += ((gb - b) * up) >> 8;
    r += ((vr - r) * dn) >> 8;  g += ((vg - g) * dn) >> 8;  b += ((vb - b) * dn) >> 8;

    return pack_argb(a, r, g, b);
}

/* Chromostereopsis, delegated whole to holo_shade rather than reimplemented.
 * v = 0 because shimmer owns the valence channel in this pipeline; depth_z is
 * the disparity, so the tint and the displacement always agree in sign.
 * holo_shade is an exact identity at v == 0 and depth_z == 0. */
static uint32_t chroma_px(const tvl_stereo_t *s, uint32_t argb, int32_t depth_z) {
    if (!s || s->chroma_k == 0 || depth_z == 0) return argb;
    uint32_t a = argb & 0xFF000000u;
    return a | holo_shade(argb & 0x00FFFFFFu, 0, depth_z, 8, (int32_t)s->chroma_k);
}

/* The living-surface modulator. shimmer owns lightness lift and the gold
 * glint; we never duplicate that arithmetic, we call it. With no field
 * attached this is the identity, so the module is fully usable standalone. */
static inline uint32_t surface_px(const tvl_stereo_t *s, uint32_t argb,
                                  int32_t x, int32_t y) {
    if (!s || !s->field) return argb;
    return shimmer_shade(s->field, argb, x, y);
}

/* ------------------------------------------------------------------------
 * compositing
 * --------------------------------------------------------------------- */

uint32_t tvl_stereo_flat_px(const tvl_stereo_t *s, uint32_t base_argb,
                            int32_t x, int32_t y) {
    return emissive_px(s, surface_px(s, base_argb, x, y));
}

uint32_t tvl_stereo_compose(const tvl_stereo_t *s,
                            uint32_t center_argb,
                            uint32_t anchor_argb,
                            uint32_t complement_argb,
                            int32_t  disparity) {
    if (!s) return center_argb;

    /* The reference the stereo result is always measured against. */
    uint32_t flat = emissive_px(s, center_argb);

    if (s->mode == TVL_STEREO_FLAT || s->separation == 0) return flat;

    uint32_t merged;
    if (s->mode == TVL_STEREO_CHROMA) {
        merged = center_argb;                  /* depth by tint alone */
    } else {
        uint32_t an, co, dummy;
        tvl_split(s, anchor_argb,     &an,    &dummy);   /* near eye's half  */
        tvl_split(s, complement_argb, &dummy, &co);      /* far  eye's half  */
        merged = tvl_merge(an, co);
        /* At disparity 0 the caller hands us the same colour three times, so
         * this collapses to tvl_merge(tvl_split(c)) == c. Flatness is the
         * split identity, not a tolerance. */
        merged = (merged & 0x00FFFFFFu) | (center_argb & 0xFF000000u);
    }

    uint32_t stereo = emissive_px(s, chroma_px(s, merged, disparity));

    if (stereo == flat) return flat;           /* nothing to blend */

    int32_t k = widen_k(s->separation);
    uint32_t a = (center_argb >> 24) & 0xFFu;
    return pack_argb(a,
        lerp8(chan(flat, 0), chan(stereo, 0), k),
        lerp8(chan(flat, 1), chan(stereo, 1), k),
        lerp8(chan(flat, 2), chan(stereo, 2), k));
}

static inline int32_t clamp_disp(int32_t d) {
    if (d >  TVL_DISPARITY_MAX) return  TVL_DISPARITY_MAX;
    if (d < -TVL_DISPARITY_MAX) return -TVL_DISPARITY_MAX;
    return d;
}

uint32_t tvl_stereo_row(tvl_stereo_t *s,
                        const uint32_t *src, const int16_t *disp,
                        uint32_t *dst, uint32_t width,
                        int32_t y, int32_t disparity) {
    if (!s || !src || !dst || width == 0) return 0;
    if (src == dst) return 0;   /* the eye offsets read behind the write head */

    for (uint32_t x = 0; x < width; x++) {
        int32_t d = clamp_disp(disp ? (int32_t)disp[x] : disparity);
        int32_t h = half_disp(d);

        int32_t xi = (int32_t)x;
        int32_t ax = xi - h, cx = xi + h;       /* opposite eye offsets */
        int32_t last = (int32_t)width - 1;
        if (ax < 0) ax = 0; else if (ax > last) ax = last;
        if (cx < 0) cx = 0; else if (cx > last) cx = last;

        /* Shimmer is a property of the SURFACE, so each half is modulated at
         * the position it was sampled from, not at the output pixel. At d == 0
         * all three positions coincide and the three calls agree exactly. */
        uint32_t center = surface_px(s, src[x],           xi, y);
        uint32_t anchor = surface_px(s, src[(uint32_t)ax], ax, y);
        uint32_t compl_ = surface_px(s, src[(uint32_t)cx], cx, y);

        dst[x] = tvl_stereo_compose(s, center, anchor, compl_, d);
    }
    s->pixels += width;
    return width;
}

uint32_t tvl_stereo_blit(tvl_stereo_t *s,
                         const uint32_t *src, uint32_t *dst,
                         uint32_t w, uint32_t h,
                         tri_role_t role, uint32_t magnitude) {
    if (!s || !src || !dst || w == 0 || h == 0) return 0;
    int32_t d = tvl_disparity(role, magnitude);
    uint32_t n = 0;
    for (uint32_t y = 0; y < h; y++)
        n += tvl_stereo_row(s, src + (uint64_t)y * w, 0, dst + (uint64_t)y * w,
                            w, (int32_t)y, d);
    return n;
}

uint32_t tvl_stereo_blit_flat(tvl_stereo_t *s,
                              const uint32_t *src, uint32_t *dst,
                              uint32_t w, uint32_t h) {
    if (!s || !src || !dst || w == 0 || h == 0) return 0;
    for (uint32_t y = 0; y < h; y++) {
        const uint32_t *sr = src + (uint64_t)y * w;
        uint32_t *dr = dst + (uint64_t)y * w;
        for (uint32_t x = 0; x < w; x++)
            dr[x] = tvl_stereo_flat_px(s, sr[x], (int32_t)x, (int32_t)y);
    }
    s->pixels += (uint64_t)w * h;
    return w * h;
}

/* ------------------------------------------------------------------------
 * selfcheck
 * --------------------------------------------------------------------- */

#define TVL_CHK_W  24
#define TVL_CHK_H  3
static uint32_t g_chk_src[TVL_CHK_W * TVL_CHK_H];
static uint32_t g_chk_a[TVL_CHK_W * TVL_CHK_H];
static uint32_t g_chk_b[TVL_CHK_W * TVL_CHK_H];

int tvl_stereo_selfcheck_detail(uint32_t *fail_mask) {
    uint32_t fail = 0;
    tvl_stereo_t s;
    tvl_stereo_init(&s, 0);          /* default house palette */

    /* ---- 1. S0 IS EXACTLY ZERO, at every magnitude a caller can express --- */
    static const uint32_t mags[] = { 0, 1, 2, 7, 32, 63, 64, 65, 1000, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof(mags) / sizeof(mags[0]); i++) {
        if (tvl_disparity(TRI_NEUTRAL, mags[i]) != 0) fail |= TVL_CHK_NEUTRAL_ZERO;
        if (tvl_depth_z (TRI_NEUTRAL, mags[i]) != 0) fail |= TVL_CHK_NEUTRAL_ZERO;
    }

    /* ---- 2. S+ and S- are equal-and-opposite ----------------------------- */
    for (unsigned i = 0; i < sizeof(mags) / sizeof(mags[0]); i++) {
        int32_t p = tvl_disparity(TRI_POSITIVE, mags[i]);
        int32_t n = tvl_disparity(TRI_NEGATIVE, mags[i]);
        if (p != -n) fail |= TVL_CHK_MIRROR;
        if (mags[i] > 0 && (p <= 0 || n >= 0)) fail |= TVL_CHK_MIRROR;   /* and non-degenerate */
        if (half_disp(p) != -half_disp(n)) fail |= TVL_CHK_MIRROR;       /* through the halving too */
    }
    if (tvl_disparity(TRI_POSITIVE, 0) != 0) fail |= TVL_CHK_MIRROR;

    /* ---- 3. split/merge round-trips EVERY colour, at every weight -------- */
    static const uint8_t wsets[][3] = {
        { 0, 0, 0 }, { 255, 255, 255 }, { 128, 128, 128 },
        { 204, 69, 0 }, { 1, 254, 7 }, { 255, 0, 128 }
    };
    for (unsigned k = 0; k <= sizeof(wsets) / sizeof(wsets[0]); k++) {
        tvl_stereo_t t = s;                       /* k == 0 keeps the themed axis */
        if (k > 0) { t.anchor_w[0] = wsets[k-1][0];
                     t.anchor_w[1] = wsets[k-1][1];
                     t.anchor_w[2] = wsets[k-1][2]; }
        for (uint32_t v = 0; v < 256; v++) {
            /* every value on every channel, plus three alphas */
            uint32_t cols[4] = {
                (v << 16) | (v << 8) | v,
                0xFF000000u | (v << 16) | ((255u - v) << 8) | ((v * 3u) & 0xFFu),
                0x80000000u | ((255u - v) << 16) | (v << 8) | (v >> 1),
                (v << 24)  | ((v ^ 0x5Au) << 16) | ((v ^ 0xA5u) << 8) | (v ^ 0x3Cu)
            };
            for (int c = 0; c < 4; c++) {
                uint32_t an, co;
                tvl_split(&t, cols[c], &an, &co);
                if (tvl_merge(an, co) != cols[c]) fail |= TVL_CHK_ROUNDTRIP;
            }
        }
    }

    /* ---- build a test surface: a hard step edge plus a varied field ------ */
    const uint32_t X = 0xFFA02010u;   /* left  of the step */
    const uint32_t Y = 0xFF40C8C8u;   /* right of the step; per-channel sums < 256 */
    for (uint32_t y = 0; y < TVL_CHK_H; y++)
        for (uint32_t x = 0; x < TVL_CHK_W; x++)
            g_chk_src[y * TVL_CHK_W + x] = (x < TVL_CHK_W / 2) ? X : Y;

    /* ---- 4. TRI_NEUTRAL is BYTE-IDENTICAL to the non-stereo path --------- */
    /* once with no shimmer field ... */
    tvl_stereo_blit(&s, g_chk_src, g_chk_a, TVL_CHK_W, TVL_CHK_H, TRI_NEUTRAL, 64);
    tvl_stereo_blit_flat(&s, g_chk_src, g_chk_b, TVL_CHK_W, TVL_CHK_H);
    for (uint32_t i = 0; i < TVL_CHK_W * TVL_CHK_H; i++)
        if (g_chk_a[i] != g_chk_b[i]) fail |= TVL_CHK_FLAT_IDENTICAL;

    /* ... and again WITH one attached, because the shimmer path samples three
     * positions per pixel and they must still coincide at zero parallax. */
    shimmer_t fld;
    shimmer_init(&fld, TVL_CHK_W, TVL_CHK_H, 7u);
    shimmer_advance(&fld); shimmer_advance(&fld); shimmer_advance(&fld);
    tvl_stereo_set_field(&s, &fld);
    tvl_stereo_blit(&s, g_chk_src, g_chk_a, TVL_CHK_W, TVL_CHK_H, TRI_NEUTRAL, 64);
    tvl_stereo_blit_flat(&s, g_chk_src, g_chk_b, TVL_CHK_W, TVL_CHK_H);
    for (uint32_t i = 0; i < TVL_CHK_W * TVL_CHK_H; i++)
        if (g_chk_a[i] != g_chk_b[i]) fail |= TVL_CHK_FLAT_IDENTICAL;
    tvl_stereo_set_field(&s, 0);

    /* ---- 5. a nonzero disparity actually does something ------------------ */
    /* A test that only proves flatness would pass on a module that does
     * nothing at all, so prove the other direction too. */
    tvl_stereo_blit(&s, g_chk_src, g_chk_a, TVL_CHK_W, TVL_CHK_H, TRI_POSITIVE, 8);
    tvl_stereo_blit_flat(&s, g_chk_src, g_chk_b, TVL_CHK_W, TVL_CHK_H);
    int differs = 0;
    for (uint32_t i = 0; i < TVL_CHK_W * TVL_CHK_H; i++)
        if (g_chk_a[i] != g_chk_b[i]) differs++;
    if (differs == 0) fail |= TVL_CHK_SEPARATES;

    /* ---- 6. +d and -d partition the two samples exactly ------------------ */
    /* With the shaping stages off, the S+ composite takes the anchor channels
     * from the left sample and the complement channels from the right; the S-
     * composite takes them the other way round. Nothing is created or lost in
     * the swap, so the two outputs must sum, channel by channel, to X + Y. */
    tvl_stereo_t raw = s;
    raw.emissive = 0; raw.chroma_k = 0; raw.separation = 255;
    tvl_stereo_blit(&raw, g_chk_src, g_chk_a, TVL_CHK_W, TVL_CHK_H, TRI_POSITIVE, 8);
    tvl_stereo_blit(&raw, g_chk_src, g_chk_b, TVL_CHK_W, TVL_CHK_H, TRI_NEGATIVE, 8);
    {
        /* the step sits at W/2, the eye offset is half(8) = 4 */
        uint32_t xi = TVL_CHK_W / 2;                 /* first pixel past the step */
        uint32_t i  = 1u * TVL_CHK_W + xi;           /* middle row                */
        uint32_t pa = g_chk_a[i], pb = g_chk_b[i];
        if (pa == pb) fail |= TVL_CHK_ANTISYMMETRY;  /* the eyes must disagree    */
        for (int c = 0; c < 3; c++)
            if (chan(pa, c) + chan(pb, c) != chan(X, c) + chan(Y, c))
                fail |= TVL_CHK_ANTISYMMETRY;
    }

    if (fail_mask) *fail_mask = fail;
    return fail == 0;
}

int tvl_stereo_selfcheck(void) {
    uint32_t m = 0;
    return tvl_stereo_selfcheck_detail(&m);
}
