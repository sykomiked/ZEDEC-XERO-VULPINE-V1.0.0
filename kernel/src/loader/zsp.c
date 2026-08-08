/* zsp.c — ZXV Signed Package verification. See zsp.h. */
#include "zsp.h"
#include "../robin_debanks/sha256.h"
#include "../robin_debanks/ed25519_verify.h"

const char *zsp_strerror(zsp_result_t r) {
    switch (r) {
        case ZSP_OK:          return "ok";
        case ZSP_ERR_SHORT:   return "package truncated";
        case ZSP_ERR_MAGIC:   return "bad magic (not a ZSP package)";
        case ZSP_ERR_HASH:    return "payload hash mismatch (tampered)";
        case ZSP_ERR_SIG:     return "signature invalid (not signed by root key)";
        case ZSP_ERR_ROLLBACK:return "version below rollback floor (anti-rollback)";
        case ZSP_ERR_ARCH:    return "wrong architecture for this machine";
        case ZSP_ERR_KEYID:   return "key-id does not match the root public key";
        default:              return "unknown error";
    }
}

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* ---- ZSP v1: integrity + signature over the 40-byte (magic||len||sha) preimage. */
static zsp_result_t zsp_verify_v1(const uint8_t *buf, uint32_t len,
                                  const uint8_t root_pubkey[32],
                                  const uint8_t **payload, uint32_t *payload_len) {
    if (len < ZSP_HEADER_LEN) return ZSP_ERR_SHORT;
    uint32_t plen = rd_u32(buf + 4);
    if (plen > len) return ZSP_ERR_SHORT;
    if ((uint32_t)ZSP_HEADER_LEN + plen < plen) return ZSP_ERR_SHORT;
    if ((uint32_t)ZSP_HEADER_LEN + plen > len) return ZSP_ERR_SHORT;

    const uint8_t *sha_field = buf + 8;
    const uint8_t *sig       = buf + 40;
    const uint8_t *pl        = buf + ZSP_HEADER_LEN;

    uint8_t digest[SHA256_DIGEST_LEN];
    sha256(pl, plen, digest);
    if (!ed25519_ct_equal(digest, sha_field, SHA256_DIGEST_LEN)) return ZSP_ERR_HASH;
    if (!ed25519_verify(buf, ZSP_PREIMAGE_LEN, sig, root_pubkey))  return ZSP_ERR_SIG;

    if (payload) *payload = pl;
    if (payload_len) *payload_len = plen;
    return ZSP_OK;
}

/* ---- ZSP v2: integrity + signature over the 96-byte rich preimage, then the
 * authenticated policy checks (key-id, arch, anti-rollback version floor). */
static zsp_result_t zsp_verify_v2(const uint8_t *buf, uint32_t len,
                                  const uint8_t root_pubkey[32],
                                  uint32_t min_version, uint16_t expected_arch,
                                  zsp_meta_t *meta,
                                  const uint8_t **payload, uint32_t *payload_len) {
    if (len < ZSP2_HEADER_LEN) return ZSP_ERR_SHORT;

    uint16_t abi     = rd_u16(buf + 6);
    uint32_t version = rd_u32(buf + 8);
    uint16_t arch    = rd_u16(buf + 12);
    uint32_t caps    = rd_u32(buf + 16);
    uint32_t plen    = rd_u32(buf + 20);
    const uint8_t *key_id   = buf + 24;
    const uint8_t *identity = buf + 32;
    const uint8_t *sha_field= buf + 64;
    const uint8_t *sig      = buf + ZSP2_PREIMAGE_LEN;   /* 96 */
    const uint8_t *pl       = buf + ZSP2_HEADER_LEN;     /* 160 */

    if (plen > len) return ZSP_ERR_SHORT;
    if ((uint32_t)ZSP2_HEADER_LEN + plen < plen) return ZSP_ERR_SHORT;
    if ((uint32_t)ZSP2_HEADER_LEN + plen > len) return ZSP_ERR_SHORT;

    /* 1) integrity */
    uint8_t digest[SHA256_DIGEST_LEN];
    sha256(pl, plen, digest);
    if (!ed25519_ct_equal(digest, sha_field, SHA256_DIGEST_LEN)) return ZSP_ERR_HASH;

    /* 2) authenticity over the full rich preimage — version/arch/caps/identity are
     * all covered, so none can be altered without invalidating the signature. */
    if (!ed25519_verify(buf, ZSP2_PREIMAGE_LEN, sig, root_pubkey)) return ZSP_ERR_SIG;

    /* 3) key-id must be the first 8 bytes of SHA-256(root_pubkey) */
    uint8_t kd[SHA256_DIGEST_LEN];
    sha256(root_pubkey, 32, kd);
    if (!ed25519_ct_equal(kd, key_id, 8)) return ZSP_ERR_KEYID;

    /* 4) architecture gate */
    if (expected_arch != ZSP_ARCH_ANY && arch != expected_arch) return ZSP_ERR_ARCH;

    /* 5) anti-rollback: version must be at or above the monotonic floor */
    if (version < min_version) return ZSP_ERR_ROLLBACK;

    if (meta) {
        meta->abi = abi; meta->arch = arch; meta->version = version;
        meta->caps = caps; meta->payload_len = plen;
        for (int i = 0; i < 8;  i++) meta->key_id[i]   = key_id[i];
        for (int i = 0; i < 32; i++) meta->identity[i] = identity[i];
    }
    if (payload) *payload = pl;
    if (payload_len) *payload_len = plen;
    return ZSP_OK;
}

zsp_result_t zsp_verify(const uint8_t *buf, uint32_t len,
                        const uint8_t root_pubkey[32],
                        const uint8_t **payload, uint32_t *payload_len) {
    if (!buf || !root_pubkey) return ZSP_ERR_SHORT;
    if (len < 4) return ZSP_ERR_SHORT;
    if (!(buf[0] == ZSP_MAGIC0 && buf[1] == ZSP_MAGIC1 && buf[2] == ZSP_MAGIC2))
        return ZSP_ERR_MAGIC;
    if (buf[3] == ZSP2_MAGIC3)
        return zsp_verify_v2(buf, len, root_pubkey, 0, ZSP_ARCH_ANY,
                             (zsp_meta_t *)0, payload, payload_len);
    if (buf[3] == ZSP_MAGIC3)
        return zsp_verify_v1(buf, len, root_pubkey, payload, payload_len);
    return ZSP_ERR_MAGIC;
}

zsp_result_t zsp_verify2(const uint8_t *buf, uint32_t len,
                         const uint8_t root_pubkey[32],
                         uint32_t min_version, uint16_t expected_arch,
                         zsp_meta_t *meta,
                         const uint8_t **payload, uint32_t *payload_len) {
    if (!buf || !root_pubkey) return ZSP_ERR_SHORT;
    if (len < 4) return ZSP_ERR_SHORT;
    if (!(buf[0] == ZSP_MAGIC0 && buf[1] == ZSP_MAGIC1 &&
          buf[2] == ZSP_MAGIC2 && buf[3] == ZSP2_MAGIC3))
        return ZSP_ERR_MAGIC;   /* v2 required for enforced execution */
    return zsp_verify_v2(buf, len, root_pubkey, min_version, expected_arch,
                         meta, payload, payload_len);
}
