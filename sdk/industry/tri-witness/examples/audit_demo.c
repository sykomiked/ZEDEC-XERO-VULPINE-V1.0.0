/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* audit_demo.c — an auditor replays two clearing cycles of a Sutra-Clear
 * house from its exports alone: the signed record chain and the ledger
 * journal. The witness runs in its own context with a read-only
 * capability. It first finds the exports consistent, then finds a copy of
 * the journal in which one posting was quietly changed, and lists each
 * discrepancy with the exact record and bytes.
 *
 * The house, the banks and their codes are invented for the example. The
 * kits are designed to support PAPSS and SWIFT ISO 20022 formats; they are
 * not connected to PAPSS, SWIFT or any payment system. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zsclear.h"
#include "ztwit.h"

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t rc_ = (x);                                                                     \
        if (rc_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s\n", #x, ztw_status_name(rc_));                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static const char *k_bank[3] = {"AAAANGLA", "BBBBGHAC", "M0042"};

static zsc_instr pay(const char *e2e, int64_t minor, int d, int c) {
    zsc_instr in;
    memset(&in, 0, sizeof in);
    snprintf(in.msg_id, sizeof in.msg_id, "MSG-%s", e2e);
    snprintf(in.cre_dt_tm, sizeof in.cre_dt_tm, "2026-10-12T08:30:00Z");
    snprintf(in.e2e, sizeof in.e2e, "%s", e2e);
    memcpy(in.ccy, "NGN", 4);
    in.amount = minor;
    snprintf(in.dbtr_agt, sizeof in.dbtr_agt, "%s", k_bank[d]);
    snprintf(in.cdtr_agt, sizeof in.cdtr_agt, "%s", k_bank[c]);
    snprintf(in.dbtr_nm, sizeof in.dbtr_nm, "Customer of %s", k_bank[d]);
    snprintf(in.dbtr_acct, sizeof in.dbtr_acct, "%s-001", k_bank[d]);
    snprintf(in.cdtr_nm, sizeof in.cdtr_nm, "Customer of %s", k_bank[c]);
    snprintf(in.cdtr_acct, sizeof in.cdtr_acct, "%s-002", k_bank[c]);
    return in;
}

static int print_findings(const ztw_witness *w) {
    uint32_t n = 0;
    if (ztw_finding_count(w, &n) != ZXV_OK) return 1;
    for (uint32_t i = 0; i < n; i++) {
        ztw_finding f;
        if (ztw_finding_at(w, i, &f) != ZXV_OK) return 1;
        char where[48];
        if (f.record == UINT32_MAX) snprintf(where, sizeof where, "the journal");
        else snprintf(where, sizeof where, "cycle %u, record %u", f.cycle_no, f.record);
        printf("  Forbidden %-12s %s, %s: %s", ztw_finding_name(f.code), where,
               f.field[0] ? f.field : "-", f.detail);
        if (f.expected != f.found)
            printf(" (expected %lld, found %lld)", (long long) f.expected, (long long) f.found);
        printf("\n");
    }
    return 0;
}

int main(void) {
    /* ---- the house's side ---- */
    zxv_ctx *hctx = NULL, *wctx = NULL;
    zxv_cap *hcap = NULL, *wcap = NULL;
    zind_signer *house_key = NULL, *op = NULL, *auditor = NULL;
    zsc_house *h = NULL;
    uint8_t seed[32] = {7}, oseed[32] = {8}, aseed[32] = {9}, msg[256], sig[ZIND_SIG_BYTES];
    uint8_t *chain = NULL, *journal = NULL;
    size_t cl = 0, jl = 0, len = 0;
    uint32_t issuer = 0;
    zsc_cycle_info info;
    ztw_witness *w = NULL;
    ztw_summary sum;
    int32_t st;

    TRY(zxv_ctx_create(&hctx));
    TRY(zxv_cap_create(hctx, 1, NULL, ZXV_CAP_ALL, &hcap));
    TRY(zind_signer_from_seed(hcap, seed, &house_key));
    TRY(zind_signer_from_seed(hcap, oseed, &op));
    TRY(zsc_house_create(hcap, "SCLR", house_key, NULL, &h));
    TRY(zsc_currency_add(h, "NGN"));
    for (int p = 0; p < 3; p++) TRY(zsc_participant_add(h, k_bank[p]));
    TRY(zsc_operator_add(h, zind_signer_pk(op)));
    TRY(zxv_ledger_open_account_cap(hcap, "central-issuer", "NGN", ZXV_ACCT_ISSUER, &issuer));
    for (int p = 0; p < 3; p++) {
        uint32_t a = 0;
        char ref[48];
        TRY(zsc_account(h, ZSC_ACCT_PARTICIPANT, k_bank[p], "NGN", &a));
        snprintf(ref, sizeof ref, "prefund-%d", p);
        TRY(zxv_ledger_post_cap(hcap, issuer, a, 100000000, "NGN", ref, NULL));
    }
    /* cycle 1: three payments and a conflict decided by the operator */
    TRY(zsc_cycle_open(h, "2026-10-12-AM", "2026-10-12T09:00:00Z"));
    {
        zsc_instr a = pay("INV-1", 1250000, 0, 1), b = pay("INV-2", 830050, 1, 2),
                  c = pay("INV-3", 99999, 2, 0);
        TRY(zsc_submit(h, &a));
        TRY(zsc_submit(h, &b));
        TRY(zsc_submit(h, &c));
        b.amount = 803050;
        st = zsc_submit(h, &b);
        if (st != ZIND_E_HELD) return 1;
    }
    TRY(zsc_decision_message(h, "INV-2", 2, msg, sizeof msg, &len));
    TRY(zind_sign(hcap, op, msg, len, "sutra-clear/operator", sig));
    TRY(zsc_resolve(h, zind_signer_pk(op), "INV-2", 2, sig));
    TRY(zsc_cycle_close(h, &info));
    /* cycle 2: the fee carry continues */
    TRY(zsc_cycle_open(h, "2026-10-12-PM", "2026-10-12T15:00:00Z"));
    {
        zsc_instr a = pay("INV-4", 777777, 1, 0), b = pay("INV-5", 4321, 0, 2);
        TRY(zsc_submit(h, &a));
        TRY(zsc_submit(h, &b));
    }
    TRY(zsc_cycle_close(h, &info));
    (void) zind_chain_export(zsc_records(h), NULL, 0, &cl);
    (void) zsc_journal_export(hcap, NULL, 0, &jl);
    chain = (uint8_t *) malloc(cl);
    journal = (uint8_t *) malloc(jl);
    if (!chain || !journal) return 1;
    TRY(zind_chain_export(zsc_records(h), chain, cl, &cl));
    TRY(zsc_journal_export(hcap, journal, jl, &jl));
    printf("house SCLR closed 2 cycles; it hands the auditor a %zu-byte record chain and a "
           "%zu-byte journal\n",
           cl, jl);

    /* ---- the auditor's side: another context, a read-only cap ---- */
    TRY(zxv_ctx_create(&wctx));
    TRY(zxv_cap_create(wctx, 2, NULL, ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN, &wcap));
    TRY(zind_signer_from_seed(wcap, aseed, &auditor));
    st = ztw_create(hcap, auditor, &w);
    printf("a witness on the house's own cap: %s (it can post)\n", ztw_status_name(st));
    TRY(ztw_create(wcap, auditor, &w));
    TRY(ztw_trust_house(w, zind_signer_pk(house_key)));
    TRY(ztw_trust_operator(w, zind_signer_pk(op)));
    st = ztw_verify(w, chain, cl, journal, jl, &sum);
    printf("replay: %s; %u records, %u cycles, %u instructions re-netted, %u postings matched, "
           "%llu of %llu journal postings covered, %u findings\n",
           ztw_status_name(st), sum.records, sum.cycles, sum.instructions, sum.postings,
           (unsigned long long) sum.covered_postings, (unsigned long long) sum.journal_postings,
           sum.findings);
    if (st != ZXV_OK) return 1;
    {
        const uint8_t *trust[1] = {zind_signer_pk(auditor)};
        uint32_t n = 0;
        TRY(zind_chain_count(ztw_report(w), &n));
        TRY(zind_chain_verify(wcap, ztw_report(w), trust, 1, NULL));
        printf("signed report: %u record (the summary), verifies under the auditor's key\n", n);
    }
    /* someone edits one posting in the journal copy: the last one, a fee
     * share of cycle 2. A posting ends with u64 amount, u8 length, ref. */
    {
        size_t i = jl;
        while (i > 16 && memcmp(journal + i - 7, "SCLR/2/", 7) != 0) i--;
        journal[i - 7 - 1 - 8] ^= 0x01; /* the low byte of its amount */
    }
    st = ztw_verify(w, chain, cl, journal, jl, &sum);
    printf("replay of the edited journal: %s, %u findings:\n", ztw_status_name(st), sum.findings);
    if (st != ZTW_E_DISCREPANCY || print_findings(w)) return 1;
    {
        uint32_t n = 0;
        TRY(zind_chain_count(ztw_report(w), &n));
        printf("signed report: %u records (one Forbidden finding each, then the summary)\n", n);
    }
    ztw_destroy(w);
    zind_signer_destroy(auditor);
    zxv_cap_destroy(wcap);
    zxv_ctx_destroy(wctx);
    free(chain);
    free(journal);
    zsc_house_destroy(h);
    zind_signer_destroy(house_key);
    zind_signer_destroy(op);
    zxv_cap_destroy(hcap);
    zxv_ctx_destroy(hctx);
    return 0;
}
