/* hkdf.c — HMAC-SHA256, HKDF and the TLS 1.3 key schedule. See hkdf.h. */
#include "hkdf.h"
#include "../robin_debanks/sha256.h"

static void cpy(uint8_t *d, const uint8_t *s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static void zero(uint8_t *d, uint32_t n) { for (uint32_t i = 0; i < n; i++) d[i] = 0; }
static uint32_t slen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

/* We only have a one-shot SHA-256, so HMAC concatenates into a scratch
 * buffer. The message TLS ever feeds HMAC here is a transcript hash or a
 * short label expansion, never bulk data, so a bounded buffer is honest
 * rather than limiting — but it IS a bound, so it is checked. */
#define HMAC_SCRATCH 512u

void hmac_sha256(const uint8_t *key, uint32_t key_len,
                 const uint8_t *msg, uint32_t msg_len,
                 uint8_t out[HASH_LEN]) {
    uint8_t k[HMAC_BLOCK_LEN];
    zero(k, HMAC_BLOCK_LEN);
    if (key_len > HMAC_BLOCK_LEN) {
        /* RFC 2104: an over-long key is hashed down first. */
        sha256(key, key_len, k);
    } else if (key && key_len) {
        cpy(k, key, key_len);
    }

    uint8_t ipad[HMAC_BLOCK_LEN], opad[HMAC_BLOCK_LEN];
    for (uint32_t i = 0; i < HMAC_BLOCK_LEN; i++) {
        ipad[i] = (uint8_t)(k[i] ^ 0x36);
        opad[i] = (uint8_t)(k[i] ^ 0x5C);
    }

    uint8_t buf[HMAC_BLOCK_LEN + HMAC_SCRATCH];
    if (msg_len > HMAC_SCRATCH) { zero(out, HASH_LEN); return; }
    cpy(buf, ipad, HMAC_BLOCK_LEN);
    if (msg && msg_len) cpy(buf + HMAC_BLOCK_LEN, msg, msg_len);
    uint8_t inner[HASH_LEN];
    sha256(buf, HMAC_BLOCK_LEN + msg_len, inner);

    uint8_t buf2[HMAC_BLOCK_LEN + HASH_LEN];
    cpy(buf2, opad, HMAC_BLOCK_LEN);
    cpy(buf2 + HMAC_BLOCK_LEN, inner, HASH_LEN);
    sha256(buf2, HMAC_BLOCK_LEN + HASH_LEN, out);
}

void hkdf_extract(const uint8_t *salt, uint32_t salt_len,
                  const uint8_t *ikm, uint32_t ikm_len,
                  uint8_t prk[HASH_LEN]) {
    uint8_t zsalt[HASH_LEN];
    if (!salt || salt_len == 0) {
        /* RFC 5869: "if not provided, it is set to a string of HashLen
         * zeros". TLS 1.3's first Extract relies on exactly this. */
        zero(zsalt, HASH_LEN);
        salt = zsalt;
        salt_len = HASH_LEN;
    }
    hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

bool hkdf_expand(const uint8_t prk[HASH_LEN],
                 const uint8_t *info, uint32_t info_len,
                 uint8_t *out, uint32_t out_len) {
    if (!prk || !out) return false;
    if (out_len > HKDF_MAX_OKM * HASH_LEN) return false;
    if (info_len > HMAC_SCRATCH - HASH_LEN - 1u) return false;

    uint8_t t[HASH_LEN];
    uint8_t block[HASH_LEN + HMAC_SCRATCH];
    uint32_t t_len = 0;
    uint32_t done = 0;
    uint8_t counter = 1;

    while (done < out_len) {
        /* T(n) = HMAC(PRK, T(n-1) | info | n) — T(0) is empty. */
        uint32_t at = 0;
        if (t_len) { cpy(block, t, t_len); at = t_len; }
        if (info && info_len) { cpy(block + at, info, info_len); at += info_len; }
        block[at++] = counter;
        hmac_sha256(prk, HASH_LEN, block, at, t);
        t_len = HASH_LEN;

        uint32_t take = out_len - done;
        if (take > HASH_LEN) take = HASH_LEN;
        cpy(out + done, t, take);
        done += take;
        counter++;
    }
    return true;
}

uint32_t tls13_hkdf_label(uint8_t *out, uint32_t cap, uint16_t out_len,
                          const char *label, const uint8_t *ctx, uint32_t ctx_len) {
    if (!out || !label) return 0;
    uint32_t ll = slen(label);
    /* Every TLS 1.3 label is prefixed with "tls13 ". Omitting the prefix
     * yields key material that looks fine and that no peer can reproduce. */
    static const char PFX[] = "tls13 ";
    uint32_t pl = 6u;
    if (ll + pl > 255u || ctx_len > 255u) return 0;
    uint32_t need = 2u + 1u + pl + ll + 1u + ctx_len;
    if (cap < need) return 0;

    uint32_t at = 0;
    out[at++] = (uint8_t)(out_len >> 8);
    out[at++] = (uint8_t)out_len;
    out[at++] = (uint8_t)(pl + ll);
    for (uint32_t i = 0; i < pl; i++) out[at++] = (uint8_t)PFX[i];
    for (uint32_t i = 0; i < ll; i++) out[at++] = (uint8_t)label[i];
    out[at++] = (uint8_t)ctx_len;
    for (uint32_t i = 0; i < ctx_len; i++) out[at++] = ctx[i];
    return at;
}

bool tls13_expand_label(const uint8_t secret[HASH_LEN],
                        const char *label,
                        const uint8_t *ctx, uint32_t ctx_len,
                        uint8_t *out, uint32_t out_len) {
    uint8_t info[2 + 1 + 6 + TLS_MAX_LABEL + 1 + HASH_LEN];
    if (out_len > 0xFFFFu) return false;
    uint32_t n = tls13_hkdf_label(info, sizeof info, (uint16_t)out_len,
                                  label, ctx, ctx_len);
    if (!n) return false;
    return hkdf_expand(secret, info, n, out, out_len);
}

bool tls13_derive_secret_h(const uint8_t secret[HASH_LEN],
                           const char *label,
                           const uint8_t transcript_hash[HASH_LEN],
                           uint8_t out[HASH_LEN]) {
    return tls13_expand_label(secret, label, transcript_hash, HASH_LEN,
                              out, HASH_LEN);
}

bool tls13_derive_secret(const uint8_t secret[HASH_LEN],
                         const char *label,
                         const uint8_t *transcript, uint32_t transcript_len,
                         uint8_t out[HASH_LEN]) {
    /* Derive-Secret hashes the messages first — passing the raw transcript
     * as context is a common and completely silent mistake. */
    uint8_t th[HASH_LEN];
    sha256(transcript, transcript_len, th);
    return tls13_derive_secret_h(secret, label, th, out);
}

void tls13_early_secret(tls13_schedule_t *s, const uint8_t *psk, uint32_t psk_len) {
    if (!s) return;
    zero((uint8_t *)s, sizeof(*s));
    uint8_t zpsk[HASH_LEN];
    if (!psk || !psk_len) { zero(zpsk, HASH_LEN); psk = zpsk; psk_len = HASH_LEN; }
    hkdf_extract(0, 0, psk, psk_len, s->early);
}

bool tls13_handshake_secret(tls13_schedule_t *s,
                            const uint8_t *dhe, uint32_t dhe_len,
                            const uint8_t *transcript, uint32_t transcript_len) {
    if (!s || !dhe || !dhe_len) return false;
    /* The salt for this Extract is Derive-Secret(Early, "derived", "") —
     * an empty-transcript derivation, NOT the early secret itself. */
    uint8_t derived[HASH_LEN];
    if (!tls13_derive_secret(s->early, "derived", (const uint8_t *)"", 0, derived))
        return false;
    hkdf_extract(derived, HASH_LEN, dhe, dhe_len, s->handshake);

    if (!tls13_derive_secret(s->handshake, "c hs traffic",
                             transcript, transcript_len, s->c_hs_traffic)) return false;
    if (!tls13_derive_secret(s->handshake, "s hs traffic",
                             transcript, transcript_len, s->s_hs_traffic)) return false;
    s->have_handshake = true;
    return true;
}

bool tls13_master_secret(tls13_schedule_t *s,
                         const uint8_t *transcript, uint32_t transcript_len) {
    if (!s || !s->have_handshake) return false;
    uint8_t derived[HASH_LEN];
    if (!tls13_derive_secret(s->handshake, "derived", (const uint8_t *)"", 0, derived))
        return false;
    uint8_t zeros[HASH_LEN];
    zero(zeros, HASH_LEN);
    hkdf_extract(derived, HASH_LEN, zeros, HASH_LEN, s->master);

    if (!tls13_derive_secret(s->master, "c ap traffic",
                             transcript, transcript_len, s->c_ap_traffic)) return false;
    if (!tls13_derive_secret(s->master, "s ap traffic",
                             transcript, transcript_len, s->s_ap_traffic)) return false;
    s->have_master = true;
    return true;
}

bool tls13_traffic_keys(const uint8_t traffic_secret[HASH_LEN],
                        uint8_t *key, uint32_t key_len,
                        uint8_t *iv, uint32_t iv_len) {
    if (!traffic_secret) return false;
    if (key && !tls13_expand_label(traffic_secret, "key", 0, 0, key, key_len))
        return false;
    if (iv && !tls13_expand_label(traffic_secret, "iv", 0, 0, iv, iv_len))
        return false;
    return true;
}

bool tls13_finished(const uint8_t traffic_secret[HASH_LEN],
                    const uint8_t transcript_hash[HASH_LEN],
                    uint8_t out[HASH_LEN]) {
    uint8_t fkey[HASH_LEN];
    if (!tls13_expand_label(traffic_secret, "finished", 0, 0, fkey, HASH_LEN))
        return false;
    hmac_sha256(fkey, HASH_LEN, transcript_hash, HASH_LEN, out);
    return true;
}

bool ct_equal(const uint8_t *a, const uint8_t *b, uint32_t len) {
    if (!a || !b) return false;
    uint8_t diff = 0;
    /* No early exit: the loop must take the same time whatever the data, or
     * an attacker learns the position of the first mismatched byte and can
     * forge a tag one byte at a time. */
    for (uint32_t i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}
