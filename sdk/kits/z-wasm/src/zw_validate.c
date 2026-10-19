/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zw_validate.c — the Z-Wasm load-time validator.
 *
 * Parses a WebAssembly 2.0 binary section by section and every function
 * body instruction by instruction, and refuses anything outside the Z-Wasm
 * profile (no floats, no SIMD, bounded memory and tables, allow-listed
 * function imports only, no start function, size limits). Index
 * immediates are bounds-checked and block structure is checked; operand
 * types are NOT (that is wasm3's load-time validator's job, and the
 * README says so).
 *
 * Integer code only (R11): no float or double anywhere in this file. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zw_wasm.h"

typedef struct {
    const zw_policy *pol;
    int allow_reserved;
    const uint8_t *b;
    size_t n;

    /* first failure */
    int32_t reason;
    uint32_t section;
    uint32_t func;
    uint64_t off;
    char msg[240];

    /* module facts gathered so far */
    uint32_t cur_sec;
    uint32_t ntypes;
    uint32_t *type_off;    /* offset of each functype (after 0x60) */
    uint32_t *type_params; /* parameter count of each functype     */
    uint32_t nfimports;
    uint32_t nfuncs_decl;
    uint32_t nfuncs;
    size_t func_sec_pos;   /* first typeidx of the function section */
    size_t func_sec_end;
    uint32_t ntables;
    uint32_t nmems;
    uint32_t nglobals;
    uint8_t gmut[(4096 + 8) / 8]; /* policy max 4096, +1 for the metered fuel global */
    uint32_t nelem;
    int has_datacount;
    uint32_t datacount;
    int saw_code;
    int saw_data;
    uint64_t data_bytes;
    uint64_t elem_total;
    uint8_t ctl[1001];     /* control stack kinds, see CTL_* */
} V;

#define CTL_BLOCK 0
#define CTL_IF    1
#define CTL_ELSE  2

#define LIM(v_, w_) ((v_)->pol->lim[(w_)])

static int fail(V *v, int32_t reason, size_t off, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

static int fail(V *v, int32_t reason, size_t off, const char *fmt, ...) {
    va_list ap;
    if (v->reason != ZW_R_OK) return -1;
    v->reason = reason;
    v->off = off;
    va_start(ap, fmt);
    vsnprintf(v->msg, sizeof v->msg, fmt, ap);
    va_end(ap);
    return -1;
}

static const char *sec_name(uint32_t id) {
    static const char *names[] = {"custom", "type",   "import", "function", "table",
                                  "memory", "global", "export", "start",    "element",
                                  "code",   "data",   "data count"};
    return id <= 12u ? names[id] : "header";
}

static const char *float_op_name(uint32_t op) {
    static const char *const fc[8] = {"i32.trunc_sat_f32_s", "i32.trunc_sat_f32_u",
                                      "i32.trunc_sat_f64_s", "i32.trunc_sat_f64_u",
                                      "i64.trunc_sat_f32_s", "i64.trunc_sat_f32_u",
                                      "i64.trunc_sat_f64_s", "i64.trunc_sat_f64_u"};
    static const char *const cmp[12] = {"f32.eq", "f32.ne", "f32.lt", "f32.gt", "f32.le", "f32.ge",
                                        "f64.eq", "f64.ne", "f64.lt", "f64.gt", "f64.le", "f64.ge"};
    static const char *const ar[53] = {
        "f32.abs",          "f32.neg",           "f32.ceil",          "f32.floor",
        "f32.trunc",        "f32.nearest",       "f32.sqrt",          "f32.add",
        "f32.sub",          "f32.mul",           "f32.div",           "f32.min",
        "f32.max",          "f32.copysign",      "f64.abs",           "f64.neg",
        "f64.ceil",         "f64.floor",         "f64.trunc",         "f64.nearest",
        "f64.sqrt",         "f64.add",           "f64.sub",           "f64.mul",
        "f64.div",          "f64.min",           "f64.max",           "f64.copysign",
        "i32.wrap_i64",     "i32.trunc_f32_s",   "i32.trunc_f32_u",   "i32.trunc_f64_s",
        "i32.trunc_f64_u",  "i64.extend_i32_s",  "i64.extend_i32_u",  "i64.trunc_f32_s",
        "i64.trunc_f32_u",  "i64.trunc_f64_s",   "i64.trunc_f64_u",   "f32.convert_i32_s",
        "f32.convert_i32_u", "f32.convert_i64_s", "f32.convert_i64_u", "f32.demote_f64",
        "f64.convert_i32_s", "f64.convert_i32_u", "f64.convert_i64_s", "f64.convert_i64_u",
        "f64.promote_f32",  "i32.reinterpret_f32", "i64.reinterpret_f64",
        "f32.reinterpret_i32", "f64.reinterpret_i64"};
    switch (op) {
    case 0x2A: return "f32.load";
    case 0x2B: return "f64.load";
    case 0x38: return "f32.store";
    case 0x39: return "f64.store";
    case 0x43: return "f32.const";
    case 0x44: return "f64.const";
    default: break;
    }
    if (op >= ZW_OP_FC(0) && op <= ZW_OP_FC(7)) return fc[op - ZW_OP_FC(0)];
    if (op >= 0x5B && op <= 0x66) return cmp[op - 0x5B];
    if (op >= 0x8B && op <= 0xBF) return ar[op - 0x8B];
    return "?";
}

static const char *bulk_op_name(uint32_t op) {
    static const char *const n[10] = {"memory.init", "data.drop",  "memory.copy", "memory.fill",
                                      "table.init",  "elem.drop",  "table.copy",  "table.grow",
                                      "table.size",  "table.fill"};
    return n[op - ZW_OP_FC(8)];
}

/* ----- small readers that report ----- */

#define RD_U32(v_, end_, pos_, out_, what_)                                                    \
    do {                                                                                       \
        if (zw_rd_u32((v_)->b, (end_), (pos_), (out_)) != 0)                                   \
            return fail((v_), ZW_R_MALFORMED, *(pos_), "malformed %s (bad LEB128 or "          \
                        "truncated) in the %s section", (what_), sec_name((v_)->cur_sec));    \
    } while (0)

#define RD_U8(v_, end_, pos_, out_, what_)                                                     \
    do {                                                                                       \
        if (zw_rd_u8((v_)->b, (end_), (pos_), (out_)) != 0)                                    \
            return fail((v_), ZW_R_MALFORMED, *(pos_), "truncated %s in the %s section",       \
                        (what_), sec_name((v_)->cur_sec));                                    \
    } while (0)

/* A value type. `where` says where it appeared (for the message). */
static int check_valtype(V *v, uint8_t t, size_t off, const char *where) {
    switch (t) {
    case WT_I32:
    case WT_I64:
    case WT_FUNCREF:
    case WT_EXTERNREF:
        return 0;
    case WT_F32:
    case WT_F64:
        return fail(v, ZW_R_FLOAT_TYPE, off,
                    "FLOAT_TYPE: %s value type in %s: Z-Wasm plugins use integers and exact "
                    "rationals (zxv.rat_*), never floats",
                    t == WT_F32 ? "f32" : "f64", where);
    case WT_V128:
        return fail(v, ZW_R_SIMD, off, "SIMD: v128 value type in %s", where);
    default:
        return fail(v, ZW_R_MALFORMED, off, "unknown value type 0x%02X in %s", (unsigned) t,
                    where);
    }
}

/* name: u32 length + UTF-8 bytes. Gives the bytes' offset and length. */
static int rd_name(V *v, size_t end, size_t *pos, size_t *out_off, uint32_t *out_len,
                   const char *what) {
    uint32_t len;
    RD_U32(v, end, pos, &len, what);
    if (len > end - *pos)
        return fail(v, ZW_R_MALFORMED, *pos, "%s runs past the end of the %s section", what,
                    sec_name(v->cur_sec));
    if (!zw_utf8_ok(v->b + *pos, len))
        return fail(v, ZW_R_MALFORMED, *pos, "%s is not valid UTF-8", what);
    *out_off = *pos;
    *out_len = len;
    *pos += len;
    return 0;
}

/* limits: flag, min[, max]. Returns has_max via *out_has_max. */
static int rd_limits(V *v, size_t end, size_t *pos, uint8_t *out_flag, uint32_t *out_min,
                     uint32_t *out_max) {
    RD_U8(v, end, pos, out_flag, "limits flag");
    if (*out_flag > 7u)
        return fail(v, ZW_R_MALFORMED, *pos - 1u, "unknown limits flag 0x%02X",
                    (unsigned) *out_flag);
    RD_U32(v, end, pos, out_min, "limits minimum");
    *out_max = 0;
    if (*out_flag & 1u) {
        RD_U32(v, end, pos, out_max, "limits maximum");
        if (*out_min > *out_max)
            return fail(v, ZW_R_MALFORMED, *pos, "limits minimum %u is above the maximum %u",
                        (unsigned) *out_min, (unsigned) *out_max);
    }
    return 0;
}

/* A constant expression of type `want` (a value type). */
static int const_expr(V *v, size_t end, size_t *pos, uint8_t want, const char *where) {
    zw_ins ins;
    size_t err;
    size_t at = *pos;
    int32_t r = zw_decode(v->b, end, at, &ins, &err);
    uint8_t got = 0;
    if (r != ZW_R_OK) return fail(v, r, err, "bad instruction in the constant expression of %s", where);
    if (zw_is_float_op(ins.op))
        return fail(v, ZW_R_FLOAT_OP, at, "FLOAT_OP: %s (0x%02X) in the constant expression of %s",
                    float_op_name(ins.op), (unsigned) ins.op, where);
    switch (ins.op) {
    case 0x41: got = WT_I32; break;
    case 0x42: got = WT_I64; break;
    case 0xD0: got = (uint8_t) ins.idx; break;
    case 0xD2:
        if (ins.idx >= v->nfuncs)
            return fail(v, ZW_R_INDEX, at, "ref.func %u out of range (%u functions) in %s",
                        (unsigned) ins.idx, (unsigned) v->nfuncs, where);
        got = WT_FUNCREF;
        break;
    case 0x23: /* global.get: only imported globals, and Z-Wasm imports none */
        return fail(v, ZW_R_INDEX, at, "global.get %u in %s: no imported globals exist",
                    (unsigned) ins.idx, where);
    default:
        return fail(v, ZW_R_MALFORMED, at, "opcode 0x%02X is not a constant instruction (%s)",
                    (unsigned) (ins.op & 0xFFu), where);
    }
    if (got != want)
        return fail(v, ZW_R_MALFORMED, at, "constant expression of %s has the wrong type", where);
    at += ins.len;
    if (at >= end || v->b[at] != 0x0B)
        return fail(v, ZW_R_MALFORMED, at, "constant expression of %s must be one instruction "
                    "then end", where);
    *pos = at + 1u;
    return 0;
}

/* ----- sections ----- */

static int sec_type(V *v, size_t pos, size_t end) {
    uint32_t count;
    RD_U32(v, end, &pos, &count, "type count");
    if (count > LIM(v, ZW_LIM_TYPES))
        return fail(v, ZW_R_LIMIT, pos, "LIMIT: %u types, the policy allows %llu",
                    (unsigned) count, (unsigned long long) LIM(v, ZW_LIM_TYPES));
    if (count > end - pos) /* each type takes at least 3 bytes; cheap sanity bound */
        return fail(v, ZW_R_MALFORMED, pos, "type count %u exceeds the section", (unsigned) count);
    if (count) {
        v->type_off = (uint32_t *) calloc((size_t) count * 2u, sizeof(uint32_t));
        if (!v->type_off) return fail(v, ZW_R_LIMIT, pos, "out of memory for %u types", (unsigned) count);
        v->type_params = v->type_off + count;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t form, t;
        uint32_t np, nr;
        char where[48];
        RD_U8(v, end, &pos, &form, "functype");
        if (form != 0x60)
            return fail(v, ZW_R_MALFORMED, pos - 1u, "type %u is not a function type (0x%02X)",
                        (unsigned) i, (unsigned) form);
        v->type_off[i] = (uint32_t) pos;
        RD_U32(v, end, &pos, &np, "parameter count");
        if (np > LIM(v, ZW_LIM_PARAMS))
            return fail(v, ZW_R_LIMIT, pos, "LIMIT: type %u has %u parameters, the policy "
                        "allows %llu", (unsigned) i, (unsigned) np,
                        (unsigned long long) LIM(v, ZW_LIM_PARAMS));
        v->type_params[i] = np;
        snprintf(where, sizeof where, "the parameters of type %u", (unsigned) i);
        for (uint32_t k = 0; k < np; k++) {
            RD_U8(v, end, &pos, &t, "parameter type");
            if (check_valtype(v, t, pos - 1u, where)) return -1;
        }
        RD_U32(v, end, &pos, &nr, "result count");
        if (nr > LIM(v, ZW_LIM_PARAMS))
            return fail(v, ZW_R_LIMIT, pos, "LIMIT: type %u has %u results", (unsigned) i,
                        (unsigned) nr);
        snprintf(where, sizeof where, "the results of type %u", (unsigned) i);
        for (uint32_t k = 0; k < nr; k++) {
            RD_U8(v, end, &pos, &t, "result type");
            if (check_valtype(v, t, pos - 1u, where)) return -1;
        }
    }
    v->ntypes = count;
    return pos == end ? 0 : fail(v, ZW_R_MALFORMED, pos, "type section size mismatch");
}

/* The wasm3 signature string of type `ti`, or "" if it cannot have one
 * (more than one result, or a non-integer type). */
static void type_sig(V *v, uint32_t ti, char *out, size_t cap) {
    size_t pos = v->type_off[ti];
    size_t end = v->n;
    uint32_t np, nr;
    uint8_t t;
    char params[ZW_SIG_MAX + 1];
    size_t k = 0;
    out[0] = '\0';
    if (zw_rd_u32(v->b, end, &pos, &np) != 0 || np > 16u) return;
    for (uint32_t i = 0; i < np; i++) {
        if (zw_rd_u8(v->b, end, &pos, &t) != 0) return;
        if (t == WT_I32) params[k++] = 'i';
        else if (t == WT_I64) params[k++] = 'I';
        else return;
    }
    params[k] = '\0';
    if (zw_rd_u32(v->b, end, &pos, &nr) != 0 || nr > 1u) return;
    char r = 'v';
    if (nr == 1u) {
        if (zw_rd_u8(v->b, end, &pos, &t) != 0) return;
        if (t == WT_I32) r = 'i';
        else if (t == WT_I64) r = 'I';
        else return;
    }
    snprintf(out, cap, "%c(%s)", r, params);
}

static int sec_import(V *v, size_t pos, size_t end) {
    uint32_t count;
    RD_U32(v, end, &pos, &count, "import count");
    if (count > LIM(v, ZW_LIM_IMPORTS))
        return fail(v, ZW_R_LIMIT, pos, "LIMIT: %u imports, the policy allows %llu",
                    (unsigned) count, (unsigned long long) LIM(v, ZW_LIM_IMPORTS));
    for (uint32_t i = 0; i < count; i++) {
        size_t moff, foff, at, ent = pos;
        uint32_t mlen, flen;
        uint8_t kind;
        if (rd_name(v, end, &pos, &moff, &mlen, "import module name")) return -1;
        if (rd_name(v, end, &pos, &foff, &flen, "import field name")) return -1;
        at = pos;
        RD_U8(v, end, &pos, &kind, "import kind");
        const char *m = (const char *) v->b + moff;
        const char *f = (const char *) v->b + foff;
        int ml = mlen > 64u ? 64 : (int) mlen;
        int fl = flen > 64u ? 64 : (int) flen;
        if (kind == 0) {
            uint32_t ti;
            RD_U32(v, end, &pos, &ti, "import type index");
            if (ti >= v->ntypes)
                return fail(v, ZW_R_INDEX, at, "import %.*s.%.*s uses type %u (%u types)", ml, m,
                            fl, f, (unsigned) ti, (unsigned) v->ntypes);
            const zw_allow *a = NULL;
            for (uint32_t k = 0; k < v->pol->n_allow; k++) {
                const zw_allow *c = &v->pol->allow[k];
                if (strlen(c->module) == mlen && strlen(c->name) == flen &&
                    memcmp(c->module, m, mlen) == 0 && memcmp(c->name, f, flen) == 0) {
                    a = c;
                    break;
                }
            }
            if (!a)
                return fail(v, ZW_R_IMPORT_DENIED, ent,
                            "IMPORT_DENIED: function import %.*s.%.*s is not on the host's "
                            "allow-list", ml, m, fl, f);
            char sig[ZW_SIG_MAX + 8];
            type_sig(v, ti, sig, sizeof sig);
            if (strcmp(sig, a->sig) != 0)
                return fail(v, ZW_R_IMPORT_SIG, at,
                            "IMPORT_SIG: %.*s.%.*s is imported as \"%s\", the host provides "
                            "\"%s\"", ml, m, fl, f, sig[0] ? sig : "(non-integer)", a->sig);
            v->nfimports++;
            v->nfuncs++;
        } else if (kind == 3) {
            uint8_t t, mut;
            RD_U8(v, end, &pos, &t, "global import type");
            char where[96];
            snprintf(where, sizeof where, "global import %.*s.%.*s", ml, m, fl, f);
            if (check_valtype(v, t, pos - 1u, where)) return -1;
            RD_U8(v, end, &pos, &mut, "global import mutability");
            return fail(v, ZW_R_IMPORT_KIND, at, "IMPORT_KIND: global import %.*s.%.*s: plugins "
                        "may import only host functions", ml, m, fl, f);
        } else if (kind == 1 || kind == 2) {
            return fail(v, ZW_R_IMPORT_KIND, at, "IMPORT_KIND: %s import %.*s.%.*s: plugins may "
                        "import only host functions", kind == 1 ? "table" : "memory", ml, m, fl, f);
        } else {
            return fail(v, ZW_R_MALFORMED, at, "unknown import kind 0x%02X", (unsigned) kind);
        }
    }
    return pos == end ? 0 : fail(v, ZW_R_MALFORMED, pos, "import section size mismatch");
}

static int sec_function(V *v, size_t pos, size_t end) {
    uint32_t count;
    RD_U32(v, end, &pos, &count, "function count");
    if ((uint64_t) count + v->nfimports > LIM(v, ZW_LIM_FUNCTIONS))
        return fail(v, ZW_R_LIMIT, pos, "LIMIT: %u functions, the policy allows %llu",
                    (unsigned) (count + v->nfimports), (unsigned long long) LIM(v, ZW_LIM_FUNCTIONS));
    v->func_sec_pos = pos;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t ti;
        size_t at = pos;
        RD_U32(v, end, &pos, &ti, "function type index");
        if (ti >= v->ntypes)
            return fail(v, ZW_R_INDEX, at, "function %u uses type %u (%u types)",
                        (unsigned) (v->nfimports + i), (unsigned) ti, (unsigned) v->ntypes);
    }
    v->func_sec_end = pos;
    v->nfuncs_decl = count;
    v->nfuncs = v->nfimports + count;
    return pos == end ? 0 : fail(v, ZW_R_MALFORMED, pos, "function section size mismatch");
}

static int sec_table(V *v, size_t pos, size_t end) {
    uint32_t count;
    RD_U32(v, end, &pos, &count, "table count");
    if (count > 1u)
        return fail(v, ZW_R_TABLE_LIMIT, pos, "TABLE_LIMIT: %u tables, Z-Wasm allows one",
                    (unsigned) count);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t rt, flag;
        uint32_t mn, mx;
        size_t at;
        RD_U8(v, end, &pos, &rt, "table element type");
        if (rt != WT_FUNCREF && rt != WT_EXTERNREF)
            return fail(v, ZW_R_MALFORMED, pos - 1u, "table element type 0x%02X", (unsigned) rt);
        at = pos;
        if (rd_limits(v, end, &pos, &flag, &mn, &mx)) return -1;
        if (flag > 1u)
            return fail(v, ZW_R_MALFORMED, at, "table limits flag 0x%02X", (unsigned) flag);
        if (!(flag & 1u))
            return fail(v, ZW_R_TABLE_LIMIT, at, "TABLE_LIMIT: the table declares no maximum");
        if (mx > LIM(v, ZW_LIM_TABLE_ELEMS))
            return fail(v, ZW_R_TABLE_LIMIT, at, "TABLE_LIMIT: table maximum %u, the policy "
                        "allows %llu", (unsigned) mx, (unsigned long long) LIM(v, ZW_LIM_TABLE_ELEMS));
    }
    v->ntables = count;
    return pos == end ? 0 : fail(v, ZW_R_MALFORMED, pos, "table section size mismatch");
}

static int sec_memory(V *v, size_t pos, size_t end) {
    uint32_t count;
    RD_U32(v, end, &pos, &count, "memory count");
    if (count > 1u)
        return fail(v, ZW_R_UNSUPPORTED, pos, "UNSUPPORTED: %u memories (multi-memory)",
                    (unsigned) count);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t flag;
        uint32_t mn, mx;
        size_t at = pos;
        if (rd_limits(v, end, &pos, &flag, &mn, &mx)) return -1;
        if (flag == 2u || flag == 3u)
            return fail(v, ZW_R_UNSUPPORTED, at, "UNSUPPORTED: shared memory (threads)");
        if (flag >= 4u)
            return fail(v, ZW_R_UNSUPPORTED, at, "UNSUPPORTED: 64-bit memory (memory64)");
        if (flag == 0u)
            return fail(v, ZW_R_MEMORY_NO_MAX, at, "MEMORY_NO_MAX: the memory declares no "
                        "maximum (link with --max-memory=<bytes>)");
        if (mx > ZW_PAGE_LIMIT_MAX || mn > ZW_PAGE_LIMIT_MAX)
            return fail(v, ZW_R_MALFORMED, at, "memory of more than 65536 pages");
        if (mx > LIM(v, ZW_LIM_MEMORY_PAGES))
            return fail(v, ZW_R_MEMORY_LIMIT, at, "MEMORY_LIMIT: memory maximum %u pages, the "
                        "policy allows %llu", (unsigned) mx,
                        (unsigned long long) LIM(v, ZW_LIM_MEMORY_PAGES));
    }
    v->nmems = count;
    return pos == end ? 0 : fail(v, ZW_R_MALFORMED, pos, "memory section size mismatch");
}

static int sec_global(V *v, size_t pos, size_t end) {
    uint32_t count;
    RD_U32(v, end, &pos, &count, "global count");
    if (count > LIM(v, ZW_LIM_GLOBALS) || count > 4097u)
        return fail(v, ZW_R_LIMIT, pos, "LIMIT: %u globals, the policy allows %llu",
                    (unsigned) count, (unsigned long long) LIM(v, ZW_LIM_GLOBALS));
    for (uint32_t i = 0; i < count; i++) {
        uint8_t t, mut;
        char where[32];
        snprintf(where, sizeof where, "global %u", (unsigned) i);
        RD_U8(v, end, &pos, &t, "global type");
        if (check_valtype(v, t, pos - 1u, where)) return -1;
        RD_U8(v, end, &pos, &mut, "global mutability");
        if (mut > 1u)
            return fail(v, ZW_R_MALFORMED, pos - 1u, "global %u mutability 0x%02X", (unsigned) i,
                        (unsigned) mut);
        if (mut) v->gmut[i >> 3] |= (uint8_t) (1u << (i & 7u));
        if (const_expr(v, end, &pos, t, where)) return -1;
    }
    v->nglobals = count;
    return pos == end ? 0 : fail(v, ZW_R_MALFORMED, pos, "global section size mismatch");
}

static int sec_export(V *v, size_t pos, size_t end) {
    uint32_t count;
    size_t first;
    RD_U32(v, end, &pos, &count, "export count");
    if (count > LIM(v, ZW_LIM_EXPORTS))
        return fail(v, ZW_R_LIMIT, pos, "LIMIT: %u exports, the policy allows %llu",
                    (unsigned) count, (unsigned long long) LIM(v, ZW_LIM_EXPORTS));
    first = pos;
    for (uint32_t i = 0; i < count; i++) {
        size_t noff, at;
        uint32_t nlen, idx;
        uint8_t kind;
        if (rd_name(v, end, &pos, &noff, &nlen, "export name")) return -1;
        const char *nm = (const char *) v->b + noff;
        int pl = nlen > 64u ? 64 : (int) nlen;
        if (nlen >= 5u && memcmp(nm, "__zw_", 5) == 0 &&
            !(v->allow_reserved && nlen == sizeof ZW_FUEL_EXPORT - 1u &&
              memcmp(nm, ZW_FUEL_EXPORT, nlen) == 0))
            return fail(v, ZW_R_RESERVED_NAME, noff, "RESERVED_NAME: export \"%.*s\": the "
                        "\"__zw_\" prefix is reserved for the kit", pl, nm);
        /* uniqueness: re-scan the earlier exports (count is bounded) */
        {
            size_t q = first;
            for (uint32_t k = 0; k < i; k++) {
                uint32_t l2;
                (void) zw_rd_u32(v->b, end, &q, &l2);
                if (l2 == nlen && memcmp(v->b + q, nm, nlen) == 0)
                    return fail(v, ZW_R_MALFORMED, noff, "duplicate export name \"%.*s\"", pl, nm);
                q += l2 + 1u; /* name + kind */
                (void) zw_rd_u32(v->b, end, &q, &l2);
            }
        }
        at = pos;
        RD_U8(v, end, &pos, &kind, "export kind");
        RD_U32(v, end, &pos, &idx, "export index");
        uint32_t lim;
        switch (kind) {
        case 0: lim = v->nfuncs; break;
        case 1: lim = v->ntables; break;
        case 2: lim = v->nmems; break;
        case 3: lim = v->nglobals; break;
        default:
            return fail(v, ZW_R_MALFORMED, at, "unknown export kind 0x%02X", (unsigned) kind);
        }
        if (idx >= lim)
            return fail(v, ZW_R_INDEX, at, "export \"%.*s\" index %u out of range", pl, nm,
                        (unsigned) idx);
    }
    return pos == end ? 0 : fail(v, ZW_R_MALFORMED, pos, "export section size mismatch");
}

static int elem_funcidx_vec(V *v, size_t end, size_t *pos) {
    uint32_t n, x;
    RD_U32(v, end, pos, &n, "element count");
    v->elem_total += n;
    if (v->elem_total > LIM(v, ZW_LIM_ELEM_SEGMENTS))
        return fail(v, ZW_R_LIMIT, *pos, "LIMIT: %llu table elements, the policy allows %llu",
                    (unsigned long long) v->elem_total,
                    (unsigned long long) LIM(v, ZW_LIM_ELEM_SEGMENTS));
    for (uint32_t i = 0; i < n; i++) {
        size_t at = *pos;
        RD_U32(v, end, pos, &x, "element function index");
        if (x >= v->nfuncs)
            return fail(v, ZW_R_INDEX, at, "element function index %u out of range", (unsigned) x);
    }
    return 0;
}

static int elem_expr_vec(V *v, size_t end, size_t *pos, uint8_t rt) {
    uint32_t n;
    RD_U32(v, end, pos, &n, "element count");
    v->elem_total += n;
    if (v->elem_total > LIM(v, ZW_LIM_ELEM_SEGMENTS))
        return fail(v, ZW_R_LIMIT, *pos, "LIMIT: %llu table elements, the policy allows %llu",
                    (unsigned long long) v->elem_total,
                    (unsigned long long) LIM(v, ZW_LIM_ELEM_SEGMENTS));
    for (uint32_t i = 0; i < n; i++)
        if (const_expr(v, end, pos, rt, "an element")) return -1;
    return 0;
}

static int sec_element(V *v, size_t pos, size_t end) {
    uint32_t count;
    RD_U32(v, end, &pos, &count, "element segment count");
    if (count > end - pos)
        return fail(v, ZW_R_MALFORMED, pos, "element segment count exceeds the section");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t flags, table = 0;
        uint8_t kind, rt = WT_FUNCREF;
        size_t at = pos;
        RD_U32(v, end, &pos, &flags, "element segment flags");
        if (flags > 7u)
            return fail(v, ZW_R_MALFORMED, at, "element segment flags %u", (unsigned) flags);
        int passive = (flags & 1u) != 0;
        int explicit_table = !passive && (flags & 2u);
        int exprs = (flags & 4u) != 0;
        if (explicit_table) RD_U32(v, end, &pos, &table, "element table index");
        if (!passive) {
            if (table >= v->ntables)
                return fail(v, ZW_R_INDEX, at, "element segment %u: table %u does not exist",
                            (unsigned) i, (unsigned) table);
            if (const_expr(v, end, &pos, WT_I32, "an element segment offset")) return -1;
        }
        if (flags != 0u && flags != 4u) { /* elemkind or reftype byte */
            RD_U8(v, end, &pos, &kind, "element kind");
            if (!exprs) {
                if (kind != 0x00)
                    return fail(v, ZW_R_MALFORMED, pos - 1u, "element kind 0x%02X", (unsigned) kind);
            } else {
                if (kind != WT_FUNCREF && kind != WT_EXTERNREF)
                    return fail(v, ZW_R_MALFORMED, pos - 1u, "element reftype 0x%02X",
                                (unsigned) kind);
                rt = kind;
            }
        }
        if (exprs ? elem_expr_vec(v, end, &pos, rt) : elem_funcidx_vec(v, end, &pos)) return -1;
    }
    v->nelem = count;
    return pos == end ? 0 : fail(v, ZW_R_MALFORMED, pos, "element section size mismatch");
}

static uint32_t natural_align(uint32_t op) {
    static const uint8_t a[0x3E - 0x28 + 1] = {2, 3, 2, 3, 0, 0, 1, 1, 0, 0, 1, 1, 2,
                                               2, 2, 3, 2, 3, 0, 1, 0, 1, 2};
    return a[op - 0x28];
}

static int need_memory(V *v, size_t at, uint32_t fidx) {
    if (v->nmems) return 0;
    return fail(v, ZW_R_INDEX, at, "function %u uses memory but the module has none",
                (unsigned) fidx);
}

static int need_table(V *v, size_t at, uint32_t fidx, uint32_t t) {
    if (t < v->ntables) return 0;
    return fail(v, ZW_R_INDEX, at, "function %u: table %u does not exist", (unsigned) fidx,
                (unsigned) t);
}

static int code_body(V *v, size_t pos, size_t end, uint32_t fidx, uint32_t nparams) {
    uint32_t ngroups;
    uint64_t nlocals = nparams;
    uint32_t depth = 1;
    char where[48];
    v->func = fidx;
    snprintf(where, sizeof where, "the locals of function %u", (unsigned) fidx);
    RD_U32(v, end, &pos, &ngroups, "local group count");
    for (uint32_t g = 0; g < ngroups; g++) {
        uint32_t cnt;
        uint8_t t;
        RD_U32(v, end, &pos, &cnt, "local count");
        nlocals += cnt;
        if (nlocals - nparams > LIM(v, ZW_LIM_LOCALS))
            return fail(v, ZW_R_LIMIT, pos, "LIMIT: function %u declares more than %llu locals",
                        (unsigned) fidx, (unsigned long long) LIM(v, ZW_LIM_LOCALS));
        RD_U8(v, end, &pos, &t, "local type");
        if (check_valtype(v, t, pos - 1u, where)) return -1;
    }
    v->ctl[0] = CTL_BLOCK;
    while (pos < end) {
        zw_ins ins;
        size_t err;
        size_t at = pos;
        int32_t r = zw_decode(v->b, end, pos, &ins, &err);
        if (r == ZW_R_SIMD)
            return fail(v, r, err, "SIMD: 0xFD-prefixed (v128) instruction in function %u",
                        (unsigned) fidx);
        if (r == ZW_R_UNSUPPORTED)
            return fail(v, r, err, "UNSUPPORTED: opcode 0x%02X in function %u is outside Wasm "
                        "2.0 integer code (exceptions, tail calls, GC or threads)",
                        (unsigned) v->b[err], (unsigned) fidx);
        if (r != ZW_R_OK)
            return fail(v, r, err, "malformed instruction in function %u", (unsigned) fidx);
        if (zw_is_float_op(ins.op)) {
            if (ins.op >= 0xFC00u)
                return fail(v, ZW_R_FLOAT_OP, at, "FLOAT_OP: %s (0xFC %u) in function %u: "
                            "Z-Wasm plugins use integers and exact rationals (zxv.rat_*), never "
                            "floats", float_op_name(ins.op), (unsigned) (ins.op & 0xFFu),
                            (unsigned) fidx);
            return fail(v, ZW_R_FLOAT_OP, at, "FLOAT_OP: %s (0x%02X) in function %u: Z-Wasm "
                        "plugins use integers and exact rationals (zxv.rat_*), never floats",
                        float_op_name(ins.op), (unsigned) ins.op, (unsigned) fidx);
        }
        if (zw_is_bulk_op(ins.op) && !LIM(v, ZW_LIM_BULK_MEMORY))
            return fail(v, ZW_R_UNSUPPORTED, at, "UNSUPPORTED: %s (0xFC %u) in function %u: bulk "
                        "memory is off in this policy (ZW_LIM_BULK_MEMORY; compile with "
                        "-mno-bulk-memory)", bulk_op_name(ins.op), (unsigned) (ins.op & 0xFFu),
                        (unsigned) fidx);
        switch (ins.op) {
        case 0x02: case 0x03: case 0x04:
            if (ins.bt_kind == 1) {
                snprintf(where, sizeof where, "a block type in function %u", (unsigned) fidx);
                if (check_valtype(v, (uint8_t) ins.idx, at + 1u, where)) return -1;
            } else if (ins.bt_kind == 2 && ins.idx >= v->ntypes) {
                return fail(v, ZW_R_INDEX, at, "function %u: block type %u out of range",
                            (unsigned) fidx, (unsigned) ins.idx);
            }
            if (depth >= LIM(v, ZW_LIM_NESTING) || depth >= sizeof v->ctl)
                return fail(v, ZW_R_LIMIT, at, "LIMIT: function %u nests blocks deeper than %llu",
                            (unsigned) fidx, (unsigned long long) LIM(v, ZW_LIM_NESTING));
            v->ctl[depth++] = ins.op == 0x04 ? CTL_IF : CTL_BLOCK;
            break;
        case 0x05:
            if (depth < 2u || v->ctl[depth - 1u] != CTL_IF)
                return fail(v, ZW_R_MALFORMED, at, "function %u: else without if", (unsigned) fidx);
            v->ctl[depth - 1u] = CTL_ELSE;
            break;
        case 0x0B:
            depth--;
            if (depth == 0u && at + 1u != end)
                return fail(v, ZW_R_MALFORMED, at + 1u, "function %u: code after the final end",
                            (unsigned) fidx);
            break;
        case 0x0C: case 0x0D:
            if (ins.idx >= depth)
                return fail(v, ZW_R_INDEX, at, "function %u: branch depth %u out of range",
                            (unsigned) fidx, (unsigned) ins.idx);
            break;
        case 0x0E: {
            size_t q = ins.vec_pos;
            for (uint32_t k = 0; k <= ins.vec_n; k++) {
                uint32_t l;
                (void) zw_rd_u32(v->b, end, &q, &l);
                if (l >= depth)
                    return fail(v, ZW_R_INDEX, at, "function %u: br_table depth %u out of range",
                                (unsigned) fidx, (unsigned) l);
            }
            break;
        }
        case 0x10: case 0xD2:
            if (ins.idx >= v->nfuncs)
                return fail(v, ZW_R_INDEX, at, "function %u: function index %u out of range",
                            (unsigned) fidx, (unsigned) ins.idx);
            break;
        case 0x11:
            if (ins.idx >= v->ntypes)
                return fail(v, ZW_R_INDEX, at, "function %u: call_indirect type %u out of range",
                            (unsigned) fidx, (unsigned) ins.idx);
            if (need_table(v, at, fidx, ins.idx2)) return -1;
            break;
        case 0x1C:
            snprintf(where, sizeof where, "a select in function %u", (unsigned) fidx);
            if (check_valtype(v, (uint8_t) ins.idx, ins.vec_pos, where)) return -1;
            break;
        case 0x20: case 0x21: case 0x22:
            if (ins.idx >= nlocals)
                return fail(v, ZW_R_INDEX, at, "function %u: local %u out of range",
                            (unsigned) fidx, (unsigned) ins.idx);
            break;
        case 0x23: case 0x24:
            if (ins.idx >= v->nglobals)
                return fail(v, ZW_R_INDEX, at, "function %u: global %u out of range",
                            (unsigned) fidx, (unsigned) ins.idx);
            if (ins.op == 0x24 && !(v->gmut[ins.idx >> 3] & (1u << (ins.idx & 7u))))
                return fail(v, ZW_R_MALFORMED, at, "function %u: global.set of immutable "
                            "global %u", (unsigned) fidx, (unsigned) ins.idx);
            break;
        case 0x25: case 0x26:
        case ZW_OP_FC(15): case ZW_OP_FC(16): case ZW_OP_FC(17):
            if (need_table(v, at, fidx, ins.idx)) return -1;
            break;
        case ZW_OP_FC(14):
            if (need_table(v, at, fidx, ins.idx) || need_table(v, at, fidx, ins.idx2)) return -1;
            break;
        case ZW_OP_FC(12):
            if (ins.idx >= v->nelem)
                return fail(v, ZW_R_INDEX, at, "function %u: element segment %u out of range",
                            (unsigned) fidx, (unsigned) ins.idx);
            if (need_table(v, at, fidx, ins.idx2)) return -1;
            break;
        case ZW_OP_FC(13):
            if (ins.idx >= v->nelem)
                return fail(v, ZW_R_INDEX, at, "function %u: element segment %u out of range",
                            (unsigned) fidx, (unsigned) ins.idx);
            break;
        case ZW_OP_FC(8): case ZW_OP_FC(9):
            if (ins.op == ZW_OP_FC(8) && need_memory(v, at, fidx)) return -1;
            if (!v->has_datacount)
                return fail(v, ZW_R_MALFORMED, at, "function %u: memory.init / data.drop needs "
                            "a data count section", (unsigned) fidx);
            if (ins.idx >= v->datacount)
                return fail(v, ZW_R_INDEX, at, "function %u: data segment %u out of range",
                            (unsigned) fidx, (unsigned) ins.idx);
            break;
        case 0x3F: case 0x40: case ZW_OP_FC(10): case ZW_OP_FC(11):
            if (need_memory(v, at, fidx)) return -1;
            break;
        default:
            if (ins.op >= 0x28 && ins.op <= 0x3E) {
                if (need_memory(v, at, fidx)) return -1;
                if (ins.idx > natural_align(ins.op))
                    return fail(v, ZW_R_MALFORMED, at, "function %u: alignment 2^%u above "
                                "natural", (unsigned) fidx, (unsigned) ins.idx);
            }
            break;
        }
        pos += ins.len;
        if (depth == 0u) break;
    }
    if (depth != 0u)
        return fail(v, ZW_R_MALFORMED, end, "function %u: body ends inside a block",
                    (unsigned) fidx);
    v->func = ZW_NO_FUNC;
    return 0;
}

static int sec_code(V *v, size_t pos, size_t end) {
    uint32_t count;
    size_t fpos = v->func_sec_pos;
    RD_U32(v, end, &pos, &count, "code count");
    if (count != v->nfuncs_decl)
        return fail(v, ZW_R_MALFORMED, pos, "code section has %u bodies, function section "
                    "declares %u", (unsigned) count, (unsigned) v->nfuncs_decl);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t size, ti;
        size_t at = pos;
        RD_U32(v, end, &pos, &size, "body size");
        if (size > LIM(v, ZW_LIM_BODY_BYTES)) {
            v->func = v->nfimports + i;
            return fail(v, ZW_R_LIMIT, at, "LIMIT: function %u body is %u bytes, the policy "
                        "allows %llu", (unsigned) (v->nfimports + i), (unsigned) size,
                        (unsigned long long) LIM(v, ZW_LIM_BODY_BYTES));
        }
        if (size > end - pos)
            return fail(v, ZW_R_MALFORMED, at, "function %u body runs past the code section",
                        (unsigned) (v->nfimports + i));
        (void) zw_rd_u32(v->b, v->func_sec_end, &fpos, &ti); /* validated already */
        if (code_body(v, pos, pos + size, v->nfimports + i, v->type_params[ti])) return -1;
        pos += size;
    }
    v->saw_code = 1;
    return pos == end ? 0 : fail(v, ZW_R_MALFORMED, pos, "code section size mismatch");
}

static int sec_data(V *v, size_t pos, size_t end) {
    uint32_t count;
    RD_U32(v, end, &pos, &count, "data segment count");
    if (v->has_datacount && count != v->datacount)
        return fail(v, ZW_R_MALFORMED, pos, "data section has %u segments, data count says %u",
                    (unsigned) count, (unsigned) v->datacount);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t flags, mem = 0, len;
        size_t at = pos;
        RD_U32(v, end, &pos, &flags, "data segment flags");
        if (flags > 2u)
            return fail(v, ZW_R_MALFORMED, at, "data segment flags %u", (unsigned) flags);
        if (flags == 2u) RD_U32(v, end, &pos, &mem, "data memory index");
        if (flags != 1u) {
            if (mem != 0u || !v->nmems)
                return fail(v, ZW_R_INDEX, at, "data segment %u: memory %u does not exist",
                            (unsigned) i, (unsigned) mem);
            if (const_expr(v, end, &pos, WT_I32, "a data segment offset")) return -1;
        }
        RD_U32(v, end, &pos, &len, "data segment length");
        if (len > end - pos)
            return fail(v, ZW_R_MALFORMED, pos, "data segment %u runs past the section",
                        (unsigned) i);
        v->data_bytes += len;
        if (v->data_bytes > LIM(v, ZW_LIM_DATA_BYTES))
            return fail(v, ZW_R_LIMIT, pos, "LIMIT: %llu data bytes, the policy allows %llu",
                        (unsigned long long) v->data_bytes,
                        (unsigned long long) LIM(v, ZW_LIM_DATA_BYTES));
        pos += len;
    }
    v->saw_data = 1;
    return pos == end ? 0 : fail(v, ZW_R_MALFORMED, pos, "data section size mismatch");
}

/* Section order rank (custom sections excluded); 0 = unknown id. */
static unsigned rank(uint8_t id) {
    switch (id) {
    case SEC_TYPE: return 1;
    case SEC_IMPORT: return 2;
    case SEC_FUNCTION: return 3;
    case SEC_TABLE: return 4;
    case SEC_MEMORY: return 5;
    case SEC_GLOBAL: return 6;
    case SEC_EXPORT: return 7;
    case SEC_START: return 8;
    case SEC_ELEMENT: return 9;
    case SEC_DATACOUNT: return 10;
    case SEC_CODE: return 11;
    case SEC_DATA: return 12;
    default: return 0;
    }
}

static int module(V *v) {
    static const uint8_t hdr[8] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
    size_t pos = 8;
    unsigned last = 0;
    v->cur_sec = ZW_SEC_HEADER;
    if (v->n > LIM(v, ZW_LIM_MODULE_BYTES))
        return fail(v, ZW_R_TOO_LARGE, 0, "TOO_LARGE: module is %llu bytes, the policy allows %llu",
                    (unsigned long long) v->n, (unsigned long long) LIM(v, ZW_LIM_MODULE_BYTES));
    if (v->n < 8u || memcmp(v->b, hdr, 4) != 0)
        return fail(v, ZW_R_MALFORMED, 0, "not a WebAssembly binary (bad magic)");
    if (memcmp(v->b + 4, hdr + 4, 4) != 0)
        return fail(v, ZW_R_MALFORMED, 4, "unsupported WebAssembly binary version");
    while (pos < v->n) {
        uint8_t id;
        uint32_t size;
        size_t at = pos;
        id = v->b[pos++];
        v->cur_sec = id <= 12u ? id : ZW_SEC_HEADER;
        if (zw_rd_u32(v->b, v->n, &pos, &size) != 0 || size > v->n - pos)
            return fail(v, ZW_R_MALFORMED, at, "bad size of the section at offset %zu", at);
        size_t end = pos + size;
        if (id != SEC_CUSTOM) {
            unsigned rk = rank(id);
            if (rk == 0)
                return fail(v, ZW_R_MALFORMED, at, "unknown section id %u", (unsigned) id);
            if (rk <= last)
                return fail(v, ZW_R_MALFORMED, at, "%s section out of order or repeated",
                            sec_name(id));
            last = rk;
        }
        int r = 0;
        switch (id) {
        case SEC_CUSTOM: {
            size_t noff;
            uint32_t nlen;
            r = rd_name(v, end, &pos, &noff, &nlen, "custom section name");
            break;
        }
        case SEC_TYPE: r = sec_type(v, pos, end); break;
        case SEC_IMPORT: r = sec_import(v, pos, end); break;
        case SEC_FUNCTION: r = sec_function(v, pos, end); break;
        case SEC_TABLE: r = sec_table(v, pos, end); break;
        case SEC_MEMORY: r = sec_memory(v, pos, end); break;
        case SEC_GLOBAL: r = sec_global(v, pos, end); break;
        case SEC_EXPORT: r = sec_export(v, pos, end); break;
        case SEC_START: {
            uint32_t f;
            RD_U32(v, end, &pos, &f, "start function index");
            r = fail(v, ZW_R_START, at, "START: the module has a start function (%u); Z-Wasm "
                     "refuses it because it would run before the host sets the fuel",
                     (unsigned) f);
            break;
        }
        case SEC_ELEMENT: r = sec_element(v, pos, end); break;
        case SEC_DATACOUNT:
            RD_U32(v, end, &pos, &v->datacount, "data count");
            v->has_datacount = 1;
            r = pos == end ? 0 : fail(v, ZW_R_MALFORMED, pos, "data count section size mismatch");
            break;
        case SEC_CODE: r = sec_code(v, pos, end); break;
        case SEC_DATA: r = sec_data(v, pos, end); break;
        default: break;
        }
        if (r) return -1;
        pos = end;
    }
    v->cur_sec = ZW_SEC_HEADER;
    if (v->nfuncs_decl && !v->saw_code)
        return fail(v, ZW_R_MALFORMED, v->n, "function section without a code section");
    if (v->has_datacount && v->datacount && !v->saw_data)
        return fail(v, ZW_R_MALFORMED, v->n, "data count section without a data section");
    return 0;
}

int32_t zw_validate_ex(const zw_policy *policy, const uint8_t *wasm, size_t len,
                       int allow_reserved, int32_t *out_reason, uint32_t *out_section,
                       uint32_t *out_func, uint64_t *out_offset, char *msg, size_t msg_cap,
                       size_t *out_msg_len) {
    V *v;
    int32_t st;
    if (!policy || (!wasm && len)) return ZXV_E_ARG;
    v = (V *) calloc(1, sizeof *v);
    if (!v) return ZXV_E_NOMEM;
    v->pol = policy;
    v->allow_reserved = allow_reserved;
    v->b = wasm;
    v->n = len;
    v->func = ZW_NO_FUNC;
    v->section = ZW_SEC_HEADER;
    if (module(v) != 0 && v->reason == ZW_R_OK)
        fail(v, ZW_R_MALFORMED, 0, "malformed module");
    if (v->reason != ZW_R_OK) {
        char full[sizeof v->msg + 96];
        v->section = v->cur_sec;
        if (v->func != ZW_NO_FUNC)
            snprintf(full, sizeof full, "%s [%s section, function %u, offset %llu]", v->msg,
                     sec_name(v->section), (unsigned) v->func, (unsigned long long) v->off);
        else if (v->section == ZW_SEC_HEADER)
            snprintf(full, sizeof full, "%s [module, offset %llu]", v->msg,
                     (unsigned long long) v->off);
        else
            snprintf(full, sizeof full, "%s [%s section, offset %llu]", v->msg,
                     sec_name(v->section), (unsigned long long) v->off);
        {
            size_t fl = strlen(full);
            if (fl >= sizeof v->msg) fl = sizeof v->msg - 1u;
            memcpy(v->msg, full, fl);
            v->msg[fl] = '\0';
        }
        st = ZW_E_REJECTED;
    } else {
        snprintf(v->msg, sizeof v->msg, "OK");
        st = ZW_OK;
    }
    if (out_reason) *out_reason = v->reason;
    if (out_section) *out_section = v->reason == ZW_R_OK ? ZW_SEC_HEADER : v->section;
    if (out_func) *out_func = v->reason == ZW_R_OK ? ZW_NO_FUNC : v->func;
    if (out_offset) *out_offset = v->off;
    {
        size_t n = strlen(v->msg);
        if (out_msg_len) *out_msg_len = n;
        if (msg && msg_cap) {
            size_t c = n < msg_cap ? n : msg_cap - 1u;
            memcpy(msg, v->msg, c);
            msg[c] = '\0';
        }
    }
    if (st != ZW_OK) zw_set_error(v->msg);
    free(v->type_off);
    free(v);
    return st;
}

int32_t zw_validate(const zw_policy *policy, const uint8_t *wasm, size_t len,
                    int32_t *out_reason, uint32_t *out_section, uint32_t *out_func,
                    uint64_t *out_offset, char *msg, size_t msg_cap, size_t *out_msg_len) {
    return zw_validate_ex(policy, wasm, len, 0, out_reason, out_section, out_func, out_offset,
                          msg, msg_cap, out_msg_len);
}
