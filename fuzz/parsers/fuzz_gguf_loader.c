/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_gguf_loader.c — the GGUF model-file reader (tensor/zt_gguf.c).
 *
 * The whole input is a candidate GGUF file. After zt_gguf_open succeeds the
 * harness walks every metadata pair (bounded), every array element (bounded)
 * and every tensor info, and dequantises the first and the last block of
 * each tensor. Properties: a tensor's data range lies inside the file, and
 * strings returned by the reader point inside the file. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zt_gguf.h"
#include "fuzz_in.h"

static const uint8_t *g_lo, *g_hi;

static void in_file(const uint8_t *p, uint64_t n)
{
    if (n == 0) return;
    if (!p || p < g_lo || p > g_hi || n > (uint64_t) (g_hi - p)) abort();
}

static bool str_cb(void *ctx, uint64_t i, zt_gguf_str_t s)
{
    (void) i;
    in_file(s.p, s.len);
    return ++*(uint32_t *) ctx < 64u;
}

static zt_fx g_out[256], g_scratch[256];
static zt_q8_t g_q8[8];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t *buf = fz_dup(data, size);
    g_lo = buf;
    g_hi = buf + size;
    zt_gguf_t g;
    if (zt_gguf_open(&g, buf, size) == ZT_GGUF_OK) {
        uint64_t nkv = g.n_kv < 256u ? g.n_kv : 256u;
        for (uint64_t i = 0; i < nkv; i++) {
            zt_gguf_str_t key;
            zt_gguf_val_t v;
            if (zt_gguf_kv(&g, i, &key, &v) != ZT_GGUF_OK) break;
            in_file(key.p, key.len);
            if (v.type == ZT_GGUF_STRING) in_file(v.str.p, v.str.len);
            if (v.type == ZT_GGUF_ARRAY) {
                uint64_t n = v.count < 64u ? v.count : 64u;
                for (uint64_t j = 0; j < n; j++) {
                    int64_t iv;
                    zt_fx q;
                    zt_gguf_str_t s;
                    (void) zt_gguf_arr_int(&g, &v, j, &iv);
                    (void) zt_gguf_arr_q16(&g, &v, j, &q);
                    if (zt_gguf_arr_str(&g, &v, j, &s) == ZT_GGUF_OK) in_file(s.p, s.len);
                }
                uint32_t cnt = 0;
                (void) zt_gguf_arr_strings(&g, &v, str_cb, &cnt);
            }
        }
        (void) zt_gguf_get_int(&g, "general.alignment", 0);
        zt_gguf_val_t fv;
        (void) zt_gguf_find(&g, "tokenizer.ggml.tokens", &fv);

        uint64_t nt = g.n_tensors < 64u ? g.n_tensors : 64u;
        for (uint64_t i = 0; i < nt; i++) {
            zt_gguf_tensor_t t;
            if (zt_gguf_tensor(&g, i, &t) != ZT_GGUF_OK) break;
            in_file(t.name.p, t.name.len);
            in_file(t.data, t.n_bytes);
            char name[64];
            uint64_t nl = t.name.len < sizeof name - 1 ? t.name.len : sizeof name - 1;
            memcpy(name, t.name.p, (size_t) nl);
            name[nl] = 0;
            zt_gguf_tensor_t t2;
            (void) zt_gguf_find_tensor(&g, name, &t2);
            uint32_t blk = zt_ggml_block(t.type);
            if (blk == 0 || blk > 256u || t.n_elems < blk) continue;
            (void) zt_gguf_dequant(&t, 0, blk, g_out);
            uint64_t last = (t.n_elems / blk - 1u) * blk;
            (void) zt_gguf_dequant(&t, last, blk, g_out);
            if (t.n_elems >= 256u && (blk <= 32u || blk == 256u)) {
                uint64_t n = blk < 32u ? 32u : blk;
                (void) zt_gguf_to_q8(&t, 0, n, g_q8, g_scratch);
            }
        }
    }
    free(buf);
    return 0;
}
