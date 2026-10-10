/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_capmkt.h — the bridge to the capacity market (kernel/src/capmkt).
 *
 * Two clearing modes share one provider registry:
 *   prov_match   continuous price-time matching at the ask price, any quote
 *                asset (VFV, ISO 4217, chain assets), with every user filter
 *                of prov_job_t (residency, licence, attestation, privacy,
 *                SLA, reputation) and the 1/phi^2 share cap.
 *   capmkt       capmkt's periodic uniform-price double auction for plain
 *                VFV commodity markets (compute, inference, storage,
 *                bandwidth per region and tenor), with its own 8/21 cap,
 *                escrow and pay-on-delivery.
 * This bridge does not duplicate capmkt; it feeds it. A provider's SIGNED
 * prov ask (G2) is moved into capmkt's book (and withdrawn from prov's, so
 * the same capacity is never sold twice), a user's bid is mirrored, and a
 * capmkt delivery proof is accepted only when its evidence is the digest of
 * a prov receipt that BOTH parties co-signed with ML-DSA-65 (G6). capmkt's
 * fee hook is routed to prov_fee, so the published fee is the same in
 * both modes (in the default mode it equals cm_fee_assure exactly).
 *
 * Unit factor: one capmkt unit = `factor` prov units of the offer (e.g. 1000
 * tokens for CM_RES_INFERENCE, 3600 accelerator-seconds for one
 * compute-hour); prices are scaled by the same factor. */
#ifndef ZXV_PROV_CAPMKT_H
#define ZXV_PROV_CAPMKT_H

#include "prov.h"
#include "../capmkt/capmkt.h"

#define PROV_CM_MAX_RECEIPTS 64u

typedef struct {
    bool used, spent;
    uint32_t contract_id;
    uint64_t units;
    uint8_t digest[PROV_HASH_LEN];
    uint8_t provider16[CM_ID_BYTES], user16[CM_ID_BYTES];
} prov_cm_rcpt_t;

typedef struct {
    prov_net_t *n;
    cm_market_t *m;
    prov_cm_rcpt_t rc[PROV_CM_MAX_RECEIPTS];
} prov_cm_t;

/* Bind the bridge and fill capmkt params (fee = prov_fee, verify = the
 * co-signed receipt check, ctx = the bridge). Call before cm_init. */
void prov_cm_bind(prov_cm_t *b, prov_net_t *n, cm_market_t *m, cm_params_t *params);
/* capmkt account id of a prov provider / user: the first 16 bytes of its id. */
void prov_cm_id(const uint8_t id[PROV_ID_LEN], uint8_t out[CM_ID_BYTES]);
/* Move a live, VFV-quoted prov ask into capmkt. */
int prov_cm_ask(prov_cm_t *b, uint32_t ask, cm_key_t key, uint64_t factor, uint64_t expires_ms,
                uint32_t *order_id);
/* Mirror a user's VFV job into capmkt (the buyer must have deposited). */
int prov_cm_bid(prov_cm_t *b, uint32_t user, const prov_job_t *job, cm_key_t key, uint64_t factor,
                uint64_t expires_ms, uint32_t *order_id);
/* Build the receipt for `units` capmkt units delivered on contract `cid`.
 * hold = gross (capmkt escrows exactly price x qty), no SLA credit. */
int prov_cm_receipt(const prov_cm_t *b, uint32_t cid, uint8_t rclass, uint8_t unit,
                    const prov_usage_t *u, prov_receipt_t *r);
/* Verify a fully co-signed receipt and register it for one delivery proof. */
int prov_cm_accept(prov_cm_t *b, uint32_t cid, const prov_receipt_t *r);
/* capmkt hooks. */
bool prov_cm_verify(void *ctx, const cm_contract_t *c, const cm_proof_t *p);
uint64_t prov_cm_fee(void *ctx, uint64_t amount);

#endif /* ZXV_PROV_CAPMKT_H */
