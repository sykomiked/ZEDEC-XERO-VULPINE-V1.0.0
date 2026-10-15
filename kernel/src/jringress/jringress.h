/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* jringress.h — the Producer Ingress driver: the optional, detachable layer
 * that brings physical lots (olive oil drums, bonded metal) into the
 * jurisdictional router (jroute.h R11).
 *
 * A build that leaves this module out never turns the physical layer on, so
 * every allotment goes to compute or mutual credit and nothing waits on a
 * warehouse. Nothing in jroute calls this module.
 *
 *   I1 PLEDGE. A producer pledges 1..JRI_MAX_MANIFEST_LOTS lots of one
 *      physical asset in a manifest signed by the producer over
 *        "zxv/jr-ingress/v1" || producer key[32] || asset u32 ||
 *        version u64 || n u32 || sha256(the n 128-byte lot headers)
 *      checked by the deployment's verify hook (no hook: refused). The
 *      version must be above the producer's last accepted one (no replay).
 *      Every header must decode, be new to the router and to the manifest,
 *      and fit; otherwise nothing is added (all or none). On success the
 *      lots join the router tagged with the producer's slot and the
 *      physical layer comes on.
 *   I2 WITHDRAW. A producer withdraws its FREE lots with a signed
 *        "zxv/jr-withdraw/v1" || producer key[32] || version u64
 *      (version above the last). Lots already allotted or held in escrow
 *      stay where they are. When no free physical lot remains in the router
 *      the physical layer goes off.
 *   I3 BOUNDS. At most JRI_MAX_PRODUCERS producers. A producer that has
 *      withdrawn keeps its slot and version so old manifests cannot replay.
 *
 * HONEST LIMITS. A signature proves which producer key signed, not that any
 * oil exists or that a warehouse holds it: the lot header carries the hash of
 * a custodian-signed UCC Article 7 receipt, and checking that receipt (and
 * the custodian behind it) is the deployment's job. Freestanding C11: no
 * allocation, no float.
 */
#ifndef ZXV_JRINGRESS_H
#define ZXV_JRINGRESS_H

#include <stdbool.h>
#include <stdint.h>
#include "../jroute/jroute.h"

#define JRI_MAX_PRODUCERS     16u
#define JRI_MAX_MANIFEST_LOTS 32u
#define JRI_PLEDGE_MSG_LEN    (17u + 32u + 4u + 8u + 4u + 32u)
#define JRI_WITHDRAW_MSG_LEN  (18u + 32u + 8u)

typedef struct {
    uint8_t key[32];
    uint64_t version;
    uint32_t pledged; /* lots ever added */
    bool used;
} jri_producer_t;

typedef struct {
    jr_router_t *router;
    jr_verify_fn verify;
    void *vctx;
    jri_producer_t prod[JRI_MAX_PRODUCERS];
} jri_t;

jr_status_t jri_init(jri_t *g, jr_router_t *r, jr_verify_fn verify, void *vctx);

/* The bytes a pledge signature covers (I1). */
void jri_pledge_msg(const uint8_t key[32], uint32_t asset_id, uint64_t version, const uint8_t *hdrs,
                    uint32_t n, uint8_t out[JRI_PLEDGE_MSG_LEN]);
jr_status_t jri_pledge(jri_t *g, const uint8_t key[32], uint32_t asset_id, uint64_t version,
                       const uint8_t *hdrs, uint32_t n, const uint8_t *sig, uint32_t siglen);

void jri_withdraw_msg(const uint8_t key[32], uint64_t version, uint8_t out[JRI_WITHDRAW_MSG_LEN]);
/* Returns JR_OK and the number of lots withdrawn in *n. */
jr_status_t jri_withdraw(jri_t *g, const uint8_t key[32], uint64_t version, const uint8_t *sig,
                         uint32_t siglen, uint32_t *n);

#endif /* ZXV_JRINGRESS_H */
