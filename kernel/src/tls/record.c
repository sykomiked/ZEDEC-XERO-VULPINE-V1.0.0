/* record.c — TLS 1.3 record layer. See record.h. */
#include "record.h"

static void cpy(uint8_t *d, const uint8_t *s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static void wipe(uint8_t *d, uint32_t n) { for (uint32_t i = 0; i < n; i++) d[i] = 0; }

void tls_keys_set(tls_keys_t *k,
                  const uint8_t key[CHACHA20_KEY_LEN],
                  const uint8_t iv[CHACHA20_NONCE_LEN]) {
    if (!k) return;
    cpy(k->key, key, CHACHA20_KEY_LEN);
    cpy(k->iv, iv, CHACHA20_NONCE_LEN);
    /* RFC 8446 5.3: the sequence number is reset to zero whenever the key
     * changes. Carrying it over can repeat a nonce under the new key, which
     * is the one failure this cipher does not survive. */
    k->seq = 0;
    k->active = true;
}

void tls_keys_clear(tls_keys_t *k) {
    if (!k) return;
    wipe(k->key, CHACHA20_KEY_LEN);
    wipe(k->iv, CHACHA20_NONCE_LEN);
    k->seq = 0;
    k->active = false;
}

uint32_t tls_record_write(tls_keys_t *k, uint8_t type,
                          const uint8_t *payload, uint32_t payload_len,
                          uint8_t *out, uint32_t cap) {
    if (!out || payload_len > TLS_MAX_PLAINTEXT) return 0;

    if (!k || !k->active) {
        /* Unprotected: the header carries the real type. */
        if (cap < TLS_REC_HDR_LEN + payload_len) return 0;
        out[0] = type;
        out[1] = 0x03; out[2] = 0x03;         /* legacy_record_version */
        out[3] = (uint8_t)(payload_len >> 8);
        out[4] = (uint8_t)payload_len;
        if (payload && payload_len) cpy(out + TLS_REC_HDR_LEN, payload, payload_len);
        return TLS_REC_HDR_LEN + payload_len;
    }

    /* Protected: inner plaintext is content || real_type (no padding added),
     * and the ciphertext carries a 16-byte tag. */
    uint32_t inner = payload_len + 1u;
    uint32_t ct_len = inner + POLY1305_TAG_LEN;
    if (ct_len > TLS_MAX_CIPHERTEXT) return 0;
    if (cap < TLS_REC_HDR_LEN + ct_len) return 0;

    /* The header must be built BEFORE encrypting, because it IS the AAD, and
     * its length field is the length of the ENCRYPTED record including the
     * tag — not of the plaintext. */
    out[0] = TLS_CT_APPLICATION_DATA;
    out[1] = 0x03; out[2] = 0x03;
    out[3] = (uint8_t)(ct_len >> 8);
    out[4] = (uint8_t)ct_len;

    uint8_t buf[TLS_MAX_PLAINTEXT + 1u];
    if (payload && payload_len) cpy(buf, payload, payload_len);
    buf[payload_len] = type;

    uint8_t nonce[CHACHA20_NONCE_LEN];
    tls13_record_nonce(k->iv, k->seq, nonce);

    aead_seal(k->key, nonce, out, TLS_REC_HDR_LEN,
              buf, out + TLS_REC_HDR_LEN, inner,
              out + TLS_REC_HDR_LEN + inner);
    k->seq++;
    wipe(buf, inner);
    return TLS_REC_HDR_LEN + ct_len;
}

int tls_record_read(tls_keys_t *k,
                    const uint8_t *in, uint32_t in_len, uint32_t *consumed,
                    uint8_t *out_type, uint8_t *out, uint32_t cap, uint32_t *out_len) {
    if (consumed) *consumed = 0;
    if (out_len) *out_len = 0;
    if (!in || !out || !out_type || !out_len) return TLS_REC_BAD;
    if (in_len < TLS_REC_HDR_LEN) return TLS_REC_NEED_MORE;

    uint8_t  type = in[0];
    uint32_t len  = ((uint32_t)in[3] << 8) | in[4];

    /* Bound the length BEFORE waiting for that many bytes, or a peer can
     * make us wait for a record that may never legally exist. */
    if (len > TLS_MAX_CIPHERTEXT) return TLS_REC_BAD;
    if (in_len < TLS_REC_HDR_LEN + len) return TLS_REC_NEED_MORE;
    if (consumed) *consumed = TLS_REC_HDR_LEN + len;

    /* Middlebox compatibility: a peer may send change_cipher_spec records at
     * any point before the handshake completes. They carry no meaning in TLS
     * 1.3 and must be dropped WITHOUT touching the sequence number — counting
     * them would desynchronise every nonce that follows. */
    if (type == TLS_CT_CHANGE_CIPHER_SPEC) return TLS_REC_SKIP;

    const uint8_t *body = in + TLS_REC_HDR_LEN;

    if (!k || !k->active) {
        if (len > cap) return TLS_REC_BAD;
        cpy(out, body, len);
        *out_type = type;
        *out_len = len;
        return TLS_REC_OK;
    }

    if (len < POLY1305_TAG_LEN + 1u) return TLS_REC_BAD;   /* no room for type+tag */
    uint32_t inner = len - POLY1305_TAG_LEN;
    if (inner > cap) return TLS_REC_BAD;

    uint8_t nonce[CHACHA20_NONCE_LEN];
    tls13_record_nonce(k->iv, k->seq, nonce);

    /* AAD is the five header bytes exactly as received. */
    if (!aead_open(k->key, nonce, in, TLS_REC_HDR_LEN,
                   body, out, inner, body + inner)) {
        /* Do NOT advance the sequence number on failure: RFC 8446 requires
         * the connection be torn down, and advancing would silently skip a
         * nonce if a caller tried to continue. */
        return TLS_REC_AUTH_FAIL;
    }
    k->seq++;

    /* Strip zero padding from the end; the last non-zero byte is the REAL
     * content type. The outer type was application_data and means nothing. */
    uint32_t n = inner;
    while (n > 0 && out[n - 1] == 0) n--;
    if (n == 0) return TLS_REC_BAD;   /* all padding: no content type at all */

    *out_type = out[n - 1];
    *out_len = n - 1u;
    out[n - 1] = 0;
    return TLS_REC_OK;
}
