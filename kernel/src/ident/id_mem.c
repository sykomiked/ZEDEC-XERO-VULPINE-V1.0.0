/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* id_mem.c -- byte helpers. Kept apart from id_core.c so that code needing
 * only Argon2id (id_argon2.c), such as disk encryption, links without the
 * post-quantum signature code id_core.c pulls in. */
#include "id_internal.h"

void id_mcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    if (d == s || n == 0) return;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
}

void id_mset(void *dst, uint8_t v, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    for (size_t i = 0; i < n; i++) d[i] = v;
}

bool id_meq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

void id_wipe(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *) p;
    for (size_t i = 0; i < n; i++) v[i] = 0;
}
