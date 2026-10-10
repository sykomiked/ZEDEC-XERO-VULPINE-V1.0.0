/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_rails_canon.c — the three rail numerics (555 / 777 / 888) and their
 * jurisdictions (NCR / NRE / PNS) are one set across every module. Each
 * module's macro is defined from pay_rails.h; the static asserts below fail
 * the build if anyone re-literalises one, and the runtime checks cover the
 * functions that map a rail to its numeric. */
#include <stdio.h>
#include <string.h>
#include "pay_rails.h"
#include "pay_ledger.h"
#include "../vino_stores/vino_stores.h"
#include "../iso20022/iso20022.h"
#include "../cbank/cb_vss.h"
#include "../cardnet/cardnet.h"
#include "../devmesh/devmesh.h"
#include "../evolve/evo.h"
#include "../finance/capital_forms.h"

#define SAME_RAILS(D, C, E, tag)                                                                   \
    _Static_assert((D) == 555u && (C) == 777u && (E) == 888u, tag " rails are 555/777/888");       \
    _Static_assert((D) == ZXV_RAIL_CODE_DEBIT && (C) == ZXV_RAIL_CODE_CREDIT &&                    \
                       (E) == ZXV_RAIL_CODE_EQUITY,                                                \
                   tag " rails are the canonical set")

SAME_RAILS(PAY_RAIL_DEBIT_CODE, PAY_RAIL_CREDIT_CODE, PAY_RAIL_EQUITY_CODE, "pay");
SAME_RAILS(VINO_ISO_DEBIT, VINO_ISO_CREDIT, VINO_ISO_EQUITY, "vino_stores");
SAME_RAILS(ISO_CCY_DEBIT, ISO_CCY_CREDIT, ISO_CCY_EQUITY, "iso20022");
SAME_RAILS(CB_RAIL_DEBIT, CB_RAIL_CREDIT, CB_RAIL_EQUITY, "cbank");
SAME_RAILS(CN_RAIL_DEBIT, CN_RAIL_CREDIT, CN_RAIL_EQUITY, "cardnet");
SAME_RAILS(DM_RAIL_DEBIT, DM_RAIL_CREDIT, DM_RAIL_EQUITY, "devmesh");
SAME_RAILS(EVO_RAIL_DEBIT, EVO_RAIL_CREDIT, EVO_RAIL_EQUITY, "evolve");
SAME_RAILS(RAIL_FINANCIAL, RAIL_PROVENANCE, RAIL_EXTERNALITY, "finance");
_Static_assert(CB_VSS_RESOLUTION_CLASS == 811u && !ZXV_RAIL_CODE_IS_RAIL(CB_VSS_RESOLUTION_CLASS),
               "811 is a procedure designator, not a rail");

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

int main(void)
{
    CHECK(pay_rail_code(PAY_RAIL_DEBIT) == ZXV_RAIL_CODE_DEBIT &&
              pay_rail_code(PAY_RAIL_CREDIT) == ZXV_RAIL_CODE_CREDIT &&
              pay_rail_code(PAY_RAIL_EQUITY) == ZXV_RAIL_CODE_EQUITY,
          "pay_rail_code maps DEBIT/CREDIT/EQUITY to 555/777/888");
    CHECK(strcmp(pay_rail_juris(PAY_RAIL_DEBIT), "NCR") == 0 &&
              strcmp(pay_rail_juris(PAY_RAIL_CREDIT), "NRE") == 0 &&
              strcmp(pay_rail_juris(PAY_RAIL_EQUITY), "PNS") == 0 &&
              strcmp(VINO_JURIS_DEBIT, ZXV_RAIL_CODE_DEBIT_JURIS) == 0 &&
              strcmp(VINO_JURIS_CREDIT, ZXV_RAIL_CODE_CREDIT_JURIS) == 0 &&
              strcmp(VINO_JURIS_EQUITY, ZXV_RAIL_CODE_EQUITY_JURIS) == 0,
          "pay and vino_stores jurisdictions are NCR/NRE/PNS");
    CHECK(iso20022_rail_ccy(ISO_RAIL_DEBIT) == ZXV_RAIL_CODE_DEBIT &&
              iso20022_rail_ccy(ISO_RAIL_CREDIT) == ZXV_RAIL_CODE_CREDIT &&
              iso20022_rail_ccy(ISO_RAIL_EQUITY) == ZXV_RAIL_CODE_EQUITY,
          "iso20022_rail_ccy maps the same three rails");
    CHECK(strstr(iso20022_ccy_caveat(ZXV_RAIL_CODE_DEBIT), "NCR") &&
              strstr(iso20022_ccy_caveat(ZXV_RAIL_CODE_CREDIT), "NRE") &&
              strstr(iso20022_ccy_caveat(ZXV_RAIL_CODE_EQUITY), "PNS"),
          "iso20022 caveats name each rail's jurisdiction");
    CHECK(cb_vss_rail_valid(ZXV_RAIL_CODE_DEBIT) && cb_vss_rail_valid(ZXV_RAIL_CODE_CREDIT) &&
              cb_vss_rail_valid(ZXV_RAIL_CODE_EQUITY) && !cb_vss_rail_valid(811u) &&
              !cb_vss_rail_valid(846u) && !cb_vss_rail_valid(847u),
          "cbank accepts exactly 555/777/888 (811, 846, 847 are not rails)");
    CHECK(capital_primary_rail(CAPITAL_FINANCIAL) == ZXV_RAIL_CODE_DEBIT &&
              capital_primary_rail(CAPITAL_KNOWLEDGE) == ZXV_RAIL_CODE_CREDIT,
          "finance form -> rail uses the canonical numerics");
    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
