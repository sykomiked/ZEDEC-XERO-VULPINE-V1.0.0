/* fab_send.c — file fabrication, sending side: the block matcher that turns
 * a target into ops plus a diff, plan writing, recipe and frame sealing,
 * and stream checkpoints. Format and rules: fab.h.
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

/* ---- matcher ------------------------------------------------------------ */

#define FAB_HASH_B 0x01000193u /* odd multiplier: the polynomial hash is mod 2^32 */
#define FAB_PROBES 8u

static uint32_t win_hash(const uint8_t *p, uint32_t w)
{
    uint32_t h = 0;
    for (uint32_t i = 0; i < w; i++) h = h * FAB_HASH_B + p[i];
    return h;
}

static uint32_t slot_of(uint32_t h, uint32_t bits)
{
    return (h * 0x9E3779B1u) >> (32u - bits);
}

typedef struct {
    uint8_t *ops;
    uint32_t cap;
    uint32_t n;
    uint8_t *diff;
    uint32_t diff_cap;
    uint32_t dlen;
    uint32_t nsrc;
    bool last_prev;
} emit_t;

static void put_op(uint8_t *op, uint8_t kind, uint16_t blk, uint32_t src, uint32_t len)
{
    op[0] = kind;
    op[1] = 0;
    op[2] = (uint8_t) (blk >> 8);
    op[3] = (uint8_t) blk;
    fab__put32(op + 4, src);
    fab__put32(op + 8, len);
}

/* Append an op. Ops never need merging: a match is extended forward until
 * a byte differs (or the target or source ends), so the next op can never
 * continue it, and a literal run always ends at the next match. */
static int emit(emit_t *e, uint8_t kind, uint16_t blk, uint32_t src, uint32_t len)
{
    if (e->n >= e->cap || e->n >= FAB_MAX_OPS) return FAB_ERR_SPACE;
    put_op(e->ops + e->n * FAB_OP_REC, kind, blk, src, len);
    e->n++;
    return FAB_OK;
}

static int emit_lit(emit_t *e, const uint8_t *t, uint32_t from, uint32_t to)
{
    if (to == from) return FAB_OK;
    uint32_t n = to - from;
    if (n > e->diff_cap - e->dlen) return FAB_ERR_SPACE;
    fab__cpy(e->diff + e->dlen, t + from, n);
    e->dlen += n;
    return emit(e, FAB_OP_LIT, 0, 0, n);
}

static int emit_src(emit_t *e, uint16_t s, uint32_t off, uint32_t len)
{
    bool prev = e->last_prev && s == e->nsrc - 1u;
    return emit(e, prev ? FAB_OP_PREV : FAB_OP_SLICE, prev ? 0 : s, off, len);
}

int fab_build(const fab_src_t *src, uint32_t nsrc, const uint8_t *target, uint32_t tlen,
              uint32_t window, fab_ient_t *table, uint32_t table_bits, uint8_t *ops,
              uint32_t ops_cap, uint32_t *n_ops, uint8_t *diff, uint32_t diff_cap,
              uint32_t *diff_len)
{
    if ((!src && nsrc) || (!target && tlen) || !table || !ops || !n_ops || !diff_len ||
        (!diff && diff_cap))
        return FAB_ERR_ARG;
    if (window < FAB_WINDOW_MIN || table_bits < 4 || table_bits > 24) return FAB_ERR_ARG;
    bool last_prev = nsrc && src[nsrc - 1u].prev;
    if (nsrc - (last_prev ? 1u : 0u) > FAB_MAX_BLOCKS) return FAB_ERR_ARG; /* so nsrc fits u16 */
    for (uint32_t i = 0; i < nsrc; i++) {
        if (!src[i].data && src[i].len) return FAB_ERR_ARG;
        if (src[i].prev && i != nsrc - 1u) return FAB_ERR_ARG;
        if (!src[i].prev && (src[i].len == 0 || src[i].len > FAB_BLOCK_MAX)) return FAB_ERR_ARG;
    }

    uint32_t tsize = 1u << table_bits;
    for (uint32_t i = 0; i < tsize; i++) table[i].used = 0;
    for (uint32_t s = 0; s < nsrc; s++)
        for (uint32_t off = 0; off + window <= src[s].len; off += window) {
            uint32_t h = win_hash(src[s].data + off, window);
            uint32_t k = slot_of(h, table_bits);
            for (uint32_t p = 0; p < FAB_PROBES; p++) {
                fab_ient_t *t = &table[(k + p) & (tsize - 1u)];
                if (!t->used) {
                    t->hash = h;
                    t->off = off;
                    t->src = (uint16_t) s;
                    t->used = 1;
                    break;
                }
            }
        }

    emit_t e = {ops, ops_cap, 0, diff, diff_cap, 0, nsrc, last_prev};
    uint32_t bw1 = 1; /* B^(window-1) */
    for (uint32_t i = 1; i < window; i++) bw1 *= FAB_HASH_B;

    uint32_t i = 0, lit = 0, h = 0;
    bool have = false;
    int rc = FAB_OK;
    while (i + window <= tlen) {
        if (!have) {
            h = win_hash(target + i, window);
            have = true;
        }
        uint32_t best = 0, bs = 0, bo = 0, back = 0;
        uint32_t k = slot_of(h, table_bits);
        for (uint32_t p = 0; p < FAB_PROBES; p++) {
            const fab_ient_t *t = &table[(k + p) & (tsize - 1u)];
            if (!t->used) break;
            if (t->hash != h) continue;
            const fab_src_t *sp = &src[t->src];
            if (!fab__eq(sp->data + t->off, target + i, window)) continue;
            uint32_t l = window;
            while (i + l < tlen && t->off + l < sp->len && sp->data[t->off + l] == target[i + l])
                l++;
            uint32_t b = 0;
            while (i - b > lit && t->off > b && sp->data[t->off - b - 1u] == target[i - b - 1u])
                b++;
            if (l + b > best) {
                best = l + b;
                bs = t->src;
                bo = t->off - b;
                back = b;
            }
        }
        if (best) {
            uint32_t at = i - back;
            if ((rc = emit_lit(&e, target, lit, at)) != FAB_OK) return rc;
            if ((rc = emit_src(&e, (uint16_t) bs, bo, best)) != FAB_OK) return rc;
            i = at + best;
            lit = i;
            have = false;
            continue;
        }
        if (i + window < tlen) h = (h - target[i] * bw1) * FAB_HASH_B + target[i + window];
        i++;
    }
    if ((rc = emit_lit(&e, target, lit, tlen)) != FAB_OK) return rc;
    *n_ops = e.n;
    *diff_len = e.dlen;
    return FAB_OK;
}

int fab_plan_write(const ipfsn_cid_t *target, uint64_t tlen, uint32_t slot_us, const fab_src_t *src,
                   uint32_t nsrc, const uint8_t *ops, uint32_t n_ops, uint8_t *out, uint32_t cap,
                   uint32_t *len)
{
    if (!target || (!src && nsrc) || (!ops && n_ops) || !out || !len) return FAB_ERR_ARG;
    uint32_t nb = nsrc && src[nsrc - 1u].prev ? nsrc - 1u : nsrc;
    if (nb > FAB_MAX_BLOCKS || n_ops > FAB_MAX_OPS) return FAB_ERR_ARG;
    uint64_t total =
        (uint64_t) FAB_PLAN_HDR + (uint64_t) nb * FAB_BLOCK_REC + (uint64_t) n_ops * FAB_OP_REC;
    if (total > cap) return FAB_ERR_SPACE;
    fab__wipe(out, FAB_PLAN_HDR);
    if (ipfsn_cid_encode(target, out, FAB_CID) != FAB_CID) return FAB_ERR_ARG;
    fab__put64(out + 36, tlen);
    fab__put32(out + 44, nb);
    fab__put32(out + 48, n_ops);
    fab__put32(out + 52, slot_us);
    uint8_t *b = out + FAB_PLAN_HDR;
    for (uint32_t i = 0; i < nb; i++, b += FAB_BLOCK_REC) {
        if (src[i].prev || ipfsn_cid_encode(&src[i].cid, b, FAB_CID) != FAB_CID) return FAB_ERR_ARG;
        fab__put32(b + FAB_CID, src[i].len);
    }
    fab__cpy(b, ops, n_ops * FAB_OP_REC);
    *len = (uint32_t) total;
    return FAB_OK;
}

/* ---- recipes ------------------------------------------------------------ */

static const uint8_t NONCE0[12] = {0};

int fab_recipe_seal(const fab_seal_args_t *a, const uint8_t *plan, uint32_t plan_len,
                    const uint8_t *diff, uint32_t diff_len, fab_stream_t *s, uint8_t *out,
                    uint32_t cap, uint32_t *len)
{
    if (!a || !plan || (!diff && diff_len) || !out || !len) return FAB_ERR_ARG;
    if (!a->sender_pk || !a->sender_sk || a->kind > FAB_KIND_STREAM || a->mode > FAB_MODE_SEALED)
        return FAB_ERR_ARG;
    bool sealed = a->mode == FAB_MODE_SEALED;
    if (sealed && (!a->recipient_ek || !a->kem_coins)) return FAB_ERR_ARG;
    if (a->kind == FAB_KIND_STREAM && (!sealed || !s)) return FAB_ERR_ARG;
    if (a->kind == FAB_KIND_FILE && s) return FAB_ERR_ARG;

    fab_plan_t p;
    uint8_t which = a->kind == FAB_KIND_STREAM ? FAB_PLAN_OPEN : FAB_PLAN_FILE;
    int rc = fab_plan_parse(plan, plan_len, which, diff_len, &p);
    if (rc != FAB_OK) return rc;

    uint64_t total = (uint64_t) FAB_HDR + (sealed ? FAB_KEM_CT + 2u * FAB_TAG : 0u) + plan_len +
                     diff_len + FAB_SIG_PK + FAB_SIG;
    if (total > cap) return FAB_ERR_SPACE;

    out[0] = 'F';
    out[1] = 'A';
    out[2] = 'B';
    out[3] = 'R';
    out[4] = FAB_VERSION;
    out[5] = a->kind;
    out[6] = a->mode;
    out[7] = 0;
    fab_key_id(a->sender_pk, FAB_SIG_PK, out + 8);
    if (sealed)
        fab_key_id(a->recipient_ek, FAB_KEM_EK, out + 40);
    else
        fab__wipe(out + 40, FAB_ID);
    fab__put32(out + 72, plan_len);
    fab__put32(out + 76, diff_len);

    uint32_t o = FAB_HDR;
    if (sealed) {
        uint8_t ss[32], prk[32], k[32];
        uint8_t *ct = out + o;
        if (!pqm_mlkem1024_encaps(a->recipient_ek, a->kem_coins, ct, ss)) return FAB_ERR_KEY;
        o += FAB_KEM_CT;
        fab__prk(out, ct, ss, prk);
        fab__wipe(ss, sizeof ss);
        fab__expand(prk, "zxv.fab.v1 plan", k);
        aead_seal(k, NONCE0, out, FAB_HDR, plan, out + o, plan_len, out + o + plan_len);
        o += plan_len + FAB_TAG;
        fab__expand(prk, "zxv.fab.v1 diff", k);
        aead_seal(k, NONCE0, out, FAB_HDR, diff, out + o, diff_len, out + o + diff_len);
        o += diff_len + FAB_TAG;
        fab__wipe(k, sizeof k);
        if (s) fab__stream_init(s, prk, a->sender_pk, p.slot_us, &p.target);
        fab__wipe(prk, sizeof prk);
    } else {
        fab__cpy(out + o, plan, plan_len);
        o += plan_len;
        if (diff_len) fab__cpy(out + o, diff, diff_len);
        o += diff_len;
    }
    fab__cpy(out + o, a->sender_pk, FAB_SIG_PK);
    uint8_t h[32];
    sha256(out, o, h);
    pqm_mldsa87_sign(a->sender_sk, h, sizeof h, (const uint8_t *) FAB_RECIPE_CTX,
                     sizeof FAB_RECIPE_CTX - 1u, a->sig_rnd, out + o + FAB_SIG_PK);
    *len = (uint32_t) total;
    return FAB_OK;
}

/* ---- frames and checkpoints ---------------------------------------------- */

int fab_frame_seal(fab_stream_t *s, uint64_t base, uint64_t slot, const uint8_t *plan,
                   uint32_t plan_len, const uint8_t *diff, uint32_t diff_len, uint8_t *out,
                   uint32_t cap, uint32_t *len)
{
    if (!s || !plan || (!diff && diff_len) || !out || !len) return FAB_ERR_ARG;
    if (!s->live) return FAB_ERR_STREAM;
    if (s->seq == UINT64_MAX || base > s->seq) return FAB_ERR_ARG;
    fab_plan_t p;
    int rc = fab_plan_parse(plan, plan_len, FAB_PLAN_FRAME, diff_len, &p);
    if (rc != FAB_OK) return rc;
    uint64_t total = (uint64_t) FAB_FHDR + plan_len + diff_len + 2u * FAB_TAG;
    if (total > cap) return FAB_ERR_SPACE;
    uint64_t seq = s->seq + 1u;

    out[0] = 'F';
    out[1] = 'A';
    out[2] = 'B';
    out[3] = 'F';
    out[4] = FAB_VERSION;
    out[5] = out[6] = out[7] = 0;
    fab__cpy(out + 8, s->id, FAB_SID);
    fab__put64(out + 24, seq);
    fab__put64(out + 32, base);
    fab__put64(out + 40, slot);
    fab__put32(out + 48, plan_len);
    fab__put32(out + 52, diff_len);
    uint8_t nonce[12];
    uint8_t *body = out + FAB_FHDR;
    fab__frame_nonce(1, seq, nonce);
    aead_seal(s->key, nonce, out, FAB_FHDR, plan, body, plan_len, body + plan_len);
    body += plan_len + FAB_TAG;
    fab__frame_nonce(2, seq, nonce);
    aead_seal(s->key, nonce, out, FAB_FHDR, diff, body, diff_len, body + diff_len);
    s->seq = seq;
    s->prev_seq = seq;
    fab__ring_push(s, seq, &p.target);
    *len = (uint32_t) total;
    return FAB_OK;
}

int fab_checkpoint_sign(const fab_stream_t *s, uint64_t from, uint64_t to, const uint8_t *sender_sk,
                        const uint8_t *rnd, uint8_t out[FAB_CP_LEN])
{
    if (!s || !sender_sk || !out) return FAB_ERR_ARG;
    if (!s->live) return FAB_ERR_STREAM;
    if (from > to || to - from >= FAB_CHAIN_RING) return FAB_ERR_ARG;
    uint8_t d[32];
    if (!fab__window(s, from, to, d)) return FAB_ERR_RESYNC;
    fab__cp_body(s, from, to, d, out);
    pqm_mldsa87_sign(sender_sk, out, FAB_CP_BODY, (const uint8_t *) FAB_CP_CTX,
                     sizeof FAB_CP_CTX - 1u, rnd, out + FAB_CP_BODY);
    return FAB_OK;
}
