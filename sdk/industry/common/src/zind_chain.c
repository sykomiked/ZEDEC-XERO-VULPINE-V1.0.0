/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zind_chain.c — ML-DSA-87 signers and append-only records chained with
 * SHA3-256, with export, import and offline verification. The record
 * layout and hash are documented in zind_common.h. Integer code only
 * (R11). */
#include <stdlib.h>
#include <string.h>

#include "zind_common.h"

/* ===== signers ===== */

struct zind_signer {
    uint8_t pk[ZIND_PK_BYTES];
    uint8_t sk[ZXV_MLDSA87_SK_BYTES];
    uint8_t id[32];
};

int32_t zind_key_id(const uint8_t pk[ZIND_PK_BYTES], uint8_t out[32]) {
    static const char tag[] = "zind/key/v1";
    uint8_t *buf;
    int32_t st;
    if (!pk || !out) return ZXV_E_ARG;
    buf = (uint8_t *) malloc(sizeof tag - 1 + ZIND_PK_BYTES);
    if (!buf) return ZXV_E_NOMEM;
    memcpy(buf, tag, sizeof tag - 1);
    memcpy(buf + sizeof tag - 1, pk, ZIND_PK_BYTES);
    st = zxv_sha3_256(buf, sizeof tag - 1 + ZIND_PK_BYTES, out);
    free(buf);
    return st;
}

static int32_t signer_finish(zind_signer *s, int32_t st, zind_signer **out) {
    if (st == ZXV_OK) st = zind_key_id(s->pk, s->id);
    if (st != ZXV_OK) {
        zind_set_error("zind_signer: key generation failed (%s); the cap needs CRYPTO_SIGN",
                       zxv_status_name(st));
        zind_signer_destroy(s);
        return st;
    }
    *out = s;
    return ZXV_OK;
}

int32_t zind_signer_from_seed(const zxv_cap *cap, const uint8_t seed[32], zind_signer **out) {
    zind_signer *s;
    if (!cap || !seed || !out) return ZXV_E_ARG;
    s = (zind_signer *) calloc(1, sizeof *s);
    if (!s) return ZXV_E_NOMEM;
    return signer_finish(s, zxv_mldsa87_keygen_seed(cap, seed, s->pk, s->sk), out);
}

int32_t zind_signer_generate(const zxv_cap *cap, zind_signer **out) {
    zind_signer *s;
    if (!cap || !out) return ZXV_E_ARG;
    s = (zind_signer *) calloc(1, sizeof *s);
    if (!s) return ZXV_E_NOMEM;
    return signer_finish(s, zxv_mldsa87_keygen(cap, s->pk, s->sk), out);
}

void zind_signer_destroy(zind_signer *s) {
    if (!s) return;
    (void) zxv_secure_zero(s->sk, sizeof s->sk);
    free(s);
}

const uint8_t *zind_signer_pk(const zind_signer *s) { return s ? s->pk : NULL; }
const uint8_t *zind_signer_key_id(const zind_signer *s) { return s ? s->id : NULL; }

static int ctx_ok(const char *context, size_t *out_len) {
    size_t n = context ? strlen(context) : 0;
    if (n > ZXV_MLDSA_CTX_MAX) return 0;
    *out_len = n;
    return 1;
}

int32_t zind_sign(const zxv_cap *cap, const zind_signer *s, const uint8_t *msg, size_t len,
                  const char *context, uint8_t sig[ZIND_SIG_BYTES]) {
    size_t cl = 0;
    if (!cap || !s || (!msg && len) || !sig || !ctx_ok(context, &cl)) return ZXV_E_ARG;
    return zxv_mldsa87_sign(cap, s->sk, msg, len, (const uint8_t *) context, cl, sig);
}

int32_t zind_verify(const zxv_cap *cap, const uint8_t pk[ZIND_PK_BYTES], const uint8_t *msg,
                    size_t len, const char *context, const uint8_t sig[ZIND_SIG_BYTES]) {
    size_t cl = 0;
    if (!cap || !pk || (!msg && len) || !sig || !ctx_ok(context, &cl)) return ZXV_E_ARG;
    return zxv_mldsa87_verify(cap, pk, msg, len, (const uint8_t *) context, cl, sig);
}

/* ===== subjects ===== */

static int domain_ok(const char *d) {
    size_t n;
    if (!d) return 0;
    for (n = 0; d[n]; n++)
        if ((unsigned char) d[n] < 0x21 || (unsigned char) d[n] > 0x7e || n >= ZIND_DOMAIN_MAX)
            return 0;
    return n > 0;
}

int32_t zind_subject(const char *domain, const uint8_t *data, size_t len, uint8_t out[32]) {
    uint8_t stackbuf[512], *buf = stackbuf;
    size_t dl, total;
    int32_t st;
    if (!domain_ok(domain) || (!data && len) || !out) return ZXV_E_ARG;
    dl = strlen(domain);
    if (len > SIZE_MAX - 6 - dl) return ZXV_E_RANGE;
    total = 5 + dl + 1 + len;
    if (total > sizeof stackbuf) {
        buf = (uint8_t *) malloc(total);
        if (!buf) return ZXV_E_NOMEM;
    }
    memcpy(buf, "zind/", 5);
    memcpy(buf + 5, domain, dl);
    buf[5 + dl] = 0;
    if (len) memcpy(buf + 6 + dl, data, len);
    st = zxv_sha3_256(buf, total, out);
    if (buf != stackbuf) free(buf);
    return st;
}

/* ===== seen-sets ===== */

struct zind_seen {
    uint32_t capacity, count;
    uint32_t mask;
    uint8_t *keys; /* (mask + 1) * 32 */
    uint8_t *used; /* mask + 1 */
};

int32_t zind_seen_create(uint32_t capacity, zind_seen **out) {
    zind_seen *s;
    uint32_t slots = 2;
    if (!out || capacity == 0 || capacity > (1u << 24)) return ZXV_E_ARG;
    while (slots < 2 * capacity) slots <<= 1;
    s = (zind_seen *) calloc(1, sizeof *s);
    if (!s) return ZXV_E_NOMEM;
    s->keys = (uint8_t *) calloc(slots, 32);
    s->used = (uint8_t *) calloc(slots, 1);
    if (!s->keys || !s->used) {
        zind_seen_destroy(s);
        return ZXV_E_NOMEM;
    }
    s->capacity = capacity;
    s->mask = slots - 1;
    *out = s;
    return ZXV_OK;
}

void zind_seen_destroy(zind_seen *s) {
    if (!s) return;
    free(s->keys);
    free(s->used);
    free(s);
}

static uint32_t seen_slot(const zind_seen *s, const uint8_t d[32], int *found) {
    uint32_t h = (uint32_t) d[0] | ((uint32_t) d[1] << 8) | ((uint32_t) d[2] << 16) |
                 ((uint32_t) d[3] << 24);
    uint32_t i = h & s->mask;
    for (;;) {
        if (!s->used[i]) {
            *found = 0;
            return i;
        }
        if (memcmp(s->keys + (size_t) i * 32, d, 32) == 0) {
            *found = 1;
            return i;
        }
        i = (i + 1) & s->mask;
    }
}

int32_t zind_seen_add(zind_seen *s, const uint8_t digest[32]) {
    int found = 0;
    uint32_t i;
    if (!s || !digest) return ZXV_E_ARG;
    i = seen_slot(s, digest, &found);
    if (found) return ZIND_E_REPLAY;
    if (s->count >= s->capacity) {
        zind_set_error("zind_seen_add: the seen-set is full (%u entries)", s->capacity);
        return ZXV_E_CAPACITY;
    }
    memcpy(s->keys + (size_t) i * 32, digest, 32);
    s->used[i] = 1;
    s->count++;
    return ZXV_OK;
}

int32_t zind_seen_has(const zind_seen *s, const uint8_t digest[32], int32_t *out_present) {
    int found = 0;
    if (!s || !digest || !out_present) return ZXV_E_ARG;
    (void) seen_slot(s, digest, &found);
    *out_present = found;
    return ZXV_OK;
}

int32_t zind_seen_count(const zind_seen *s, uint32_t *out_count) {
    if (!s || !out_count) return ZXV_E_ARG;
    *out_count = s->count;
    return ZXV_OK;
}

/* ===== chains ===== */

typedef struct {
    uint64_t seq;
    uint16_t kind;
    uint32_t len;
    uint8_t prev[32], hash[32], key_id[32];
    uint8_t *data; /* payload (len) then signature (ZIND_SIG_BYTES) */
} rec_t;

struct zind_chain {
    char domain[ZIND_DOMAIN_MAX + 1];
    uint32_t capacity, count, alloc;
    rec_t *recs;
    uint8_t head[32];
};

int32_t zind_chain_create(const char *domain, uint32_t capacity, zind_chain **out) {
    zind_chain *c;
    if (!out || !domain_ok(domain) || capacity == 0 || capacity > ZIND_CHAIN_MAX) {
        zind_set_error("zind_chain_create: domain must be 1..32 printable bytes and capacity "
                       "1..%u",
                       ZIND_CHAIN_MAX);
        return ZXV_E_ARG;
    }
    c = (zind_chain *) calloc(1, sizeof *c);
    if (!c) return ZXV_E_NOMEM;
    memcpy(c->domain, domain, strlen(domain) + 1);
    c->capacity = capacity;
    *out = c;
    return ZXV_OK;
}

void zind_chain_destroy(zind_chain *c) {
    if (!c) return;
    for (uint32_t i = 0; i < c->count; i++) free(c->recs[i].data);
    free(c->recs);
    free(c);
}

const char *zind_chain_domain(const zind_chain *c) { return c ? c->domain : ""; }

static int32_t rec_hash(const char *domain, uint64_t seq, uint16_t kind, const uint8_t *payload,
                        uint32_t len, const uint8_t prev[32], const uint8_t key_id[32],
                        uint8_t out[32]) {
    uint8_t buf[11 + 1 + ZIND_DOMAIN_MAX + 8 + 2 + 4 + ZIND_PAYLOAD_MAX + 64];
    zind_wr w;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_bytes(&w, "zind/rec/v1", 11);
    zind_wr_str(&w, domain);
    zind_wr_u64(&w, seq);
    zind_wr_u16(&w, kind);
    zind_wr_u32(&w, len);
    zind_wr_bytes(&w, payload, len);
    zind_wr_bytes(&w, prev, 32);
    zind_wr_bytes(&w, key_id, 32);
    if (w.overflow) return ZXV_E_RANGE;
    return zxv_sha3_256(buf, w.len, out);
}

static int32_t grow(zind_chain *c) {
    rec_t *n;
    uint32_t a;
    if (c->count < c->alloc) return ZXV_OK;
    a = c->alloc ? c->alloc * 2 : 8;
    if (a > c->capacity) a = c->capacity;
    n = (rec_t *) realloc(c->recs, (size_t) a * sizeof *n);
    if (!n) return ZXV_E_NOMEM;
    c->recs = n;
    c->alloc = a;
    return ZXV_OK;
}

int32_t zind_chain_append(const zxv_cap *cap, zind_chain *c, const zind_signer *signer,
                          uint16_t kind, const uint8_t *payload, size_t len, uint64_t *out_seq,
                          uint8_t out_hash[32]) {
    rec_t r;
    int32_t st;
    if (!cap || !c || !signer || (!payload && len)) return ZXV_E_ARG;
    if (len > ZIND_PAYLOAD_MAX) {
        zind_set_error("zind_chain_append: payload of %zu bytes is over %u", len,
                       ZIND_PAYLOAD_MAX);
        return ZXV_E_TOO_LONG;
    }
    if (c->count >= c->capacity) {
        zind_set_error("zind_chain_append: chain \"%s\" is full (%u records)", c->domain,
                       c->capacity);
        return ZXV_E_CAPACITY;
    }
    st = grow(c);
    if (st != ZXV_OK) return st;
    memset(&r, 0, sizeof r);
    r.seq = c->count;
    r.kind = kind;
    r.len = (uint32_t) len;
    memcpy(r.prev, c->head, 32);
    memcpy(r.key_id, signer->id, 32);
    st = rec_hash(c->domain, r.seq, kind, payload, r.len, r.prev, r.key_id, r.hash);
    if (st != ZXV_OK) return st;
    r.data = (uint8_t *) malloc(len + ZIND_SIG_BYTES);
    if (!r.data) return ZXV_E_NOMEM;
    if (len) memcpy(r.data, payload, len);
    st = zind_sign(cap, signer, r.hash, 32, c->domain, r.data + len);
    if (st != ZXV_OK) {
        zind_set_error("zind_chain_append: signing failed (%s); the cap needs CRYPTO_SIGN",
                       zxv_status_name(st));
        free(r.data);
        return st;
    }
    c->recs[c->count++] = r;
    memcpy(c->head, r.hash, 32);
    if (out_seq) *out_seq = r.seq;
    if (out_hash) memcpy(out_hash, r.hash, 32);
    return ZXV_OK;
}

int32_t zind_chain_count(const zind_chain *c, uint32_t *out_count) {
    if (!c || !out_count) return ZXV_E_ARG;
    *out_count = c->count;
    return ZXV_OK;
}

int32_t zind_chain_head(const zind_chain *c, uint8_t out[32]) {
    if (!c || !out) return ZXV_E_ARG;
    memcpy(out, c->head, 32);
    return ZXV_OK;
}

int32_t zind_chain_at(const zind_chain *c, uint32_t i, zind_record *out) {
    const rec_t *r;
    if (!c || !out) return ZXV_E_ARG;
    if (i >= c->count) return ZXV_E_NOT_FOUND;
    r = &c->recs[i];
    out->seq = r->seq;
    out->kind = r->kind;
    out->payload = r->data;
    out->payload_len = r->len;
    out->prev = r->prev;
    out->hash = r->hash;
    out->key_id = r->key_id;
    out->sig = r->data + r->len;
    return ZXV_OK;
}

int32_t zind_chain_verify(const zxv_cap *cap, const zind_chain *c,
                          const uint8_t *const *trusted, size_t n_trusted, uint32_t *out_bad) {
    uint8_t (*ids)[32] = NULL;
    uint8_t expect_prev[32], h[32];
    int32_t st = ZXV_OK;
    uint32_t i;
    if (out_bad) *out_bad = UINT32_MAX;
    if (!cap || !c || (!trusted && n_trusted) || n_trusted > 4096) return ZXV_E_ARG;
    st = zxv_cap_check(cap, ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("zind_chain_verify: the cap needs CRYPTO_SIGN");
        return st;
    }
    if (n_trusted) {
        ids = (uint8_t (*)[32]) malloc(n_trusted * 32);
        if (!ids) return ZXV_E_NOMEM;
        for (size_t k = 0; k < n_trusted; k++) {
            if (!trusted[k]) {
                free(ids);
                return ZXV_E_ARG;
            }
            st = zind_key_id(trusted[k], ids[k]);
            if (st != ZXV_OK) {
                free(ids);
                return st;
            }
        }
    }
    memset(expect_prev, 0, 32);
    for (i = 0; i < c->count; i++) {
        const rec_t *r = &c->recs[i];
        size_t k;
        if (r->seq != i || memcmp(r->prev, expect_prev, 32) != 0) {
            zind_set_error("zind_chain_verify: record %u does not link to its predecessor", i);
            st = ZIND_E_CHAIN;
            break;
        }
        st = rec_hash(c->domain, r->seq, r->kind, r->data, r->len, r->prev, r->key_id, h);
        if (st != ZXV_OK) break;
        if (memcmp(h, r->hash, 32) != 0) {
            zind_set_error("zind_chain_verify: record %u was altered (hash mismatch)", i);
            st = ZIND_E_CHAIN;
            break;
        }
        for (k = 0; k < n_trusted; k++)
            if (memcmp(ids[k], r->key_id, 32) == 0) break;
        if (k == n_trusted) {
            zind_set_error("zind_chain_verify: record %u is signed by a key outside the trust set",
                           i);
            st = ZIND_E_UNTRUSTED;
            break;
        }
        st = zind_verify(cap, trusted[k], r->hash, 32, c->domain, r->data + r->len);
        if (st != ZXV_OK) {
            zind_set_error("zind_chain_verify: record %u has a bad signature", i);
            st = st == ZXV_E_VERIFY ? ZIND_E_CHAIN : st;
            break;
        }
        memcpy(expect_prev, r->hash, 32);
    }
    if (st == ZXV_OK && memcmp(expect_prev, c->head, 32) != 0) {
        zind_set_error("zind_chain_verify: the head does not match the last record");
        st = ZIND_E_CHAIN;
        i = c->count ? c->count - 1 : 0;
    }
    free(ids);
    if (st != ZXV_OK && out_bad) *out_bad = i;
    return st;
}

#define REC_FIXED (8 + 2 + 4 + 32 + 32 + 32 + ZIND_SIG_BYTES)

int32_t zind_chain_export(const zind_chain *c, uint8_t *buf, size_t cap, size_t *out_len) {
    size_t need;
    zind_wr w;
    if (!c || !out_len) return ZXV_E_ARG;
    need = 4 + 1 + 1 + strlen(c->domain) + 4;
    for (uint32_t i = 0; i < c->count; i++) need += REC_FIXED + c->recs[i].len;
    *out_len = need;
    if (!buf || cap < need) return ZXV_E_BUFFER;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "ZIND", 4);
    zind_wr_u8(&w, 1);
    zind_wr_str(&w, c->domain);
    zind_wr_u32(&w, c->count);
    for (uint32_t i = 0; i < c->count; i++) {
        const rec_t *r = &c->recs[i];
        zind_wr_u64(&w, r->seq);
        zind_wr_u16(&w, r->kind);
        zind_wr_u32(&w, r->len);
        zind_wr_bytes(&w, r->data, r->len);
        zind_wr_bytes(&w, r->prev, 32);
        zind_wr_bytes(&w, r->hash, 32);
        zind_wr_bytes(&w, r->key_id, 32);
        zind_wr_bytes(&w, r->data + r->len, ZIND_SIG_BYTES);
    }
    return w.overflow || w.len != need ? ZXV_E_INTERNAL : ZXV_OK;
}

int32_t zind_chain_import(const char *domain, const uint8_t *buf, size_t len, zind_chain **out) {
    zind_rd r;
    char dom[256];
    uint32_t count;
    zind_chain *c = NULL;
    int32_t st;
    if (!domain || !buf || !out) return ZXV_E_ARG;
    *out = NULL;
    zind_rd_init(&r, buf, len);
    if (len < 4 || memcmp(buf, "ZIND", 4) != 0) goto bad;
    r.pos = 4;
    if (zind_rd_u8(&r) != 1) goto bad;
    zind_rd_str(&r, dom, sizeof dom);
    count = zind_rd_u32(&r);
    if (r.bad || strcmp(dom, domain) != 0 || count > ZIND_CHAIN_MAX) goto bad;
    /* every record needs at least REC_FIXED bytes: refuse an impossible count early */
    if ((uint64_t) count * REC_FIXED > (uint64_t) (len - r.pos)) goto bad;
    st = zind_chain_create(domain, ZIND_CHAIN_MAX, &c);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < count; i++) {
        rec_t rec;
        const uint8_t *p;
        memset(&rec, 0, sizeof rec);
        rec.seq = zind_rd_u64(&r);
        rec.kind = zind_rd_u16(&r);
        rec.len = zind_rd_u32(&r);
        if (r.bad || rec.seq != i || rec.len > ZIND_PAYLOAD_MAX ||
            (size_t) rec.len + REC_FIXED - 14 > len - r.pos)
            goto bad;
        st = grow(c);
        if (st != ZXV_OK) {
            zind_chain_destroy(c);
            return st;
        }
        rec.data = (uint8_t *) malloc((size_t) rec.len + ZIND_SIG_BYTES);
        if (!rec.data) {
            zind_chain_destroy(c);
            return ZXV_E_NOMEM;
        }
        p = r.buf + r.pos;
        memcpy(rec.data, p, rec.len);
        p += rec.len;
        memcpy(rec.prev, p, 32);
        memcpy(rec.hash, p + 32, 32);
        memcpy(rec.key_id, p + 64, 32);
        memcpy(rec.data + rec.len, p + 96, ZIND_SIG_BYTES);
        r.pos += (size_t) rec.len + 96 + ZIND_SIG_BYTES;
        c->recs[c->count++] = rec;
        memcpy(c->head, rec.hash, 32);
    }
    if (r.pos != len) goto bad;
    *out = c;
    return ZXV_OK;
bad:
    zind_chain_destroy(c);
    zind_set_error("zind_chain_import: malformed export for domain \"%s\"", domain);
    return ZXV_E_FORMAT;
}
