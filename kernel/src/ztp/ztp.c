/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztp.c — zero-touch provisioning. See ztp.h for the rules and formats.
 * Freestanding C11, integer only, no allocation. Crypto: ML-DSA-87
 * (pqsec/pq_mldsa87.c), SHA3-256 (mlkem/keccak.c), HMAC-SHA256
 * (robin_debanks/crypto_verify.c, the streaming one). tls/hkdf.c's
 * hmac_sha256 is NOT used: it returns an all-zero MAC for messages longer
 * than 512 bytes, and a claim message is 2632 bytes before its MAC. */
#include "ztp.h"
#include "../mlkem/keccak.h"
#include "../pqsec/pq_matrix_algs.h"
#include "../robin_debanks/crypto_verify.h"

static const uint8_t CTX_PROFILE[] = "ZXV-ZHP1-PROFILE";
static const uint8_t CTX_MSG[] = "ZXV-ZTP1-MSG";
#define CTX_PROFILE_LEN (sizeof CTX_PROFILE - 1u)
#define CTX_MSG_LEN     (sizeof CTX_MSG - 1u)

/* ===== helpers ======================================================== */

static void z_cpy(void *d, const void *s, size_t n)
{
    uint8_t *x = (uint8_t *) d;
    const uint8_t *y = (const uint8_t *) s;
    for (size_t i = 0; i < n; i++) x[i] = y[i];
}

static void z_zero(void *d, size_t n)
{
    volatile uint8_t *x = (volatile uint8_t *) d;
    for (size_t i = 0; i < n; i++) x[i] = 0;
}

static bool z_eq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a;
    const uint8_t *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

static bool z_allzero(const uint8_t *p, size_t n)
{
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= p[i];
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

static bool printable(const uint8_t *p, unsigned n)
{
    if (n == 0 || n > ZTP_TEXT_MAX) return false;
    for (unsigned i = 0; i < n; i++)
        if (p[i] < 0x20 || p[i] > 0x7E) return false;
    return true;
}

const char *ztp_strerror(ztp_err_t e)
{
    static const char *const names[ZTP_E_COUNT_] = {
        "ok", "arg", "state", "format", "device", "already-claimed", "epoch", "mac", "sig",
        "same-owner", "disabled", "signer", "space", "profile-format", "profile-key",
        "profile-sig", "qr-format", "qr-mismatch",
    };
    if ((unsigned) e >= (unsigned) ZTP_E_COUNT_) return "unknown";
    return names[e];
}

/* ===== device ========================================================= */

ztp_err_t ztp_device_init(ztp_device_t *d, const uint8_t device_pk[BA_PK_LEN],
                          const uint8_t secret[ZTP_SECRET_LEN])
{
    if (!d || !device_pk || !secret) return ZTP_E_ARG;
    if (d->state != ZTP_FACTORY) return ZTP_E_STATE;
    if (z_allzero(secret, ZTP_SECRET_LEN)) return ZTP_E_ARG;
    z_zero(d, sizeof *d);
    ba_key_hash(device_pk, d->device_key_hash);
    z_cpy(d->claim_secret, secret, ZTP_SECRET_LEN);
    d->state = ZTP_UNCLAIMED;
    return ZTP_OK;
}

ztp_err_t ztp_first_boot(ztp_device_t *d, const uint8_t entropy[64], uint8_t pk[BA_PK_LEN],
                         uint8_t sk[BA_SK_LEN])
{
    if (!d || !entropy || !pk || !sk) return ZTP_E_ARG;
    if (d->state != ZTP_FACTORY) return ZTP_E_STATE;
    ba_soft_keygen(entropy, pk, sk);
    return ztp_device_init(d, pk, entropy + 32);
}

void ztp_device_id(const ztp_device_t *d, uint8_t id[ZTP_ID_LEN])
{
    z_cpy(id, d->device_key_hash, ZTP_ID_LEN);
}

ztp_err_t ztp_qr_text(const ztp_device_t *d, char out[ZTP_QR_TEXT_LEN + 1])
{
    if (!d || !out) return ZTP_E_ARG;
    if (d->state != ZTP_UNCLAIMED) return ZTP_E_STATE;
    uint8_t raw[ZTP_ID_LEN + ZTP_SECRET_LEN];
    z_cpy(raw, d->device_key_hash, ZTP_ID_LEN);
    z_cpy(raw + ZTP_ID_LEN, d->claim_secret, ZTP_SECRET_LEN);
    out[0] = 'Z'; out[1] = 'T'; out[2] = 'P'; out[3] = '1'; out[4] = ':';
    size_t n = ba_base32(raw, sizeof raw, out + 5, ZTP_QR_TEXT_LEN - 4u);
    z_zero(raw, sizeof raw);
    return n == ZTP_QR_TEXT_LEN - 5u ? ZTP_OK : ZTP_E_ARG;
}

ztp_err_t ztp_device_accept(ztp_device_t *d, const uint8_t *m, size_t len,
                            const uint8_t new_secret[ZTP_SECRET_LEN])
{
    if (!d || !m) return ZTP_E_ARG;
    /* format */
    if (len != ZTP_MSG_LEN || m[0] != 'Z' || m[1] != 'T' || m[2] != 'M' || m[3] != '1' ||
        m[4] != 1 || m[6] != 0 || m[7] != 0 || rd32(m + 28) != 0)
        return ZTP_E_FORMAT;
    unsigned kind = m[5];
    if (kind < ZTP_MSG_CLAIM || kind > ZTP_MSG_RESET) return ZTP_E_FORMAT;
    const uint8_t *id = m + 8;
    uint32_t epoch = rd32(m + 24);
    const uint8_t *npk = m + 40;
    const uint8_t *mac = m + ZTP_MSG_MAC_OFF;
    const uint8_t *sig = mac + 32;
    bool npk_zero = z_allzero(npk, BA_PK_LEN);
    if ((kind == ZTP_MSG_RESET) != npk_zero) return ZTP_E_FORMAT;
    if (kind != ZTP_MSG_CLAIM && !z_allzero(mac, 32)) return ZTP_E_FORMAT;
    if (kind == ZTP_MSG_RESET && !new_secret) return ZTP_E_ARG;
    if (kind == ZTP_MSG_RESET && z_allzero(new_secret, ZTP_SECRET_LEN)) return ZTP_E_ARG;

    /* state, device, claimed-already */
    if (d->state == ZTP_FACTORY) return ZTP_E_STATE;
    if (!z_eq(id, d->device_key_hash, ZTP_ID_LEN)) return ZTP_E_DEVICE;
    if (kind == ZTP_MSG_CLAIM && d->state == ZTP_OWNED) return ZTP_E_ALREADY_CLAIMED;
    if (kind != ZTP_MSG_CLAIM && d->state != ZTP_OWNED) return ZTP_E_STATE;

    /* epoch */
    if (epoch != d->epoch || d->epoch == 0xFFFFFFFFu) return ZTP_E_EPOCH;

    /* MAC (claim only) */
    if (kind == ZTP_MSG_CLAIM) {
        uint8_t want[32];
        crypto_hmac_sha256(m, ZTP_MSG_MAC_OFF, d->claim_secret, want);
        bool ok = z_eq(want, mac, 32);
        z_zero(want, sizeof want);
        if (!ok) return ZTP_E_MAC;
    }

    /* signature */
    const uint8_t *signer = kind == ZTP_MSG_CLAIM ? npk : d->owner_pk;
    if (!pqm_mldsa87_verify(signer, m, ZTP_MSG_MAC_OFF + 32u, CTX_MSG, CTX_MSG_LEN, sig))
        return ZTP_E_SIG;

    uint8_t nkh[BA_KEYHASH_LEN];
    switch (kind) {
    case ZTP_MSG_CLAIM:
        z_cpy(d->owner_pk, npk, BA_PK_LEN);
        ba_key_hash(npk, d->owner_key_hash);
        z_zero(d->claim_secret, ZTP_SECRET_LEN);
        d->state = ZTP_OWNED;
        break;
    case ZTP_MSG_TRANSFER:
        ba_key_hash(npk, nkh);
        if (z_eq(nkh, d->owner_key_hash, BA_KEYHASH_LEN)) return ZTP_E_SAME_OWNER;
        z_cpy(d->owner_pk, npk, BA_PK_LEN);
        z_cpy(d->owner_key_hash, nkh, BA_KEYHASH_LEN);
        break;
    default: /* reset */
        z_zero(d->owner_pk, BA_PK_LEN);
        z_zero(d->owner_key_hash, BA_KEYHASH_LEN);
        z_cpy(d->claim_secret, new_secret, ZTP_SECRET_LEN);
        d->state = ZTP_UNCLAIMED;
        break;
    }
    d->epoch++;
    return ZTP_OK;
}

ztp_err_t ztp_reset_physical(ztp_device_t *d, const uint8_t new_secret[ZTP_SECRET_LEN])
{
#if ZTP_PHYSICAL_RESET
    if (!d || !new_secret || z_allzero(new_secret, ZTP_SECRET_LEN)) return ZTP_E_ARG;
    if (d->state != ZTP_OWNED) return ZTP_E_STATE;
    if (d->epoch == 0xFFFFFFFFu) return ZTP_E_EPOCH;
    z_zero(d->owner_pk, BA_PK_LEN);
    z_zero(d->owner_key_hash, BA_KEYHASH_LEN);
    z_cpy(d->claim_secret, new_secret, ZTP_SECRET_LEN);
    d->state = ZTP_UNCLAIMED;
    d->epoch++;
    return ZTP_OK;
#else
    (void) d;
    (void) new_secret;
    return ZTP_E_DISABLED;
#endif
}

/* ===== maker ========================================================== */

void ztp_commit(const uint8_t id[ZTP_ID_LEN], const uint8_t secret[ZTP_SECRET_LEN],
                uint8_t out[32])
{
    static const uint8_t tag[] = "ZXV-ZTP1-COMMIT";
    uint8_t buf[sizeof tag - 1u + ZTP_ID_LEN + ZTP_SECRET_LEN];
    z_cpy(buf, tag, sizeof tag - 1u);
    z_cpy(buf + sizeof tag - 1u, id, ZTP_ID_LEN);
    z_cpy(buf + sizeof tag - 1u + ZTP_ID_LEN, secret, ZTP_SECRET_LEN);
    sha3_256(buf, sizeof buf, out);
    z_zero(buf, sizeof buf);
}

ztp_err_t ztp_profile_build(const ztp_profile_t *p, ba_sign_fn sign, void *user, uint8_t *out,
                            size_t cap, size_t *out_len)
{
    if (!p || !sign || !out || !out_len) return ZTP_E_ARG;
    if (!printable((const uint8_t *) p->model, p->model_len) ||
        !printable((const uint8_t *) p->serial, p->serial_len))
        return ZTP_E_ARG;
    size_t body = ZTP_PROFILE_HDR + p->model_len + p->serial_len;
    if (cap < body + BA_SIG_LEN) return ZTP_E_SPACE;
    out[0] = 'Z'; out[1] = 'P'; out[2] = 'R'; out[3] = '1';
    out[4] = 1; out[5] = p->model_len; out[6] = p->serial_len; out[7] = 0;
    size_t o = 8;
    z_cpy(out + o, p->device_key_hash, 32); o += 32;
    z_cpy(out + o, p->maker_key_hash, 32); o += 32;
    z_cpy(out + o, p->manifest_id, BA_ID_LEN); o += BA_ID_LEN;
    z_cpy(out + o, p->claim_commit, 32); o += 32;
    z_cpy(out + o, p->hw_profile_digest, 32); o += 32;
    wr64(out + o, p->issued_at); o += 8;
    z_cpy(out + o, p->model, p->model_len); o += p->model_len;
    z_cpy(out + o, p->serial, p->serial_len); o += p->serial_len;
    if (!sign(user, out, o, CTX_PROFILE, CTX_PROFILE_LEN, out + o)) {
        z_zero(out, o + BA_SIG_LEN);
        return ZTP_E_SIGNER;
    }
    *out_len = o + BA_SIG_LEN;
    return ZTP_OK;
}

ztp_err_t ztp_profile_verify(const uint8_t *rec, size_t len, const uint8_t maker_pk[BA_PK_LEN],
                             ztp_profile_t *p)
{
    if (!rec || !maker_pk || !p) return ZTP_E_ARG;
    z_zero(p, sizeof *p);
    if (len < ZTP_PROFILE_HDR + 2u + BA_SIG_LEN || len > ZTP_PROFILE_MAX) return ZTP_E_PROFILE_FORMAT;
    if (rec[0] != 'Z' || rec[1] != 'P' || rec[2] != 'R' || rec[3] != '1' || rec[4] != 1 ||
        rec[7] != 0)
        return ZTP_E_PROFILE_FORMAT;
    unsigned ml = rec[5], sl = rec[6];
    size_t body = ZTP_PROFILE_HDR + ml + sl;
    if (len != body + BA_SIG_LEN) return ZTP_E_PROFILE_FORMAT;
    if (!printable(rec + ZTP_PROFILE_HDR, ml) || !printable(rec + ZTP_PROFILE_HDR + ml, sl))
        return ZTP_E_PROFILE_FORMAT;
    size_t o = 8;
    z_cpy(p->device_key_hash, rec + o, 32); o += 32;
    z_cpy(p->maker_key_hash, rec + o, 32); o += 32;
    z_cpy(p->manifest_id, rec + o, BA_ID_LEN); o += BA_ID_LEN;
    z_cpy(p->claim_commit, rec + o, 32); o += 32;
    z_cpy(p->hw_profile_digest, rec + o, 32); o += 32;
    p->issued_at = rd64(rec + o); o += 8;
    p->model_len = (uint8_t) ml;
    p->serial_len = (uint8_t) sl;
    z_cpy(p->model, rec + o, ml); o += ml;
    z_cpy(p->serial, rec + o, sl);
    uint8_t kh[BA_KEYHASH_LEN];
    ba_key_hash(maker_pk, kh);
    if (!z_eq(kh, p->maker_key_hash, BA_KEYHASH_LEN)) return ZTP_E_PROFILE_KEY;
    if (!pqm_mldsa87_verify(maker_pk, rec, body, CTX_PROFILE, CTX_PROFILE_LEN, rec + body))
        return ZTP_E_PROFILE_SIG;
    return ZTP_OK;
}

/* ===== owner ========================================================== */

static int b32val(char c)
{
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

ztp_err_t ztp_qr_parse(const char *text, size_t len, uint8_t id[ZTP_ID_LEN],
                       uint8_t secret[ZTP_SECRET_LEN])
{
    if (!text || !id || !secret) return ZTP_E_ARG;
    if (len != ZTP_QR_TEXT_LEN || text[0] != 'Z' || text[1] != 'T' || text[2] != 'P' ||
        text[3] != '1' || text[4] != ':')
        return ZTP_E_QR_FORMAT;
    uint8_t raw[ZTP_ID_LEN + ZTP_SECRET_LEN];
    uint32_t acc = 0;
    unsigned bits = 0, o = 0;
    for (size_t i = 5; i < len; i++) {
        int v = b32val(text[i]);
        if (v < 0) return ZTP_E_QR_FORMAT;
        acc = (acc << 5) | (uint32_t) v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            raw[o++] = (uint8_t) (acc >> bits);
        }
        acc &= (1u << bits) - 1u;
    }
    if (o != sizeof raw || acc != 0) return ZTP_E_QR_FORMAT; /* canonical: spare bits zero */
    z_cpy(id, raw, ZTP_ID_LEN);
    z_cpy(secret, raw + ZTP_ID_LEN, ZTP_SECRET_LEN);
    z_zero(raw, sizeof raw);
    return ZTP_OK;
}

ztp_err_t ztp_qr_check(const ztp_profile_t *p, const uint8_t id[ZTP_ID_LEN],
                       const uint8_t secret[ZTP_SECRET_LEN])
{
    if (!p || !id || !secret) return ZTP_E_ARG;
    uint8_t c[32];
    ztp_commit(id, secret, c);
    if (!z_eq(id, p->device_key_hash, ZTP_ID_LEN) || !z_eq(c, p->claim_commit, 32))
        return ZTP_E_QR_MISMATCH;
    return ZTP_OK;
}

ztp_err_t ztp_msg_build(ztp_kind_t kind, const uint8_t id[ZTP_ID_LEN], uint32_t epoch,
                        uint64_t issued_at, const uint8_t *new_owner_pk, const uint8_t *secret,
                        ba_sign_fn sign, void *user, uint8_t out[ZTP_MSG_LEN])
{
    if (!id || !sign || !out) return ZTP_E_ARG;
    if (kind != ZTP_MSG_CLAIM && kind != ZTP_MSG_TRANSFER && kind != ZTP_MSG_RESET) return ZTP_E_ARG;
    if ((kind == ZTP_MSG_RESET) != (new_owner_pk == NULL)) return ZTP_E_ARG;
    if ((kind == ZTP_MSG_CLAIM) != (secret != NULL)) return ZTP_E_ARG;
    z_zero(out, ZTP_MSG_LEN);
    out[0] = 'Z'; out[1] = 'T'; out[2] = 'M'; out[3] = '1';
    out[4] = 1; out[5] = (uint8_t) kind;
    z_cpy(out + 8, id, ZTP_ID_LEN);
    wr32(out + 24, epoch);
    wr64(out + 32, issued_at);
    if (new_owner_pk) z_cpy(out + 40, new_owner_pk, BA_PK_LEN);
    if (secret) crypto_hmac_sha256(out, ZTP_MSG_MAC_OFF, secret, out + ZTP_MSG_MAC_OFF);
    if (!sign(user, out, ZTP_MSG_MAC_OFF + 32u, CTX_MSG, CTX_MSG_LEN,
              out + ZTP_MSG_MAC_OFF + 32u)) {
        z_zero(out, ZTP_MSG_LEN);
        return ZTP_E_SIGNER;
    }
    return ZTP_OK;
}
