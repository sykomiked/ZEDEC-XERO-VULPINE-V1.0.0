/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ap_log.c — the twin audit chain. See ap_log.h. */
#include "ap_log.h"

#define E ap_entry_t
static const vna_field_t entry_fields[] = {
    VNA_FCONST(E, magic, AP_LOG_MAGIC),         VNA_FFIX(E, owner, VNA_AXIS_PROVENANCE),
    VNA_FU64(E, seq, 0, VNA_AXIS_NONE),         VNA_FFIX(E, prev, VNA_AXIS_PROVENANCE),
    VNA_FU64(E, time, 0, VNA_AXIS_NONE),        VNA_FU16(E, twin, 0, VNA_AXIS_NONE),
    VNA_FU16(E, event, 0, VNA_AXIS_NONE),       VNA_FU8(E, tag, AP_TAG_B, VNA_AXIS_PROVENANCE),
    VNA_FFIX(E, peer, VNA_AXIS_EXTERNALITY),    VNA_FFIX(E, subject, VNA_AXIS_PROVENANCE),
    VNA_FU64(E, amount, 0, VNA_AXIS_FINANCIAL), VNA_FU16(E, code, 0, VNA_AXIS_NONE),
};
#undef E
/* UBH class 2 = LEDGER_ENTRY */
const vna_schema_t ap_entry_schema = {"ap_entry", 0x0140, 2, entry_fields, 12};

#define H ap_head_t
static const vna_field_t head_fields[] = {
    VNA_FCONST(H, magic, AP_HEAD_MAGIC), VNA_FFIX(H, owner, VNA_AXIS_PROVENANCE),
    VNA_FU64(H, seq, 0, VNA_AXIS_NONE),  VNA_FFIX(H, head, VNA_AXIS_PROVENANCE),
    VNA_FU64(H, time, 0, VNA_AXIS_NONE), VNA_FSIG(H, sig),
};
#undef H
const vna_schema_t ap_head_schema = {"ap_head", 0x0141, 2, head_fields, 6};

void ap_log_genesis(const vna_id_t *owner, uint8_t head[32])
{
    vna_htag("aproto/v1/genesis", owner->b, 32, head);
}

void ap_log_init(ap_log_t *l, const vna_id_t *owner, ap_sink_fn sink, void *ctx)
{
    vna_zero(l, sizeof *l);
    l->owner = *owner;
    ap_log_genesis(owner, l->head);
    l->sink = sink;
    l->ctx = ctx;
}

vna_status_t ap_log_append(ap_log_t *l, uint16_t twin, uint16_t event, uint8_t tag,
                           const vna_id_t *peer, const vna_id_t *subject, uint64_t amount,
                           int32_t status, uint64_t now)
{
    if (!l || tag > AP_TAG_B || status > 0 || status < -65535) return VNA_ERR_ARG;
    ap_entry_t e;
    vna_zero(&e, sizeof e);
    e.magic = AP_LOG_MAGIC;
    e.owner = l->owner;
    e.seq = l->seq + 1u;
    vna_copy(e.prev, l->head, 32);
    e.time = now < l->last_time ? l->last_time : now; /* L3 */
    e.twin = twin;
    e.event = event;
    e.tag = tag;
    if (peer) e.peer = *peer;
    if (subject) e.subject = *subject;
    e.amount = amount;
    e.code = (uint16_t) (uint32_t) -status;
    uint8_t buf[AP_ENTRY_LEN];
    int32_t n = vna_schema_pack(&ap_entry_schema, &e, buf, sizeof buf, true);
    if (n != (int32_t) AP_ENTRY_LEN) return VNA_ERR_ARG;
    if (l->sink && l->sink(l->ctx, buf, (uint32_t) n) != 0) { /* L4 */
        l->sink_failures++;
        return VNA_ERR_SPACE;
    }
    vna_htag("aproto/v1/log", buf, (uint32_t) n, l->head);
    l->seq = e.seq;
    l->last_time = e.time;
    l->by_tag[tag]++;
    return VNA_OK;
}

int32_t ap_log_checkpoint(const ap_log_t *l, const vna_identity_t *self, const uint8_t rnd[32],
                          uint64_t now, uint8_t *out, uint32_t cap)
{
    if (!l || !self || !rnd || !out || !vna_id_eq(&l->owner, &self->id)) return VNA_ERR_ARG;
    static ap_head_t h; /* the signature is large; keep it off small stacks */
    vna_zero(&h, sizeof h);
    h.magic = AP_HEAD_MAGIC;
    h.owner = l->owner;
    h.seq = l->seq;
    vna_copy(h.head, l->head, 32);
    h.time = now;
    int32_t m = vna_schema_pack(&ap_head_schema, &h, out, cap, false);
    if (m < 0) return VNA_ERR_SPACE;
    vna_sign(self->sk, AP_CTX_HEAD, out, (uint32_t) m, rnd, h.sig);
    int32_t n = vna_schema_pack(&ap_head_schema, &h, out, cap, true);
    return n < 0 ? VNA_ERR_SPACE : n;
}

vna_status_t ap_head_open(const uint8_t *in, uint32_t len, const uint8_t pk[VNA_PK_LEN],
                          ap_head_t *out)
{
    uint32_t sig_off = 0;
    if (!in || !pk || !out) return VNA_ERR_ARG;
    if (vna_schema_unpack(&ap_head_schema, in, len, out, &sig_off) < 0) return VNA_ERR_PARSE;
    vna_id_t id;
    vna_node_id(pk, &id);
    if (!vna_id_eq(&id, &out->owner)) return VNA_ERR_BINDING;
    if (!vna_verify(pk, AP_CTX_HEAD, in, sig_off, out->sig)) return VNA_ERR_SIG;
    return VNA_OK;
}

uint8_t ap_head_compare(const ap_head_t *a, const ap_head_t *b)
{
    if (!vna_id_eq(&a->owner, &b->owner) || a->seq != b->seq) return AP_TAG_N;
    return vna_eq(a->head, b->head, 32) ? AP_TAG_T : AP_TAG_B;
}

void ap_audit_init(ap_audit_t *a, const vna_id_t *owner)
{
    vna_zero(a, sizeof *a);
    a->owner = *owner;
    ap_log_genesis(owner, a->head);
}

vna_status_t ap_audit_feed(ap_audit_t *a, const uint8_t *entry, uint32_t len, ap_entry_t *out)
{
    ap_entry_t e;
    if (!a || !entry) return VNA_ERR_ARG;
    if (vna_schema_unpack(&ap_entry_schema, entry, len, &e, 0) < 0) return VNA_ERR_PARSE;
    if (!vna_id_eq(&e.owner, &a->owner)) return VNA_ERR_PARSE;
    if (e.seq != a->seq + 1u || !vna_eq(e.prev, a->head, 32) || e.time < a->last_time)
        return VNA_ERR_FORK;
    vna_htag("aproto/v1/log", entry, len, a->head);
    a->seq = e.seq;
    a->last_time = e.time;
    if (out) *out = e;
    return VNA_OK;
}

uint8_t ap_audit_matches(const ap_audit_t *a, const ap_head_t *h)
{
    if (!vna_id_eq(&a->owner, &h->owner) || a->seq != h->seq) return AP_TAG_N;
    return vna_eq(a->head, h->head, 32) ? AP_TAG_T : AP_TAG_F;
}
