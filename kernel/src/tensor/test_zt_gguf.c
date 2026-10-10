/* test_zt_gguf.c — the GGUF reader against llama.cpp's own writer.
 *
 * The fixture is a GGUF file written by gguf-py (llama.cpp's reference
 * Python package), with reference dequantised values from gguf.quants, so
 * every comparison here is against an independent implementation:
 *   metadata of every scalar type, string arrays, int and float arrays;
 *   tensor lookup; F32/F16/BF16/Q8_0/Q4_0/Q4_K/Q6_K decoding to Q16;
 *   Q8_0 to zt_q8_t losslessly; and refusal of truncated or corrupt files.
 *
 *   gcc -std=c11 -Wall -Wextra -Werror -O2 -Isrc/tensor src/tensor/test_zt_gguf.c \
 *       src/tensor/zt_gguf.c src/tensor/zt.c -o /tmp/test_zt_gguf && /tmp/test_zt_gguf
 * Regenerate the fixture with src/tensor/gen_gguf_fixture.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt_gguf.h"
#include "test_gguf_fixture.h"

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

static bool collect(void *ctx, uint64_t i, zt_gguf_str_t s)
{
    char(*names)[16] = ctx;
    uint64_t n = s.len < 15 ? s.len : 15;
    memcpy(names[i], s.p, n);
    names[i][n] = 0;
    return true;
}

int main(void)
{
    zt_gguf_t g;
    CHECK(zt_gguf_open(&g, GGUF_FIXTURE, sizeof(GGUF_FIXTURE)) == ZT_GGUF_OK, "open");
    CHECK(g.version == 3, "version %u", g.version);
    CHECK(g.n_tensors == sizeof(GGUF_REFS) / sizeof(GGUF_REFS[0]), "tensor count");

    /* metadata */
    zt_gguf_val_t v;
    CHECK(zt_gguf_find(&g, "general.architecture", &v) == 0 && zt_gguf_str_eq(v.str, "qwen3"),
          "arch");
    CHECK(zt_gguf_get_int(&g, "test.u32", 0) == 4000000000ll, "u32");
    CHECK(zt_gguf_get_int(&g, "test.i32", 0) == -123456, "i32");
    CHECK(zt_gguf_get_int(&g, "qwen3.context_length", 0) == 40960, "ctx len");
    CHECK(zt_gguf_get_int(&g, "missing.key", 7) == 7, "default");
    CHECK(zt_gguf_find(&g, "test.f32", &v) == 0 && v.q16 == -163840, "f32 -2.5 -> %d", v.q16);
    CHECK(zt_gguf_find(&g, "test.bool", &v) == 0 && v.u == 1, "bool");
    CHECK(zt_gguf_find(&g, "test.str", &v) == 0 && zt_gguf_str_eq(v.str, "ZXV tensor engine"),
          "string");

    zt_gguf_val_t toks;
    CHECK(zt_gguf_find(&g, "tokenizer.ggml.tokens", &toks) == 0 && toks.type == ZT_GGUF_ARRAY &&
              toks.elem_type == ZT_GGUF_STRING && toks.count == 6,
          "token array");
    char names[6][16];
    CHECK(zt_gguf_arr_strings(&g, &toks, collect, names) == 0, "walk strings");
    CHECK(!strcmp(names[0], "<|endoftext|>") && !strcmp(names[3], "ab"), "token text");
    zt_gguf_str_t s5;
    CHECK(zt_gguf_arr_str(&g, &toks, 5, &s5) == 0 && s5.len == 3 && s5.p[0] == 0xE4, "utf-8 token");
    CHECK(zt_gguf_arr_str(&g, &toks, 6, &s5) == ZT_GGUF_ERANGE, "string index range");

    zt_gguf_val_t ints, fl;
    int64_t iv = 0;
    zt_fx fv = 0;
    CHECK(zt_gguf_find(&g, "test.ints", &ints) == 0 && zt_gguf_arr_int(&g, &ints, 1, &iv) == 0 &&
              iv == -2,
          "int array");
    CHECK(zt_gguf_find(&g, "test.floats", &fl) == 0 && zt_gguf_arr_q16(&g, &fl, 1, &fv) == 0 &&
              fv == -81920,
          "float array");
    CHECK(zt_gguf_arr_q16(&g, &ints, 0, &fv) == ZT_GGUF_ETYPE, "type checked");

    /* tensors against the reference dequantiser */
    static zt_fx out[512];
    for (uint32_t k = 0; k < sizeof(GGUF_REFS) / sizeof(GGUF_REFS[0]); k++) {
        const gguf_ref_t *r = &GGUF_REFS[k];
        zt_gguf_tensor_t t;
        CHECK(zt_gguf_find_tensor(&g, r->name, &t) == 0, "find %s", r->name);
        CHECK(t.type == r->type && t.n_elems == r->n && t.dims[0] == 256 && t.dims[1] == 2,
              "shape %s", r->name);
        CHECK(zt_gguf_dequant(&t, 0, t.n_elems, out) == 0, "dequant %s", r->name);
        int32_t worst = 0;
        for (uint32_t i = 0; i < r->n; i++) {
            int32_t d = abs(out[i] - r->ref[i]);
            if (d > worst) worst = d;
        }
        /* the reference rounds in float32; this is exact, then rounds once */
        CHECK(worst <= 1, "%s differs from gguf-py by %d Q16 units", r->name, worst);
        printf("  %-7s type %2u: max |zxv - gguf-py| = %d / 65536\n", r->name + 2, r->type, worst);

        /* to zt_q8_t: Q8_0 exactly, others through requantisation */
        static zt_q8_t q[16];
        static zt_fx back[512], scratch[256];
        CHECK(zt_gguf_to_q8(&t, 0, t.n_elems, q, scratch) == 0, "to q8 %s", r->name);
        zt_dequantize(q, (uint32_t) t.n_elems / 32, back);
        int32_t qw = 0;
        for (uint32_t i = 0; i < r->n; i++) {
            int32_t d = abs(back[i] - out[i]);
            if (d > qw) qw = d;
        }
        if (r->type == ZT_GGML_Q8_0) CHECK(qw <= 1, "Q8_0 to zt_q8_t is exact (%d)", qw);
    }

    /* partial rows: second row only */
    zt_gguf_tensor_t t;
    CHECK(zt_gguf_find_tensor(&g, "t.q4_k", &t) == 0 && zt_gguf_dequant(&t, 256, 256, out) == 0,
          "row 2");
    CHECK(abs(out[3] - GGUF_REFS[5].ref[259]) <= 1, "row offset");
    CHECK(zt_gguf_dequant(&t, 100, 256, out) == ZT_GGUF_ERANGE, "unaligned start refused");
    CHECK(zt_gguf_dequant(&t, 256, 512, out) == ZT_GGUF_ERANGE, "past the end refused");
    CHECK(zt_gguf_find_tensor(&g, "t.missing", &t) == ZT_GGUF_ENOTFOUND, "missing tensor");

    /* a dot product straight from Q8_0 weights matches the Q16 reference */
    {
        zt_gguf_tensor_t w;
        static zt_q8_t wq[8], xq[8];
        static zt_fx xs[256], wf[256];
        CHECK(zt_gguf_find_tensor(&g, "t.q8_0", &w) == 0, "q8");
        zt_gguf_to_q8(&w, 0, 256, wq, 0);
        zt_gguf_dequant(&w, 0, 256, wf);
        for (int i = 0; i < 256; i++) xs[i] = (zt_fx) ((i * 7919) % 131072 - 65536);
        zt_quantize(xs, 256, xq, false);
        zt_dequantize(xq, 8, xs);
        int64_t ref = 0;
        for (int i = 0; i < 256; i++) ref += (int64_t) wf[i] * xs[i];
        ref /= 65536;
        zt_fx got = zt_dot(wq, xq, 8);
        CHECK(llabs(got - ref) <= 256, "dot from file weights %d vs %lld", got, (long long) ref);
    }

    /* corrupt input is refused, never read past the end */
    static uint8_t bad[sizeof(GGUF_FIXTURE)];
    /* only the writer's trailing alignment padding may be cut away */
    uint64_t data_end = 0;
    for (uint64_t i = 0; i < g.n_tensors; i++) {
        zt_gguf_tensor_t ti;
        zt_gguf_tensor(&g, i, &ti);
        uint64_t e = (uint64_t) (ti.data - GGUF_FIXTURE) + ti.n_bytes;
        if (e > data_end) data_end = e;
    }
    int refused = 0, tried = 0;
    for (uint64_t cut = 0; cut < data_end; cut += 7) {
        memcpy(bad, GGUF_FIXTURE, cut);
        zt_gguf_t b;
        tried++;
        if (zt_gguf_open(&b, bad, cut) != ZT_GGUF_OK) refused++;
    }
    CHECK(refused == tried, "every truncation refused (%d of %d)", refused, tried);
    memcpy(bad, GGUF_FIXTURE, sizeof(bad));
    bad[0] = 'X';
    CHECK(zt_gguf_open(&g, bad, sizeof(bad)) == ZT_GGUF_EMAGIC, "bad magic");
    memcpy(bad, GGUF_FIXTURE, sizeof(bad));
    bad[4] = 9;
    CHECK(zt_gguf_open(&g, bad, sizeof(bad)) == ZT_GGUF_EVERSION, "bad version");
    /* random byte flips: open may succeed or fail, but must stay in bounds (ASan) */
    srand(7);
    for (int it = 0; it < 3000; it++) {
        memcpy(bad, GGUF_FIXTURE, sizeof(bad));
        for (int f = 0; f < 4; f++) bad[rand() % 400] ^= (uint8_t) (1 + rand() % 255);
        zt_gguf_t b;
        if (zt_gguf_open(&b, bad, sizeof(bad)) == ZT_GGUF_OK)
            for (uint64_t i = 0; i < b.n_tensors; i++) {
                zt_gguf_tensor_t bt;
                if (zt_gguf_tensor(&b, i, &bt) == ZT_GGUF_OK)
                    zt_gguf_dequant(&bt, 0, bt.n_elems < 512 ? bt.n_elems : 0, out);
            }
    }

    /* float decoding corner cases */
    CHECK(zt_f32_to_q16(0x3F800000u) == 65536, "1.0f");
    CHECK(zt_f32_to_q16(0xBF000000u) == -32768, "-0.5f");
    CHECK(zt_f32_to_q16(0x7F800000u) == 0x7FFFFFFF, "inf saturates");
    CHECK(zt_f32_to_q16(0x7FC00000u) == 0, "nan is 0");
    CHECK(zt_f16_to_q16(0x3C00) == 65536 && zt_f16_to_q16(0x0001) == 0 &&
              zt_f16_to_q16(0xC500) == -327680,
          "f16");
    CHECK(zt_bf16_to_q16(0x4049) == 205824, "bf16 3.140625 -> %d", zt_bf16_to_q16(0x4049));

    printf("test_zt_gguf: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
