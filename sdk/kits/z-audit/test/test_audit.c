/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_audit.c — z-audit: every check on modules assembled in C (z-wasm's
 * test/wasm_build.h), the grant table against a real Z-Wasm host, the
 * bounded analysis (in range, may leave, always outside, constant
 * proposals, propose counts, folding, overflow, skipped bodies), the
 * reports (JSON syntax, text, R4 buffers, determinism) and truncated
 * modules. */
#include "wasm_build.h"
#include "zau_test.h"

static wb g_code, g_mod;

static void op(uint8_t b) { wb_u8(&g_code, b); }
static void i32c(int32_t v) {
    op(0x41);
    wb_sleb(&g_code, v);
}
static void i64c(int64_t v) {
    op(0x42);
    wb_sleb(&g_code, v);
}
static void lget(uint32_t i) {
    op(0x20);
    wb_uleb(&g_code, i);
}
static void code_reset(void) { wb_reset(&g_code); }

/* One function `name` of type `type` ("I:i" ...) whose body is g_code (+ end);
 * with_propose adds the import zxv.propose (function 0). */
static void build(const char *type, const char *name, int with_propose, const char *locals) {
    wt t;
    wt_one(&t, g_code.b, g_code.n);
    t.types[0] = type;
    t.ntypes = 1;
    if (with_propose) {
        t.types[1] = "iIIii:i";
        t.ntypes = 2;
        t.fimports[0].mod = "zxv";
        t.fimports[0].name = "propose";
        t.fimports[0].type = 1;
        t.nfimports = 1;
    }
    t.funcs[0].locals = locals;
    t.exports[0] = name;
    t.nexports = 1;
    wt_build(&g_mod, &t);
}

static zau_report *audit_flags(const wb *m, const char *contract, uint32_t grants, uint32_t flags) {
    zau_options *o = NULL;
    zau_report *r = NULL;
    if (zau_options_create(&o) != ZXV_OK) return NULL;
    if (grants) (void) zau_options_grant(o, grants);
    (void) zau_options_set_flags(o, flags);
    if (zau_audit(o, m->b, m->n, contract, contract ? strlen(contract) : 0, &r) != ZXV_OK) r = NULL;
    zau_options_destroy(o);
    return r;
}

static zau_report *audit(const wb *m, const char *contract) {
    return audit_flags(m, contract, ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE, 0);
}

/* fee(amount i64, bps i64) -> i32 = zxv.propose(2, amount * bps, 10000, 0, 0) */
static void build_fee(void) {
    code_reset();
    i32c(2);
    lget(0);
    lget(1);
    op(0x7E);
    i64c(10000);
    i32c(0);
    i32c(0);
    op(0x10);
    wb_uleb(&g_code, 0);
    build("II:i", "fee", 1, NULL);
}

static const char k_fee[] = "fn fee(amount: int[0..10^12], bps: int[0..10000]) -> int[0..0]\n"
                            "    proposes rat[0..10^12] den|10000 max 1;\n";

static void test_clean(void) {
    zau_report *r;
    build_fee();
    r = audit(&g_mod, k_fee);
    CHECK(r != NULL, "audit runs");
    if (!r) return;
    CHECK(passed(r), "a clean plugin passes");
    if (!passed(r)) dump(r);
    for (uint32_t i = 0; i < 7; i++)
        CHECK(check_status(r, i) == ZAU_PASS, "check %u passes (status %d)", (unsigned) i, (int) check_status(r, i));
    CHECK(count_code(r, "IMPORT_ALLOWED", ZAU_INFO, NULL) == 1, "the propose import is allowed");
    CHECK(count_code(r, "DECLARED", ZAU_INFO, "fee") == 1, "fee is declared");
    CHECK(count_code(r, "ANALYZED", ZAU_INFO, "fee") == 1, "fee's straight-line body was analysed");
    CHECK(count_code(r, "RESULT_NOT_BOUNDED", ZAU_INFO, "fee") == 1,
          "the result of a host call cannot be bounded statically (the guard checks it)");
    uint32_t n = 0;
    const char *id = NULL, *sum = NULL;
    CHECK(zau_report_check_count(r, &n) == ZXV_OK && n == 7, "seven checks");
    CHECK(zau_report_check(r, 0, &id, NULL, &sum) == ZXV_OK && strcmp(id, "module.validate") == 0 &&
              zau_report_check(r, 6, &id, NULL, NULL) == ZXV_OK && strcmp(id, "exports.analysis") == 0 &&
              zau_report_check(r, 7, &id, NULL, NULL) == ZXV_E_NOT_FOUND,
          "check ids in order");
    CHECK(zau_report_finding(r, 9999, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ZXV_E_NOT_FOUND,
          "finding out of range");
    zau_report_destroy(r);

    /* the same module without the propose grant */
    r = audit_flags(&g_mod, k_fee, ZW_GRANT_RATIONAL, 0);
    CHECK(r && !passed(r) && check_status(r, 0) == ZAU_FAIL && count_in(r, 0, "IMPORT_DENIED") == 1 &&
              count_in(r, 2, "IMPORT_DENIED") == 1 && count_code(r, "IMPORT_ALLOWED", -1, NULL) == 0,
          "without the propose grant: the validator and the import check both refuse");
    CHECK(r && check_status(r, 6) == ZAU_SKIP, "no analysis of a module the validator refuses");
    zau_report_destroy(r);
}

static void test_float_simd(void) {
    zau_report *r;
    static const uint8_t f64one[8] = {0, 0, 0, 0, 0, 0, 0xF0, 0x3F};
    code_reset();
    op(0x44);
    wb_bytes(&g_code, f64one, 8);
    op(0x44);
    wb_bytes(&g_code, f64one, 8);
    op(0xA0); /* f64.add */
    op(0x1A);
    i32c(0);
    build("F:i", "f", 0, "F");
    r = audit(&g_mod, "fn f(x: int) -> int;");
    CHECK(r && !passed(r), "a float plugin fails");
    if (!r) return;
    CHECK(check_status(r, 0) == ZAU_FAIL && check_status(r, 1) == ZAU_FAIL, "validate and float-simd fail");
    CHECK(count_in(r, 1, "FLOAT_OP") == 3, "every float instruction is listed (2 const + add): %u",
          (unsigned) count_in(r, 1, "FLOAT_OP"));
    CHECK(count_in(r, 1, "FLOAT_TYPE") == 2, "the f64 parameter type and the f64 local: %u",
          (unsigned) count_in(r, 1, "FLOAT_TYPE"));
    CHECK(count_in(r, 0, "FLOAT_TYPE") == 1, "the validator names the first one");
    zau_report_destroy(r);

    code_reset();
    op(0xFD);
    op(0x0C);
    build(":", "f", 0, NULL);
    r = audit(&g_mod, "fn f();");
    CHECK(r && count_in(r, 1, "SIMD") == 1 && check_status(r, 0) == ZAU_FAIL, "SIMD is listed");
    zau_report_destroy(r);

    /* typed select of f32 and a block with an f64 result */
    code_reset();
    op(0x02);
    op(0x7C);
    op(0x44);
    wb_bytes(&g_code, f64one, 8);
    op(0x0B);
    op(0x1A);
    build(":", "f", 0, NULL);
    r = audit(&g_mod, "fn f();");
    CHECK(r && count_in(r, 1, "FLOAT_TYPE") == 1 && count_in(r, 1, "FLOAT_OP") == 1,
          "a block with an f64 result and its f64.const");
    zau_report_destroy(r);
}

/* Each Z-Wasm host function: allowed under its grant only, in z-audit and
 * in a real Z-Wasm host. */
static const struct {
    const char *name, *wt_type;
    uint32_t grant;
} k_fns[] = {
    {"rat_add", "IIIIi:i", ZW_GRANT_RATIONAL}, {"rat_sub", "IIIIi:i", ZW_GRANT_RATIONAL},
    {"rat_mul", "IIIIi:i", ZW_GRANT_RATIONAL}, {"rat_div", "IIIIi:i", ZW_GRANT_RATIONAL},
    {"rat_cmp", "IIIIi:i", ZW_GRANT_RATIONAL}, {"rat_normalize", "IIi:i", ZW_GRANT_RATIONAL},
    {"lp_not", "i:i", ZW_GRANT_LOGIC},         {"lp_and", "ii:i", ZW_GRANT_LOGIC},
    {"lp_or", "ii:i", ZW_GRANT_LOGIC},         {"lp_from_evidence", "ii:i", ZW_GRANT_LOGIC},
    {"propose", "iIIii:i", ZW_GRANT_PROPOSE},
};

static void test_grants(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *bcap = NULL;
    zxv_rmag *rm = NULL;
    uint32_t bud = 0;
    int setup = zxv_ctx_create(&ctx) == ZXV_OK && zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) == ZXV_OK &&
                zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &bcap) == ZXV_OK && zxv_rmag_create_cap(root, &rm) == ZXV_OK &&
                zxv_rmag_budget(root, rm, "t", ZXV_RES_COMPUTE_CYCLES, 1000000, 1, 1, &bud) == ZXV_OK;
    CHECK(setup, "a Z-Wasm host setup");
    if (!setup) return;
    for (size_t i = 0; i < sizeof k_fns / sizeof k_fns[0]; i++) {
        wt t;
        uint32_t other = (ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE) & ~k_fns[i].grant;
        code_reset();
        wt_one(&t, g_code.b, 0);
        t.types[0] = ":";
        t.types[1] = k_fns[i].wt_type;
        t.ntypes = 2;
        t.fimports[0].mod = "zxv";
        t.fimports[0].name = k_fns[i].name;
        t.fimports[0].type = 1;
        t.nfimports = 1;
        t.exports[0] = "f";
        t.nexports = 1;
        wt_build(&g_mod, &t);
        for (int pass = 0; pass < 2; pass++) {
            uint32_t g = pass ? other : k_fns[i].grant;
            zw_host *h = NULL;
            zw_plugin *p = NULL;
            int32_t st = -1;
            if (zw_host_create(bcap, rm, bud, &h) == ZXV_OK && zw_host_grant(h, g) == ZXV_OK)
                st = zw_plugin_load(h, g_mod.b, g_mod.n, "g", &p);
            zau_report *r = audit_flags(&g_mod, "fn f();", g, 0);
            int zw_ok = st == ZXV_OK, zau_ok = r && passed(r) && check_status(r, 2) == ZAU_PASS;
            CHECK(zw_ok == !pass && zau_ok == !pass,
                  "zxv.%s with grant %u: Z-Wasm host %s, z-audit %s (expected %s)", k_fns[i].name, (unsigned) g,
                  zw_ok ? "loads" : "refuses", zau_ok ? "passes" : "fails", pass ? "refuse" : "accept");
            zau_report_destroy(r);
            zw_plugin_destroy(p);
            zw_host_destroy(h);
        }
    }
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(bcap);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
}

static void test_imports(void) {
    wt t;
    zau_report *r;
    zau_options *o = NULL;
    /* a wrong signature for propose, and an import nobody allowed */
    code_reset();
    wt_one(&t, g_code.b, 0);
    t.types[0] = ":";
    t.types[1] = "iIIi:i";
    t.ntypes = 2;
    t.fimports[0].mod = "zxv";
    t.fimports[0].name = "propose";
    t.fimports[0].type = 1;
    t.fimports[1].mod = "env";
    t.fimports[1].name = "clock";
    t.fimports[1].type = 0;
    t.nfimports = 2;
    t.exports[0] = "f";
    t.nexports = 1;
    wt_build(&g_mod, &t);
    r = audit(&g_mod, "fn f();");
    CHECK(r && count_in(r, 2, "IMPORT_SIG") == 1 && count_in(r, 2, "IMPORT_DENIED") == 1 &&
              check_status(r, 2) == ZAU_FAIL,
          "wrong signature and a denied import are both listed");
    zau_report_destroy(r);
    /* --allow env.clock=v() */
    CHECK(zau_options_create(&o) == ZXV_OK && zau_options_allow_import(o, "env", "clock", "v()") == ZXV_OK &&
              zau_options_grant(o, ZW_GRANT_PROPOSE) == ZXV_OK && zau_options_grant(o, ZW_GRANT_PROPOSE) == ZXV_OK,
          "allow an extra import (granting twice is fine)");
    r = NULL;
    CHECK(o && zau_audit(o, g_mod.b, g_mod.n, "fn f();", 7, &r) == ZXV_OK && r &&
              count_code(r, "IMPORT_DENIED", -1, NULL) == 0 && count_in(r, 2, "IMPORT_SIG") == 1,
          "env.clock now allowed; the propose signature still wrong");
    zau_report_destroy(r);
    CHECK(zau_options_grant(o, 0x8u) == ZXV_E_ARG && zau_options_set_flags(o, 0x10u) == ZXV_E_ARG &&
              zau_options_grant(NULL, 1) == ZXV_E_ARG && zau_options_set_limit(o, 999, 1) == ZXV_E_ARG &&
              zau_options_allow_import(o, "m", "n", "f()") == ZXV_E_ARG,
          "option misuse");
    zau_options_destroy(o);
    zau_options_destroy(NULL);

    /* a global import: refused by kind */
    {
        static const uint8_t gimp[] = {0x03, 'e', 'n', 'v', 0x01, 'g', 0x03, 0x7F, 0x00};
        code_reset();
        wt_one(&t, g_code.b, 0);
        t.raw_imports = gimp;
        t.raw_imports_len = sizeof gimp;
        t.raw_imports_count = 1;
        t.exports[0] = "f";
        t.nexports = 1;
        wt_build(&g_mod, &t);
        r = audit(&g_mod, "fn f();");
        CHECK(r && count_in(r, 2, "IMPORT_KIND") == 1 && count_in(r, 0, "IMPORT_KIND") == 1, "a global import is refused");
        zau_report_destroy(r);
    }
}

static void test_contract(void) {
    zau_report *r;
    zau_options *o = NULL;
    build_fee();
    CHECK(zau_options_create(&o) == ZXV_OK && zau_options_grant(o, ZW_GRANT_PROPOSE) == ZXV_OK, "options");
    r = NULL;
    CHECK(zau_audit(o, g_mod.b, g_mod.n, NULL, 0, &r) == ZXV_OK && r && !passed(r) &&
              count_code(r, "NO_CONTRACT", ZAU_ERROR, NULL) == 1 && count_code(r, "UNDECLARED", ZAU_ERROR, "fee") == 1 &&
              check_status(r, 5) == ZAU_SKIP && check_status(r, 6) == ZAU_SKIP,
          "no contract: every exported function fails");
    zau_report_destroy(r);
    r = NULL;
    CHECK(zau_audit(o, g_mod.b, g_mod.n, "fn fee(a: int[0..", 17, &r) == ZXV_OK && r &&
              count_code(r, "SYNTAX", ZAU_ERROR, NULL) == 1 && check_status(r, 4) == ZAU_SKIP,
          "a contract that does not parse");
    {
        const char *msg = NULL;
        uint32_t n = 0;
        int found = 0;
        zau_report_finding_count(r, &n);
        for (uint32_t i = 0; i < n; i++) {
            const char *code = NULL;
            zau_report_finding(r, i, NULL, NULL, &code, &msg, NULL, NULL, NULL);
            if (strcmp(code, "SYNTAX") == 0 && strstr(msg, "line 1, column 18")) found = 1;
        }
        CHECK(found, "the syntax finding has the line and column");
    }
    zau_report_destroy(r);
    CHECK(zau_audit(o, g_mod.b, g_mod.n, NULL, 3, &r) == ZXV_E_ARG && zau_audit(NULL, g_mod.b, 1, NULL, 0, &r) == ZXV_E_ARG &&
              zau_audit(o, NULL, 0, NULL, 0, &r) == ZXV_E_ARG && zau_audit(o, g_mod.b, g_mod.n, NULL, 0, NULL) == ZXV_E_ARG,
          "audit misuse");
    zau_options_destroy(o);
}

static void test_declared(void) {
    zau_report *r;
    build_fee();
    r = audit(&g_mod, "fn other(x: int);");
    CHECK(r && count_code(r, "UNDECLARED", ZAU_ERROR, "fee") == 1 && count_code(r, "NOT_EXPORTED", ZAU_WARNING, "other") == 1,
          "an undeclared export is an error; a declared function not exported is a warning");
    zau_report_destroy(r);
    r = audit(&g_mod, "fn fee(amount: int) -> int proposes rat max 1;");
    CHECK(r && count_code(r, "ARITY", ZAU_ERROR, "fee") == 1, "one parameter declared, two in the export");
    zau_report_destroy(r);
    r = audit(&g_mod, "fn fee(amount: rat) -> int proposes rat max 1;");
    CHECK(r && passed(r) && count_code(r, "DECLARED", ZAU_INFO, "fee") == 1, "a rat parameter takes two arguments");
    zau_report_destroy(r);
    r = audit(&g_mod, "fn fee(a: bytes, b: int) -> int proposes rat max 1;");
    CHECK(r && count_code(r, "MAPPING", ZAU_ERROR, "fee") == 1, "bytes cannot be passed");
    zau_report_destroy(r);
    r = audit(&g_mod, "fn fee(a: int, b: int) -> rat proposes rat max 1;");
    CHECK(r && count_code(r, "MAPPING", ZAU_ERROR, "fee") == 1, "a rat result cannot come back");
    zau_report_destroy(r);
    r = audit(&g_mod, "fn fee(a: int, b: int) proposes rat max 1;");
    CHECK(r && count_code(r, "RESULT_UNDECLARED", ZAU_ERROR, "fee") == 1, "an undeclared result");
    zau_report_destroy(r);
    code_reset();
    build("II", "fee", 0, NULL);
    r = audit(&g_mod, "fn fee(a: int, b: int) -> int;");
    CHECK(r && count_code(r, "RESULT_MISSING", ZAU_ERROR, "fee") == 1, "a declared result the export does not return");
    zau_report_destroy(r);

    /* a memory export needs no declaration */
    {
        wb s;
        static const uint8_t hdr[8] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
        wb_reset(&g_mod);
        wb_bytes(&g_mod, hdr, 8);
        wb_reset(&s);
        wb_uleb(&s, 1);
        wb_u8(&s, 1);
        wb_uleb(&s, 1);
        wb_uleb(&s, 1);
        wb_section(&g_mod, 5, &s);
        wb_reset(&s);
        wb_uleb(&s, 1);
        wb_name(&s, "memory");
        wb_u8(&s, 2);
        wb_uleb(&s, 0);
        wb_section(&g_mod, 7, &s);
        r = audit(&g_mod, "");
        CHECK(r && passed(r) && count_code(r, "NOT_A_FUNCTION", ZAU_INFO, "memory") == 1, "memory export: a note only");
        if (r && !passed(r)) dump(r);
        zau_report_destroy(r);
    }
}

static void test_bounds(void) {
    zau_report *r;
    code_reset();
    lget(0);
    build("i:i", "f", 0, NULL);
    r = audit(&g_mod, "fn f(x: int[0..10^10]) -> int;");
    CHECK(r && count_code(r, "I32_RANGE", ZAU_WARNING, "f") == 1 && passed(r),
          "an i32 parameter with a range wider than int32: a warning");
    zau_report_destroy(r);
    r = audit_flags(&g_mod, "fn f(x: int[0..10^10]) -> int;", 0, ZAU_F_STRICT);
    CHECK(r && !passed(r), "... which fails under --strict");
    zau_report_destroy(r);
    r = audit(&g_mod, "fn f(x: int[3000000000..4000000000]) -> int;");
    CHECK(r && count_code(r, "I32_RANGE", ZAU_ERROR, "f") == 1, "a range no i32 can carry: an error");
    zau_report_destroy(r);
    r = audit(&g_mod, "fn f(x: int) -> int[3000000000..*];");
    CHECK(r && count_code(r, "I32_RANGE", ZAU_ERROR, "f") == 1, "a result range no i32 result reaches: an error");
    zau_report_destroy(r);
    r = audit(&g_mod, "fn f(x: int[-5..5]) -> int;\nfn g(r: rat[1/3..1/3] den|10);");
    CHECK(r && count_code(r, "EMPTY_BOUNDS", ZAU_ERROR, "g") == 1, "1/3 is no multiple of 1/10: empty");
    zau_report_destroy(r);
    r = audit(&g_mod, "fn f(x: int) -> int;\nfn g(r: rat[1/3..1/3] den<=2);\nfn h(r: rat[1/3..1/2] den<=3);\n"
                      "fn k() proposes rat[1/7..1/7] den|14 max 1;\nfn m() proposes rat[1/7..1/7] den|13 max 1;");
    CHECK(r && count_code(r, "EMPTY_BOUNDS", ZAU_ERROR, "g") == 1 && count_code(r, "EMPTY_BOUNDS", -1, "h") == 0 &&
              count_code(r, "EMPTY_BOUNDS", -1, "k") == 0 && count_code(r, "EMPTY_BOUNDS", ZAU_ERROR, "m") == 1,
          "den<= and den| feasibility");
    zau_report_destroy(r);
    r = audit(&g_mod, "fn f(x: int) -> int;\nfn g(r: rat[1/100003..1/100003] den<=200000);\n"
                      "fn h(r: rat[-7/2..-3/2] den|2);\nfn k(r: rat[-1/3..-1/4] den|5);");
    CHECK(r && count_code(r, "BOUNDS_UNDETERMINED", ZAU_INFO, "g") == 1 && count_code(r, "EMPTY_BOUNDS", -1, "h") == 0 &&
              count_code(r, "EMPTY_BOUNDS", ZAU_ERROR, "k") == 1,
          "beyond the search bound: undetermined, said so; negative ranges");
    zau_report_destroy(r);
    code_reset();
    lget(0);
    op(0x1A);
    build("ii", "f", 0, NULL);
    r = audit(&g_mod, "fn f(x: rat);");
    CHECK(r && count_code(r, "I32_RANGE", ZAU_WARNING, "f") == 1, "a rat through i32 parameters: a warning");
    zau_report_destroy(r);
}

/* Analyse one body: type, contract, and what must be found. */
static zau_report *an(const char *type, const char *contract, int with_propose, const char *locals) {
    build(type, "f", with_propose, locals);
    return audit(&g_mod, contract);
}

static void test_analysis(void) {
    zau_report *r;
    code_reset();
    i32c(5);
    r = an(":i", "fn f() -> int[0..10];", 0, NULL);
    CHECK(r && count_code(r, "RESULT_IN_RANGE", ZAU_INFO, "f") == 1 && passed(r), "constant 5 in [0..10]");
    zau_report_destroy(r);
    code_reset();
    i32c(50);
    r = an(":i", "fn f() -> int[0..10];", 0, NULL);
    CHECK(r && count_code(r, "RESULT_ALWAYS_OUTSIDE", ZAU_ERROR, "f") == 1 && !passed(r), "constant 50: always outside");
    zau_report_destroy(r);
    code_reset();
    lget(0);
    i32c(5);
    op(0x6A);
    r = an("i:i", "fn f(x: int[0..10]) -> int[0..10];", 0, NULL);
    CHECK(r && count_code(r, "RESULT_MAY_LEAVE", ZAU_WARNING, "f") == 1 && passed(r), "x + 5 in [5..15]: may leave");
    zau_report_destroy(r);
    r = an("i:i", "fn f(x: int[0..10]) -> int[5..15];", 0, NULL);
    CHECK(r && count_code(r, "RESULT_IN_RANGE", ZAU_INFO, "f") == 1, "x + 5 in [5..15]: in range");
    zau_report_destroy(r);
    /* i32 overflow wraps: INT32_MAX + 1 is not 2^31 */
    code_reset();
    i32c(INT32_MAX);
    i32c(1);
    op(0x6A);
    r = an(":i", "fn f() -> int[0..*];", 0, NULL);
    CHECK(r && count_code(r, "RESULT_NOT_BOUNDED", ZAU_INFO, "f") == 1 && count_code(r, "RESULT_IN_RANGE", -1, NULL) == 0,
          "i32 overflow gives top, not a wrong interval");
    zau_report_destroy(r);
    /* i64 multiplication overflow */
    code_reset();
    lget(0);
    lget(0);
    op(0x7E);
    r = an("I:I", "fn f(x: int[0..10^12]) -> int[0..10^24];", 0, NULL);
    CHECK(r == NULL || count_code(r, "RESULT_IN_RANGE", -1, NULL) == 0, "10^24 is outside int64 (contract refuses or top)");
    zau_report_destroy(r);
    r = an("I:I", "fn f(x: int[0..10^9]) -> int[0..10^18];", 0, NULL);
    CHECK(r && count_code(r, "RESULT_IN_RANGE", ZAU_INFO, "f") == 1, "x*x for x in [0..10^9] stays in [0..10^18]");
    zau_report_destroy(r);
    r = an("I:I", "fn f(x: int[-10^10..10^10]) -> int[0..*];", 0, NULL);
    CHECK(r && count_code(r, "RESULT_NOT_BOUNDED", ZAU_INFO, "f") == 1, "x*x for |x| <= 10^10 overflows: top");
    zau_report_destroy(r);
    /* folding */
    code_reset();
    i32c(-7);
    i32c(3);
    op(0x6F); /* rem_s: -1 */
    i64c(-16);
    i64c(2);
    op(0x87); /* i64.shr_s: -4 */
    op(0xA7); /* wrap: -4 */
    op(0x6A); /* -5 */
    r = an(":i", "fn f() -> int[-5..-5];", 0, NULL);
    CHECK(r && count_code(r, "RESULT_IN_RANGE", ZAU_INFO, "f") == 1, "rem_s, shr_s, wrap and add fold to -5");
    if (r && !count_code(r, "RESULT_IN_RANGE", ZAU_INFO, "f")) dump(r);
    zau_report_destroy(r);
    code_reset();
    i32c(-1);
    op(0xAD); /* i64.extend_i32_u */
    r = an(":I", "fn f() -> int[4294967295..4294967295];", 0, NULL);
    CHECK(r && count_code(r, "RESULT_IN_RANGE", ZAU_INFO, "f") == 1, "extend_i32_u of -1 is 2^32 - 1");
    zau_report_destroy(r);
    code_reset();
    i32c(1);
    i32c(2);
    lget(0);
    i32c(49);
    op(0x4A); /* gt_s */
    op(0x1B); /* select */
    r = an("i:i", "fn f(p: int[0..100]) -> int[1..2];", 0, NULL);
    CHECK(r && count_code(r, "RESULT_IN_RANGE", ZAU_INFO, "f") == 1, "select of 1 and 2");
    zau_report_destroy(r);
    r = an("i:i", "fn f(p: int[50..100]) -> int[1..1];", 0, NULL);
    CHECK(r && count_code(r, "RESULT_MAY_LEAVE", ZAU_WARNING, "f") == 1,
          "the comparison is not refined from the parameter range: a warning, not a wrong claim");
    zau_report_destroy(r);
    code_reset();
    i32c(0);
    op(0x45); /* eqz: 1 */
    i32c(3);
    i32c(3);
    op(0x46); /* eq: 1 */
    op(0x6A);
    r = an(":i", "fn f() -> truth{false};", 0, NULL);
    CHECK(r && count_code(r, "RESULT_IN_RANGE", ZAU_INFO, "f") == 1, "eqz + eq = 2 = ZXV_LP_FALSE, a declared truth");
    zau_report_destroy(r);
    r = an(":i", "fn f() -> truth{true};", 0, NULL);
    CHECK(r && count_code(r, "RESULT_ALWAYS_OUTSIDE", ZAU_ERROR, "f") == 1, "2 is not truth{true}");
    zau_report_destroy(r);
    /* locals start at zero; set / tee */
    code_reset();
    lget(1);
    i32c(4);
    op(0x6A);
    op(0x22);
    wb_uleb(&g_code, 1); /* tee 1 */
    op(0x1A);
    lget(1);
    lget(0);
    op(0x6C); /* mul */
    r = an("i:i", "fn f(x: int[1..3]) -> int[4..12];", 0, "i");
    CHECK(r && count_code(r, "RESULT_IN_RANGE", ZAU_INFO, "f") == 1, "a local, tee and mul: [4..12]");
    zau_report_destroy(r);

    /* proposals */
    code_reset();
    i32c(1);
    i64c(3);
    i64c(2);
    i32c(0);
    i32c(0);
    op(0x10);
    wb_uleb(&g_code, 0);
    r = an(":i", "fn f() -> int proposes rat[0..1] max 1;", 1, NULL);
    CHECK(r && count_code(r, "PROPOSAL_OUT_OF_BOUNDS", ZAU_ERROR, "f") == 1 && !passed(r), "the constant 3/2 is out of [0..1]");
    zau_report_destroy(r);
    r = an(":i", "fn f() -> int proposes rat[0..2] den|2 max 1;", 1, NULL);
    CHECK(r && passed(r) && count_code(r, "PROPOSAL_OUT_OF_BOUNDS", -1, NULL) == 0, "3/2 in [0..2] den|2 is fine");
    zau_report_destroy(r);
    r = an(":i", "fn f() -> int;", 1, NULL);
    CHECK(r && count_code(r, "PROPOSES_UNDECLARED", ZAU_ERROR, "f") == 1, "a propose call without a proposes clause");
    zau_report_destroy(r);
    op(0x1A);
    i32c(1);
    i64c(1);
    i64c(2);
    i32c(0);
    i32c(0);
    op(0x10);
    wb_uleb(&g_code, 0);
    r = an(":i", "fn f() -> int proposes rat[0..2] max 1;", 1, NULL);
    CHECK(r && count_code(r, "PROPOSAL_COUNT", ZAU_ERROR, "f") == 1, "two propose calls, max 1");
    zau_report_destroy(r);

    /* skipped bodies */
    code_reset();
    op(0x03);
    op(0x40);
    op(0x0B);
    i32c(0);
    r = an(":i", "fn f() -> int[0..0];", 0, NULL);
    CHECK(r && count_code(r, "NOT_ANALYZED", ZAU_INFO, "f") == 1 && count_code(r, "RESULT_IN_RANGE", -1, NULL) == 0,
          "a loop is not analysed, and the report says so");
    zau_report_destroy(r);
    code_reset();
    op(0x00);
    r = an(":i", "fn f() -> int[0..0];", 0, NULL);
    CHECK(r && count_code(r, "ALWAYS_TRAPS", ZAU_INFO, "f") == 1, "unreachable: never returns normally");
    zau_report_destroy(r);
    code_reset();
    i32c(0);
    r = an(":i", "fn f() -> int[0..0];", 0, NULL);
    zau_report_destroy(r);
    {
        wt t;
        static const uint8_t c0[] = {0x10, 0x01}; /* call 1 */
        static const uint8_t c1[] = {0x41, 0x07};
        memset(&t, 0, sizeof t);
        wt_one(&t, c0, sizeof c0);
        t.types[0] = ":i";
        t.funcs[1].type = 0;
        t.funcs[1].code = c1;
        t.funcs[1].code_len = sizeof c1;
        t.nfuncs = 2;
        t.exports[0] = "f";
        t.nexports = 1;
        wt_build(&g_mod, &t);
        r = audit(&g_mod, "fn f() -> int[7..7];");
        CHECK(r && count_code(r, "NOT_ANALYZED", ZAU_INFO, "f") == 1, "a call to a defined function: not analysed");
        zau_report_destroy(r);
    }
    code_reset();
    i32c(50);
    build(":i", "f", 0, NULL);
    r = audit_flags(&g_mod, "fn f() -> int[0..10];", 0, ZAU_F_NO_ANALYSIS);
    CHECK(r && passed(r) && check_status(r, 6) == ZAU_SKIP, "--no-analysis skips the analysis");
    zau_report_destroy(r);
}

static void test_reports(void) {
    zau_report *r1, *r2;
    size_t n1 = 0, n2 = 0, tn = 0;
    char *j1, *j2, *t;
    char small[8];
    build_fee();
    r1 = audit(&g_mod, k_fee);
    r2 = audit(&g_mod, k_fee);
    j1 = r1 ? report_json(r1, "plugin \"q\"\n.wasm", "fees.zc", &n1) : NULL;
    j2 = r2 ? report_json(r2, "plugin \"q\"\n.wasm", "fees.zc", &n2) : NULL;
    CHECK(j1 && j2 && n1 == n2 && memcmp(j1, j2, n1) == 0, "the JSON report is deterministic");
    CHECK(j1 && json_ok(j1, n1), "the JSON report is valid JSON");
    CHECK(j1 && strstr(j1, "\"result\": \"pass\"") && strstr(j1, "\"tool\": \"z-audit\"") &&
              strstr(j1, "plugin \\\"q\\\"\\n.wasm") && strstr(j1, "\"digest_sha3_256\": \"") &&
              strstr(j1, "not prove"),
          "the JSON has the verdict, escaped labels, the digest and the bounded-checks note");
    CHECK(r1 && zau_report_json(r1, NULL, NULL, small, sizeof small, &n2) == ZXV_E_BUFFER && n2 > 100 &&
              zau_report_json(r1, NULL, NULL, NULL, 0, NULL) == ZXV_E_ARG,
          "R4 two-call buffer");
    t = r1 ? report_text(r1, &tn) : NULL;
    CHECK(t && strstr(t, "RESULT: PASS") && strstr(t, "PASS  module.validate") && strstr(t, "not prove"),
          "the text report");
    free(t);
    free(j1);
    free(j2);
    zau_report_destroy(r2);
    zau_report_destroy(r1);

    /* a failing report */
    code_reset();
    i32c(50);
    build(":i", "f", 0, NULL);
    r1 = audit(&g_mod, "fn f() -> int[0..10];\nfn g();");
    j1 = r1 ? report_json(r1, NULL, NULL, &n1) : NULL;
    t = r1 ? report_text(r1, &tn) : NULL;
    CHECK(j1 && json_ok(j1, n1) && strstr(j1, "\"result\": \"fail\"") && strstr(j1, "\"label\": null") &&
              strstr(j1, "RESULT_ALWAYS_OUTSIDE"),
          "the failing JSON report");
    CHECK(t && strstr(t, "RESULT: FAIL (1 error(s), 1 warning(s))") && strstr(t, "FAIL  exports.analysis") &&
              strstr(t, "WARN  exports.declared"),
          "the failing text report");
    if (t && !strstr(t, "RESULT: FAIL (1 error(s), 1 warning(s))")) fprintf(stderr, "%s", t);
    uint32_t e = 0, w = 0;
    int32_t p = 1;
    CHECK(r1 && zau_report_verdict(r1, &p, &e, &w) == ZXV_OK && p == 0 && e == 1 && w == 1 &&
              zau_report_verdict(NULL, &p, NULL, NULL) == ZXV_E_ARG,
          "verdict");
    free(t);
    free(j1);
    zau_report_destroy(r1);
    zau_report_destroy(NULL);
    {
        char b[64];
        size_t n = 0;
        CHECK(strcmp(zau_status_name(ZAU_E_INPUT), "ZAU_E_INPUT") == 0 &&
                  strcmp(zau_status_name(ZXV_E_ARG), "ZXV_E_ARG") == 0 && zau_last_error(b, sizeof b, &n) == ZXV_OK,
              "status names, last error");
    }
}

static void test_truncated(void) {
    int ok = 1, consistent = 1;
    wb full;
    build_fee();
    full = g_mod;
    for (size_t cut = 0; cut < full.n; cut++) {
        zau_options *o = NULL;
        zau_report *r = NULL;
        size_t n = 0;
        char *j;
        if (zau_options_create(&o) != ZXV_OK) return;
        (void) zau_options_grant(o, ZW_GRANT_PROPOSE);
        if (zau_audit(o, full.b, cut, k_fee, strlen(k_fee), &r) != ZXV_OK || !r) ok = 0;
        else {
            if (passed(r) && check_status(r, 0) != ZAU_PASS) consistent = 0;
            j = report_json(r, "t", "c", &n);
            if (!j || !json_ok(j, n)) ok = 0;
            free(j);
        }
        zau_report_destroy(r);
        zau_options_destroy(o);
    }
    CHECK(ok, "every prefix of a module gives a report with valid JSON");
    CHECK(consistent, "a prefix passes only if the validator accepts it");
    {
        static const uint8_t junk[] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF};
        wb_reset(&g_mod);
        wb_bytes(&g_mod, junk, sizeof junk);
        zau_report *r = audit(&g_mod, k_fee);
        CHECK(r && count_code(r, "MALFORMED", ZAU_INFO, NULL) == 1 && check_status(r, 2) == ZAU_SKIP &&
                  check_status(r, 4) == ZAU_SKIP && !passed(r),
              "a malformed module: the validator fails it, the rest says why it was skipped");
        zau_report_destroy(r);
    }
}

int main(void) {
    printf("test_audit: z-audit checks, analysis and reports\n");
    test_clean();
    test_float_simd();
    test_grants();
    test_imports();
    test_contract();
    test_declared();
    test_bounds();
    test_analysis();
    test_reports();
    test_truncated();
    printf("test_audit: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
