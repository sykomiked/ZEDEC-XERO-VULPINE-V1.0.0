/* shimmer.c — ZXV desktop shimmer field. See shimmer.h. */
#include "shimmer.h"

/* Exact quarter-wave sine table: 65 entries of sin(i * (pi/2)/64) * 127.
 * A table beats an iterative rotation here: the Q14 rotation recurrence
 * accumulates rounding error over 256 steps and the vector visibly decays
 * (the first version of this file lost ~10% of amplitude by a quarter
 * turn). 65 bytes of .rodata buys exactness. Full turn by symmetry. */
static const int8_t s_quarter[65] = {
       0,    3,    6,    9,   12,   16,   19,   22,   25,   28,   31,   34,   37,
      40,   43,   46,   49,   51,   54,   57,   60,   63,   65,   68,   71,   73,
      76,   78,   81,   83,   85,   88,   90,   92,   94,   96,   98,  100,  102,
     104,  106,  107,  109,  111,  112,  113,  115,  116,  117,  118,  120,  121,
     122,  122,  123,  124,  125,  125,  126,  126,  126,  127,  127,  127,  127
};

int32_t shm_sin(uint32_t turn8) {
    uint32_t t = turn8 & SHM_LUT_MASK;      /* 0..255 = one full turn */
    uint32_t q = t >> 6;                    /* quadrant 0..3 */
    uint32_t i = t & 63;                    /* index within the quadrant */
    switch (q) {
        case 0: return  s_quarter[i];
        case 1: return  s_quarter[64 - i];
        case 2: return -s_quarter[i];
        default:return -s_quarter[64 - i];
    }
}

void shimmer_init(shimmer_t *s, uint32_t width, uint32_t height, uint32_t seed) {
    if (!s) return;

    s->width = width ? width : 1;
    s->height = height ? height : 1;
    s->phase = 0;
    s->frames = 0;

    /* Three travelling waves at mutually incommensurate angles/rates.
     * The prime-ish spacings keep the sum from repeating on any short
     * period, so the surface never looks like a loop. `seed` rotates the
     * whole set so two shimmering surfaces don't move in lockstep. */
    uint32_t k = seed * 2654435761u;         /* Knuth mix, decorrelates */
    s->ax[0] =  3 + (int32_t)((k >>  0) & 1);
    s->ay[0] =  5 + (int32_t)((k >>  3) & 1);
    s->w[0]  =  2;
    s->ax[1] =  7 + (int32_t)((k >>  6) & 1);
    s->ay[1] = -3 + (int32_t)((k >>  9) & 1);
    s->w[1]  =  3;
    s->ax[2] = -11 + (int32_t)((k >> 12) & 1);
    s->ay[2] =  9 + (int32_t)((k >> 15) & 1);
    s->w[2]  =  5;

    s->scale = 3;          /* broad desktop-scale ribbons */
    s->warp = 20;          /* wavefront bending; 0 = straight plaid */
    s->amplitude = 34;     /* a hint of movement, not a light show */
    s->glint = 228;        /* only the rarest crests catch gold */
}

void shimmer_advance(shimmer_t *s) {
    if (!s) return;
    s->phase++;            /* one causal step; no clock is consulted */
    s->frames++;
}

/* Raw field value on the coarse lattice (integer lattice coordinates). */
static int32_t field_at(const shimmer_t *s, int32_t lx, int32_t ly) {
    /* Domain warp: displace the sample point by a slow low-frequency
     * wave so the interference bends into caustic ribbons instead of a
     * regular grid. This is the single trick that makes it read as
     * light on water rather than as a test pattern. */
    int32_t wx = (shm_sin((uint32_t)((ly * 2 + (int32_t)s->phase))) * s->warp) >> 7;
    int32_t wy = (shm_sin((uint32_t)((lx * 2 - (int32_t)s->phase))) * s->warp) >> 7;
    int32_t px = lx + wx;
    int32_t py = ly + wy;

    int32_t acc = 0;
    for (int i = 0; i < 3; i++) {
        int32_t arg = px * s->ax[i] + py * s->ay[i]
                    + (int32_t)s->phase * s->w[i];
        acc += shm_sin((uint32_t)arg);
    }
    /* acc in [-381, 381] -> 0..255 */
    int32_t v = ((acc + 381) * 255) / 762;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return v;
}

uint8_t shimmer_sample(const shimmer_t *s, int32_t x, int32_t y) {
    if (!s) return 0;

    /* The field is evaluated on a coarse lattice (2^scale pixels) so the
     * ribbons are desktop-scale, then BILINEARLY INTERPOLATED back to
     * pixel resolution. Without the interpolation the shift quantises
     * the field into visible 2^scale blocks; with it the gradient is
     * smooth and the surface reads as light rather than as tiles. */
    uint32_t sc = s->scale;
    int32_t lx = x >> sc, ly = y >> sc;
    int32_t fx = x & ((1 << sc) - 1);       /* fractional position 0..2^sc-1 */
    int32_t fy = y & ((1 << sc) - 1);

    int32_t v00 = field_at(s, lx,     ly);
    int32_t v10 = field_at(s, lx + 1, ly);
    int32_t v01 = field_at(s, lx,     ly + 1);
    int32_t v11 = field_at(s, lx + 1, ly + 1);

    int32_t top = v00 + (((v10 - v00) * fx) >> sc);
    int32_t bot = v01 + (((v11 - v01) * fx) >> sc);
    int32_t v   = top + (((bot - top) * fy) >> sc);

    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

/* ARGB helpers kept local so this module has no dependencies. */
static uint32_t argb(uint32_t a, int32_t r, int32_t g, int32_t b) {
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return (a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t shimmer_shade(const shimmer_t *s, uint32_t base, int32_t x, int32_t y) {
    if (!s) return base;
    uint32_t a = (base >> 24) & 0xFF;
    int32_t r = (int32_t)((base >> 16) & 0xFF);
    int32_t g = (int32_t)((base >>  8) & 0xFF);
    int32_t b = (int32_t)( base        & 0xFF);

    int32_t f = (int32_t)shimmer_sample(s, x, y);   /* 0..255 */
    int32_t c = f - 128;                            /* -128..127, signed lift */

    /* Base lift: a gentle brightening/darkening of the ground. Scaled by
     * amplitude so the default is a hint rather than a strobe. */
    int32_t lift = (c * (int32_t)s->amplitude) >> 8;
    r += lift; g += lift; b += lift;

    /* Gold glint: only the crests above `glint` pick up warm light, so
     * the surface sparkles occasionally instead of glowing everywhere. */
    if (f > (int32_t)s->glint) {
        int32_t over = f - (int32_t)s->glint;             /* 0..(255-glint) */
        int32_t k = (over * 255) / (255 - (int32_t)s->glint + 1);
        /* pull toward gold #D4AF37 */
        r += ((0xD4 - r) * k) >> 9;
        g += ((0xAF - g) * k) >> 9;
        b += ((0x37 - b) * k) >> 9;
    }
    return argb(a, r, g, b);
}

void shimmer_set_amplitude(shimmer_t *s, uint8_t amplitude) {
    if (s) s->amplitude = amplitude;
}
void shimmer_set_glint(shimmer_t *s, uint8_t glint) {
    if (s) s->glint = glint;
}
void shimmer_set_scale(shimmer_t *s, uint8_t scale) {
    if (s) s->scale = (scale > 8) ? 8 : scale;
}
