/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* z-audit — check a ZXV plugin before it is published.
 *
 *   z-audit [options] --contract FILE.zc PLUGIN.wasm
 *
 *   --contract FILE.zc   the plugin's z-contract (required unless --no-contract)
 *   --no-contract        audit without one (every exported function then fails)
 *   --grant LIST         the host's Z-Wasm grants: all, or rational,logic,propose
 *   --allow MOD.NAME=SIG one more allowed import (wasm3 form, e.g. i(IIi))
 *   --pages N            the memory page limit (default 16)
 *   --bulk               allow bulk-memory instructions
 *   --strict             warnings fail the audit too
 *   --no-analysis        skip the bounded analysis
 *   --json FILE          write the JSON report to FILE ("-" for stdout)
 *   --quiet              no text report
 *
 * Exit status: 0 the audit passed, 1 it failed, 2 usage or I/O error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zaudit.h"
#include "zwasm.h"

static unsigned char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    unsigned char *b = NULL;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    b = (unsigned char *) malloc((size_t) n + 1u);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *len = (size_t) n;
    return b;
}

static int usage(void) {
    fprintf(stderr,
            "usage: z-audit [--grant LIST] [--allow MOD.NAME=SIG]... [--pages N] [--bulk]\n"
            "               [--strict] [--no-analysis] [--json FILE|-] [--quiet]\n"
            "               (--contract FILE.zc | --no-contract) PLUGIN.wasm\n"
            "exit status: 0 pass, 1 fail, 2 usage or I/O error\n");
    return 2;
}

static int write_out(const char *path, const char *s, size_t n) {
    FILE *f = strcmp(path, "-") == 0 ? stdout : fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(s, 1, n, f) == n;
    if (f != stdout) ok = (fclose(f) == 0) && ok;
    else fflush(stdout);
    return ok ? 0 : -1;
}

/* Render through the R4 two-call buffer. */
static char *render(const zau_report *r, const char *ml, const char *cl, int json, size_t *len) {
    size_t n = 0;
    char *b;
    int32_t st = json ? zau_report_json(r, ml, cl, NULL, 0, &n) : zau_report_text(r, ml, cl, NULL, 0, &n);
    if (st != ZXV_E_BUFFER) return NULL;
    b = (char *) malloc(n + 1u);
    if (!b) return NULL;
    st = json ? zau_report_json(r, ml, cl, b, n + 1u, &n) : zau_report_text(r, ml, cl, b, n + 1u, &n);
    if (st != ZXV_OK) {
        free(b);
        return NULL;
    }
    *len = n;
    return b;
}

/* Everything but creating and destroying the options, so that every return
 * path leaves nothing allocated (the sanitizer builds check this). */
static int run(zau_options *o, int argc, char **argv) {
    zau_report *r = NULL;
    const char *wasm_path = NULL, *contract_path = NULL, *json_path = NULL;
    int no_contract = 0, quiet = 0;
    uint32_t flags = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--contract") == 0 && i + 1 < argc) {
            contract_path = argv[++i];
        } else if (strcmp(a, "--no-contract") == 0) {
            no_contract = 1;
        } else if (strcmp(a, "--grant") == 0 && i + 1 < argc) {
            const char *l = argv[++i];
            uint32_t g = 0;
            if (strcmp(l, "all") == 0) g = ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE;
            if (strstr(l, "rational")) g |= ZW_GRANT_RATIONAL;
            if (strstr(l, "logic")) g |= ZW_GRANT_LOGIC;
            if (strstr(l, "propose")) g |= ZW_GRANT_PROPOSE;
            if (!g || zau_options_grant(o, g) != ZXV_OK) return usage();
        } else if (strcmp(a, "--allow") == 0 && i + 1 < argc) {
            char b[200];
            snprintf(b, sizeof b, "%s", argv[++i]);
            char *dot = strchr(b, '.'), *eq = strchr(b, '=');
            if (!dot || !eq || eq < dot) return usage();
            *dot = *eq = '\0';
            if (zau_options_allow_import(o, b, dot + 1, eq + 1) != ZXV_OK) return usage();
        } else if (strcmp(a, "--pages") == 0 && i + 1 < argc) {
            if (zau_options_set_limit(o, ZW_LIM_MEMORY_PAGES, strtoull(argv[++i], NULL, 10)) != ZXV_OK)
                return usage();
        } else if (strcmp(a, "--bulk") == 0) {
            (void) zau_options_set_limit(o, ZW_LIM_BULK_MEMORY, 1);
        } else if (strcmp(a, "--strict") == 0) {
            flags |= ZAU_F_STRICT;
        } else if (strcmp(a, "--no-analysis") == 0) {
            flags |= ZAU_F_NO_ANALYSIS;
        } else if (strcmp(a, "--json") == 0 && i + 1 < argc) {
            json_path = argv[++i];
        } else if (strcmp(a, "--quiet") == 0) {
            quiet = 1;
        } else if (a[0] == '-' && a[1] == '-') {
            return usage();
        } else if (!wasm_path) {
            wasm_path = a;
        } else {
            return usage();
        }
    }
    if (!wasm_path || (!contract_path && !no_contract) || (contract_path && no_contract)) return usage();
    (void) zau_options_set_flags(o, flags);

    size_t wlen = 0, clen = 0;
    unsigned char *w = slurp(wasm_path, &wlen), *c = NULL;
    if (!w) {
        fprintf(stderr, "z-audit: cannot read %s\n", wasm_path);
        return 2;
    }
    if (contract_path && !(c = slurp(contract_path, &clen))) {
        fprintf(stderr, "z-audit: cannot read %s\n", contract_path);
        free(w);
        return 2;
    }
    if (zau_audit(o, w, wlen, (const char *) c, c ? clen : 0, &r) != ZXV_OK) {
        fprintf(stderr, "z-audit: the audit could not run (out of memory)\n");
        free(w);
        free(c);
        return 2;
    }
    int rc = 0;
    size_t n = 0;
    char *out;
    if (json_path) {
        out = render(r, wasm_path, contract_path, 1, &n);
        if (!out || write_out(json_path, out, n) != 0) {
            fprintf(stderr, "z-audit: cannot write the JSON report to %s\n", json_path);
            rc = 2;
        }
        free(out);
    }
    if (!quiet && !(json_path && strcmp(json_path, "-") == 0)) {
        out = render(r, wasm_path, contract_path, 0, &n);
        if (out) fwrite(out, 1, n, stdout);
        free(out);
    }
    int32_t pass = 0;
    (void) zau_report_verdict(r, &pass, NULL, NULL);
    if (!rc) rc = pass ? 0 : 1;
    zau_report_destroy(r);
    free(w);
    free(c);
    return rc;
}

int main(int argc, char **argv) {
    zau_options *o = NULL;
    if (zau_options_create(&o) != ZXV_OK) return 2;
    int rc = run(o, argc, argv);
    zau_options_destroy(o);
    return rc;
}
