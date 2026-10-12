/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwr.c — signed reconfiguration images. See hwr.h (R1-R7). */
#include "hwr.h"
#include "../robin_debanks/sha256.h"

static uint32_t rd32(const uint8_t *b)
{
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) |
           ((uint32_t) b[3] << 24);
}
static void wr32(uint8_t *b, uint32_t v)
{
    for (int i = 0; i < 4; i++) b[i] = (uint8_t) (v >> (8 * i));
}
static bool eq32(const uint8_t *a, const uint8_t *b)
{
    uint8_t d = 0;
    for (int i = 0; i < 32; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}
static bool zero32(const uint8_t *a)
{
    uint8_t d = 0;
    for (int i = 0; i < 32; i++) d |= a[i];
    return d == 0;
}
static void cp32(uint8_t *d, const uint8_t *s)
{
    for (int i = 0; i < 32; i++) d[i] = s[i];
}

uint32_t hwr_sig_len(uint16_t alg)
{
    switch (alg) {
    case HWR_ALG_MLDSA87:
        return HWR_MLDSA87_SIG;
    case HWR_ALG_MLDSA65:
        return HWR_MLDSA65_SIG;
    case HWR_ALG_DUAL:
        return HWR_DUAL_SIG;
    default:
        return 0;
    }
}
uint32_t hwr_pk_len(uint16_t alg)
{
    switch (alg) {
    case HWR_ALG_MLDSA87:
        return HWR_MLDSA87_PK;
    case HWR_ALG_MLDSA65:
        return HWR_MLDSA65_PK;
    case HWR_ALG_DUAL:
        return HWR_DUAL_PK;
    default:
        return 0;
    }
}

void hwr_build_header(const hwr_image_t *im, uint8_t out[HWR_HEADER])
{
    out[0] = 'Z';
    out[1] = 'X';
    out[2] = 'H';
    out[3] = 'R';
    out[4] = 1;
    out[5] = 0;
    out[6] = (uint8_t) im->alg;
    out[7] = (uint8_t) (im->alg >> 8);
    wr32(out + 8, im->kind);
    wr32(out + 12, im->target_class);
    wr32(out + 16, im->fn_id);
    wr32(out + 20, im->version);
    wr32(out + 24, im->svn);
    wr32(out + 28, im->payload_len);
    cp32(out + 32, im->target_digest);
    cp32(out + 64, im->payload_sha256);
}

int hwr_parse(const uint8_t *img, uint32_t len, hwr_image_t *out)
{
    if (!img || !out) return HWR_E_ARG;
    if (len < HWR_HEADER || img[0] != 'Z' || img[1] != 'X' || img[2] != 'H' || img[3] != 'R' ||
        img[4] != 1 || img[5] != 0)
        return HWR_E_FORMAT;
    out->format = 1;
    out->alg = (uint16_t) (img[6] | (img[7] << 8));
    out->kind = rd32(img + 8);
    out->target_class = rd32(img + 12);
    out->fn_id = rd32(img + 16);
    out->version = rd32(img + 20);
    out->svn = rd32(img + 24);
    out->payload_len = rd32(img + 28);
    cp32(out->target_digest, img + 32);
    cp32(out->payload_sha256, img + 64);
    out->sig_len = hwr_sig_len(out->alg);
    if (!out->sig_len || out->kind < HWR_KIND_BITSTREAM || out->kind > HWR_KIND_VECTORS ||
        out->payload_len == 0 || out->payload_len > HWR_MAX_PAYLOAD)
        return HWR_E_FORMAT;
    /* The length must be exact: header + payload + signature, nothing else. */
    if ((uint64_t) len != (uint64_t) HWR_HEADER + out->payload_len + out->sig_len)
        return HWR_E_FORMAT;
    out->header = img;
    out->payload = img + HWR_HEADER;
    out->sig = img + HWR_HEADER + out->payload_len;
    return HWR_OK;
}

int hwr_region_init(hwr_region_t *r, uint32_t id, uint32_t target_class,
                    const uint8_t target_digest[32], uint16_t alg, hwr_verify_fn verify,
                    void *verify_ctx, const hwr_ops_t *ops, void *dev, hwa_log_t *log)
{
    if (!r || !verify || !ops || !ops->load || !ops->readback || !ops->activate || !log ||
        !hwr_sig_len(alg))
        return HWR_E_ARG;
    uint8_t *b = (uint8_t *) r;
    for (uint32_t i = 0; i < (uint32_t) sizeof *r; i++) b[i] = 0;
    r->id = id;
    r->target_class = target_class;
    if (target_digest) cp32(r->target_digest, target_digest);
    r->alg = alg;
    r->verify = verify;
    r->verify_ctx = verify_ctx;
    r->ops = ops;
    r->dev = dev;
    r->log = log;
    r->resource = r->provider = HWM_NONE;
    r->active = r->staged = HWR_NO_SLOT;
    r->state = HWR_ST_EMPTY;
    return HWR_OK;
}

void hwr_set_event(hwr_region_t *r, hwr_event_fn fn, void *ctx)
{
    if (!r) return;
    r->event = fn;
    r->event_ctx = ctx;
}

int hwr_trust_key(hwr_region_t *r, const uint8_t *pk)
{
    if (!r || !pk) return HWR_E_ARG;
    if (r->nkeys >= HWR_MAX_KEYS) return HWR_E_FULL;
    r->keys[r->nkeys++] = pk;
    return HWR_OK;
}

int hwr_link(hwr_region_t *r, hwm_t *m, uint32_t res_index, uint32_t pv_index)
{
    if (!r || !m || res_index >= m->nres || pv_index >= m->npv ||
        m->pv[pv_index].resource != res_index || !hwm_kind_reconfigurable(m->pv[pv_index].kind))
        return HWR_E_ARG;
    r->m = m;
    r->resource = res_index;
    r->provider = pv_index;
    return HWR_OK;
}

/* ---- recording (R6, R7) ---- */

/* Append one step to the audit log and report it. False (and the region
 * halted) if the log is full. */
static bool record(hwr_region_t *r, const uint8_t *header, lpx_t tag, hwa_event_t ev,
                   uint8_t reason, uint32_t version)
{
    uint8_t subject[32];
    sha256(header, HWR_HEADER, subject);
    if (!hwa_append(r->log, tag, (uint8_t) ev, reason, r->id, version, subject)) {
        r->state = HWR_ST_HALTED; /* R7: a full log halts, fail closed */
        return false;
    }
    if (r->event) r->event(r->event_ctx, r->id, (uint32_t) ev, reason, version);
    return true;
}

static uint32_t hdr_version(const uint8_t *header)
{
    return rd32(header + 20);
}

static int quarantine(hwr_region_t *r, const uint8_t *header, uint8_t phase, hwr_qreason_t why,
                      const uint8_t *block_digest)
{
    lpx_t tag = lpx_make(LPX_B_FALSE, LPX_P_FORBIDDEN, phase);
    hwa_event_t ev =
        phase == LPX_PH_EMERGING && !block_digest ? HWA_EV_REJECTED : HWA_EV_QUARANTINED;
    if (!record(r, header, tag, ev, (uint8_t) why, hdr_version(header))) return HWR_E_HALTED;
    r->n_quarantined++;
    if (block_digest && r->nbad < HWR_MAX_BAD) cp32(r->bad[r->nbad++], block_digest);
    return HWR_OK;
}

static bool blocked(const hwr_region_t *r, const uint8_t d[32])
{
    for (uint32_t i = 0; i < r->nbad; i++)
        if (eq32(r->bad[i], d)) return true;
    return false;
}

/* ---- staging (R1-R4) ---- */

int hwr_stage(hwr_region_t *r, const uint8_t *img, uint32_t len)
{
    if (!r || !img) return HWR_E_ARG;
    if (r->state == HWR_ST_HALTED) return HWR_E_HALTED;
    if (r->state != HWR_ST_EMPTY && r->state != HWR_ST_ACTIVE) return HWR_E_STATE;
    hwr_image_t im;
    int rc = hwr_parse(img, len, &im);
    if (rc != HWR_OK) return rc;
    if (im.alg != r->alg) return HWR_E_FORMAT;
    if (im.kind == HWR_KIND_VECTORS) return HWR_E_KIND; /* contracts go to the matrix */
    /* R2 */
    if (im.target_class != r->target_class) return HWR_E_TARGET;
    if (!zero32(im.target_digest) && !eq32(im.target_digest, r->target_digest)) return HWR_E_TARGET;
    if (r->m && hwm_func_find(r->m, im.fn_id, 0) != HWM_OK) return HWR_E_FUNCTION;
    /* The payload must be what the header says before the signature is worth checking. */
    uint8_t d[32];
    sha256(im.payload, im.payload_len, d);
    if (!eq32(d, im.payload_sha256)) return HWR_E_FORMAT;
    if (blocked(r, d)) return HWR_E_BLOCKED;
    /* R1 */
    bool ok = false;
    for (uint32_t k = 0; k < r->nkeys && !ok; k++)
        ok = r->verify(r->verify_ctx, r->alg, r->keys[k], hwr_pk_len(r->alg), im.header, HWR_HEADER,
                       im.sig, im.sig_len);
    if (!ok) {
        rc = quarantine(r, im.header, LPX_PH_EMERGING, HWR_Q_SIGNATURE, 0);
        return rc == HWR_OK ? HWR_E_VERIFY : rc;
    }
    /* R3 */
    if ((r->active != HWR_NO_SLOT && im.version <= r->slot[r->active].version) ||
        im.svn < r->floor) {
        rc = quarantine(r, im.header, LPX_PH_EMERGING, HWR_Q_ROLLBACK, 0);
        return rc == HWR_OK ? HWR_E_ROLLBACK : rc;
    }
    /* R4 */
    uint32_t s = r->active == 0 ? 1u : 0u;
    r->slot[s].valid = false;
    if (r->ops->load(r->dev, s, im.payload, im.payload_len) != 0) return HWR_E_DEVICE;
    uint8_t got[32];
    if (r->ops->readback(r->dev, s, got) != 0 || !eq32(got, d)) {
        rc = quarantine(r, im.header, LPX_PH_EMERGING, HWR_Q_READBACK, d);
        return rc == HWR_OK ? HWR_E_READBACK : rc;
    }
    hwr_slot_t *sl = &r->slot[s];
    sl->valid = true;
    sl->version = im.version;
    sl->svn = im.svn;
    sl->fn_id = im.fn_id;
    cp32(sl->digest, d);
    /* R7: record before the region counts it as staged. */
    if (!record(r, im.header, lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_EMERGING), HWA_EV_STAGED,
                0, im.version)) {
        sl->valid = false;
        return HWR_E_HALTED;
    }
    r->staged = s;
    r->state = HWR_ST_STAGED;
    r->n_staged++;
    /* Keep the header for the records of the later steps. */
    for (uint32_t i = 0; i < HWR_HEADER; i++) r->staged_header[i] = im.header[i];
    return HWR_OK;
}

/* ---- trial, commit, rollback (R5, R6) ---- */

/* Point the matrix provider at slot `s` (or at nothing) and prove it. */
static lpres_state_t prove(hwr_region_t *r, uint32_t s)
{
    if (!r->m) return LPRES_STATE_TRUE; /* nothing to prove against: caller's matrix-free use */
    uint32_t fn;
    if (s == HWR_NO_SLOT || hwm_func_find(r->m, r->slot[s].fn_id, &fn) != HWM_OK) {
        hwm_provider_set_attest(r->m, r->provider, LPRES_STATE_NEITHER); /* S0 */
        return LPRES_STATE_NEITHER;
    }
    hwm_provider_retarget(r->m, r->provider, fn);
    return hwm_attest(r->m, r->provider, 0);
}

static void res_state(hwr_region_t *r, hwm_res_state_t st)
{
    if (r->m) hwm_resource_set(r->m, r->resource, st);
}

/* Back to the committed slot after a failed or abandoned new one. */
static void revert(hwr_region_t *r)
{
    uint32_t s = r->staged;
    res_state(r, HWM_RES_RECONFIGURING);
    if (s != HWR_NO_SLOT) r->slot[s].valid = false;
    r->staged = HWR_NO_SLOT;
    if (r->active != HWR_NO_SLOT && r->ops->activate(r->dev, r->active) == 0 &&
        prove(r, r->active) == LPRES_STATE_TRUE) {
        r->state = HWR_ST_ACTIVE;
    } else {
        /* Nothing proven to fall back to: S0, the emulators serve. */
        if (r->active != HWR_NO_SLOT) r->slot[r->active].valid = false;
        r->active = HWR_NO_SLOT;
        prove(r, HWR_NO_SLOT);
        r->state = HWR_ST_EMPTY;
    }
    res_state(r, HWM_RES_OK);
    r->n_rolled_back++;
}

static lpx_t rollback_tag(void)
{
    return lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_DISSOLVING);
}

static int fail_trial(hwr_region_t *r, hwr_qreason_t why, int status)
{
    uint8_t d[32];
    cp32(d, r->slot[r->staged].digest);
    revert(r);
    if (!record(r, r->staged_header, rollback_tag(), HWA_EV_ROLLED_BACK, (uint8_t) why,
                hdr_version(r->staged_header)))
        return HWR_E_HALTED;
    /* An expired probation is recorded but not blocked: nobody confirmed
     * it, which is not proof that it is bad. */
    int rc = quarantine(r, r->staged_header, LPX_PH_MANIFEST, why, why == HWR_Q_EXPIRED ? 0 : d);
    return rc == HWR_OK ? status : rc;
}

int hwr_trial(hwr_region_t *r, uint32_t probation_ticks)
{
    if (!r || probation_ticks == 0) return HWR_E_ARG;
    if (r->state == HWR_ST_HALTED) return HWR_E_HALTED;
    if (r->state != HWR_ST_STAGED) return HWR_E_STATE;
    res_state(r, HWM_RES_RECONFIGURING); /* R5: callers fail over meanwhile */
    if (r->ops->activate(r->dev, r->staged) != 0)
        return fail_trial(r, HWR_Q_ACTIVATE, HWR_E_ACTIVATE);
    lpres_state_t st = prove(r, r->staged);
    if (st != LPRES_STATE_TRUE) return fail_trial(r, HWR_Q_CONFORMANCE, HWR_E_CONFORMANCE);
    res_state(r, HWM_RES_OK);
    r->state = HWR_ST_PROBATION;
    r->probation_left = probation_ticks;
    if (!record(r, r->staged_header, lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_MANIFEST),
                HWA_EV_TESTED, 0, hdr_version(r->staged_header))) {
        revert(r); /* R7: an unrecorded image does not stay in service */
        r->state = HWR_ST_HALTED;
        return HWR_E_HALTED;
    }
    return HWR_OK;
}

int hwr_commit(hwr_region_t *r)
{
    if (!r) return HWR_E_ARG;
    if (r->state == HWR_ST_HALTED) return HWR_E_HALTED;
    if (r->state != HWR_ST_PROBATION) return HWR_E_STATE;
    if (!record(r, r->staged_header, lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST),
                HWA_EV_ACTIVATED, 0, hdr_version(r->staged_header))) {
        revert(r); /* R7: an activation that cannot be recorded is not made */
        r->state = HWR_ST_HALTED;
        return HWR_E_HALTED;
    }
    /* The old slot keeps its bytes until the next stage overwrites it. */
    r->active = r->staged;
    r->staged = HWR_NO_SLOT;
    if (r->slot[r->active].svn > r->floor) r->floor = r->slot[r->active].svn;
    r->state = HWR_ST_ACTIVE;
    r->probation_left = 0;
    r->n_committed++;
    return HWR_OK;
}

int hwr_rollback(hwr_region_t *r)
{
    if (!r) return HWR_E_ARG;
    if (r->state == HWR_ST_HALTED) return HWR_E_HALTED;
    if (r->state == HWR_ST_STAGED) {
        /* Nothing switched yet: drop the staged slot. */
        r->slot[r->staged].valid = false;
        r->staged = HWR_NO_SLOT;
        r->state = r->active == HWR_NO_SLOT ? HWR_ST_EMPTY : HWR_ST_ACTIVE;
    } else if (r->state == HWR_ST_PROBATION) {
        revert(r);
    } else {
        return HWR_E_STATE;
    }
    if (!record(r, r->staged_header, rollback_tag(), HWA_EV_ROLLED_BACK, 0,
                hdr_version(r->staged_header)))
        return HWR_E_HALTED;
    return HWR_OK;
}

int hwr_tick(hwr_region_t *r)
{
    if (!r) return HWR_E_ARG;
    if (r->state == HWR_ST_HALTED) return HWR_E_HALTED;
    if (r->state != HWR_ST_PROBATION) return HWR_OK;
    if (--r->probation_left > 0) return HWR_OK;
    return fail_trial(r, HWR_Q_EXPIRED, HWR_E_CONFORMANCE);
}

/* ---- contract packages ---- */

int hwr_package_verify(const uint8_t *img, uint32_t len, uint16_t alg, hwr_verify_fn verify,
                       void *verify_ctx, const uint8_t *const *keys, uint32_t nkeys,
                       hwr_image_t *out)
{
    if (!img || !verify || !out || (nkeys && !keys)) return HWR_E_ARG;
    int rc = hwr_parse(img, len, out);
    if (rc != HWR_OK) return rc;
    if (out->alg != alg) return HWR_E_FORMAT;
    if (out->kind != HWR_KIND_VECTORS) return HWR_E_KIND;
    uint8_t d[32];
    sha256(out->payload, out->payload_len, d);
    if (!eq32(d, out->payload_sha256)) return HWR_E_FORMAT;
    for (uint32_t k = 0; k < nkeys; k++)
        if (keys[k] && verify(verify_ctx, alg, keys[k], hwr_pk_len(alg), out->header, HWR_HEADER,
                              out->sig, out->sig_len))
            return HWR_OK;
    return HWR_E_VERIFY;
}

int hwr_vectors_next(const uint8_t *payload, uint32_t len, uint32_t *off, uint32_t *op,
                     const uint8_t **in, uint32_t *in_len, const uint8_t **out, uint32_t *out_len)
{
    if (!payload || !off || !op || !in || !in_len || !out || !out_len) return HWR_E_ARG;
    uint32_t o = *off;
    if (o == len) return 1;
    if (o > len || len - o < 8u) return HWR_E_FORMAT;
    *op = rd32(payload + o);
    uint32_t il = rd32(payload + o + 4);
    o += 8;
    if (il > len - o) return HWR_E_FORMAT;
    *in = payload + o;
    *in_len = il;
    o += il;
    if (len - o < 4u) return HWR_E_FORMAT;
    uint32_t ol = rd32(payload + o);
    o += 4;
    if (ol > 512u || ol > len - o) return HWR_E_FORMAT;
    *out = payload + o;
    *out_len = ol;
    *off = o + ol;
    return HWR_OK;
}

const char *hwr_state_name(hwr_state_t s)
{
    switch (s) {
    case HWR_ST_EMPTY:
        return "empty";
    case HWR_ST_ACTIVE:
        return "active";
    case HWR_ST_STAGED:
        return "staged";
    case HWR_ST_PROBATION:
        return "probation";
    case HWR_ST_HALTED:
        return "halted";
    default:
        return "invalid";
    }
}
