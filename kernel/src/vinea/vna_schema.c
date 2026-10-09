/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_schema.c — the one generic packer/validator. See vna_schema.h. */
#include "vna_schema.h"
#include "../swarm/swarm_hk.h"

static uint64_t rd_int(const uint8_t *p, uint8_t type)
{
    switch (type) {
    case VNA_F_U8:
        return *p;
    case VNA_F_U16:
        return *(const uint16_t *) (const void *) p;
    case VNA_F_U32:
    case VNA_F_CONST32:
        return *(const uint32_t *) (const void *) p;
    default:
        return *(const uint64_t *) (const void *) p;
    }
}

static void wr_int(uint8_t *p, uint8_t type, uint64_t v)
{
    switch (type) {
    case VNA_F_U8:
        *p = (uint8_t) v;
        break;
    case VNA_F_U16:
        *(uint16_t *) (void *) p = (uint16_t) v;
        break;
    case VNA_F_U32:
    case VNA_F_CONST32:
        *(uint32_t *) (void *) p = (uint32_t) v;
        break;
    default:
        *(uint64_t *) (void *) p = v;
        break;
    }
}

static uint32_t int_width(uint8_t type)
{
    switch (type) {
    case VNA_F_U8:
        return 1;
    case VNA_F_U16:
        return 2;
    case VNA_F_U32:
    case VNA_F_CONST32:
        return 4;
    default:
        return 8;
    }
}

bool vna_hk_is_canonical(const uint8_t *text, uint32_t len)
{
    char src[SWARM_HK_MAX_TEXT];
    char canon[SWARM_HK_MAX_TEXT];
    swarm_hk_ast_t ast;
    if (!text || len == 0 || len >= SWARM_HK_MAX_TEXT) return false;
    for (uint32_t i = 0; i < len; i++) {
        if (text[i] == 0 || text[i] >= 0x7f || text[i] < 0x20) return false; /* printable ASCII */
        src[i] = (char) text[i];
    }
    src[len] = 0;
    if (swarm_hk_parse(src, &ast) != SWARM_HK_OK) return false;
    int32_t n = swarm_hk_canonical(&ast, canon, sizeof canon);
    if (n < 0 || (uint32_t) n != len) return false;
    return vna_eq(canon, src, len);
}

int32_t vna_hk_canonicalize(const char *text, uint8_t *out, uint32_t cap)
{
    char canon[SWARM_HK_MAX_TEXT];
    swarm_hk_ast_t ast;
    if (!text || !out) return -1;
    if (swarm_hk_parse(text, &ast) != SWARM_HK_OK) return -1;
    int32_t n = swarm_hk_canonical(&ast, canon, sizeof canon);
    if (n <= 0 || (uint32_t) n > cap) return -1;
    vna_copy(out, canon, (uint32_t) n);
    return n;
}

static int32_t pack_fields(const vna_schema_t *s, const uint8_t *r, uint8_t *out, uint32_t cap,
                           bool with_sig, uint32_t depth)
{
    uint32_t pos = 0;
    for (uint16_t i = 0; i < s->n; i++) {
        const vna_field_t *f = &s->f[i];
        if (f->sig && !with_sig) break;
        switch (f->type) {
        case VNA_F_U8:
        case VNA_F_U16:
        case VNA_F_U32:
        case VNA_F_U64:
        case VNA_F_CONST32: {
            uint32_t w = int_width(f->type);
            uint64_t v = rd_int(r + f->off, f->type);
            if (f->type == VNA_F_CONST32 && v != f->maxv) return -1;
            if (f->type != VNA_F_CONST32 && f->maxv && v > f->maxv) return -1;
            if (cap - pos < w) return -1;
            for (uint32_t b = 0; b < w; b++) out[pos + b] = (uint8_t) (v >> (8 * b));
            pos += w;
            break;
        }
        case VNA_F_FIXED:
            if (cap - pos < f->max) return -1;
            vna_copy(out + pos, r + f->off, f->max);
            pos += f->max;
            break;
        case VNA_F_VAR:
        case VNA_F_HK: {
            uint16_t l = *(const uint16_t *) (const void *) (r + f->len_off);
            if (l > f->max) return -1;
            if (f->type == VNA_F_HK && !vna_hk_is_canonical(r + f->off, l)) return -1;
            if (cap - pos < 2u + l) return -1;
            vna_put16(out + pos, l);
            vna_copy(out + pos + 2, r + f->off, l);
            pos += 2u + l;
            break;
        }
        case VNA_F_ARR: {
            uint16_t cnt = *(const uint16_t *) (const void *) (r + f->len_off);
            if (cnt > f->max || !f->sub || depth > 0) return -1;
            if (cap - pos < 2) return -1;
            vna_put16(out + pos, cnt);
            pos += 2;
            for (uint16_t e = 0; e < cnt; e++) {
                int32_t w = pack_fields(f->sub, r + f->off + (uint32_t) e * f->stride, out + pos,
                                        cap - pos, true, depth + 1);
                if (w < 0) return -1;
                pos += (uint32_t) w;
            }
            break;
        }
        default:
            return -1;
        }
    }
    return (int32_t) pos;
}

int32_t vna_schema_pack(const vna_schema_t *s, const void *rec, uint8_t *out, uint32_t cap,
                        bool with_sig)
{
    if (!s || !rec || !out || cap > 0x7fffffffu) return -1;
    return pack_fields(s, (const uint8_t *) rec, out, cap, with_sig, 0);
}

static int32_t unpack_fields(const vna_schema_t *s, const uint8_t *in, uint32_t len, uint8_t *r,
                             uint32_t *sig_off, uint32_t depth)
{
    uint32_t pos = 0;
    bool sig_seen = false;
    for (uint16_t i = 0; i < s->n; i++) {
        const vna_field_t *f = &s->f[i];
        if (f->sig && !sig_seen) {
            sig_seen = true;
            if (sig_off) *sig_off = pos;
        }
        switch (f->type) {
        case VNA_F_U8:
        case VNA_F_U16:
        case VNA_F_U32:
        case VNA_F_U64:
        case VNA_F_CONST32: {
            uint32_t w = int_width(f->type);
            if (len - pos < w) return -1;
            uint64_t v = 0;
            for (uint32_t b = 0; b < w; b++) v |= (uint64_t) in[pos + b] << (8 * b);
            if (f->type == VNA_F_CONST32 && v != f->maxv) return -1;
            if (f->type != VNA_F_CONST32 && f->maxv && v > f->maxv) return -1;
            wr_int(r + f->off, f->type, v);
            pos += w;
            break;
        }
        case VNA_F_FIXED:
            if (len - pos < f->max) return -1;
            vna_copy(r + f->off, in + pos, f->max);
            pos += f->max;
            break;
        case VNA_F_VAR:
        case VNA_F_HK: {
            if (len - pos < 2) return -1;
            uint16_t l = vna_get16(in + pos);
            if (l > f->max || len - pos - 2 < l) return -1;
            if (f->type == VNA_F_HK && !vna_hk_is_canonical(in + pos + 2, l)) return -1;
            *(uint16_t *) (void *) (r + f->len_off) = l;
            vna_copy(r + f->off, in + pos + 2, l);
            pos += 2u + l;
            break;
        }
        case VNA_F_ARR: {
            if (!f->sub || depth > 0 || len - pos < 2) return -1;
            uint16_t cnt = vna_get16(in + pos);
            if (cnt > f->max) return -1;
            pos += 2;
            *(uint16_t *) (void *) (r + f->len_off) = cnt;
            for (uint16_t e = 0; e < cnt; e++) {
                int32_t w = unpack_fields(f->sub, in + pos, len - pos,
                                          r + f->off + (uint32_t) e * f->stride, 0, depth + 1);
                if (w < 0) return -1;
                pos += (uint32_t) w;
            }
            break;
        }
        default:
            return -1;
        }
    }
    if (!sig_seen && sig_off && depth == 0) *sig_off = pos;
    return (int32_t) pos;
}

int32_t vna_schema_unpack(const vna_schema_t *s, const uint8_t *in, uint32_t len, void *rec,
                          uint32_t *sig_off)
{
    if (!s || !in || !rec || len > 0x7fffffffu) return -1;
    int32_t used = unpack_fields(s, in, len, (uint8_t *) rec, sig_off, 0);
    if (used < 0 || (uint32_t) used != len) return -1; /* S8: exact consumption */
    return used;
}

uint32_t vna_schema_max_len(const vna_schema_t *s)
{
    uint32_t n = 0;
    for (uint16_t i = 0; i < s->n; i++) {
        const vna_field_t *f = &s->f[i];
        switch (f->type) {
        case VNA_F_FIXED:
            n += f->max;
            break;
        case VNA_F_VAR:
        case VNA_F_HK:
            n += 2u + f->max;
            break;
        case VNA_F_ARR:
            n += 2u + f->max * vna_schema_max_len(f->sub);
            break;
        default:
            n += int_width(f->type);
            break;
        }
    }
    return n;
}
