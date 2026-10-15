/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pp_core.c -- the passport's seed, keys and topics. See passport.h. */
#include "pp_int.h"
#include "../mlkem/keccak.h"

/* ---- small helpers (freestanding: no libc) ---- */
void pp_mcpy(void *d, const void *s, uint32_t n)
{
    uint8_t *dd = (uint8_t *) d;
    const uint8_t *ss = (const uint8_t *) s;
    for (uint32_t i = 0; i < n; i++) dd[i] = ss[i];
}

void pp_mset(void *d, uint8_t v, uint32_t n)
{
    uint8_t *dd = (uint8_t *) d;
    for (uint32_t i = 0; i < n; i++) dd[i] = v;
}

int pp_mcmp(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    for (uint32_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

bool pp_ct_eq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t d = 0;
    for (uint32_t i = 0; i < n; i++) d |= (uint8_t) (x[i] ^ y[i]);
    return d == 0;
}

void pp_wipe_mem(void *p, uint32_t n)
{
    volatile uint8_t *v = (volatile uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) v[i] = 0;
}

bool pp_is_zero(const void *p, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) p;
    uint8_t d = 0;
    for (uint32_t i = 0; i < n; i++) d |= x[i];
    return d == 0;
}

uint32_t pp_strlen(const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

void pp_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

void pp_put64(uint8_t *p, uint64_t v)
{
    pp_put32(p, (uint32_t) (v >> 32));
    pp_put32(p + 4, (uint32_t) v);
}

uint32_t pp_get32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

uint64_t pp_get64(const uint8_t *p)
{
    return ((uint64_t) pp_get32(p) << 32) | pp_get32(p + 4);
}

/* buf = len(label) || label || a || b || c, as ident's id_shake builds it. */
static uint32_t gather(uint8_t *buf, const char *label, const void *a, uint32_t alen, const void *b,
                       uint32_t blen, const void *c, uint32_t clen)
{
    uint32_t ll = pp_strlen(label, 64);
    if (1u + (uint64_t) ll + alen + blen + clen > PP_HASH_IN) return UINT32_MAX;
    uint32_t n = 1;
    buf[0] = (uint8_t) ll;
    pp_mcpy(buf + n, label, ll);
    n += ll;
    if (a) {
        pp_mcpy(buf + n, a, alen);
        n += alen;
    }
    if (b) {
        pp_mcpy(buf + n, b, blen);
        n += blen;
    }
    if (c) {
        pp_mcpy(buf + n, c, clen);
        n += clen;
    }
    return n;
}

bool pp_shake(uint8_t *out, uint32_t out_len, const char *label, const void *a, uint32_t alen,
              const void *b, uint32_t blen, const void *c, uint32_t clen)
{
    uint8_t buf[PP_HASH_IN];
    uint32_t n = gather(buf, label, a, alen, b, blen, c, clen);
    if (n == UINT32_MAX) return false;
    shake256(buf, n, out, out_len);
    pp_wipe_mem(buf, n);
    return true;
}

bool pp_sha3(uint8_t out[PP_HASH], const char *label, const void *a, uint32_t alen, const void *b,
             uint32_t blen, const void *c, uint32_t clen)
{
    uint8_t buf[PP_HASH_IN];
    uint32_t n = gather(buf, label, a, alen, b, blen, c, clen);
    if (n == UINT32_MAX) return false;
    sha3_256(buf, n, out);
    pp_wipe_mem(buf, n);
    return true;
}

/* ---- names ---- */
const char *pp_status_name(pp_status_t s)
{
    switch (s) {
    case PP_OK:
        return "ok";
    case PP_ERR_ARG:
        return "bad argument";
    case PP_ERR_SIZE:
        return "buffer too small";
    case PP_ERR_FORMAT:
        return "malformed";
    case PP_ERR_CHECKSUM:
        return "seed checksum wrong";
    case PP_ERR_WEAK:
        return "passphrase too short";
    case PP_ERR_KDF:
        return "Argon2id refused";
    case PP_ERR_CRYPTO:
        return "crypto failure";
    case PP_ERR_AUTH:
        return "authentication failed";
    case PP_ERR_FULL:
        return "full";
    case PP_ERR_OVERFLOW:
        return "overflow";
    case PP_ERR_FUNDS:
        return "insufficient balance";
    case PP_ERR_EXPIRED:
        return "grant not valid now";
    case PP_ERR_RIGHTS:
        return "grant lacks the right";
    case PP_ERR_CAP:
        return "over the cap";
    case PP_ERR_REVOKED:
        return "revoked";
    case PP_ERR_MISMATCH:
        return "does not match";
    case PP_ERR_NOTFOUND:
        return "not found";
    case PP_ERR_STATE:
        return "out of order";
    }
    return "unknown";
}

const char *pp_entry_state_name(uint8_t st)
{
    switch (st) {
    case PP_ST_SETTLED:
        return "settled";
    case PP_ST_HELD:
        return "held";
    case PP_ST_QUARANTINED:
        return "quarantined";
    case PP_ST_REFUSED:
        return "refused";
    case PP_ST_UNKNOWN:
        return "unknown";
    case PP_ST_COUNTED:
        return "counted";
    case PP_ST_CONTESTED:
        return "contested";
    }
    return "none";
}

lpres_state_t pp_entry_tag(uint8_t st)
{
    switch (st) {
    case PP_ST_SETTLED:
    case PP_ST_COUNTED:
        return LPRES_STATE_TRUE;
    case PP_ST_HELD:
    case PP_ST_QUARANTINED:
    case PP_ST_CONTESTED:
        return LPRES_STATE_BOTH;
    case PP_ST_REFUSED:
        return LPRES_STATE_FALSE;
    }
    return LPRES_STATE_NEITHER;
}

/* ---- seed ---- */
void pp_kdf_v1(id_kdf_params_t *kp)
{
    if (!kp) return;
    kp->m_kib = PP_KDF_M_KIB;
    kp->t = PP_KDF_T;
    kp->p = PP_KDF_P;
}

static const char B32[32] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

/* Crockford value of one character, -1 for a separator, -2 for invalid. */
static int b32_val(char ch)
{
    if (ch == '-' || ch == ' ') return -1;
    if (ch >= 'a' && ch <= 'z') ch = (char) (ch - 'a' + 'A');
    if (ch == 'O') return 0;
    if (ch == 'I' || ch == 'L') return 1;
    for (int i = 0; i < 32; i++)
        if (B32[i] == ch) return i;
    return -2;
}

#define SEED_TEXT_BYTES 34u /* seed + 2 checksum bytes */
#define SEED_TEXT_CHARS 55u /* ceil(272 / 5) */

static void seed_check(const uint8_t seed[PP_SEED], uint8_t ck[2])
{
    uint8_t h[PP_HASH];
    pp_sha3(h, "zxv.passport.seed.v1", seed, PP_SEED, 0, 0, 0, 0);
    ck[0] = h[0];
    ck[1] = h[1];
}

pp_status_t pp_seed_to_text(const uint8_t seed[PP_SEED], char out[PP_SEED_TEXT])
{
    uint8_t b[SEED_TEXT_BYTES];
    if (!seed || !out) return PP_ERR_ARG;
    pp_mcpy(b, seed, PP_SEED);
    seed_check(seed, b + PP_SEED);
    uint32_t acc = 0, bits = 0, o = 0, nc = 0;
    for (uint32_t i = 0; i < SEED_TEXT_BYTES; i++) {
        acc = (acc << 8) | b[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            if (nc && nc % 5u == 0) out[o++] = '-';
            out[o++] = B32[(acc >> bits) & 31u];
            nc++;
        }
    }
    /* 272 = 54 * 5 + 2: the 55th character carries the last 2 bits, and
     * 54 is not a multiple of 5, so no dash goes before it */
    out[o++] = B32[(acc << (5u - bits)) & 31u];
    out[o] = 0;
    pp_wipe_mem(b, sizeof b);
    pp_wipe_mem(&acc, sizeof acc);
    return PP_OK;
}

pp_status_t pp_seed_from_text(const char *text, uint32_t len, uint8_t seed[PP_SEED])
{
    uint8_t b[SEED_TEXT_BYTES], ck[2];
    if (!text || !seed) return PP_ERR_ARG;
    uint32_t acc = 0, bits = 0, nb = 0, nc = 0;
    pp_status_t s = PP_OK;
    for (uint32_t i = 0; i < len && text[i]; i++) {
        int v = b32_val(text[i]);
        if (v == -1) continue;
        if (v < 0 || nc >= SEED_TEXT_CHARS) {
            s = PP_ERR_FORMAT;
            break;
        }
        nc++;
        acc = (acc << 5) | (uint32_t) v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            b[nb++] = (uint8_t) (acc >> bits); /* nc <= 55 keeps nb <= 34 */
        }
    }
    /* 55 characters = 34 bytes and 3 padding bits, which must be zero */
    if (s == PP_OK && (nc != SEED_TEXT_CHARS || nb != SEED_TEXT_BYTES || (acc & 7u) != 0))
        s = PP_ERR_FORMAT;
    if (s == PP_OK) {
        seed_check(b, ck);
        if (!pp_ct_eq(ck, b + PP_SEED, 2)) s = PP_ERR_CHECKSUM;
    }
    if (s == PP_OK) pp_mcpy(seed, b, PP_SEED);
    pp_wipe_mem(b, sizeof b);
    pp_wipe_mem(&acc, sizeof acc);
    return s;
}

pp_status_t pp_seed_from_passphrase(const id_kdf_params_t *kp, const uint8_t *pass,
                                    uint32_t pass_len, const uint8_t *handle, uint32_t handle_len,
                                    uint64_t *mem, uint32_t mem_blocks, uint8_t seed[PP_SEED])
{
    uint8_t salt[PP_HASH];
    if (!kp || !pass || !handle || !mem || !seed) return PP_ERR_ARG;
    if (handle_len == 0 || handle_len > PP_HANDLE_MAX || pass_len > PP_PASS_MAX) return PP_ERR_ARG;
    if (pass_len < PP_PASS_MIN) return PP_ERR_WEAK;
    pp_sha3(salt, "zxv.passport.v1/salt", handle, handle_len, 0, 0, 0, 0);
    id_status_t r =
        id_argon2id(kp, pass, pass_len, salt, PP_HASH, 0, 0, 0, 0, mem, mem_blocks, seed, PP_SEED);
    if (r != ID_OK) {
        pp_wipe_mem(seed, PP_SEED);
        return PP_ERR_KDF;
    }
    return PP_OK;
}

/* ---- passport ---- */
pp_status_t pp_hydrate(pp_passport_t *p, pqm_level_t level, const uint8_t seed[PP_SEED])
{
    if (!p || !seed) return PP_ERR_ARG;
    pp_mset(p, 0, sizeof *p);
    id_status_t r = id_root_from_seed(&p->root, level, seed);
    if (r == ID_ERR_ARG) return PP_ERR_ARG;
    if (r != ID_OK) {
        pp_wipe(p);
        return PP_ERR_CRYPTO;
    }
    id_root_derive(seed, "passport-vault", 0, p->vault_key);
    id_root_derive(seed, "passport-rendezvous", 0, p->rendezvous);
    return PP_OK;
}

void pp_wipe(pp_passport_t *p)
{
    if (!p) return;
    id_root_wipe(&p->root);
    pp_wipe_mem(p, sizeof *p);
}

pp_status_t pp_topic(const pp_passport_t *p, uint64_t epoch, uint8_t topic[PP_HASH])
{
    uint8_t e[8];
    if (!p || !topic) return PP_ERR_ARG;
    pp_put64(e, epoch);
    pp_shake(topic, PP_HASH, "zxv.passport.topic.v1", p->rendezvous, PP_KEY, e, 8, 0, 0);
    return PP_OK;
}
