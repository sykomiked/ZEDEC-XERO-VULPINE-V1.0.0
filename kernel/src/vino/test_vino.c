/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_vino.c — the Vino in-memory ledger: bounds, conservation, and that an
 * issue is recorded like every other ledger event. Each check is a property a
 * caller relies on, not "the code returned what the code returned". */
#include <stdio.h>
#include <string.h>
#include "vino.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static vino_ledger_t g_v; /* ~10 MB: too big for the stack */

int main(void)
{
    vino_ledger_t *v = &g_v;
    vino_init(v, 1);

    /* an over-long address/name/memo is truncated inside its field */
    char longs[300];
    memset(longs, 'A', sizeof longs - 1);
    longs[sizeof longs - 1] = 0;
    CHECK(vino_create_account(v, "alice", longs) == 0, "an over-long name is accepted");
    CHECK(strlen(v->balances[0].name) == VINO_NAME_LEN - 1,
          "...and truncated to its field, not copied past it");
    CHECK(vino_create_account(v, "bob", "Bob") == 1, "a second account is created");
    CHECK(vino_create_account(v, "bob", "Bob again") == -1,
          "a duplicate address is refused (it could never be looked up)");
    CHECK(vino_create_account(v, "", "nobody") == -1, "an empty address is refused");

    vino_account_t *a = vino_get_account(v, "alice");
    vino_account_t *b = vino_get_account(v, "bob");
    a->balance[CAP_FINANCIAL] = 1000;

    /* conservation */
    int32_t id = vino_transfer(v, "alice", "bob", 400, CAP_FINANCIAL, RAIL_VINO_NATIVE, longs);
    CHECK(id >= 0, "a covered transfer succeeds");
    CHECK(a->balance[CAP_FINANCIAL] == 600 && b->balance[CAP_FINANCIAL] == 400,
          "value moves and is conserved");
    CHECK(strlen(v->primary[id].memo) == sizeof v->primary[id].memo - 1,
          "an over-long memo is truncated inside the transaction");
    CHECK(vino_transfer(v, "alice", "bob", 601, CAP_FINANCIAL, RAIL_VINO_NATIVE, 0) == -1,
          "an uncovered transfer is refused");
    CHECK(vino_transfer(v, "alice", "bob", 1, (capital_type_t) 99, RAIL_VINO_NATIVE, 0) == -1,
          "an out-of-range capital type is refused, not indexed");

    /* a credit that would wrap destroys value: refused */
    b->balance[CAP_MATERIAL] = UINT64_MAX - 5;
    a->balance[CAP_MATERIAL] = 10;
    CHECK(vino_transfer(v, "alice", "bob", 10, CAP_MATERIAL, RAIL_VINO_NATIVE, 0) == -1,
          "a transfer whose credit would overflow is refused");
    CHECK(a->balance[CAP_MATERIAL] == 10 && b->balance[CAP_MATERIAL] == UINT64_MAX - 5,
          "...and leaves both balances untouched");

    /* an issue is recorded: counted, chained, audited */
    uint32_t n0 = v->num_txns;
    uint8_t head0[VINO_HASH_LEN];
    memcpy(head0, v->chain_head_hash, VINO_HASH_LEN);
    int32_t iid = vino_issue(v, "alice", ASSET_CBDC, 50, "genesis");
    CHECK(iid == (int32_t) n0 && v->num_txns == n0 + 1, "an issue is counted in the ledger");
    CHECK(memcmp(head0, v->chain_head_hash, VINO_HASH_LEN) != 0 &&
              memcmp(v->primary[iid].prev_hash, head0, VINO_HASH_LEN) == 0,
          "...and chained onto the audit hash chain");
    CHECK(v->audit[iid].type == TXN_ISSUE, "...and copied to the audit ledger");
    int32_t tid = vino_transfer(v, "alice", "bob", 1, CAP_FINANCIAL, RAIL_VINO_NATIVE, 0);
    CHECK(tid == iid + 1 && v->primary[iid].type == TXN_ISSUE,
          "the next transfer does not overwrite the issue record");
    CHECK(vino_issue(v, "alice", (asset_class_t) 77, 1, 0) == -1,
          "an out-of-range asset class is refused, not indexed");
    CHECK(vino_issue(v, "alice", ASSET_CBDC, 0x100000000ull, 0) == -1,
          "an issue that would truncate the 32-bit asset count is refused");
    CHECK(a->asset_balances[ASSET_CBDC] == 50, "...and leaves the count unchanged");

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
