/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_orbital_compat.c — the elevator car, exercised.
 *
 * The five anchors:
 *  (1) a known COBOL COMP-3 value lowers to the EXACT IR field — asserted
 *      against Lightning Rod's OWN known result, not this module's echo.
 *  (2) a Fortran fixed-format value round-trips (lower then lift = itself).
 *  (3) the SAME numeric value from COBOL and from Fortran yields EQUAL IR.
 *  (4) the Sutra language registers at the ops boundary and an IR round-trips.
 *  (5) an unregistered language fails CLOSED (OC_ERR_NO_LANG), never fabricates.
 *
 *   cc -std=c11 -Wall -Werror -Wextra -O1 -fsanitize=address,undefined \
 *      -DTEST_HOST -Isrc/orbital_compat -Isrc/lightningrod -Isrc/rational \
 *      src/orbital_compat/test_orbital_compat.c src/orbital_compat/orbital_compat.c \
 *      src/lightningrod/lightningrod.c src/rational/rational.c -o /tmp/test_oc -lm && /tmp/test_oc
 */
#include <stdio.h>
#include <string.h>
#include "orbital_compat.h"
#include "lightningrod.h"

static int failures = 0;
static int checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)
static const char *S(rat_t r)
{
    static char b[4][64];
    static int i = 0;
    i = (i + 1) & 3;
    rat_to_string(r, b[i], 64);
    return b[i];
}

/* ---- a REAL Sutra ops boundary: the AI language speaks exact ratios ---- */
static int32_t t_sutra_lower(const void *src, uint32_t len, oc_ir_t *out)
{
    if (len < sizeof(oc_sutra_src_t)) return OC_ERR_ARG;
    const oc_sutra_src_t *s = (const oc_sutra_src_t *) src;
    rat_t v = rat_make(s->num, s->den);
    if (!v.valid) return OC_ERR_CONV;
    out->num_fields = 1;
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = v;
    out->fields[0].scale = 0;
    return OC_OK;
}
static int32_t t_sutra_lift(const oc_ir_t *ir, void *out, uint32_t cap)
{
    if (cap < sizeof(oc_sutra_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 1 || ir->fields[0].type != OC_TYPE_RATIONAL) return OC_ERR_ARG;
    rat_t v = ir->fields[0].num;
    if (!v.valid) return OC_ERR_CONV;
    oc_sutra_src_t *s = (oc_sutra_src_t *) out;
    s->num = v.num;
    s->den = v.den;
    return (int32_t) sizeof(oc_sutra_src_t);
}
static const oc_lang_ops_t SUTRA_OPS = {"Sutra/ratio", t_sutra_lower, t_sutra_lift};

int main(void)
{
    printf("=== Orbital Compat — one IR, every language floor ===\n");
    /* ---------- Anchor 5 (part A): unregistered fails CLOSED ----------
     * Runs before oc_register_builtins(), which now binds every language. */
    {
        CHECK(!oc_lang_registered(OC_LANG_SUTRA), "Sutra starts UNBOUND");
        CHECK(!oc_lang_registered(OC_LANG_TELECOM), "Telecom starts UNBOUND");
        oc_sutra_src_t s = {.num = 2469, .den = 20};
        oc_ir_t ir;
        CHECK(oc_lower(OC_LANG_SUTRA, &s, sizeof(s), &ir) == OC_ERR_NO_LANG,
              "lowering an unbound language fails closed (OC_ERR_NO_LANG)");
        oc_ir_t any;
        memset(&any, 0, sizeof(any));
        uint8_t buf[16];
        CHECK(oc_lift(OC_LANG_TELECOM, &any, buf, sizeof(buf)) == OC_ERR_NO_LANG,
              "lifting to an unbound language fails closed (no fabrication)");
    }

    oc_register_builtins();

    /* ---------- Anchor 1: COBOL COMP-3 -> exact IR (vs Lightning Rod) ---------- */
    {
        /* PIC S9(5)V99 holding 12345.67 => 12 34 56 7C, scale 2 */
        uint8_t comp3[] = {0x12, 0x34, 0x56, 0x7C};

        /* Lightning Rod's OWN known answer — the ground truth we assert to. */
        rat_t truth;
        lr_result_t lr = lr_packed_to_rat(comp3, 4, 2, &truth);
        CHECK(lr.ok && lr.exact, "Lightning Rod converts the COMP-3 field exactly");

        oc_cobol_src_t src = {comp3, 4, 2};
        oc_ir_t ir;
        int32_t rc = oc_lower(OC_LANG_COBOL, &src, sizeof(src), &ir);
        printf("       COBOL COMP-3 12 34 56 7C @2 -> IR %s\n", S(ir.fields[0].num));
        CHECK(rc == OC_OK, "COBOL lowers to the IR");
        CHECK(ir.num_fields == 1 && ir.fields[0].type == OC_TYPE_RATIONAL,
              "IR field is a normalized RATIONAL");
        CHECK(rat_eq(ir.fields[0].num, truth),
              "IR value EQUALS Lightning Rod's own result (not an echo)");
        CHECK(rat_eq(ir.fields[0].num, rat_from_string("12345.67")),
              "IR value is exactly 12345.67");
    }

    /* ---------- Anchor 2: Fortran fixed-format round-trip ---------- */
    {
        oc_fortran_src_t x = {12345, 2}; /* fixed-format 123.45 */
        oc_ir_t ir;
        CHECK(oc_lower(OC_LANG_FORTRAN, &x, sizeof(x), &ir) == OC_OK,
              "Fortran value lowers to the IR");
        oc_fortran_src_t y = {0, 0};
        int32_t n = oc_lift(OC_LANG_FORTRAN, &ir, &y, sizeof(y));
        CHECK(n == (int32_t) sizeof(y), "Fortran lifts back out of the IR");
        CHECK(y.value == x.value && y.scale == x.scale,
              "Fortran round-trip reproduces the exact source value");
    }

    /* ---------- Anchor 3: IR is language-agnostic ---------- */
    {
        /* same value, 123.45, from two utterly different legacy layouts */
        uint8_t comp3[] = {0x12, 0x34, 0x5C}; /* COBOL 12345 @2 */
        oc_cobol_src_t csrc = {comp3, 3, 2};
        oc_fortran_src_t fsrc = {12345, 2};
        oc_ir_t ic, iff;
        CHECK(oc_lower(OC_LANG_COBOL, &csrc, sizeof(csrc), &ic) == OC_OK &&
                  oc_lower(OC_LANG_FORTRAN, &fsrc, sizeof(fsrc), &iff) == OC_OK,
              "both COBOL and Fortran lower the same value");
        printf("       COBOL=%s  Fortran=%s\n", S(ic.fields[0].num), S(iff.fields[0].num));
        CHECK(oc_field_eq(&ic.fields[0], &iff.fields[0]),
              "COBOL and Fortran produce an EQUAL IR field (cross-language)");
        CHECK(rat_eq(ic.fields[0].num, rat_from_string("123.45")),
              "the shared IR value is exactly 123.45");
    }

    /* ---------- Anchor 4: Sutra registers, IR round-trips ---------- */
    {
        CHECK(oc_register_lang(OC_LANG_SUTRA, &SUTRA_OPS) == OC_OK,
              "Sutra binds a real adapter at the ops boundary");
        CHECK(oc_lang_registered(OC_LANG_SUTRA), "Sutra is now registered");

        oc_sutra_src_t s = {.num = 2469, .den = 20}; /* 123.45 as an exact ratio */
        oc_ir_t ir;
        CHECK(oc_lower(OC_LANG_SUTRA, &s, sizeof(s), &ir) == OC_OK, "Sutra lowers to the IR");
        oc_sutra_src_t back = {.num = 0, .den = 0};
        CHECK(oc_lift(OC_LANG_SUTRA, &ir, &back, sizeof(back)) == (int32_t) sizeof(back),
              "Sutra lifts back out of the IR");
        CHECK(rat_eq(rat_make(back.num, back.den), rat_make(s.num, s.den)),
              "Sutra round-trip preserves the exact value");
        /* bonus: Sutra's IR equals the COBOL/Fortran IR for the same value */
        CHECK(rat_eq(ir.fields[0].num, rat_from_string("123.45")),
              "Sutra reaches the same floor as COBOL and Fortran (123.45)");
    }

    /* ---------- honesty: a lossy lift is REFUSED, not rounded ---------- */
    {
        /* 1/3 has no exact scaled-integer form -> Fortran back-end must refuse */
        oc_ir_t ir;
        ir.num_fields = 1;
        ir.fields[0].type = OC_TYPE_RATIONAL;
        ir.fields[0].num = rat_make(1, 3);
        ir.fields[0].scale = 2;
        oc_fortran_src_t y;
        CHECK(oc_lift(OC_LANG_FORTRAN, &ir, &y, sizeof(y)) == OC_ERR_CONV,
              "1/3 lifted to a 2-place field is REFUSED, never silently rounded");
    }

    /* ---------- honesty: an out-of-range scale is REFUSED, not overflowed ---- */
    {
        /* scale 25 would make 10^scale overflow int64 and emit a wrong-but-
         * plausible value; the lift must refuse rather than fabricate. */
        oc_ir_t ir;
        ir.num_fields = 1;
        ir.fields[0].type = OC_TYPE_RATIONAL;
        ir.fields[0].num = rat_make(5, 1);
        ir.fields[0].scale = 25;
        oc_fortran_src_t y;
        CHECK(oc_lift(OC_LANG_FORTRAN, &ir, &y, sizeof(y)) == OC_ERR_CONV,
              "a scale of 25 (10^scale overflows int64) is REFUSED, not overflowed");
    }

    /* ---------- the real Sutra adapter keeps every truth value ----------
     * The tag rides in lpres numbering. A contradiction must survive the
     * round trip, NEITHER is never embedded, and a malformed tag can only
     * ever read back as NEITHER, never as a truth value. */
    {
        const lpres_state_t all[4] = {LPRES_STATE_NEITHER, LPRES_STATE_TRUE, LPRES_STATE_FALSE,
                                      LPRES_STATE_BOTH};
        for (int k = 0; k < 4; k++) {
            oc_sutra_src_t x = {7, 3, all[k], 0}, y;
            oc_ir_t ir;
            memset(&y, 0, sizeof(y));
            CHECK(sutra_lower(&x, sizeof(x), &ir) == OC_OK, "real Sutra lowers 7/3 with a tag");
            CHECK(ir.num_fields == (all[k] == LPRES_STATE_NEITHER ? 1u : 2u),
                  "a tag is embedded exactly when it is not NEITHER");
            CHECK(sutra_lift(&ir, &y, sizeof(y)) == (int32_t) sizeof(y),
                  "real Sutra lifts the IR back");
            CHECK(y.num == 7 && y.den == 3 && y.logic_state == all[k],
                  "value and tag round-trip unchanged");
        }

        oc_sutra_src_t bad = {1, 1, (lpres_state_t) 4, 0};
        oc_ir_t ir;
        CHECK(sutra_lower(&bad, sizeof(bad), &ir) == OC_ERR_CONV,
              "a tag outside the four states is REFUSED on lowering");

        ir.num_fields = 2;
        ir.fields[0].type = OC_TYPE_RATIONAL;
        ir.fields[0].num = rat_make(1, 1);
        ir.fields[0].scale = 0;
        ir.fields[1].type = OC_TYPE_RATIONAL;
        ir.fields[1].scale = 0;
        const int64_t junk[3] = {4, -1, 99};
        for (int k = 0; k < 3; k++) {
            ir.fields[1].num = rat_make(junk[k], 1);
            oc_sutra_src_t y;
            CHECK(sutra_lift(&ir, &y, sizeof(y)) > 0 && y.logic_state == LPRES_STATE_NEITHER,
                  "an out-of-range tag lifts as NEITHER, never as a truth value");
        }
        ir.fields[1].num = rat_make(1, 2);
        CHECK(oc_field_lpres(&ir, 1, LPRES_STATE_NEITHER) == LPRES_STATE_NEITHER,
              "a fractional tag reads as NEITHER");
        CHECK(oc_field_lpres(&ir, 5, LPRES_STATE_FALSE) == LPRES_STATE_FALSE,
              "a missing field gives the caller's default");

        /* The logic ops read the same numbering as lpres itself. */
        const oc_sutra_extended_ops_t *ops = oc_get_sutra_extended_ops();
        oc_ir_t a, b, r;
        oc_sutra_src_t ta = {1, 1, LPRES_STATE_TRUE, 0}, tb = {1, 1, LPRES_STATE_BOTH, 0};
        oc_sutra_src_t tn = {1, 1, LPRES_STATE_NEITHER, 0};
        CHECK(sutra_lower(&ta, sizeof(ta), &a) == OC_OK && sutra_lower(&tb, sizeof(tb), &b) == OC_OK,
              "operands lower");
        CHECK(ops->lpres_conjoin(&a, &b, &r) == OC_OK && r.fields[0].num.num == LPRES_STATE_BOTH,
              "TRUE AND BOTH is BOTH: the contradiction is kept");
        CHECK(ops->lpres_negate(&b, &r) == OC_OK && r.fields[0].num.num == LPRES_STATE_BOTH,
              "NOT BOTH is BOTH");
        CHECK(sutra_lower(&tn, sizeof(tn), &b) == OC_OK && ops->lpres_disjoin(&a, &b, &r) == OC_OK &&
                  r.fields[0].num.num == lpres_disjoin(LPRES_STATE_TRUE, LPRES_STATE_NEITHER),
              "an untagged value takes part as NEITHER");
        CHECK(ops->self_audit(&a) == 0, "a well-formed tagged value passes self-audit");
    }

    printf("\n%d checks, %s: %d failure(s)\n", checks, failures ? "*** FAILED ***" : "ALL PASS",
           failures);
    return failures ? 1 : 0;
}
