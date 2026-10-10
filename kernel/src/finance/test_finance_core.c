/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_finance_core.c — triple ledger, payment rails and bridge records:
 * conservation, bounds, and the refusals that keep money from being minted. */
#include <stdio.h>
#include "triple_ledger.h"
#include "rails.h"
#include "crypto_bridge.h"

static int failures = 0;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            printf("  [PASS] %s\n", msg);                                                          \
        } else {                                                                                   \
            printf("  [FAIL] %s\n", msg);                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static triple_ledger_t tl;
static rail_processor_t rp;
static bridge_registry_t br;

#define U(x) SR_FROM_INT(x)

static void ledger_tests(void)
{
    printf("triple ledger:\n");
    triple_ledger_init(&tl);
    char longname[100];
    for (int i = 0; i < 99; i++) longname[i] = 'N';
    longname[99] = 0;
    uint32_t a = triple_ledger_create_account(&tl, 1, CAP_FINANCIAL, longname);
    uint32_t b = triple_ledger_create_account(&tl, 2, CAP_FINANCIAL, "bob");
    CHECK(tl.accounts[a].name[63] == 0, "a long account name is cut at 63 chars + NUL");
    CHECK(triple_ledger_post(&tl, a, LEDGER_FINANCIAL, U(100), SR_ONE, 0, U(100), 0, 0, "seed") ==
              0,
          "seed posting");
    CHECK(triple_ledger_post(&tl, a, (ledger_type_t) 7, U(1), SR_ONE, 0, U(1), 0, 0, "bad") == -1,
          "a ledger index outside the three ledgers is refused");

    CHECK(triple_ledger_transfer(&tl, a, b, CAP_FINANCIAL, U(10), SR_ONE, U(1), "x") == 0,
          "transfer 10");
    CHECK(tl.accounts[a].conventional_balance == U(90) &&
              tl.accounts[b].conventional_balance == U(10),
          "conventional balances move by exactly the amount (provenance/externality legs "
          "do not leak in)");
    CHECK(tl.accounts[a].balance[LEDGER_FINANCIAL] == U(90), "financial ledger balance 90");
    CHECK(triple_ledger_transfer(&tl, a, b, CAP_FINANCIAL, U(-5), SR_ONE, 0, "neg") == -1,
          "a negative transfer is refused");
    CHECK(triple_ledger_transfer(&tl, a, b, CAP_FINANCIAL, 0, SR_ONE, 0, "zero") == -1,
          "a zero transfer is refused");
    CHECK(triple_ledger_transfer(&tl, a, a, CAP_FINANCIAL, U(5), SR_ONE, 0, "self") == -1,
          "a transfer to the same account is refused");

    /* fill b to 254 entries: a 3-leg side no longer fits, so nothing posts */
    while (tl.accounts[b].num_entries < 254)
        triple_ledger_post(&tl, b, LEDGER_PROVENANCE, U(1), SR_ONE, 0, 0, 0, 0, "fill");
    uint32_t na = tl.accounts[a].num_entries;
    surplus_real_t ca = tl.accounts[a].conventional_balance;
    CHECK(triple_ledger_transfer(&tl, a, b, CAP_FINANCIAL, U(5), SR_ONE, 0, "full") == -1,
          "a transfer the receiver has no room for is refused");
    CHECK(tl.accounts[a].num_entries == na && tl.accounts[a].conventional_balance == ca,
          "... and the sender was not debited (all legs or none)");

    conventional_report_t rep;
    triple_ledger_export_conventional(&tl, &rep);
    CHECK(rep.trial_balance == U(100), "trial balance = the one-sided seed debit (100)");
    CHECK(rep.balance_sheet_assets == U(100), "balance-sheet assets = sum of account balances, "
                                              "not counted twice");

    uint64_t v = triple_ledger_issue_voucher(&tl, 1, 2, CAP_FINANCIAL, U(50), SR_ONE, "v");
    CHECK(v != 0, "voucher issued to entity 2");
    CHECK(triple_ledger_redeem_voucher(&tl, v, a) == -1,
          "a voucher cannot be redeemed into an account its holder does not own");
}

static void rail_tests(void)
{
    printf("payment rails:\n");
    rail_processor_init(&rp, RAIL_DRAGON);
    uint32_t c = rail_issue_card(&rp, 1, 1, 123, U(100), CAP_FINANCIAL, NET_VISA, "x");
    CHECK(rail_process_tx(&rp, TX_SALE, c, 9, U(-1000), NULL) == -1 &&
              rp.cards[c].reg_balance == U(100),
          "a negative sale is refused (it credited the card)");
    CHECK(rail_process_tx(&rp, TX_REFUND, c, 9, U(5000), NULL) == -1 &&
              rp.cards[c].reg_balance == U(100),
          "a refund above the card limit is refused (it minted money)");
    uint32_t first = rp.num_transactions;
    CHECK(rail_process_tx(&rp, TX_SALE, c, 9, U(10), NULL) == 0, "sale 10");
    CHECK(rail_process_tx(&rp, TX_SALE, c, 9, U(20), NULL) == 0, "sale 20");
    CHECK(rail_process_tx(&rp, TX_REFUND, c, 9, U(30), NULL) == 0 &&
              rp.cards[c].reg_balance == U(100),
          "a refund up to the limit is accepted");
    CHECK(rail_process_tx(&rp, TX_CAPTURE, c, 9, U(1), NULL) == -1,
          "an unimplemented transaction type is refused, not reported as success");
    rail_settle_batch(&rp);
    CHECK(rp.transactions[first].settled && rp.transactions[first + 1].settled &&
              !rp.transactions[first - 1].settled,
          "settlement settles the approved sales themselves (tx_id is 1-based)");
    CHECK(rail_process_voucher_tx(&rp, 999, 1, U(1)) == -1, "an unknown card id is refused");
    transaction_t tx = rp.transactions[first];
    CHECK(rail_bridge_to_conventional(&rp, &tx, (conventional_network_t) 50) == -1,
          "an out-of-range network index is refused");

    rail_processor_init(&rp, RAIL_PHOENIX);
    c = rail_issue_card(&rp, 1, 1, 123, U(1000), CAP_FINANCIAL, NET_VISA, "x");
    int ok = 0;
    for (int i = 0; i < 70; i++)
        if (rail_process_tx(&rp, TX_SALE, c, 9, U(1), NULL) == 0) ok++;
    CHECK(ok == 63, "the 64-slot settlement ring holds 63 approvals, then declines");
    CHECK(rp.cards[c].reg_balance == U(1000 - 63), "declined sales did not debit the card");
}

static void bridge_tests(void)
{
    printf("bridge records:\n");
    bridge_registry_init(&br);
    CHECK(bridge_create(&br, (chain_id_t) 99, CHAIN_ETHEREUM, BRIDGE_CROSS_CHAIN, U(10), 1, 2,
                        TOKEN_FUNGIBLE, LANG_SOLIDITY, "X", "") == 0xFFFFFFFFu,
          "an out-of-range chain id is refused");
    uint32_t id = bridge_create(&br, CHAIN_BITCOIN, CHAIN_ETHEREUM, BRIDGE_CROSS_CHAIN, U(10), 1, 2,
                                TOKEN_FUNGIBLE, LANG_SOLIDITY, "BTC", "");
    CHECK(id == 0, "bridge record created");
    CHECK(bridge_execute(&br, id) == 0, "executed once");
    CHECK(bridge_execute(&br, id) == -1, "a second execute is refused (volume counted once)");
    CHECK(bridge_confirm_dest(&br, id, 7) == -1,
          "the destination cannot complete before the source has its confirmations");
    uint32_t h = br.bridges[id].source_tx_hash;
    bridge_confirm_source(&br, id, h, 3);
    CHECK(bridge_confirm_dest(&br, id, 7) == -1, "3 confirmations are not enough for Bitcoin");
    bridge_confirm_source(&br, id, h, 6);
    CHECK(bridge_confirm_dest(&br, id, 7) == 0, "6 confirmations complete it");
}

int main(void)
{
    ledger_tests();
    rail_tests();
    bridge_tests();
    printf("%s (%d failures)\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
