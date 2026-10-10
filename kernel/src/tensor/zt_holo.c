/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_holo.c — holographic coding on the coil, and UBH-168 frames with
 * alternating endianness. See zt.h T15, T16. The E8 form of the code works
 * on the doubled coordinates of src/e8/e8_lattice.h (zt_lattice.h T17). */
#include "zt.h"
#include "../e8/e8_lattice.h" /* the canonical E8 tables */

static uint32_t pidx(const zt_coil_t *c, zt_place_t p)
{
    return c->offset[p.shell] + p.slot;
}

/* a - b and a + b modulo 2^64, as int64, with no signed overflow: the code
 * is exact for every int64 value because decoding undoes the same wrap. */
static int64_t wrap64(uint64_t u)
{
    return u <= (uint64_t) INT64_MAX ? (int64_t) u : -(int64_t) ~u - 1;
}

static int64_t sub_wrap(int64_t a, int64_t b)
{
    return wrap64((uint64_t) a - (uint64_t) b);
}

static int64_t add_wrap(int64_t a, int64_t b)
{
    return wrap64((uint64_t) a + (uint64_t) b);
}

static int32_t sat8(int32_t v)
{
    return v > 127 ? 127 : v < -128 ? -128 : v;
}

/* The per-shell residual tap (zt.h). One pointer, read once per encode. */
static zt_shell_tap_t *shell_tap;

void zt_set_shell_tap(zt_shell_tap_t *tap)
{
    shell_tap = tap;
}

/* acc += floor(delta / 256), saturated to int32. For a negative delta,
 * ~delta = -delta - 1 >= 0 and floor(delta / 256) = -(~delta >> 8) - 1, so
 * no signed shift is needed. */
static void tap_add(int32_t *acc, int64_t delta)
{
    int64_t q = delta >= 0 ? delta / 256 : -(int64_t) ((uint64_t) ~delta >> 8) - 1;
    int64_t s = (int64_t) *acc + q; /* |q| < 2^56: no overflow */
    *acc = s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : (int32_t) s;
}

void zt_holo_encode(const zt_coil_t *c, const int64_t *value, int64_t *residual)
{
    zt_shell_tap_t *tap = shell_tap;
    /* shell 0 is predicted by nothing (0): its residual is its value */
    for (uint32_t j = 0; j < c->size[0]; j++) {
        residual[j] = value[j];
        if (tap) tap_add(&tap->acc[0], value[j]);
    }
    for (uint32_t s = 1; s < ZT_COIL_SHELLS; s++)
        for (uint32_t j = 0; j < c->size[s]; j++) {
            zt_place_t p = {s, j};
            int64_t delta = sub_wrap(value[pidx(c, p)], value[pidx(c, zt_coil_parent(c, p))]);
            residual[pidx(c, p)] = delta;
            if (tap) tap_add(&tap->acc[s], delta);
        }
}

void zt_holo_decode(const zt_coil_t *c, const int64_t *residual, uint32_t shells, int64_t *value)
{
    for (uint32_t j = 0; j < c->size[0]; j++) value[j] = shells ? residual[j] : 0;
    for (uint32_t s = 1; s < ZT_COIL_SHELLS; s++)
        for (uint32_t j = 0; j < c->size[s]; j++) {
            zt_place_t p = {s, j};
            int64_t up = value[pidx(c, zt_coil_parent(c, p))];
            value[pidx(c, p)] = s < shells ? add_wrap(up, residual[pidx(c, p)]) : up;
        }
}

/* T15 on E8. Every place holds one E8 point (8 doubled coordinates). The
 * lattice is a group, so a place minus its parent is again a lattice point:
 * the residuals are E8 points, small ones exactly where neighbours agree,
 * and a residual in the 26641-point ball is one zt_e8 codebook index. */
bool zt_holo_e8_encode(const zt_coil_t *c, const int8_t *v2, int16_t *res2)
{
    for (uint32_t i = 0; i < c->total; i++) {
        int32_t w[8];
        for (uint32_t k = 0; k < 8; k++) w[k] = v2[8 * i + k];
        if (!e8l_is_point2(w)) return false;
    }
    for (uint32_t j = 0; j < c->size[0]; j++)
        for (uint32_t k = 0; k < 8; k++) res2[8 * j + k] = v2[8 * j + k];
    for (uint32_t s = 1; s < ZT_COIL_SHELLS; s++)
        for (uint32_t j = 0; j < c->size[s]; j++) {
            zt_place_t p = {s, j};
            uint32_t a = pidx(c, p), b = pidx(c, zt_coil_parent(c, p));
            for (uint32_t k = 0; k < 8; k++)
                res2[8 * a + k] = (int16_t) (v2[8 * a + k] - v2[8 * b + k]);
        }
    return true;
}

void zt_holo_e8_decode(const zt_coil_t *c, const int16_t *res2, uint32_t shells, int8_t *v2)
{
    for (uint32_t j = 0; j < c->size[0]; j++)
        for (uint32_t k = 0; k < 8; k++)
            v2[8 * j + k] = (int8_t) (shells ? sat8(res2[8 * j + k]) : 0);
    for (uint32_t s = 1; s < ZT_COIL_SHELLS; s++)
        for (uint32_t j = 0; j < c->size[s]; j++) {
            zt_place_t p = {s, j};
            uint32_t a = pidx(c, p), b = pidx(c, zt_coil_parent(c, p));
            for (uint32_t k = 0; k < 8; k++)
                v2[8 * a + k] =
                    (int8_t) (s < shells ? sat8(v2[8 * b + k] + res2[8 * a + k]) : v2[8 * b + k]);
        }
}

static uint8_t check4(const uint8_t *b)
{
    uint8_t x = 0;
    for (uint32_t i = 0; i < 20; i++) x ^= b[i] ^ (uint8_t) (b[i] >> 4);
    return x & 0xFu;
}

uint32_t zt_frame_pack(const uint32_t *words, uint32_t n, uint32_t phase, uint8_t *out)
{
    uint32_t frames = (n + ZT_FRAME_WORDS - 1) / ZT_FRAME_WORDS;
    for (uint32_t f = 0; f < frames; f++) {
        uint8_t *o = out + f * ZT_FRAME_OCTETS;
        uint32_t first = f * ZT_FRAME_WORDS;
        uint32_t count = n - first < ZT_FRAME_WORDS ? n - first : ZT_FRAME_WORDS;
        uint32_t ph = (first + phase) & 1u;
        for (uint32_t w = 0; w < ZT_FRAME_WORDS; w++) {
            uint32_t v = w < count ? words[first + w] : 0;
            uint8_t *b = o + 1 + 4 * w;
            bool big = ((ph + w) & 1u) != 0;
            for (uint32_t k = 0; k < 4; k++) b[big ? 3 - k : k] = (uint8_t) (v >> (8 * k));
        }
        o[0] = (uint8_t) (ph | (count << 1) | (check4(o + 1) << 4));
    }
    return frames;
}

int32_t zt_frame_unpack(const uint8_t *in, uint32_t frames, uint32_t *words)
{
    uint32_t n = 0;
    for (uint32_t f = 0; f < frames; f++) {
        const uint8_t *o = in + f * ZT_FRAME_OCTETS;
        uint32_t ph = o[0] & 1u, count = (o[0] >> 1) & 7u;
        if (count == 0 || count > ZT_FRAME_WORDS || (o[0] >> 4) != check4(o + 1)) return -1;
        if (f + 1 < frames && count != ZT_FRAME_WORDS) return -1;
        for (uint32_t w = 0; w < count; w++) {
            const uint8_t *b = o + 1 + 4 * w;
            bool big = ((ph + w) & 1u) != 0;
            uint32_t v = 0;
            for (uint32_t k = 0; k < 4; k++) v |= (uint32_t) b[big ? 3 - k : k] << (8 * k);
            words[n++] = v;
        }
    }
    return (int32_t) n;
}
