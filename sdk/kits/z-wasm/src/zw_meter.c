/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zw_meter.c — fuel metering by bytecode instrumentation.
 *
 * Input: a module zw_validate accepted. Output: the same module plus
 *   - one new mutable i64 global G (index = number of globals, so no
 *     existing index moves; the plugin's own code cannot name it, because
 *     the validator bounded every global index by the original count),
 *   - an export "__zw_fuel" of G (the host reads and writes it),
 *   - at the start of every basic block, the charge sequence
 *         global.get G  i64.const <cost>  i64.sub  global.set G
 *         global.get G  i64.const 0  i64.lt_s  if  unreachable  end
 *     where <cost> is the number of instructions in the block.
 *
 * A basic block here starts at the function entry and right after every
 * loop, if, else, end, br, br_if, br_table, return and unreachable. Every
 * branch target (a loop head, the code after a block's end, the else arm)
 * is such a start, so control can only enter a block at its top: every
 * instruction that runs was paid for, before it ran (a block that traps
 * half-way was paid for in full). The inserted `if` is closed before any
 * original instruction, so no branch depth changes.
 *
 * Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "zw_wasm.h"

typedef struct {
    uint8_t *p;
    size_t n, cap;
    int oom;
} buf;

static void put(buf *b, const void *src, size_t len) {
    if (b->oom) return;
    if (len > (size_t) -1 - b->n) {
        b->oom = 1;
        return;
    }
    if (b->n + len > b->cap) {
        size_t nc = b->cap ? b->cap : 256u;
        while (nc < b->n + len) {
            if (nc > ((size_t) -1) / 2u) {
                b->oom = 1;
                return;
            }
            nc *= 2u;
        }
        uint8_t *np = (uint8_t *) realloc(b->p, nc);
        if (!np) {
            b->oom = 1;
            return;
        }
        b->p = np;
        b->cap = nc;
    }
    if (len) memcpy(b->p + b->n, src, len);
    b->n += len;
}

static void put_u8(buf *b, uint8_t v) {
    put(b, &v, 1);
}

static void put_uleb(buf *b, uint64_t v) {
    do {
        uint8_t c = (uint8_t) (v & 0x7Fu);
        v >>= 7;
        if (v) c |= 0x80u;
        put_u8(b, c);
    } while (v);
}

/* Signed LEB128 of a NON-NEGATIVE value (costs and zero). */
static void put_sleb_pos(buf *b, uint64_t v) {
    for (;;) {
        uint8_t c = (uint8_t) (v & 0x7Fu);
        v >>= 7;
        if (v == 0 && !(c & 0x40u)) {
            put_u8(b, c);
            return;
        }
        put_u8(b, (uint8_t) (c | 0x80u));
    }
}

static void put_section(buf *out, uint8_t id, const buf *content) {
    put_u8(out, id);
    put_uleb(out, content->n);
    put(out, content->p, content->n);
}

static void put_charge(buf *b, uint32_t g, uint64_t cost) {
    put_u8(b, 0x23); put_uleb(b, g);    /* global.get G   */
    put_u8(b, 0x42); put_sleb_pos(b, cost); /* i64.const cost */
    put_u8(b, 0x7D);                    /* i64.sub        */
    put_u8(b, 0x24); put_uleb(b, g);    /* global.set G   */
    put_u8(b, 0x23); put_uleb(b, g);    /* global.get G   */
    put_u8(b, 0x42); put_u8(b, 0x00);   /* i64.const 0    */
    put_u8(b, 0x53);                    /* i64.lt_s       */
    put_u8(b, 0x04); put_u8(b, 0x40);   /* if (no result) */
    put_u8(b, 0x00);                    /*   unreachable  */
    put_u8(b, 0x0B);                    /* end            */
}

static int ends_block(uint32_t op) {
    switch (op) {
    case 0x00: case 0x03: case 0x04: case 0x05: case 0x0B:
    case 0x0C: case 0x0D: case 0x0E: case 0x0F:
        return 1;
    default:
        return 0;
    }
}

/* Instrument the code section [pos, end). */
static int meter_code(const uint8_t *w, size_t pos, size_t end, uint32_t g, buf *sec) {
    uint32_t count;
    buf body = {0};
    if (zw_rd_u32(w, end, &pos, &count) != 0) return -1;
    put_uleb(sec, count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t size, ngroups;
        size_t bstart, bend, p, code, seg;
        uint64_t cost;
        if (zw_rd_u32(w, end, &pos, &size) != 0 || size > end - pos) goto bad;
        bstart = pos;
        bend = pos + size;
        p = bstart;
        if (zw_rd_u32(w, bend, &p, &ngroups) != 0) goto bad;
        for (uint32_t k = 0; k < ngroups; k++) {
            uint32_t cnt;
            if (zw_rd_u32(w, bend, &p, &cnt) != 0 || p >= bend) goto bad;
            p++;
        }
        code = p;
        body.n = 0;
        put(&body, w + bstart, code - bstart);
        /* One pass: find each block's extent, then emit charge + its bytes. */
        seg = code;
        cost = 0;
        while (p < bend) {
            zw_ins ins;
            size_t err;
            if (zw_decode(w, bend, p, &ins, &err) != ZW_R_OK) goto bad;
            if (zw_is_float_op(ins.op)) goto bad; /* defence in depth */
            cost++;
            p += ins.len;
            if (ends_block(ins.op) || p == bend) {
                put_charge(&body, g, cost);
                put(&body, w + seg, p - seg);
                seg = p;
                cost = 0;
            }
        }
        if (body.oom) goto oom;
        put_uleb(sec, body.n);
        put(sec, body.p, body.n);
        pos = bend;
    }
    free(body.p);
    return pos == end ? 0 : -1;
bad:
    free(body.p);
    return -1;
oom:
    free(body.p);
    return -2;
}

static unsigned rank(uint8_t id) {
    static const unsigned r[13] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 10};
    return id <= 12u ? r[id] : 99u;
}

#define RANK_GLOBAL 6u
#define RANK_EXPORT 7u

int32_t zw_meter(const uint8_t *w, size_t len, uint8_t **out, size_t *out_len) {
    buf o = {0}, sec = {0};
    size_t pos = 8;
    uint32_t nglobals = 0;
    int have_global = 0, have_export = 0;
    int emitted_global = 0, emitted_export = 0;
    int rc = ZW_E_REJECTED;
    static const uint8_t fuel_name[] = ZW_FUEL_EXPORT;

    if (!w || !out || !out_len || len < 8u) return ZXV_E_ARG;
    /* First pass: how many globals, and which sections exist. */
    while (pos < len) {
        uint8_t id = w[pos++];
        uint32_t size;
        if (zw_rd_u32(w, len, &pos, &size) != 0 || size > len - pos) return ZW_E_REJECTED;
        if (id == SEC_GLOBAL) {
            size_t q = pos;
            if (zw_rd_u32(w, pos + size, &q, &nglobals) != 0) return ZW_E_REJECTED;
            have_global = 1;
        }
        if (id == SEC_EXPORT) have_export = 1;
        /* The validator admits function imports only, so no imported
         * global shifts the global index space. */
        pos += size;
    }

    put(&o, w, 8);
    pos = 8;
    while (pos < len) {
        uint8_t id = w[pos++];
        uint32_t size;
        size_t start, end;
        (void) zw_rd_u32(w, len, &pos, &size);
        start = pos;
        end = pos + size;
        pos = end;
        if (id == SEC_CUSTOM) continue; /* dropped */
        unsigned rk = rank(id);
        if (rk > RANK_GLOBAL && !emitted_global && !have_global) {
            sec.n = 0;
            put_uleb(&sec, 1);
            put_u8(&sec, WT_I64); put_u8(&sec, 1); put_u8(&sec, 0x42); put_u8(&sec, 0x00);
            put_u8(&sec, 0x0B);
            put_section(&o, SEC_GLOBAL, &sec);
            emitted_global = 1;
        }
        if (rk > RANK_EXPORT && !emitted_export && !have_export) {
            sec.n = 0;
            put_uleb(&sec, 1);
            put_uleb(&sec, sizeof fuel_name - 1u);
            put(&sec, fuel_name, sizeof fuel_name - 1u);
            put_u8(&sec, 3);
            put_uleb(&sec, nglobals);
            put_section(&o, SEC_EXPORT, &sec);
            emitted_export = 1;
        }
        sec.n = 0;
        if (id == SEC_GLOBAL || id == SEC_EXPORT) {
            size_t q = start;
            uint32_t cnt;
            if (zw_rd_u32(w, end, &q, &cnt) != 0) goto done;
            put_uleb(&sec, (uint64_t) cnt + 1u);
            put(&sec, w + q, end - q);
            if (id == SEC_GLOBAL) {
                put_u8(&sec, WT_I64); put_u8(&sec, 1); put_u8(&sec, 0x42); put_u8(&sec, 0x00);
                put_u8(&sec, 0x0B);
                emitted_global = 1;
            } else {
                put_uleb(&sec, sizeof fuel_name - 1u);
                put(&sec, fuel_name, sizeof fuel_name - 1u);
                put_u8(&sec, 3);
                put_uleb(&sec, nglobals);
                emitted_export = 1;
            }
            put_section(&o, id, &sec);
        } else if (id == SEC_CODE) {
            int r = meter_code(w, start, end, nglobals, &sec);
            if (r == -2) {
                rc = ZXV_E_NOMEM;
                goto done;
            }
            if (r != 0) goto done;
            put_section(&o, id, &sec);
        } else {
            put_u8(&o, id);
            put_uleb(&o, size);
            put(&o, w + start, size);
        }
    }
    if (!emitted_global) {
        sec.n = 0;
        put_uleb(&sec, 1);
        put_u8(&sec, WT_I64); put_u8(&sec, 1); put_u8(&sec, 0x42); put_u8(&sec, 0x00);
        put_u8(&sec, 0x0B);
        put_section(&o, SEC_GLOBAL, &sec);
    }
    if (!emitted_export) {
        sec.n = 0;
        put_uleb(&sec, 1);
        put_uleb(&sec, sizeof fuel_name - 1u);
        put(&sec, fuel_name, sizeof fuel_name - 1u);
        put_u8(&sec, 3);
        put_uleb(&sec, nglobals);
        put_section(&o, SEC_EXPORT, &sec);
    }
    if (o.oom || sec.oom) {
        rc = ZXV_E_NOMEM;
        goto done;
    }
    free(sec.p);
    *out = o.p;
    *out_len = o.n;
    return ZW_OK;
done:
    free(sec.p);
    free(o.p);
    return rc;
}
