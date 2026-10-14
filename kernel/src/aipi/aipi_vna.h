/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aipi_vna.h — AIPI over Vinea: the glue between an AIPI context (aipi.h) and
 * one Vinea node (src/vinea/vna_node.h). Like both, a pure state machine: no
 * sockets, no clock, no allocation. The host hands it the time and flushes
 * the node's outbox. Design: docs/AIPI.md section 12.
 *
 * CALLER SIDE
 *   V1 BROADCAST. aipi_execute's mesh route calls the adapter's broadcast
 *      hook. The call record goes, as one signed VNA_MSG_AIPI CALL, to up to
 *      `want` executors: first the owner's chosen executors that are
 *      contacts (aipi_vna_add_executor), then the provider the DHT holds
 *      under aipi_provider_key(function CID) (a FIND_VALUE lookup; the
 *      record is verified, ML-DSA-87, before it is used). Nothing else is
 *      asked: whom to trust is the owner's choice (docs/AIPI.md section 13).
 *   V2 CLAIMS. A claim counts only if (a) the Vinea message carrying it
 *      verified, (b) its executor field equals the message's sender, and (c)
 *      that sender is one this node asked for this call. Then it goes to
 *      aipi_submit, which verifies the result itself (checker, certificate
 *      or quorum). A claim from anyone else is refused before AIPI sees it,
 *      so a stranger cannot stuff a quorum.
 *   V3 PAYMENT. reserve / refund go to the host's economy hooks (fail
 *      closed: with no reserve hook only a zero bounty can be escrowed).
 *      When AIPI pays an executor (only after verification, rule A2), the
 *      host's pay hook arranges the payment (zxv_net_host: a want for one
 *      compute receipt on credit bound to that executor, vna_link.h) and the
 *      adapter sends the executor a signed PAID notice naming the call and
 *      amount; the executor then offers the receipt and both books apply it.
 *
 * EXECUTOR SIDE
 *   V4 SERVE. A CALL reaches the adapter only if the node's agreement shares
 *      compute with the caller (vna_node.c checks degree, audience and the
 *      compute quota). The adapter also requires aipi_vna_serve(true), a
 *      bounty at or above min_bounty, and aipi_serve to succeed (it refuses
 *      private and LOCAL-verified functions and needs the code locally or
 *      fetchable). Otherwise it answers BUSY. The claim goes back to the
 *      caller signed by the node.
 *   V5 PAID. A PAID notice goes to the host's paid hook with the caller, the
 *      amount and the call CID; the host decides whether to collect.
 *
 * HONEST LIMITS. One provider record per lookup (the first found); a
 * function's code is fetched through the IPFS node's own peer hook, which
 * this adapter does not bind; calls and claims are single datagrams up to
 * about 10 KB (IP fragmentation off the loopback).
 */
#ifndef ZXV_AIPI_VNA_H
#define ZXV_AIPI_VNA_H

#include "aipi.h"
#include "../vinea/vna_node.h"

#define AIPI_VNA_CALLS     8u
#define AIPI_VNA_EXECUTORS 8u

_Static_assert(AIPI_CLAIM_ENC_MAX <= VNA_AIPI_DATA_MAX, "an AIPI claim must fit one Vinea message");
_Static_assert(AIPI_CALL_ENC_MAX <= VNA_AIPI_DATA_MAX, "an AIPI call must fit one Vinea message");

typedef struct {
    void *ctx;
    /* caller: put `amount` in escrow; 0 = held */
    int (*reserve)(void *ctx, uint64_t amount);
    /* caller: arrange paying `amount` to `executor` for `call`; 0 = arranged */
    int (*pay)(void *ctx, const vna_id_t *executor, uint64_t amount, const ipfsn_cid_t *call);
    /* caller: escrow returned */
    void (*refund)(void *ctx, uint64_t amount);
    /* executor: `caller` says it pays `amount` for `call` (V5) */
    void (*paid)(void *ctx, const vna_id_t *caller, uint64_t amount, const ipfsn_cid_t *call);
} aipi_vna_econ_t;

typedef struct {
    bool used;
    ipfsn_cid_t call;
    int32_t lookup; /* provider lookup slot, or -1 */
    uint64_t bounty;
    uint32_t want;
    uint64_t deadline;
    uint8_t nasked;
    vna_id_t asked[AIPI_CLAIMS_MAX];
    uint16_t rec_len;
    uint8_t rec[AIPI_CALL_ENC_MAX];
} aipi_vna_call_t;

typedef struct {
    aipi_ctx_t *ai;
    vna_node_t *node;
    vna_outbox_t *ob; /* where hook-driven messages go; the host sends it */
    aipi_vna_econ_t econ;
    bool serve;
    uint64_t min_bounty;
    uint64_t now;      /* the latest time an entry point was given */
    uint64_t call_ttl; /* how long a call slot waits for claims */
    vna_id_t exec[AIPI_VNA_EXECUTORS];
    uint32_t nexec;
    aipi_vna_call_t calls[AIPI_VNA_CALLS];
    const aipi_vna_call_t *cur; /* the call whose claim is being submitted */
    uint8_t claim[AIPI_CLAIM_ENC_MAX];
    /* statistics */
    uint32_t calls_out, lookups, provider_sends, served, serve_refused, busy_in;
    uint32_t claims_in, claims_refused, paid_out, pay_failed, paid_in;
} aipi_vna_t;

/* Bind ai's mesh hooks and the node's AIPI handler to the adapter, and set
 * ai->self to the node's NodeID (what it puts in the claims it serves).
 * econ may be NULL (no economy: only zero bounties, no payments). */
int aipi_vna_init(aipi_vna_t *v, aipi_ctx_t *ai, vna_node_t *node, vna_outbox_t *ob,
                  const aipi_vna_econ_t *econ);

/* V1: an executor the owner chose to ask (at most AIPI_VNA_EXECUTORS). */
int aipi_vna_add_executor(aipi_vna_t *v, const vna_id_t *id);
void aipi_vna_clear_executors(aipi_vna_t *v);

/* V4: answer calls (false: every call gets BUSY). */
void aipi_vna_serve(aipi_vna_t *v, bool on, uint64_t min_bounty);

/* Announce in the DHT that this node serves fn_cid (a public provider
 * record under aipi_provider_key; addr: where to reach it, may be empty).
 * Returns the publish lookup slot (>= 0) or a negative vna_status_t. */
int32_t aipi_vna_announce(aipi_vna_t *v, const ipfsn_cid_t *fn_cid, const uint8_t *addr,
                          uint32_t addr_len, uint64_t now, uint64_t ttl_ms);

/* aipi_execute with the time the broadcast hook needs. */
int aipi_vna_execute(aipi_vna_t *v, const ipfsn_cid_t *fn_cid, const uint8_t *in, uint32_t in_len,
                     bool input_private, uint64_t bounty, uint64_t now, uint64_t ttl,
                     uint32_t *job);

/* Time passes: finish provider lookups (send the call to what they found),
 * expire AIPI jobs (aipi_tick: refunds) and free finished call slots. */
void aipi_vna_tick(aipi_vna_t *v, uint64_t now);

/* Calls still waiting for claims. */
uint32_t aipi_vna_pending(const aipi_vna_t *v);

#endif /* ZXV_AIPI_VNA_H */
