/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* za_tool.c — ingest one payload file and print its record and label.
 *
 *   za-tool json|fix|fix-pipe|mqtt PAYLOAD [CONTRACT.zc FN] [TOPIC]
 *
 * mqtt needs the topic as the last argument. With a contract, the record
 * is validated against function FN. No network access: the payload is a
 * file. Exit status: 0 accepted, 1 refused, 2 usage or I/O error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zadapter.h"

static unsigned char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    unsigned char *b = NULL;
    size_t cap = 0, n = 0;
    if (!f) return NULL;
    for (;;) {
        if (n == cap) {
            cap = cap ? cap * 2u : 4096u;
            if (cap > (size_t) 2 * ZA_MAX_PAYLOAD) break;
            unsigned char *nb = (unsigned char *) realloc(b, cap);
            if (!nb) break;
            b = nb;
        }
        size_t r = fread(b + n, 1, cap - n, f);
        n += r;
        if (r == 0) break;
    }
    fclose(f);
    *len = n;
    return b ? b : (unsigned char *) calloc(1, 1);
}

static void print_record(const za_record *r) {
    int32_t lp = 0, tag = 0, kind = 0;
    uint32_t nf = 0, nr = 0;
    const char *src = "";
    char tn[64];
    size_t tl = 0;
    za_record_state(r, &lp, &tag);
    za_record_source(r, &kind, &src);
    zc_tag_format(tag, tn, sizeof tn, &tl);
    printf("record from '%s': %s [%s]\n", src,
           lp == ZXV_LP_NEITHER ? "accepted, unverified" : lp == ZXV_LP_FALSE ? "REFUSED" : zxv_lp_name(lp), tn);
    za_record_reason_count(r, &nr);
    for (uint32_t i = 0; i < nr; i++) {
        int32_t code = 0;
        const char *m = "";
        za_record_reason(r, i, &code, &m);
        printf("  reason %s: %s\n", za_reason_name(code), m);
    }
    za_record_field_count(r, &nf);
    for (uint32_t i = 0; i < nf; i++) {
        const char *name = "";
        const uint8_t *t = NULL;
        size_t len = 0;
        int64_t n = 0, d = 1;
        za_field(r, i, &name, &kind);
        za_field_text(r, i, &t, &len);
        if (kind == ZA_F_NUMBER && za_field_rat(r, i, &n, &d) == ZXV_OK) {
            char b[64];
            size_t bl = 0;
            zxv_rat_format(n, d, b, sizeof b, &bl);
            if (d != 1) printf("  %-24s number %s (%lld/%lld)\n", name, b, (long long) n, (long long) d);
            else printf("  %-24s number %s\n", name, b);
        } else {
            printf("  %-24s %s \"%.*s\"\n", name, kind == ZA_F_TEXT ? "text  " : kind == ZA_F_BOOL ? "bool  " : "null  ",
                   (int) (len > 60u ? 60u : len), (const char *) t);
        }
    }
}

int main(int argc, char **argv) {
    int32_t kind;
    const char *topic = NULL;
    size_t len = 0;
    za_record *r = NULL;
    if (argc < 3) {
        fprintf(stderr, "usage: za-tool json|fix|fix-pipe|mqtt PAYLOAD [CONTRACT.zc FN] [TOPIC]\n");
        return 2;
    }
    if (strcmp(argv[1], "json") == 0) kind = ZA_SRC_JSON;
    else if (strcmp(argv[1], "fix") == 0) kind = ZA_SRC_FIX;
    else if (strcmp(argv[1], "fix-pipe") == 0) kind = ZA_SRC_FIX_PIPE;
    else if (strcmp(argv[1], "mqtt") == 0) kind = ZA_SRC_MQTT;
    else {
        fprintf(stderr, "za-tool: unknown adapter '%s'\n", argv[1]);
        return 2;
    }
    if (kind == ZA_SRC_MQTT) {
        if (argc != 4 && argc != 6) {
            fprintf(stderr, "za-tool: mqtt needs the topic as the last argument\n");
            return 2;
        }
        topic = argv[argc - 1];
    }
    unsigned char *payload = slurp(argv[2], &len);
    if (!payload) {
        fprintf(stderr, "za-tool: cannot read %s\n", argv[2]);
        return 2;
    }
    if (za_ingest(kind, topic ? topic : argv[2], payload, len, &r) != ZXV_OK) return 2;
    int with_contract = (kind == ZA_SRC_MQTT) ? argc == 6 : argc >= 5;
    if (with_contract) {
        size_t cl = 0;
        unsigned char *ct = slurp(argv[3], &cl);
        zc_contract *c = NULL;
        char msg[300];
        if (!ct || zc_parse((const char *) ct, cl, &c, NULL, NULL, NULL, msg, sizeof msg, NULL) != ZXV_OK) {
            fprintf(stderr, "za-tool: %s: %s\n", argv[3], ct ? msg : "cannot read");
            return 2;
        }
        int32_t st = za_validate(r, c, argv[4], NULL);
        if (st != ZXV_OK && st != ZA_E_REFUSED && st != ZXV_E_STATE) {
            fprintf(stderr, "za-tool: validate: %s\n", za_status_name(st));
            return 2;
        }
        printf("contract %s: %s\n", argv[4], st == ZXV_OK ? "passed" : "not passed");
        zc_contract_destroy(c);
        free(ct);
    }
    print_record(r);
    int32_t lp = 0;
    za_record_state(r, &lp, NULL);
    za_record_destroy(r);
    free(payload);
    return lp == ZXV_LP_FALSE ? 1 : 0;
}
