/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_agree.c — sharing agreements and the fail-closed check. See vna_agree.h. */
#include "vna_agree.h"

static const vna_field_t file_fields[] = {
    VNA_FFIX(vna_agr_file_t, root, VNA_AXIS_NONE),
    VNA_FU8(vna_agr_file_t, visibility, 1, VNA_AXIS_NONE),
};
static const vna_schema_t file_schema = {"agr_file", 0x0110, 4, file_fields, 2};

static const vna_field_t id_fields[] = {
    VNA_FFIX(vna_agr_id_t, id, VNA_AXIS_PROVENANCE),
};
static const vna_schema_t id_schema = {"agr_id", 0x0111, 4, id_fields, 1};

#define A vna_agreement_t
static const vna_field_t agr_fields[] = {
    VNA_FCONST(A, magic, VNA_AGR_MAGIC),
    VNA_FU8(A, version, VNA_VERSION, VNA_AXIS_NONE),
    VNA_FU8(A, degree, VNA_DEG_MAX, VNA_AXIS_NONE),
    VNA_FU8(A, audience, VNA_AUD_MAX, VNA_AXIS_NONE),
    VNA_FU8(A, flags, 0, VNA_AXIS_NONE),
    VNA_FFIX(A, owner, VNA_AXIS_PROVENANCE),
    VNA_FU64(A, seqno, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(A, issued_ms, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(A, expires_ms, 0, VNA_AXIS_PROVENANCE),
    VNA_FU64(A, trust_min, 0, VNA_AXIS_EXTERNALITY),
    VNA_FU64(A, compute_per_cycle, 0, VNA_AXIS_FINANCIAL),
    VNA_FU64(A, storage_bytes, 0, VNA_AXIS_FINANCIAL),
    VNA_FU64(A, memory_bytes, 0, VNA_AXIS_FINANCIAL),
    VNA_FU32(A, memory_max_cycles, 0, VNA_AXIS_FINANCIAL),
    VNA_FU32(A, cycle_ms, 0, VNA_AXIS_NONE),
    VNA_FU64(A, quota_period_ms, 0, VNA_AXIS_NONE),
    VNA_FU32(A, q_chunks, 0, VNA_AXIS_FINANCIAL),
    VNA_FU64(A, q_compute, 0, VNA_AXIS_FINANCIAL),
    VNA_FU64(A, q_storage, 0, VNA_AXIS_FINANCIAL),
    VNA_FU64(A, q_memory, 0, VNA_AXIS_FINANCIAL),
    VNA_FU32(A, q_records, 0, VNA_AXIS_FINANCIAL),
    VNA_FU32(A, price[0], 0, VNA_AXIS_FINANCIAL),
    VNA_FU32(A, price[1], 0, VNA_AXIS_FINANCIAL),
    VNA_FU32(A, price[2], 0, VNA_AXIS_FINANCIAL),
    VNA_FU32(A, price[3], 0, VNA_AXIS_FINANCIAL),
    VNA_FU32(A, price[4], 0, VNA_AXIS_FINANCIAL),
    VNA_FU32(A, price[5], 0, VNA_AXIS_FINANCIAL),
    VNA_FARR(A, files, n_files, &file_schema, vna_agr_file_t, VNA_AXIS_NONE),
    VNA_FARR(A, allow, n_allow, &id_schema, vna_agr_id_t, VNA_AXIS_PROVENANCE),
    VNA_FARR(A, block, n_block, &id_schema, vna_agr_id_t, VNA_AXIS_PROVENANCE),
    VNA_FVAR(A, terms, terms_len, VNA_AXIS_NONE),
};
#undef A
/* UBH frame class 4 = CAPABILITY_POLICY */
const vna_schema_t vna_agreement_schema = {"agreement", 0x0100, 4, agr_fields,
                                           sizeof agr_fields / sizeof agr_fields[0]};

void vna_agree_default(vna_agreement_t *a, const vna_id_t *owner)
{
    vna_zero(a, sizeof *a);
    a->magic = VNA_AGR_MAGIC;
    a->version = VNA_VERSION;
    a->degree = VNA_DEG_OFF;
    a->audience = VNA_AUD_ALLOWLIST;
    if (owner) a->owner = *owner;
}

static bool terms_ok(const vna_agreement_t *a)
{
    for (uint16_t i = 0; i < a->terms_len; i++) {
        uint8_t c = a->terms[i];
        if (c != '\n' && (c < 0x20 || c == 0x7f)) return false; /* printable UTF-8 bytes */
    }
    return true;
}

int32_t vna_agree_encode(const vna_agreement_t *a, uint8_t *out, uint32_t cap)
{
    if (!a || !terms_ok(a)) return -1;
    return vna_schema_pack(&vna_agreement_schema, a, out, cap, true);
}

vna_status_t vna_agree_decode(const uint8_t *in, uint32_t len, vna_agreement_t *a)
{
    if (vna_schema_unpack(&vna_agreement_schema, in, len, a, 0) < 0) return VNA_ERR_PARSE;
    return terms_ok(a) ? VNA_OK : VNA_ERR_PARSE;
}

vna_status_t vna_agree_cid(const vna_agreement_t *a, uint8_t cid[32])
{
    static uint8_t buf[4096]; /* single-threaded scratch, like the rest of the security layer */
    int32_t n = vna_agree_encode(a, buf, sizeof buf);
    if (n < 0) return VNA_ERR_ARG;
    vna_sha3(buf, (uint32_t) n, cid);
    return VNA_OK;
}

void vna_agree_key(const vna_id_t *owner, vna_id_t *key)
{
    vna_htag("vinea/v2/agreement-of", owner->b, VNA_ID_LEN, key->b);
}

uint8_t vna_res_min_degree(vna_resource_t r)
{
    switch (r) {
    case VNA_RES_ROUTE:
        return VNA_DEG_ROUTE;
    case VNA_RES_RECORD:
        return VNA_DEG_STORE;
    case VNA_RES_STORAGE:
        return VNA_DEG_STORE;
    case VNA_RES_FILE:
        return VNA_DEG_SERVE;
    case VNA_RES_COMPUTE:
        return VNA_DEG_COMPUTE;
    case VNA_RES_MEMORY:
        return VNA_DEG_MEMORY;
    default:
        return 0xFF; /* unknown resource: never offered */
    }
}

bool vna_agree_lists_file(const vna_agreement_t *a, const vna_id_t *root, bool *is_private)
{
    for (uint16_t i = 0; i < a->n_files && i < VNA_AGR_MAX_FILES; i++) {
        if (vna_id_eq(&a->files[i].root, root)) {
            if (is_private) *is_private = a->files[i].visibility != 0;
            return true;
        }
    }
    return false;
}

static bool in_list(const vna_agr_id_t *l, uint16_t n, const vna_id_t *id)
{
    for (uint16_t i = 0; i < n && i < VNA_AGR_MAX_IDS; i++)
        if (vna_id_eq(&l[i].id, id)) return true;
    return false;
}

void vna_usage_init(vna_usage_t *us, vna_usage_ent_t *u, uint32_t ucap, vna_lease_t *l,
                    uint32_t lcap)
{
    us->u = u;
    us->ucap = ucap;
    us->l = l;
    us->lcap = lcap;
    us->compute_this_cycle = us->storage_used = us->memory_leased = 0;
    for (uint32_t i = 0; i < ucap; i++) u[i].used = false;
    for (uint32_t i = 0; i < lcap; i++) l[i].used = false;
}

void vna_usage_new_cycle(vna_usage_t *us)
{
    us->compute_this_cycle = 0;
}

uint64_t vna_usage_expire(vna_usage_t *us, uint64_t now)
{
    uint64_t freed = 0;
    for (uint32_t i = 0; i < us->lcap; i++) {
        vna_lease_t *l = &us->l[i];
        if (!l->used || l->expires_ms > now) continue;
        freed += l->bytes;
        us->memory_leased -= l->bytes;
        for (uint32_t k = 0; k < us->ucap; k++) {
            vna_usage_ent_t *e = &us->u[k];
            if (e->used && vna_id_eq(&e->peer, &l->peer)) {
                e->memory = e->memory >= l->bytes ? e->memory - l->bytes : 0;
                break;
            }
        }
        l->used = false;
    }
    return freed;
}

static vna_usage_ent_t *usage_for(vna_usage_t *us, const vna_id_t *peer, uint64_t now,
                                  uint64_t period, bool create)
{
    vna_usage_ent_t *e = 0, *free_ = 0;
    for (uint32_t i = 0; i < us->ucap; i++) {
        if (us->u[i].used && vna_id_eq(&us->u[i].peer, peer)) {
            e = &us->u[i];
            break;
        }
        if (!us->u[i].used && !free_) free_ = &us->u[i];
    }
    if (!e) {
        if (!create || !free_) return 0;
        e = free_;
        vna_zero(e, sizeof *e);
        e->peer = *peer;
        e->period_start = now;
        e->used = true;
    }
    if (period && now - e->period_start >= period) { /* new quota period (memory stays leased) */
        e->period_start = now;
        e->chunks = e->records = 0;
        e->compute = e->storage = 0;
    }
    return e;
}

vna_status_t vna_agree_check(const vna_agreement_t *a, vna_usage_t *us, const vna_id_t *peer,
                             uint64_t trust, vna_resource_t res, const vna_id_t *root,
                             uint64_t amount, uint32_t cycles, uint64_t now, bool commit)
{
    if (!a || !peer || res >= VNA_RES_COUNT) return VNA_ERR_DENIED; /* fail closed */
    if (a->magic != VNA_AGR_MAGIC || a->version != VNA_VERSION) return VNA_ERR_DENIED;
    if (a->expires_ms && now >= a->expires_ms) return VNA_ERR_EXPIRED;
    if (in_list(a->block, a->n_block, peer)) return VNA_ERR_DENIED;
    if (a->degree < vna_res_min_degree(res)) return VNA_ERR_DENIED;

    bool allowed = in_list(a->allow, a->n_allow, peer);
    bool trusted = trust >= a->trust_min;
    bool aud;
    switch (a->audience) {
    case VNA_AUD_EVERYONE:
        aud = true;
        break;
    case VNA_AUD_ALLOWLIST:
        aud = allowed;
        break;
    case VNA_AUD_TRUST:
        aud = trusted;
        break;
    case VNA_AUD_ALLOW_OR_TRUST:
        aud = allowed || trusted;
        break;
    default:
        aud = false;
        break;
    }
    if (!aud) return VNA_ERR_DENIED;
    if (res == VNA_RES_ROUTE) return VNA_OK; /* routing has no quota */
    if (!us) return VNA_ERR_DENIED;

    vna_usage_ent_t *e = usage_for(us, peer, now, a->quota_period_ms, commit);
    vna_usage_ent_t zero;
    if (!e) {
        if (commit) return VNA_ERR_CAP; /* usage table full: refuse rather than not count */
        vna_zero(&zero, sizeof zero);
        e = &zero;
    }

    switch (res) {
    case VNA_RES_FILE: {
        bool priv = false;
        if (!root || !vna_agree_lists_file(a, root, &priv)) return VNA_ERR_DENIED;
        if (priv && !allowed) return VNA_ERR_DENIED; /* PRIVATE: allowlist only */
        if (amount != 1) return VNA_ERR_ARG;
        if ((uint64_t) e->chunks + 1u > a->q_chunks) return VNA_ERR_CAP;
        if (commit) e->chunks++;
        return VNA_OK;
    }
    case VNA_RES_RECORD:
        if ((uint64_t) e->records + 1u > a->q_records) return VNA_ERR_CAP;
        if (commit) e->records++;
        return VNA_OK;
    case VNA_RES_COMPUTE:
        if (amount == 0) return VNA_ERR_ARG;
        if (amount > a->q_compute - vna_min64(e->compute, a->q_compute)) return VNA_ERR_CAP;
        if (amount > a->compute_per_cycle - vna_min64(us->compute_this_cycle, a->compute_per_cycle))
            return VNA_ERR_CAP;
        if (commit) {
            e->compute += amount;
            us->compute_this_cycle += amount;
        }
        return VNA_OK;
    case VNA_RES_STORAGE:
        if (amount == 0) return VNA_ERR_ARG;
        if (amount > a->q_storage - vna_min64(e->storage, a->q_storage)) return VNA_ERR_CAP;
        if (amount > a->storage_bytes - vna_min64(us->storage_used, a->storage_bytes))
            return VNA_ERR_CAP;
        if (commit) {
            e->storage += amount;
            us->storage_used += amount;
        }
        return VNA_OK;
    case VNA_RES_MEMORY: {
        if (amount == 0 || cycles == 0) return VNA_ERR_ARG;
        if (cycles > a->memory_max_cycles || a->cycle_ms == 0) return VNA_ERR_CAP;
        if (amount > a->q_memory - vna_min64(e->memory, a->q_memory)) return VNA_ERR_CAP;
        if (amount > a->memory_bytes - vna_min64(us->memory_leased, a->memory_bytes))
            return VNA_ERR_CAP;
        if (!commit) return VNA_OK;
        vna_lease_t *l = 0;
        for (uint32_t i = 0; i < us->lcap; i++)
            if (!us->l[i].used) {
                l = &us->l[i];
                break;
            }
        if (!l) return VNA_ERR_SPACE;
        l->peer = *peer;
        l->bytes = amount;
        l->expires_ms = now + (uint64_t) cycles * a->cycle_ms;
        l->used = true;
        e->memory += amount;
        us->memory_leased += amount;
        return VNA_OK;
    }
    default:
        return VNA_ERR_DENIED;
    }
}
