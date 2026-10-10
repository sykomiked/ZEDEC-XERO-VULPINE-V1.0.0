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

    /* ===== SHA-256 audit chain ===== */
    {
        /* vino_hash is SHA-256 (FIPS 180-4 "abc" vector) */
        static const uint8_t abc_want[VINO_HASH_LEN] = {
            0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
            0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
            0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
        uint8_t d[VINO_HASH_LEN];
        vino_hash("abc", 3, d);
        CHECK(memcmp(d, abc_want, VINO_HASH_LEN) == 0, "vino_hash is SHA-256 (abc vector)");
    }
    for (int i = 0; i < 5; i++)
        vino_transfer(v, "alice", "bob", 1, CAP_FINANCIAL, RAIL_VINO_NATIVE, "chain");
    uint32_t bad = 77;
    CHECK(v->num_txns >= 7, "several entries on the chain");
    CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_OK, "an untouched chain verifies");
    int all_v2 = 1;
    for (uint32_t i = 0; i < v->num_txns; i++)
        if (v->primary[i].chain_ver != VINO_CHAIN_V2_SHA256) all_v2 = 0;
    CHECK(all_v2, "every written entry carries chain version 2 (SHA-256)");
    {
        uint8_t d[VINO_HASH_LEN];
        vino_entry_digest(&v->primary[2], d);
        CHECK(memcmp(d, v->primary[2].hash, VINO_HASH_LEN) == 0,
              "an entry's hash is the documented V2 digest of its fields");
    }

    /* tamper: change one entry's amount in the primary ledger */
    {
        vino_transaction_t save = v->primary[3];
        v->primary[3].amount += 1;
        CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_E_DIGEST && bad == 3,
              "tamper: a modified amount fails verification at that entry");
        v->primary[3] = save;
        CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_OK, "...and verifies again once restored");

        /* re-hashing the edited entry (and its audit copy) breaks the next link */
        vino_transaction_t save_audit = v->audit[3];
        v->primary[3].amount += 1;
        vino_entry_digest(&v->primary[3], v->primary[3].hash);
        CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_E_AUDIT && bad == 3,
              "tamper: a re-hashed entry no longer matches its audit copy");
        v->audit[3] = v->primary[3];
        CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_E_LINK && bad == 4,
              "tamper: re-hashing the edited entry and its copy breaks the next link");
        v->primary[3] = save;
        v->audit[3] = save_audit;

        /* the memo is covered, including bytes after its NUL */
        v->primary[3].memo[60] = 'X';
        CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_E_DIGEST && bad == 3,
              "tamper: a byte hidden after the memo NUL is detected");
        v->primary[3] = save;

        /* the capital form is covered */
        v->primary[3].capital = CAP_SPIRITUAL;
        CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_E_DIGEST,
              "tamper: a changed capital form is detected");
        v->primary[3] = save;

        /* the audit copy must match the primary */
        v->audit[3].to_addr[0] = 'm';
        CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_E_AUDIT && bad == 3,
              "tamper: an edited audit copy is detected");
        v->audit[3] = save;

        /* a dropped last entry leaves the head pointing past the chain */
        v->num_txns--;
        CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_E_HEAD && bad == v->num_txns,
              "tamper: truncating the ledger is detected at the head");
        v->num_txns++;

        /* a legacy (pre-SHA-256) entry is never reported as verified */
        v->primary[0].chain_ver = 0;
        CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_E_LEGACY && bad == 0,
              "an unversioned (FNV-era) entry is reported as legacy, not verified");
        v->primary[0].chain_ver = VINO_CHAIN_V1_FNV1A;
        CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_E_LEGACY,
              "...and so is an entry tagged V1 (FNV-1a)");
        v->primary[0].chain_ver = VINO_CHAIN_V2_SHA256;
        CHECK(vino_chain_verify(v, &bad) == VINO_CHAIN_OK, "the restored chain verifies");
        CHECK(vino_chain_verify(0, &bad) == VINO_CHAIN_E_ARG, "a NULL ledger is refused");
    }

    /* vino_get_balance bounds */
    {
        uint64_t out = 0;
        CHECK(vino_get_balance(v, "alice", (capital_type_t) 99, &out) == -1,
              "vino_get_balance refuses an out-of-range capital type");
        CHECK(vino_get_balance(v, "alice", CAP_FINANCIAL, &out) == 0 &&
                  out == a->balance[CAP_FINANCIAL],
              "...and reads an in-range one");
    }

    /* ===== conservation: transfers move value, never make or destroy it =====
     * sum(balances after) == sum(before) + minted - burned, per form; vino
     * transfers mint and burn nothing, so the per-form sums are constant
     * through accepted AND refused transfers. vino_issue mints asset counts
     * only, which are checked separately. */
    {
        uint64_t before[CAP_MAX], after[CAP_MAX];
        int conserved = 1;
#define SUM_FORMS(dst)                                                                             \
    for (uint32_t f_ = 0; f_ < CAP_MAX; f_++) {                                                    \
        dst[f_] = 0;                                                                               \
        for (uint32_t i_ = 0; i_ < v->num_accounts; i_++) dst[f_] += v->balances[i_].balance[f_];  \
    }
        CHECK(vino_create_account(v, "carol", "Carol") >= 0, "a third account");
        b->balance[CAP_MATERIAL] = 0; /* the near-UINT64_MAX fixture above would wrap the sum */
        a->balance[CAP_HUMAN] = 500;
        SUM_FORMS(before);
        static const struct {
            const char *from, *to;
            uint64_t amt;
            capital_type_t cap;
        } ops[] = {{"alice", "bob", 100, CAP_HUMAN},   {"bob", "carol", 40, CAP_HUMAN},
                   {"carol", "alice", 41, CAP_HUMAN},  {"alice", "alice", 5, CAP_HUMAN},
                   {"bob", "alice", 1, CAP_FINANCIAL}, {"carol", "bob", 0, CAP_HUMAN},
                   {"alice", "nobody", 1, CAP_HUMAN},  {"bob", "carol", 61, CAP_HUMAN}};
        for (uint32_t k = 0; k < sizeof ops / sizeof ops[0]; k++) {
            (void) vino_transfer(v, ops[k].from, ops[k].to, ops[k].amt, ops[k].cap,
                                 RAIL_VINO_NATIVE, 0);
            SUM_FORMS(after);
            for (uint32_t f = 0; f < CAP_MAX; f++)
                if (after[f] != before[f]) conserved = 0;
        }
#undef SUM_FORMS
        CHECK(conserved, "conservation: per-form balance sums are unchanged after every transfer");
        uint32_t na = v->balances[0].asset_balances[ASSET_CBDC];
        CHECK(vino_issue(v, "alice", ASSET_CBDC, 7, "mint") >= 0 &&
                  v->balances[0].asset_balances[ASSET_CBDC] == na + 7,
              "conservation: an issue adds exactly the minted amount");
        CHECK(vino_chain_verify(v, 0) == VINO_CHAIN_OK, "...and the chain still verifies");

        /* the volume counter is checked before anything moves */
        uint64_t ab = a->balance[CAP_SOCIAL] = 10, bb = b->balance[CAP_SOCIAL];
        v->total_volume[CAP_SOCIAL] = UINT64_MAX - 3;
        uint32_t nt = v->num_txns;
        CHECK(vino_transfer(v, "alice", "bob", 5, CAP_SOCIAL, RAIL_VINO_NATIVE, 0) == -1 &&
                  a->balance[CAP_SOCIAL] == ab && b->balance[CAP_SOCIAL] == bb && v->num_txns == nt,
              "a transfer that would wrap the volume counter is refused with no state change");
        v->total_volume[CAP_SOCIAL] = 0;
    }

    /* canonical names: vino's capital_type_t is the zcap order */
    CHECK(strcmp(vino_capital_name(CAP_MANUFACTURED), "MANUFACTURED") == 0 &&
              strcmp(vino_capital_name(CAP_HUMAN), "HUMAN") == 0 &&
              strcmp(vino_capital_name(CAP_SYSTEM), "SYSTEM") == 0 &&
              strcmp(vino_capital_name(CAP_MAX), "UNKNOWN") == 0,
          "capital names are the canonical zcap names");

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
