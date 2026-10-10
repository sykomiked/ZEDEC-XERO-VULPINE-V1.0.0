/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_upcheck.c — the update checker's untrusted-input parsers
 * (update/zx_upcheck.c, zx_upmanifest.c, zx_ipns.c): the release manifest,
 * IPNS records, IPNS names, versions and RFC 3339 times.
 *
 * Byte 0 (mod 3) picks: 0 manifest text; 1 IPNS record (next 32 bytes are
 * the expected Ed25519 key, the rest the serialized IpnsEntry); 2 the short
 * string parsers on the rest. Properties: a parsed manifest's counts and
 * paths stay within their fields; a parsed version formats back and
 * re-parses to the same number. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zx_upcheck.h"
#include "fuzz_in.h"

static zxu_manifest_t g_m;
static uint8_t g_scratch[ZXU_MANIFEST_MAX + 64];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > ZXU_MANIFEST_MAX) return 0;
    uint8_t mode = data[0] % 3u;
    uint8_t *buf = fz_dup(data + 1, size - 1);
    uint32_t len = (uint32_t) (size - 1);

    if (mode == 0) {
        if (zxu_manifest_parse(buf, len, &g_m) == ZXU_OK) {
            if (g_m.n > ZXU_MAX_ENTRIES) abort();
            for (uint32_t i = 0; i < g_m.n; i++) {
                const zxu_entry_t *e = &g_m.e[i];
                if (e->path_len > ZXU_PATH_MAX || e->path[e->path_len] != 0) abort();
            }
        }
    } else if (mode == 1) {
        if (len >= 32) {
            uint8_t pk[32];
            memcpy(pk, buf, 32);
            uint8_t *rec = fz_dup(buf + 32, len - 32);
            zxu_ipns_t out;
            if (15u + (len - 32) <= sizeof g_scratch)
                (void) zxu_ipns_verify(rec, len - 32, pk, 1791504000ull, g_scratch,
                                       sizeof g_scratch, &out);
            free(rec);
        }
    } else {
        uint8_t pk[32];
        (void) zxu_ipns_name_parse((const char *) buf, len, pk);
        uint32_t v = 0;
        if (zxu_version_parse((const char *) buf, len, &v) == ZXU_OK) {
            char s[32];
            int n = zxu_version_format(v, s, sizeof s);
            uint32_t v2 = 0;
            if (n <= 0 || zxu_version_parse(s, (uint32_t) n, &v2) != ZXU_OK || v2 != v) abort();
        }
        uint64_t secs = 0;
        uint32_t ns = 0;
        if (zxu_rfc3339_parse(buf, len, &secs, &ns) == ZXU_OK && ns >= 1000000000u) abort();
    }
    free(buf);
    return 0;
}
