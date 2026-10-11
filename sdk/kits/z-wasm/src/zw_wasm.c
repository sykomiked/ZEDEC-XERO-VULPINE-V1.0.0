/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zw_wasm.c — LEB128 / UTF-8 readers and the Wasm 2.0 instruction decoder
 * shared by the validator and the fuel meter. Integer code only (R11). */
#include "zw_wasm.h"

/* ----- LEB128 ----- */

/* Unsigned LEB128 of at most `bits` bits, canonical-length checked as the
 * spec requires (at most ceil(bits/7) bytes, unused high bits zero). */
static int rd_uleb(const uint8_t *b, size_t end, size_t *pos, unsigned bits, uint64_t *out) {
    uint64_t result = 0;
    unsigned shift = 0;
    unsigned maxbytes = (bits + 6u) / 7u;
    size_t p = *pos;
    uint8_t byte = 0;
    for (unsigned i = 0;; i++) {
        if (p >= end || i >= maxbytes) return -1;
        byte = b[p++];
        result |= (uint64_t) (byte & 0x7Fu) << shift;
        shift += 7u;
        if (!(byte & 0x80u)) break;
    }
    if (shift > bits) {
        unsigned used = bits - (shift - 7u); /* meaningful bits in the last byte */
        uint8_t mask = (uint8_t) (0x7Fu & ~((1u << used) - 1u));
        if (byte & mask) return -1;
    }
    *pos = p;
    *out = result;
    return 0;
}

/* Signed LEB128 of `bits` bits (32, 33 or 64). */
static int rd_sleb(const uint8_t *b, size_t end, size_t *pos, unsigned bits, int64_t *out) {
    uint64_t result = 0;
    unsigned shift = 0;
    unsigned maxbytes = (bits + 6u) / 7u;
    size_t p = *pos;
    uint8_t byte = 0;
    for (unsigned i = 0;; i++) {
        if (p >= end || i >= maxbytes) return -1;
        byte = b[p++];
        result |= (uint64_t) (byte & 0x7Fu) << shift;
        shift += 7u;
        if (!(byte & 0x80u)) break;
    }
    if (shift >= bits) {
        /* The last byte holds the sign bit (bit used-1); every bit above it
         * up to bit 6 must repeat it. */
        unsigned used = bits - (shift - 7u);
        uint8_t mask = (uint8_t) (0x7Fu & ~((1u << (used - 1u)) - 1u));
        uint8_t v = (uint8_t) (byte & mask);
        if (v != 0 && v != mask) return -1;
    }
    if (shift < 64u && (byte & 0x40u)) result |= ~UINT64_C(0) << shift;
    *pos = p;
    /* Two's complement reinterpretation without signed overflow. */
    if (result > (uint64_t) INT64_MAX)
        *out = -(int64_t) (~result) - 1;
    else
        *out = (int64_t) result;
    return 0;
}

int zw_rd_u8(const uint8_t *b, size_t end, size_t *pos, uint8_t *out) {
    if (*pos >= end) return -1;
    *out = b[(*pos)++];
    return 0;
}

int zw_rd_u32(const uint8_t *b, size_t end, size_t *pos, uint32_t *out) {
    uint64_t v;
    if (rd_uleb(b, end, pos, 32u, &v) != 0) return -1;
    *out = (uint32_t) v;
    return 0;
}

int zw_rd_s32(const uint8_t *b, size_t end, size_t *pos, int32_t *out) {
    int64_t v;
    if (rd_sleb(b, end, pos, 32u, &v) != 0) return -1;
    *out = (int32_t) v;
    return 0;
}

int zw_rd_s33(const uint8_t *b, size_t end, size_t *pos, int64_t *out) {
    return rd_sleb(b, end, pos, 33u, out);
}

int zw_rd_s64(const uint8_t *b, size_t end, size_t *pos, int64_t *out) {
    return rd_sleb(b, end, pos, 64u, out);
}

/* ----- UTF-8 ----- */

int zw_utf8_ok(const uint8_t *p, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = p[i];
        if (c < 0x80u) {
            i++;
            continue;
        }
        size_t need;
        uint32_t cp;
        uint32_t min;
        if (c >= 0xC2u && c <= 0xDFu) {
            need = 1;
            cp = c & 0x1Fu;
            min = 0x80u;
        } else if (c >= 0xE0u && c <= 0xEFu) {
            need = 2;
            cp = c & 0x0Fu;
            min = 0x800u;
        } else if (c >= 0xF0u && c <= 0xF4u) {
            need = 3;
            cp = c & 0x07u;
            min = 0x10000u;
        } else {
            return 0;
        }
        if (n - i - 1u < need) return 0;
        for (size_t k = 1; k <= need; k++) {
            uint8_t cc = p[i + k];
            if ((cc & 0xC0u) != 0x80u) return 0;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (cp < min || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return 0;
        i += need + 1u;
    }
    return 1;
}

/* ----- Instruction classification ----- */

int zw_is_float_op(uint32_t op) {
    if (op >= 0xFC00u) return op <= ZW_OP_FC(7); /* i32/i64.trunc_sat_f32/f64_s/u */
    switch (op) {
    case 0x2A: case 0x2B: /* f32.load f64.load   */
    case 0x38: case 0x39: /* f32.store f64.store */
    case 0x43: case 0x44: /* f32.const f64.const */
        return 1;
    case 0xA7: /* i32.wrap_i64       */
    case 0xAC: /* i64.extend_i32_s   */
    case 0xAD: /* i64.extend_i32_u   */
        return 0;
    default:
        break;
    }
    if (op >= 0x5B && op <= 0x66) return 1; /* f32/f64 comparisons              */
    if (op >= 0x8B && op <= 0xBF) return 1; /* f32/f64 arithmetic, conversions, */
                                            /* truncations, reinterpretations   */
    return 0;
}

int zw_is_bulk_op(uint32_t op) {
    return op >= ZW_OP_FC(8) && op <= ZW_OP_FC(17);
}

/* ----- Decoder ----- */

#define BAD(off_, r_)                                                                          \
    do {                                                                                       \
        *err_off = (off_);                                                                     \
        return (r_);                                                                           \
    } while (0)

int32_t zw_decode(const uint8_t *b, size_t end, size_t pos, zw_ins *ins, size_t *err_off) {
    size_t start = pos;
    size_t p = pos;
    uint8_t op;
    ins->idx = 0;
    ins->idx2 = 0;
    ins->bt_kind = 0;
    ins->vec_pos = 0;
    ins->vec_n = 0;
    if (zw_rd_u8(b, end, &p, &op) != 0) BAD(start, ZW_R_MALFORMED);
    ins->op = op;

    switch (op) {
    case 0x00: case 0x01: case 0x05: case 0x0B: case 0x0F: /* unreachable nop else end return */
    case 0x1A: case 0x1B: /* drop select */
    case 0xD1:            /* ref.is_null */
        break;
    case 0x02: case 0x03: case 0x04: { /* block loop if: blocktype */
        uint8_t t;
        if (p >= end) BAD(p, ZW_R_MALFORMED);
        t = b[p];
        if (t == WT_EMPTY) {
            ins->bt_kind = 0;
            p++;
        } else if (t == WT_I32 || t == WT_I64 || t == WT_F32 || t == WT_F64 || t == WT_V128 ||
                   t == WT_FUNCREF || t == WT_EXTERNREF) {
            ins->bt_kind = 1;
            ins->idx = t;
            p++;
        } else {
            int64_t x;
            size_t q = p;
            if (zw_rd_s33(b, end, &p, &x) != 0 || x < 0 || x > (int64_t) UINT32_MAX)
                BAD(q, ZW_R_MALFORMED);
            ins->bt_kind = 2;
            ins->idx = (uint32_t) x;
        }
        break;
    }
    case 0x0C: case 0x0D: /* br br_if */
    case 0x10:            /* call */
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: /* local.* global.* */
    case 0x25: case 0x26: /* table.get table.set */
    case 0xD2:            /* ref.func */
        if (zw_rd_u32(b, end, &p, &ins->idx) != 0) BAD(p, ZW_R_MALFORMED);
        break;
    case 0x0E: { /* br_table vec(label) label */
        uint32_t n, l;
        if (zw_rd_u32(b, end, &p, &n) != 0) BAD(p, ZW_R_MALFORMED);
        ins->vec_pos = p;
        ins->vec_n = n;
        for (uint32_t i = 0; i < n; i++)
            if (zw_rd_u32(b, end, &p, &l) != 0) BAD(p, ZW_R_MALFORMED);
        if (zw_rd_u32(b, end, &p, &ins->idx) != 0) BAD(p, ZW_R_MALFORMED); /* default */
        break;
    }
    case 0x11: /* call_indirect typeidx tableidx */
        if (zw_rd_u32(b, end, &p, &ins->idx) != 0) BAD(p, ZW_R_MALFORMED);
        if (zw_rd_u32(b, end, &p, &ins->idx2) != 0) BAD(p, ZW_R_MALFORMED);
        break;
    case 0x1C: { /* select vec(valtype): exactly one in 2.0 */
        uint32_t n;
        if (zw_rd_u32(b, end, &p, &n) != 0) BAD(p, ZW_R_MALFORMED);
        if (n != 1) BAD(start, ZW_R_MALFORMED);
        ins->vec_pos = p;
        ins->vec_n = 1;
        if (p >= end) BAD(p, ZW_R_MALFORMED);
        ins->idx = b[p++];
        break;
    }
    case 0x3F: case 0x40: { /* memory.size memory.grow 0x00 */
        uint8_t z;
        if (zw_rd_u8(b, end, &p, &z) != 0) BAD(p, ZW_R_MALFORMED);
        if (z != 0) BAD(p - 1u, ZW_R_MALFORMED);
        break;
    }
    case 0x41: { /* i32.const */
        int32_t v;
        if (zw_rd_s32(b, end, &p, &v) != 0) BAD(p, ZW_R_MALFORMED);
        break;
    }
    case 0x42: { /* i64.const */
        int64_t v;
        if (zw_rd_s64(b, end, &p, &v) != 0) BAD(p, ZW_R_MALFORMED);
        break;
    }
    case 0x43: /* f32.const: 4 raw bytes (decoded only to report it) */
        if (end - p < 4u) BAD(p, ZW_R_MALFORMED);
        p += 4u;
        break;
    case 0x44: /* f64.const */
        if (end - p < 8u) BAD(p, ZW_R_MALFORMED);
        p += 8u;
        break;
    case 0xD0: /* ref.null reftype */
        if (p >= end) BAD(p, ZW_R_MALFORMED);
        ins->idx = b[p++];
        if (ins->idx != WT_FUNCREF && ins->idx != WT_EXTERNREF) BAD(p - 1u, ZW_R_MALFORMED);
        break;
    case 0xFC: { /* prefixed: u32 sub-opcode */
        uint32_t sub;
        size_t q = p;
        if (zw_rd_u32(b, end, &p, &sub) != 0) BAD(q, ZW_R_MALFORMED);
        if (sub > 17u) BAD(start, ZW_R_MALFORMED);
        ins->op = ZW_OP_FC(sub);
        switch (sub) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
            break; /* trunc_sat: no immediates */
        case 8: { /* memory.init dataidx 0x00 */
            uint8_t z;
            if (zw_rd_u32(b, end, &p, &ins->idx) != 0) BAD(p, ZW_R_MALFORMED);
            if (zw_rd_u8(b, end, &p, &z) != 0 || z != 0) BAD(p, ZW_R_MALFORMED);
            break;
        }
        case 9:  /* data.drop dataidx */
        case 13: /* elem.drop elemidx */
        case 15: case 16: case 17: /* table.grow/size/fill tableidx */
            if (zw_rd_u32(b, end, &p, &ins->idx) != 0) BAD(p, ZW_R_MALFORMED);
            break;
        case 10: { /* memory.copy 0x00 0x00 */
            uint8_t z1, z2;
            if (zw_rd_u8(b, end, &p, &z1) != 0 || zw_rd_u8(b, end, &p, &z2) != 0 || z1 || z2)
                BAD(p, ZW_R_MALFORMED);
            break;
        }
        case 11: { /* memory.fill 0x00 */
            uint8_t z;
            if (zw_rd_u8(b, end, &p, &z) != 0 || z != 0) BAD(p, ZW_R_MALFORMED);
            break;
        }
        case 12: /* table.init elemidx tableidx */
        case 14: /* table.copy dst src */
            if (zw_rd_u32(b, end, &p, &ins->idx) != 0) BAD(p, ZW_R_MALFORMED);
            if (zw_rd_u32(b, end, &p, &ins->idx2) != 0) BAD(p, ZW_R_MALFORMED);
            break;
        default:
            BAD(start, ZW_R_MALFORMED);
        }
        break;
    }
    case 0xFD: /* SIMD: every one of them, refused before decoding */
        BAD(start, ZW_R_SIMD);
    case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: /* exception handling */
    case 0x12: case 0x13: case 0x14: case 0x15:             /* tail calls, call_ref */
    case 0xFB: /* GC */
    case 0xFE: /* threads / atomics */
        BAD(start, ZW_R_UNSUPPORTED);
    default:
        if (op >= 0x28 && op <= 0x3E) { /* loads and stores: memarg */
            if (zw_rd_u32(b, end, &p, &ins->idx) != 0) BAD(p, ZW_R_MALFORMED);  /* align */
            if (zw_rd_u32(b, end, &p, &ins->idx2) != 0) BAD(p, ZW_R_MALFORMED); /* offset */
            break;
        }
        if (op >= 0x45 && op <= 0xC4) break; /* numeric, no immediates */
        BAD(start, ZW_R_MALFORMED);
    }
    ins->len = p - start;
    return ZW_R_OK;
}
