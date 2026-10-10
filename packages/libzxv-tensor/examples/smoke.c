/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* smoke.c - links against libzxv-tensor.a and zxv_tensor.h only.
 *
 *   cc -Ibuild/libzxv-tensor/include smoke.c build/libzxv-tensor/libzxv-tensor.a
 *
 * Builds a two-tensor GGUF file in memory (one Q8_0 row, one Q5_0 row, with
 * hand-chosen values), opens it, decodes both to Q16 and checks the values,
 * then quantises them to zt_q8_t blocks and checks the integer dot product. */
#include <stdio.h>
#include <string.h>
#include "zxv_tensor.h"

static int fails, checks;
#define CHECK(c, what)                                                                             \
    do {                                                                                           \
        checks++;                                                                                  \
        printf("%s %s\n", (c) ? "ok  " : "FAIL", what);                                            \
        if (!(c)) fails++;                                                                         \
    } while (0)

static uint8_t file[1024];
static size_t fn;
static void put(const void *p, size_t n)
{
    memcpy(file + fn, p, n);
    fn += n;
}
static void p32(uint32_t v)
{
    for (int i = 0; i < 4; i++) file[fn++] = (uint8_t) (v >> (8 * i));
}
static void p64(uint64_t v)
{
    p32((uint32_t) v);
    p32((uint32_t) (v >> 32));
}
static void pstr(const char *s)
{
    p64(strlen(s));
    put(s, strlen(s));
}

int main(void)
{
    /* Q8_0 block: d = 0.5 (f16 0x3800), q[i] = i - 16  -> value (i - 16) / 2 */
    uint8_t q8[34] = {0x00, 0x38};
    for (int i = 0; i < 32; i++) q8[2 + i] = (uint8_t) (int8_t) (i - 16);
    /* Q5_0 block: d = 1.0 (f16 0x3C00), qh bit j set for even j; low nibble
     * of byte j = j (element j), high nibble = 15 - j (element j + 16) */
    uint8_t q5[22] = {0x00, 0x3C};
    uint32_t qh = 0x55555555u;
    for (int i = 0; i < 4; i++) q5[2 + i] = (uint8_t) (qh >> (8 * i));
    for (int j = 0; j < 16; j++) q5[6 + j] = (uint8_t) (j | ((15 - j) << 4));

    put("GGUF", 4);
    p32(3);
    p64(2);
    p64(1);
    pstr("general.architecture");
    p32(ZT_GGUF_STRING);
    pstr("smoke");
    pstr("a.q8_0");
    p32(1);
    p64(32);
    p32(ZT_GGML_Q8_0);
    p64(0);
    pstr("b.q5_0");
    p32(1);
    p64(32);
    p32(ZT_GGML_Q5_0);
    p64(64);
    while (fn % 32) file[fn++] = 0;
    size_t data = fn;
    put(q8, sizeof q8);
    while (fn < data + 64) file[fn++] = 0;
    put(q5, sizeof q5);

    zt_gguf_t g;
    CHECK(zt_gguf_open(&g, file, fn) == ZT_GGUF_OK, "GGUF opens");
    zt_gguf_tensor_t a, b;
    CHECK(zt_gguf_find_tensor(&g, "a.q8_0", &a) == ZT_GGUF_OK && a.n_bytes == 34, "Q8_0 tensor");
    CHECK(zt_gguf_find_tensor(&g, "b.q5_0", &b) == ZT_GGUF_OK && b.n_bytes == 22, "Q5_0 tensor");

    zt_fx va[32], vb[32];
    int ok = zt_gguf_dequant(&a, 0, 32, va) == ZT_GGUF_OK;
    for (int i = 0; i < 32; i++) ok &= va[i] == (i - 16) * ZT_ONE / 2;
    CHECK(ok, "Q8_0 decodes to (i - 16) / 2");
    ok = zt_gguf_dequant(&b, 0, 32, vb) == ZT_GGUF_OK;
    int64_t want_dot = 0;
    for (int j = 0; j < 16; j++) {
        int hi0 = (qh >> j) & 1, hi1 = (qh >> (j + 16)) & 1;
        ok &= vb[j] == (j + 16 * hi0 - 16) * ZT_ONE;
        ok &= vb[j + 16] == ((15 - j) + 16 * hi1 - 16) * ZT_ONE;
    }
    CHECK(ok, "Q5_0 decodes with ggml's bit layout");
    for (int i = 0; i < 32; i++) want_dot += (int64_t) va[i] * vb[i];
    want_dot >>= 16;

    zt_q8_t qa, qb;
    zt_quantize(va, 32, &qa, false);
    zt_quantize(vb, 32, &qb, false);
    zt_fx dot = zt_dot(&qa, &qb, 1);
    int64_t err = dot - want_dot;
    if (err < 0) err = -err;
    /* 8-bit requantisation of both vectors: a few percent of the magnitude at most */
    CHECK(err <= (want_dot < 0 ? -want_dot : want_dot) / 20 + ZT_ONE, "zt_dot close to exact");
    printf("dot %d (exact %lld)\n", (int) dot, (long long) want_dot);

    printf("%d/%d checks\n", checks - fails, checks);
    return fails ? 1 : 0;
}
