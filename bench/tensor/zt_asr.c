/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_asr.c -- hosted speech-to-text driver for the integer Whisper (T25,
 * kernel/src/tensor/zt_whisper.h).
 *
 * Usage: zt_asr MODEL.gguf in.wav [--kern c|avx2|avx512|avx512vnni|neon]
 *               [--threads N] [--lang CODE|auto] [--max-tokens N]
 *               [--dump-mel FILE] [--dump-enc FILE] [--dump-logits FILE]
 *
 * MODEL.gguf comes from bench/tensor/convert_whisper.py. in.wav must be a
 * 16 kHz mono 16-bit PCM WAV (anything else is refused: no resampling or
 * down-mixing here; e.g. ffmpeg -i x -ar 16000 -ac 1 -c:a pcm_s16le y.wav).
 * Only the first 30 s are transcribed. --lang defaults to en (as
 * whisper.cpp's whisper-cli); auto detects it.
 * Prints the transcript, then lines of key=value:
 *   zt.kern, zt.threads, zt.lang, zt.load_s, zt.mel_s, zt.enc_s, zt.dec_s,
 *   zt.n_tokens, zt.dec_tps (decoded tokens a second, prompt included in
 *   the time), zt.ids, zt.mel_fnv, zt.enc_fnv, zt.logits_fnv (FNV-1a 64 of
 *   the Q16 values, so two machines can check they computed the same bits),
 *   zt.n_sat.
 * --dump-mel / --dump-enc / --dump-logits write the Q16 values as raw
 * little-endian int32: mel [n_frames][n_mels], encoder output
 * [n_audio_ctx][n_state], and the logits of every decoding step (before
 * suppression) [steps][n_vocab].
 * Exit: 0 ok, 2 usage, 3 cannot read a file, 4 model not loadable, 5 run
 * failed, 6 unsupported WAV. Hosted only (libc, malloc, mmap). */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "zt_gguf.h"
#include "zt_simd.h"
#include "zt_tok.h"
#include "zt_whisper.h"

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

static void *alloc64(uint64_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes ? (size_t) bytes : 64) != 0) return NULL;
    return p;
}

static const uint8_t *map_file(const char *path, uint64_t *size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat sb;
    if (fstat(fd, &sb) != 0 || sb.st_size <= 0) {
        close(fd);
        return NULL;
    }
    void *p = mmap(NULL, (size_t) sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return NULL;
    *size = (uint64_t) sb.st_size;
    return p;
}

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return rd16(p) | (rd16(p + 2) << 16);
}

/* 16 kHz mono 16-bit PCM only. Returns the sample count or -1 (msg set). */
static int64_t parse_wav(const uint8_t *b, uint64_t n, const int16_t **pcm, const char **msg)
{
    if (n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) {
        *msg = "not a RIFF/WAVE file";
        return -1;
    }
    uint64_t o = 12;
    int have_fmt = 0;
    while (o + 8 <= n) {
        uint32_t len = rd32(b + o + 4);
        const uint8_t *c = b + o + 8;
        if (!memcmp(b + o, "fmt ", 4)) {
            if (len < 16 || o + 8 + len > n) {
                *msg = "bad fmt chunk";
                return -1;
            }
            uint32_t fmt = rd16(c), ch = rd16(c + 2), rate = rd32(c + 4), bits = rd16(c + 14);
            if (fmt == 0xFFFE && len >= 26) fmt = rd16(c + 24); /* WAVE_FORMAT_EXTENSIBLE */
            if (fmt != 1) {
                *msg = "not integer PCM (convert to 16-bit PCM)";
                return -1;
            }
            if (ch != 1) {
                *msg = "not mono (mixing channels is out of scope)";
                return -1;
            }
            if (rate != 16000) {
                *msg = "sample rate is not 16000 Hz (resampling is out of scope)";
                return -1;
            }
            if (bits != 16) {
                *msg = "not 16-bit samples";
                return -1;
            }
            have_fmt = 1;
        } else if (!memcmp(b + o, "data", 4)) {
            if (!have_fmt) {
                *msg = "data chunk before fmt chunk";
                return -1;
            }
            uint64_t avail = n - (o + 8);
            uint64_t bytes = len < avail ? len : avail;
            *pcm = (const int16_t *) c; /* little-endian hosts only */
            return (int64_t) (bytes / 2);
        }
        o += 8 + (uint64_t) len + (len & 1);
    }
    *msg = "no data chunk";
    return -1;
}

static uint64_t fnv(uint64_t h, const int32_t *v, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) {
        uint32_t x = (uint32_t) v[i];
        for (int b = 0; b < 4; b++) h = (h ^ (uint8_t) (x >> (8 * b))) * 0x100000001b3ull;
    }
    return h;
}

static int dump(const char *path, const int32_t *v, uint64_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(v, 4, (size_t) n, f);
    fclose(f);
    return w == n ? 0 : -1;
}

typedef struct {
    uint32_t n_vocab;
    uint64_t h;
    FILE *f;
} step_ctx_t;

static bool on_step(void *ctx, uint32_t step, const zt_fx *logits, int32_t token)
{
    step_ctx_t *s = ctx;
    (void) step;
    (void) token;
    s->h = fnv(s->h, logits, s->n_vocab);
    if (s->f) fwrite(logits, 4, s->n_vocab, s->f);
    return true;
}

int main(int argc, char **argv)
{
    const char *mpath = NULL, *wpath = NULL, *kern_name = NULL, *lang = "en";
    const char *dmel = NULL, *denc = NULL, *dlog = NULL;
    uint32_t n_threads = 1, max_tokens = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--kern") && more)
            kern_name = argv[++i];
        else if (!strcmp(a, "--threads") && more)
            n_threads = (uint32_t) strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--lang") && more)
            lang = argv[++i];
        else if (!strcmp(a, "--max-tokens") && more)
            max_tokens = (uint32_t) strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--dump-mel") && more)
            dmel = argv[++i];
        else if (!strcmp(a, "--dump-enc") && more)
            denc = argv[++i];
        else if (!strcmp(a, "--dump-logits") && more)
            dlog = argv[++i];
        else if (a[0] != '-' && !mpath)
            mpath = a;
        else if (a[0] != '-' && !wpath)
            wpath = a;
        else {
            mpath = NULL;
            break;
        }
    }
    if (!mpath || !wpath) {
        fprintf(stderr,
                "usage: %s MODEL.gguf in.wav [--kern c|avx2|avx512|avx512vnni|neon] "
                "[--threads N] [--lang CODE|auto] [--max-tokens N] [--dump-mel FILE] "
                "[--dump-enc FILE] [--dump-logits FILE]\n",
                argv[0]);
        return 2;
    }
    double t0 = now_s();
    uint64_t msize = 0, wsize = 0;
    const uint8_t *mfile = map_file(mpath, &msize), *wfile = map_file(wpath, &wsize);
    if (!mfile || !wfile) {
        fprintf(stderr, "zt_asr: cannot read %s\n", !mfile ? mpath : wpath);
        return 3;
    }
    const int16_t *pcm = NULL;
    const char *msg = "";
    int64_t n_pcm = parse_wav(wfile, wsize, &pcm, &msg);
    if (n_pcm < 0) {
        fprintf(stderr, "zt_asr: %s: unsupported WAV: %s (need 16 kHz mono 16-bit PCM)\n", wpath,
                msg);
        return 6;
    }
    zt_gguf_t g;
    if (zt_gguf_open(&g, mfile, msize) != 0) {
        fprintf(stderr, "zt_asr: not a readable GGUF file\n");
        return 4;
    }
    zt_model_err_t err;
    memset(&err, 0, sizeof err);
    uint64_t abytes = zt_whisper_arena_bytes(&g);
    zt_whisper_t w;
    void *arena = abytes ? alloc64(abytes) : NULL;
    if (!arena || zt_whisper_load(&w, &g, arena, abytes, &err) != 0) {
        if (!abytes) {
            zt_whisper_cfg_t c;
            zt_whisper_config(&g, &c, &err);
        }
        fprintf(stderr, "zt_asr: cannot load the model: %s%s%s (code %d)\n",
                err.what ? err.what : "unknown", err.name[0] ? ": " : "", err.name, err.code);
        return 4;
    }
    uint64_t tbytes = zt_tok_arena_bytes(&g);
    void *tarena = tbytes ? alloc64(tbytes) : NULL;
    zt_tok_t tok;
    if (!tarena || zt_tok_load(&tok, &g, tarena, tbytes) != 0) {
        fprintf(stderr, "zt_asr: the file has no tokenizer the engine can load\n");
        return 4;
    }
    const zt_kern_t *kb = kern_name ? zt_simd_find(kern_name) : zt_simd_best();
    if (!kb) {
        fprintf(stderr, "zt_asr: kernel set '%s' is not available on this CPU\n", kern_name);
        return 2;
    }
    zt_pool_t *pool = n_threads > 1 ? zt_pool_new(n_threads) : NULL;
    zt_kern_t kern;
    zt_kern_threaded(&kern, kb, pool);
    w.kern = &kern;
    uint64_t sbytes = zt_whisper_state_bytes(&w);
    void *smem = alloc64(sbytes);
    zt_whisper_state_t st;
    if (!smem || zt_whisper_state_init(&st, &w, smem, sbytes) != 0) {
        fprintf(stderr, "zt_asr: cannot allocate %llu bytes of state\n",
                (unsigned long long) sbytes);
        return 5;
    }
    double t1 = now_s();
    const zt_whisper_cfg_t *c = &w.cfg;
    if ((uint64_t) n_pcm > c->n_samples)
        fprintf(stderr, "zt_asr: note: only the first %u s of %.1f s are transcribed\n",
                c->n_samples / c->sample_rate, (double) n_pcm / c->sample_rate);
    if (zt_whisper_mel(&w, &st, pcm, (uint64_t) n_pcm) != 0) return 5;
    double t2 = now_s();
    if (zt_whisper_encode(&w, &st) != 0) return 5;
    double t3 = now_s();
    int32_t li = 0;
    if (!strcmp(lang, "auto")) {
        uint32_t d = 0;
        if (zt_whisper_detect_lang(&w, &st, &d) != 0) return 5;
        li = (int32_t) d;
    } else if ((li = zt_whisper_lang_find(&g, lang)) < 0) {
        fprintf(stderr, "zt_asr: unknown language code '%s'\n", lang);
        return 2;
    }
    double t4 = now_s();
    uint32_t cap = c->n_text_ctx, n_out = 0;
    if (!max_tokens || max_tokens > c->n_text_ctx / 2) max_tokens = c->n_text_ctx / 2;
    int32_t *ids = malloc(cap * sizeof *ids);
    step_ctx_t sc = {c->n_vocab, 0xcbf29ce484222325ull, dlog ? fopen(dlog, "wb") : NULL};
    if (!ids || (dlog && !sc.f)) return 5;
    if (zt_whisper_greedy(&w, &st, (uint32_t) li, false, max_tokens, ids, cap, &n_out, on_step,
                          &sc) != 0) {
        fprintf(stderr, "zt_asr: decoding failed\n");
        return 5;
    }
    double t5 = now_s();
    if (sc.f) fclose(sc.f);
    uint64_t tcap = (uint64_t) n_out * 64 + 16, n_txt = 0;
    uint8_t *txt = malloc(tcap);
    if (!txt || zt_tok_decode(&tok, ids, n_out, txt, tcap, &n_txt) != 0) return 5;
    fwrite(txt, 1, n_txt, stdout);
    printf("\n");
    uint64_t nmel = (uint64_t) c->n_frames * c->n_mels,
             nenc = (uint64_t) c->n_audio_ctx * c->n_state;
    zt_gguf_str_t lc = zt_whisper_lang_code(&g, (uint32_t) li);
    printf("zt.kern=%s\nzt.threads=%u\nzt.lang=%.*s\n", kb->name, pool ? n_threads : 1,
           (int) lc.len, (const char *) lc.p);
    printf("zt.load_s=%.3f\nzt.mel_s=%.3f\nzt.enc_s=%.3f\nzt.lang_s=%.3f\nzt.dec_s=%.3f\n", t1 - t0,
           t2 - t1, t3 - t2, t4 - t3, t5 - t4);
    printf("zt.n_tokens=%u\nzt.dec_tps=%.2f\nzt.ids=", n_out,
           t5 > t4 ? (double) (n_out + 1) / (t5 - t4) : 0.0);
    for (uint32_t i = 0; i < n_out; i++) printf(i ? ",%d" : "%d", ids[i]);
    printf("\nzt.mel_fnv=%016llx\nzt.enc_fnv=%016llx\nzt.logits_fnv=%016llx\nzt.n_sat=%u\n",
           (unsigned long long) fnv(0xcbf29ce484222325ull, st.mel, nmel),
           (unsigned long long) fnv(0xcbf29ce484222325ull, st.enc, nenc), (unsigned long long) sc.h,
           st.n_sat);
    if ((dmel && dump(dmel, st.mel, nmel)) || (denc && dump(denc, st.enc, nenc))) {
        fprintf(stderr, "zt_asr: cannot write a dump file\n");
        return 3;
    }
    zt_pool_free(pool);
    free(txt);
    free(ids);
    free(smem);
    free(tarena);
    free(arena);
    return 0;
}
