/* ipfsn_util.h — private helpers for src/ipfs_node (no libc in the kernel).
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_IPFSN_UTIL_H
#define ZXV_IPFSN_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include "ipfs_node.h"

static inline void ipfsn__cpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

/* Overlap-safe when dst <= src. */
static inline void ipfsn__move_down(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

static inline void ipfsn__set(void *dst, uint8_t v, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = v;
}

static inline bool ipfsn__eq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a;
    const uint8_t *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

static inline uint32_t ipfsn__strlen(const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

static inline void ipfsn__cid_copy(ipfsn_cid_t *d, const ipfsn_cid_t *s)
{
    ipfsn__cpy(d, s, (uint32_t) sizeof(*d));
}

static inline void ipfsn__wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static inline uint32_t ipfsn__rd32le(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

#endif /* ZXV_IPFSN_UTIL_H */
