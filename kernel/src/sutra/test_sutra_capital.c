#include <stdio.h>
#include <string.h>
#include "sutra.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

static rational_t R(int64_t n, int64_t d) { rational_t r = {n, d}; return r; }

int main(void) {
    printf("=== sutra_capital_name ===\n");
    CHECK(strcmp(sutra_capital_name(CAP_FINANCIAL), "FINANCIAL") == 0, "CAP_FINANCIAL -> FINANCIAL");
    CHECK(strcmp(sutra_capital_name(CAP_KNOWLEDGE), "INTELLECTUAL") == 0, "CAP_KNOWLEDGE -> INTELLECTUAL (documented mapping)");
    CHECK(strcmp(sutra_capital_name(CAP_LIVING), "NATURAL") == 0, "CAP_LIVING -> NATURAL (documented mapping)");
    CHECK(strcmp(sutra_capital_name(CAP_BUILT), "TEMPORAL") == 0, "CAP_BUILT -> TEMPORAL (documented mapping)");
    CHECK(strcmp(sutra_capital_name(CAP_HUMAN), "RELATIONAL") == 0, "CAP_HUMAN -> RELATIONAL (documented mapping)");
    CHECK(strcmp(sutra_capital_name((capital_type_t)99), "UNKNOWN") == 0, "out-of-range capital -> UNKNOWN, no OOB read");

    printf("\n=== sutra_check_coverage ===\n");
    {
        static vino_ledger_t ledger; /* too large for the stack (65536 txn slots) */
        vino_init(&ledger, 1);
        vino_create_account(&ledger, "alice@vino", "Alice");
        vino_transfer(&ledger, "alice@vino", "alice@vino", 0, CAP_FINANCIAL, RAIL_VINO_NATIVE, "seed"); /* no-op, just to exercise account existence path if needed */
        /* Directly seed a balance via a deposit-style issue since vino_transfer needs both accounts to exist and sufficient funds -- create a funder. */
        vino_create_account(&ledger, "genesis@vino", "Genesis");
        /* Give genesis a starting balance by direct struct access (test-only shortcut). */
        vino_account_t *g = vino_get_account(&ledger, "genesis@vino");
        g->balance[CAP_FINANCIAL] = 1000;
        vino_transfer(&ledger, "genesis@vino", "alice@vino", 700, CAP_FINANCIAL, RAIL_VINO_NATIVE, "fund alice");

        CHECK(sutra_check_coverage(&ledger, "alice@vino", CAP_FINANCIAL, R(500, 1)) == true,
              "alice (balance 700) has coverage for 500/1");
        CHECK(sutra_check_coverage(&ledger, "alice@vino", CAP_FINANCIAL, R(700, 1)) == true,
              "alice has coverage for exactly her full balance 700/1");
        CHECK(sutra_check_coverage(&ledger, "alice@vino", CAP_FINANCIAL, R(701, 1)) == false,
              "alice does NOT have coverage for 701/1 (1 over balance)");
        CHECK(sutra_check_coverage(&ledger, "nonexistent@vino", CAP_FINANCIAL, R(1, 1)) == false,
              "nonexistent account never has coverage");
        CHECK(sutra_check_coverage(&ledger, "alice@vino", CAP_FINANCIAL, R(-5, 1)) == false,
              "negative required amount never has coverage (malformed input handled safely)");
        CHECK(sutra_check_coverage(&ledger, "alice@vino", CAP_FINANCIAL, R(1, 0)) == false,
              "zero denominator (malformed rational) never crashes, returns false");
        CHECK(sutra_check_coverage(&ledger, "alice@vino", CAP_FINANCIAL, R(699, 2)) == true,
              "fractional requirement 699/2=349.5 <= balance 700 -> true (non-integer den path)");
        CHECK(sutra_check_coverage(&ledger, "alice@vino", CAP_FINANCIAL, R(1401, 2)) == false,
              "fractional requirement 1401/2=700.5 > balance 700 -> false (non-integer den path)");
    }

    if (failures == 0) printf("\n=== ALL SUTRA CAPITAL TESTS PASSED ===\n");
    else printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
