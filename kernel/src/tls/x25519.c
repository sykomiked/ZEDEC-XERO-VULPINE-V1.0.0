/* x25519.c — Curve25519 (RFC 7748). See x25519.h.
 *
 * Field elements are 10 limbs alternating 26 and 25 bits, little-endian:
 *     x = f[0] + f[1]*2^26 + f[2]*2^51 + f[3]*2^77 + ... + f[9]*2^230
 * held in int64 so products accumulate without overflow before reduction.
 * This is the classic radix-2^25.5 representation; it is used because it
 * needs no 128-bit integer type and therefore builds for 32-bit targets.
 */
#include "x25519.h"

typedef int64_t fe[10];

static void fe_0(fe h) { for (int i = 0; i < 10; i++) h[i] = 0; }
static void fe_1(fe h) { fe_0(h); h[0] = 1; }
static void fe_copy(fe h, const fe f) { for (int i = 0; i < 10; i++) h[i] = f[i]; }
static void fe_add(fe h, const fe f, const fe g) { for (int i = 0; i < 10; i++) h[i] = f[i] + g[i]; }
static void fe_sub(fe h, const fe f, const fe g) { for (int i = 0; i < 10; i++) h[i] = f[i] - g[i]; }

/* Conditional swap driven by an arithmetic MASK, never a branch: the same
 * instructions execute whatever the secret bit is. */
static void fe_cswap(fe f, fe g, uint32_t b) {
    int64_t mask = -(int64_t)b;
    for (int i = 0; i < 10; i++) {
        int64_t x = (f[i] ^ g[i]) & mask;
        f[i] ^= x;
        g[i] ^= x;
    }
}

/* Floor-carry every limb into [0, 2^shift). Three passes, because folding the
 * top limb back into h[0] with the 2^255 == 19 identity can push h[0] out of
 * range again (and can make it negative when the top limb was negative).
 *
 * Subtraction uses a MULTIPLY, not a left shift: shifting a negative value
 * left is undefined behaviour in C, and UBSan flags it. The carries here are
 * genuinely negative whenever a preceding fe_sub produced a negative limb. */
static void fe_carry(fe h) {
    for (int r = 0; r < 3; r++) {
        int64_t c;
        for (int i = 0; i < 9; i++) {
            int shift = (i & 1) ? 25 : 26;
            c = h[i] >> shift;                       /* arithmetic = floor */
            h[i] -= c * ((int64_t)1 << shift);
            h[i + 1] += c;
        }
        c = h[9] >> 25;
        h[9] -= c * ((int64_t)1 << 25);
        h[0] += c * 19;          /* 2^255 == 19 (mod 2^255 - 19) */
    }
}

static void fe_mul(fe h, const fe f, const fe g) {
    /* Schoolbook with the 2^255=19 folding applied to the high half. The
     * odd-index cross terms pick up an extra factor of 2 because of the
     * 25/26-bit alternation. */
    int64_t t[19];
    for (int i = 0; i < 19; i++) t[i] = 0;
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            int64_t p = f[i] * g[j];
            if ((i & 1) && (j & 1)) p *= 2;
            t[i + j] += p;
        }
    }
    for (int i = 0; i < 9; i++) t[i] += 19 * t[i + 10];
    for (int i = 0; i < 10; i++) h[i] = t[i];
    fe_carry(h);
}

static void fe_sq(fe h, const fe f) { fe_mul(h, f, f); }

static void fe_mul121666(fe h, const fe f) {
    for (int i = 0; i < 10; i++) h[i] = f[i] * 121666;
    fe_carry(h);
}

/* Inversion by Fermat: x^(p-2) with p = 2^255-19, via the standard
 * addition chain (11 squarings of chained blocks). */
static void fe_invert(fe out, const fe z) {
    fe z2, z9, z11, z2_5_0, z2_10_0, z2_20_0, z2_50_0, z2_100_0, t0, t1;
    int i;
    fe_sq(z2, z);
    fe_sq(t1, z2); fe_sq(t0, t1); fe_mul(z9, t0, z);
    fe_mul(z11, z9, z2);
    fe_sq(t0, z11); fe_mul(z2_5_0, t0, z9);

    fe_sq(t0, z2_5_0); for (i = 1; i < 5; i++) fe_sq(t0, t0);
    fe_mul(z2_10_0, t0, z2_5_0);

    fe_sq(t0, z2_10_0); for (i = 1; i < 10; i++) fe_sq(t0, t0);
    fe_mul(z2_20_0, t0, z2_10_0);

    fe_sq(t0, z2_20_0); for (i = 1; i < 20; i++) fe_sq(t0, t0);
    fe_mul(t0, t0, z2_20_0);

    fe_sq(t0, t0); for (i = 1; i < 10; i++) fe_sq(t0, t0);
    fe_mul(z2_50_0, t0, z2_10_0);

    fe_sq(t0, z2_50_0); for (i = 1; i < 50; i++) fe_sq(t0, t0);
    fe_mul(z2_100_0, t0, z2_50_0);

    fe_sq(t0, z2_100_0); for (i = 1; i < 100; i++) fe_sq(t0, t0);
    fe_mul(t0, t0, z2_100_0);

    fe_sq(t0, t0); for (i = 1; i < 50; i++) fe_sq(t0, t0);
    fe_mul(t0, t0, z2_50_0);

    fe_sq(t0, t0); fe_sq(t0, t0); fe_sq(t0, t0); fe_sq(t0, t0); fe_sq(t0, t0);
    fe_mul(out, t0, z11);
}

static uint64_t load3(const uint8_t *p) {
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16);
}
static uint64_t load4(const uint8_t *p) {
    return load3(p) | ((uint64_t)p[3] << 24);
}

static void fe_frombytes(fe h, const uint8_t s[32]) {
    /* The 26/25 alternation means the limb boundaries do not fall on byte
     * boundaries; these are the fixed offsets and shifts for radix 2^25.5.
     * Bit 255 is masked off, as RFC 7748 requires — a peer may set it and the
     * value must still be interpreted mod 2^255-19. */
    h[0] = (int64_t)load4(s);
    h[1] = (int64_t)(load3(s + 4) << 6);
    h[2] = (int64_t)(load3(s + 7) << 5);
    h[3] = (int64_t)(load3(s + 10) << 3);
    h[4] = (int64_t)(load3(s + 13) << 2);
    h[5] = (int64_t)load4(s + 16);
    h[6] = (int64_t)(load3(s + 20) << 7);
    h[7] = (int64_t)(load3(s + 23) << 5);
    h[8] = (int64_t)(load3(s + 26) << 4);
    h[9] = (int64_t)((load3(s + 29) & 0x7FFFFFu) << 2);
    fe_carry(h);
}

static void fe_tobytes(uint8_t s[32], const fe f) {
    fe h;
    fe_copy(h, f);
    fe_carry(h);                 /* limbs now in [0, 2^shift) */

    /* h is in [0, 2p). Decide whether to subtract p ONCE, by propagating a
     * trial carry as if we had added 19 at the bottom: q ends as 1 exactly
     * when h >= p. The earlier hand-rolled version got this wrong and
     * subtracted 19 one time too many — the result was correct in every limb
     * except the lowest, which is exactly the kind of near-miss that looks
     * like random bytes and matches no peer. */
    int64_t q = (19 * h[9] + ((int64_t)1 << 24)) >> 25;
    q = (h[0] + q) >> 26;
    q = (h[1] + q) >> 25;
    q = (h[2] + q) >> 26;
    q = (h[3] + q) >> 25;
    q = (h[4] + q) >> 26;
    q = (h[5] + q) >> 25;
    q = (h[6] + q) >> 26;
    q = (h[7] + q) >> 25;
    q = (h[8] + q) >> 26;
    q = (h[9] + q) >> 25;

    h[0] += 19 * q;

    /* final carry chain; the carry out of h[9] is the 2^255 we just accounted */
    int64_t c = 0;
    for (int i = 0; i < 10; i++) {
        int shift = (i & 1) ? 25 : 26;
        h[i] += c;
        c = h[i] >> shift;
        h[i] -= c * ((int64_t)1 << shift);
    }

    uint32_t h0=(uint32_t)h[0], h1=(uint32_t)h[1], h2=(uint32_t)h[2],
             h3=(uint32_t)h[3], h4=(uint32_t)h[4], h5=(uint32_t)h[5],
             h6=(uint32_t)h[6], h7=(uint32_t)h[7], h8=(uint32_t)h[8],
             h9=(uint32_t)h[9];
    s[0]  = (uint8_t)(h0 >> 0);
    s[1]  = (uint8_t)(h0 >> 8);
    s[2]  = (uint8_t)(h0 >> 16);
    s[3]  = (uint8_t)((h0 >> 24) | (h1 << 2));
    s[4]  = (uint8_t)(h1 >> 6);
    s[5]  = (uint8_t)(h1 >> 14);
    s[6]  = (uint8_t)((h1 >> 22) | (h2 << 3));
    s[7]  = (uint8_t)(h2 >> 5);
    s[8]  = (uint8_t)(h2 >> 13);
    s[9]  = (uint8_t)((h2 >> 21) | (h3 << 5));
    s[10] = (uint8_t)(h3 >> 3);
    s[11] = (uint8_t)(h3 >> 11);
    s[12] = (uint8_t)((h3 >> 19) | (h4 << 6));
    s[13] = (uint8_t)(h4 >> 2);
    s[14] = (uint8_t)(h4 >> 10);
    s[15] = (uint8_t)(h4 >> 18);
    s[16] = (uint8_t)(h5 >> 0);
    s[17] = (uint8_t)(h5 >> 8);
    s[18] = (uint8_t)(h5 >> 16);
    s[19] = (uint8_t)((h5 >> 24) | (h6 << 1));
    s[20] = (uint8_t)(h6 >> 7);
    s[21] = (uint8_t)(h6 >> 15);
    s[22] = (uint8_t)((h6 >> 23) | (h7 << 3));
    s[23] = (uint8_t)(h7 >> 5);
    s[24] = (uint8_t)(h7 >> 13);
    s[25] = (uint8_t)((h7 >> 21) | (h8 << 4));
    s[26] = (uint8_t)(h8 >> 4);
    s[27] = (uint8_t)(h8 >> 12);
    s[28] = (uint8_t)((h8 >> 20) | (h9 << 6));
    s[29] = (uint8_t)(h9 >> 2);
    s[30] = (uint8_t)(h9 >> 10);
    s[31] = (uint8_t)(h9 >> 18);
}

void x25519(uint8_t out[X25519_LEN],
            const uint8_t scalar[X25519_LEN],
            const uint8_t point[X25519_LEN]) {
    uint8_t e[32];
    for (int i = 0; i < 32; i++) e[i] = scalar[i];
    /* RFC 7748 clamping — done here so a caller cannot omit it. */
    e[0]  &= 248;
    e[31] &= 127;
    e[31] |= 64;

    fe x1, x2, z2, x3, z3, t0, t1;
    fe_frombytes(x1, point);
    fe_1(x2); fe_0(z2);
    fe_copy(x3, x1); fe_1(z3);

    uint32_t swap = 0;
    for (int pos = 254; pos >= 0; pos--) {
        uint32_t b = (uint32_t)((e[pos >> 3] >> (pos & 7)) & 1);
        swap ^= b;
        fe_cswap(x2, x3, swap);
        fe_cswap(z2, z3, swap);
        swap = b;

        /* one Montgomery ladder step — identical work for every bit */
        fe_sub(t0, x3, z3);
        fe_sub(t1, x2, z2);
        fe_add(x2, x2, z2);
        fe_add(z2, x3, z3);
        fe_mul(z3, t0, x2);
        fe_mul(z2, z2, t1);
        fe_sq(t0, t1);
        fe_sq(t1, x2);
        fe_add(x3, z3, z2);
        fe_sub(z2, z3, z2);
        fe_mul(x2, t1, t0);
        fe_sub(t1, t1, t0);
        fe_sq(z2, z2);
        fe_mul121666(z3, t1);
        fe_sq(x3, x3);
        fe_add(t0, t0, z3);
        fe_mul(z3, x1, z2);
        fe_mul(z2, t1, t0);
    }
    fe_cswap(x2, x3, swap);
    fe_cswap(z2, z3, swap);

    fe_invert(z2, z2);
    fe_mul(x2, x2, z2);
    fe_tobytes(out, x2);
}

void x25519_public(uint8_t out[X25519_LEN], const uint8_t scalar[X25519_LEN]) {
    static const uint8_t base[32] = { 9 };
    x25519(out, scalar, base);
}

bool x25519_shared(uint8_t out[X25519_LEN],
                   const uint8_t private_scalar[X25519_LEN],
                   const uint8_t peer_public[X25519_LEN]) {
    x25519(out, private_scalar, peer_public);
    /* RFC 8446 7.4.2 requires this: a peer that sends a low-order point forces
     * an all-zero shared secret that it can predict. Checked without an early
     * exit so the check itself leaks nothing. */
    uint8_t acc = 0;
    for (uint32_t i = 0; i < X25519_LEN; i++) acc |= out[i];
    return acc != 0;
}
