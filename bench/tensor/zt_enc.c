/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_enc.c -- hosted driver: run an encoder cell (zt_bert) over a batch of
 * texts, for bench/tensor/compare/ref_bert.py.
 *
 * CASES is binary, so any text goes through unchanged: per case a
 * little-endian u32 length and the bytes of text A, then a u32 that is 1 when
 * a text B follows (its u32 length and bytes) and 0 when not. For each case
 * it prints one line:
 *   ids <id> ...                          (with --tok: nothing else)
 *   ids ... | logits <Q16> ...            (a classifier)
 *   ids ... | embd <Q16> ...              (a sentence embedder)
 * then zt.kern, zt.threads, zt.n_sat (values that hit the Q16 limit) and
 * zt.seconds.
 *
 * Usage: zt_enc MODEL.gguf CASES [--tok] [--special] [--max N] [--kern NAME]
 *               [--threads T]
 * --max: tokens per case (default the file's zxv.wordpiece.max_tokens);
 * --special: special-token text in the input becomes the token.
 * Exit: 0 ok, 2 usage, 3 cannot read a file, 4 not loadable, 5 run failed. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zt_bert.h"
#include "zt_gguf.h"
#include "zt_simd.h"

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

static uint8_t *read_file(const char *path, uint64_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = malloc((size_t) n + 1);
    if (buf && fread(buf, 1, (size_t) n, f) != (size_t) n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = (uint64_t) n;
    return buf;
}

static void *alloc64(uint64_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes ? (size_t) bytes : 64) != 0) return NULL;
    return p;
}

static bool rd32(const uint8_t *buf, uint64_t size, uint64_t *off, uint32_t *v)
{
    if (size - *off < 4) return false;
    *v = (uint32_t) buf[*off] | (uint32_t) buf[*off + 1] << 8 | (uint32_t) buf[*off + 2] << 16 |
         (uint32_t) buf[*off + 3] << 24;
    *off += 4;
    return true;
}

int main(int argc, char **argv)
{
    const char *kern_name = NULL;
    uint32_t n_threads = 1, max_tok = 0;
    bool tok_only = false, special = false;
    if (argc < 3) goto usage;
    for (int i = 3; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--tok"))
            tok_only = true;
        else if (!strcmp(a, "--special"))
            special = true;
        else if (!strcmp(a, "--max") && v)
            max_tok = (uint32_t) strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--kern") && v)
            kern_name = argv[++i];
        else if (!strcmp(a, "--threads") && v)
            n_threads = (uint32_t) strtoul(argv[++i], NULL, 10);
        else
            goto usage;
    }
    uint64_t msize, csize;
    uint8_t *mbuf = read_file(argv[1], &msize), *cbuf = read_file(argv[2], &csize);
    if (!mbuf || !cbuf) {
        fprintf(stderr, "zt_enc: cannot read %s\n", mbuf ? argv[2] : argv[1]);
        return 3;
    }
    zt_gguf_t g;
    zt_model_err_t err = {0};
    zt_bert_t b;
    if (zt_gguf_open(&g, mbuf, msize) != ZT_GGUF_OK) {
        fprintf(stderr, "zt_enc: not a GGUF file\n");
        return 4;
    }
    uint64_t abytes = zt_bert_arena_bytes(&g);
    void *arena = abytes ? alloc64(abytes) : NULL;
    if (!arena || zt_bert_load(&b, &g, arena, abytes, &err)) {
        zt_bert_config(&g, &b.cfg, &err);
        fprintf(stderr, "zt_enc: %s %s\n", err.what ? err.what : "not loadable", err.name);
        return 4;
    }
    const zt_kern_t *kb = kern_name ? zt_simd_find(kern_name) : zt_simd_best();
    if (!kb) {
        fprintf(stderr, "zt_enc: kernel set '%s' is not available on this CPU\n", kern_name);
        return 2;
    }
    zt_pool_t *pool = n_threads > 1 ? zt_pool_new(n_threads) : NULL;
    zt_kern_t kern;
    zt_kern_threaded(&kern, kb, pool);
    b.kern = &kern;
    if (!max_tok) max_tok = b.cfg.max_tokens;
    if (max_tok < 3 || max_tok > b.cfg.n_ctx) {
        fprintf(stderr, "zt_enc: --max must be 3..%u\n", b.cfg.n_ctx);
        return 2;
    }
    uint64_t wbytes = zt_bert_work_bytes(&b, max_tok);
    void *work = alloc64(wbytes);
    int32_t *ids = malloc((size_t) max_tok * 4);
    uint8_t *types = malloc(max_tok);
    zt_fx *out = malloc((size_t) (b.cfg.n_labels > b.cfg.n_embd ? b.cfg.n_labels : b.cfg.n_embd) *
                        4);
    if (!work || !ids || !types || !out) return 5;
    uint64_t off = 0, sat = 0;
    double t0 = now_s();
    while (off < csize) {
        uint32_t la, has_b, lb = 0;
        if (!rd32(cbuf, csize, &off, &la) || csize - off < la) goto bad;
        const uint8_t *ta = cbuf + off, *tb = NULL;
        off += la;
        if (!rd32(cbuf, csize, &off, &has_b) || has_b > 1) goto bad;
        if (has_b) {
            if (!rd32(cbuf, csize, &off, &lb) || csize - off < lb) goto bad;
            tb = cbuf + off;
            off += lb;
        }
        uint64_t tw = zt_bert_tok_work_bytes(la, lb);
        void *twork = alloc64(tw);
        uint32_t n = 0;
        int32_t r = twork ? zt_bert_tokenize(&b, ta, la, has_b ? (tb ? tb : (const uint8_t *) "")
                                                              : NULL,
                                             lb, special, max_tok, ids, types, &n, twork, tw)
                          : ZT_NN_ESPACE;
        free(twork);
        if (r) {
            fprintf(stderr, "zt_enc: tokenize failed (%d)\n", r);
            return 5;
        }
        printf("ids");
        for (uint32_t i = 0; i < n; i++) printf(" %d", ids[i]);
        if (!tok_only) {
            uint32_t ns = 0, k = b.cfg.n_labels ? b.cfg.n_labels : b.cfg.n_embd;
            r = b.cfg.n_labels ? zt_bert_classify(&b, ids, types, n, out, work, wbytes, &ns)
                               : zt_bert_embed(&b, ids, types, n, out, work, wbytes, &ns);
            if (r) {
                fprintf(stderr, "zt_enc: forward pass failed (%d)\n", r);
                return 5;
            }
            sat += ns;
            printf(" | %s", b.cfg.n_labels ? "logits" : "embd");
            for (uint32_t i = 0; i < k; i++) printf(" %d", out[i]);
        }
        printf("\n");
    }
    printf("zt.kern=%s\nzt.threads=%u\nzt.n_sat=%llu\nzt.seconds=%.3f\n", kb->name,
           pool ? n_threads : 1, (unsigned long long) sat, now_s() - t0);
    zt_pool_free(pool);
    return 0;
bad:
    fprintf(stderr, "zt_enc: CASES is malformed at byte %llu\n", (unsigned long long) off);
    return 3;
usage:
    fprintf(stderr, "usage: zt_enc MODEL.gguf CASES [--tok] [--special] [--max N] [--kern NAME] "
                    "[--threads T]\n");
    return 2;
}
