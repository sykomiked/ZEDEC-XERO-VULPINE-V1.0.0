/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* triage_demo.c — vinea-triage at a bus crash with a patchy radio link.
 * Two medics triage patients on their devices. Bundles arrive out of
 * order and twice; the station waits for the gap, then applies them in
 * order and drops the repeat. Both medics triage the same patient without
 * seeing each other's record and disagree: the patient is held as BOTH,
 * never averaged or overwritten, until the doctor at the station signs a
 * decision. Acknowledgements prune the devices. A cloned device is caught
 * as a fork. Advisory only: not a medical device; bundles are signed but
 * NOT encrypted (the transport must encrypt). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zvtriage.h"

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t rc_ = (x);                                                                     \
        if (rc_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s\n", #x, zvt_status_name(rc_));                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static const char *k_cat[] = {"-", "MINOR (green)", "DELAYED (yellow)", "IMMEDIATE (red)",
                              "EXPECTANT (gray)", "DEAD (black)"};

static zvt_record rec(const char *patient, uint8_t cat, uint64_t t, int resp, int spo2,
                      const char *note) {
    zvt_record r;
    memset(&r, 0, sizeof r);
    snprintf(r.patient, sizeof r.patient, "%s", patient);
    r.system = ZVT_SYS_START;
    r.category = cat;
    r.observed = t;
    r.n_vitals = 2;
    r.vitals[0] = (zvt_vital){ZVT_V_RESP, ZVT_U_PER_MIN, resp};
    r.vitals[1] = (zvt_vital){ZVT_V_SPO2, ZVT_U_PERCENT, spo2};
    snprintf(r.note, sizeof r.note, "%s", note);
    return r;
}

static uint8_t *bundle(zvt_outbox *o, uint32_t i, size_t *len) {
    uint8_t *b;
    if (zvt_outbox_bundle(o, i, NULL, 0, len) != ZXV_E_BUFFER) return NULL;
    b = (uint8_t *) malloc(*len);
    if (b && zvt_outbox_bundle(o, i, b, *len, len) != ZXV_OK) {
        free(b);
        return NULL;
    }
    return b;
}

static void show(const zvt_station *s, const char *p) {
    zvt_patient_view v;
    if (zvt_patient_state(s, p, &v) != ZXV_OK) return;
    if (v.lp == ZXV_LP_BOTH) {
        printf("  %s: HELD as BOTH (%u current records:", p, v.heads);
        for (uint32_t i = 0; i < v.heads; i++) {
            char who[40];
            uint8_t c = 0;
            zvt_patient_head(s, p, i, NULL, who, sizeof who, NULL, &c, NULL);
            printf(" %s says %s;", who, k_cat[c]);
        }
        printf(" a person decides)\n");
    } else {
        printf("  %s: %s%s, %u record(s), tag %s\n", p, k_cat[v.category],
               v.resolved ? " (reviewer's decision)" : "", v.records, zxv_hex_name(v.tag));
    }
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_hexlog *qlog = NULL;
    zind_signer *stk = NULL, *doc = NULL, *m1k = NULL, *m2k = NULL;
    zvt_station *st = NULL;
    zvt_outbox *m1 = NULL, *m2 = NULL, *clone = NULL;
    zvt_record r;
    zvt_receipt rc;
    uint8_t seed[32], *b[3], h[32], sig[ZIND_SIG_BYTES], msg[512], ack[6000];
    size_t bl[3], len = 0;
    uint32_t n = 0;

    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_CRYPTO_SIGN | ZXV_CAP_ATTEST, &cap));
    TRY(zxv_hexlog_create(cap, 256, &qlog));
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) i;
    TRY(zind_signer_from_seed(cap, seed, &stk));
    seed[0] = 1;
    TRY(zind_signer_from_seed(cap, seed, &doc));
    seed[0] = 2;
    TRY(zind_signer_from_seed(cap, seed, &m1k));
    seed[0] = 3;
    TRY(zind_signer_from_seed(cap, seed, &m2k));
    TRY(zvt_station_create(cap, stk, qlog, &st));
    TRY(zvt_responder_add(st, "MEDIC-1", zind_signer_pk(m1k)));
    TRY(zvt_responder_add(st, "MEDIC-2", zind_signer_pk(m2k)));
    TRY(zvt_reviewer_add(st, "DR-OKAFOR", zind_signer_pk(doc)));
    TRY(zvt_outbox_create(cap, "MEDIC-1", m1k, &m1));
    TRY(zvt_outbox_create(cap, "MEDIC-2", m2k, &m2));

    printf("vinea-triage demo (advisory; bundles are signed, NOT encrypted)\n\n");
    printf("MEDIC-1 triages three patients, one bundle each, while the radio drops in and out:\n");
    r = rec("TAG-0141", ZVT_CAT_IMMEDIATE, 1000, 34, 88, "airway open after repositioning");
    TRY(zvt_outbox_record(m1, &r, NULL));
    TRY(zvt_outbox_seal(m1, 1000, NULL));
    r = rec("TAG-0142", ZVT_CAT_DELAYED, 1060, 22, 95, "fractured left forearm");
    TRY(zvt_outbox_record(m1, &r, NULL));
    TRY(zvt_outbox_seal(m1, 1060, NULL));
    r = rec("TAG-0143", ZVT_CAT_MINOR, 1120, 16, 98, "walking wounded");
    TRY(zvt_outbox_record(m1, &r, NULL));
    TRY(zvt_outbox_seal(m1, 1120, NULL));
    for (uint32_t i = 0; i < 3; i++) b[i] = bundle(m1, i, &bl[i]);
    printf("  bundle 2 arrives first: %s\n", zvt_status_name(zvt_station_receive(st, b[2], bl[2], &rc)));
    printf("  bundle 0 arrives:       %s\n", zvt_status_name(zvt_station_receive(st, b[0], bl[0], &rc)));
    {
        int32_t st1 = zvt_station_receive(st, b[1], bl[1], &rc);
        printf("  bundle 1 arrives:       %s (applied %u bundles: it unblocked bundle 2)\n",
               zvt_status_name(st1), rc.bundles);
    }
    printf("  bundle 1 again:         %s (dropped)\n",
           zvt_status_name(zvt_station_receive(st, b[1], bl[1], &rc)));
    b[0][40] ^= 0x20; /* in the signed body */
    printf("  a corrupted copy:       %s (refused, logged)\n",
           zvt_status_name(zvt_station_receive(st, b[0], bl[0], &rc)));
    for (int i = 0; i < 3; i++) free(b[i]);
    show(st, "TAG-0141");
    show(st, "TAG-0142");
    show(st, "TAG-0143");

    printf("\nMEDIC-2 reaches TAG-0142 without seeing MEDIC-1's record and finds it worse:\n");
    r = rec("TAG-0142", ZVT_CAT_IMMEDIATE, 1300, 31, 89, "pale, cap refill slow");
    TRY(zvt_outbox_record(m2, &r, NULL));
    TRY(zvt_outbox_seal(m2, 1300, NULL));
    b[0] = bundle(m2, 0, &bl[0]);
    TRY(zvt_station_receive(st, b[0], bl[0], &rc));
    free(b[0]);
    printf("  received: %u record, %u new conflict\n", rc.records, rc.new_conflicts);
    show(st, "TAG-0142");

    printf("\nDR-OKAFOR at the station signs IMMEDIATE for TAG-0142:\n");
    TRY(zvt_resolve_message(st, "TAG-0142", ZVT_CAT_IMMEDIATE, "DR-OKAFOR", msg, sizeof msg, &len));
    TRY(zind_sign(cap, doc, msg, len, ZVT_RESOLVE_CTX, sig));
    printf("  presented as DELAYED instead: %s\n",
           zvt_status_name(zvt_resolve(st, "TAG-0142", ZVT_CAT_DELAYED, "DR-OKAFOR", sig)));
    TRY(zvt_resolve(st, "TAG-0142", ZVT_CAT_IMMEDIATE, "DR-OKAFOR", sig));
    show(st, "TAG-0142");
    TRY(zvt_patient_head(st, "TAG-0142", 0, NULL, NULL, 0, h, NULL, NULL));
    r = rec("TAG-0142", ZVT_CAT_IMMEDIATE, 1400, 28, 93, "loaded for transport");
    memcpy(r.based_on, h, 32); /* MEDIC-1 heard the decision and builds on it */
    TRY(zvt_outbox_record(m1, &r, NULL));
    TRY(zvt_outbox_seal(m1, 1400, NULL));
    b[0] = bundle(m1, 3, &bl[0]);
    TRY(zvt_station_receive(st, b[0], bl[0], &rc));
    free(b[0]);
    show(st, "TAG-0142");

    printf("\nacknowledgements:\n");
    TRY(zvt_outbox_pending(m1, &n));
    printf("  MEDIC-1 holds %u unacknowledged bundle(s)\n", n);
    TRY(zvt_station_ack(st, "MEDIC-1", ack, sizeof ack, &len));
    printf("  an acknowledgement checked against the doctor's key: %s\n",
           zvt_status_name(zvt_outbox_ack(m1, zind_signer_pk(doc), ack, len)));
    TRY(zvt_outbox_ack(m1, zind_signer_pk(stk), ack, len));
    TRY(zvt_outbox_pending(m1, &n));
    printf("  after the station's signed acknowledgement: %u\n", n);

    printf("\na second device with MEDIC-2's key (cloned):\n");
    TRY(zvt_outbox_create(cap, "MEDIC-2", m2k, &clone));
    r = rec("TAG-0150", ZVT_CAT_DEAD, 1500, 0, 0, "");
    r.n_vitals = 0;
    TRY(zvt_outbox_record(clone, &r, NULL));
    TRY(zvt_outbox_seal(clone, 1500, NULL));
    b[0] = bundle(clone, 0, &bl[0]);
    printf("  its bundle 0: %s (two different bundles numbered 0: MEDIC-2 is held)\n",
           zvt_status_name(zvt_station_receive(st, b[0], bl[0], &rc)));
    free(b[0]);

    {
        const uint8_t *trust[1] = {zind_signer_pk(stk)};
        uint32_t jn = 0, qn = 0;
        zind_chain_count(zvt_station_journal(st), &jn);
        zxv_hexlog_count(qlog, &qn);
        printf("\nstation journal: %u signed records, verifies: %s; quarantine log: %u entries\n",
               jn, zvt_status_name(zind_chain_verify(cap, zvt_station_journal(st), trust, 1, NULL)),
               qn);
    }

    zvt_outbox_destroy(clone);
    zvt_outbox_destroy(m1);
    zvt_outbox_destroy(m2);
    zvt_station_destroy(st);
    zind_signer_destroy(stk);
    zind_signer_destroy(doc);
    zind_signer_destroy(m1k);
    zind_signer_destroy(m2k);
    zxv_hexlog_destroy(qlog);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
