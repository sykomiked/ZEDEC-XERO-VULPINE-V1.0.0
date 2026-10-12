/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* clearing_demo.c — one clearing cycle at a Sutra-Clear house with four
 * participants in two currencies. Payments arrive as pacs.008 messages and
 * as a Sutra treaty; one end-to-end id arrives twice with different
 * amounts and is held as BOTH until a (simulated) operator signs a
 * decision. The cycle nets per currency, charges the 0.08889% assurance
 * fee, settles through the hub, renders ISO 20022 and signs the record.
 *
 * The house, the banks and their codes are invented for the example. The
 * kit is designed to support PAPSS and SWIFT ISO 20022 formats; it is not
 * connected to PAPSS, SWIFT or any payment system. */
#include <stdio.h>
#include <string.h>

#include "zsclear.h"

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t rc_ = (x);                                                                     \
        if (rc_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s\n", #x, zsc_status_name(rc_));                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static const char *k_bank[4] = {"AAAANGLA", "BBBBGHAC", "CCCCNGLA", "M0042"};

static zsc_instr pay(const char *e2e, const char *ccy, int64_t minor, int d, int c) {
    zsc_instr in;
    memset(&in, 0, sizeof in);
    snprintf(in.msg_id, sizeof in.msg_id, "MSG-%s", e2e);
    snprintf(in.cre_dt_tm, sizeof in.cre_dt_tm, "2026-10-12T08:30:00Z");
    snprintf(in.e2e, sizeof in.e2e, "%s", e2e);
    snprintf(in.ccy, sizeof in.ccy, "%s", ccy);
    in.amount = minor;
    snprintf(in.dbtr_agt, sizeof in.dbtr_agt, "%s", k_bank[d]);
    snprintf(in.cdtr_agt, sizeof in.cdtr_agt, "%s", k_bank[c]);
    snprintf(in.dbtr_nm, sizeof in.dbtr_nm, "Customer of %s", k_bank[d]);
    snprintf(in.dbtr_acct, sizeof in.dbtr_acct, "%s-001", k_bank[d]);
    snprintf(in.cdtr_nm, sizeof in.cdtr_nm, "Customer of %s", k_bank[c]);
    snprintf(in.cdtr_acct, sizeof in.cdtr_acct, "%s-002", k_bank[c]);
    return in;
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_hexlog *qlog = NULL;
    zind_signer *house_key = NULL, *op = NULL;
    zsc_house *h = NULL;
    uint8_t seed[32] = {7}, oseed[32] = {8}, msg[256], sig[ZIND_SIG_BYTES];
    uint32_t issuer[2], n = 0, q = 0;
    static const char *ccy[2] = {"NGN", "GHS"};
    char xml[8192];
    size_t len = 0;
    zsc_cycle_info info;
    zsc_batch b;
    int32_t st;

    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    TRY(zxv_hexlog_create(cap, 64, &qlog));
    TRY(zind_signer_from_seed(cap, seed, &house_key));
    TRY(zind_signer_from_seed(cap, oseed, &op));
    TRY(zsc_house_create(cap, "SCLR", house_key, qlog, &h));
    for (int c = 0; c < 2; c++) TRY(zsc_currency_add(h, ccy[c]));
    for (int p = 0; p < 4; p++) TRY(zsc_participant_add(h, k_bank[p]));
    TRY(zsc_operator_add(h, zind_signer_pk(op)));
    /* prefunding: each participant's settlement account, from a central issuer */
    for (int c = 0; c < 2; c++) {
        TRY(zxv_ledger_open_account_cap(cap, "central-issuer", ccy[c], ZXV_ACCT_ISSUER, &issuer[c]));
        for (int p = 0; p < 4; p++) {
            uint32_t a = 0;
            char ref[32];
            TRY(zsc_account(h, ZSC_ACCT_PARTICIPANT, k_bank[p], ccy[c], &a));
            snprintf(ref, sizeof ref, "prefund-%s-%d", ccy[c], p);
            TRY(zxv_ledger_post_cap(cap, issuer[c], a, 50000000, ccy[c], ref, NULL));
        }
    }
    printf("house SCLR: 4 participants prefunded with 500,000.00 in NGN and GHS\n");

    TRY(zsc_cycle_open(h, "2026-10-12-AM", "2026-10-12T09:00:00Z"));
    /* pacs.008 messages, in the documented subset */
    {
        zsc_instr p1 = pay("INV-1001", "NGN", 1250000, 0, 1), p2 = pay("INV-1002", "NGN", 830050, 1, 2),
                  p3 = pay("INV-1003", "GHS", 99999, 3, 0);
        zsc_instr *all[3] = {&p1, &p2, &p3};
        for (int i = 0; i < 3; i++) {
            TRY(zsc_render_pacs008(cap, all[i], xml, sizeof xml, &len));
            TRY(zsc_submit_pacs008(h, xml, len, NULL));
        }
        printf("pacs.008: 3 accepted (%zu bytes for the last one)\n", len);
        /* INV-1002 again with a different amount: both versions are held */
        p2.amount = 803050;
        TRY(zsc_render_pacs008(cap, &p2, xml, sizeof xml, &len));
        st = zsc_submit_pacs008(h, xml, len, NULL);
        printf("INV-1002 resent as 8,030.50 instead of 8,300.50: %s (held as BOTH for an operator)\n",
               zsc_status_name(st));
        /* a damaged message is refused at its byte */
        {
            size_t off = 0;
            char *p = strstr(xml, "<ChrgBr>SLEV");
            if (p) memcpy(p + 8, "SHAR", 4);
            st = zsc_parse_pacs008(xml, len, &p1, &off);
            printf("a message with ChrgBr SHAR: %s at byte %zu\n", zsc_status_name(st), off);
        }
    }
    /* a Sutra treaty: a batch from a COBOL shop */
    TRY(zsc_submit_sutra(h,
                         "TREATY BATCH-77 GHS NET\n"
                         "PARTY AAAANGLA 0 0 1/2\n"
                         "PARTY BBBBGHAC 0 0 1/2\n"
                         "OWE AAAANGLA BBBBGHAC 40000\n"
                         "OWE BBBBGHAC AAAANGLA 15000\n"
                         "END\n",
                         &b));
    printf("Sutra treaty BATCH-77: %u obligations accepted\n", b.accepted);
    /* the operator decides: the second version (8,030.50) was right */
    TRY(zsc_decision_message(h, "INV-1002", 2, msg, sizeof msg, &len));
    TRY(zind_sign(cap, op, msg, len, "sutra-clear/operator", sig));
    TRY(zsc_resolve(h, zind_signer_pk(op), "INV-1002", 2, sig));
    printf("operator signed: INV-1002 settles as version 2; version 1 is logged as Forbidden\n");

    TRY(zsc_cycle_close(h, &info));
    printf("cycle %u closed: %u instructions, %u postings, entries %llu..%llu, netted by %s\n",
           info.cycle_no, info.instructions, info.postings, (unsigned long long) info.first_entry,
           (unsigned long long) info.last_entry,
           info.backend == ZSC_NET_TREATY ? "the treaty engine (zxv_treaty_run)" : "zxv_netting");
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < 4; p++) {
            int64_t net = 0;
            zsc_fee_info fi;
            if (zsc_position(h, k_bank[p], ccy[c], &net) != ZXV_OK) continue;
            memset(&fi, 0, sizeof fi);
            (void) zsc_fee(h, k_bank[p], ccy[c], &fi);
            printf("  %s %-9s net %+12lld  fee %4llu (reserve %llu, dividend %llu, infra %llu, "
                   "regen %llu)\n",
                   ccy[c], k_bank[p], (long long) net, (unsigned long long) fi.fee,
                   (unsigned long long) fi.split[0], (unsigned long long) fi.split[1],
                   (unsigned long long) fi.split[2], (unsigned long long) fi.split[3]);
        }
    TRY(zsc_iso_count(h, &n));
    TRY(zsc_iso_at(h, 0, NULL, xml, sizeof xml, &len));
    printf("ISO 20022 output: %u messages; the first settlement pacs.008:\n%s", n, xml);
    TRY(zxv_hexlog_count(qlog, &q));
    TRY(zind_chain_count(zsc_records(h), &n));
    {
        const uint8_t *trust[1] = {zind_signer_pk(house_key)};
        size_t cl = 0, jl = 0;
        TRY(zind_chain_verify(cap, zsc_records(h), trust, 1, NULL));
        (void) zind_chain_export(zsc_records(h), NULL, 0, &cl);
        (void) zsc_journal_export(cap, NULL, 0, &jl);
        printf("signed record chain: %u record(s), verifies; export %zu bytes, journal %zu bytes "
               "(for Tri-Witness)\nquarantine log: %u events\n",
               n, cl, jl, q);
    }
    TRY(zxv_ledger_check_cap(cap));
    zsc_house_destroy(h);
    zxv_hexlog_destroy(qlog);
    zind_signer_destroy(house_key);
    zind_signer_destroy(op);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
