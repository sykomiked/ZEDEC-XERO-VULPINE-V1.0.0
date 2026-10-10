/* test_rational_regress.c — regressions for the 2026-08-04 self-audit.
 * Every case here silently produced a WRONG answer with valid==true before
 * the fix. Do not delete: these are the audit findings, frozen. */
#include <stdio.h>
#include <string.h>
#include "rational.h"
static int F = 0;
#define CK(c, m)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            F++;                                                                                   \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static void audit_round_two(void)
{
    char b[96];
    bool ex;
    /* #5: invalids sort after valids, consistently */
    rat_t inv = rat_make(1, 0), three = rat_from_int(3);
    CK(rat_cmp(inv, three) == 1 && rat_cmp(three, inv) == -1,
       "invalid sorts AFTER every valid value");
    CK(rat_cmp(inv, inv) == 0, "two invalids compare equal");
    /* #6: digit-exact bounds */
    CK(rat_from_string("9223372036854775807").valid, "I64_MAX itself parses");
    CK(rat_from_string("1/9223372036854775807").valid,
       "denominator I64_MAX parses (digit-exact bound; red-team)");
    CK(!rat_from_string("1/9223372036854775808").valid, "denominator I64_MAX+1 refuses");
    CK(!rat_from_string("9223372036854775808").valid, "I64_MAX+1 refuses");
    CK(rat_from_string("0.1000000000000000000000").valid,
       "trailing zeros past 18 places stay exact and accepted");
    CK(!rat_from_string("0.0000000000000000001").valid,
       "a significant digit past 18 places refuses");
    /* #8: places cap is explicit — emitted width equals min(places,18) */
    rat_to_fixed(rat_from_string("1.5"), 25, b, sizeof b, &ex);
    CK(strlen(b) == 2 + 18, "places=25 renders exactly 18 decimals");
    /* #9: invalid + max==1 still terminates */
    b[0] = 0x7E;
    rat_to_fixed(inv, 2, b, 1, &ex);
    CK(b[0] == '\0', "invalid with max==1 NUL-terminates the buffer");
    /* #3: split shares are equal exact rationals summing back */
    rat_t shares[7];
    CK(rat_split(rat_from_string("100.37"), 7, shares), "split succeeds");
    rat_t sum = rat_zero();
    for (int i = 0; i < 7; i++) sum = rat_add(sum, shares[i]);
    CK(rat_eq(sum, rat_from_string("100.37")), "7 shares sum back EXACTLY");
}

int main(void)
{
    char b[96];
    /* the reported silent-corruption cases */
    rat_t x = rat_from_int(4836801);
    for (int i = 0; i < 18; i++) x = rat_div(x, rat_from_int(2));
    rat_to_string(x, b, sizeof b);
    printf("  4836801/2^18 -> %s\n", b);
    CK(!strcmp(b, "18.450931549072265625"), "4836801/2^18 renders exactly (was 0.0041874...)");

    rat_to_string(rat_make(9223372036854775807LL, 2), b, sizeof b);
    printf("  I64_MAX/2     -> %s\n", b);
    CK(!strcmp(b, "4611686018427387903.5"), "I64_MAX/2 renders exactly (was 922337203685477580.3)");

    rat_to_string(rat_div(rat_from_int(3000000000000000001LL), rat_from_int(4)), b, sizeof b);
    printf("  3e18+1 / 4    -> %s\n", b);
    CK(!strcmp(b, "750000000000000000.25"), "3e18/4 renders exactly");

    /* INT64_MIN no longer manufactures a bogus 'valid' */
    rat_t m = rat_from_int((-9223372036854775807LL) - 1);
    CK(!m.valid, "rat_from_int(INT64_MIN) is INVALID, matching rat_make");
    CK(!rat_abs(m).valid, "rat_abs of it stays invalid (no negative abs)");

    /* trailing garbage */
    CK(!rat_from_string("1.5x").valid, "\"1.5x\" rejected");
    CK(!rat_from_string("3/4junk").valid, "\"3/4junk\" rejected");
    CK(!rat_from_string("42abc").valid, "\"42abc\" rejected");
    CK(rat_from_string("1.5").valid, "\"1.5\" still accepted");
    CK(rat_from_string(" 3/4 ").valid, "\"3/4\" with spaces still accepted");
    rat_to_string(rat_from_string("0.1"), b, sizeof b);
    CK(!strcmp(b, "0.1"), "0.1 round-trips");

    /* the original headline still holds */
    rat_t s = rat_add(rat_from_string("0.1"), rat_from_string("0.2"));
    CK(rat_eq(s, rat_from_string("0.3")), "0.1 + 0.2 == 0.3 exactly");
    audit_round_two();
    printf("\n%s: %d failure(s)\n", F ? "*** FAILED ***" : "ALL PASS", F);
    return F ? 1 : 0;
}
