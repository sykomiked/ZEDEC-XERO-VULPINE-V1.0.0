/* zxv_rat_shim.c — the SDK's rational ABI (zxv_rat_*) over the kernel's
 * exact rational, for the browser's wasm app engine. The SDK's own
 * implementation lives in libzxv; the engine links this small adapter
 * instead of the whole SDK. Every result is reduced by the kernel's
 * rat_make, so the wasm and the node agree bit for bit.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>

#include <string.h>

#include "rational.h"
#include "keccak.h"

static void put(rat_t r, int64_t *num, int64_t *den)
{
    *num = r.num;
    *den = r.den;
}

int32_t zxv_rat_add(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *out_num,
                    int64_t *out_den)
{
    if (!out_num || !out_den) return -1;
    put(rat_add(rat_make(an, ad), rat_make(bn, bd)), out_num, out_den);
    return 0;
}

int32_t zxv_rat_sub(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *out_num,
                    int64_t *out_den)
{
    if (!out_num || !out_den) return -1;
    put(rat_sub(rat_make(an, ad), rat_make(bn, bd)), out_num, out_den);
    return 0;
}

int32_t zxv_rat_mul(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *out_num,
                    int64_t *out_den)
{
    if (!out_num || !out_den) return -1;
    put(rat_mul(rat_make(an, ad), rat_make(bn, bd)), out_num, out_den);
    return 0;
}

int32_t zxv_rat_div(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *out_num,
                    int64_t *out_den)
{
    if (!out_num || !out_den) return -1;
    put(rat_div(rat_make(an, ad), rat_make(bn, bd)), out_num, out_den);
    return 0;
}

int32_t zxv_rat_cmp(int64_t an, int64_t ad, int64_t bn, int64_t bd)
{
    return rat_cmp(rat_make(an, ad), rat_make(bn, bd));
}

int32_t zxv_rat_normalize(int64_t *num, int64_t *den)
{
    if (!num || !den) return -1;
    put(rat_make(*num, *den), num, den);
    return 0;
}

int32_t zxv_rat_parse(const char *text, int64_t *out_num, int64_t *out_den)
{
    if (!text || !out_num || !out_den) return -1;
    rat_t r = rat_from_string(text);
    *out_num = r.num;
    *out_den = r.den;
    return 0;
}

int32_t zxv_rat_format(int64_t num, int64_t den, char *buf, size_t cap, size_t *out_len)
{
    if (!buf || !cap) return -1;
    uint32_t n = rat_to_string(rat_make(num, den), buf, (uint32_t) cap);
    if (out_len) *out_len = n;
    return 0;
}

int32_t zxv_sha3_256(const uint8_t *data, size_t len, uint8_t out[32])
{
    if (!out) return -1;
    sha3_256(data, len, out);
    return 0;
}
