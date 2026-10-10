/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_recon.c — the recon operations report success only on an S-Map that
 * passes smap_verify(), and the archetype table resolves file extensions. */
#include <stdio.h>
#include <string.h>
#include "recon.h"

static int failures;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c) {                                                                                   \
            printf("[PASS] %s\n", m);                                                              \
        } else {                                                                                   \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static smap_store_t st;
static recon_t rc;

int main(void)
{
    uint8_t data[3000];
    for (uint32_t i = 0; i < sizeof data; i++) data[i] = (uint8_t) (i * 7 + 3);
    smap_store_init(&st);
    int32_t idx = smap_ingest(&st, data, sizeof data, "doc", SMAP_KEY_K2);
    CHECK(idx >= 0, "payload ingested");
    recon_init(&rc);
    recon_result_t res;

    CHECK(recon_detect_archetype("a.9n63") == RECON_ARCH_9N63, ".9n63 detected");
    CHECK(recon_detect_archetype("a.vino") == RECON_ARCH_VINO, ".vino detected");
    CHECK(recon_detect_archetype("noext") == RECON_ARCH_36N9, "no extension -> default .36n9");

    smap_t *sm = &st.smaps[idx];
    CHECK(recon_reconstruct(&rc, sm, RECON_ARCH_36N9, &res) == 0 && res.success,
          "verified S-Map: forward reconstruction succeeds");
    CHECK(recon_reconstruct(&rc, sm, RECON_ARCH_VINO, &res) == 0 && res.success,
          "verified S-Map: ledger archetype succeeds");

    sm->merkle_root[0] ^= 1u; /* tamper with the manifest */
    CHECK(recon_reconstruct(&rc, sm, RECON_ARCH_36N9, &res) != 0 && !res.success,
          "tampered S-Map: forward reconstruction refused");
    CHECK(recon_reconstruct(&rc, sm, RECON_ARCH_36M9, &res) != 0 && !res.success,
          "tampered S-Map: parity archetype refused");

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
