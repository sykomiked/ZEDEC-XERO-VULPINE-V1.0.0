/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_ipfs_cid.c — CID decoding in both IPFS modules: the URI form in
 * ipfs/ipfs.c (ipfs_cid_parse) and the multiformats in ipfs_node
 * (varints, binary and string CIDs, base32 / base58, the ubh168: text form).
 *
 * Properties (canonical encodings, so decode then encode is the identity):
 *   varint_get ok           => varint_put gives the same bytes
 *   cid_decode_exact ok     => cid_encode gives the input bytes back
 *   cid_parse ok            => cid_to_string gives the input string back
 *   base32/58 decode ok     => encode gives the input string back */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs.h"
#include "ipfs_node.h"
#include "fuzz_in.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 4096u) return 0;
    uint8_t *b = fz_dup(data, size);
    char *z = fz_dupz(data, size);
    uint32_t len = (uint32_t) size;

    uint8_t cid32[IPFS_CID_LEN];
    (void) ipfs_cid_parse(z, cid32);

    uint64_t v = 0;
    int r = ipfsn_varint_get(b, len, &v);
    if (r > 0) {
        uint8_t o[16];
        uint32_t w = ipfsn_varint_put(v, o, sizeof o);
        if (w != (uint32_t) r || memcmp(o, b, w) != 0) abort();
    }

    ipfsn_cid_t c;
    uint8_t enc[IPFSN_CID_BIN_MAX + 16];
    r = ipfsn_cid_decode(b, len, &c);
    if (r > 0 && (uint32_t) r > len) abort();
    if (ipfsn_cid_decode_exact(b, len, &c) >= 0) {
        int w = ipfsn_cid_encode(&c, enc, sizeof enc);
        if (w != (int) len || memcmp(enc, b, len) != 0) abort();
        char s[IPFSN_CID_STR_MAX + 8];
        int sl = ipfsn_cid_to_string(&c, s, sizeof s);
        ipfsn_cid_t c2;
        if (sl <= 0 || ipfsn_cid_parse(s, (uint32_t) sl, &c2) != 0 || !ipfsn_cid_equal(&c, &c2))
            abort();
    }

    if (ipfsn_cid_parse(z, len, &c) == 0) {
        char s[IPFSN_CID_STR_MAX + 8];
        int sl = ipfsn_cid_to_string(&c, s, sizeof s);
        if (sl != (int) len || memcmp(s, z, len) != 0) abort();
    }
    (void) ipfsn_ubh_cid_from_text(z, len, &c);

    uint8_t raw[4096];
    static char back[8192];
    int n = ipfsn_base32_decode(z, len, raw, sizeof raw);
    if (n >= 0) {
        int m = ipfsn_base32_encode(raw, (uint32_t) n, back, sizeof back);
        if (m != (int) len || memcmp(back, z, len) != 0) abort();
    }
    n = ipfsn_base58_decode(z, len, raw, sizeof raw);
    if (n >= 0) {
        int m = ipfsn_base58_encode(raw, (uint32_t) n, back, sizeof back);
        if (m != (int) len || memcmp(back, z, len) != 0) abort();
    }

    free(z);
    free(b);
    return 0;
}
