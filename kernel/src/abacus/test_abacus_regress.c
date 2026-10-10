/* test_abacus_regress.c — regressions for the 2026-08-04 self-audit.
 * Clearing used to net across capitals, relabel every transfer to capital 0,
 * and destroy the book when it could not complete — forgiving real debt. */
#include <stdio.h>
#include "abacus.h"
static int F = 0;
#define CK(c, m)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            F++;                                                                                   \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)
static void mk(abacus_t *a, uint32_t n)
{
    smaug_init(a);
    for (uint32_t i = 0; i < n; i++) smaug_add_member(a, "m");
}

int main(void)
{
    /* ---- capitals must never net against each other ---- */
    {
        abacus_t a;
        mk(&a, 3);
        smaug_owe(&a, 0, 1, rat_from_int(100), 1); /* A owes B 100 of capital 1 */
        smaug_owe(&a, 1, 0, rat_from_int(100), 2); /* B owes A 100 of capital 2 */
        smaug_clear(&a);
        uint32_t t = smaug_transfer_count(&a);
        printf("  two capitals, mutually opposite: %u transfer(s) remain\n", t);
        CK(t == 2, "opposite debts in DIFFERENT capitals do NOT cancel");
        bool c1 = false, c2 = false;
        for (uint32_t i = 0; i < AB_MAX_OBLIGATIONS; i++) {
            /* scan via public count only; check capitals survived */
        }
        (void) c1;
        (void) c2;
    }
    /* ---- same capital still nets ---- */
    {
        abacus_t a;
        mk(&a, 3);
        smaug_owe(&a, 0, 1, rat_from_int(100), 1);
        smaug_owe(&a, 1, 0, rat_from_int(100), 1);
        smaug_clear(&a);
        CK(smaug_transfer_count(&a) == 0, "opposite debts in the SAME capital cancel to zero");
    }
    /* ---- capital label is preserved, not rewritten to 0 ---- */
    {
        abacus_t a;
        mk(&a, 3);
        smaug_owe(&a, 0, 1, rat_from_int(50), 7);
        smaug_owe(&a, 1, 2, rat_from_int(50), 7);
        smaug_clear(&a);
        bool all7 = true;
        uint32_t n = 0;
        for (uint32_t i = 0; i < AB_MAX_OBLIGATIONS; i++) {
            const ab_obligation_t *o = &a.obl[i];
            if (!o->active) continue;
            n++;
            if (o->capital != 7) all7 = false;
        }
        printf("  capital-7 chain cleared to %u transfer(s)\n", n);
        CK(n == 1 && all7, "the surviving transfer keeps capital 7 (not relabelled 0)");
    }
    /* ---- an uncleavable book is left INTACT, not destroyed ---- */
    {
        abacus_t a;
        mk(&a, 3);
        /* amounts near the rational limit so netting overflows */
        rat_t huge = rat_from_int(9223372036854775807LL);
        smaug_owe(&a, 0, 2, huge, 0);
        smaug_owe(&a, 1, 2, huge, 0); /* net[2] = I64_MAX + I64_MAX -> overflow */
        uint32_t before = smaug_transfer_count(&a);
        rat_t r = smaug_clear(&a);
        uint32_t after = smaug_transfer_count(&a);
        printf("  uncleavable book: %u transfers before, %u after; failed=%d result=%s\n", before,
               after, (int) a.clear_failed, r.valid ? "valid" : "invalid");
        CK(a.clear_failed, "failure is reported EXPLICITLY, not inferred from the return");
        CK(before == after && after == 2,
           "clearing failed -> THE BOOK IS LEFT EXACTLY AS IT WAS (no debt forgiven)");
    }
    /* a successful clear whose savings figure overflows must NOT look like failure */
    {
        abacus_t a;
        mk(&a, 3);
        smaug_owe(&a, 0, 1, rat_make(9223372036854775807LL, 7), 0);
        smaug_owe(&a, 1, 2, rat_make(9223372036854775807LL, 7), 0);
        rat_t r = smaug_clear(&a);
        printf("  big-but-clearable book: failed=%d result=%s\n", (int) a.clear_failed,
               r.valid ? "valid" : "invalid");
        CK(!a.clear_failed, "a clean run is NOT reported as failed even if savings overflow");
    }
    printf("\n%s: %d failure(s)\n", F ? "*** FAILED ***" : "ALL PASS", F);
    return F ? 1 : 0;
}
