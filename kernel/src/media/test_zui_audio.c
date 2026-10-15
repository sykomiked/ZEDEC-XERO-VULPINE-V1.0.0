/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_audio.c — WAV, AIFF/AIFC and FLAC decoding (zui_audio.h AU1..AU3):
 * exact samples against hashes computed by the fixture generator from the
 * integer source, FLAC MD5, refusals with reasons, float conversion, and
 * fuzzing (every truncation, random byte flips) under exact-size buffers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_audio.h"
#include "test/fx_audio.inc"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static int sec_fails;
#define SECTION() (sec_fails = fails)
#define OK()      (fails == sec_fails ? "[PASS]" : "[FAIL]")

static uint32_t fnv(const int32_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k < 4; k++) h = (h ^ ((uint32_t) p[i] >> (8 * k) & 0xFFu)) * 16777619u;
    return h;
}

static uint64_t rng = 0x617564696f2026ull;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t) rng;
}

/* decode a copy held in an exact-size heap block (ASan sees any over-read) */
static int64_t decode_copy(const uint8_t *d, uint32_t n, int32_t **pcm, zui_audio_info_t *in,
                           uint32_t flags)
{
    uint8_t *c = malloc(n ? n : 1);
    memcpy(c, d, n);
    zui_audio_info_t z;
    int64_t r = -100;
    *pcm = 0;
    if (zui_audio_probe(c, n, &z) == 0) {
        uint32_t ww = zui_audio_work_words(&z);
        int32_t *work = ww ? malloc((size_t) ww * 4) : 0;
        /* count first (out = NULL), then decode into an exact-size buffer */
        r = zui_audio_decode(c, n, 0, 0, work, ww, &z, flags);
        if (r >= 0) {
            size_t cnt = (size_t) r * z.nch;
            *pcm = malloc(cnt ? cnt * 4 : 4);
            int64_t r2 = zui_audio_decode(c, n, *pcm, (uint64_t) r, work, ww, &z, flags);
            if (r2 != r) r = -101;
        }
        free(work);
    } else {
        r = zui_audio_probe(c, n, &z);
    }
    if (in) *in = z;
    free(c);
    return r;
}

static void test_fixtures(void)
{
    SECTION();
    int nflac_md5 = 0;
    for (size_t i = 0; i < sizeof FX_AUDIO / sizeof FX_AUDIO[0]; i++) {
        const fx_audio_t *f = &FX_AUDIO[i];
        int32_t *pcm;
        zui_audio_info_t in;
        CHECK(zui_audio_sniff(f->d, f->n) == f->kind, "%s: sniffed %u", f->name,
              zui_audio_sniff(f->d, f->n));
        int64_t r = decode_copy(f->d, f->n, &pcm, &in, ZUI_AUD_F_MD5);
        CHECK(r == (int64_t) f->frames, "%s: %lld frames, want %u", f->name, (long long) r,
              f->frames);
        if (r == (int64_t) f->frames) {
            CHECK(in.rate == f->rate && in.nch == f->nch && in.bits == f->bits,
                  "%s: %u Hz %u ch %u bit", f->name, in.rate, in.nch, in.bits);
            uint32_t h = fnv(pcm, (size_t) r * in.nch);
            CHECK(h == f->fnv, "%s: samples hash %08x, want %08x", f->name, h, f->fnv);
            if (f->kind == ZUI_AUD_FLAC) {
                CHECK(in.md5_present && in.md5_checked, "%s: MD5 not checked", f->name);
                nflac_md5 += in.md5_checked;
            }
        }
        free(pcm);
    }
    printf("%s fixtures: %zu files decoded bit-exact (%d FLAC streams MD5-verified)\n", OK(),
           sizeof FX_AUDIO / sizeof FX_AUDIO[0], nflac_md5);
}

static const fx_audio_t *fx(const char *name)
{
    for (size_t i = 0; i < sizeof FX_AUDIO / sizeof FX_AUDIO[0]; i++)
        if (!strcmp(FX_AUDIO[i].name, name)) return &FX_AUDIO[i];
    return 0;
}

static void test_partial_and_count(void)
{
    SECTION();
    const fx_audio_t *f = fx("flac16s");
    zui_audio_info_t in;
    CHECK(zui_audio_probe(f->d, f->n, &in) == 0, "probe");
    uint32_t ww = zui_audio_work_words(&in);
    CHECK(ww == in.max_block * 2u && ww > 0, "work words %u", ww);
    int32_t *work = malloc((size_t) ww * 4), *out = malloc(1000 * 2 * 4);
    /* only the first 1000 frames fit: whole blocks within the cap are written */
    int64_t r = zui_audio_decode(f->d, f->n, out, 1000, work, ww, &in, 0);
    CHECK(r == (int64_t) f->frames, "capped decode returns the full count (%lld)", (long long) r);
    CHECK(zui_audio_decode(f->d, f->n, 0, 0, 0, 0, &in, 0) == ZUI_AUD_EARG,
          "FLAC count without work is refused");
    free(work);
    free(out);
    printf("%s capped decode and probe-for-size\n", OK());
}

static void test_refusals(void)
{
    SECTION();
    static const uint8_t mp3[] = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0};
    static const uint8_t mp3s[] = {0xFF, 0xFB, 0x90, 0x64, 0, 0};
    static const uint8_t aac[] = {0xFF, 0xF1, 0x50, 0x80, 0, 0};
    static const uint8_t ogg[] = {'O', 'g', 'g', 'S', 0, 2, 0, 0};
    static const uint8_t mp4[] = {0, 0, 0, 0x20, 'f', 't', 'y', 'p', 'M', '4', 'A', ' '};
    static const uint8_t rf64[] = {'R', 'F', '6', '4', 0xFF, 0xFF, 0xFF, 0xFF, 'W', 'A', 'V', 'E'};
    struct {
        const uint8_t *d;
        uint32_t n, kind;
        int rc;
    } t[] = {{mp3, sizeof mp3, ZUI_AUD_MP3, ZUI_AUD_EUNSUP},
             {mp3s, sizeof mp3s, ZUI_AUD_MP3, ZUI_AUD_EUNSUP},
             {aac, sizeof aac, ZUI_AUD_AAC, ZUI_AUD_EUNSUP},
             {ogg, sizeof ogg, ZUI_AUD_OGG, ZUI_AUD_EUNSUP},
             {mp4, sizeof mp4, ZUI_AUD_MP4, ZUI_AUD_EUNSUP},
             {rf64, sizeof rf64, ZUI_AUD_WAV, ZUI_AUD_EUNSUP}};
    zui_audio_info_t in;
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        CHECK(zui_audio_sniff(t[i].d, t[i].n) == t[i].kind, "refusal %zu sniff", i);
        CHECK(zui_audio_probe(t[i].d, t[i].n, &in) == t[i].rc, "refusal %zu code", i);
    }
    CHECK(strstr(zui_audio_reason(ZUI_AUD_MP3), "patents") != 0, "MP3 reason");
    CHECK(strstr(zui_audio_reason(ZUI_AUD_AAC), "pool") != 0, "AAC reason");
    CHECK(strstr(zui_audio_reason(ZUI_AUD_OGG), "royalty-free") != 0, "Ogg reason");
    /* a WAV with an ADPCM format tag, and an AIFC with mu-law */
    const fx_audio_t *w = fx("wav16s");
    uint8_t *c = malloc(w->n);
    memcpy(c, w->d, w->n);
    c[20] = 0x11; /* IMA ADPCM */
    CHECK(zui_audio_probe(c, w->n, &in) == ZUI_AUD_EUNSUP, "ADPCM WAV refused");
    free(c);
    const fx_audio_t *a = fx("aifcsowt");
    c = malloc(a->n);
    memcpy(c, a->d, a->n);
    for (uint32_t i = 0; i + 4 <= a->n; i++)
        if (!memcmp(c + i, "sowt", 4)) memcpy(c + i, "ulaw", 4);
    CHECK(zui_audio_probe(c, a->n, &in) == ZUI_AUD_EUNSUP, "mu-law AIFC refused");
    free(c);
    CHECK(zui_audio_probe((const uint8_t *) "hello world!", 12, &in) == ZUI_AUD_EFORMAT, "text");
    CHECK(zui_audio_probe(0, 0, &in) == ZUI_AUD_EARG, "NULL");
    printf("%s refusals: MP3, AAC, Ogg, MP4, RF64, ADPCM, mu-law, with reasons\n", OK());
}

static void test_checks(void)
{
    SECTION();
    /* a flipped CRC-16 byte, a flipped CRC-8 header bit, a wrong MD5 */
    const fx_audio_t *f = fx("flacexo");
    uint8_t *c = malloc(f->n);
    zui_audio_info_t in;
    memcpy(c, f->d, f->n);
    c[f->n - 1] ^= 1;
    int32_t *pcm;
    CHECK(decode_copy(c, f->n, &pcm, &in, 0) == ZUI_AUD_ECRC, "CRC-16");
    free(pcm);
    memcpy(c, f->d, f->n);
    c[8 + 34 + 2] ^= 0x10; /* first frame's block-size / rate byte */
    int64_t r = decode_copy(c, f->n, &pcm, &in, 0);
    CHECK(r == ZUI_AUD_ECRC || r == ZUI_AUD_EFORMAT, "header flip refused (%lld)", (long long) r);
    free(pcm);
    memcpy(c, f->d, f->n);
    c[8 + 18 + 5] ^= 0x40; /* STREAMINFO MD5 */
    CHECK(decode_copy(c, f->n, &pcm, &in, ZUI_AUD_F_MD5) == ZUI_AUD_EMD5, "MD5 mismatch caught");
    free(pcm);
    CHECK(decode_copy(c, f->n, &pcm, &in, 0) == (int64_t) f->frames, "MD5 not asked: decodes");
    free(pcm);
    free(c);
    /* MD5 known answer */
    zui_md5_t m;
    uint8_t h[16];
    static const uint8_t abc[16] = {0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
                                    0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72};
    zui_md5_init(&m);
    zui_md5_update(&m, (const uint8_t *) "abc", 3);
    zui_md5_final(&m, h);
    CHECK(!memcmp(h, abc, 16), "MD5(abc)");
    /* float conversion edges */
    CHECK(zui_audio_f32_to_i32(0x3F800000u) == INT32_MAX, "1.0 saturates");
    CHECK(zui_audio_f32_to_i32(0xBF800000u) == INT32_MIN, "-1.0 exact");
    CHECK(zui_audio_f32_to_i32(0x3F000000u) == 1 << 30, "0.5");
    CHECK(zui_audio_f32_to_i32(0x7FC00000u) == 0, "NaN");
    CHECK(zui_audio_f32_to_i32(0x2F800000u) == 0, "2^-32 rounds to even 0");
    CHECK(zui_audio_f32_to_i32(0x30000000u) == 1, "2^-31 is one LSB");
    CHECK(zui_audio_f32_to_i32(0x30400000u) == 2, "2^-31 * 1.5 rounds to 2");
    CHECK(zui_audio_f64_to_i32(0x3E14000000000000ull) == 2, "f64 2^-31 * 2.5 rounds to even 2");
    printf("%s CRC-8/CRC-16/MD5 checks, MD5 known answer, float edges\n", OK());
}

static void test_fuzz(void)
{
    SECTION();
    long trunc = 0, trunc_ok = 0, flips = 0, flip_ok = 0, flac_flips = 0, flac_refused = 0;
    for (size_t i = 0; i < sizeof FX_AUDIO / sizeof FX_AUDIO[0]; i++) {
        const fx_audio_t *f = &FX_AUDIO[i];
        uint32_t step = f->n > 6000 ? 7 : 1; /* every length for small files, every 7th for big */
        for (uint32_t n = 0; n < f->n; n += (n < 256 ? 1 : step)) {
            int32_t *pcm;
            int64_t r = decode_copy(f->d, n, &pcm, 0, ZUI_AUD_F_MD5);
            trunc++;
            if (r >= 0) trunc_ok++;
            free(pcm);
        }
        for (int k = 0; k < 150; k++) {
            uint8_t *c = malloc(f->n);
            memcpy(c, f->d, f->n);
            int nb = 1 + (int) (rnd() % 3);
            for (int j = 0; j < nb; j++) c[rnd() % f->n] ^= (uint8_t) (1u << (rnd() % 8));
            int32_t *pcm;
            int64_t r = decode_copy(c, f->n, &pcm, 0, ZUI_AUD_F_MD5);
            flips++;
            if (r >= 0) flip_ok++;
            if (f->kind == ZUI_AUD_FLAC) {
                flac_flips++;
                if (r < 0) flac_refused++;
            }
            free(pcm);
            free(c);
        }
    }
    /* a truncated FLAC must never decode as complete (STREAMINFO counts frames) */
    for (size_t i = 0; i < sizeof FX_AUDIO / sizeof FX_AUDIO[0]; i++) {
        const fx_audio_t *f = &FX_AUDIO[i];
        if (f->kind != ZUI_AUD_FLAC) continue;
        int32_t *pcm;
        CHECK(decode_copy(f->d, f->n - 1, &pcm, 0, 0) < 0, "%s: cut by one byte decoded", f->name);
        free(pcm);
    }
    CHECK(flac_refused * 100 >= flac_flips * 99, "FLAC flips refused %ld/%ld", flac_refused,
          flac_flips);
    printf("%s fuzz: %ld truncations (%ld decoded as shorter valid files), %ld flipped files "
           "(%ld still valid; FLAC %ld/%ld refused), no crash\n",
           OK(), trunc, trunc_ok, flips, flip_ok, flac_refused, flac_flips);
}

int main(void)
{
    test_fixtures();
    test_partial_and_count();
    test_refusals();
    test_checks();
    test_fuzz();
    printf("%s test_zui_audio: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails != 0;
}
