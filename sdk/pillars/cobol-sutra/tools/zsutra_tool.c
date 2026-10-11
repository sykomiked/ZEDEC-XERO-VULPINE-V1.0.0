/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zsutra_tool.c — the command-line tool.
 *
 *   zsutra layout   FILE          canonical Sutra, record size and digest
 *   zsutra copybook FILE          the same layout as a fixed-format copybook
 *   zsutra fields   FILE          every field: offset, size, picture, usage
 *   zsutra same     FILE FILE     do two files declare the same record?
 *   zsutra decode   FILE HEXFILE  decode records (one hex line each)
 *
 * FILE is Sutra when it ends in .sutra, otherwise a copybook (fixed
 * format; --free before the command for free format). --ascii-sign reads
 * overpunched signs in the ASCII (GnuCOBOL default) convention. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zsutra.h"

static uint32_t g_format = ZS_CB_FIXED;
static int32_t g_overpunch = ZS_OVERPUNCH_IBM;

static char *slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    char *b = NULL;
    size_t n = 0, cap = 0;
    if (!f) return NULL;
    for (;;) {
        size_t r;
        if (n + 4096u + 1u > cap) {
            char *nb;
            cap = cap ? cap * 2u : 8192u;
            nb = (char *) realloc(b, cap);
            if (!nb) {
                free(b);
                fclose(f);
                return NULL;
            }
            b = nb;
        }
        r = fread(b + n, 1, cap - n - 1u, f);
        n += r;
        if (r == 0) break;
    }
    fclose(f);
    b[n] = '\0';
    *out_len = n;
    return b;
}

static int ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

/* Parse FILE; print a compiler-style error on failure. */
static zs_layout *load(const char *path) {
    size_t len = 0;
    char *text = slurp(path, &len);
    zs_layout *l = NULL;
    zs_diag d;
    int32_t st;
    if (!text) {
        fprintf(stderr, "%s: cannot read\n", path);
        return NULL;
    }
    st = ends_with(path, ".sutra") ? zs_sutra_parse(text, len, &l, &d)
                                   : zs_copybook_parse(text, len, g_format, &l, &d);
    if (st != ZXV_OK) {
        const char *msg = strstr(d.msg, ": ");
        size_t ls = (size_t) d.offset, le;
        while (ls > 0 && text[ls - 1u] != '\n') ls--;
        le = ls;
        while (le < len && text[le] != '\n' && text[le] != '\r') le++;
        fprintf(stderr, "%s:%u:%u: %s [%s]\n", path, (unsigned) d.line, (unsigned) d.col,
                msg ? msg + 2 : d.msg, zs_status_name(st));
        fprintf(stderr, "  %.*s\n  %*s^\n", (int) (le - ls), text + ls, (int) (d.col - 1u), "");
        free(text);
        return NULL;
    }
    free(text);
    zs_layout_set_overpunch(l, g_overpunch);
    return l;
}

static void print_digest(const zs_layout *l) {
    uint8_t dg[32];
    zs_layout_digest(l, dg);
    for (int i = 0; i < 32; i++) printf("%02x", dg[i]);
}

static int print_text(const zs_layout *l, int copybook) {
    size_t n = 0;
    char *b;
    if (copybook) zs_layout_copybook(l, NULL, 0, &n);
    else zs_layout_sutra(l, NULL, 0, &n);
    b = (char *) malloc(n + 1u);
    if (!b) return 1;
    if (copybook) zs_layout_copybook(l, b, n + 1u, &n);
    else zs_layout_sutra(l, b, n + 1u, &n);
    fputs(b, stdout);
    free(b);
    return 0;
}

static const char *usage_name(int32_t u) {
    return u == ZS_U_COMP ? "comp" : u == ZS_U_COMP3 ? "comp-3" : "display";
}

static int cmd_fields(const zs_layout *l) {
    uint32_t n = 0;
    zs_field_count(l, &n);
    printf("%-32s %7s %6s %-6s %-8s %s\n", "field", "offset", "size", "occurs", "usage", "picture");
    for (uint32_t i = 0; i < n; i++) {
        zs_field f;
        char pic[48] = "";
        zs_field_at(l, i, &f);
        if (f.kind == ZS_K_TEXT) snprintf(pic, sizeof pic, "x(%u)", (unsigned) f.digits);
        else if (f.kind == ZS_K_NUM) {
            int k = snprintf(pic, sizeof pic, "%s", f.sign ? "s" : "");
            if (f.digits > f.scale)
                k += snprintf(pic + k, sizeof pic - (size_t) k, "9(%u)", (unsigned) (f.digits - f.scale));
            if (f.scale) snprintf(pic + k, sizeof pic - (size_t) k, "v9(%u)", (unsigned) f.scale);
        }
        printf("%*s%-*s %7llu %6llu %-6u %-8s %s\n", (int) (2u * f.depth), "",
               (int) (32u - 2u * (f.depth > 16u ? 16u : f.depth)), f.name,
               (unsigned long long) f.offset, (unsigned long long) f.size, (unsigned) f.occurs,
               f.kind == ZS_K_GROUP ? "group" : usage_name(f.usage), pic);
    }
    return 0;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int cmd_decode(const zs_layout *l, const char *hexpath) {
    size_t len = 0, pos = 0;
    char *text = slurp(hexpath, &len);
    uint64_t size = 0;
    uint32_t nf = 0, recno = 0;
    uint8_t *rec;
    int rc = 0;
    if (!text) {
        fprintf(stderr, "%s: cannot read\n", hexpath);
        return 2;
    }
    zs_layout_size(l, &size);
    zs_field_count(l, &nf);
    rec = (uint8_t *) malloc((size_t) size + 1u);
    while (rec && pos < len) {
        size_t n = 0, w = pos;
        int hi = -1;
        /* skip comment lines, and a leading label such as "payment" */
        if (text[pos] == '#') {
            while (pos < len && text[pos] != '\n') pos++;
            pos++;
            continue;
        }
        while (w < len && text[w] != ' ' && text[w] != '\n' && hexval(text[w]) >= 0) w++;
        if (w < len && text[w] != ' ' && text[w] != '\n')
            while (pos < len && text[pos] != ' ' && text[pos] != '\n') pos++;
        while (pos < len && text[pos] != '\n') {
            int v = hexval(text[pos++]);
            if (v < 0) continue; /* spaces between bytes */
            if (hi < 0) hi = v;
            else {
                if (n < size) rec[n] = (uint8_t) (hi << 4 | v);
                n++;
                hi = -1;
            }
        }
        pos++;
        if (n == 0) continue;
        recno++;
        if (n != size) {
            printf("record %u: %zu bytes, the layout needs %llu\n", (unsigned) recno, n,
                   (unsigned long long) size);
            rc = 1;
            continue;
        }
        printf("record %u\n", (unsigned) recno);
        for (uint32_t i = 1; i < nf; i++) {
            zs_field f;
            uint32_t occ[8], sub[8], nd = 0;
            zs_field_at(l, i, &f);
            if (f.kind == ZS_K_GROUP || strcmp(f.name, "FILLER") == 0) continue;
            /* the OCCURS counts from the outermost table in */
            for (uint32_t k = i;;) {
                zs_field a;
                zs_field_at(l, k, &a);
                if (a.occurs && nd < 8) occ[nd++] = a.occurs;
                if (k == 0) break;
                k = a.parent;
            }
            for (uint32_t k = 0; k < nd / 2u; k++) {
                uint32_t t = occ[k];
                occ[k] = occ[nd - 1u - k];
                occ[nd - 1u - k] = t;
            }
            for (uint32_t k = 0; k < nd; k++) sub[k] = 1;
            for (;;) {
                char ref[160], out[512];
                size_t k = 0, rn;
                int32_t st;
                uint32_t d;
                rn = (size_t) snprintf(ref, sizeof ref, "%s", f.name);
                for (d = 0; d < nd; d++)
                    rn += (size_t) snprintf(ref + rn, sizeof ref - rn, "%s%u", d ? "," : "(",
                                            (unsigned) sub[d]);
                if (nd) snprintf(ref + rn, sizeof ref - rn, ")");
                st = zs_get_value(l, rec, (size_t) size, ref, out, sizeof out, &k);
                if (st == ZXV_OK) printf("  %s = %s\n", ref, out);
                else {
                    char why[512];
                    size_t wn = 0;
                    zs_last_error(why, sizeof why, &wn);
                    printf("  %s: %s (%s)\n", ref, zs_status_name(st), why);
                    rc = 1;
                }
                for (d = nd; d-- > 0;) {
                    if (++sub[d] <= occ[d]) break;
                    sub[d] = 1;
                }
                if (d == UINT32_MAX) break;
            }
        }
    }
    free(rec);
    free(text);
    return rc;
}

static int usage(void) {
    fprintf(stderr,
            "usage: zsutra [--free] [--ascii-sign] layout|copybook|fields FILE\n"
            "       zsutra same FILE FILE\n"
            "       zsutra decode FILE HEXFILE\n"
            "FILE: *.sutra is Sutra, anything else a copybook (fixed format unless --free)\n");
    return 2;
}

int main(int argc, char **argv) {
    int a = 1, rc;
    zs_layout *l, *l2;
    while (a < argc && strncmp(argv[a], "--", 2) == 0) {
        if (strcmp(argv[a], "--free") == 0) g_format = ZS_CB_FREE;
        else if (strcmp(argv[a], "--ascii-sign") == 0) g_overpunch = ZS_OVERPUNCH_ASCII;
        else return usage();
        a++;
    }
    if (argc - a < 2) return usage();
    if (strcmp(argv[a], "same") == 0) {
        int32_t same = 0;
        if (argc - a != 3) return usage();
        if (!(l = load(argv[a + 1]))) return 1;
        if (!(l2 = load(argv[a + 2]))) {
            zs_layout_destroy(l);
            return 1;
        }
        zs_layout_same(l, l2, &same);
        if (same) {
            printf("same record: %s and %s, sha3-256 ", argv[a + 1], argv[a + 2]);
            print_digest(l);
            printf("\n");
        } else {
            printf("DIFFERENT records:\n--- %s\n", argv[a + 1]);
            print_text(l, 0);
            printf("--- %s\n", argv[a + 2]);
            print_text(l2, 0);
        }
        zs_layout_destroy(l);
        zs_layout_destroy(l2);
        return same ? 0 : 1;
    }
    if (!(l = load(argv[a + 1]))) return 1;
    if (strcmp(argv[a], "layout") == 0 && argc - a == 2) {
        uint64_t size = 0;
        rc = print_text(l, 0);
        zs_layout_size(l, &size);
        printf("# %llu bytes, sha3-256 ", (unsigned long long) size);
        print_digest(l);
        printf("\n");
    } else if (strcmp(argv[a], "copybook") == 0 && argc - a == 2) {
        rc = print_text(l, 1);
    } else if (strcmp(argv[a], "fields") == 0 && argc - a == 2) {
        rc = cmd_fields(l);
    } else if (strcmp(argv[a], "decode") == 0 && argc - a == 3) {
        rc = cmd_decode(l, argv[a + 2]);
    } else {
        rc = usage();
    }
    zs_layout_destroy(l);
    return rc;
}
