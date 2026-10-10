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
    if ((uint32_t) capital >= CAP_MAX) return false; /* out-of-range capital: no balance to read */
    uint64_t balance = acc->balance[(uint32_t)capital];
    /* balance >= num/den, decided exactly. The old non-integer path compared
     * doubles, which round above 2^53 and could approve a balance that is
     * short of the requirement (e.g. 2^53 vs 2^53 + 1/2). */
    if (required_amount.den <= 0 || required_amount.num < 0) return false; /* malformed */
    if (required_amount.num == 0) return true;
    uint64_t num = (uint64_t) required_amount.num, den = (uint64_t) required_amount.den;
    uint64_t q = num / den, r = num % den;
    return balance > q || (balance == q && r == 0);
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
