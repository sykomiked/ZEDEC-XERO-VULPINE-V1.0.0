/* test_lightningrod.c — legacy representation adapters.
 *
 * The headline test is the COBOL one: a real COMP-3 payroll field is
 * converted to exact rational, round-tripped back, and compared against
 * what the same value becomes through a double. The double loses money;
 * the rational does not.
 *
 *   gcc -std=c11 -Wall -Wextra -Isrc/lightningrod -Isrc/rational \
 *       src/lightningrod/test_lightningrod.c src/lightningrod/lightningrod.c \
 *       src/rational/rational.c -o /tmp/test_lr && /tmp/test_lr
 */
#include <stdio.h>
#include <string.h>
#include "lightningrod.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)
static const char *S(rat_t r) {
    static char b[4][64]; static int i=0; i=(i+1)&3;
    rat_to_string(r,b[i],64); return b[i];
}

int main(void) {
    printf("=== Lightning Rod — legacy/modern representation adapters ===\n");

    /* ---- COBOL COMP-3: PIC S9(5)V99 holding 12345.67 ----
     * digits 1234567, scale 2, positive => 0x01 0x23 0x45 0x67 0xC? ...
     * packed as 4 bytes: 12 34 56 7C  (7 digits + sign nibble) */
    {
        uint8_t comp3[] = { 0x12, 0x34, 0x56, 0x7C };
        rat_t v;
        lr_result_t r = lr_packed_to_rat(comp3, 4, 2, &v);
        printf("       COMP-3 12 34 56 7C @scale2 -> %s\n", S(v));
        CHECK(r.ok && r.exact, "packed decimal converts exactly");
        CHECK(rat_eq(v, rat_from_string("12345.67")), "value is exactly 12345.67");

        /* round-trip back to COMP-3 */
        uint8_t back[8]; uint32_t n = 0;
        lr_result_t w = lr_rat_to_packed(v, 2, back, sizeof(back), &n);
        CHECK(w.ok && n == 4, "round-trips back to a 4-byte COMP-3 field");
        CHECK(memcmp(comp3, back, 4) == 0, "round-trip is BYTE-IDENTICAL");
    }

    /* ---- negative COMP-3 ---- */
    {
        uint8_t neg[] = { 0x00, 0x12, 0x3D };     /* -123, scale 0 */
        rat_t v;
        lr_result_t r = lr_packed_to_rat(neg, 3, 0, &v);
        CHECK(r.ok && rat_eq(v, rat_from_int(-123)), "sign nibble D = negative");
        uint8_t back[8]; uint32_t n=0;
        CHECK(lr_rat_to_packed(v, 0, back, sizeof(back), &n).ok,
              "negative value re-packs");
        rat_t v2; lr_packed_to_rat(back, n, 0, &v2);
        CHECK(rat_eq(v, v2), "negative round-trip preserves value");
    }

    /* ---- THE MIGRATION CLAIM: rational keeps the cent, double loses it ---- */
    {
        /* 0.10 in COMP-3: digits 010, scale 2 */
        uint8_t dime[] = { 0x01, 0x0C };
        rat_t r10; lr_packed_to_rat(dime, 2, 2, &r10);
        CHECK(rat_eq(r10, rat_from_string("0.10")), "COMP-3 0.10 is exactly 1/10");

        /* sum it 1000 times both ways */
        rat_t acc = rat_zero();
        for (int i = 0; i < 1000; i++) acc = rat_add(acc, r10);
        double dacc = 0.0;
        for (int i = 0; i < 1000; i++) dacc += 0.10;
        printf("       1000 x COMP-3 0.10  rational=%s   double=%.17f\n", S(acc), dacc);
        CHECK(rat_eq(acc, rat_from_int(100)), "rational ledger totals exactly 100.00");
        CHECK(dacc != 100.0, "the same migration through double DRIFTS (the bug avoided)");
    }

    /* ---- malformed packed fields are rejected, not guessed ---- */
    {
        uint8_t bad[] = { 0xAB, 0xCF };            /* 0xA,0xB are not digits */
        rat_t v;
        CHECK(!lr_packed_to_rat(bad, 2, 0, &v).ok, "invalid BCD digit rejected");
        uint8_t badsign[] = { 0x12, 0x31 };        /* 1 is not a sign nibble */
        CHECK(!lr_packed_to_rat(badsign, 2, 0, &v).ok, "invalid sign nibble rejected");
    }

    /* ---- precision is refused, never rounded ---- */
    {
        uint8_t out[8]; uint32_t n;
        rat_t third = rat_make(1,3);
        lr_result_t r = lr_rat_to_packed(third, 2, out, sizeof(out), &n);
        CHECK(!r.ok, "1/3 into a 2dp field is REFUSED, not silently rounded");
        printf("       refusal note: %s\n", r.note);
    }

    /* ---- zoned decimal ---- */
    {
        uint8_t zoned[] = { 0xF1, 0xF2, 0xF3 };    /* 123 */
        rat_t v;
        CHECK(lr_zoned_to_rat(zoned, 3, 0, &v).ok && rat_eq(v, rat_from_int(123)),
              "zoned decimal 123 converts exactly");
        uint8_t zneg[] = { 0xF4, 0xF5, 0xD6 };     /* -456 via overpunch */
        CHECK(lr_zoned_to_rat(zneg, 3, 0, &v).ok && rat_eq(v, rat_from_int(-456)),
              "zoned overpunched sign handled");
    }

    /* ---- IEEE double reports its own lossiness honestly ---- */
    {
        rat_t v;
        /* 0.5 is exactly representable */
        lr_result_t r = lr_double_bits_to_rat(0x3FE0000000000000ull, &v);
        CHECK(r.ok && r.exact && rat_eq(v, rat_make(1,2)), "double 0.5 is exact");
        /* 0.1 is NOT */
        lr_result_t r2 = lr_double_bits_to_rat(0x3FB999999999999Aull, &v);
        printf("       double 0.1 -> %s (exact=%s)\n", S(v), r2.exact?"yes":"NO");
        CHECK(r2.ok && !r2.exact,
              "double 0.1 is reported NON-exact (honest about binary approximation)");
        CHECK(!lr_double_bits_to_rat(0x7FF0000000000000ull, &v).ok,
              "Infinity has no rational value and is rejected");
    }

    /* ---- EBCDIC <-> ASCII ---- */
    {
        uint8_t eb[] = { 0xC8, 0xC5, 0xD3, 0xD3, 0xD6 };   /* HELLO */
        uint8_t as[16];
        lr_result_t r = lr_ebcdic_to_ascii(eb, 5, as, sizeof(as));
        CHECK(r.ok && r.exact && memcmp(as, "HELLO", 5) == 0, "EBCDIC HELLO -> ASCII");
        uint8_t back[16];
        CHECK(lr_ascii_to_ebcdic(as, 5, back, sizeof(back)).ok &&
              memcmp(back, eb, 5) == 0, "ASCII -> EBCDIC round-trips");
        uint8_t digits[] = { 0xF0, 0xF9 };
        lr_ebcdic_to_ascii(digits, 2, as, sizeof(as));
        CHECK(as[0]=='0' && as[1]=='9', "EBCDIC digits map correctly");
    }

    /* ---- fixed-width <-> C strings ---- */
    {
        uint8_t fixed[10] = { 'A','C','M','E',' ',' ',' ',' ',' ',' ' };
        char c[16];
        CHECK(lr_fixed_to_cstr(fixed, 10, c, sizeof(c)).ok && strcmp(c,"ACME")==0,
              "fixed-width field -> C string strips padding");
        uint8_t f2[10];
        CHECK(lr_cstr_to_fixed("ACME", f2, 10).ok && memcmp(f2, fixed, 10)==0,
              "C string -> fixed-width re-pads exactly");
        uint8_t f3[4];
        CHECK(!lr_cstr_to_fixed("TOOLONG", f3, 4).ok,
              "overlong string into a short field is rejected");
    }

    /* ---- Fortran column-major <-> C row-major ---- */
    {
        /* 2x3 matrix [[1,2,3],[4,5,6]] stored column-major = 1,4,2,5,3,6 */
        uint8_t fortran[6] = { 1,4,2,5,3,6 };
        uint8_t c[6];
        CHECK(lr_transpose(fortran, c, 2, 3, 1, LR_ARR_COL_MAJOR).ok,
              "transpose Fortran -> C");
        uint8_t want[6] = { 1,2,3,4,5,6 };
        CHECK(memcmp(c, want, 6) == 0, "row-major order is 1,2,3,4,5,6");
        uint8_t back[6];
        lr_transpose(c, back, 2, 3, 1, LR_ARR_ROW_MAJOR);
        CHECK(memcmp(back, fortran, 6) == 0, "transpose round-trips");
        CHECK(lr_index_f_to_c(1,1,3) == 0 && lr_index_f_to_c(2,3,3) == 5,
              "Fortran 1-based (i,j) maps to the right C offset");
    }

    /* ---- the bridge registry answers honestly ---- */
    {
        bool lossless = false; const char *note = 0;
        CHECK(lr_bridge_exists(LR_NUM_PACKED_DECIMAL, LR_NUM_RATIONAL, &lossless, &note)
              && lossless, "COMP-3 -> rational: bridge exists and is LOSSLESS");
        CHECK(lr_bridge_exists(LR_NUM_PACKED_DECIMAL, LR_NUM_IEEE_DOUBLE, &lossless, &note)
              && !lossless, "COMP-3 -> double: bridge exists but is flagged LOSSY");
        printf("       lossy note: %s\n", note);
        CHECK(lr_bridge_exists(LR_NUM_RATIONAL, LR_NUM_RATIONAL, &lossless, &note)
              && lossless, "identity bridge is lossless");
        CHECK(!lr_bridge_exists((lr_numfmt_t)99, LR_NUM_RATIONAL, &lossless, &note),
              "unknown representation reports NO bridge (never guesses)");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
