/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_q5.c - GGUF Q5_0 / Q5_1 decoding against golden values.
 *
 * The fixture (test_q5_fixture.h, from gen_q5_fixture.py) holds layout
 * probes, edge-case and random blocks with, per element, the Q16 rounding
 * of the exact value and of ggml's f32 reference value. Checked here:
 *   block sizes; zt_gguf_dequant bit-exact against EXACT and within one
 *   Q16 step of GGML; a tiny GGUF file built in memory with a Q5_0 and a
 *   Q5_1 tensor that zt_gguf_open accepts, sized and decoded by row;
 *   zt_gguf_to_q8 agreeing with the dequantised values; truncation refused.
 *
 *   gcc -std=c11 -Wall -Wextra -Werror -Isrc/tensor src/tensor/test_zt_q5.c \
 *       src/tensor/zt_gguf.c src/tensor/zt.c -o /tmp/test_zt_q5 && /tmp/test_zt_q5
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt_gguf.h"
#include "test_q5_fixture.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define ROW 256u /* elements per row in the in-memory GGUF: 8 blocks */

static uint8_t file[4096];
static size_t fn;

static void put(const void *p, size_t n)
{
    memcpy(file + fn, p, n);
    fn += n;
}
static void p32(uint32_t v)
{
    uint8_t b[4] = {(uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24)};
    put(b, 4);
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

/* GGUF v3: one string key, two 2-D tensors (ROW x rows), data aligned to 32. */
static size_t build_gguf(void)
{
    uint64_t r0 = Q5_0_NBLK * 32 / ROW, r1 = Q5_1_NBLK * 32 / ROW;
    uint64_t off1 = (sizeof Q5_0_BLOCKS + 31u) & ~(uint64_t) 31u;
    fn = 0;
    put("GGUF", 4);
    p32(3);
    p64(2); /* tensors */
    p64(1); /* kv */
    pstr("general.architecture");
    p32(ZT_GGUF_STRING);
    pstr("test");
    pstr("w.q5_0");
    p32(2);
    p64(ROW);
    p64(r0);
    p32(ZT_GGML_Q5_0);
    p64(0);
    pstr("w.q5_1");
    p32(2);
    p64(ROW);
    p64(r1);
    p32(ZT_GGML_Q5_1);
    p64(off1);
    while (fn % 32) file[fn++] = 0;
    size_t data = fn;
    put(Q5_0_BLOCKS, sizeof Q5_0_BLOCKS);
    while (fn < data + off1) file[fn++] = 0;
    put(Q5_1_BLOCKS, sizeof Q5_1_BLOCKS);
    return fn;
}

static void check_type(const char *name, uint32_t type, const uint8_t *blocks, uint32_t nblk,
                       const int32_t *exact, const int32_t *ggml)
{
    uint32_t n = nblk * 32;
    static zt_fx out[32 * 64];
    zt_gguf_tensor_t t = {0};
    t.type = type;
    t.n_dims = 1;
    t.dims[0] = t.n_elems = n;
    t.n_bytes = zt_ggml_bytes(type, n);
    t.data = blocks;
    CHECK(zt_gguf_dequant(&t, 0, n, out) == ZT_GGUF_OK, "%s decodes", name);
    int bad = 0, off1 = 0, off_ggml = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (out[i] != exact[i]) {
            if (bad < 4)
                printf("  %s[%u] = %d, expected %d\n", name, i, (int) out[i], (int) exact[i]);
            bad++;
        }
        /* ggml's f32 add rounds once: at most half an f32 ulp of the value
         * (in Q16 units, 2^(log2|v| - 24)), plus the Q16 rounding step */
        int64_t d = (int64_t) out[i] - ggml[i], a = exact[i] < 0 ? -(int64_t) exact[i] : exact[i];
        int64_t tol = 1;
        while (a >= (int64_t) 1 << 25) a >>= 1, tol <<= 1;
        tol += 1;
        if (d != 0) off_ggml++;
        if (d > tol || d < -tol) off1++;
    }
    CHECK(bad == 0, "%s: %d of %u values differ from the exact golden", name, bad, n);
    CHECK(off1 == 0, "%s: %d values further from ggml's f32 than one f32 rounding", name, off1);
    printf("  %s: %u values checked; %d differ from ggml's f32 result (its f32 add rounds)\n", name,
           n, off_ggml);
    /* every block on its own gives the same values (block-aligned offsets) */
    for (uint32_t b = 0; b < nblk; b++) {
        zt_fx one[32];
        CHECK(zt_gguf_dequant(&t, (uint64_t) b * 32, 32, one) == ZT_GGUF_OK &&
                  !memcmp(one, out + b * 32, sizeof one),
              "%s block %u alone", name, b);
    }
    CHECK(zt_gguf_dequant(&t, 16, 32, out) == ZT_GGUF_ERANGE, "%s: unaligned first", name);
    CHECK(zt_gguf_dequant(&t, 0, n + 32, out) == ZT_GGUF_ERANGE, "%s: past the end", name);
}

int main(void)
{
    CHECK(zt_ggml_block(ZT_GGML_Q5_0) == 32 && zt_ggml_block(ZT_GGML_Q5_1) == 32, "block 32");
    CHECK(zt_ggml_bytes(ZT_GGML_Q5_0, 64) == 44 && zt_ggml_bytes(ZT_GGML_Q5_1, 64) == 48,
          "22 and 24 bytes a block");
    CHECK(zt_ggml_bytes(ZT_GGML_Q5_0, 48) == 0 && zt_ggml_bytes(ZT_GGML_Q5_1, 1) == 0,
          "partial blocks refused");
    CHECK(sizeof Q5_0_BLOCKS == Q5_0_NBLK * 22u && sizeof Q5_1_BLOCKS == Q5_1_NBLK * 24u,
          "fixture sizes");

    check_type("Q5_0", ZT_GGML_Q5_0, Q5_0_BLOCKS, Q5_0_NBLK, Q5_0_EXACT, Q5_0_GGML);
    check_type("Q5_1", ZT_GGML_Q5_1, Q5_1_BLOCKS, Q5_1_NBLK, Q5_1_EXACT, Q5_1_GGML);

    /* layout probes (blocks 0-2 of each type; see gen_q5_fixture.py) */
    CHECK(Q5_0_EXACT[0] == 0 && Q5_0_EXACT[1] == -16 * 65536 && Q5_0_EXACT[16] == -16 * 65536,
          "Q5_0 qh bit 0 is element 0's high bit");
    CHECK(Q5_0_EXACT[32 + 16] == 0 && Q5_0_EXACT[32 + 0] == -16 * 65536,
          "Q5_0 qh bit 16 is element 16's high bit");
    CHECK(Q5_0_EXACT[64 + 15] == 15 * 65536 && Q5_0_EXACT[64 + 31] == 15 * 65536 &&
              Q5_0_EXACT[64 + 14] == -65536,
          "Q5_0 qh bits 15 and 31; high nibble is element j + 16");

    /* a tiny GGUF file holding one tensor of each type */
    size_t size = build_gguf();
    zt_gguf_t g;
    CHECK(zt_gguf_open(&g, file, size) == ZT_GGUF_OK, "GGUF with Q5_0 and Q5_1 tensors opens");
    const char *names[2] = {"w.q5_0", "w.q5_1"};
    const uint32_t types[2] = {ZT_GGML_Q5_0, ZT_GGML_Q5_1};
    const int32_t *refs[2] = {Q5_0_EXACT, Q5_1_EXACT};
    const uint64_t bytes[2] = {sizeof Q5_0_BLOCKS, sizeof Q5_1_BLOCKS};
    for (int k = 0; k < 2; k++) {
        zt_gguf_tensor_t t;
        CHECK(zt_gguf_find_tensor(&g, names[k], &t) == ZT_GGUF_OK, "%s found", names[k]);
        CHECK(t.type == types[k] && t.n_bytes == bytes[k] && t.dims[0] == ROW &&
                  t.n_elems == bytes[k] / (k ? 24 : 22) * 32,
              "%s type and size", names[k]);
        static zt_fx row[ROW], back[ROW], scratch[ROW];
        static zt_q8_t q8[ROW / 32];
        int bad = 0, far = 0;
        for (uint64_t r = 0; r < t.dims[1]; r++) {
            CHECK(zt_gguf_dequant(&t, r * ROW, ROW, row) == ZT_GGUF_OK, "%s row", names[k]);
            bad += memcmp(row, refs[k] + r * ROW, sizeof row) != 0;
            /* to zt_q8_t: re-quantised, so within half a step of each block's scale */
            CHECK(zt_gguf_to_q8(&t, r * ROW, ROW, q8, scratch) == ZT_GGUF_OK, "%s to q8", names[k]);
            zt_dequantize(q8, ROW / 32, back);
            for (uint32_t b = 0; b < ROW / 32; b++) {
                int64_t amax = 0;
                for (int i = 0; i < 32; i++) {
                    int64_t a = row[b * 32 + i] < 0 ? -(int64_t) row[b * 32 + i] : row[b * 32 + i];
                    if (a > amax) amax = a;
                }
                int64_t tol = amax / 127 + 2;
                for (int i = 0; i < 32; i++) {
                    int64_t d = (int64_t) back[b * 32 + i] - row[b * 32 + i];
                    if (d > tol || d < -tol) far++;
                }
            }
        }
        CHECK(bad == 0, "%s rows from the file match the golden values", names[k]);
        CHECK(far == 0, "%s to zt_q8_t: %d values off by more than one 8-bit step", names[k], far);
    }
    zt_gguf_t g2;
    CHECK(zt_gguf_open(&g2, file, size - 1) != ZT_GGUF_OK, "truncated Q5_1 data refused");

    printf("test_zt_q5: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
