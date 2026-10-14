/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zfab_dump.c — dump libzxv's four-valued logic and 64-state hexagram
 * tables to JSON, through the public ABI only (zxv_api.h).
 *
 *   zfab_dump > tables/zxv_tables.json
 *
 * Every entry is asked of the linked libzxv, one call per input, so the
 * JSON (and the HDL generated from it) is whatever the C library computes.
 * Any non-OK status for an input the ABI accepts is a hard failure (exit
 * 1); nothing is defaulted. Integers only (R11). */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "zxv_api.h"

static void die(const char *what, int32_t rc)
{
    char msg[256] = "";
    size_t len = 0;
    (void) zxv_last_error(msg, sizeof msg, &len);
    fprintf(stderr, "zfab_dump: %s failed with %d (%s)\n", what, (int) rc, msg);
    exit(1);
}

/* Print `n` integers as a JSON array, `per_line` to a line. */
static void row(const char *key, const int32_t *v, unsigned n, unsigned per_line, int last)
{
    printf("  \"%s\": [", key);
    for (unsigned i = 0; i < n; i++) {
        if (n > per_line && i % per_line == 0) printf("\n    ");
        printf("%d%s", (int) v[i], i + 1 < n ? (n > per_line ? "," : ", ") : "");
    }
    printf("%s]%s\n", n > per_line ? "\n  " : "", last ? "" : ",");
}

static int32_t lp_not[4], lp_and[16], lp_or[16];
static int32_t hex_action[64], hex_phase_next[4], hex_not[64];
static int32_t hex_and[4096], hex_or[4096], hex_from_lp[4], hex_to_lp[4];

int main(void)
{
    uint32_t major = 0, minor = 0, patch = 0;
    int32_t rc = zxv_version(&major, &minor, &patch);
    if (rc) die("zxv_version", rc);
    if (major != ZXV_ABI_VERSION_MAJOR) {
        fprintf(stderr, "zfab_dump: header ABI %d, library ABI %u\n", ZXV_ABI_VERSION_MAJOR,
                (unsigned) major);
        return 1;
    }

    for (int32_t a = 0; a < 4; a++) {
        if ((rc = zxv_lp_not(a, &lp_not[a]))) die("zxv_lp_not", rc);
        for (int32_t b = 0; b < 4; b++) {
            if ((rc = zxv_lp_and(a, b, &lp_and[a * 4 + b]))) die("zxv_lp_and", rc);
            if ((rc = zxv_lp_or(a, b, &lp_or[a * 4 + b]))) die("zxv_lp_or", rc);
        }
        uint8_t u = 0;
        if ((rc = zxv_hex_from_lp(a, &u))) die("zxv_hex_from_lp", rc);
        hex_from_lp[a] = u;
        if ((rc = zxv_hex_to_lp((uint8_t) a, &hex_to_lp[a]))) die("zxv_hex_to_lp", rc);
        if ((rc = zxv_hex_phase_next((uint8_t) a, &u))) die("zxv_hex_phase_next", rc);
        hex_phase_next[a] = u;
    }
    for (unsigned t = 0; t < 64; t++) {
        uint8_t u = 0;
        if ((rc = zxv_hex_action((uint8_t) t, &hex_action[t]))) die("zxv_hex_action", rc);
        if ((rc = zxv_hex_not((uint8_t) t, &u))) die("zxv_hex_not", rc);
        hex_not[t] = u;
        for (unsigned s = 0; s < 64; s++) {
            if ((rc = zxv_hex_and((uint8_t) t, (uint8_t) s, &u))) die("zxv_hex_and", rc);
            hex_and[t * 64 + s] = u;
            if ((rc = zxv_hex_or((uint8_t) t, (uint8_t) s, &u))) die("zxv_hex_or", rc);
            hex_or[t * 64 + s] = u;
        }
        /* pack/unpack must be the documented bit layout the HDL assumes. */
        uint8_t b = 0, p = 0, ph = 0, back = 0;
        if ((rc = zxv_hex_unpack((uint8_t) t, &b, &p, &ph))) die("zxv_hex_unpack", rc);
        if ((rc = zxv_hex_pack(b, p, ph, &back))) die("zxv_hex_pack", rc);
        if (back != t || b != (t >> 4) || p != ((t >> 2) & 3u) || ph != (t & 3u)) {
            fprintf(stderr, "zfab_dump: tag %u does not have the layout base<<4|pol<<2|phase\n", t);
            return 1;
        }
    }

    printf("{\n");
    printf("  \"format\": \"zxv-fabric-tables/1\",\n");
    printf("  \"source\": \"libzxv public ABI (zxv_api.h), dumped by tools/zfab_dump.c\",\n");
    printf("  \"abi_major\": %u,\n", (unsigned) major);
    printf("  \"tag_layout\": \"tag = base << 4 | polarity << 2 | phase\",\n");
    printf("  \"index\": \"unary tables: [a]; binary tables: [a * width + b]\",\n");
    printf("  \"action_names\": [");
    for (int32_t a = ZXV_HEX_ACT_PROCEED; a <= ZXV_HEX_ACT_ROLLBACK; a++)
        printf("\"%s\"%s", zxv_hex_action_name(a), a < ZXV_HEX_ACT_ROLLBACK ? ", " : "");
    printf("],\n");
    printf("  \"lp_names\": [");
    for (int32_t a = 0; a < 4; a++) printf("\"%s\"%s", zxv_lp_name(a), a < 3 ? ", " : "");
    printf("],\n");
    row("lp_not", lp_not, 4, 64, 0);
    row("lp_and", lp_and, 16, 64, 0);
    row("lp_or", lp_or, 16, 64, 0);
    row("hex_from_lp", hex_from_lp, 4, 64, 0);
    row("hex_to_lp", hex_to_lp, 4, 64, 0);
    row("hex_phase_next", hex_phase_next, 4, 64, 0);
    row("hex_action", hex_action, 64, 16, 0);
    row("hex_not", hex_not, 64, 16, 0);
    row("hex_and", hex_and, 4096, 64, 0);
    row("hex_or", hex_or, 4096, 64, 1);
    printf("}\n");
    return fflush(stdout) ? 1 : 0;
}
