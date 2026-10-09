/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_holo.c — holographic coding on the coil, and UBH-168 frames with
 * alternating endianness. See zt.h T15, T16. */
#include "zt.h"

static uint32_t pidx(const zt_coil_t *c, zt_place_t p)
{
    return c->offset[p.shell] + p.slot;
}

void zt_holo_encode(const zt_coil_t *c, const int64_t *value, int64_t *residual)
{
    for (uint32_t j = 0; j < c->size[0]; j++) residual[j] = value[j];
    for (uint32_t s = 1; s < ZT_COIL_SHELLS; s++)
        for (uint32_t j = 0; j < c->size[s]; j++) {
            zt_place_t p = {s, j};
            residual[pidx(c, p)] = value[pidx(c, p)] - value[pidx(c, zt_coil_parent(c, p))];
        }
}

void zt_holo_decode(const zt_coil_t *c, const int64_t *residual, uint32_t shells, int64_t *value)
{
    for (uint32_t j = 0; j < c->size[0]; j++) value[j] = shells ? residual[j] : 0;
    for (uint32_t s = 1; s < ZT_COIL_SHELLS; s++)
        for (uint32_t j = 0; j < c->size[s]; j++) {
            zt_place_t p = {s, j};
            int64_t up = value[pidx(c, zt_coil_parent(c, p))];
            value[pidx(c, p)] = s < shells ? up + residual[pidx(c, p)] : up;
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
