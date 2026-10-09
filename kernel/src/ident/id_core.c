/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* id_core.c -- byte helpers, hashing, DIDs and the root identity. */
#include "id_internal.h"
#include "../mlkem/keccak.h"

void id_mcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    if (d == s || n == 0) return;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
}

void id_mset(void *dst, uint8_t v, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    for (size_t i = 0; i < n; i++) d[i] = v;
}

bool id_meq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

void id_wipe(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *) p;
    for (size_t i = 0; i < n; i++) v[i] = 0;
}

uint32_t id_strlen(const char *s, uint32_t max)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (n < max && s[n]) n++;
    return n;
}

bool id_streq(const char *a, const char *b, uint32_t max)
{
    for (uint32_t i = 0; i < max; i++) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return true;
}

/* ---- writer / reader ---- */
void id_w_init(id_w_t *w, uint8_t *p, uint32_t cap)
{
    w->p = p;
    w->cap = cap;
    w->len = 0;
    w->err = false;
}

void id_w_bytes(id_w_t *w, const void *b, uint32_t n)
{
    if (w->err || n > w->cap - w->len) {
        w->err = true;
        return;
    }
    if (n) id_mcpy(w->p + w->len, b, n);
    w->len += n;
}

void id_w_u8(id_w_t *w, uint8_t v)
{
    id_w_bytes(w, &v, 1);
}

void id_w_u16(id_w_t *w, uint16_t v)
{
    uint8_t b[2] = {(uint8_t) (v >> 8), (uint8_t) v};
    id_w_bytes(w, b, 2);
}

void id_w_u32(id_w_t *w, uint32_t v)
{
    uint8_t b[4] = {(uint8_t) (v >> 24), (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v};
    id_w_bytes(w, b, 4);
}

void id_w_u64(id_w_t *w, uint64_t v)
{
    id_w_u32(w, (uint32_t) (v >> 32));
    id_w_u32(w, (uint32_t) v);
}

void id_r_init(id_r_t *r, const uint8_t *p, uint32_t len)
{
    r->p = p;
    r->len = len;
    r->pos = 0;
    r->err = false;
}

void id_r_bytes(id_r_t *r, void *b, uint32_t n)
{
    if (r->err || n > r->len - r->pos) {
        r->err = true;
        id_mset(b, 0, n);
        return;
    }
    id_mcpy(b, r->p + r->pos, n);
    r->pos += n;
}

uint8_t id_r_u8(id_r_t *r)
{
    uint8_t v;
    id_r_bytes(r, &v, 1);
    return v;
}

uint16_t id_r_u16(id_r_t *r)
{
    uint8_t b[2];
    id_r_bytes(r, b, 2);
    return (uint16_t) ((b[0] << 8) | b[1]);
}

uint32_t id_r_u32(id_r_t *r)
{
    uint8_t b[4];
    id_r_bytes(r, b, 4);
    return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) | ((uint32_t) b[2] << 8) | b[3];
}

uint64_t id_r_u64(id_r_t *r)
{
    uint64_t hi = id_r_u32(r);
    return (hi << 32) | id_r_u32(r);
}

/* ---- hashing ---- */
static bool id_cat(uint8_t *buf, uint32_t *n, const char *label, const void *a, uint32_t alen,
                   const void *b, uint32_t blen, const void *c, uint32_t clen)
{
    id_w_t w;
    uint32_t ll = id_strlen(label, 64);
    id_w_init(&w, buf, ID_HBUF);
    id_w_u8(&w, (uint8_t) ll);
    id_w_bytes(&w, label, ll);
    if (a) id_w_bytes(&w, a, alen);
    if (b) id_w_bytes(&w, b, blen);
    if (c) id_w_bytes(&w, c, clen);
    *n = w.len;
    return !w.err;
}

bool id_shake(uint8_t *out, uint32_t out_len, const char *label, const void *a, uint32_t alen,
              const void *b, uint32_t blen, const void *c, uint32_t clen)
{
    uint8_t buf[ID_HBUF];
    uint32_t n;
    bool ok = id_cat(buf, &n, label, a, alen, b, blen, c, clen);
    if (ok)
        shake256(buf, n, out, out_len);
    else
        id_mset(out, 0, out_len);
    id_wipe(buf, sizeof buf);
    return ok;
}

bool id_sha3(uint8_t out[ID_HASH], const char *label, const void *a, uint32_t alen, const void *b,
             uint32_t blen, const void *c, uint32_t clen)
{
    uint8_t buf[ID_HBUF];
    uint32_t n;
    bool ok = id_cat(buf, &n, label, a, alen, b, blen, c, clen);
    if (ok)
        sha3_256(buf, n, out);
    else
        id_mset(out, 0, ID_HASH);
    id_wipe(buf, sizeof buf);
    return ok;
}

/* ---- signatures ---- */
static pqm_sig_t g_sig;
static uint8_t g_pkenc[PQM_SIG_PK_MAX_BYTES];

id_status_t id_pqm_sign(const pqm_sig_sk_t *sk, const id_host_t *h, const char *ctx,
                        const uint8_t *msg, uint32_t len, uint8_t *sig, uint32_t *sig_len)
{
    uint8_t rnd[32];
    if (!sk || !h || !h->random || !sig || !sig_len) return ID_ERR_ARG;
    h->random(h->ctx, rnd, 32);
    bool ok =
        pqm_sign(sk, msg, len, (const uint8_t *) ctx, id_strlen(ctx, PQM_CTX_MAX), rnd, &g_sig);
    id_wipe(rnd, sizeof rnd);
    if (!ok) return ID_ERR_CRYPTO;
    size_t n = pqm_sig_encode(&g_sig, sig, PQM_SIG_MAX_BYTES);
    if (n == 0) return ID_ERR_CRYPTO;
    *sig_len = (uint32_t) n;
    return ID_OK;
}

id_status_t id_pqm_verify(const pqm_sig_pk_t *pk, const char *ctx, const uint8_t *msg, uint32_t len,
                          const uint8_t *sig, uint32_t sig_len)
{
    if (!pk || !sig || sig_len > PQM_SIG_MAX_BYTES) return ID_ERR_ARG;
    if (!pqm_sig_decode(&g_sig, sig, sig_len)) return ID_ERR_FORMAT;
    if (g_sig.level != pk->level) return ID_ERR_AUTH;
    if (!pqm_verify(pk, msg, len, (const uint8_t *) ctx, id_strlen(ctx, PQM_CTX_MAX), &g_sig))
        return ID_ERR_AUTH;
    return ID_OK;
}

void id_alert(const id_host_t *h, uint8_t kind, const uint8_t *account, const uint8_t *vault,
              const uint8_t *ref, uint64_t when_ms, uint32_t count)
{
    id_alert_t a;
    if (!h || !h->on_alert) return;
    id_mset(&a, 0, sizeof a);
    a.kind = kind;
    if (account) id_mcpy(a.account, account, ID_HASH);
    if (vault) id_mcpy(a.vault_id, vault, ID_HASH);
    if (ref) id_mcpy(a.ref, ref, ID_CRED_ID);
    a.when_ms = when_ms;
    a.count = count;
    h->on_alert(h->ctx, &a);
}

int32_t id_b64url(const uint8_t *in, uint32_t len, char *out, uint32_t cap)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint32_t o = 0, acc = 0, bits = 0;
    for (uint32_t i = 0; i < len; i++) {
        acc = (acc << 8) | in[i];
        bits += 8;
        while (bits >= 6) {
            if (o + 1 >= cap) return -1;
            bits -= 6;
            out[o++] = T[(acc >> bits) & 63u];
        }
    }
    if (bits) {
        if (o + 1 >= cap) return -1;
        out[o++] = T[(acc << (6 - bits)) & 63u];
    }
    if (o >= cap) return -1;
    out[o] = 0;
    return (int32_t) o;
}

/* ---- DIDs ---- */
id_status_t id_did_of(const pqm_sig_pk_t *pk, uint8_t did[ID_HASH])
{
    size_t n;
    if (!pk || !did) return ID_ERR_ARG;
    n = pqm_sig_pk_encode(pk, g_pkenc, sizeof g_pkenc);
    if (n == 0) return ID_ERR_CRYPTO;
    uint8_t lab[] = "ZXV-ident/did";
    uint8_t buf[sizeof lab - 1 + PQM_SIG_PK_MAX_BYTES];
    id_mcpy(buf, lab, sizeof lab - 1);
    id_mcpy(buf + sizeof lab - 1, g_pkenc, n);
    sha3_256(buf, sizeof lab - 1 + n, did);
    return ID_OK;
}

void id_did_string(const uint8_t did[ID_HASH], char out[ID_DID_CHARS])
{
    static const char A[] = "abcdefghijklmnopqrstuvwxyz234567";
    const char *pre = "did:zxv:";
    uint32_t o = 0, acc = 0, bits = 0;
    for (; pre[o]; o++) out[o] = pre[o];
    for (uint32_t i = 0; i < ID_HASH; i++) {
        acc = (acc << 8) | did[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out[o++] = A[(acc >> bits) & 31u];
        }
    }
    if (bits) out[o++] = A[(acc << (5 - bits)) & 31u];
    out[o] = 0;
}

/* ---- root identity ---- */
void id_root_derive(const uint8_t seed[32], const char *label, uint32_t index, uint8_t out[32])
{
    uint8_t ib[4] = {(uint8_t) (index >> 24), (uint8_t) (index >> 16), (uint8_t) (index >> 8),
                     (uint8_t) index};
    uint8_t l[64];
    uint32_t ll = id_strlen(label, 63);
    id_mcpy(l, label, ll);
    id_shake(out, 32, "ZXV-ident/derive", l, ll, ib, 4, seed, 32);
    id_wipe(l, sizeof l);
}

id_status_t id_root_from_seed(id_root_t *r, pqm_level_t level, const uint8_t seed[32])
{
    uint8_t ks[32];
    if (!r || !seed) return ID_ERR_ARG;
    if (level != PQM_LEVEL_STANDARD && level != PQM_LEVEL_HIGH && level != PQM_LEVEL_MATRIX)
        return ID_ERR_ARG;
    id_mset(r, 0, sizeof *r);
    r->level = level;
    id_mcpy(r->seed, seed, 32);
    id_root_derive(seed, "root-sig", 0, ks);
    bool ok = pqm_sig_keygen(level, ks, &r->pk, &r->sk);
    id_wipe(ks, sizeof ks);
    if (!ok) return ID_ERR_CRYPTO;
    return id_did_of(&r->pk, r->did);
}

void id_root_wipe(id_root_t *r)
{
    if (!r) return;
    pqm_sig_sk_wipe(&r->sk);
    id_wipe(r, sizeof *r);
}
