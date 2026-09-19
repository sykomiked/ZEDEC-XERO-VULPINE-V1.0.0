/* sutra_capital.c — SUTRA capital name + coverage check.
 * Delegates to vino.h's real capital_type_t and ledger.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "sutra.h"

const char *sutra_capital_name(sutra_capital_t c) {
    return vino_capital_name((capital_type_t)c);
}

bool sutra_check_coverage(vino_ledger_t *ledger, const char *account_addr,
                           sutra_capital_t capital, rational_t required_amount) {
    if (!ledger || !account_addr) return false;
    vino_account_t *acc = vino_get_account(ledger, account_addr);
    if (!acc) return false;
    uint64_t balance = acc->balance[(uint32_t)capital];
    /* Compare balance (uint64) against required_amount (rational).
     * required_amount.den is always >= 1 (normalized); treat as
     * integer comparison when den==1, otherwise approximate. */
    if (required_amount.den == 1) {
        return balance >= (uint64_t)required_amount.num;
    }
    /* For non-integer rationals, use the double magnitude for comparison */
    double needed = (double)required_amount.num / (double)required_amount.den;
    return (double)balance >= needed;
}

/* ---- DECLARATION -----------------------------------------------------------

 * REQUIRES(vino_ledger_ready) measured from sutra_capital.o's `nm -u` =
 * {vino_capital_name, vino_get_account}. Capital coverage is a ledger query.
 */
#include "zxv_decl.h"
ZXV_DECLARE(sutra_capital,
    ZXV_PROVIDES(sutra_capital_ready),
    ZXV_REQUIRES(vino_ledger_ready),
    ZXV_NO_BRINGUP);
