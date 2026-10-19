/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zscob_shim.c — a thin C shim so a GnuCOBOL program can CALL libzsutra
 * and libzxv with BY REFERENCE records (cobol/PAYPOST.cbl).
 *
 * COBOL passes everything by reference:
 *   PIC X(n)            -> char * (space padded, not NUL-terminated)
 *   PIC S9(9)  COMP-5   -> int32_t *  (native binary)
 *   PIC S9(18) COMP-5   -> int64_t *
 *   the 01 record       -> unsigned char * (its bytes, as the copybook says)
 *
 * The shim holds one context, a root capability (kept here, never given
 * to COBOL) and an attenuated LEDGER_POST | LEDGER_READ capability that
 * every posting goes through. It is an example of the pattern, not a
 * general binding: the posting map names the fields of payment.cpy.
 * Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zsutra.h"

/* COBOL calls these by name; declared here for -Wmissing-prototypes. */
int zscob_open(const char *path, const int32_t *overpunch, int32_t *rc);
int zscob_currency(const char *ccy3, int32_t *rc);
int zscob_account(const char *name30, const char *ccy3, const int32_t *kind, int32_t *out_id,
                  int32_t *rc);
int zscob_post(const unsigned char *rec, int64_t *entry_id, int32_t *rc, char *status24);
int zscob_value(const unsigned char *rec, const char *field30, char *out40, int32_t *rc);
int zscob_balance(const int32_t *account, int64_t *equity, int32_t *rc);
int zscob_check(int32_t *rc, char *status24);
int zscob_close(void);

static zxv_ctx *g_ctx;
static zxv_cap *g_root, *g_poster;
static zs_layout *g_layout;
static uint64_t g_size;

static const zs_posting_map k_map = {"PAY-AMOUNT", "PAY-FROM", "PAY-TO", "PAY-CCY",
                                     NULL,         "PAY-REF",  -1};

/* a space-padded COBOL field -> a C string */
static void from_cobol(const char *src, size_t n, char *dst) {
    while (n > 0 && (src[n - 1u] == ' ' || src[n - 1u] == '\0')) n--;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* a C string -> a space-padded COBOL field */
static void to_cobol(const char *src, char *dst, size_t n) {
    size_t k = strlen(src);
    if (k > n) k = n;
    memcpy(dst, src, k);
    memset(dst + k, ' ', n - k);
}

int zscob_open(const char *path, const int32_t *overpunch, int32_t *rc) {
    char p[257], *text = NULL;
    FILE *f;
    long n;
    zs_diag d;
    from_cobol(path, 256, p);
    *rc = ZXV_E_IO;
    if (!(f = fopen(p, "rb"))) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char *) malloc((size_t) n + 1u);
    if (text && fread(text, 1, (size_t) n, f) == (size_t) n) {
        *rc = zs_copybook_parse(text, (size_t) n, ZS_CB_FIXED, &g_layout, &d);
        if (*rc != ZXV_OK) fprintf(stderr, "%s: %s\n", p, d.msg);
    }
    free(text);
    fclose(f);
    if (*rc != ZXV_OK) return 0;
    zs_layout_set_overpunch(g_layout, *overpunch);
    zs_layout_size(g_layout, &g_size);
    if ((*rc = zxv_ctx_create(&g_ctx)) != ZXV_OK) return 0;
    if ((*rc = zxv_cap_create(g_ctx, 1, NULL, ZXV_CAP_ALL, &g_root)) != ZXV_OK) return 0;
    *rc = zxv_cap_attenuate(g_root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &g_poster);
    return 0;
}

int zscob_currency(const char *ccy3, int32_t *rc) {
    char c[4];
    from_cobol(ccy3, 3, c);
    *rc = zxv_ccy_enable_cap(g_root, c);
    return 0;
}

int zscob_account(const char *name30, const char *ccy3, const int32_t *kind, int32_t *out_id,
                  int32_t *rc) {
    char name[31], c[4];
    uint32_t id = 0;
    from_cobol(name30, 30, name);
    from_cobol(ccy3, 3, c);
    *rc = zxv_ledger_open_account_cap(g_root, name, c, *kind, &id);
    if (*rc == ZXV_OK) *out_id = (int32_t) id;
    return 0;
}

int zscob_post(const unsigned char *rec, int64_t *entry_id, int32_t *rc, char *status24) {
    zs_posting p;
    *rc = zs_post(g_poster, g_layout, &k_map, rec, (size_t) g_size, &p);
    *entry_id = *rc == ZXV_OK ? (int64_t) p.entry_id : 0;
    to_cobol(zs_status_name(*rc), status24, 24);
    return 0;
}

int zscob_value(const unsigned char *rec, const char *field30, char *out40, int32_t *rc) {
    char ref[31], buf[41];
    size_t n = 0;
    from_cobol(field30, 30, ref);
    *rc = zs_get_value(g_layout, rec, (size_t) g_size, ref, buf, sizeof buf, &n);
    to_cobol(*rc == ZXV_OK ? buf : zs_status_name(*rc), out40, 40);
    return 0;
}

int zscob_balance(const int32_t *account, int64_t *equity, int32_t *rc) {
    *rc = zxv_ledger_balance_cap(g_poster, (uint32_t) *account, NULL, NULL, equity);
    return 0;
}

int zscob_check(int32_t *rc, char *status24) {
    *rc = zxv_ledger_check_cap(g_poster);
    to_cobol(zs_status_name(*rc), status24, 24);
    return 0;
}

int zscob_close(void) {
    zxv_cap_destroy(g_poster);
    zxv_cap_destroy(g_root);
    zxv_ctx_destroy(g_ctx);
    zs_layout_destroy(g_layout);
    g_poster = g_root = NULL;
    g_ctx = NULL;
    g_layout = NULL;
    return 0;
}
