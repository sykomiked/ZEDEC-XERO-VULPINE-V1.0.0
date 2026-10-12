/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_tts.c -- hosted text-to-speech driver for the integer tensor engine (T26).
 *
 * Text -> OuteTTS 0.3 prompt (zt_tts_prompt) -> audio-code tokens (zt_model,
 * sampled with zt_sample: top-k 4 at temperature 1, llama.cpp's OuteTTS
 * settings) -> WavTokenizer decoder + integer iSTFT (zt_tts_decode) -> a
 * 24 kHz mono 16-bit WAV file. Prints, one key=value per line:
 *
 *   zt.kern, zt.threads
 *   zt.n_prompt, zt.prefill_s, zt.prefill_tps   (the language model)
 *   zt.n_gen, zt.gen_s, zt.gen_tps, zt.stop, zt.lm_n_sat
 *   zt.n_codes, zt.codes_fnv                    (FNV-1a 64 of the code ids)
 *   zt.audio_s, zt.dec_s, zt.dec_rtf            (decoder seconds per audio second)
 *   zt.pcm_fnv, zt.n_sat, zt.n_clip             (FNV-1a 64 of the PCM bytes)
 *
 * zt.codes_fnv and zt.pcm_fnv let two machines or kernel sets check they
 * produced the same bits. Hosted only (libc, malloc, clock_gettime).
 *
 * Usage: zt_tts TTS.gguf DECODER.gguf "text" out.wav [--seed N] [--kern NAME]
 *               [--threads N] [--codes-out FILE] [--codes-in FILE]
 *               [--top-k K] [--temp T] [--max N] [--skip N] [--spec-out FILE]
 *               [--lean] [--prompt-out FILE]
 * --codes-in reads whitespace-separated code ids (0..4095) and skips the
 * language model (TTS.gguf and the text are then ignored; pass "-").
 * --codes-out writes the code ids the same way. --top-k 1 (or --temp 0) is
 * greedy. --max caps the generated tokens (default 4096, as llama.cpp).
 * --skip is how many leading samples are zeroed (default 6000 = 0.25 s, as
 * llama.cpp). --lean loads the decoder with ZT_TTS_LOAD_LEAN (one 8-bit block
 * per 32 weights, no residual block). --spec-out writes the decoder output (n_codes x 1282 Q16
 * int32, little-endian) for bench/tensor/compare/ref_wavtokenizer.py.
 * Exit: 0 ok, 2 usage, 3 cannot read a file, 4 model not loadable, 5 run failed. */
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
#include "zt_tts.h"

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
    uint8_t *buf = malloc((size_t) n + 1);
    if (buf && fread(buf, 1, (size_t) n, f) != (size_t) n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) buf[n] = 0;
    *size = (uint64_t) n;
    return buf;
}

static void *alloc64(uint64_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes ? (size_t) bytes : 64) != 0) return NULL;
    return p;
}

static uint64_t fnv(uint64_t h, const void *p, uint64_t n)
{
    const uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

static void le16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static void le32(uint8_t *p, uint32_t v)
{
    le16(p, v);
    le16(p + 2, v >> 16);
}

static int write_wav(const char *path, const int16_t *pcm, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    le32(h + 4, 36 + 2 * n);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);
    le16(h + 20, 1);
    le16(h + 22, 1);
    le32(h + 24, ZT_TTS_RATE);
    le32(h + 28, ZT_TTS_RATE * 2);
    le16(h + 32, 2);
    le16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    le32(h + 40, 2 * n);
    int ok = fwrite(h, 1, 44, f) == 44;
    for (uint32_t i = 0; ok && i < n; i++) {
        uint8_t s[2];
        le16(s, (uint16_t) pcm[i]);
        ok = fwrite(s, 1, 2, f) == 2;
    }
    return fclose(f) == 0 && ok ? 0 : -1;
}

typedef struct {
    int32_t *ids;
    uint32_t n, cap;
    double t_first;
} gen_ctx_t;

static bool on_token(void *ctx, int32_t id, const uint8_t *piece, uint32_t len)
{
    (void) piece;
    (void) len;
    gen_ctx_t *g = ctx;
    if (g->n == 0) g->t_first = now_s();
    if (g->n < g->cap) g->ids[g->n++] = id;
    return true;
}

static void usage(const char *a0)
{
    fprintf(stderr,
            "usage: %s TTS.gguf DECODER.gguf \"text\" out.wav [--seed N] [--kern NAME] "
            "[--threads N] [--codes-out FILE] [--codes-in FILE] [--top-k K] [--temp T] "
            "[--max N] [--skip N] [--lean] [--spec-out FILE] [--prompt-out FILE]\n",
            a0);
}

int main(int argc, char **argv)
{
    const char *pos[4] = {0, 0, 0, 0};
    const char *kern_name = NULL, *codes_out = NULL, *codes_in = NULL, *spec_out = NULL;
    const char *prompt_out = NULL;
    uint64_t seed = 42;
    uint32_t lflags = 0, n_threads = 1, top_k = 4, max_gen = 4096, skip = ZT_TTS_SKIP, npos = 0;
    double temp = 1.0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool hv = i + 1 < argc;
        if (!strcmp(a, "--seed") && hv)
            seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--kern") && hv)
            kern_name = argv[++i];
        else if (!strcmp(a, "--threads") && hv)
            n_threads = (uint32_t) strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--codes-out") && hv)
            codes_out = argv[++i];
        else if (!strcmp(a, "--codes-in") && hv)
            codes_in = argv[++i];
        else if (!strcmp(a, "--spec-out") && hv)
            spec_out = argv[++i];
        else if (!strcmp(a, "--prompt-out") && hv)
            prompt_out = argv[++i];
        else if (!strcmp(a, "--top-k") && hv)
            top_k = (uint32_t) strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--temp") && hv)
            temp = strtod(argv[++i], NULL);
        else if (!strcmp(a, "--max") && hv)
            max_gen = (uint32_t) strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--lean"))
            lflags = ZT_TTS_LOAD_LEAN;
        else if (!strcmp(a, "--skip") && hv)
            skip = (uint32_t) strtoul(argv[++i], NULL, 10);
        else if ((a[0] != '-' || !strcmp(a, "-")) && npos < 4)
            pos[npos++] = a;
        else {
            usage(argv[0]);
            return 2;
        }
    }
    if (npos != 4 || max_gen == 0) {
        usage(argv[0]);
        return 2;
    }
    const zt_kern_t *kb = kern_name ? zt_simd_find(kern_name) : zt_simd_best();
    if (!kb) {
        fprintf(stderr, "zt_tts: kernel set '%s' is not available on this CPU\n", kern_name);
        return 2;
    }
    zt_pool_t *pool = n_threads > 1 ? zt_pool_new(n_threads) : NULL;
    zt_kern_t kern;
    zt_kern_threaded(&kern, kb, pool);
    printf("zt.kern=%s\nzt.threads=%u\n", kb->name, pool ? n_threads : 1);

    int32_t *codes = NULL;
    uint32_t n_codes = 0;
    zt_model_err_t err;
    memset(&err, 0, sizeof err);

    if (codes_in) {
        uint64_t sz = 0;
        char *txt = (char *) read_file(codes_in, &sz);
        if (!txt) {
            fprintf(stderr, "zt_tts: cannot read %s\n", codes_in);
            return 3;
        }
        codes = malloc((sz / 2 + 1) * sizeof *codes);
        char *p = txt, *e;
        for (;;) {
            long v = strtol(p, &e, 10);
            if (e == p) break;
            if (v < 0 || v >= (long) ZT_TTS_CODES) {
                fprintf(stderr, "zt_tts: code %ld out of range\n", v);
                return 5;
            }
            codes[n_codes++] = (int32_t) v;
            p = e;
        }
        free(txt);
    } else {
        uint64_t size = 0;
        uint8_t *file = read_file(pos[0], &size);
        if (!file) {
            fprintf(stderr, "zt_tts: cannot read %s\n", pos[0]);
            return 3;
        }
        zt_gguf_t g;
        if (zt_gguf_open(&g, file, size) != 0) {
            fprintf(stderr, "zt_tts: %s is not a readable GGUF file\n", pos[0]);
            return 4;
        }
        uint64_t mbytes = zt_model_arena_bytes(&g);
        void *marena = mbytes ? alloc64(mbytes) : NULL;
        zt_model_t m;
        if (!marena || zt_model_load(&m, &g, marena, mbytes, &err) != 0) {
            fprintf(stderr, "zt_tts: TTS model load failed: %s %s\n", err.what ? err.what : "",
                    err.name);
            return 4;
        }
        m.kern = &kern;
        uint64_t tbytes = zt_tok_arena_bytes(&g);
        void *tarena = tbytes ? alloc64(tbytes) : NULL;
        zt_tok_t tok;
        zt_tts_vocab_t voc;
        if (!tarena || zt_tok_load(&tok, &g, tarena, tbytes) != 0 || zt_tts_vocab(&tok, &voc)) {
            fprintf(stderr, "zt_tts: the TTS file has no OuteTTS vocabulary\n");
            return 4;
        }
        const uint8_t *text = (const uint8_t *) pos[2];
        uint64_t tlen = strlen(pos[2]);
        uint64_t plen = zt_tts_prompt(text, tlen, NULL, 0);
        uint8_t *prompt = malloc(plen + 1);
        zt_tts_prompt(text, tlen, prompt, plen);
        prompt[plen] = 0;
        if (prompt_out) {
            FILE *f = fopen(prompt_out, "wb");
            if (f) {
                fwrite(prompt, 1, plen, f);
                fclose(f);
            }
        }
        uint32_t n_ctx = (uint32_t) (plen + max_gen + 8 < ZT_MODEL_MAX_CTX ? plen + max_gen + 8
                                                                           : ZT_MODEL_MAX_CTX);
        uint64_t sbytes = zt_model_state_bytes(&m, n_ctx, 64, ZT_KV_Q16);
        void *smem = alloc64(sbytes);
        zt_model_state_t st;
        if (!smem || zt_model_state_init(&st, &m, n_ctx, 64, ZT_KV_Q16, smem, sbytes) != 0) {
            fprintf(stderr, "zt_tts: cannot set up a %u-token context\n", n_ctx);
            return 5;
        }
        zt_sampler_params_t sp = {0};
        sp.temperature = (zt_fx) (temp * 65536.0 + 0.5);
        sp.top_k = top_k;
        sp.seed = seed;
        zt_sampler_t smp;
        zt_sampler_init(&smp, &sp);
        int32_t stops[2] = {voc.audio_end, voc.eot};
        gen_ctx_t gc = {malloc((size_t) max_gen * sizeof(int32_t)), 0, max_gen, 0};
        zt_gen_params_t gp;
        memset(&gp, 0, sizeof gp);
        gp.sampler = (temp > 0 && top_k != 1) ? &smp : NULL;
        gp.max_tokens = max_gen;
        gp.parse_special = true;
        gp.stop_ids = stops;
        gp.n_stop_ids = voc.eot >= 0 ? 2 : 1;
        gp.on_token = on_token;
        gp.ctx = &gc;
        uint64_t wb = zt_model_gen_work_bytes(&m, &tok, plen, max_gen);
        void *work = alloc64(wb);
        zt_gen_result_t res;
        double t0 = now_s();
        int32_t r = zt_model_generate(&m, &st, &tok, prompt, plen, &gp, work, wb, &res);
        double t2 = now_s();
        if (r != 0) {
            fprintf(stderr, "zt_tts: generation failed (%d)\n", r);
            return 5;
        }
        double t1 = gc.n ? gc.t_first : t2;
        static const char *const stop_name[6] = {"?", "eos", "max", "stop_str", "callback", "ctx"};
        printf("zt.n_prompt=%u\nzt.prefill_s=%.3f\nzt.prefill_tps=%.1f\n", res.n_prompt, t1 - t0,
               t1 > t0 ? res.n_prompt / (t1 - t0) : 0.0);
        printf("zt.n_gen=%u\nzt.gen_s=%.3f\nzt.gen_tps=%.2f\nzt.stop=%s\nzt.lm_n_sat=%u\n", gc.n,
               t2 - t1, gc.n > 1 && t2 > t1 ? (gc.n - 1) / (t2 - t1) : 0.0,
               stop_name[res.stop < 6 ? res.stop : 0], st.n_sat);
        codes = malloc(((size_t) gc.n + 1) * sizeof *codes);
        n_codes = zt_tts_codes(&voc, gc.ids, gc.n, codes);
        free(gc.ids);
        free(work);
        free(smem);
        free(prompt);
        free(tarena);
        free(marena);
        free(file);
    }
    printf("zt.n_codes=%u\nzt.codes_fnv=%016llx\n", n_codes,
           (unsigned long long) fnv(0xcbf29ce484222325ull, codes, (uint64_t) n_codes * 4));
    if (codes_out) {
        FILE *f = fopen(codes_out, "w");
        if (!f) return 5;
        for (uint32_t i = 0; i < n_codes; i++)
            fprintf(f, "%d%c", codes[i], i % 16 == 15 ? '\n' : ' ');
        fprintf(f, "\n");
        fclose(f);
    }
    if (n_codes == 0) {
        fprintf(stderr, "zt_tts: no audio codes\n");
        return 5;
    }

    /* the decoder */
    uint64_t dsize = 0;
    uint8_t *dfile = read_file(pos[1], &dsize);
    if (!dfile) {
        fprintf(stderr, "zt_tts: cannot read %s\n", pos[1]);
        return 3;
    }
    zt_gguf_t dg;
    if (zt_gguf_open(&dg, dfile, dsize) != 0) {
        fprintf(stderr, "zt_tts: %s is not a readable GGUF file\n", pos[1]);
        return 4;
    }
    double tl = now_s();
    uint64_t abytes = zt_tts_arena_bytes_ex(&dg, lflags);
    void *arena = abytes ? alloc64(abytes) : NULL;
    zt_tts_dec_t dec;
    if (!arena || zt_tts_load_ex(&dec, &dg, arena, abytes, lflags, &err) != 0) {
        zt_tts_cfg_t c;
        zt_tts_config(&dg, &c, &err);
        fprintf(stderr, "zt_tts: decoder load failed: %s %s (code %d)\n", err.what ? err.what : "",
                err.name, err.code);
        return 4;
    }
    printf("zt.dec_load_s=%.3f\nzt.dec_arena_mb=%.1f\n", now_s() - tl, abytes / 1048576.0);
    dec.kern = &kern;
    uint64_t wbytes = zt_tts_work_bytes(&dec, n_codes);
    void *work = alloc64(wbytes);
    int16_t *pcm = malloc((size_t) n_codes * ZT_TTS_HOP * 2);
    zt_tts_stats_t ds;
    if (!work || !pcm) return 5;
    double d0 = now_s();
    int32_t r = zt_tts_decode(&dec, codes, n_codes, pcm, skip, work, wbytes, &ds);
    double d1 = now_s();
    if (r) {
        fprintf(stderr, "zt_tts: decode failed (%d)\n", r);
        return 5;
    }
    double audio_s = (double) ds.n_samples / ZT_TTS_RATE;
    printf("zt.audio_s=%.3f\nzt.dec_s=%.3f\nzt.dec_rtf=%.4f\n", audio_s, d1 - d0,
           (d1 - d0) / audio_s);
    printf("zt.pcm_fnv=%016llx\nzt.n_sat=%u\nzt.n_clip=%u\n",
           (unsigned long long) fnv(0xcbf29ce484222325ull, pcm, (uint64_t) ds.n_samples * 2),
           ds.n_sat, ds.n_clip);
    if (spec_out) {
        zt_fx *spec = malloc((size_t) n_codes * dec.cfg.n_out * 4);
        FILE *f = fopen(spec_out, "wb");
        if (!spec || !f || zt_tts_spec(&dec, codes, n_codes, spec, work, wbytes, NULL) != 0 ||
            fwrite(spec, 4, (size_t) n_codes * dec.cfg.n_out, f) !=
                (size_t) n_codes * dec.cfg.n_out) {
            fprintf(stderr, "zt_tts: could not write the spectrogram\n");
            return 5;
        }
        fclose(f);
        free(spec);
    }
    if (write_wav(pos[3], pcm, ds.n_samples) != 0) {
        fprintf(stderr, "zt_tts: cannot write %s\n", pos[3]);
        return 5;
    }
    zt_pool_free(pool);
    free(pcm);
    free(work);
    free(arena);
    free(dfile);
    free(codes);
    return 0;
}
