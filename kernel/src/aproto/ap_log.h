/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ap_log.h — the audit chain every protocol twin logs to (docs/P2P_PROTOCOL_TWINS.md 4.1).
 *
 *   L1  An entry is one schema record: owner, seq, previous head, time, twin
 *       id, event, lpres tag, peer, subject, amount and a status code.
 *   L2  head_0 = H("aproto/v1/genesis", owner); head_k = H("aproto/v1/log",
 *       entry_k bytes), and entry k carries head_{k-1} as `prev`. Changing,
 *       dropping, inserting or reordering any entry breaks every later head.
 *       H is vna_htag (domain-separated SHA3-256).
 *   L3  seq starts at 1 and rises by exactly one. Time never goes backwards:
 *       an earlier reading is recorded as the last time seen.
 *   L4  The log keeps only its head. Each entry's bytes go to the host's sink;
 *       if the sink refuses, nothing advances and ap_log_append fails, so the
 *       caller does not act ("no audit, no action").
 *   L5  A checkpoint {owner, seq, head, time} is signed with ML-DSA-87 under
 *       "aproto/v1/log-head".
 *   L6  An auditor replays entries from genesis and compares with a
 *       checkpoint; two checkpoints with one owner and seq but different heads
 *       are a fork (tag BOTH).
 *
 * Freestanding C11, integer only, no allocation, like src/vinea.
 */
#ifndef ZXV_AP_LOG_H
#define ZXV_AP_LOG_H

#include "../vinea/vna_id.h"
#include "../vinea/vna_schema.h"

#define AP_LOG_MAGIC  0x31474C41u /* "ALG1" */
#define AP_HEAD_MAGIC 0x31484C41u /* "ALH1" */
#define AP_CTX_HEAD   "aproto/v1/log-head"

/* lpres tags (values of lpres_state_t, src/lpres/lpres.h). */
#define AP_TAG_N 0u /* neither: pending, unknown */
#define AP_TAG_T 1u /* true: verified, done */
#define AP_TAG_F 2u /* false: refused, failed */
#define AP_TAG_B 3u /* both: contradictory evidence, held */

/* Twin ids: the IANA port of the protocol a twin replaces. */
#define AP_TWIN_LOG  514u /* syslog: the chain's own events */
#define AP_TWIN_AFTP 21u

typedef struct {
    uint32_t magic;
    vna_id_t owner;
    uint64_t seq;
    uint8_t prev[32];
    uint64_t time;
    uint16_t twin;
    uint16_t event;
    uint8_t tag;
    vna_id_t peer;
    vna_id_t subject;
    uint64_t amount;
    uint16_t code; /* 0, or minus a vna_status_t */
} ap_entry_t;

typedef struct {
    uint32_t magic;
    vna_id_t owner;
    uint64_t seq;
    uint8_t head[32];
    uint64_t time;
    uint8_t sig[VNA_SIG_LEN];
} ap_head_t;

extern const vna_schema_t ap_entry_schema;
extern const vna_schema_t ap_head_schema;

#define AP_ENTRY_LEN 163u /* every entry has this fixed encoding */
#define AP_HEAD_MAX  (4u + 32u + 8u + 32u + 8u + VNA_SIG_LEN)

/* The host stores each entry; return 0 when it is durably kept. */
typedef int (*ap_sink_fn)(void *ctx, const uint8_t *entry, uint32_t len);

typedef struct {
    vna_id_t owner;
    uint64_t seq;
    uint8_t head[32];
    uint64_t last_time;
    ap_sink_fn sink; /* NULL: entries are only folded into the head */
    void *ctx;
    uint32_t by_tag[4];
    uint32_t sink_failures;
} ap_log_t;

void ap_log_genesis(const vna_id_t *owner, uint8_t head[32]);
void ap_log_init(ap_log_t *l, const vna_id_t *owner, ap_sink_fn sink, void *ctx);

/* Append one entry (L1-L4). peer / subject may be NULL (zero). Returns
 * VNA_OK, VNA_ERR_ARG (bad tag or log) or VNA_ERR_SPACE (the sink refused). */
vna_status_t ap_log_append(ap_log_t *l, uint16_t twin, uint16_t event, uint8_t tag,
                           const vna_id_t *peer, const vna_id_t *subject, uint64_t amount,
                           int32_t status, uint64_t now);

/* L5: sign the current head. Returns the checkpoint length or a negative
 * vna_status_t. `self` must be the log's owner. */
int32_t ap_log_checkpoint(const ap_log_t *l, const vna_identity_t *self, const uint8_t rnd[32],
                          uint64_t now, uint8_t *out, uint32_t cap);

/* Verify a checkpoint under `pk` (owner == SHA3-256(pk)). */
vna_status_t ap_head_open(const uint8_t *in, uint32_t len, const uint8_t pk[VNA_PK_LEN],
                          ap_head_t *out);

/* L6: compare two verified checkpoints of one owner. T: same seq and head;
 * B: same seq, different head (a fork); N: different seq or owners (cannot
 * tell from heads alone). */
uint8_t ap_head_compare(const ap_head_t *a, const ap_head_t *b);

/* ---- auditor (L6) ---- */
typedef struct {
    vna_id_t owner;
    uint64_t seq;
    uint8_t head[32];
    uint64_t last_time;
} ap_audit_t;

void ap_audit_init(ap_audit_t *a, const vna_id_t *owner);
/* Feed the next entry's bytes. VNA_OK, or VNA_ERR_PARSE (malformed, wrong
 * owner), VNA_ERR_FORK (seq gap, prev mismatch, time going backwards). On
 * error the auditor is unchanged. */
vna_status_t ap_audit_feed(ap_audit_t *a, const uint8_t *entry, uint32_t len, ap_entry_t *out);
/* T if the replayed chain reaches exactly the checkpoint, F if the checkpoint
 * is at this seq with another head, N if the seq differs. */
uint8_t ap_audit_matches(const ap_audit_t *a, const ap_head_t *h);

#endif /* ZXV_AP_LOG_H */
