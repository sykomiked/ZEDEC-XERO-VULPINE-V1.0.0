/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_adapter_text.c — base-N symbol packing and dialectic answers. See
 * zt_adapters.h X1-X2. Every division is 32-bit. */
#include "zt_adapters.h"

#define NUM_BYTES 24u /* room for 2^192 */

static int digit_of(const zt_text_adapter_state_t *st, char c)
{
    for (uint32_t i = 0; i < st->letters; i++)
        if (st->alphabet[i] == c) return (int) i + 1;
    return -1;
}

/* num = num * base + d over nb little-endian bytes; returns the carry out. */
static uint32_t mul_add(uint8_t *num, uint32_t nb, uint32_t base, uint32_t d)
{
    uint32_t carry = d;
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t t = num[i] * base + carry;
        num[i] = (uint8_t) t;
        carry = t >> 8;
    }
    return carry;
}

/* num /= base over nb bytes; returns the remainder (the next digit). */
static uint32_t div_small(uint8_t *num, uint32_t nb, uint32_t base)
{
    uint32_t rem = 0;
    for (uint32_t i = nb; i-- > 0;) {
        uint32_t t = rem << 8 | num[i];
        num[i] = (uint8_t) (t / base);
        rem = t % base;
    }
    return rem;
}

uint32_t zt_text_symbols_fit(uint32_t base, uint32_t bits)
{
    uint8_t num[NUM_BYTES];
    if (base < 2u || bits == 0 || bits > 8u * (NUM_BYTES - 1u)) return 0;
    for (uint32_t i = 0; i < NUM_BYTES; i++) num[i] = 0;
    num[0] = 1;
    for (uint32_t k = 0;; k++) {
        /* num = base^k < 2^bits holds here; try base^(k+1) */
        mul_add(num, NUM_BYTES, base, 0);
        bool ge = false;
        for (uint32_t b = bits; b < 8u * NUM_BYTES; b++)
            if (num[b >> 3] >> (b & 7u) & 1u) ge = true;
        if (ge) return k;
    }
}

bool zt_adapter_text_init(zt_adapter_t *a, zt_text_adapter_state_t *st, uint8_t trunk,
                          const char *alphabet, uint32_t letters);

/* Pack `count` digits (from text) into nb bytes. */
static int pack_digits(const zt_text_adapter_state_t *st, const char *text, uint32_t count,
                       uint32_t slots, uint8_t *out, uint32_t nb)
{
    for (uint32_t i = 0; i < nb; i++) out[i] = 0;
    for (uint32_t i = slots; i-- > 0;) {
        uint32_t d = 0; /* end mark pads the block */
        if (i < count) {
            int v = digit_of(st, text[i]);
            if (v < 0) return ZT_ADAPTER_EDOMAIN;
            d = (uint32_t) v;
        }
        if (mul_add(out, nb, st->base, d)) return ZT_ADAPTER_EDOMAIN; /* cannot happen */
    }
    return 0;
}

int zt_text_pack_raw168(const zt_text_adapter_state_t *st, const char *text, size_t len,
                        uint8_t *out, size_t cap)
{
    if (!st || (!text && len) || !out || st->base > 25u) return ZT_ADAPTER_EARG;
    size_t blocks = (len + 35u) / 36u;
    if (blocks * 21u > cap || blocks * 21u > 0x7FFFFFF0u) return ZT_ADAPTER_ESPACE;
    for (size_t b = 0; b < blocks; b++) {
        size_t off = b * 36u;
        uint32_t cnt = (uint32_t) (len - off < 36u ? len - off : 36u);
        int e = pack_digits(st, text + off, cnt, 36u, out + 21u * b, 21u);
        if (e) return e;
    }
    return (int) (blocks * 21u);
}

int zt_text_unpack_raw168(const zt_text_adapter_state_t *st, const uint8_t *in, size_t len,
                          char *out, size_t cap)
{
    if (!st || !in || !out || len % 21u) return ZT_ADAPTER_EARG;
    size_t n = 0;
    for (size_t b = 0; b < len / 21u; b++) {
        uint8_t num[21];
        for (uint32_t i = 0; i < 21u; i++) num[i] = in[21u * b + i];
        for (uint32_t k = 0; k < 36u; k++) {
            uint32_t d = div_small(num, 21u, st->base);
            if (d == 0) goto done;
            if (d > st->letters) return ZT_ADAPTER_EFORMAT;
            if (n + 1u >= cap) return ZT_ADAPTER_ESPACE;
            out[n++] = st->alphabet[d - 1u];
        }
    }
done:
    if (n >= cap) return ZT_ADAPTER_ESPACE;
    out[n] = 0;
    return (int) n;
}

static int text_pump(zt_adapter_t *self, const void *dc_src, size_t len, zt_ubh168_frame_t *dst,
                     size_t max_frames)
{
    zt_text_adapter_state_t *st = self ? (zt_text_adapter_state_t *) self->state : 0;
    const char *src = (const char *) dc_src;
    if (!st || (!src && len) || !dst || len > 0x7FFFFFF0u) return ZT_ADAPTER_EARG;
    size_t need = 1u + (len + st->per_frame - 1u) / st->per_frame;
    if (need > max_frames) return ZT_ADAPTER_ESPACE;
    zt_adapter_header(self->kind, self->trunk, (uint32_t) len, (uint32_t) (need - 1u),
                      zt_adapter_fnv1a((const uint8_t *) src, len, 2166136261u), &dst[0]);
    for (size_t f = 0; f + 1u < need; f++) {
        uint8_t num[20];
        size_t off = f * st->per_frame;
        uint32_t cnt = (uint32_t) (len - off < st->per_frame ? len - off : st->per_frame);
        int e = pack_digits(st, src + off, cnt, st->per_frame, num, 20u);
        if (e) return e;
        const uint32_t sp[3] = {zt_ld_le32(num), zt_ld_le32(num + 8), zt_ld_le32(num + 16)};
        const uint32_t sm[2] = {zt_ld_le32(num + 4), zt_ld_le32(num + 12)};
        zt_wire_pack_tagged(zt_adapter_data_tag(self->trunk, (uint32_t) f), sp, sm, &dst[f + 1u]);
    }
    return (int) need;
}

static int put(char *out, size_t cap, size_t *n, const char *s)
{
    for (; s && *s; s++) {
        if (*n + 1u >= cap) return ZT_ADAPTER_ESPACE;
        out[(*n)++] = *s;
    }
    return 0;
}

static int text_drain(zt_adapter_t *self, const zt_ubh168_frame_t *src, size_t n_frames,
                      zt_truth_state_t truth, void *dc_dst, size_t max_len)
{
    zt_text_adapter_state_t *st = self ? (zt_text_adapter_state_t *) self->state : 0;
    char *dst = (char *) dc_dst;
    uint32_t len, frames, sum;
    if (!st || !src) return ZT_ADAPTER_EARG;
    int e = zt_adapter_read_header(&src[0], self->kind, n_frames, &len, &frames, &sum);
    if (e) return e;
    if (frames != (len + st->per_frame - 1u) / st->per_frame) return ZT_ADAPTER_EFORMAT;
    if (truth == ZT_TRUTH_FALSE || truth == ZT_TRUTH_UNKNOWN) return 0;
    if (!dst) return ZT_ADAPTER_ESPACE;
    size_t n = 0;
    if (truth == ZT_TRUTH_GLUT && put(dst, max_len + 1u, &n, "[GLUT] ")) return ZT_ADAPTER_ESPACE;
    if (truth == ZT_TRUTH_PARADOX && put(dst, max_len + 1u, &n, "[PARADOX] "))
        return ZT_ADAPTER_ESPACE;
    size_t start = n;
    if (n + len > max_len) return ZT_ADAPTER_ESPACE;
    for (uint32_t f = 0; f < frames; f++) {
        zt_unpacked_rails_t r;
        uint8_t num[20];
        zt_wire_rails_init(&r);
        zt_wire_unpack_ubh168(&src[f + 1u], &r);
        if (r.sync) return ZT_ADAPTER_EFORMAT;
        zt_st_le32(num, r.s_plus[0]);
        zt_st_le32(num + 4, r.s_minus[0]);
        zt_st_le32(num + 8, r.s_plus[1]);
        zt_st_le32(num + 12, r.s_minus[1]);
        zt_st_le32(num + 16, r.s_plus[2]);
        for (uint32_t k = 0; k < st->per_frame && n - start < len; k++) {
            uint32_t d = div_small(num, 20u, st->base);
            if (d == 0 || d > st->letters) return ZT_ADAPTER_EFORMAT;
            dst[n++] = st->alphabet[d - 1u];
        }
    }
    if (n - start != len ||
        zt_adapter_fnv1a((const uint8_t *) dst + start, len, 2166136261u) != sum)
        return ZT_ADAPTER_EFORMAT;
    return (int) n;
}

bool zt_adapter_text_init(zt_adapter_t *a, zt_text_adapter_state_t *st, uint8_t trunk,
                          const char *alphabet, uint32_t letters)
{
    if (!a || !st || !alphabet || letters == 0 || letters > ZT_TEXT_MAX_ALPHABET) return false;
    for (uint32_t i = 0; i < letters; i++)
        for (uint32_t j = 0; j < i; j++)
            if (alphabet[i] == alphabet[j]) return false;
    for (uint32_t i = 0; i < letters; i++) st->alphabet[i] = alphabet[i];
    st->letters = letters;
    st->base = letters + 1u;
    st->per_frame = zt_text_symbols_fit(st->base, 160u);
    a->name = "text";
    a->state = st;
    a->kind = ZT_ADAPTER_KIND_TEXT;
    a->trunk = (uint8_t) (trunk % 10u);
    a->conflict_run = 0;
    a->threshold_q16 = ZT_ADAPTER_THRESHOLD;
    a->pump_in = text_pump;
    a->drain_out = text_drain;
    a->evaluate_interference = zt_adapter_evaluate_default;
    return true;
}

int zt_text_answer(const char *premise_a, const char *premise_not_a, zt_truth_state_t truth,
                   char *out, size_t cap)
{
    size_t n = 0;
    int e = 0;
    if (!out || cap == 0) return ZT_ADAPTER_EARG;
    switch (truth) {
    case ZT_TRUTH_TRUE:
        e = put(out, cap, &n, "TRUE: ");
        if (!e) e = put(out, cap, &n, premise_a);
        break;
    case ZT_TRUTH_FALSE:
        e = put(out, cap, &n, "FALSE: ");
        if (!e) e = put(out, cap, &n, premise_not_a);
        break;
    case ZT_TRUTH_GLUT:
    case ZT_TRUTH_PARADOX:
        e = put(out, cap, &n,
                truth == ZT_TRUTH_GLUT
                    ? "GLUT: the evidence supports both premises. A: "
                    : "PARADOX: the evidence has supported both premises on every recent "
                      "evaluation; escalate. A: ");
        if (!e) e = put(out, cap, &n, premise_a);
        if (!e) e = put(out, cap, &n, " | NOT A: ");
        if (!e) e = put(out, cap, &n, premise_not_a);
        if (!e) e = put(out, cap, &n, " | unresolved");
        break;
    case ZT_TRUTH_NEUTRAL:
        e = put(out, cap, &n, "NEUTRAL: the evidence is silent");
        break;
    case ZT_TRUTH_UNKNOWN:
        e = put(out, cap, &n, "UNKNOWN: no evidence for either premise");
        break;
    }
    out[n] = 0;
    return e ? e : (int) n;
}
