/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_validate.c — the Z-Wasm validator, LEB128 readers and fuel meter.
 * Modules are assembled in C (wasm_build.h); no toolchain needed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm_build.h"
#include "zw_wasm.h"
#include "zwasm.h"

static int g_checks, g_fail;

#define CHECK(cond, ...)                                                                       \
    do {                                                                                       \
        g_checks++;                                                                            \
        if (!(cond)) {                                                                         \
            g_fail++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                               \
            fprintf(stderr, __VA_ARGS__);                                                      \
            fprintf(stderr, "\n");                                                             \
        }                                                                                      \
    } while (0)

static zw_policy *g_pol;

typedef struct {
    int32_t st, reason;
    uint32_t section, func;
    uint64_t off;
    char msg[512];
} res;

static res run(const zw_policy *p, const wb *m) {
    res r;
    size_t ml = 0;
    memset(&r, 0, sizeof r);
    r.st = zw_validate(p, m->b, m->n, &r.reason, &r.section, &r.func, &r.off, r.msg,
                       sizeof r.msg, &ml);
    return r;
}

static int32_t reason_of(const zw_policy *p, const wb *m) {
    return run(p, m).reason;
}

/* ----- the expected classification, written out from the spec ----- */

static const uint8_t k_float_ops[] = {
    0x2A, 0x2B, 0x38, 0x39, 0x43, 0x44,                                     /* mem, const */
    0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, /* compare   */
    0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    /* 0xA7 i32.wrap_i64 is integer */
    0xA8, 0xA9, 0xAA, 0xAB,
    /* 0xAC, 0xAD i64.extend_i32_s/u are integer */
    0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB,
    0xBC, 0xBD, 0xBE, 0xBF};

static int in_list(uint8_t op, const uint8_t *l, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (l[i] == op) return 1;
    return 0;
}

static int32_t expected_reason(uint8_t op) {
    if (in_list(op, k_float_ops, sizeof k_float_ops)) return ZW_R_FLOAT_OP;
    if (op == 0xFC) return ZW_R_FLOAT_OP; /* the sweep uses 0xFC 0x00 */
    if (op == 0xFD) return ZW_R_SIMD;
    if ((op >= 0x06 && op <= 0x0A) || (op >= 0x12 && op <= 0x15) || op == 0xFB || op == 0xFE)
        return ZW_R_UNSUPPORTED;
    if (op <= 0x05 || (op >= 0x0B && op <= 0x11) || (op >= 0x1A && op <= 0x1C) ||
        (op >= 0x20 && op <= 0x26) || (op >= 0x28 && op <= 0x44) || (op >= 0x45 && op <= 0xC4) ||
        (op >= 0xD0 && op <= 0xD2))
        return ZW_R_OK;
    return ZW_R_MALFORMED;
}

/* Instruction bytes (without the function's final end) for `op`. */
static size_t body_for(uint8_t op, uint8_t *c) {
    size_t n = 0;
    c[n++] = op;
    switch (op) {
    case 0x02: case 0x03: case 0x04: c[n++] = 0x40; c[n++] = 0x0B; break;
    case 0x05: c[0] = 0x04; c[n++] = 0x40; c[n++] = 0x05; c[n++] = 0x0B; break;
    case 0x0B: n = 0; break; /* just the final end */
    case 0x0C: case 0x0D: c[n++] = 0x00; break;
    case 0x0E: c[n++] = 0x00; c[n++] = 0x00; break;
    case 0x10: c[n++] = 0x00; break;
    case 0x11: c[n++] = 0x00; c[n++] = 0x00; break;
    case 0x1C: c[n++] = 0x01; c[n++] = 0x7F; break;
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
        c[n++] = 0x00;
        break;
    case 0x3F: case 0x40: c[n++] = 0x00; break;
    case 0x41: case 0x42: c[n++] = 0x00; break;
    case 0x43: for (int i = 0; i < 4; i++) c[n++] = 0x00; break;
    case 0x44: for (int i = 0; i < 8; i++) c[n++] = 0x00; break;
    case 0xD0: c[n++] = 0x70; break;
    case 0xD2: c[n++] = 0x00; break;
    case 0xFC: c[n++] = 0x00; break;
    default:
        if (op >= 0x28 && op <= 0x3E) {
            c[n++] = 0x00; /* align */
            c[n++] = 0x00; /* offset */
        }
        break;
    }
    return n;
}

static void sweep_template(wt *t, const uint8_t *code, size_t n) {
    wt_one(t, code, n);
    t->funcs[0].locals = "i";
    t->memory = 1;
    t->mem_min = 1;
    t->mem_max = 1;
    t->table = 1;
    t->table_min = 1;
    t->table_max = 1;
    t->globals = "i";
    t->datacount = 1;
}

static void test_opcode_sweep(void) {
    wb m;
    wt t;
    uint8_t code[32];
    int nfloat = 0;
    for (unsigned op = 0; op <= 0xFF; op++) {
        size_t n = body_for((uint8_t) op, code);
        sweep_template(&t, code, n);
        wt_build(&m, &t);
        res r = run(g_pol, &m);
        int32_t want = expected_reason((uint8_t) op);
        CHECK(r.reason == want, "opcode 0x%02X: reason %s, want %s (%s)", op,
              zw_reason_name(r.reason), zw_reason_name(want), r.msg);
        CHECK((r.st == ZW_OK) == (want == ZW_R_OK), "opcode 0x%02X status %d", op, r.st);
        if (want == ZW_R_FLOAT_OP) {
            nfloat++;
            CHECK(r.section == 10 && r.func == 0, "opcode 0x%02X: section %u func %u", op,
                  r.section, r.func);
            CHECK(strstr(r.msg, "FLOAT_OP") != NULL, "opcode 0x%02X message: %s", op, r.msg);
            if (op != 0xFC) CHECK(zw_is_float_op(op) == 1, "zw_is_float_op(0x%02X)", op);
        } else if (op != 0xFC) {
            CHECK(zw_is_float_op(op) == 0, "zw_is_float_op(0x%02X) should be 0", op);
        }
    }
    CHECK(nfloat == 68 + 1, "float opcodes in the sweep: %d (68 + 0xFC 0x00)", nfloat);

    /* 0xFC sub-opcodes: 0..7 float, 8..17 bulk (off by default), 18+ bad */
    zw_policy *bulk;
    CHECK(zw_policy_create(&bulk) == ZXV_OK, "policy");
    CHECK(zw_policy_set_limit(bulk, ZW_LIM_BULK_MEMORY, 1) == ZXV_OK, "bulk on");
    for (unsigned sub = 0; sub <= 20; sub++) {
        size_t n = 0;
        code[n++] = 0xFC;
        code[n++] = (uint8_t) sub;
        switch (sub) {
        case 8: code[n++] = 0; code[n++] = 0; break;       /* memory.init 0 0 */
        case 9: case 13: case 15: case 16: case 17: code[n++] = 0; break;
        case 10: code[n++] = 0; code[n++] = 0; break;
        case 11: code[n++] = 0; break;
        case 12: case 14: code[n++] = 0; code[n++] = 0; break;
        default: break;
        }
        sweep_template(&t, code, n);
        wt_build(&m, &t);
        int32_t off = reason_of(g_pol, &m), on = reason_of(bulk, &m);
        if (sub <= 7) {
            CHECK(off == ZW_R_FLOAT_OP && on == ZW_R_FLOAT_OP, "0xFC %u: %d %d", sub, off, on);
            CHECK(zw_is_float_op(ZW_OP_FC(sub)), "zw_is_float_op(FC %u)", sub);
        } else if (sub <= 17) {
            int32_t want_on = (sub == 12 || sub == 13) ? ZW_R_INDEX : ZW_R_OK; /* no elems */
            CHECK(off == ZW_R_UNSUPPORTED, "0xFC %u bulk off: %s", sub, zw_reason_name(off));
            CHECK(on == want_on, "0xFC %u bulk on: %s", sub, zw_reason_name(on));
        } else {
            CHECK(off == ZW_R_MALFORMED, "0xFC %u: %s", sub, zw_reason_name(off));
        }
    }
    zw_policy_destroy(bulk);
}

static void test_float_types(void) {
    wb m;
    wt t;
    static const uint8_t nop[] = {0x01};
    /* f32 / f64 in a function type's params and results */
    wt_one(&t, nop, sizeof nop);
    t.types[0] = "f:";
    wt_build(&m, &t);
    res r = run(g_pol, &m);
    CHECK(r.reason == ZW_R_FLOAT_TYPE && r.section == 1, "f32 param: %s", r.msg);
    CHECK(strstr(r.msg, "f32") != NULL, "message names f32: %s", r.msg);
    t.types[0] = "i:F";
    wt_build(&m, &t);
    r = run(g_pol, &m);
    CHECK(r.reason == ZW_R_FLOAT_TYPE && strstr(r.msg, "f64") != NULL, "f64 result: %s", r.msg);
    t.types[0] = "v:";
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_SIMD, "v128 param");
    /* a float local */
    wt_one(&t, nop, sizeof nop);
    t.funcs[0].locals = "iF";
    wt_build(&m, &t);
    r = run(g_pol, &m);
    CHECK(r.reason == ZW_R_FLOAT_TYPE && r.section == 10 && r.func == 0, "f64 local: %s", r.msg);
    /* float globals */
    wt_one(&t, nop, sizeof nop);
    t.globals = "if";
    wt_build(&m, &t);
    r = run(g_pol, &m);
    CHECK(r.reason == ZW_R_FLOAT_TYPE && r.section == 6, "f32 global: %s", r.msg);
    /* block types and select t */
    static const uint8_t blk[] = {0x02, 0x7D, 0x00, 0x0B};
    wt_one(&t, blk, sizeof blk);
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_FLOAT_TYPE, "f32 block type");
    static const uint8_t sel[] = {0x1C, 0x01, 0x7C};
    wt_one(&t, sel, sizeof sel);
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_FLOAT_TYPE, "select f64");
    static const uint8_t blkv[] = {0x02, 0x7B, 0x00, 0x0B};
    wt_one(&t, blkv, sizeof blkv);
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_SIMD, "v128 block type");
    /* a float global import is reported as a float, an i32 one as a kind */
    static const uint8_t gimp_f[] = {3, 'e', 'n', 'v', 1, 'g', 3, 0x7D, 0};
    static const uint8_t gimp_i[] = {3, 'e', 'n', 'v', 1, 'g', 3, 0x7F, 0};
    wt_one(&t, nop, sizeof nop);
    t.raw_imports = gimp_f;
    t.raw_imports_len = sizeof gimp_f;
    t.raw_imports_count = 1;
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_FLOAT_TYPE, "f32 global import");
    t.raw_imports = gimp_i;
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_IMPORT_KIND, "i32 global import");
    /* f32.const in a global initializer */
    {
        wb s;
        wb_header(&m);
        wb_reset(&s);
        wb_uleb(&s, 1);
        wb_u8(&s, 0x7F);
        wb_u8(&s, 0);
        wb_u8(&s, 0x43);
        for (int i = 0; i < 4; i++) wb_u8(&s, 0);
        wb_u8(&s, 0x0B);
        wb_section(&m, 6, &s);
        r = run(g_pol, &m);
        CHECK(r.reason == ZW_R_FLOAT_OP && r.section == 6, "f32.const initializer: %s", r.msg);
    }
}

static void test_memory_table(void) {
    wb m;
    wt t;
    static const uint8_t nop[] = {0x01};
    zw_policy *p;
    wt_one(&t, nop, sizeof nop);
    t.memory = 1;
    t.mem_flag = 0;
    t.mem_min = 1;
    wt_build(&m, &t);
    res r = run(g_pol, &m);
    CHECK(r.reason == ZW_R_MEMORY_NO_MAX && r.section == 5, "no max: %s", r.msg);
    t.mem_flag = 1;
    t.mem_max = 17;
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_MEMORY_LIMIT, "17 pages > 16");
    t.mem_max = 16;
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_OK, "16 pages");
    CHECK(zw_policy_create(&p) == ZXV_OK, "policy");
    CHECK(zw_policy_set_limit(p, ZW_LIM_MEMORY_PAGES, 2) == ZXV_OK, "set pages");
    CHECK(reason_of(p, &m) == ZW_R_MEMORY_LIMIT, "16 pages > 2");
    t.mem_min = 3;
    t.mem_max = 2;
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "min > max");
    t.mem_min = 1;
    t.mem_max = 1;
    t.mem_flag = 3;
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_UNSUPPORTED, "shared memory");
    t.mem_flag = 5;
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_UNSUPPORTED, "memory64");
    {
        wb s;
        static const uint8_t two[] = {2, 1, 1, 1, 1, 1, 1};
        wb_header(&m);
        wb_reset(&s);
        wb_bytes(&s, two, sizeof two);
        wb_section(&m, 5, &s);
        CHECK(reason_of(g_pol, &m) == ZW_R_UNSUPPORTED, "two memories");
    }
    /* tables */
    wt_one(&t, nop, sizeof nop);
    t.table = 1;
    t.table_flag = 0;
    t.table_min = 1;
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_TABLE_LIMIT, "table without max");
    t.table_flag = 1;
    t.table_max = 2000;
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_TABLE_LIMIT, "table 2000 > 1024");
    CHECK(zw_policy_set_limit(p, ZW_LIM_TABLE_ELEMS, 4096) == ZXV_OK, "set table");
    CHECK(reason_of(p, &m) == ZW_R_OK, "table 2000 <= 4096");
    {
        wb s;
        static const uint8_t two[] = {2, 0x70, 1, 1, 1, 0x70, 1, 1, 1};
        wb_header(&m);
        wb_reset(&s);
        wb_bytes(&s, two, sizeof two);
        wb_section(&m, 4, &s);
        CHECK(reason_of(g_pol, &m) == ZW_R_TABLE_LIMIT, "two tables");
    }
    zw_policy_destroy(p);
}

static void test_imports(void) {
    wb m;
    wt t;
    zw_policy *p;
    static const uint8_t nop[] = {0x01};
    CHECK(zw_policy_create(&p) == ZXV_OK, "policy");
    CHECK(zw_policy_allow_import(p, "zxv", "lp_and", "i(ii)") == ZXV_OK, "allow");
    CHECK(zw_policy_allow_import(p, "zxv", "lp_and", "i(ii)") == ZXV_E_DUPLICATE, "dup");
    CHECK(zw_policy_allow_import(p, "zxv", "x", "f(ii)") == ZXV_E_ARG, "bad sig f");
    CHECK(zw_policy_allow_import(p, "zxv", "x", "i(iF)") == ZXV_E_ARG, "bad sig F");
    CHECK(zw_policy_allow_import(p, "zxv", "x", "i") == ZXV_E_ARG, "bad sig short");
    CHECK(zw_policy_allow_import(p, "", "x", "v()") == ZXV_E_ARG, "empty module");
    CHECK(zw_policy_allow_import(p, "zxv", "\xC0\x80", "v()") == ZXV_E_ARG, "bad utf8");

    wt_one(&t, nop, sizeof nop);
    t.types[1] = "ii:i";
    t.ntypes = 2;
    t.fimports[0].mod = "zxv";
    t.fimports[0].name = "lp_and";
    t.fimports[0].type = 1;
    t.nfimports = 1;
    wt_build(&m, &t);
    CHECK(reason_of(p, &m) == ZW_R_OK, "allowed import");
    res r = run(g_pol, &m);
    CHECK(r.reason == ZW_R_IMPORT_DENIED && r.section == 2, "denied: %s", r.msg);
    CHECK(strstr(r.msg, "zxv.lp_and") != NULL, "message names it: %s", r.msg);
    t.types[1] = "iI:i";
    wt_build(&m, &t);
    r = run(p, &m);
    CHECK(r.reason == ZW_R_IMPORT_SIG, "signature mismatch: %s", r.msg);
    CHECK(strstr(r.msg, "i(iI)") && strstr(r.msg, "i(ii)"), "sig message: %s", r.msg);
    t.types[1] = "ii:ii";
    wt_build(&m, &t);
    CHECK(reason_of(p, &m) == ZW_R_IMPORT_SIG, "two results");
    t.fimports[0].name = "lp_or";
    t.types[1] = "ii:i";
    wt_build(&m, &t);
    CHECK(reason_of(p, &m) == ZW_R_IMPORT_DENIED, "other name");
    /* memory / table imports */
    static const uint8_t mimp[] = {3, 'e', 'n', 'v', 3, 'm', 'e', 'm', 2, 1, 1, 1};
    static const uint8_t timp[] = {3, 'e', 'n', 'v', 3, 't', 'a', 'b', 1, 0x70, 1, 1, 1};
    wt_one(&t, nop, sizeof nop);
    t.raw_imports = mimp;
    t.raw_imports_len = sizeof mimp;
    t.raw_imports_count = 1;
    wt_build(&m, &t);
    CHECK(reason_of(p, &m) == ZW_R_IMPORT_KIND, "memory import");
    t.raw_imports = timp;
    t.raw_imports_len = sizeof timp;
    wt_build(&m, &t);
    CHECK(reason_of(p, &m) == ZW_R_IMPORT_KIND, "table import");
    zw_policy_destroy(p);
}

static void test_limits_and_misc(void) {
    wb m;
    wt t;
    zw_policy *p;
    static const uint8_t nop[] = {0x01};
    CHECK(zw_policy_create(&p) == ZXV_OK, "policy");
    /* start function */
    wt_one(&t, nop, sizeof nop);
    t.start = 1;
    wt_build(&m, &t);
    res r = run(g_pol, &m);
    CHECK(r.reason == ZW_R_START && r.section == 8, "start: %s", r.msg);
    /* module bytes */
    wt_one(&t, nop, sizeof nop);
    wt_build(&m, &t);
    CHECK(zw_policy_set_limit(p, ZW_LIM_MODULE_BYTES, 10) == ZXV_OK, "set");
    CHECK(reason_of(p, &m) == ZW_R_TOO_LARGE, "too large");
    CHECK(zw_policy_set_limit(p, ZW_LIM_MODULE_BYTES, m.n) == ZXV_OK, "set");
    CHECK(reason_of(p, &m) == ZW_R_OK, "exactly at the limit");
    CHECK(zw_policy_set_limit(p, ZW_LIM_MODULE_BYTES, 1u << 20) == ZXV_OK, "reset");
    /* locals */
    wt_one(&t, nop, sizeof nop);
    t.funcs[0].locals = "iiii";
    wt_build(&m, &t);
    CHECK(zw_policy_set_limit(p, ZW_LIM_LOCALS, 3) == ZXV_OK, "set");
    r = run(p, &m);
    CHECK(r.reason == ZW_R_LIMIT && r.func == 0, "locals: %s", r.msg);
    CHECK(zw_policy_set_limit(p, ZW_LIM_LOCALS, 4) == ZXV_OK, "set");
    CHECK(reason_of(p, &m) == ZW_R_OK, "4 locals");
    /* one huge local count must not wrap */
    {
        wb s, body;
        wb_header(&m);
        wb_reset(&s); wb_uleb(&s, 1); wb_u8(&s, 0x60); wb_uleb(&s, 0); wb_uleb(&s, 0);
        wb_section(&m, 1, &s);
        wb_reset(&s); wb_uleb(&s, 1); wb_uleb(&s, 0); wb_section(&m, 3, &s);
        wb_reset(&body);
        wb_uleb(&body, 2);
        wb_uleb(&body, 0xFFFFFFFFu); wb_u8(&body, 0x7F);
        wb_uleb(&body, 0xFFFFFFFFu); wb_u8(&body, 0x7F);
        wb_u8(&body, 0x0B);
        wb_reset(&s); wb_uleb(&s, 1); wb_uleb(&s, body.n); wb_bytes(&s, body.b, body.n);
        wb_section(&m, 10, &s);
        CHECK(reason_of(g_pol, &m) == ZW_R_LIMIT, "2 x 2^32-1 locals");
    }
    /* nesting */
    {
        uint8_t code[64];
        size_t n = 0;
        for (int i = 0; i < 10; i++) { code[n++] = 0x02; code[n++] = 0x40; }
        for (int i = 0; i < 10; i++) code[n++] = 0x0B;
        wt_one(&t, code, n);
        wt_build(&m, &t);
        CHECK(zw_policy_set_limit(p, ZW_LIM_NESTING, 10) == ZXV_OK, "set");
        CHECK(reason_of(p, &m) == ZW_R_LIMIT, "nesting 11 > 10");
        CHECK(zw_policy_set_limit(p, ZW_LIM_NESTING, 11) == ZXV_OK, "set");
        CHECK(reason_of(p, &m) == ZW_R_OK, "nesting 11");
    }
    /* body bytes */
    {
        uint8_t code[100];
        memset(code, 0x01, sizeof code);
        wt_one(&t, code, sizeof code);
        wt_build(&m, &t);
        CHECK(zw_policy_set_limit(p, ZW_LIM_BODY_BYTES, 50) == ZXV_OK, "set");
        r = run(p, &m);
        CHECK(r.reason == ZW_R_LIMIT && r.func == 0 && r.section == 10, "body: %s", r.msg);
    }
    /* functions / exports / types / params */
    wt_one(&t, nop, sizeof nop);
    t.exports[0] = "run";
    t.nexports = 1;
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_OK, "export run");
    CHECK(zw_policy_set_limit(p, ZW_LIM_FUNCTIONS, 0) == ZXV_OK, "set");
    CHECK(reason_of(p, &m) == ZW_R_LIMIT, "functions 1 > 0");
    zw_policy_destroy(p);
    CHECK(zw_policy_create(&p) == ZXV_OK, "policy");
    CHECK(zw_policy_set_limit(p, ZW_LIM_EXPORTS, 0) == ZXV_OK, "set");
    CHECK(reason_of(p, &m) == ZW_R_LIMIT, "exports 1 > 0");
    CHECK(zw_policy_set_limit(p, ZW_LIM_TYPES, 0) == ZXV_OK, "set");
    CHECK(reason_of(p, &m) == ZW_R_LIMIT, "types 1 > 0");
    zw_policy_destroy(p);
    CHECK(zw_policy_create(&p) == ZXV_OK, "policy");
    wt_one(&t, nop, sizeof nop);
    t.types[0] = "iii:";
    wt_build(&m, &t);
    CHECK(zw_policy_set_limit(p, ZW_LIM_PARAMS, 2) == ZXV_OK, "set");
    CHECK(reason_of(p, &m) == ZW_R_LIMIT, "3 params > 2");
    /* data bytes */
    wt_one(&t, nop, sizeof nop);
    t.memory = 1;
    t.mem_min = 1;
    t.mem_max = 1;
    t.datacount = 3; /* 3 x 2 bytes */
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_OK, "6 data bytes");
    CHECK(zw_policy_set_limit(p, ZW_LIM_DATA_BYTES, 5) == ZXV_OK, "set");
    CHECK(reason_of(p, &m) == ZW_R_LIMIT, "6 data bytes > 5");
    /* reserved export */
    wt_one(&t, nop, sizeof nop);
    t.exports[0] = "__zw_fuel";
    t.nexports = 1;
    wt_build(&m, &t);
    r = run(g_pol, &m);
    CHECK(r.reason == ZW_R_RESERVED_NAME && r.section == 7, "reserved: %s", r.msg);
    t.exports[0] = "__zw_x";
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_RESERVED_NAME, "reserved prefix");
    /* policy API */
    CHECK(zw_policy_set_limit(p, 0, 1) == ZXV_E_ARG, "limit 0");
    CHECK(zw_policy_set_limit(p, 99, 1) == ZXV_E_ARG, "limit 99");
    CHECK(zw_policy_set_limit(p, ZW_LIM_MEMORY_PAGES, 65537) == ZXV_E_ARG, "pages > 65536");
    CHECK(zw_policy_set_limit(p, ZW_LIM_GLOBALS, 4097) == ZXV_E_ARG, "globals > 4096");
    CHECK(zw_policy_set_limit(p, ZW_LIM_NESTING, 991) == ZXV_E_ARG, "nesting > 990");
    {
        uint64_t v = 0;
        CHECK(zw_policy_get_limit(g_pol, ZW_LIM_MEMORY_PAGES, &v) == ZXV_OK && v == 16,
              "default pages 16");
        CHECK(zw_policy_get_limit(g_pol, ZW_LIM_BULK_MEMORY, &v) == ZXV_OK && v == 0,
              "bulk off by default");
    }
    CHECK(zw_validate(NULL, m.b, m.n, NULL, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_E_ARG,
          "NULL policy");
    CHECK(zw_validate(g_pol, NULL, 1, NULL, NULL, NULL, NULL, NULL, 0, NULL) == ZXV_E_ARG,
          "NULL bytes");
    zw_policy_destroy(p);
}

static void test_indices(void) {
    wb m;
    wt t;
    struct {
        uint8_t code[8];
        size_t n;
        int mem;
        const char *what;
    } cases[] = {
        {{0x10, 0x05}, 2, 1, "call 5"},
        {{0x20, 0x03}, 2, 1, "local.get 3"},
        {{0x23, 0x01}, 2, 1, "global.get 1"},
        {{0x0C, 0x01}, 2, 1, "br 1 at depth 1"},
        {{0x0E, 0x01, 0x00, 0x02}, 4, 1, "br_table default 2"},
        {{0x11, 0x05, 0x00}, 3, 1, "call_indirect type 5"},
        {{0x11, 0x00, 0x01}, 3, 1, "call_indirect table 1"},
        {{0x28, 0x02, 0x00}, 3, 0, "i32.load without memory"},
        {{0x3F, 0x00}, 2, 0, "memory.size without memory"},
        {{0x02, 0x05, 0x0B}, 3, 1, "block type 5"},
        {{0xD2, 0x09}, 2, 1, "ref.func 9"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        sweep_template(&t, cases[i].code, cases[i].n);
        if (!cases[i].mem) {
            t.memory = 0;
            t.datacount = -1;
        }
        wt_build(&m, &t);
        res r = run(g_pol, &m);
        CHECK(r.reason == ZW_R_INDEX && r.func == 0, "%s: %s", cases[i].what, r.msg);
    }
    /* global.set of an immutable global is malformed */
    {
        wb s, body;
        wb_header(&m);
        wb_reset(&s); wb_uleb(&s, 1); wb_u8(&s, 0x60); wb_uleb(&s, 0); wb_uleb(&s, 0);
        wb_section(&m, 1, &s);
        wb_reset(&s); wb_uleb(&s, 1); wb_uleb(&s, 0); wb_section(&m, 3, &s);
        wb_reset(&s); wb_uleb(&s, 1); wb_u8(&s, 0x7F); wb_u8(&s, 0); wb_u8(&s, 0x41);
        wb_u8(&s, 0); wb_u8(&s, 0x0B); wb_section(&m, 6, &s);
        wb_reset(&body); wb_uleb(&body, 0);
        wb_u8(&body, 0x41); wb_u8(&body, 1); wb_u8(&body, 0x24); wb_u8(&body, 0); wb_u8(&body, 0x0B);
        wb_reset(&s); wb_uleb(&s, 1); wb_uleb(&s, body.n); wb_bytes(&s, body.b, body.n);
        wb_section(&m, 10, &s);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "global.set immutable");
    }
    /* alignment above natural */
    {
        uint8_t code[] = {0x41, 0x00, 0x28, 0x03, 0x00, 0x1A};
        sweep_template(&t, code, sizeof code);
        wt_build(&m, &t);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "i32.load align 2^3");
    }
}

static void test_malformed(void) {
    wb m;
    wt t;
    static const uint8_t nop[] = {0x01};
    static const uint8_t magic[] = {0x00, 0x61, 0x73, 0x6E, 1, 0, 0, 0};
    static const uint8_t vers[] = {0x00, 0x61, 0x73, 0x6D, 2, 0, 0, 0};
    res r;
    wb_reset(&m);
    wb_bytes(&m, magic, sizeof magic);
    r = run(g_pol, &m);
    CHECK(r.reason == ZW_R_MALFORMED && r.section == ZW_SEC_HEADER && r.off == 0, "magic");
    wb_reset(&m);
    wb_bytes(&m, vers, sizeof vers);
    r = run(g_pol, &m);
    CHECK(r.reason == ZW_R_MALFORMED && r.off == 4, "version");
    wb_reset(&m);
    wb_bytes(&m, vers, 5);
    CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "short");
    wb_header(&m);
    CHECK(reason_of(g_pol, &m) == ZW_R_OK, "empty module");
    /* truncation of a good module at every length */
    wt_one(&t, nop, sizeof nop);
    t.exports[0] = "run";
    t.nexports = 1;
    t.memory = 1;
    t.mem_min = 1;
    t.mem_max = 1;
    wt_build(&m, &t);
    {
        size_t full = m.n;
        int bad = 0;
        for (size_t k = 9; k < full; k++) {
            wb c = m;
            c.n = k;
            if (reason_of(g_pol, &c) == ZW_R_OK) {
                /* a cut exactly at a section boundary is a valid module */
                continue;
            }
            bad++;
        }
        CHECK(bad > 0, "truncations rejected: %d", bad);
    }
    /* section order and duplicates */
    {
        wb s;
        wb_header(&m);
        wb_reset(&s); wb_uleb(&s, 0);
        wb_section(&m, 3, &s);
        wb_section(&m, 1, &s);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "function before type");
        wb_header(&m);
        wb_section(&m, 1, &s);
        wb_section(&m, 1, &s);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "two type sections");
        wb_header(&m);
        wb_section(&m, 13, &s);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "section id 13");
        /* custom section with a bad UTF-8 name; and a good one anywhere */
        static const uint8_t badname[] = {2, 0xC3, 0x28};
        wb_header(&m);
        wb_section_raw(&m, 0, badname, sizeof badname);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "custom name utf8");
        static const uint8_t goodname[] = {4, 'n', 'a', 'm', 'e', 9, 9, 9};
        wb_header(&m);
        wb_section_raw(&m, 0, goodname, sizeof goodname);
        wb_section(&m, 1, &s);
        wb_section_raw(&m, 0, goodname, sizeof goodname);
        CHECK(reason_of(g_pol, &m) == ZW_R_OK, "custom sections");
    }
    /* non-canonical LEB128 */
    {
        static const uint8_t over[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x00}; /* 6 bytes */
        static const uint8_t high[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x1F};       /* bit 33+ */
        wb_header(&m);
        wb_u8(&m, 1);
        wb_bytes(&m, over, sizeof over);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "6-byte u32");
        wb_header(&m);
        wb_u8(&m, 1);
        wb_bytes(&m, high, sizeof high);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "u32 high bits");
    }
    /* body structure */
    {
        static const uint8_t els[] = {0x05};
        wt_one(&t, els, sizeof els);
        wt_build(&m, &t);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "else without if");
        static const uint8_t two_else[] = {0x04, 0x40, 0x05, 0x05, 0x0B};
        wt_one(&t, two_else, sizeof two_else);
        wt_build(&m, &t);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "two elses");
        static const uint8_t after[] = {0x0B, 0x01};
        wt_one(&t, after, sizeof after);
        wt_build(&m, &t);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "code after end");
        static const uint8_t open[] = {0x02, 0x40};
        wt_one(&t, open, sizeof open);
        wt_build(&m, &t);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "unterminated block");
        static const uint8_t s32bad[] = {0x41, 0xFF, 0xFF, 0xFF, 0xFF, 0x4F, 0x1A};
        wt_one(&t, s32bad, sizeof s32bad);
        wt_build(&m, &t);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "i32.const bad sign bits");
        static const uint8_t s32ok[] = {0x41, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x1A};
        wt_one(&t, s32ok, sizeof s32ok);
        wt_build(&m, &t);
        CHECK(reason_of(g_pol, &m) == ZW_R_OK, "i32.const -1 in 5 bytes");
    }
    /* duplicate export names, bad UTF-8 export */
    wt_one(&t, nop, sizeof nop);
    t.exports[0] = "a";
    t.exports[1] = "a";
    t.nexports = 2;
    t.funcs[1] = t.funcs[0];
    t.nfuncs = 2;
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "duplicate export");
    t.exports[1] = "\xED\xA0\x80"; /* a surrogate */
    wt_build(&m, &t);
    CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "surrogate in export name");
    /* function section without code section */
    {
        wb s;
        wb_header(&m);
        wb_reset(&s); wb_uleb(&s, 1); wb_u8(&s, 0x60); wb_uleb(&s, 0); wb_uleb(&s, 0);
        wb_section(&m, 1, &s);
        wb_reset(&s); wb_uleb(&s, 1); wb_uleb(&s, 0);
        wb_section(&m, 3, &s);
        CHECK(reason_of(g_pol, &m) == ZW_R_MALFORMED, "no code section");
    }
}

static void test_location(void) {
    /* Function index space: 1 import + 2 functions; the float op is in
     * function 2 (the second defined one). */
    wb m;
    wt t;
    zw_policy *p;
    static const uint8_t ok[] = {0x01};
    static const uint8_t bad[] = {0x41, 0x07, 0xB2, 0x1A}; /* i32.const 7 f32.convert_i32_s drop */
    CHECK(zw_policy_create(&p) == ZXV_OK, "policy");
    CHECK(zw_policy_allow_import(p, "zxv", "lp_not", "i(i)") == ZXV_OK, "allow");
    wt_one(&t, ok, sizeof ok);
    t.types[1] = "i:i";
    t.ntypes = 2;
    t.fimports[0].mod = "zxv";
    t.fimports[0].name = "lp_not";
    t.fimports[0].type = 1;
    t.nfimports = 1;
    t.funcs[1].type = 0;
    t.funcs[1].code = bad;
    t.funcs[1].code_len = sizeof bad;
    t.nfuncs = 2;
    wt_build(&m, &t);
    size_t want = 0;
    for (size_t i = 0; i + 2 < m.n; i++)
        if (m.b[i] == 0x41 && m.b[i + 1] == 0x07 && m.b[i + 2] == 0xB2) want = i + 2;
    char small[16];
    size_t full = 0;
    int32_t reason = -1;
    uint32_t sec = 0, func = 0;
    uint64_t off = 0;
    int32_t st = zw_validate(p, m.b, m.n, &reason, &sec, &func, &off, small, sizeof small, &full);
    CHECK(st == ZW_E_REJECTED && reason == ZW_R_FLOAT_OP, "rejected");
    CHECK(sec == 10 && func == 2 && off == want, "location sec %u func %u off %llu (want %zu)",
          sec, func, (unsigned long long) off, want);
    CHECK(full > sizeof small && strlen(small) == sizeof small - 1u, "message truncated");
    char msg[512];
    st = zw_validate(p, m.b, m.n, NULL, NULL, NULL, NULL, msg, sizeof msg, &full);
    CHECK(strlen(msg) == full, "full message length");
    CHECK(strstr(msg, "f32.convert_i32_s") && strstr(msg, "function 2") &&
              strstr(msg, "code section"), "message: %s", msg);
    {
        char le[512];
        size_t n = 0;
        CHECK(zw_last_error(le, sizeof le, &n) == ZXV_OK && strcmp(le, msg) == 0,
              "zw_last_error = message");
        CHECK(zw_last_error(NULL, 0, &n) == ZXV_E_BUFFER && n == full, "two-call");
    }
    printf("  example rejection: %s\n", msg);
    zw_policy_destroy(p);
}

static void test_leb(void) {
    struct {
        uint8_t b[12];
        size_t n;
        int ok;
        int64_t v;
        int bits;
    } c[] = {
        {{0x00}, 1, 1, 0, 32},
        {{0x7F}, 1, 1, -1, 32},
        {{0x80, 0x7F}, 2, 1, -128, 32},
        {{0xFF, 0xFF, 0xFF, 0xFF, 0x07}, 5, 1, INT32_MAX, 32},
        {{0x80, 0x80, 0x80, 0x80, 0x78}, 5, 1, INT32_MIN, 32},
        {{0x80, 0x80, 0x80, 0x80, 0x08}, 5, 0, 0, 32},
        {{0x80, 0x80, 0x80, 0x80, 0x80, 0x00}, 6, 0, 0, 32},
        {{0x80}, 1, 0, 0, 32},
        {{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F}, 10, 1, INT64_MIN, 64},
        {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00}, 10, 1, INT64_MAX, 64},
        {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01}, 10, 0, 0, 64},
        {{0x80, 0x80, 0x80, 0x80, 0x10}, 5, 0, 0, 33},
        {{0xFF, 0xFF, 0xFF, 0xFF, 0x0F}, 5, 1, INT64_C(4294967295), 33},
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        size_t pos = 0;
        int64_t v = 0;
        int r;
        if (c[i].bits == 32) {
            int32_t v32 = 0;
            r = zw_rd_s32(c[i].b, c[i].n, &pos, &v32);
            v = v32;
        } else if (c[i].bits == 33) {
            r = zw_rd_s33(c[i].b, c[i].n, &pos, &v);
        } else {
            r = zw_rd_s64(c[i].b, c[i].n, &pos, &v);
        }
        CHECK((r == 0) == c[i].ok, "sleb case %zu ok", i);
        if (c[i].ok) CHECK(v == c[i].v && pos == c[i].n, "sleb case %zu value", i);
        else CHECK(pos == 0, "sleb case %zu pos unchanged", i);
    }
    {
        static const uint8_t u1[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
        static const uint8_t u2[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x1F};
        size_t pos = 0;
        uint32_t v = 0;
        CHECK(zw_rd_u32(u1, sizeof u1, &pos, &v) == 0 && v == UINT32_MAX, "u32 max");
        pos = 0;
        CHECK(zw_rd_u32(u2, sizeof u2, &pos, &v) != 0, "u32 overflow");
    }
    CHECK(zw_utf8_ok((const uint8_t *) "h\xC3\xA9llo", 6), "utf8 ok");
    CHECK(!zw_utf8_ok((const uint8_t *) "\xC0\x80", 2), "overlong");
    CHECK(!zw_utf8_ok((const uint8_t *) "\xF4\x90\x80\x80", 4), "> U+10FFFF");
    CHECK(!zw_utf8_ok((const uint8_t *) "\xE2\x82", 2), "truncated");
}

static void test_meter(void) {
    wb m;
    wt t;
    zw_policy *relaxed;
    static const uint8_t loop[] = {0x03, 0x40, 0x41, 0x01, 0x0D, 0x00, 0x0B}; /* loop br_if 0 */
    static const uint8_t ifelse[] = {0x20, 0x00, 0x04, 0x40, 0x01, 0x05, 0x01, 0x0B, 0x0F};
    CHECK(zw_policy_create(&relaxed) == ZXV_OK, "policy");
    for (int k = 1; k <= ZW_LIM_COUNT; k++) {
        if (k == ZW_LIM_BULK_MEMORY) continue;
        uint64_t big = k == ZW_LIM_NESTING ? 990 : (k == ZW_LIM_GLOBALS ? 4096 : (k == ZW_LIM_PARAMS ? 16 : UINT64_C(1) << 20));
        (void) zw_policy_set_limit(relaxed, k, big);
    }
    for (int variant = 0; variant < 6; variant++) {
        wt_one(&t, variant & 1 ? loop : ifelse, variant & 1 ? sizeof loop : sizeof ifelse);
        t.funcs[0].locals = "i";
        if (variant >= 2) t.globals = "iI";
        if (variant >= 4) {
            t.exports[0] = "f";
            t.nexports = 1;
        }
        wt_build(&m, &t);
        CHECK(reason_of(g_pol, &m) == ZW_R_OK, "meter input %d valid", variant);
        uint8_t *out = NULL;
        size_t olen = 0;
        CHECK(zw_meter(m.b, m.n, &out, &olen) == ZW_OK, "meter %d", variant);
        int32_t reason = -1;
        char msg[256];
        CHECK(zw_validate_ex(relaxed, out, olen, 1, &reason, NULL, NULL, NULL, msg, sizeof msg,
                             NULL) == ZW_OK,
              "metered %d validates: %s", variant, msg);
        CHECK(zw_validate(relaxed, out, olen, &reason, NULL, NULL, NULL, NULL, 0, NULL) ==
                      ZW_E_REJECTED && reason == ZW_R_RESERVED_NAME,
              "metered %d has the reserved export", variant);
        CHECK(olen > m.n, "metered %d grew", variant);
        free(out);
    }
    zw_policy_destroy(relaxed);
}

int main(void) {
    printf("test_validate: Z-Wasm validator %d.%d.%d\n", ZW_VERSION_MAJOR, ZW_VERSION_MINOR,
           ZW_VERSION_PATCH);
    if (zw_policy_create(&g_pol) != ZXV_OK) return 1;
    test_leb();
    test_opcode_sweep();
    test_float_types();
    test_memory_table();
    test_imports();
    test_limits_and_misc();
    test_indices();
    test_malformed();
    test_location();
    test_meter();
    zw_policy_destroy(g_pol);
    printf("test_validate: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
