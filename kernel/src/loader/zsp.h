/* zsp.h — ZXV Signed Package verification (Ed25519 + SHA-256)
 *
 * A .zsp binds a payload (an ELF) to a signature over its SHA-256 under
 * an offline root key. The kernel loads code only if BOTH the hash
 * (integrity) and the signature (authenticity, against a compiled-in
 * root public key) verify. This is the on-target trust chain the audit
 * requires (P0-3): a root key gates what executes.
 *
 * Layout (little-endian), then payload:
 *   magic[4]="ZSP1" | payload_len:u32 | sha256[32] | sig[64] | payload
 * The signed preimage is (magic || payload_len || sha256) = 40 bytes.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV signed-package slice)
 * License: SEL-3.3
 */
#ifndef ZXV_ZSP_H
#define ZXV_ZSP_H

#include <stdint.h>
#include <stdbool.h>

#define ZSP_MAGIC0 'Z'
#define ZSP_MAGIC1 'S'
#define ZSP_MAGIC2 'P'
#define ZSP_MAGIC3 '1'
#define ZSP_PREIMAGE_LEN 40   /* magic(4)+len(4)+sha256(32) */
#define ZSP_HEADER_LEN   104  /* preimage(40)+sig(64) */

typedef enum {
    ZSP_OK = 0,
    ZSP_ERR_SHORT   = -1,  /* buffer smaller than header / declared payload */
    ZSP_ERR_MAGIC   = -2,  /* bad magic */
    ZSP_ERR_HASH    = -3,  /* SHA-256 mismatch (payload tampered) */
    ZSP_ERR_SIG     = -4,  /* Ed25519 signature invalid (not from root key) */
} zsp_result_t;

/* Verify a .zsp buffer against `root_pubkey` (32 bytes). On success sets
 * *payload / *payload_len to point inside `buf` and returns ZSP_OK. */
zsp_result_t zsp_verify(const uint8_t *buf, uint32_t len,
                        const uint8_t root_pubkey[32],
                        const uint8_t **payload, uint32_t *payload_len);

const char *zsp_strerror(zsp_result_t r);

#endif /* ZXV_ZSP_H */
