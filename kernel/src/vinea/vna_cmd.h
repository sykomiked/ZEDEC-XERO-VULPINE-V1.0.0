/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_cmd.h — requests between nodes and agents as one-line Hackronomicon
 * commands (src/swarm/swarm_hk.h, rules K1-K5).
 *
 *   ask(compute, units: 100, price: 21)          buy compute tokens
 *   offer(storage, units: 1048576, price: 3)     sell storage bytes
 *   bid(memory, units: 4096, cycles: 8, price: 40)
 *   counter(compute, units: 100, price: 34)      negotiation step
 *   accept(compute, units: 100, price: 30)
 *   reject(compute)
 *   lease(memory, units: 65536, cycles: 4)       request a memory lease
 *   fetch(file, root: <64 hex>, index: 7)        request one file chunk
 *
 * The text is parsed with swarm_hk_parse and must equal its own
 * swarm_hk_canonical form (one reading, K2); more than five operators is
 * refused (K3). Only the verbs and keys above exist: anything else (for
 * example "set(allocation, ...)" aimed at someone's internal economy) is
 * VNA_ERR_HK. No command reaches a node's internal swarm budget: the
 * internal economy is touched only by the owner, through the gate in
 * vna_econ.h.
 */
#ifndef VNA_CMD_H
#define VNA_CMD_H

#include "vna_common.h"
#include "vna_agree.h"

typedef enum {
    VNA_V_ASK = 0,
    VNA_V_OFFER,
    VNA_V_BID,
    VNA_V_COUNTER,
    VNA_V_ACCEPT,
    VNA_V_REJECT,
    VNA_V_LEASE,
    VNA_V_FETCH,
    VNA_V_COUNT
} vna_verb_t;

#define VNA_CMD_UNITS  0x01u
#define VNA_CMD_PRICE  0x02u
#define VNA_CMD_CYCLES 0x04u
#define VNA_CMD_FORM   0x08u
#define VNA_CMD_ROOT   0x10u
#define VNA_CMD_INDEX  0x20u

typedef struct {
    uint8_t verb;     /* vna_verb_t */
    uint8_t resource; /* vna_resource_t */
    uint8_t form;     /* swarm_cap_t / zcap_form_t, if VNA_CMD_FORM */
    uint8_t has;      /* VNA_CMD_* bits */
    uint64_t units, price, cycles, index;
    vna_id_t root;
} vna_cmd_t;

/* Decode one canonical line. VNA_OK or VNA_ERR_HK. */
vna_status_t vna_cmd_decode(const uint8_t *text, uint32_t len, vna_cmd_t *out);

/* Encode to canonical text. Returns its length or -1. */
int32_t vna_cmd_encode(const vna_cmd_t *c, uint8_t *out, uint32_t cap);

/* Does this verb ask the receiver to give up a resource (checked against the
 * receiver's agreement)? */
bool vna_cmd_requests_resource(const vna_cmd_t *c);

#endif /* VNA_CMD_H */
