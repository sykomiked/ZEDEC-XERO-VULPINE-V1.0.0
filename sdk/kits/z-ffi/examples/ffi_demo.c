/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ffi_demo.c — calling libc and libm through the wrappers zf-gen made
 * from examples/libc.zf and examples/libm.zf. No float type in this file:
 * values go in and come out as exact rationals. */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "libc_zf.h"
#include "libm_zf.h"

static void report(const char *call, int32_t st, int64_t n, int64_t d) {
    char why[300], txt[64];
    size_t len = 0;
    if (st == ZXV_OK) {
        int dec = d != 1 && zxv_rat_format(n, d, txt, sizeof txt, &len) == ZXV_OK && !strchr(txt, '/');
        if (d == 1) printf("  %-22s = %lld\n", call, (long long) n);
        else printf("  %-22s = %lld/%lld%s%s%s\n", call, (long long) n, (long long) d, dec ? " = " : "",
                    dec ? txt : "", "");
    } else {
        if (zf_last_error(why, sizeof why, &len) != ZXV_OK) why[0] = '\0';
        printf("  %-22s refused, %s\n  %-22s   %s\n", call, zf_status_name(st), "", why);
    }
}

/* Make the call first, then report what it wrote (C leaves the order of
 * evaluating function arguments unspecified). */
#define SHOW(text, call, num, den)                                                             \
    do {                                                                                       \
        int32_t st_ = (call);                                                                  \
        report(text, st_, (num), (den));                                                       \
    } while (0)

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *cap = NULL;
    int64_t n = 0, d = 1, r = 0;
    uint64_t u = 0;
    if (zxv_ctx_create(&ctx) || zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) ||
        zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &cap))
        return 1;

    printf("libm (spec %.16s...):\n", ZF_LIBM_SPEC_SHA3);
    SHOW("sqrt(9/4)", zf_libm_sqrt(cap, 9, 4, &n, &d), n, d);
    SHOW("sqrt(2)", zf_libm_sqrt(cap, 2, 1, &n, &d), n, d);
    SHOW("sqrt(1/10)", zf_libm_sqrt(cap, 1, 10, &n, &d), n, d);
    SHOW("sqrt(-1)", zf_libm_sqrt(cap, -1, 1, &n, &d), n, d);
    SHOW("ldexp(3/4, 4)", zf_libm_ldexp(cap, 3, 4, 4, &n, &d), n, d);
    SHOW("ldexp(1, 1024)", zf_libm_ldexp(cap, 1, 1, 1024, &n, &d), n, d);

    printf("libc (spec %.16s...):\n", ZF_LIBC_SPEC_SHA3);
    SHOW("abs(-42)", zf_libc_abs(cap, -42, &r), r, 1);
    SHOW("abs(INT_MIN)", zf_libc_abs(cap, INT_MIN, &r), r, 1);
    SHOW("toupper(300)", zf_libc_toupper(cap, 300, &r), r, 1);
    SHOW("strlen(\"ZEDEC\")", zf_libc_strlen(cap, "ZEDEC", 5, &u), (int64_t) u, 1);
    SHOW("abs(-42), no cap", zf_libc_abs(NULL, -42, &r), r, 1);

    zxv_cap_destroy(cap);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    return 0;
}
