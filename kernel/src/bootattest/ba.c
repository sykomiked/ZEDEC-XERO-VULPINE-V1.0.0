/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ba.c — boot-state attestation. See ba.h for the rules and formats.
 * Freestanding C11, integer only, no allocation. Crypto comes from
 * robin_debanks/sha256.c, mlkem/keccak.c (SHA3-256) and pqsec/pq_mldsa87.c. */
#include "ba.h"
#include "../robin_debanks/sha256.h"
#include "../mlkem/keccak.h"
#include "../pqsec/pq_matrix_algs.h"

static const uint8_t CTX_QUOTE[] = "ZXV-BA1-QUOTE";
static const uint8_t CTX_MANIFEST[] = "ZXV-BA1-MANIFEST";
#define CTX_QUOTE_LEN    (sizeof CTX_QUOTE - 1u)
#define CTX_MANIFEST_LEN (sizeof CTX_MANIFEST - 1u)

/* ===== small helpers (no libc) ======================================== */

static void b_cpy(void *d, const void *s, size_t n)
{
    uint8_t *x = (uint8_t *) d;
    const uint8_t *y = (const uint8_t *) s;
    for (size_t i = 0; i < n; i++) x[i] = y[i];
}

static void b_zero(void *d, size_t n)
{
    volatile uint8_t *x = (volatile uint8_t *) d;
    for (size_t i = 0; i < n; i++) x[i] = 0;
}

static bool b_eq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a;
    const uint8_t *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

static void wr32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static void wr64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) v |= (uint64_t) p[i] << (8 * i);
    return v;
}

static bool name_char_ok(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

/* Length of a valid stage name, or 0 if invalid (rule B2). */
static unsigned name_len(const char *name)
{
    if (!name) return 0;
    unsigned n = 0;
    while (name[n]) {
        if (n >= BA_NAME_MAX || !name_char_ok((uint8_t) name[n])) return 0;
        n++;
    }
    return n;
}

static bool name_bytes_ok(const uint8_t *p, unsigned n)
{
    if (n == 0 || n > BA_NAME_MAX) return false;
    for (unsigned i = 0; i < n; i++)
        if (!name_char_ok(p[i])) return false;
    return true;
}

const char *ba_strerror(ba_err_t e)
{
    static const char *const names[BA_E_COUNT_] = {
        "ok",
        "arg",
        "name",
        "full",
        "space",
        "signer",
        "format",
        "manifest-format",
        "manifest-key",
        "manifest-sig",
        "rollback",
        "manifest-id",
        "wrong-key",
        "sig",
        "nonce-replay",
        "nonce-unknown",
        "chain",
        "stage-unknown",
        "stage-order",
        "stage-digest",
        "stage-missing",
    };
    if ((unsigned) e >= (unsigned) BA_E_COUNT_) return "unknown";
    return names[e];
}

/* ===== hashing helpers ================================================ */

bool ba_soft_signer(void *user, const uint8_t *msg, size_t msg_len, const uint8_t *ctx,
                    size_t ctx_len, uint8_t sig[BA_SIG_LEN])
{
    const ba_soft_key_t *k = (const ba_soft_key_t *) user;
    if (!k || !k->sk || !sig || (!msg && msg_len) || ctx_len > 255u) return false;
    pqm_mldsa87_sign(k->sk, msg, msg_len, ctx, ctx_len, k->rnd, sig);
    return true;
}

void ba_soft_keygen(const uint8_t seed[32], uint8_t pk[BA_PK_LEN], uint8_t sk[BA_SK_LEN])
{
    pqm_mldsa87_keygen(seed, pk, sk);
}

void ba_key_hash(const uint8_t pk[BA_PK_LEN], uint8_t out[BA_KEYHASH_LEN])
{
    sha3_256(pk, BA_PK_LEN, out);
}

void ba_cid(const uint8_t *rec, size_t len, uint8_t id[BA_ID_LEN])
{
    id[0] = 0x01; /* CIDv1 */
    id[1] = 0x55; /* raw */
    id[2] = 0x12; /* sha2-256 */
    id[3] = 0x20; /* 32 bytes */
    sha256(rec, len, id + 4);
}

size_t ba_base32(const uint8_t *in, size_t len, char *out, size_t cap)
{
    static const char A[] = "abcdefghijklmnopqrstuvwxyz234567";
    size_t chars = (len * 8u + 4u) / 5u;
    if (!out || cap < chars + 1u || (!in && len)) return 0;
    uint32_t acc = 0;
    unsigned bits = 0;
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        acc = (acc << 8) | in[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out[o++] = A[(acc >> bits) & 31u];
        }
        acc &= (1u << bits) - 1u;
    }
    if (bits) out[o++] = A[(acc << (5u - bits)) & 31u];
    out[o] = '\0';
    return o;
}

void ba_id_text(const uint8_t id[BA_ID_LEN], char out[BA_ID_TEXT_LEN + 1])
{
    out[0] = 'b';
    (void) ba_base32(id, BA_ID_LEN, out + 1, BA_ID_TEXT_LEN);
}

/* ===== measuring ====================================================== */

void ba_log_init(ba_log_t *log)
{
    if (log) b_zero(log, sizeof *log);
}

static void extend_one(uint8_t chain[BA_DIGEST_LEN], const uint8_t *name, unsigned nlen,
                       const uint8_t digest[BA_DIGEST_LEN])
{
    sha256_ctx_t c;
    uint8_t l = (uint8_t) nlen;
    sha256_init(&c);
    sha256_update(&c, chain, BA_DIGEST_LEN);
    sha256_update(&c, &l, 1);
    sha256_update(&c, name, nlen);
    sha256_update(&c, digest, BA_DIGEST_LEN);
    sha256_final(&c, chain);
}

ba_err_t ba_extend(ba_log_t *log, const char *name, const uint8_t digest[BA_DIGEST_LEN])
{
    if (!log || !digest) return BA_E_ARG;
    unsigned n = name_len(name);
    if (n == 0) return BA_E_NAME;
    if (log->n >= BA_MAX_EVENTS) return BA_E_FULL;
    ba_event_t *e = &log->ev[log->n];
    b_zero(e, sizeof *e);
    e->name_len = (uint8_t) n;
    b_cpy(e->name, name, n);
    b_cpy(e->digest, digest, BA_DIGEST_LEN);
    extend_one(log->chain, (const uint8_t *) e->name, n, digest);
    log->n++;
    return BA_OK;
}

ba_err_t ba_measure(ba_log_t *log, const char *name, const uint8_t *data, size_t len)
{
    if (!log || (!data && len)) return BA_E_ARG;
    uint8_t d[BA_DIGEST_LEN];
    sha256(data, len, d);
    return ba_extend(log, name, d);
}

void ba_replay(const ba_event_t *ev, unsigned n, uint8_t chain[BA_DIGEST_LEN])
{
    b_zero(chain, BA_DIGEST_LEN);
    for (unsigned i = 0; i < n; i++)
        extend_one(chain, (const uint8_t *) ev[i].name, ev[i].name_len, ev[i].digest);
}

/* ===== event list encoding ============================================ */

static size_t events_size(const ba_event_t *ev, unsigned n)
{
    size_t s = 0;
    for (unsigned i = 0; i < n; i++) s += 1u + ev[i].name_len + BA_DIGEST_LEN;
    return s;
}

static bool events_valid(const ba_event_t *ev, unsigned n)
{
    if (n > BA_MAX_EVENTS) return false;
    for (unsigned i = 0; i < n; i++)
        if (!name_bytes_ok((const uint8_t *) ev[i].name, ev[i].name_len)) return false;
    return true;
}

static size_t events_write(uint8_t *p, const ba_event_t *ev, unsigned n)
{
    size_t o = 0;
    for (unsigned i = 0; i < n; i++) {
        p[o++] = ev[i].name_len;
        b_cpy(p + o, ev[i].name, ev[i].name_len);
        o += ev[i].name_len;
        b_cpy(p + o, ev[i].digest, BA_DIGEST_LEN);
        o += BA_DIGEST_LEN;
    }
    return o;
}

/* Reads exactly n events from p[0..len); returns bytes used or 0. */
static size_t events_read(const uint8_t *p, size_t len, ba_event_t *ev, unsigned n)
{
    size_t o = 0;
    for (unsigned i = 0; i < n; i++) {
        if (o >= len) return 0;
        unsigned nl = p[o];
        if (len - o < 1u + nl + BA_DIGEST_LEN) return 0;
        if (!name_bytes_ok(p + o + 1, nl)) return 0;
        b_zero(&ev[i], sizeof ev[i]);
        ev[i].name_len = (uint8_t) nl;
        b_cpy(ev[i].name, p + o + 1, nl);
        b_cpy(ev[i].digest, p + o + 1 + nl, BA_DIGEST_LEN);
        o += 1u + nl + BA_DIGEST_LEN;
    }
    return o;
}

/* ===== quote ========================================================== */

ba_err_t ba_quote(const ba_log_t *log, const uint8_t nonce[BA_NONCE_LEN],
                  const uint8_t manifest_id[BA_ID_LEN],
                  const uint8_t device_key_hash[BA_KEYHASH_LEN], ba_sign_fn sign, void *sign_user,
                  uint8_t *out, size_t cap, size_t *out_len)
{
    if (!log || !nonce || !manifest_id || !device_key_hash || !sign || !out || !out_len)
        return BA_E_ARG;
    if (!events_valid(log->ev, log->n)) return BA_E_NAME;
    size_t body = BA_QUOTE_HDR + events_size(log->ev, log->n);
    if (cap < body + BA_SIG_LEN) return BA_E_SPACE;
    uint8_t *p = out;
    p[0] = 'Z';
    p[1] = 'B';
    p[2] = 'Q';
    p[3] = '1';
    p[4] = 1;
    p[5] = log->n;
    p[6] = 0;
    p[7] = 0;
    size_t o = 8;
    b_cpy(p + o, nonce, BA_NONCE_LEN);
    o += BA_NONCE_LEN;
    b_cpy(p + o, manifest_id, BA_ID_LEN);
    o += BA_ID_LEN;
    b_cpy(p + o, device_key_hash, BA_KEYHASH_LEN);
    o += BA_KEYHASH_LEN;
    b_cpy(p + o, log->chain, BA_DIGEST_LEN);
    o += BA_DIGEST_LEN;
    o += events_write(p + o, log->ev, log->n);
    if (!sign(sign_user, out, o, CTX_QUOTE, CTX_QUOTE_LEN, out + o)) {
        b_zero(out, o + BA_SIG_LEN);
        return BA_E_SIGNER;
    }
    *out_len = o + BA_SIG_LEN;
    return BA_OK;
}

/* ===== manifest ======================================================= */

ba_err_t ba_manifest_build(const ba_manifest_t *m, ba_sign_fn sign, void *sign_user, uint8_t *out,
                           size_t cap, size_t *out_len)
{
    if (!m || !sign || !out || !out_len) return BA_E_ARG;
    if (m->n == 0 || !events_valid(m->stage, m->n)) return BA_E_NAME;
    for (unsigned i = 0; i < m->n; i++) /* names must be unique */
        for (unsigned j = 0; j < i; j++)
            if (m->stage[i].name_len == m->stage[j].name_len &&
                b_eq(m->stage[i].name, m->stage[j].name, m->stage[i].name_len))
                return BA_E_NAME;
    size_t body = BA_MANIFEST_HDR + events_size(m->stage, m->n);
    if (cap < body + BA_SIG_LEN) return BA_E_SPACE;
    out[0] = 'Z';
    out[1] = 'B';
    out[2] = 'M';
    out[3] = '1';
    out[4] = 1;
    out[5] = m->n;
    out[6] = 0;
    out[7] = 0;
    wr32(out + 8, m->image_version);
    wr32(out + 12, 0);
    wr64(out + 16, m->issued_at);
    b_cpy(out + 24, m->release_key_hash, BA_KEYHASH_LEN);
    size_t o = BA_MANIFEST_HDR;
    o += events_write(out + o, m->stage, m->n);
    if (!sign(sign_user, out, o, CTX_MANIFEST, CTX_MANIFEST_LEN, out + o)) {
        b_zero(out, o + BA_SIG_LEN);
        return BA_E_SIGNER;
    }
    *out_len = o + BA_SIG_LEN;
    return BA_OK;
}

/* Body length of a well-formed manifest, or 0. */
static size_t manifest_body(const uint8_t *rec, size_t len, ba_manifest_t *m)
{
    if (!rec || len < BA_MANIFEST_HDR + BA_SIG_LEN || len > BA_MANIFEST_MAX) return 0;
    if (rec[0] != 'Z' || rec[1] != 'B' || rec[2] != 'M' || rec[3] != '1') return 0;
    if (rec[4] != 1 || rec[6] != 0 || rec[7] != 0 || rd32(rec + 12) != 0) return 0;
    unsigned n = rec[5];
    if (n == 0 || n > BA_MAX_EVENTS) return 0;
    b_zero(m, sizeof *m);
    m->image_version = rd32(rec + 8);
    m->issued_at = rd64(rec + 16);
    b_cpy(m->release_key_hash, rec + 24, BA_KEYHASH_LEN);
    m->n = (uint8_t) n;
    size_t evlen = len - BA_SIG_LEN - BA_MANIFEST_HDR;
    size_t used = events_read(rec + BA_MANIFEST_HDR, evlen, m->stage, n);
    if (used == 0 || used != evlen) return 0;
    for (unsigned i = 0; i < n; i++)
        for (unsigned j = 0; j < i; j++)
            if (m->stage[i].name_len == m->stage[j].name_len &&
                b_eq(m->stage[i].name, m->stage[j].name, m->stage[i].name_len))
                return 0;
    return BA_MANIFEST_HDR + used;
}

ba_err_t ba_manifest_parse(const uint8_t *rec, size_t len, ba_manifest_t *m)
{
    if (!m) return BA_E_ARG;
    return manifest_body(rec, len, m) ? BA_OK : BA_E_MANIFEST_FORMAT;
}

ba_err_t ba_manifest_verify(const uint8_t *rec, size_t len, const uint8_t release_pk[BA_PK_LEN],
                            ba_manifest_t *m)
{
    if (!m || !release_pk) return BA_E_ARG;
    size_t body = manifest_body(rec, len, m);
    if (!body) return BA_E_MANIFEST_FORMAT;
    uint8_t kh[BA_KEYHASH_LEN];
    ba_key_hash(release_pk, kh);
    if (!b_eq(kh, m->release_key_hash, BA_KEYHASH_LEN)) return BA_E_MANIFEST_KEY;
    if (!pqm_mldsa87_verify(release_pk, rec, body, CTX_MANIFEST, CTX_MANIFEST_LEN, rec + body))
        return BA_E_MANIFEST_SIG;
    return BA_OK;
}

/* ===== verifier ======================================================= */

void ba_verifier_init(ba_verifier_t *v, uint32_t min_image_version)
{
    if (!v) return;
    b_zero(v, sizeof *v);
    v->min_image_version = min_image_version;
}

void ba_verifier_issue(ba_verifier_t *v, const uint8_t nonce[BA_NONCE_LEN])
{
    if (!v || !nonce) return;
    unsigned i = v->issued_next % BA_NONCE_SLOTS;
    b_cpy(v->issued[i], nonce, BA_NONCE_LEN);
    v->issued_live[i] = 1;
    v->issued_next = (i + 1u) % BA_NONCE_SLOTS;
}

static int find_nonce(uint8_t tab[][BA_NONCE_LEN], const uint8_t *live, const uint8_t *nonce)
{
    for (unsigned i = 0; i < BA_NONCE_SLOTS; i++)
        if (live[i] && b_eq(tab[i], nonce, BA_NONCE_LEN)) return (int) i;
    return -1;
}

static bool event_name_eq(const ba_event_t *a, const ba_event_t *b)
{
    return a->name_len == b->name_len && b_eq(a->name, b->name, a->name_len);
}

ba_err_t ba_verify_quote(ba_verifier_t *v, const uint8_t *quote, size_t quote_len,
                         const uint8_t device_pk[BA_PK_LEN], const uint8_t *manifest,
                         size_t manifest_len, const uint8_t release_pk[BA_PK_LEN], ba_result_t *res)
{
    if (!v || !quote || !device_pk || !manifest || !release_pk || !res) return BA_E_ARG;
    b_zero(res, sizeof *res);

    /* 1. quote format */
    if (quote_len < BA_QUOTE_HDR + BA_SIG_LEN || quote_len > BA_QUOTE_MAX) return BA_E_FORMAT;
    if (quote[0] != 'Z' || quote[1] != 'B' || quote[2] != 'Q' || quote[3] != '1' || quote[4] != 1 ||
        quote[6] != 0 || quote[7] != 0)
        return BA_E_FORMAT;
    unsigned n = quote[5];
    if (n > BA_MAX_EVENTS) return BA_E_FORMAT;
    const uint8_t *nonce = quote + 8;
    const uint8_t *mid = nonce + BA_NONCE_LEN;
    const uint8_t *kh = mid + BA_ID_LEN;
    const uint8_t *chain = kh + BA_KEYHASH_LEN;
    size_t evlen = quote_len - BA_SIG_LEN - BA_QUOTE_HDR;
    size_t used = events_read(quote + BA_QUOTE_HDR, evlen, res->ev, n);
    if (used != evlen || (n > 0 && used == 0)) return BA_E_FORMAT;
    res->n_events = (uint8_t) n;
    b_cpy(res->chain, chain, BA_DIGEST_LEN);
    size_t body = BA_QUOTE_HDR + used;

    /* 2-5. manifest */
    ba_err_t me = ba_manifest_verify(manifest, manifest_len, release_pk, &res->manifest);
    if (me != BA_OK) return me;
    if (res->manifest.image_version < v->min_image_version) return BA_E_ROLLBACK;
    uint8_t id[BA_ID_LEN];
    ba_cid(manifest, manifest_len, id);
    if (!b_eq(id, mid, BA_ID_LEN)) return BA_E_MANIFEST_ID;

    /* 6-7. device key and signature */
    uint8_t dkh[BA_KEYHASH_LEN];
    ba_key_hash(device_pk, dkh);
    if (!b_eq(dkh, kh, BA_KEYHASH_LEN)) return BA_E_WRONG_KEY;
    if (!pqm_mldsa87_verify(device_pk, quote, body, CTX_QUOTE, CTX_QUOTE_LEN, quote + body))
        return BA_E_SIG;

    /* 8-9. freshness */
    if (find_nonce(v->used, v->used_live, nonce) >= 0) return BA_E_NONCE_REPLAY;
    int slot = find_nonce(v->issued, v->issued_live, nonce);
    if (slot < 0) return BA_E_NONCE_UNKNOWN;

    /* 10. replay the log */
    uint8_t c[BA_DIGEST_LEN];
    ba_replay(res->ev, n, c);
    if (!b_eq(c, chain, BA_DIGEST_LEN)) return BA_E_CHAIN;

    /* 11. against the golden manifest, stage by stage, in order */
    const ba_manifest_t *m = &res->manifest;
    for (unsigned i = 0; i < n; i++) {
        res->bad_index = i;
        int at = -1;
        for (unsigned j = 0; j < m->n; j++)
            if (event_name_eq(&res->ev[i], &m->stage[j])) {
                at = (int) j;
                break;
            }
        if (at < 0) return BA_E_STAGE_UNKNOWN;
        if ((unsigned) at != i) return BA_E_STAGE_ORDER;
        if (!b_eq(res->ev[i].digest, m->stage[i].digest, BA_DIGEST_LEN)) return BA_E_STAGE_DIGEST;
    }
    if (n < m->n) {
        res->bad_index = n;
        return BA_E_STAGE_MISSING;
    }
    res->bad_index = 0;

    /* success: consume the nonce */
    v->issued_live[slot] = 0;
    unsigned u = v->used_next % BA_NONCE_SLOTS;
    b_cpy(v->used[u], nonce, BA_NONCE_LEN);
    v->used_live[u] = 1;
    v->used_next = (u + 1u) % BA_NONCE_SLOTS;
    return BA_OK;
}
