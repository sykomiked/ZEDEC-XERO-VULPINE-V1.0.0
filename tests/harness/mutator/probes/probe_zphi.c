/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* probe_zphi.c: differential probe for kernel/src/e8/zphi.c (classify.py). */
#include <stdint.h>
#include "probe_common.h"
#include "zphi.h"

static void pz(const char *tag, zphi_t x) {
    printf(" %s%d:%lld,%lld", tag, (int)x.valid, (long long)x.a, (long long)x.b);
}

int main(void) {
    static const int64_t v[] = {INT64_MIN, INT64_MIN + 1, -3037000500LL, -1000003, -7, -2, -1, 0, 1, 2, 3, 5,
                                8, 1000003, 3037000499LL, 3037000500LL, INT64_MAX - 1, INT64_MAX};
    const unsigned n = sizeof v / sizeof v[0];
    zphi_t k[6] = {zphi_zero(), zphi_one(), zphi_golden(), zphi_inv_golden(), zphi_invalid(), zphi_int(-4)};
    for (int i = 0; i < 6; i++) { pz("k", k[i]); printf(" %a\n", zphi_to_double(k[i])); }
    for (unsigned i = 0; i < n; i++)
        for (unsigned j = 0; j < n; j++) {
            zphi_t x = zphi_make(v[i], v[j]);
            bool ok1 = 0, ok2 = 0;
            int64_t nm = zphi_norm(x, &ok1), tr = zphi_trace(x, &ok2);
            printf("x %u %u", i, j);
            pz("", x);
            pz("neg", zphi_neg(x));
            pz("conj", zphi_conj(x));
            printf(" n%lld/%d t%lld/%d z%d i%d d%a", (long long)nm, (int)ok1, (long long)tr, (int)ok2,
                   (int)zphi_is_zero(x), (int)zphi_is_int(x), zphi_to_double(x));
            for (unsigned m = 0; m < n; m += 3) pz("s", zphi_scale(x, v[m]));
            printf("\n");
            for (unsigned p = 0; p < n; p += 2)
                for (unsigned q = 1; q < n; q += 3) {
                    zphi_t y = zphi_make(v[p], v[q]);
                    zphi_t s = zphi_add(x, y), d = zphi_sub(x, y), m = zphi_mul(x, y);
                    printf("  %u.%u", p, q);
                    pz("+", s); pz("-", d); pz("*", m);
                    printf(" eq%d %a %a %a\n", (int)zphi_eq(x, y), zphi_to_double(s), zphi_to_double(d), zphi_to_double(m));
                }
            for (int t = 0; t < 6; t++) {
                pz(" kk", zphi_add(x, k[t])); pz("", zphi_mul(x, k[t])); pz("", zphi_sub(k[t], x));
                printf(" e%d", (int)zphi_eq(x, k[t]));
            }
            printf("\n");
        }
    bool ok;
    printf("norm null %lld trace null %lld\n", (long long)zphi_norm(zphi_make(3, 4), NULL),
           (long long)zphi_trace(zphi_make(3, 4), NULL));
    printf("norm invalid %lld", (long long)zphi_norm(zphi_invalid(), &ok)); printf(" %d\n", (int)ok);
    zphi_t junk = {7, 3, false}; /* the type is public: invalid with leftover fields */
    printf("junk %a %d %d %d %d\n", zphi_to_double(junk), (int)zphi_eq(junk, zphi_make(7, 3)),
           (int)zphi_is_zero(junk), (int)zphi_is_int(junk), (int)zphi_add(junk, zphi_one()).valid);
    printf("selfcheck %u\n", zphi_selfcheck());
    return 0;
}
