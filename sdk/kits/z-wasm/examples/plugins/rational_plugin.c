/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* rational_plugin.c — an integer / exact-rational ZXV plugin.
 *
 *   run(n)              sums 1/(k(k+1)) for k = 1..n exactly with zxv.rat_add
 *                       (the telescoping sum n/(n+1)), checks the closed form
 *                       with zxv.rat_cmp, turns the check into four-valued
 *                       logic, and PROPOSES the sum (kind 1). Returns the
 *                       ZXV_LP_* verdict, or a negative ZXV status.
 *   fee(amount, bps)    proposes amount * bps / 10000 exactly (kind 2): an
 *                       integer when it divides, a fraction otherwise; the
 *                       host decides whether a fraction can be posted.
 *   checksum(n)         pure integer work (no host calls), for fuel tests.
 *
 * Build (see the kit Makefile):
 *   python3 -m ziglang cc --target=wasm32-freestanding -O2 -nostdlib \
 *     -mno-bulk-memory -Wl,--no-entry -Wl,--max-memory=131072 ... */
#include "zxv_plugin.h"

ZXV_EXPORT("run") int32_t run(int64_t n) {
    zxv_rat acc = {0, 1};
    int32_t st, cmp = 0;
    if (n < 1 || n > 1000) return -1; /* ZXV_E_ARG */
    for (int64_t k = 1; k <= n; k++) {
        st = zxv_rat_add(acc.num, acc.den, 1, k * (k + 1), &acc);
        if (st != ZXV_OK) return st;
    }
    st = zxv_rat_cmp(acc.num, acc.den, n, n + 1, &cmp);
    if (st != ZXV_OK) return st;
    int32_t verdict = zxv_lp_from_evidence(cmp == 0, cmp != 0);
    static const char label[] = "telescoping sum n/(n+1)";
    st = zxv_propose(1, acc.num, acc.den, label, zxv_strlen(label));
    if (st != ZXV_OK) return st;
    return verdict;
}

ZXV_EXPORT("fee") int32_t fee(int64_t amount_minor, int64_t bps) {
    zxv_rat f;
    int32_t st = zxv_rat_mul(amount_minor, 1, bps, 10000, &f);
    if (st != ZXV_OK) return st;
    static const char label[] = "fee in minor units";
    return zxv_propose(2, f.num, f.den, label, zxv_strlen(label));
}

ZXV_EXPORT("checksum") int64_t checksum(int64_t n) {
    uint64_t h = 1469598103934665603ull; /* FNV-1a over 0..n-1 */
    for (int64_t i = 0; i < n; i++) {
        h ^= (uint64_t) i;
        h *= 1099511628211ull;
    }
    return (int64_t) h;
}
