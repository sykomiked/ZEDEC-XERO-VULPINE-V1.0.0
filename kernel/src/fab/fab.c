/* fab.c — file fabrication: recipe parsing, key schedule, plan rules and
 * assembly (receiving side), plus the shared helpers fab_send.c uses.
 * Format and rules: fab.h and docs/spec/FABRICATION.md.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 * Integer only, no libc, no allocation.
 */
#include "fab.h"
#include "fab_int.h"

#include "../pqsec/pq_matrix_algs.h"
#include "../robin_debanks/sha256.h"
#include "../tls/aead.h"
#include "../tls/hkdf.h"

static const uint8_t FAB_MAGIC[4] = {'F', 'A', 'B', 'R'};
static const uint8_t FAB_FMAGIC[4] = {'F', 'A', 'B', 'F'};
static const uint8_t FAB_CMAGIC[4] = {'F', 'A', 'B', 'C'};

/* ---- byte helpers ------------------------------------------------------- */

void fab__cpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

void fab__wipe(void *dst, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

bool fab__eq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a;
    const uint8_t *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

bool fab__zero(const uint8_t *p, uint32_t n)
{
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= p[i];
    return acc == 0;
}

uint32_t fab__get32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

uint64_t fab__get64(const uint8_t *p)
{
    return ((uint64_t) fab__get32(p) << 32) | fab__get32(p + 4);
}

void fab__put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

void fab__put64(uint8_t *p, uint64_t v)
{
    fab__put32(p, (uint32_t) (v >> 32));
    fab__put32(p + 4, (uint32_t) v);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | p[1]);
}

/* ---- keys --------------------------------------------------------------- */

void fab_key_id(const uint8_t *pk, uint32_t len, uint8_t id[FAB_ID])
{
    sha256(pk, len, id);
}

void fab_sig_keypair(const uint8_t seed[32], uint8_t pk[FAB_SIG_PK], uint8_t sk[FAB_SIG_SK])
{
    pqm_mldsa87_keygen(seed, pk, sk);
}

void fab_kem_keypair(const uint8_t d[32], const uint8_t z[32], uint8_t ek[FAB_KEM_EK],
                     uint8_t dk[FAB_KEM_DK])
{
    pqm_mlkem1024_keygen(d, z, ek, dk);
}

/* prk = HKDF-Extract(SHA-256(header || ct), ss). */
void fab__prk(const uint8_t *hdr, const uint8_t *ct, const uint8_t ss[32], uint8_t prk[32])
{
    sha256_ctx_t c;
    uint8_t salt[32];
    sha256_init(&c);
    sha256_update(&c, hdr, FAB_HDR);
    sha256_update(&c, ct, FAB_KEM_CT);
    sha256_final(&c, salt);
    hkdf_extract(salt, sizeof salt, ss, 32, prk);
}

void fab__expand(const uint8_t prk[32], const char *label, uint8_t out[32])
{
    uint32_t n = 0;
    while (label[n]) n++;
    (void) hkdf_expand(prk, (const uint8_t *) label, n, out, 32);
}

void fab__frame_nonce(uint8_t which, uint64_t seq, uint8_t nonce[12])
{
    nonce[0] = which;
    nonce[1] = nonce[2] = nonce[3] = 0;
    fab__put64(nonce + 4, seq);
}

void fab__stream_init(fab_stream_t *s, const uint8_t prk[32], const uint8_t *sender_pk,
                      uint32_t slot_us, const ipfsn_cid_t *frame0)
{
    uint8_t idk[32];
    fab__expand(prk, "zxv.fab.v1 stream", s->key);
    fab__expand(prk, "zxv.fab.v1 stream id", idk);
    fab__cpy(s->id, idk, FAB_SID);
    fab__cpy(s->sender_pk, sender_pk, FAB_SIG_PK);
    s->slot_us = slot_us;
    s->seq = 0;
    s->prev_seq = 0;
    s->checked_seq = 0;
    for (uint32_t i = 0; i < FAB_CHAIN_RING; i++) s->ring_seq[i] = UINT64_MAX;
    fab__ring_push(s, 0, frame0);
    s->live = true;
}

void fab__ring_push(fab_stream_t *s, uint64_t seq, const ipfsn_cid_t *cid)
{
    uint32_t i = (uint32_t) (seq % FAB_CHAIN_RING);
    s->ring_seq[i] = seq;
    (void) ipfsn_cid_encode(cid, s->ring_cid[i], FAB_CID);
}

/* SHA-256 over (seq || cid) for from..to. false if any is not in the ring. */
bool fab__window(const fab_stream_t *s, uint64_t from, uint64_t to, uint8_t out[32])
{
    sha256_ctx_t c;
    uint8_t q[8];
    sha256_init(&c);
    for (uint64_t k = from;; k++) {
        uint32_t i = (uint32_t) (k % FAB_CHAIN_RING);
        if (s->ring_seq[i] != k) return false;
        fab__put64(q, k);
        sha256_update(&c, q, 8);
        sha256_update(&c, s->ring_cid[i], FAB_CID);
        if (k == to) break;
    }
    sha256_final(&c, out);
    return true;
}

/* ---- recipes ------------------------------------------------------------ */

int fab_recipe_parse(const uint8_t *buf, uint32_t len, fab_recipe_t *r)
{
    if (!buf || !r) return FAB_ERR_ARG;
    fab__wipe(r, sizeof *r);
    if (len < FAB_HDR) return FAB_ERR_FORM;
    if (!fab__eq(buf, FAB_MAGIC, 4) || buf[4] != FAB_VERSION || buf[7] != 0) return FAB_ERR_FORM;
    uint8_t kind = buf[5], mode = buf[6];
    if (kind > FAB_KIND_STREAM || mode > FAB_MODE_SEALED) return FAB_ERR_FORM;
    if (kind == FAB_KIND_STREAM && mode != FAB_MODE_SEALED) return FAB_ERR_FORM;
    if (mode == FAB_MODE_PUBLIC && !fab__zero(buf + 40, FAB_ID)) return FAB_ERR_FORM;
    if (mode == FAB_MODE_SEALED && fab__zero(buf + 40, FAB_ID)) return FAB_ERR_FORM;
    uint32_t plan_len = fab__get32(buf + 72), diff_len = fab__get32(buf + 76);
    if (plan_len < FAB_PLAN_HDR) return FAB_ERR_FORM;
    bool sealed = mode == FAB_MODE_SEALED;
    uint64_t want = (uint64_t) FAB_HDR + (sealed ? FAB_KEM_CT + 2u * FAB_TAG : 0u) + plan_len +
                    diff_len + FAB_SIG_PK + FAB_SIG;
    if (want != len) return FAB_ERR_FORM;

    uint32_t o = FAB_HDR;
    r->buf = buf;
    r->len = len;
    r->kind = kind;
    r->mode = mode;
    r->sender_id = buf + 8;
    r->recipient_id = buf + 40;
    r->plan_len = plan_len;
    r->diff_len = diff_len;
    if (sealed) {
        r->kem_ct = buf + o;
        o += FAB_KEM_CT;
    }
    r->plan = buf + o;
    o += plan_len;
    if (sealed) {
        r->plan_tag = buf + o;
        o += FAB_TAG;
    }
    r->diff = buf + o;
    o += diff_len;
    if (sealed) {
        r->diff_tag = buf + o;
        o += FAB_TAG;
    }
    r->signed_len = o;
    r->sender_pk = buf + o;
    r->sig = buf + o + FAB_SIG_PK;
    return FAB_OK;
}

int fab_recipe_check(fab_recipe_t *r, const uint8_t (*trusted)[FAB_ID], uint32_t n_trusted)
{
    if (!r || !r->buf) return FAB_ERR_ARG;
    if (!trusted && n_trusted) return FAB_ERR_ARG;
    r->checked = false;
    uint8_t id[FAB_ID];
    fab_key_id(r->sender_pk, FAB_SIG_PK, id);
    if (!fab__eq(id, r->sender_id, FAB_ID)) return FAB_ERR_SIGN;
    if (trusted) {
        bool hit = false;
        for (uint32_t i = 0; i < n_trusted; i++)
            if (fab__eq(trusted[i], id, FAB_ID)) hit = true;
        if (!hit) return FAB_ERR_TRUST;
    }
    uint8_t h[32];
    sha256(r->buf, r->signed_len, h);
    if (!pqm_mldsa87_verify(r->sender_pk, h, sizeof h, (const uint8_t *) FAB_RECIPE_CTX,
                            sizeof FAB_RECIPE_CTX - 1u, r->sig))
        return FAB_ERR_SIGN;
    r->checked = true;
    return FAB_OK;
}

/* Recipient check and decapsulation; prk out. */
static int recipe_prk(const fab_recipe_t *r, const uint8_t *dk, uint8_t prk[32])
{
    if (!dk) return FAB_ERR_ARG;
    if (!pqm_mlkem1024_check_dk(dk)) return FAB_ERR_KEY;
    uint8_t id[FAB_ID];
    fab_key_id(dk + FAB_KEM_DK - FAB_KEM_EK - 64u, FAB_KEM_EK, id);
    if (!fab__eq(id, r->recipient_id, FAB_ID)) return FAB_ERR_KEY;
    uint8_t ss[32];
    pqm_mlkem1024_decaps(dk, r->kem_ct, ss);
    fab__prk(r->buf, r->kem_ct, ss, prk);
    fab__wipe(ss, sizeof ss);
    return FAB_OK;
}

static const uint8_t NONCE0[12] = {0};

int fab_open_plan(const fab_recipe_t *r, const uint8_t *dk, uint8_t *plan_buf, uint32_t cap,
                  fab_plan_t *p)
{
    if (!r || !r->buf || !plan_buf || !p) return FAB_ERR_ARG;
    if (!r->checked) return FAB_ERR_SIGN;
    if (cap < r->plan_len) return FAB_ERR_SPACE;
    if (r->mode == FAB_MODE_PUBLIC) {
        fab__cpy(plan_buf, r->plan, r->plan_len);
    } else {
        uint8_t prk[32], k[32];
        int rc = recipe_prk(r, dk, prk);
        if (rc != FAB_OK) return rc;
        fab__expand(prk, "zxv.fab.v1 plan", k);
        fab__wipe(prk, sizeof prk);
        bool ok =
            aead_open(k, NONCE0, r->buf, FAB_HDR, r->plan, plan_buf, r->plan_len, r->plan_tag);
        fab__wipe(k, sizeof k);
        if (!ok) return FAB_ERR_AUTH;
    }
    uint8_t which = r->kind == FAB_KIND_STREAM ? FAB_PLAN_OPEN : FAB_PLAN_FILE;
    int rc = fab_plan_parse(plan_buf, r->plan_len, which, r->diff_len, p);
    if (rc != FAB_OK) fab__wipe(plan_buf, r->plan_len);
    return rc;
}

/* ---- plans -------------------------------------------------------------- */

static bool cid_ok(const uint8_t *b, ipfsn_cid_t *cid)
{
    if (ipfsn_cid_decode_exact(b, FAB_CID, cid) != IPFSN_OK) return false;
    return cid->version == 1 && cid->mh_code == IPFSN_MH_SHA2_256 && cid->digest_len == 32 &&
           (cid->codec == IPFSN_MC_RAW || cid->codec == IPFSN_MC_DAG_PB);
}

int fab_plan_parse(const uint8_t *plan, uint32_t len, uint8_t which, uint32_t diff_len,
                   fab_plan_t *p)
{
    if (!plan || !p || which > FAB_PLAN_FRAME) return FAB_ERR_ARG;
    fab__wipe(p, sizeof *p);
    if (len < FAB_PLAN_HDR) return FAB_ERR_PLAN;
    uint32_t nb = fab__get32(plan + 44), no = fab__get32(plan + 48);
    if (nb > FAB_MAX_BLOCKS || no > FAB_MAX_OPS) return FAB_ERR_PLAN;
    if ((uint64_t) FAB_PLAN_HDR + (uint64_t) nb * FAB_BLOCK_REC + (uint64_t) no * FAB_OP_REC != len)
        return FAB_ERR_PLAN;
    if (!cid_ok(plan, &p->target)) return FAB_ERR_PLAN;
    p->target_len = fab__get64(plan + 36);
    p->slot_us = fab__get32(plan + 52);
    if ((which == FAB_PLAN_OPEN) != (p->slot_us != 0)) return FAB_ERR_PLAN;
    if (!fab__zero(plan + 56, 8)) return FAB_ERR_PLAN;
    p->n_blocks = nb;
    p->n_ops = no;
    p->which = which;
    p->blocks = plan + FAB_PLAN_HDR;
    p->ops = p->blocks + nb * FAB_BLOCK_REC;

    for (uint32_t i = 0; i < nb; i++) {
        const uint8_t *b = p->blocks + i * FAB_BLOCK_REC;
        ipfsn_cid_t c;
        uint32_t bl = fab__get32(b + FAB_CID);
        if (!cid_ok(b, &c) || bl == 0 || bl > FAB_BLOCK_MAX) return FAB_ERR_PLAN;
        for (uint32_t j = 0; j < i; j++)
            if (fab__eq(p->blocks + j * FAB_BLOCK_REC, b, FAB_CID)) return FAB_ERR_PLAN;
    }

    uint64_t sum = 0, lit = 0, prev_need = 0;
    for (uint32_t i = 0; i < no; i++) {
        const uint8_t *op = p->ops + i * FAB_OP_REC;
        uint16_t blk = get16(op + 2);
        uint32_t src = fab__get32(op + 4), l = fab__get32(op + 8);
        if (op[1] != 0 || l == 0) return FAB_ERR_PLAN;
        switch (op[0]) {
        case FAB_OP_SLICE:
            if (blk >= nb) return FAB_ERR_PLAN;
            if ((uint64_t) src + l > fab__get32(p->blocks + blk * FAB_BLOCK_REC + FAB_CID))
                return FAB_ERR_PLAN;
            break;
        case FAB_OP_LIT:
            if (blk != 0 || src != 0) return FAB_ERR_PLAN;
            lit += l;
            break;
        case FAB_OP_PREV:
            if (which != FAB_PLAN_FRAME || blk != 0) return FAB_ERR_PLAN;
            if ((uint64_t) src + l > prev_need) prev_need = (uint64_t) src + l;
            break;
        default:
            return FAB_ERR_PLAN;
        }
        sum += l;
    }
    if (sum != p->target_len || lit != diff_len) return FAB_ERR_PLAN;
    p->lit_total = lit;
    p->prev_need = prev_need;
    return FAB_OK;
}

int fab_plan_block(const fab_plan_t *p, uint32_t i, ipfsn_cid_t *cid, uint32_t *len)
{
    if (!p || !cid || !len || i >= p->n_blocks) return FAB_ERR_ARG;
    const uint8_t *b = p->blocks + i * FAB_BLOCK_REC;
    if (ipfsn_cid_decode_exact(b, FAB_CID, cid) != IPFSN_OK) return FAB_ERR_PLAN;
    *len = fab__get32(b + FAB_CID);
    return FAB_OK;
}

/* The bytes of block i, checked against its CID and length. The source is
 * never trusted: whatever it returns is hashed here. */
static int block_bytes(const fab_plan_t *p, uint32_t i, fab_io_t *io, const uint8_t **bytes)
{
    fab_work_t *w = io->work;
    if (io->view && w->view[i]) {
        *bytes = w->view[i];
        return FAB_OK;
    }
    if (!io->view && w->loaded == (int32_t) i) {
        *bytes = w->block;
        return FAB_OK;
    }
    ipfsn_cid_t cid;
    uint32_t want, got = 0;
    int rc = fab_plan_block(p, i, &cid, &want);
    if (rc != FAB_OK) return rc;
    const uint8_t *b = w->block;
    if (io->view) {
        if (io->view(io->view_ctx, &cid, &b, &got) != 0 || !b) return FAB_ERR_FETCH;
    } else {
        w->loaded = -1;
        if (io->get(io->get_ctx, &cid, w->block, FAB_BLOCK_MAX, &got) != 0) return FAB_ERR_FETCH;
    }
    if (got != want || ipfsn_cid_verify(&cid, b, got) != IPFSN_OK) return FAB_ERR_BLOCK;
    if (io->view)
        w->view[i] = b;
    else
        w->loaded = (int32_t) i;
    *bytes = b;
    return FAB_OK;
}

static void forget(const fab_plan_t *p, fab_work_t *w)
{
    w->loaded = -1;
    for (uint32_t i = 0; i < p->n_blocks; i++) w->view[i] = 0;
}

int fab_plan_bytes(const fab_plan_t *p, uint32_t n, uint64_t *bytes)
{
    if (!p || !bytes || n > p->n_blocks) return FAB_ERR_ARG;
    uint64_t t = 0;
    for (uint32_t i = 0; i < n; i++) t += fab__get32(p->blocks + i * FAB_BLOCK_REC + FAB_CID);
    *bytes = t;
    return FAB_OK;
}

static void report(fab_io_t *io, uint8_t phase, uint32_t blocks_done, uint32_t blocks_total,
                   uint64_t bytes_done, uint64_t bytes_total)
{
    if (!io->progress) return;
    fab_progress_t pr = {phase, blocks_done, blocks_total, bytes_done, bytes_total};
    io->progress(io->progress_ctx, &pr);
}

int fab_hydrate_from(const fab_plan_t *p, fab_io_t *io, uint32_t from, uint32_t n,
                     uint32_t *fetched)
{
    if (!p || !io || (!io->get && !io->view) || !io->work || from > p->n_blocks) return FAB_ERR_ARG;
    uint32_t end = n > p->n_blocks - from ? p->n_blocks : from + n, i = from;
    uint64_t done, total;
    (void) fab_plan_bytes(p, from, &done);
    (void) fab_plan_bytes(p, p->n_blocks, &total);
    forget(p, io->work);
    int rc = FAB_OK;
    for (; i < end; i++) {
        const uint8_t *b;
        rc = block_bytes(p, i, io, &b);
        if (rc != FAB_OK) break;
        done += fab__get32(p->blocks + i * FAB_BLOCK_REC + FAB_CID);
        report(io, FAB_PHASE_FETCH, i + 1u, p->n_blocks, done, total);
    }
    if (fetched) *fetched = i;
    return rc;
}

int fab_hydrate(const fab_plan_t *p, fab_io_t *io, uint32_t *fetched)
{
    return p ? fab_hydrate_from(p, io, 0, p->n_blocks, fetched) : FAB_ERR_ARG;
}

static int no_sink(void *ctx, const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len)
{
    (void) ctx;
    (void) cid;
    (void) block;
    (void) len;
    return 0;
}

int fab_target_cid(const uint8_t *data, uint32_t len, fab_work_t *w, ipfsn_cid_t *cid)
{
    if ((!data && len) || !w || !cid) return FAB_ERR_ARG;
    uint64_t size = 0;
    if (ipfsn_ufs_init(&w->ufs, w->chunk, 0, 0, no_sink, 0) != IPFSN_OK) return FAB_ERR_ARG;
    if (len && ipfsn_ufs_update(&w->ufs, data, len) != IPFSN_OK) return FAB_ERR_ARG;
    if (ipfsn_ufs_final(&w->ufs, cid, &size) != IPFSN_OK || size != len) return FAB_ERR_ARG;
    return FAB_OK;
}

/* Apply the ops into io->out, then check the target CID. */
static int apply(const fab_plan_t *p, fab_io_t *io, const uint8_t *diff, const uint8_t *prev,
                 uint32_t prev_len)
{
    uint32_t pos = 0, dpos = 0;
    for (uint32_t i = 0; i < p->n_ops; i++) {
        const uint8_t *op = p->ops + i * FAB_OP_REC;
        uint16_t blk = get16(op + 2);
        uint32_t src = fab__get32(op + 4), l = fab__get32(op + 8);
        if (op[0] == FAB_OP_SLICE) {
            const uint8_t *b;
            int rc = block_bytes(p, blk, io, &b);
            if (rc != FAB_OK) return rc;
            fab__cpy(io->out + pos, b + src, l);
        } else if (op[0] == FAB_OP_LIT) {
            fab__cpy(io->out + pos, diff + dpos, l);
            dpos += l;
        } else {
            if (!prev || (uint64_t) src + l > prev_len) return FAB_ERR_RESYNC;
            fab__cpy(io->out + pos, prev + src, l);
        }
        pos += l;
        report(io, FAB_PHASE_BUILD, p->n_blocks, p->n_blocks, pos, p->target_len);
    }
    ipfsn_cid_t got;
    int rc = fab_target_cid(io->out, pos, io->work, &got);
    if (rc != FAB_OK) return rc;
    if (!ipfsn_cid_equal(&got, &p->target)) return FAB_ERR_TARGET;
    report(io, FAB_PHASE_DONE, p->n_blocks, p->n_blocks, pos, p->target_len);
    return FAB_OK;
}

static int io_ok(const fab_io_t *io)
{
    if (!io || (!io->get && !io->view) || !io->work || !io->out || (!io->diff_buf && io->diff_cap))
        return FAB_ERR_ARG;
    return FAB_OK;
}

static int seal_ok(const fab_io_t *io)
{
    if (!io->seal) return FAB_OK;
    return io->seal(io->seal_ctx) == LPRES_STATE_TRUE ? FAB_OK : FAB_ERR_SEAL;
}

int fab_assemble(const fab_recipe_t *r, const fab_plan_t *p, const uint8_t *dk, fab_io_t *io,
                 fab_stream_t *s, uint32_t *out_len)
{
    if (!r || !r->buf || !p || !out_len || io_ok(io) != FAB_OK) return FAB_ERR_ARG;
    if (!r->checked) return FAB_ERR_SIGN;
    if ((r->kind == FAB_KIND_STREAM) != (s != 0)) return FAB_ERR_ARG;
    uint8_t which = r->kind == FAB_KIND_STREAM ? FAB_PLAN_OPEN : FAB_PLAN_FILE;
    if (p->which != which || p->lit_total != r->diff_len) return FAB_ERR_ARG;
    if (p->target_len > io->out_cap) return FAB_ERR_SPACE;
    if (r->diff_len > io->diff_cap) return FAB_ERR_SPACE;
    *out_len = 0;
    int rc = seal_ok(io);
    if (rc != FAB_OK) return rc;

    uint8_t prk[32];
    if (r->mode == FAB_MODE_PUBLIC) {
        fab__cpy(io->diff_buf, r->diff, r->diff_len);
    } else {
        uint8_t k[32];
        rc = recipe_prk(r, dk, prk);
        if (rc != FAB_OK) return rc;
        fab__expand(prk, "zxv.fab.v1 diff", k);
        bool ok =
            aead_open(k, NONCE0, r->buf, FAB_HDR, r->diff, io->diff_buf, r->diff_len, r->diff_tag);
        fab__wipe(k, sizeof k);
        if (!ok) {
            fab__wipe(prk, sizeof prk);
            return FAB_ERR_AUTH;
        }
    }
    forget(p, io->work);
    rc = apply(p, io, io->diff_buf, 0, 0);
    if (rc != FAB_OK) {
        fab__wipe(io->out, (uint32_t) p->target_len);
        fab__wipe(io->diff_buf, r->diff_len);
        fab__wipe(prk, sizeof prk);
        return rc;
    }
    if (s) fab__stream_init(s, prk, r->sender_pk, p->slot_us, &p->target);
    fab__wipe(prk, sizeof prk);
    *out_len = (uint32_t) p->target_len;
    return FAB_OK;
}

/* ---- frames ------------------------------------------------------------- */

int fab__frame_parse(const uint8_t *f, uint32_t len, uint32_t *plan_len, uint32_t *diff_len)
{
    if (len < FAB_FHDR) return FAB_ERR_FORM;
    if (!fab__eq(f, FAB_FMAGIC, 4) || f[4] != FAB_VERSION || !fab__zero(f + 5, 3))
        return FAB_ERR_FORM;
    *plan_len = fab__get32(f + 48);
    *diff_len = fab__get32(f + 52);
    if (*plan_len < FAB_PLAN_HDR) return FAB_ERR_FORM;
    if ((uint64_t) FAB_FHDR + *plan_len + *diff_len + 2u * FAB_TAG != len) return FAB_ERR_FORM;
    return FAB_OK;
}

static bool overlap(const uint8_t *a, uint32_t al, const uint8_t *b, uint32_t bl)
{
    if (!a || !b || !al || !bl) return false;
    return a < b + bl && b < a + al;
}

int fab_frame_apply(fab_stream_t *s, const uint8_t *frame, uint32_t len, const uint8_t *prev,
                    uint32_t prev_len, uint8_t *plan_buf, uint32_t plan_cap, fab_io_t *io,
                    uint32_t *out_len)
{
    if (!s || !frame || !plan_buf || !out_len || io_ok(io) != FAB_OK) return FAB_ERR_ARG;
    if (!s->live) return FAB_ERR_STREAM;
    if (overlap(prev, prev_len, io->out, io->out_cap)) return FAB_ERR_ARG;
    *out_len = 0;
    uint32_t pl, dl;
    int rc = fab__frame_parse(frame, len, &pl, &dl);
    if (rc != FAB_OK) return rc;
    if (!fab__eq(frame + 8, s->id, FAB_SID)) return FAB_ERR_STREAM;
    uint64_t seq = fab__get64(frame + 24), base = fab__get64(frame + 32);
    if (seq <= s->seq) return FAB_ERR_REPLAY;
    if (pl > plan_cap || dl > io->diff_cap) return FAB_ERR_SPACE;
    rc = seal_ok(io);
    if (rc != FAB_OK) return rc;

    uint8_t nonce[12];
    const uint8_t *body = frame + FAB_FHDR;
    fab__frame_nonce(1, seq, nonce);
    if (!aead_open(s->key, nonce, frame, FAB_FHDR, body, plan_buf, pl, body + pl))
        return FAB_ERR_AUTH;
    body += pl + FAB_TAG;
    fab__frame_nonce(2, seq, nonce);
    if (!aead_open(s->key, nonce, frame, FAB_FHDR, body, io->diff_buf, dl, body + dl)) {
        fab__wipe(plan_buf, pl);
        return FAB_ERR_AUTH;
    }
    fab_plan_t p;
    rc = fab_plan_parse(plan_buf, pl, FAB_PLAN_FRAME, dl, &p);
    if (rc == FAB_OK && p.prev_need && (base != s->prev_seq || !prev || prev_len < p.prev_need))
        rc = FAB_ERR_RESYNC;
    if (rc == FAB_OK && p.target_len > io->out_cap) rc = FAB_ERR_SPACE;
    if (rc == FAB_OK) {
        forget(&p, io->work);
        rc = apply(&p, io, io->diff_buf, prev, prev_len);
        if (rc != FAB_OK) fab__wipe(io->out, (uint32_t) p.target_len);
    }
    if (rc != FAB_OK) {
        fab__wipe(plan_buf, pl);
        fab__wipe(io->diff_buf, dl);
        return rc;
    }
    s->seq = seq;
    s->prev_seq = seq;
    fab__ring_push(s, seq, &p.target);
    *out_len = (uint32_t) p.target_len;
    return FAB_OK;
}

void fab__cp_body(const fab_stream_t *s, uint64_t from, uint64_t to, const uint8_t digest[32],
                  uint8_t out[FAB_CP_BODY])
{
    fab__cpy(out, FAB_CMAGIC, 4);
    out[4] = FAB_VERSION;
    out[5] = out[6] = out[7] = 0;
    fab__cpy(out + 8, s->id, FAB_SID);
    fab__put64(out + 24, from);
    fab__put64(out + 32, to);
    fab__cpy(out + 40, digest, 32);
}

int fab_checkpoint_verify(fab_stream_t *s, const uint8_t *cp, uint32_t len)
{
    if (!s || !cp) return FAB_ERR_ARG;
    if (!s->live) return FAB_ERR_STREAM;
    if (len != FAB_CP_LEN || !fab__eq(cp, FAB_CMAGIC, 4) || cp[4] != FAB_VERSION ||
        !fab__zero(cp + 5, 3))
        return FAB_ERR_FORM;
    if (!fab__eq(cp + 8, s->id, FAB_SID)) return FAB_ERR_STREAM;
    uint64_t from = fab__get64(cp + 24), to = fab__get64(cp + 32);
    if (from > to || to - from >= FAB_CHAIN_RING) return FAB_ERR_FORM;
    if (!pqm_mldsa87_verify(s->sender_pk, cp, FAB_CP_BODY, (const uint8_t *) FAB_CP_CTX,
                            sizeof FAB_CP_CTX - 1u, cp + FAB_CP_BODY))
        return FAB_ERR_SIGN;
    uint8_t d[32];
    if (!fab__window(s, from, to, d)) return FAB_ERR_RESYNC;
    if (!fab__eq(d, cp + 40, 32)) return FAB_ERR_STREAM;
    if (to > s->checked_seq) s->checked_seq = to;
    return FAB_OK;
}
