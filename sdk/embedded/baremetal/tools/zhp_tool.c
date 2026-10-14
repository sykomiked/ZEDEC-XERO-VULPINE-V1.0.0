/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zhp_tool.c — ZHP-1 hardware profiles for board manifests, through the
 * public libzxv ABI only (zxv_hw_profile_*).
 *
 *   zhp_tool reference OUT.zhp   write the public reference machine R1
 *   zhp_tool host OUT.zhp        write a profile of the machine this runs on
 *   zhp_tool digest FILE.zhp     decode (refuses anything non-canonical) and
 *                                print the profile digest
 *
 * Every command prints the SHA-256 profile digest (zxv_hw_profile_digest)
 * as 64 lower-case hex digits on stdout, the value a board manifest pins in
 * hw_profile.digest. Exit 0 on success, 1 on a libzxv or I/O error, 2 on
 * usage. Integers only (R11). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_api.h"

#define MAX_PROFILE (1u << 20) /* far above 64 nodes x (16 caps + 512 B) */

static int fail(const char *what, int32_t rc)
{
    char msg[256] = "";
    size_t len = 0;
    (void) zxv_last_error(msg, sizeof msg, &len);
    fprintf(stderr, "zhp_tool: %s failed with %d (%s)\n", what, (int) rc, msg);
    return 1;
}

static int print_digest(const zxv_hw_profile *p)
{
    uint8_t d[32];
    int32_t rc = zxv_hw_profile_digest(p, d);
    if (rc) return fail("zxv_hw_profile_digest", rc);
    for (unsigned i = 0; i < 32; i++) printf("%02x", (unsigned) d[i]);
    printf("\n");
    return 0;
}

static int write_profile(const zxv_hw_profile *p, const char *path)
{
    size_t need = 0;
    int32_t rc = zxv_hw_profile_encode(p, NULL, 0, &need);
    if (rc != ZXV_E_BUFFER && rc != ZXV_OK) return fail("zxv_hw_profile_encode (size)", rc);
    uint8_t *buf = malloc(need ? need : 1);
    if (!buf) return fail("malloc", ZXV_E_NOMEM);
    size_t len = 0;
    rc = zxv_hw_profile_encode(p, buf, need, &len);
    if (rc) {
        free(buf);
        return fail("zxv_hw_profile_encode", rc);
    }
    FILE *f = fopen(path, "wb");
    int bad = !f || fwrite(buf, 1, len, f) != len;
    if (f && fclose(f)) bad = 1;
    free(buf);
    if (bad) {
        fprintf(stderr, "zhp_tool: cannot write %s\n", path);
        return 1;
    }
    return print_digest(p);
}

static int digest_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "zhp_tool: cannot open %s\n", path);
        return 1;
    }
    uint8_t *buf = malloc(MAX_PROFILE + 1);
    if (!buf) {
        fclose(f);
        return fail("malloc", ZXV_E_NOMEM);
    }
    size_t len = fread(buf, 1, MAX_PROFILE + 1, f);
    int err = ferror(f);
    fclose(f);
    if (err || len > MAX_PROFILE) {
        free(buf);
        fprintf(stderr, "zhp_tool: %s: read error or larger than %u bytes\n", path, MAX_PROFILE);
        return 1;
    }
    zxv_hw_profile *p = NULL;
    int32_t rc = zxv_hw_profile_decode(buf, len, &p);
    free(buf);
    if (rc) return fail("zxv_hw_profile_decode", rc);
    int r = print_digest(p);
    zxv_hw_profile_destroy(p);
    return r;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: zhp_tool reference|host OUT.zhp | zhp_tool digest FILE.zhp\n");
        return 2;
    }
    if (!strcmp(argv[1], "digest")) return digest_file(argv[2]);
    zxv_hw_profile *p = NULL;
    int32_t rc;
    if (!strcmp(argv[1], "reference"))
        rc = zxv_hw_profile_reference(&p);
    else if (!strcmp(argv[1], "host"))
        rc = zxv_hw_profile_probe_host(&p);
    else {
        fprintf(stderr, "zhp_tool: unknown command %s\n", argv[1]);
        return 2;
    }
    if (rc) return fail(argv[1], rc);
    int r = write_profile(p, argv[2]);
    zxv_hw_profile_destroy(p);
    return r;
}
