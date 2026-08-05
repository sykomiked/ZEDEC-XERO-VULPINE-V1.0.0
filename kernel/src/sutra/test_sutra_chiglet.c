#include <stdio.h>
#include <string.h>
#include "sutra.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

int main(void) {
    printf("=== Basic flagship sentence ===\n");
    {
        chiglet_result_t r = sutra_chiglet_translate("Send 500 rupees from Alice to Bob");
        CHECK(r.matched == true, "flagship sentence matches");
        CHECK(r.amount.num == 500 && r.amount.den == 1, "amount extracted as 500/1");
        CHECK(strcmp(r.from_account, "alice@vino") == 0, "from_account is alice@vino (lowercased + suffixed)");
        CHECK(strcmp(r.to_account, "bob@vino") == 0, "to_account is bob@vino (lowercased + suffixed)");
        CHECK(r.rail == RAIL_UPI, "rail inferred as RAIL_UPI from 'rupees'");
        CHECK(r.capital == CAP_FINANCIAL, "capital is CAP_FINANCIAL");
    }

    printf("\n=== Asymmetric name lengths (the bug I caught and fixed) ===\n");
    {
        chiglet_result_t r = sutra_chiglet_translate("Send 10 dollars from Al to Bartholomew");
        CHECK(r.matched == true, "asymmetric-length-name sentence matches");
        CHECK(strcmp(r.from_account, "al@vino") == 0, "short from-name 'Al' not truncated/corrupted");
        CHECK(strcmp(r.to_account, "bartholomew@vino") == 0,
              "long to-name 'Bartholomew' is NOT truncated to match the shorter from-name's length (the original generated bug)");
        CHECK(r.rail == RAIL_FEDWIRE, "rail inferred as RAIL_FEDWIRE from 'dollars'");
    }
    {
        /* the reverse asymmetry: short name second */
        chiglet_result_t r = sutra_chiglet_translate("Send 20 euros from Bartholomew to Al");
        CHECK(strcmp(r.from_account, "bartholomew@vino") == 0, "long from-name not truncated");
        CHECK(strcmp(r.to_account, "al@vino") == 0, "short to-name correct");
        CHECK(r.rail == RAIL_SEPA, "rail inferred as RAIL_SEPA from 'euros'");
    }

    printf("\n=== Already-qualified account (has '@') is not double-suffixed ===\n");
    {
        chiglet_result_t r = sutra_chiglet_translate("Send 5 pix from alice@othernet to bob@othernet");
        CHECK(strcmp(r.from_account, "alice@othernet") == 0, "already-qualified from_account left untouched (no double @vino)");
        CHECK(strcmp(r.to_account, "bob@othernet") == 0, "already-qualified to_account left untouched");
        CHECK(r.rail == RAIL_PIX, "rail inferred as RAIL_PIX");
    }

    printf("\n=== No match cases ===\n");
    {
        chiglet_result_t r1 = sutra_chiglet_translate("There is no amount here from Alice to Bob");
        CHECK(r1.matched == false, "sentence with no numeric amount does not match");
        chiglet_result_t r2 = sutra_chiglet_translate("Send 500 rupees to nowhere");
        CHECK(r2.matched == false, "sentence missing the from/to pattern does not match");
        chiglet_result_t r3 = sutra_chiglet_translate("");
        CHECK(r3.matched == false, "empty sentence does not match, does not crash");
    }

    if (failures == 0) printf("\n=== ALL SUTRA CHIGLET TESTS PASSED ===\n");
    else printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
