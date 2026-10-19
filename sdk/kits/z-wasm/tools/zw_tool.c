/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zw-tool — check a plugin against the Z-Wasm profile from the command line.
 *
 *   zw-tool validate [--pages N] [--bulk] [--grant all|rational,logic,propose]
 *                    [--allow module.name=sig]... FILE.wasm
 *       exit 0 accepted, 1 rejected (reason, section, function, offset and
 *       message printed), 2 usage / I/O error
 *   zw-tool meter IN.wasm OUT.wasm
 *       write the fuel-metered module (what the host actually runs)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zw_wasm.h"
#include "zwasm.h"

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    uint8_t *b = NULL;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    b = (uint8_t *) malloc((size_t) n + 1u);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *len = (size_t) n;
    return b;
}

static int usage(void) {
    fprintf(stderr, "usage: zw-tool validate [--pages N] [--bulk] [--grant LIST] "
                    "[--allow mod.name=sig]... FILE.wasm\n"
                    "       zw-tool meter IN.wasm OUT.wasm\n");
    return 2;
}

static const struct {
    const char *name, *sig, *group;
} k_grants[] = {
    {"rat_add", "i(IIIIi)", "rational"}, {"rat_sub", "i(IIIIi)", "rational"},
    {"rat_mul", "i(IIIIi)", "rational"}, {"rat_div", "i(IIIIi)", "rational"},
    {"rat_cmp", "i(IIIIi)", "rational"}, {"rat_normalize", "i(IIi)", "rational"},
    {"lp_not", "i(i)", "logic"},         {"lp_and", "i(ii)", "logic"},
    {"lp_or", "i(ii)", "logic"},         {"lp_from_evidence", "i(ii)", "logic"},
    {"propose", "i(iIIii)", "propose"},
};

static int grant(zw_policy *p, const char *list) {
    for (size_t i = 0; i < sizeof k_grants / sizeof k_grants[0]; i++)
        if (strcmp(list, "all") == 0 || strstr(list, k_grants[i].group))
            if (zw_policy_allow_import(p, "zxv", k_grants[i].name, k_grants[i].sig) == ZXV_E_ARG)
                return -1;
    return 0;
}

static int cmd_validate(int argc, char **argv) {
    zw_policy *p;
    const char *file = NULL;
    if (zw_policy_create(&p) != ZXV_OK) return 2;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--pages") == 0 && i + 1 < argc) {
            if (zw_policy_set_limit(p, ZW_LIM_MEMORY_PAGES, strtoull(argv[++i], NULL, 10)))
                return usage();
        } else if (strcmp(argv[i], "--bulk") == 0) {
            (void) zw_policy_set_limit(p, ZW_LIM_BULK_MEMORY, 1);
        } else if (strcmp(argv[i], "--grant") == 0 && i + 1 < argc) {
            if (grant(p, argv[++i])) return usage();
        } else if (strcmp(argv[i], "--allow") == 0 && i + 1 < argc) {
            char buf[200];
            snprintf(buf, sizeof buf, "%s", argv[++i]);
            char *dot = strchr(buf, '.'), *eq = strchr(buf, '=');
            if (!dot || !eq || eq < dot) return usage();
            *dot = *eq = '\0';
            if (zw_policy_allow_import(p, buf, dot + 1, eq + 1) != ZXV_OK) return usage();
        } else if (!file) {
            file = argv[i];
        } else {
            return usage();
        }
    }
    if (!file) return usage();
    size_t len = 0;
    uint8_t *w = slurp(file, &len);
    if (!w) {
        fprintf(stderr, "zw-tool: cannot read %s\n", file);
        return 2;
    }
    int32_t reason = 0;
    uint32_t sec = 0, func = 0;
    uint64_t off = 0;
    char msg[512];
    int32_t st = zw_validate(p, w, len, &reason, &sec, &func, &off, msg, sizeof msg, NULL);
    if (st == ZW_OK) {
        printf("%s: accepted (%zu bytes)\n", file, len);
    } else {
        char secs[16] = "-", funcs[16] = "-";
        if (sec != ZW_SEC_HEADER) snprintf(secs, sizeof secs, "%u", (unsigned) sec);
        if (func != ZW_NO_FUNC) snprintf(funcs, sizeof funcs, "%u", (unsigned) func);
        printf("%s: REJECTED %s\n  section id: %s\n  function index: %s\n  byte offset: %llu\n"
               "  %s\n", file, zw_reason_name(reason), secs, funcs, (unsigned long long) off, msg);
    }
    free(w);
    zw_policy_destroy(p);
    return st == ZW_OK ? 0 : 1;
}

static int cmd_meter(int argc, char **argv) {
    size_t len = 0, olen = 0;
    uint8_t *w, *out = NULL;
    FILE *f;
    if (argc != 2) return usage();
    w = slurp(argv[0], &len);
    if (!w) return 2;
    if (zw_meter(w, len, &out, &olen) != ZW_OK) {
        fprintf(stderr, "zw-tool: %s cannot be metered (validate it first)\n", argv[0]);
        free(w);
        return 1;
    }
    f = fopen(argv[1], "wb");
    if (!f || fwrite(out, 1, olen, f) != olen) {
        if (f) fclose(f);
        free(w);
        free(out);
        return 2;
    }
    fclose(f);
    free(w);
    free(out);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    if (strcmp(argv[1], "validate") == 0) return cmd_validate(argc - 2, argv + 2);
    if (strcmp(argv[1], "meter") == 0) return cmd_meter(argc - 2, argv + 2);
    return usage();
}
