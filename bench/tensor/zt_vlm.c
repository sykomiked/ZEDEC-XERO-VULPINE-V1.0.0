/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_vlm.c -- hosted driver: ask the integer engine a question about an image.
 *
 * Loads a SmolVLM language model GGUF (zt_model, zt_tok) and its vision
 * projector GGUF (zt_vision), reads a binary PPM (P6) image, scales it to the
 * encoder's size (zt_vision_resize), encodes it, and answers the question
 * greedily in SmolVLM's chat format:
 *   <|im_start|>User:<fake_token_around_image><global-img>[image vectors]
 *   <fake_token_around_image>QUESTION<end_of_utterance>\nAssistant:
 * It prints the answer and:
 *   zt.resize_s, zt.encode_s, zt.prefill_s, zt.gen_s, zt.gen_tps
 *   zt.embd_fnv=<FNV-1a 64 of the image vectors>  zt.logits_fnv=<... of every logit vector>
 *   zt.n_sat_vision, zt.n_sat
 * so two machines can check they computed the same bits.
 * --dump-embd FILE writes the image vectors (int32 Q16, little-endian) and
 * --dump-rgb FILE the resized pixels, for bench/tensor/compare/ref_vision.py.
 *
 * Usage: zt_vlm MODEL.gguf MMPROJ.gguf IMAGE.ppm [--prompt TEXT] [-n N]
 *               [--kern NAME] [--threads T] [--dump-embd FILE] [--dump-rgb FILE]
 * Convert other formats first, e.g. `python3 -c "from PIL import Image;
 * Image.open('x.jpg').convert('RGB').save('x.ppm')"`.
 * Exit: 0 ok, 2 usage, 3 cannot read a file, 4 not loadable, 5 run failed. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zt_gguf.h"
#include "zt_model.h"
#include "zt_simd.h"
#include "zt_tok.h"
#include "zt_vision.h"

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
    if (n <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = malloc((size_t) n);
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

static uint64_t fnv(uint64_t h, const zt_fx *v, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) {
        uint32_t x = (uint32_t) v[i];
        for (int b = 0; b < 4; b++) h = (h ^ (uint8_t) (x >> (8 * b))) * 0x100000001b3ull;
    }
    return h;
}

/* binary PPM: "P6", width, height, maxval 255, one white-space byte, pixels */
static const uint8_t *ppm_pixels(const uint8_t *b, uint64_t n, uint32_t *w, uint32_t *h)
{
    uint64_t i = 2;
    uint32_t v[3] = {0, 0, 0};
    if (n < 2 || b[0] != 'P' || b[1] != '6') return NULL;
    for (int k = 0; k < 3; k++) {
        for (;;) {
            while (i < n && (b[i] == ' ' || b[i] == '\t' || b[i] == '\n' || b[i] == '\r')) i++;
            if (i < n && b[i] == '#')
                while (i < n && b[i] != '\n') i++;
            else
                break;
        }
        if (i >= n || b[i] < '0' || b[i] > '9') return NULL;
        while (i < n && b[i] >= '0' && b[i] <= '9' && v[k] < 100000)
            v[k] = v[k] * 10 + (b[i++] - '0');
    }
    i++;
    if (v[2] != 255 || !v[0] || !v[1] || (uint64_t) v[0] * v[1] * 3 > n - i) return NULL;
    *w = v[0];
    *h = v[1];
    return b + i;
}

static int32_t encode_text(const zt_tok_t *t, const char *s, int32_t *ids, uint64_t cap,
                           uint64_t *n)
{
    uint64_t len = strlen(s), wb = zt_tok_work_bytes(len);
    void *work = alloc64(wb);
    if (!work) return -1;
    int32_t r = zt_tok_encode(t, (const uint8_t *) s, len, true, ids, cap, n, work, wb);
    free(work);
    return r;
}

int main(int argc, char **argv)
{
    const char *paths[3] = {NULL, NULL, NULL}, *prompt = "Describe this image in one sentence.";
    const char *kern_name = NULL, *dump_embd = NULL, *dump_rgb = NULL;
    uint32_t n_gen = 64, n_threads = 1, np = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--prompt") && v)
            prompt = argv[++i];
        else if (!strcmp(a, "-n") && v)
            n_gen = (uint32_t) strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--kern") && v)
            kern_name = argv[++i];
        else if (!strcmp(a, "--threads") && v)
            n_threads = (uint32_t) strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--dump-embd") && v)
            dump_embd = argv[++i];
        else if (!strcmp(a, "--dump-rgb") && v)
            dump_rgb = argv[++i];
        else if (a[0] != '-' && np < 3)
            paths[np++] = a;
        else {
            fprintf(stderr,
                    "usage: %s MODEL.gguf MMPROJ.gguf IMAGE.ppm [--prompt TEXT] [-n N] "
                    "[--kern NAME] [--threads T] [--dump-embd FILE] [--dump-rgb FILE]\n",
                    argv[0]);
            return 2;
        }
    }
    if (np != 3 || !n_gen) {
        fprintf(stderr, "zt_vlm: a model, a projector and an image are required\n");
        return 2;
    }
    uint64_t sz[3];
    uint8_t *buf[3];
    for (int i = 0; i < 3; i++)
        if (!(buf[i] = read_file(paths[i], &sz[i]))) {
            fprintf(stderr, "zt_vlm: cannot read %s\n", paths[i]);
            return 3;
        }
    zt_gguf_t g, gv;
    if (zt_gguf_open(&g, buf[0], sz[0]) || zt_gguf_open(&gv, buf[1], sz[1])) {
        fprintf(stderr, "zt_vlm: not a readable GGUF file\n");
        return 4;
    }
    uint32_t iw, ih;
    const uint8_t *px = ppm_pixels(buf[2], sz[2], &iw, &ih);
    if (!px) {
        fprintf(stderr, "zt_vlm: %s is not a binary PPM (P6, maxval 255)\n", paths[2]);
        return 3;
    }

    zt_model_err_t err;
    memset(&err, 0, sizeof err);
    uint64_t mbytes = zt_model_arena_bytes(&g), vbytes = zt_vision_arena_bytes(&gv);
    void *marena = mbytes ? alloc64(mbytes) : NULL, *varena = vbytes ? alloc64(vbytes) : NULL;
    zt_model_t m;
    zt_vision_t vis;
    if (!marena || zt_model_load(&m, &g, marena, mbytes, &err)) {
        fprintf(stderr, "zt_vlm: model: %s %s\n", err.what ? err.what : "not loadable", err.name);
        return 4;
    }
    if (!varena || zt_vision_load(&vis, &gv, varena, vbytes, &err)) {
        zt_vision_config(&gv, &vis.cfg, &err);
        fprintf(stderr, "zt_vlm: projector: %s %s\n", err.what ? err.what : "not loadable",
                err.name);
        return 4;
    }
    if (vis.cfg.proj_dim != m.cfg.n_embd) {
        fprintf(stderr, "zt_vlm: projector width %u != model width %u\n", vis.cfg.proj_dim,
                m.cfg.n_embd);
        return 4;
    }
    const zt_kern_t *kb = kern_name ? zt_simd_find(kern_name) : zt_simd_best();
    if (!kb) {
        fprintf(stderr, "zt_vlm: kernel set '%s' is not available on this CPU\n", kern_name);
        return 2;
    }
    zt_pool_t *pool = n_threads > 1 ? zt_pool_new(n_threads) : NULL;
    zt_kern_t kern;
    zt_kern_threaded(&kern, kb, pool);
    m.kern = &kern;
    vis.kern = &kern;
    uint64_t tbytes = zt_tok_arena_bytes(&g);
    void *tarena = tbytes ? alloc64(tbytes) : NULL;
    zt_tok_t tok;
    if (!tarena || zt_tok_load(&tok, &g, tarena, tbytes)) {
        fprintf(stderr, "zt_vlm: the model has no tokenizer the engine can load\n");
        return 4;
    }

    /* image -> vectors */
    uint32_t S = vis.cfg.image_size;
    uint8_t *rgb = malloc((size_t) S * S * 3);
    double t0 = now_s();
    zt_vision_resize(px, iw, ih, iw * 3, rgb, S);
    double t1 = now_s();
    if (dump_rgb) {
        FILE *f = fopen(dump_rgb, "wb");
        if (!f || fwrite(rgb, 1, (size_t) S * S * 3, f) != (size_t) S * S * 3) return 5;
        fclose(f);
    }
    uint32_t no = vis.cfg.n_out, ne = m.cfg.n_embd, vsat = 0;
    zt_fx *embd = malloc((size_t) no * ne * sizeof(zt_fx));
    uint64_t wbytes = zt_vision_work_bytes(&vis);
    void *vwork = alloc64(wbytes);
    if (!embd || !vwork || zt_vision_encode(&vis, rgb, embd, vwork, wbytes, &vsat)) {
        fprintf(stderr, "zt_vlm: image encoding failed\n");
        return 5;
    }
    double t2 = now_s();
    free(vwork);
    if (dump_embd) {
        FILE *f = fopen(dump_embd, "wb");
        if (!f || fwrite(embd, sizeof(zt_fx), (size_t) no * ne, f) != (size_t) no * ne) return 5;
        fclose(f);
    }

    /* text before and after the image */
    char *pre = "<|im_start|>User:<fake_token_around_image><global-img>";
    uint64_t plen = strlen(prompt);
    char *post = malloc(plen + 96);
    snprintf(post, plen + 96, "<fake_token_around_image>%s<end_of_utterance>\nAssistant:", prompt);
    uint64_t cap = plen + 256, n_pre = 0, n_post = 0;
    int32_t *ids = malloc((size_t) (cap + n_gen) * sizeof(int32_t));
    if (!ids || encode_text(&tok, pre, ids, cap, &n_pre) ||
        encode_text(&tok, post, ids + n_pre, cap - n_pre, &n_post)) {
        fprintf(stderr, "zt_vlm: prompt encoding failed\n");
        return 5;
    }
    uint32_t n_ctx = (uint32_t) (n_pre + no + n_post + n_gen + 1);
    uint64_t sbytes = zt_model_state_bytes(&m, n_ctx, 128, ZT_KV_Q16);
    void *smem = alloc64(sbytes);
    zt_model_state_t st;
    if (!smem || zt_model_state_init(&st, &m, n_ctx, 128, ZT_KV_Q16, smem, sbytes)) return 5;
    uint32_t nv = m.cfg.n_vocab;
    zt_fx *logits = malloc((size_t) nv * sizeof(zt_fx));
    if (!logits) return 5;
    double t3 = now_s();
    if (zt_model_eval(&m, &st, ids, (uint32_t) n_pre, NULL, 0) ||
        zt_model_eval_embd(&m, &st, embd, no, NULL, 0) ||
        zt_model_eval(&m, &st, ids + n_pre, (uint32_t) n_post, logits, 0)) {
        fprintf(stderr, "zt_vlm: prefill failed\n");
        return 5;
    }
    double t4 = now_s();
    int32_t eou = zt_tok_find(&tok, (const uint8_t *) "<end_of_utterance>", 18);
    uint64_t lh = fnv(0xcbf29ce484222325ull, logits, nv);
    int32_t *gen = ids + n_pre + n_post;
    uint32_t n_out = 0;
    for (;;) {
        int32_t id = zt_argmax(logits, nv);
        if (id == tok.eos || id == eou) break;
        gen[n_out++] = id;
        uint8_t piece[256];
        uint64_t pl = 0;
        if (!zt_tok_decode(&tok, &id, 1, piece, sizeof piece, &pl)) {
            fwrite(piece, 1, (size_t) pl, stdout);
            fflush(stdout);
        }
        if (n_out == n_gen || zt_model_decode(&m, &st, id, logits)) break;
        lh = fnv(lh, logits, nv);
    }
    double t5 = now_s();
    printf("\nzt.kern=%s\nzt.threads=%u\n", kb->name, pool ? n_threads : 1);
    printf("zt.image=%ux%u\nzt.image_tokens=%u\nzt.n_prompt=%llu\n", iw, ih, no,
           (unsigned long long) (n_pre + no + n_post));
    printf("zt.resize_s=%.3f\nzt.encode_s=%.3f\nzt.prefill_s=%.3f\n", t1 - t0, t2 - t1, t4 - t3);
    printf("zt.n_gen=%u\nzt.gen_s=%.3f\nzt.gen_tps=%.2f\n", n_out, t5 - t4,
           t5 > t4 ? n_out / (t5 - t4) : 0.0);
    printf("zt.ids=");
    for (uint32_t i = 0; i < n_out; i++) printf(i ? ",%d" : "%d", gen[i]);
    printf("\nzt.embd_fnv=%016llx\nzt.logits_fnv=%016llx\nzt.n_sat_vision=%u\nzt.n_sat=%u\n",
           (unsigned long long) fnv(0xcbf29ce484222325ull, embd, (uint64_t) no * ne),
           (unsigned long long) lh, vsat, st.n_sat);
    zt_pool_free(pool);
    return 0;
}
