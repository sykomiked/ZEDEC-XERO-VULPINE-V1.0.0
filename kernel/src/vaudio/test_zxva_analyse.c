/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxva_analyse.c — the integer analyser (zxva_analyse.c) against
 * tools/zxva/wav2zxva.py, and its own guarantees.
 *
 *   cc -std=c11 -Wall -Wextra -Werror -DTEST_HOST -Iinclude -Isrc/modbind \
 *      -Isrc/vaudio -Isrc/media src/vaudio/test_zxva_analyse.c \
 *      src/vaudio/zxva_analyse.c src/vaudio/zxva.c src/media/zui_audio.c \
 *      src/robin_debanks/sha256.c -o /tmp/t && /tmp/t
 *
 * Checks:
 *   - parity: the five fixtures of tools/zxva/gen_analyse_fixtures.py
 *     (wav2zxva.py's output for the same PCM is in the fixture); both files
 *     are measured against the source with zxva_measure (one definition), and
 *     the kernel's must be within the measured margins of the Python one's;
 *   - every output passes zxva_check and renders; META carries the CID, the
 *     depth and the same numbers as the report;
 *   - with the residual, the render at the native rate and depth equals the
 *     input sample for sample (8, 16, 24, 32 bit, mono and stereo), and a FLAC
 *     file decoded by zui_audio.c carries its file CID;
 *   - determinism (same bytes twice, and after the work buffer is dirtied);
 *   - the STFT stage on a known sinusoid (bin, magnitude) and the CPU
 *     backend called directly;
 *   - error paths (EARG, EWORK, EOUT) and a fuzz over random PCM, lengths
 *     from 1 sample, silence, DC, full-scale square waves.
 * Prints one [PASS]/[FAIL] line per section. Exit 0 when all pass. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_audio.h"
#include "zxva.h"
#include "zxva_analyse.h"

#include "test/zxva_analyse_fixtures.inc"
#include "../media/test/fx_audio.inc"

static int g_pass, g_fail, s_bad;
static const char *s_name;

#define SECTION(name) (s_name = (name), s_bad = 0)
#define OK(c, ...)                                                                                 \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            if (s_bad++ < 5) {                                                                     \
                printf("    fail %s:%d: ", s_name, __LINE__);                                      \
                printf(__VA_ARGS__);                                                               \
                printf("\n");                                                                      \
            }                                                                                      \
        }                                                                                          \
    } while (0)

static void end_section(const char *detail)
{
    if (s_bad) {
        printf("[FAIL] %s (%d failures) %s\n", s_name, s_bad, detail ? detail : "");
        g_fail++;
    } else {
        printf("[PASS] %s %s\n", s_name, detail ? detail : "");
        g_pass++;
    }
}

static uint32_t rng_s = 0x12345678u;
static uint32_t rnd(void)
{
    rng_s ^= rng_s << 13;
    rng_s ^= rng_s >> 17;
    rng_s ^= rng_s << 5;
    return rng_s;
}

typedef struct {
    int64_t n;
    uint8_t *out;
    zxva_an_report_t rep;
} res_t;

/* analyse with exact-size buffers; caller frees out */
static res_t run(const zxva_pcm_t *p, const zxva_an_cfg_t *cfg)
{
    res_t r;
    memset(&r, 0, sizeof r);
    uint64_t ws = zxva_analyse_work_size(p, cfg), ob = zxva_analyse_out_bound(p, cfg);
    if (!ws || !ob) {
        r.n = ZXVA_AN_EARG;
        return r;
    }
    void *w = malloc((size_t) ws);
    r.out = malloc((size_t) ob);
    if (!w || !r.out) {
        free(w);
        r.n = ZXVA_AN_EWORK;
        return r;
    }
    r.n = zxva_analyse(p, cfg, w, ws, r.out, ob, &r.rep);
    free(w);
    return r;
}

static int measure(const zxva_pcm_t *p, const uint8_t *d, uint32_t n, zxva_an_report_t *rep)
{
    zxva_an_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    uint64_t ws = zxva_analyse_work_size(p, &cfg);
    void *w = malloc((size_t) ws);
    if (!w) return -1;
    int rc = zxva_measure(p, d, n, w, ws, rep);
    free(w);
    return rc;
}

static zxva_work_t g_w; /* large: keep off the stack */

/* render the whole file at rate/bits; returns frames or < 0; *out malloc'd */
static int64_t render_all(const uint8_t *d, uint32_t n, uint32_t rate, uint32_t bits,
                          uint32_t flags, int32_t **out, uint32_t *nch)
{
    zxva_info_t in;
    if (zxva_check(d, n, &in) != 0) return -1;
    if (zxva_open(&g_w, d, n, rate, bits, flags) != 0) return -2;
    uint64_t F = zxva_frames(&g_w);
    int32_t *o = malloc((size_t) (F ? F : 1) * in.nch * 4);
    if (!o) return -3;
    for (uint64_t f = 0; f < F;) {
        uint32_t k = (uint32_t) (F - f > 4096 ? 4096 : F - f);
        if (zxva_render(&g_w, f, o + f * in.nch, k) != (int32_t) k) {
            free(o);
            return -4;
        }
        f += k;
    }
    *out = o;
    *nch = in.nch;
    return (int64_t) F;
}

static int exact_roundtrip(const zxva_pcm_t *p, const uint8_t *d, uint32_t n, uint32_t rbits)
{
    int32_t *o;
    uint32_t nch;
    int64_t F = render_all(d, n, p->rate, rbits, ZXVA_L_ALL | ZXVA_F_RESIDUAL, &o, &nch);
    if (F < 0) return (int) F;
    int ok = (uint64_t) F >= p->frames && nch == p->nch;
    uint32_t sh = rbits - p->bits;
    for (uint64_t i = 0; ok && i < p->frames * p->nch; i++)
        if (o[i] != (int32_t) ((uint32_t) p->pcm[i] << sh)) ok = 0;
    free(o);
    return ok ? 0 : -10;
}

/* ---- parity with wav2zxva.py ------------------------------------------ */
/* Margins (dB): measured C - Python on these fixtures, then a little slack.
 * The C analyser takes amplitude and phase from the bin rather than a joint
 * refit, so it is allowed to trail by these amounts; it may lead freely.
 * The stereo chord has harmonics closer than the main lobe (3 x 261.63 Hz and
 * 2 x 392 Hz), where the joint refit matters most: measured gap 2.5 dB. */
static const struct {
    const char *name;
    int snr_margin_cdb, band_margin_cdb;
} MARGIN[] = {
    {"piano", 150, 50},   {"vowel24", 150, 50}, {"burst", 150, 50},
    {"clicks", 300, 100}, {"stereo", 300, 50},
};

static void t_parity(void)
{
    SECTION("parity with wav2zxva.py (zxva_measure on both)");
    char det[1024];
    size_t dl = 0;
    det[0] = 0;
    for (size_t i = 0; i < sizeof AN_FX / sizeof AN_FX[0]; i++) {
        const an_fx_t *fx = &AN_FX[i];
        zxva_pcm_t p = {fx->pcm, fx->frames, fx->nch, AN_RATE, fx->bits};
        zxva_an_cfg_t cfg;
        memset(&cfg, 0, sizeof cfg);
        res_t r = run(&p, &cfg);
        OK(r.n > 0, "%s analyse rc %lld", fx->name, (long long) r.n);
        if (r.n <= 0) {
            free(r.out);
            continue;
        }
        zxva_an_report_t mc, mp;
        int rc1 = measure(&p, r.out, (uint32_t) r.n, &mc);
        int rc2 = measure(&p, fx->py, fx->py_len, &mp);
        OK(rc1 == 0 && rc2 == 0, "%s measure %d %d", fx->name, rc1, rc2);
        /* zxva_measure agrees with what the analyser recorded */
        OK(mc.snr_cdb == r.rep.snr_cdb && mc.band_err_cdb == r.rep.band_err_cdb,
           "%s measure %d/%u vs report %d/%u", fx->name, mc.snr_cdb, mc.band_err_cdb, r.rep.snr_cdb,
           r.rep.band_err_cdb);
        OK(mc.snr_cdb >= mp.snr_cdb - MARGIN[i].snr_margin_cdb, "%s snr C %d py %d", fx->name,
           mc.snr_cdb, mp.snr_cdb);
        OK((int) mc.band_err_cdb <= (int) mp.band_err_cdb + MARGIN[i].band_margin_cdb,
           "%s band C %u py %u", fx->name, mc.band_err_cdb, mp.band_err_cdb);
        int w = snprintf(
            det + dl, sizeof det - dl, "%s%s %d.%02d/%d.%02d dB (band %u.%02u/%u.%02u, %lld/%u B)",
            i ? "; " : "\n    ", fx->name, mc.snr_cdb / 100, abs(mc.snr_cdb % 100),
            mp.snr_cdb / 100, abs(mp.snr_cdb % 100), mc.band_err_cdb / 100, mc.band_err_cdb % 100,
            mp.band_err_cdb / 100, mp.band_err_cdb % 100, (long long) r.n, fx->py_len);
        if (w > 0 && (size_t) w < sizeof det - dl) dl += (size_t) w;
        free(r.out);
    }
    end_section(det);
}

/* ---- validity, META, render ------------------------------------------- */
static void t_meta(void)
{
    SECTION("zxva_check, render and META (CID, depth, metrics)");
    const an_fx_t *fx = &AN_FX[1]; /* vowel24 */
    zxva_pcm_t p = {fx->pcm, fx->frames, fx->nch, AN_RATE, fx->bits};
    zxva_an_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    res_t r = run(&p, &cfg);
    OK(r.n > 0, "rc %lld", (long long) r.n);
    if (r.n > 0) {
        zxva_info_t in;
        OK(zxva_check(r.out, (uint32_t) r.n, &in) == 0, "check");
        OK(in.has_meta && in.res_bits == 0, "has_meta %d res_bits %u", in.has_meta,
           (unsigned) in.res_bits);
        zxva_meta_t m;
        OK(zxva_meta(r.out, (uint32_t) r.n, &m) == 0, "meta");
        /* CID of the PCM bytes (int32 LE) when no file CID is given */
        uint8_t *b = malloc(fx->frames * 4);
        for (uint32_t i = 0; i < fx->frames; i++) {
            uint32_t v = (uint32_t) fx->pcm[i];
            b[4 * i] = (uint8_t) v;
            b[4 * i + 1] = (uint8_t) (v >> 8);
            b[4 * i + 2] = (uint8_t) (v >> 16);
            b[4 * i + 3] = (uint8_t) (v >> 24);
        }
        uint8_t cid[ZXVA_CID_LEN];
        zxva_cid(b, fx->frames * 4, cid);
        free(b);
        OK(memcmp(cid, m.cid, ZXVA_CID_LEN) == 0 && cid[0] == 1 && cid[1] == 0x55 &&
               cid[2] == 0x12 && cid[3] == 0x20,
           "cid");
        OK(m.src_rate == AN_RATE && m.src_bits == 24 && m.src_bytes == fx->frames * 4u,
           "src %u %u %llu", m.src_rate, m.src_bits, (unsigned long long) m.src_bytes);
        OK(m.analyser == ZXVA_AN_KERNEL && m.exact == ZXVA_EXACT_NONE, "analyser %u exact %u",
           m.analyser, m.exact);
        OK(m.snr_cdb == r.rep.snr_cdb && m.band_err_cdb == r.rep.band_err_cdb, "metrics %d %llu",
           m.snr_cdb, (unsigned long long) m.band_err_cdb);
        /* renders at other rates and depths (it scales cleanly) */
        static const uint32_t rates[] = {8000, 16000, 44100, 96000};
        for (int k = 0; k < 4; k++) {
            int32_t *o;
            uint32_t nch;
            int64_t F = render_all(r.out, (uint32_t) r.n, rates[k], k & 1 ? 24 : 16, 0, &o, &nch);
            OK(F > 0, "render %u -> %lld", rates[k], (long long) F);
            if (F > 0) {
                uint64_t e = 0;
                for (int64_t i = 0; i < F; i++) e += (uint64_t) ((int64_t) o[i] * o[i] >> 16);
                OK(e > 0, "render %u silent", rates[k]);
                free(o);
            }
        }
        /* SHA-256 known answer through zxva_cid: "abc" */
        static const uint8_t abc[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
                                        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                                        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
                                        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
        zxva_cid((const uint8_t *) "abc", 3, cid);
        OK(memcmp(cid + 4, abc, 32) == 0, "sha256(abc)");
    }
    free(r.out);
    end_section(NULL);
}

/* ---- residual exactness at every depth --------------------------------- */
static void t_exact(void)
{
    SECTION("lossless residual: native render == input (8/16/24/32-bit, stereo)");
    char det[256];
    size_t dl = 0;
    det[0] = 0;
    static const uint32_t depths[] = {8, 16, 24, 32};
    for (int di = 0; di < 4; di++) {
        uint32_t bits = depths[di];
        for (uint32_t nch = 1; nch <= 2; nch++) {
            const an_fx_t *fx = &AN_FX[nch == 2 ? 4 : 0];
            uint32_t F = 3200; /* 0.2 s */
            int32_t *pcm = malloc((size_t) F * nch * 4);
            for (uint32_t i = 0; i < F * nch; i++) {
                int64_t v = fx->nch == nch ? fx->pcm[i] : fx->pcm[i / nch];
                int sh = (int) bits - (int) fx->bits;
                v = sh >= 0 ? v * ((int64_t) 1 << sh) : v / ((int64_t) 1 << -sh);
                v += (int64_t) (rnd() % 3) - 1; /* a little noise below the tones */
                int64_t top = ((int64_t) 1 << (bits - 1)) - 1;
                pcm[i] = (int32_t) (v > top ? top : v < -top - 1 ? -top - 1 : v);
            }
            zxva_pcm_t p = {pcm, F, nch, AN_RATE, bits};
            zxva_an_cfg_t cfg;
            memset(&cfg, 0, sizeof cfg);
            cfg.residual = true;
            res_t r = run(&p, &cfg);
            OK(r.n > 0 && r.rep.exact, "%u-bit %uch rc %lld exact %d", bits, nch, (long long) r.n,
               r.rep.exact);
            if (r.n > 0) {
                uint32_t rb = bits <= 16 ? 16 : bits <= 24 ? 24 : 32;
                OK(r.rep.res_bits == rb, "res_bits %u", r.rep.res_bits);
                OK(exact_roundtrip(&p, r.out, (uint32_t) r.n, rb) == 0, "%u-bit %uch roundtrip",
                   bits, nch);
                zxva_meta_t m;
                OK(zxva_meta(r.out, (uint32_t) r.n, &m) == 0 && m.exact == ZXVA_EXACT_VERIFIED,
                   "meta exact");
                if (nch == 1) {
                    int w = snprintf(det + dl, sizeof det - dl, "%s%u-bit %u B/%u B pcm",
                                     di ? ", " : "", bits, (unsigned) r.n, F * ((bits + 7) / 8));
                    if (w > 0 && (size_t) w < sizeof det - dl) dl += (size_t) w;
                }
            }
            free(r.out);
            free(pcm);
        }
    }
    end_section(det);
}

/* ---- FLAC file -> analyse, with the file's CID ------------------------- */
static void t_flac(void)
{
    SECTION("FLAC via zui_audio -> analyse with the file CID, exact");
    int done = 0;
    for (size_t i = 0; i < sizeof FX_AUDIO / sizeof FX_AUDIO[0] && done < 3; i++) {
        const fx_audio_t *fx = &FX_AUDIO[i];
        if (fx->kind != ZUI_AUD_FLAC || fx->frames < 256 || fx->rate < ZXVA_RATE_MIN) continue;
        zui_audio_info_t in;
        OK(zui_audio_probe(fx->d, fx->n, &in) == 0, "%s probe", fx->name);
        uint32_t ww = zui_audio_work_words(&in);
        int32_t *work = malloc((size_t) (ww ? ww : 1) * 4);
        int32_t *pcm = malloc((size_t) in.frames * in.nch * 4);
        int64_t fr = zui_audio_decode(fx->d, fx->n, pcm, in.frames, work, ww, &in, ZUI_AUD_F_MD5);
        OK(fr == (int64_t) in.frames, "%s decode %lld", fx->name, (long long) fr);
        if (fr > 0) {
            uint8_t cid[ZXVA_CID_LEN];
            zxva_cid(fx->d, fx->n, cid);
            zxva_pcm_t p = {pcm, (uint64_t) fr, in.nch, in.rate, in.bits};
            zxva_an_cfg_t cfg;
            memset(&cfg, 0, sizeof cfg);
            cfg.residual = true;
            cfg.cid = cid;
            cfg.src_bytes = fx->n;
            cfg.src_kind = ZXVA_SRC_FLAC;
            res_t r = run(&p, &cfg);
            OK(r.n > 0 && r.rep.exact, "%s rc %lld", fx->name, (long long) r.n);
            zxva_meta_t m;
            if (r.n > 0) {
                OK(zxva_meta(r.out, (uint32_t) r.n, &m) == 0 &&
                       memcmp(m.cid, cid, ZXVA_CID_LEN) == 0 && m.src_bytes == fx->n &&
                       m.src_kind == ZXVA_SRC_FLAC,
                   "%s meta", fx->name);
                OK(exact_roundtrip(&p, r.out, (uint32_t) r.n, r.rep.res_bits) == 0, "%s roundtrip",
                   fx->name);
            }
            free(r.out);
            done++;
        }
        free(pcm);
        free(work);
    }
    OK(done > 0, "no FLAC fixture used");
    char det[48];
    snprintf(det, sizeof det, "(%d files)", done);
    end_section(det);
}

/* ---- determinism -------------------------------------------------------- */
static void t_determinism(void)
{
    SECTION("determinism (same bytes twice, dirty work buffer)");
    const an_fx_t *fx = &AN_FX[0];
    zxva_pcm_t p = {fx->pcm, fx->frames, fx->nch, AN_RATE, fx->bits};
    zxva_an_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.residual = true;
    uint64_t ws = zxva_analyse_work_size(&p, &cfg), ob = zxva_analyse_out_bound(&p, &cfg);
    uint8_t *w = malloc((size_t) ws), *o1 = malloc((size_t) ob), *o2 = malloc((size_t) ob);
    memset(w, 0, (size_t) ws);
    int64_t n1 = zxva_analyse(&p, &cfg, w, ws, o1, ob, NULL);
    for (uint64_t i = 0; i < ws; i++) w[i] = (uint8_t) rnd();
    int64_t n2 = zxva_analyse(&p, &cfg, w, ws, o2, ob, NULL);
    OK(n1 > 0 && n1 == n2 && memcmp(o1, o2, (size_t) n1) == 0, "n1 %lld n2 %lld", (long long) n1,
       (long long) n2);
    free(w);
    free(o1);
    free(o2);
    end_section(NULL);
}

/* ---- the STFT stage ---------------------------------------------------- */
static void t_stft(void)
{
    SECTION("STFT stage: sinusoid bin and magnitude, CPU backend");
    enum { n = 1024, N = 4096, L = 12, H = N / 2 + 1 };
    static int32_t win[n], tw[N], x[8192], re[2 * H], im[2 * H], scratch[2 * N];
    static uint32_t mag[2 * H], mask[2 * ((H + 31) / 32)], top[2];
    zxva_tables_bh(win, n);
    zxva_tables_tw(tw, N);
    /* f = 37.25 bins of n (149 bins of N), amplitude 0.5 full scale (Q23) */
    for (int i = 0; i < 8192; i++)
        x[i] =
            (int32_t) ((int64_t) zxva_sin_q30((uint64_t) ((uint64_t) i * 149u) << (64 - L)) >> 8);
    zxva_backend_t b;
    zxva_backend_cpu(&b);
    b.scratch = scratch;
    b.scratch_words = 2 * N;
    zxva_stft_plan_t pl = {n, N, L, 1000, 1, 2048, win, tw, x, 8192, 1, 1000};
    int rc = b.stft(&b, &pl, 0, 2, re, im, mag, mask, top);
    OK(rc == 0, "rc %d", rc);
    for (int f = 0; f < 2; f++) {
        uint32_t best = 0, bi = 0, npk = 0;
        for (uint32_t k = 0; k < H; k++)
            if (mag[f * H + k] > best) best = mag[f * H + k], bi = k;
        for (uint32_t k = 0; k < H; k++)
            npk += (mask[f * ((H + 31) / 32) + k / 32] >> (k % 32)) & 1u;
        OK(bi == 149, "frame %d peak bin %u", f, bi);
        OK(top[f] == best, "top %u best %u", top[f], best);
        OK((mask[f * ((H + 31) / 32) + 149 / 32] >> (149 % 32)) & 1u, "149 not masked");
        /* X/N for a sinusoid of amplitude A: A/2 * sum(w)/N; BH sum = 0.35875 n
         * A = 2^22 (Q23 half scale), w Q30 >> 24 => Q29; Q29 * 2^22 / 2 ... */
        int64_t expect =
            (int64_t) ((1 << 22) >> 1) * 64 * 35875 / 100000 * n / N; /* >>24 then Q30 w */
        OK(llabs((int64_t) best - expect) * 100 < expect, "mag %u expect %lld", best,
           (long long) expect);
        OK(npk >= 1 && npk < 8, "peaks %u", npk);
    }
    /* the frame at centre 2048 + 1000 differs from frame 0 only by phase */
    /* (the two frames see the window at different sample phases: < 1e-5) */
    OK(llabs((int64_t) mag[149] - (int64_t) mag[H + 149]) * 100000 <= (int64_t) mag[149],
       "mag drift %u %u", mag[149], mag[H + 149]);
    end_section(NULL);
}

/* ---- error paths ------------------------------------------------------- */
static void t_errors(void)
{
    SECTION("error paths: EARG, EWORK, EOUT");
    const an_fx_t *fx = &AN_FX[0];
    zxva_pcm_t p = {fx->pcm, fx->frames, fx->nch, AN_RATE, fx->bits};
    zxva_an_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    uint64_t ws = zxva_analyse_work_size(&p, &cfg), ob = zxva_analyse_out_bound(&p, &cfg);
    uint8_t *w = malloc((size_t) ws), *o = malloc((size_t) ob);
    zxva_pcm_t bad = p;
    bad.rate = 100;
    OK(zxva_analyse(&bad, &cfg, w, ws, o, ob, NULL) == ZXVA_AN_EARG, "rate");
    bad = p;
    bad.nch = 0;
    OK(zxva_analyse(&bad, &cfg, w, ws, o, ob, NULL) == ZXVA_AN_EARG, "nch");
    bad = p;
    bad.bits = 33;
    OK(zxva_analyse(&bad, &cfg, w, ws, o, ob, NULL) == ZXVA_AN_EARG, "bits");
    bad = p;
    bad.frames = 0;
    OK(zxva_analyse(&bad, &cfg, w, ws, o, ob, NULL) == ZXVA_AN_EARG, "frames");
    OK(zxva_analyse(&p, &cfg, w, ws, NULL, ob, NULL) == ZXVA_AN_EARG, "out NULL");
    /* work_size includes 16 bytes of alignment slack; w is 16-aligned */
    OK(zxva_analyse(&p, &cfg, w, ws - 17, o, ob, NULL) == ZXVA_AN_EWORK, "work - 17");
    OK(zxva_analyse(&p, &cfg, w, ws / 2, o, ob, NULL) == ZXVA_AN_EWORK, "work / 2");
    OK(zxva_analyse(&p, &cfg, NULL, ws, o, ob, NULL) < 0, "work NULL");
    int64_t n = zxva_analyse(&p, &cfg, w, ws, o, ob, NULL);
    OK(n > 0, "full %lld", (long long) n);
    if (n > 0) {
        OK(zxva_analyse(&p, &cfg, w, ws, o, (uint64_t) n - 1, NULL) == ZXVA_AN_EOUT, "out n-1");
        OK(zxva_analyse(&p, &cfg, w, ws, o, 8, NULL) == ZXVA_AN_EOUT, "out 8");
        /* measure refuses a channel-count mismatch and an invalid file */
        zxva_pcm_t st = {AN_FX[4].pcm, AN_FX[4].frames, 2, AN_RATE, 16};
        zxva_an_report_t rr;
        OK(measure(&st, o, (uint32_t) n, &rr) == ZXVA_AN_EARG, "measure nch");
        o[5] ^= 0xFF;
        OK(measure(&p, o, (uint32_t) n, &rr) == ZXVA_AN_EARG, "measure invalid");
    }
    free(w);
    free(o);
    end_section(NULL);
}

/* ---- fuzz: odd signals and lengths --------------------------------------- */
static void t_fuzz(void)
{
    SECTION("fuzz: random PCM, short lengths, silence, DC, square");
    int runs = 0;
    for (int it = 0; it < 48; it++) {
        uint32_t kind = (uint32_t) it % 6;
        uint32_t nch = 1 + rnd() % 3, bits = (uint32_t[]){8, 12, 16, 20, 24, 32}[rnd() % 6];
        uint32_t rate = (uint32_t[]){8000, 11025, 16000, 22050}[rnd() % 4];
        uint32_t F = it < 12 ? (uint32_t) it + 1 : 1 + rnd() % 2400;
        int32_t *pcm = malloc((size_t) F * nch * 4);
        int64_t top = ((int64_t) 1 << (bits - 1)) - 1;
        for (uint32_t i = 0; i < F * nch; i++) {
            int64_t v;
            switch (kind) {
            case 0:
                v = 0;
                break;
            case 1:
                v = top / 3;
                break;
            case 2:
                v = ((i / nch) / 20) & 1 ? top : -top - 1;
                break;
            case 3:
                v = (int64_t) (rnd() % (uint32_t) (2 * top + 1 > 0xFFFFFFFFLL ? 0xFFFFFFFFu
                                                                              : 2 * top + 1)) -
                    top;
                break;
            case 4:
                v = (rnd() & 1) ? top : -top - 1;
                break;
            default:
                v = (i % 97 == 0) ? top : 0;
                break;
            }
            if (v > top) v = top;
            if (v < -top - 1) v = -top - 1;
            pcm[i] = (int32_t) v;
        }
        zxva_pcm_t p = {pcm, F, nch, rate, bits};
        zxva_an_cfg_t cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.residual = (it & 1) != 0;
        cfg.no_grains = it % 7 == 3;
        cfg.no_noise = it % 11 == 5;
        res_t r = run(&p, &cfg);
        OK(r.n > 0, "it %d kind %u F %u nch %u bits %u rate %u rc %lld", it, kind, F, nch, bits,
           rate, (long long) r.n);
        if (r.n > 0) {
            OK(zxva_check(r.out, (uint32_t) r.n, NULL) == 0, "it %d check", it);
            if (cfg.residual) {
                OK(r.rep.exact, "it %d not exact", it);
                OK(exact_roundtrip(&p, r.out, (uint32_t) r.n, r.rep.res_bits) == 0,
                   "it %d roundtrip", it);
            }
            runs++;
        }
        free(r.out);
        free(pcm);
    }
    char det[48];
    snprintf(det, sizeof det, "(%d inputs)", runs);
    end_section(det);
}

int main(void)
{
    t_stft();
    t_parity();
    t_meta();
    t_exact();
    t_flac();
    t_determinism();
    t_errors();
    t_fuzz();
    printf("test_zxva_analyse: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
