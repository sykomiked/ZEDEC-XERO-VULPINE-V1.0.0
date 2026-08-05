/* zsp.c — ZXV Signed Package verification. See zsp.h. */
#include "zsp.h"
#include "../robin_debanks/sha256.h"
#include "../robin_debanks/ed25519_verify.h"

const char *zsp_strerror(zsp_result_t r) {
    switch (r) {
        case ZSP_OK:        return "ok";
        case ZSP_ERR_SHORT: return "package truncated";
        case ZSP_ERR_MAGIC: return "bad magic (not a ZSP1 package)";
        case ZSP_ERR_HASH:  return "payload hash mismatch (tampered)";
        case ZSP_ERR_SIG:   return "signature invalid (not signed by root key)";
        default:            return "unknown error";
    }
}

zsp_result_t zsp_verify(const uint8_t *buf, uint32_t len,
                        const uint8_t root_pubkey[32],
                        const uint8_t **payload, uint32_t *payload_len) {
    if (!buf || !root_pubkey) return ZSP_ERR_SHORT;
    if (len < ZSP_HEADER_LEN) return ZSP_ERR_SHORT;

    if (!(buf[0] == ZSP_MAGIC0 && buf[1] == ZSP_MAGIC1 &&
          buf[2] == ZSP_MAGIC2 && buf[3] == ZSP_MAGIC3))
        return ZSP_ERR_MAGIC;

    /* payload_len is little-endian at offset 4 */
    uint32_t plen = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                    ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);

    /* header + declared payload must fit inside the buffer (overflow-safe) */
    if (plen > len) return ZSP_ERR_SHORT;
    if ((uint32_t)ZSP_HEADER_LEN + plen < plen) return ZSP_ERR_SHORT;
    if ((uint32_t)ZSP_HEADER_LEN + plen > len) return ZSP_ERR_SHORT;

    const uint8_t *sha_field = buf + 8;         /* 32 bytes */
    const uint8_t *sig       = buf + 40;        /* 64 bytes */
    const uint8_t *pl        = buf + ZSP_HEADER_LEN;

    /* 1) integrity: SHA-256(payload) must equal the header field. */
    uint8_t digest[SHA256_DIGEST_LEN];
    sha256(pl, plen, digest);
    if (!ed25519_ct_equal(digest, sha_field, SHA256_DIGEST_LEN))
        return ZSP_ERR_HASH;

    /* 2) authenticity: Ed25519 over the 40-byte preimage (magic||len||sha). */
    if (!ed25519_verify(buf, ZSP_PREIMAGE_LEN, sig, root_pubkey))
        return ZSP_ERR_SIG;

    if (payload) *payload = pl;
    if (payload_len) *payload_len = plen;
    return ZSP_OK;
}
